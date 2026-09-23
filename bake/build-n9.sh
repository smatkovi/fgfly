#!/bin/sh
# Cross-builds btgbake for the Nokia N9/N950.  Runs on the build machine.
# Braucht nur zlib aus dem Sysroot - der Backofen kennt weder X noch GL.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
XGCC=${XGCC:-/tmp/xgcc-harmattan}
SYSROOT=${SYSROOT:-$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim}
OUT=$HERE/build
CC=$XGCC/bin/arm-none-linux-gnueabi-gcc

[ -x "$CC" ] || { echo "cross compiler missing: $CC" >&2; exit 1; }
mkdir -p "$OUT"
"$CC" --sysroot="$SYSROOT" -O2 -Wall -std=gnu99 \
    -o "$OUT/btgbake" "$HERE/btgbake.c" \
    -static-libgcc -Wl,--as-needed -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -lz -lm -lrt          # clock_gettime liegt bei glibc 2.10 noch in librt
"$XGCC/bin/arm-none-linux-gnueabi-strip" "$OUT/btgbake"
ls -la "$OUT/btgbake"
