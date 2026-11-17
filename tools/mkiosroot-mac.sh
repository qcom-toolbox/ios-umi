#!/bin/bash
# Build the SWEET iOS root on macOS: what tools/mkiosroot.sh + tools/sweet-root-tweaks.py do on Linux,
# with hdiutil / newfs_hfs / ditto (macOS reads the IPSW's APFS images natively, decompressing).
#
#   tools/mkiosroot-mac.sh WORKDIR OUT.img [SIZE]        (SIZE default 32g; run from the repo, asks sudo once)
#
# WORKDIR holds the downloads (dl/) and mount points; the IPSW members are fetched one at a time with
# tools/remotezip.py and deleted once copied (the OS image is 4.7 GB, SystemOS 2.7 GB), so the peak disk use
# is about one image plus the root's data. OUT.img is a raw, sparse case-sensitive journaled HFS+ volume
# "iOSRoot" for `fastboot -S 512M flash userdata OUT.img`. Owners are kept (volumes attached with -owners on,
# ditto as root); hard links are kept; files are written uncompressed (--nopreserveHFSCompression).
set -euo pipefail
[[ $# -ge 2 ]] || { sed -n '2,12p' "$0"; exit 2; }
work=$(mkdir -p "$1" && cd "$1" && pwd) out=$2 size=${3:-32g}
repo=$(cd "$(dirname "$0")/.." && pwd)
IPSW='https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77006/1158E502-D76D-4226-AA06-ED5E30CD1111/iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw'
OS=087-86698-044.dmg SYSOS=087-86593-047.dmg APPOS=087-86715-047.dmg
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }

sudo -v
while true; do sudo -n true; sleep 50; kill -0 "$$" 2>/dev/null || exit; done 2>/dev/null &
keep=$!
devs=()
cleanup() {
	kill "$keep" 2>/dev/null || true
	for d in ${devs[@]+"${devs[@]}"}; do [[ -n $d ]] && hdiutil detach -force "$d" >/dev/null 2>&1 || true; done
}
trap cleanup EXIT

step() { echo; echo "=== $(date +%H:%M:%S) $*"; }

# attach IMAGE [raw]: attach read-only with owners honoured, print the mount point (last field of the line
# that has one) and remember the device
attach_ro() {
	local key=()
	[[ ${2:-} == raw ]] && key=(-imagekey diskimage-class=CRawDiskImage)
	local o
	o=$(hdiutil attach -readonly -noverify -owners on -nobrowse ${key[@]+"${key[@]}"} -mountrandom "$work/mnt" "$1")
	devs+=("$(echo "$o" | awk '/^\/dev\//{print $1; exit}')")
	echo "$o" | awk -F'\t' 'NF>=3 && $NF ~ /\// {m=$NF} END{gsub(/^ +| +$/,"",m); print m}'
}
# a mount point under WORKDIR/mnt, or stop (an empty path would make the checks look at the Mac's own /System)
need_mnt() { [[ -n $1 && $1 == "$work/mnt/"* && -d $1 ]] || { echo "image did not mount (got '$1')" >&2; exit 1; }; }

fetch() {
	[[ -f $work/dl/$1 ]] || python3 -I "$repo/tools/remotezip.py" "$IPSW" "$1" "$work/dl"
	rm -f "$work/dl/$1".mtree "$work/dl/$1".root_hash "$work/dl/$1".trustcache "$work/dl"/Firmware/"$1".* 2>/dev/null || true
}

mkdir -p "$work/dl" "$work/mnt" "$work/root"

step "raw sparse image $out ($size), case-sensitive journaled HFS+ iOSRoot"
mkfile -n "$size" "$out"
dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$out" | awk '/^\/dev\//{print $1; exit}')
newfs_hfs -s -J -v iOSRoot "$dev"
hdiutil detach "$dev" >/dev/null
hdiutil attach -imagekey diskimage-class=CRawDiskImage -owners on -nobrowse -mountpoint "$work/root" "$out" >/dev/null
devs+=("$work/root")
R=$work/root
sudo mkdir -p "$R/.fseventsd" && sudo touch "$R/.fseventsd/no_log" "$R/.metadata_never_index"
sudo mdutil -i off "$R" >/dev/null 2>&1 || true

step "OS: $OS"
fetch "$OS"
m=$(attach_ro "$work/dl/$OS")
echo "mounted at $m"; need_mnt "$m"; [[ -d $m/System/Library/xpc ]] || { echo "the OS image did not mount as expected" >&2; exit 1; }
sudo ditto --nopreserveHFSCompression "$m" "$R"
hdiutil detach "$m" >/dev/null; rm -f "$work/dl/$OS"

step "SystemOS cryptex: $SYSOS"
fetch "$SYSOS"
m=$(attach_ro "$work/dl/$SYSOS" raw)
echo "mounted at $m"; need_mnt "$m"; [[ -d $m/System/Library/Caches/com.apple.dyld ]] || { echo "SystemOS did not mount as expected" >&2; exit 1; }
sudo mkdir -p "$R/private/preboot/Cryptexes/OS"
sudo ditto --nopreserveHFSCompression "$m" "$R/private/preboot/Cryptexes/OS"
hdiutil detach "$m" >/dev/null; rm -f "$work/dl/$SYSOS"

step "AppOS cryptex: $APPOS"
fetch "$APPOS"
m=$(attach_ro "$work/dl/$APPOS" raw)
echo "mounted at $m"; need_mnt "$m"
sudo mkdir -p "$R/private/preboot/Cryptexes/App"
sudo ditto --nopreserveHFSCompression "$m" "$R/private/preboot/Cryptexes/App"
hdiutil detach "$m" >/dev/null; rm -f "$work/dl/$APPOS"

# no macOS volume junk inside the cryptexes
sudo rm -rf "$R"/private/preboot/Cryptexes/{OS,App}/{.fseventsd,.Spotlight-V100,.Trashes,.TemporaryItems} 2>/dev/null || true

step "shared cache hard links where dyld looks at ignition level 1"
dsc=private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
sudo mkdir -p "$R/System/Library/Caches/com.apple.dyld"
for f in "$R/$dsc"/*; do sudo ln -f "$f" "$R/System/Library/Caches/com.apple.dyld/${f##*/}"; done

