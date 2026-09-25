#!/bin/sh
# Cross-builds cockpit for the Nokia N9/N950.  Runs on the build machine.
# Same chain as the probe next door: GCC 14 against the MADDE sysroot, hard
# float, Harmattan's loader.  Needs EGL, GLESv2, X11 and libm from the sysroot.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
XGCC=${XGCC:-/tmp/xgcc-harmattan}
SYSROOT=${SYSROOT:-$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim}
OUT=$HERE/build
CC=$XGCC/bin/arm-none-linux-gnueabi-gcc

[ -x "$CC" ] || { echo "cross compiler missing: $CC" >&2; exit 1; }
mkdir -p "$OUT"
# libXi liegt im Sysroot ohne den ueblichen Symlink, deshalb der volle Pfad -
# die Finger kommen ueber die Eingabeerweiterung von X (xtouch.c).
"$CC" --sysroot="$SYSROOT" -O2 -Wall -std=gnu99 \
    -o "$OUT/cockpit" "$HERE/cockpit.c" "$HERE/fdm.c" "$HERE/blade.c" "$HERE/ton.c" "$HERE/terrain.c" \
    "$HERE/touchinput.c" "$HERE/xtouch.c" \
    -static-libgcc -Wl,--as-needed -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -lEGL -lGLESv2 -lX11 "$SYSROOT/usr/lib/libXi.so.6" \
    "$SYSROOT/usr/lib/libpulse-simple.so.0" "$SYSROOT/usr/lib/libpulse.so.0" \
    -lpthread -lm
"$XGCC/bin/arm-none-linux-gnueabi-strip" "$OUT/cockpit"
ls -la "$OUT/cockpit"
