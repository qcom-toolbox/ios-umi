# Hand-off: HEIC / HEVC image decoding on umi (2026-11-14)

Task: on the iOS 16 port for the Xiaomi Mi 10 (umi, presented as iPhone X `iPhone10,3` / `D22AP`, iOS 16.7.16 20H392),
make HEIC images decode, so wallpapers, Photos, thumbnails, etc. that are stored as HEIC draw like on a real iPhone.
Done = an unmodified HEIC asset (e.g. the iOS 16 WWDC 2022 wallpaper with its original `main.caml.sweet-orig`)
shows on screen, and the user confirms it.

## Read first
- `ios-sweet/NOTES.md`, umi section, newest entries: "2026-11-14 16:30: the iOS 16 wallpaper is plain blue because
  no HEIC image decodes" and "16:10: wallpaper choice survives reboots".
- Kernel tree `xnu-work/bt/xnu` (build: `ios-sweet/tools/build-kernel-local.sh`, whole tree, never git stash).
  Boot image now: `shim/out/umi-0298-files-spaces.img`. New images must keep 0298's boot line exactly (extras saved
  by the wallpaper session: the line after `sweetfb_direct=1` in `strings -n 100 shim/out/umi-0298-files-spaces.img`);
  the line is at 599 of 607 chars, so there is barely room for a new boot-arg. `umi-boot-image.sh` needs
  `PATH=/Users/enzo/ios-sweet-port/toolbin/bin:$PATH` (aarch64-linux-gnu-gcc, mkbootimg).
- Boot only with `fastboot boot` when the user says the phone is in fastboot; never flash.

## What is known
- The WWDC 2022 poster is a CAGradientLayer + 5 `<contents type="CGImage" src="assets/X.HEIC"/>` layers. On the
  phone only the gradient drew (flat blue). PNG copies of the same layers (made with `sips` on the Mac) draw fine
  (user-verified) -> CoreAnimation, the poster pipeline and PNG decoding work; HEIC decoding is what fails.
- SpringBoard's IOKit trace (`IOK SpringBoard: matching1 ...`) looks up `AppleVXD375`, `AppleVXD390`, `AppleVXD393`
  (Apple's hardware video decoder) -> none. No AppleAVD/VXD driver exists in our kernel.
- Real A8 iPhones (no HEVC hardware) on iOS 11+ still view HEIC: there is a software path somewhere
  (VideoToolbox software HEVC decoder and/or CMPhoto/ImageIO). The D22 (A11) build may simply never select it
  because MobileGestalt/IOKit say the hardware is there, or it fails when IOService lookups fail.
- Photos won't open: `assetsd` aborts in a loop (sig 6, uncaught exception: `abort <- demangling_terminate_handler
  <- _objc_terminate <- std::terminate <- _dispatch_client_callout`; reason not logged). May or may not be HEIC
  related (separate investigation; check if it is the same root).

## Leads / plan
1. Find the decode path in the shared cache (`ios-sweet/research/dsc/20H392__iPhone10,3_6`, `ipsw dsc disass /
   a2s / str ... --cache dyld_shared_cache_arm64.a2s`): ImageIO's HEIF plugin -> CMPhoto
   (`CMPhotoDecompressionSession...`) -> VideoToolbox (`VTDecompressionSessionCreate`, HEVC decoder selection,
   `com.apple.videotoolbox.videodecoder.hevc`/software vs `AppleAVD`/`AppleVXD` hardware). Look for the decision
   that requires hardware (MobileGestalt keys like HEVC decode capability, `IOServiceMatching("AppleVXD...")`,
   `VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)`).
2. Reproduce small: a tiny test binary on the phone (see `phone/` build scripts, ad-hoc `ldid`, `-miphoneos-version-min=16.0`)
   that calls `CGImageSourceCreateWithURL` + `CGImageSourceCreateImageAtIndex` on a HEIC and logs the error/status
   (stderr reaches the log via the /dev/null tee for named processes, or write to /var/tmp and fetch it).
3. Options for the fix, in order of preference: make the stack choose its own software decoder (correct
   capability answer from the kernel/IOKit or MobileGestalt); provide a decoder service that the stack accepts;
   last resort a userland shim via `kern.exec_inject` (libsweet-sb shows how) - check the project rule against
   patching Apple binaries before going that way.
4. When it works, restore the original `main.caml` (from `main.caml.sweet-orig`) in the iOS 16 wallpaper to prove it,
   and record everything in NOTES.md and Status.md (Wallpaper row is ⚠️ now).

## Tools and rules
- Phone files: `tools/umi-files.py` (kernel file server 192.168.7.2:2223). Kernel 0298+: paths may contain spaces
  (sent %-escaped), PUT cuts the file to the new size. No RMDIR. Ask the user before writing any phone file; keep
  `*.sweet-orig` backups of Apple files; read every write back.
- After each boot the Mac loses 192.168.7.1: the user runs `tools/umi-usbnet.sh` (sudo). The USB link may die a few
  minutes into a boot; only a reboot helps.
- Live log: UDP 5140 is already received by another session into
  `/private/tmp/claude-501/-Users-enzo-ios-sweet-port/3034b72e-0815-4d9a-a572-682517c59cf0/scratchpad/umi-0286-live.log`
  (read it, don't bind the port). Crash lines `CRASH name ... slide ...`: symbolicate `addr - slide` with `ipsw dyld a2s`.
- Shared folders: commit only your own files, never stash/reset/switch branches. Framebuffer/GPU files belong to the
  gpu-stability session; `sweet_wl*.c` / `redwood_*.c` to the Wi-Fi session. Commit + push to `umi-scanout` and
  fast-forward `main` after a tested change. The user answers in short messages; ask one concrete question at a time.
