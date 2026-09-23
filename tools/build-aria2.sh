#!/bin/sh
# Baut aria2c fuer Harmattan: erst OpenSSL 1.1.1 (die Spiegel von TerraSync
# sprechen HTTPS, und das OpenSSL des Geraets ist 0.9.8), dann aria2 ohne
# BitTorrent und Metalink - das spart libxml2, gmp und nettle.
set -e
XGCC=/tmp/xgcc-harmattan
SYSROOT=$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim
WORK=/tmp/aria2-build
PREFIX=$WORK/prefix
CROSS=$XGCC/bin/arm-none-linux-gnueabi-
JOBS=$(nproc)

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f "$PREFIX/lib/libssl.a" ]; then
    [ -f openssl-1.1.1w.tar.gz ] || curl -fsSLO https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz
    rm -rf openssl-1.1.1w && tar xf openssl-1.1.1w.tar.gz
    cd openssl-1.1.1w
    ./Configure linux-armv4 no-shared no-asm no-tests no-unit-test \
        --prefix="$PREFIX" --cross-compile-prefix="$CROSS" \
        --sysroot="$SYSROOT" -march=armv7-a -mfloat-abi=hard -O2
    make -j"$JOBS" >/dev/null
    make install_sw >/dev/null
    cd "$WORK"
    echo "== OpenSSL fertig"
fi

[ -f aria2-1.37.0.tar.xz ] || curl -fsSLO https://github.com/aria2/aria2/releases/download/release-1.37.0/aria2-1.37.0.tar.xz
rm -rf aria2-1.37.0 && tar xf aria2-1.37.0.tar.xz
cd aria2-1.37.0

# Harmattan behaelt den alten Ladernamen, und libstdc++ kommt statisch mit,
# damit das Geraet seine eigene (GCC 4.4) behalten darf.
export CC="${CROSS}gcc --sysroot=$SYSROOT"
export CXX="${CROSS}g++ --sysroot=$SYSROOT"
export CPPFLAGS="-I$PREFIX/include"
export LDFLAGS="-L$PREFIX/lib -static-libstdc++ -static-libgcc -Wl,--dynamic-linker=/lib/ld-linux.so.3"
export OPENSSL_CFLAGS="-I$PREFIX/include"
export OPENSSL_LIBS="-L$PREFIX/lib -lssl -lcrypto -ldl"

./configure --host=arm-none-linux-gnueabi --build=x86_64-pc-linux-gnu \
    --disable-bittorrent --disable-metalink --disable-nls \
    --without-libxml2 --without-libexpat --without-sqlite3 --without-libcares \
    --without-libssh2 --without-gnutls --without-libnettle --without-libgmp \
    --without-libgcrypt --with-openssl \
    ARIA2_STATIC=yes >configure.log 2>&1 || { tail -25 configure.log; exit 1; }

make -j"$JOBS" >build.log 2>&1 || { tail -30 build.log; exit 1; }
"${CROSS}strip" src/aria2c
ls -la src/aria2c
file src/aria2c
