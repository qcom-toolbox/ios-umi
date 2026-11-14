#!/bin/bash
# phone/build-sweet-sb.sh: build libsweet-sb.dylib (phone/sweet-sb/sweet_sb_inject.c: the status bar's clock moved
# right of the Mi 10 camera hole) for the phone (iPhoneOS SDK, arm64, ad-hoc signed with ldid) and, with "install",
# put it on the phone as /usr/local/lib/libsweet-sb.dylib through tools/umi-files.py (read back and compared).
# The kernel (0297) inserts it into every program when that file exists (boot-arg sweetsb=0: never).
set -eu
cd "$(dirname "$0")/.."
sdk=$(xcrun --sdk iphoneos --show-sdk-path)
mkdir -p phone/out
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$sdk" -miphoneos-version-min=16.0 \
    -O2 -Wall -Wextra -Werror -dynamiclib -install_name /usr/local/lib/libsweet-sb.dylib -lobjc \
    -o phone/out/libsweet-sb.dylib phone/sweet-sb/sweet_sb_inject.c
/opt/local/bin/ldid -S phone/out/libsweet-sb.dylib
echo "built phone/out/libsweet-sb.dylib"
if [ "${1:-}" = install ]; then
	tools/umi-files.py mkdir /usr/local/lib 755 || true
	tools/umi-files.py rm /usr/local/lib/libsweet-sb.dylib || true
	tools/umi-files.py put phone/out/libsweet-sb.dylib /usr/local/lib/libsweet-sb.dylib 755
	tools/umi-files.py get /usr/local/lib/libsweet-sb.dylib phone/out/libsweet-sb.readback
	cmp phone/out/libsweet-sb.dylib phone/out/libsweet-sb.readback
	echo "installed and read back: /usr/local/lib/libsweet-sb.dylib"
fi
