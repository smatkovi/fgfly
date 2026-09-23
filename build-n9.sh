#!/bin/sh
# Cross-builds sgxprobe for the Nokia N9/N950 (Harmattan).  Runs on the build
# machine, inside this tree.  Same toolchain as the Snapszer MeeGo edition:
# GCC 14 for arm-none-linux-gnueabi against the MADDE sysroot, hard float, but
# with Harmattan's loader name - the device has /lib/ld-linux.so.3, not the
# armhf one GCC would ask for.
#
# EGL and GLESv2 come from the sysroot, so this links against the same PowerVR
# SGX530 stack the device runs.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
XGCC=${XGCC:-/tmp/xgcc-harmattan}
SYSROOT=${SYSROOT:-$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim}
OUT=$HERE/build/n9
CC=$XGCC/bin/arm-none-linux-gnueabi-gcc

[ -x "$CC" ] || { echo "cross compiler missing: $CC" >&2; exit 1; }
[ -d "$SYSROOT" ] || { echo "sysroot missing: $SYSROOT" >&2; exit 1; }
# The slim MADDE sysroot may carry the libraries without the development
# headers or the .so symlinks - it was assembled for Qt, not for GL.  In that
# case build against our own minimal declarations and name the libraries by
# their soname path, exactly as the Jolla build does.
# The SGX530 has no ES2 config that can draw into a pbuffer, so the probe takes
# an X pixmap instead - that needs libX11, which Harmattan has.
HEADERS="-DSGXPROBE_X11"
LIBS="-lEGL -lGLESv2 -lX11"
if [ ! -f "$SYSROOT/usr/include/EGL/egl.h" ] || [ ! -f "$SYSROOT/usr/include/GLES2/gl2.h" ]; then
    echo "== sysroot without GL headers, using compat/gles_min.h"
    HEADERS="$HEADERS -DSGXPROBE_MIN_HEADERS -I$HERE"
fi
if [ ! -e "$SYSROOT/usr/lib/libEGL.so" ] || [ ! -e "$SYSROOT/usr/lib/libGLESv2.so" ]; then
    for so in "$SYSROOT/usr/lib/libEGL.so".* "$SYSROOT/usr/lib/libGLESv2.so".*; do
        [ -e "$so" ] || { echo "no libEGL/libGLESv2 in $SYSROOT/usr/lib" >&2; exit 1; }
    done
    LIBS="$(ls "$SYSROOT/usr/lib/libEGL.so".* | head -1) $(ls "$SYSROOT/usr/lib/libGLESv2.so".* | head -1) -lX11"
fi

mkdir -p "$OUT"
# shellcheck disable=SC2086
"$CC" --sysroot="$SYSROOT" -O2 -Wall -std=gnu99 $HEADERS \
    -o "$OUT/sgxprobe" "$HERE/sgxprobe.c" \
    -static-libgcc -Wl,--as-needed -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    $LIBS
"$XGCC/bin/arm-none-linux-gnueabi-strip" "$OUT/sgxprobe"
ls -la "$OUT/sgxprobe"
file "$OUT/sgxprobe" 2>/dev/null || true
