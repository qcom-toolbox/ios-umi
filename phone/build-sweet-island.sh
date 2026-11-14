#!/bin/bash
# Local build only; install separately after preserving the current phone libraries.
set -eu
cd "$(dirname "$0")/.."
sdk=$(xcrun --sdk iphoneos --show-sdk-path)
mkdir -p phone/out
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$sdk" -miphoneos-version-min=16.0 \
    -O2 -Wall -Wextra -Werror -dynamiclib -framework UIKit -framework Foundation -framework CoreGraphics \
    -install_name /usr/local/lib/libsweet-island.dylib \
    -o phone/out/libsweet-island.dylib phone/sweet-island/sweet_island.m
/opt/local/bin/ldid -S phone/out/libsweet-island.dylib