step "cryptex files with no counterpart in the OS root: hard-linked to their root path"
for c in OS App; do
	(cd "$R/private/preboot/Cryptexes/$c" && sudo find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
	while IFS= read -r f; do
		f=${f#./}
		if ! sudo test -e "$R/$f" && ! sudo test -L "$R/$f"; then
			sudo mkdir -p "$R/$(dirname "$f")"
			src="$R/private/preboot/Cryptexes/$c/$f"
			if sudo test -L "$src"; then sudo cp -P "$src" "$R/$f"; else sudo ln "$src" "$R/$f"; fi
			echo "linked $c:/$f"
		fi
	done
done

step "logd stores, system keybag"
db=$R/private/var/db
sudo install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
sudo install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do sudo install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"; done
sudo install -d -m 0700 -o 0 -g 0 "$R/private/var/keybags"
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' >"$work/systembag.kb"
sudo install -m 0600 -o 0 -g 0 "$work/systembag.kb" "$R/private/var/keybags/systembag.kb"

step "SWEET tweaks (tools/sweet-root-tweaks.py)"
sudo python3 -I "$repo/tools/sweet-root-tweaks.py" "$R"

step "clean up and check"
sudo rm -rf "$R"/{.Spotlight-V100,.Trashes,.TemporaryItems,.metadata_never_index,.fseventsd}
df -h "$R"
hdiutil detach "$R" >/dev/null
devs=("${devs[@]/$R}")
dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$out" | awk '/^\/dev\//{print $1; exit}')
sudo fsck_hfs -f -y "$dev" || true
sudo fsck_hfs -f -n "$dev"
hdiutil detach "$dev" >/dev/null
ls -ls "$out"
step "done: fastboot -S 512M flash userdata $out"
