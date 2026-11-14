#!/bin/bash
# phone/build-heic-trace.sh: build sweet-heic-trace (decode one image, say which step fails) and
# libsweet-heic-trace.dylib (call trace, inserted with DYLD_INSERT_LIBRARIES); "install" puts them on the
# phone and reads them back. The program is installed as /usr/local/bin/sweet-heic-probe because the
# phone's libsweet-sb runs that path once when /var/tmp/sweet-heic-probe.run exists (log:
# /var/tmp/sweet-heic-probe.log).
set -eu
cd "$(dirname "$0")/.."
sdk=$(xcrun --sdk iphoneos --show-sdk-path)
cc="xcrun --sdk iphoneos clang -arch arm64 -isysroot $sdk -miphoneos-version-min=16.0 -O2 -Wall -Wextra -Werror"
mkdir -p phone/out
$cc -dynamiclib -install_name /usr/local/lib/libsweet-heic-trace.dylib \
    -framework CoreFoundation -framework CoreVideo -framework CoreMedia -framework VideoToolbox -framework IOSurface \
    phone/heic-trace/IOSurfaceAccelerator.tbd -o phone/out/libsweet-heic-trace.dylib phone/heic-trace/sweet_heic_trace.c \
    phone/heic-trace/sweet_heic_trace_gen.c phone/heic-trace/sweet_heic_trace_cv.c
$cc -framework CoreFoundation -framework CoreGraphics -framework ImageIO -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework IOSurface \
    -o phone/out/sweet-heic-trace phone/heic-trace/sweet_heic_trace_main.c phone/heic-trace/sweet_heic_wrapcm.c
$cc -dynamiclib -install_name /usr/local/lib/libsweet-heic-mstest.dylib \
    -framework CoreFoundation -framework CoreMedia -framework VideoToolbox \
    -o phone/out/libsweet-heic-mstest.dylib phone/heic-trace/sweet_heic_mstest.c
/opt/local/bin/ldid -S phone/out/libsweet-heic-mstest.dylib
$cc -dynamiclib -install_name /usr/local/lib/libsweet-exc.dylib -framework CoreFoundation -lobjc \
    -o phone/out/libsweet-exc.dylib phone/heic-trace/sweet_exc_log.c
/opt/local/bin/ldid -S phone/out/libsweet-exc.dylib
/opt/local/bin/ldid -S phone/out/libsweet-heic-trace.dylib
/opt/local/bin/ldid -S phone/out/sweet-heic-trace
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$sdk" -miphoneos-version-min=16.0 -O2 -Wall -Werror -fobjc-arc \
    -framework Foundation -framework OSLog -o phone/out/sweet-oslog-dump phone/heic-trace/sweet_oslog_dump.m
/opt/local/bin/ldid -Sphone/heic-trace/oslog.entitlements phone/out/sweet-oslog-dump
echo "built phone/out/sweet-heic-trace + libsweet-heic-trace.dylib"
if [ "${1:-}" = install ]; then
	put() {
		tools/umi-files.py rm "$2" >/dev/null 2>&1 || true
		tools/umi-files.py put "$1" "$2" "$3"
		tools/umi-files.py get "$2" "$1.readback" >/dev/null
		cmp "$1" "$1.readback"
	}
	put phone/out/libsweet-heic-trace.dylib /usr/local/lib/libsweet-heic-trace.dylib 755
	put phone/out/libsweet-heic-mstest.dylib /usr/local/lib/libsweet-heic-mstest.dylib 755
	put phone/out/libsweet-exc.dylib /usr/local/lib/libsweet-exc.dylib 755
	put phone/out/sweet-oslog-dump /usr/local/bin/sweet-oslog-dump 755
	put phone/out/sweet-heic-trace /usr/local/bin/sweet-heic-probe 755
	echo "installed and read back"
fi
