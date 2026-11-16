#!/bin/bash
# Cross-build the stock wolfTPM examples for Linux on the PolarFire SoC U54
# harts, talking to the hart-4 fwTPM through tpm_io_mpfs_mbox.c. Builds static
# wolfSSL and wolfTPM from copies of the given source trees with the Yocto
# riscv64 cross compiler, then statically links each example with the mailbox
# HAL ahead of libwolftpm.a.
#
# Usage: build_wolftpm_client.sh
#   WOLFSSL_DIR  wolfSSL source tree          (default ../../../../../../wolfssl)
#   WOLFTPM_DIR  wolfTPM source tree          (default ../../../../../../wolftpm)
#   YOCTO_TMP    Yocto build tmp directory    (default ~/yocto-dev-polarfire/build/tmp-glibc)
#   SYSROOT      riscv64 sysroot with glibc   (default: wolfssl recipe-sysroot in YOCTO_TMP)
#   OUT          output directory             (default ./build-riscv64)
# Binaries land in $OUT/bin.

set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WOLFSSL_DIR=$(cd "${WOLFSSL_DIR:-$HERE/../../../../../../wolfssl}" && pwd)
WOLFTPM_DIR=$(cd "${WOLFTPM_DIR:-$HERE/../../../../../../wolftpm}" && pwd)
YOCTO_TMP=$(realpath -m "${YOCTO_TMP:-$HOME/yocto-dev-polarfire/build/tmp-glibc}")
OUT=$(realpath -m "${OUT:-$HERE/build-riscv64}")
JOBS=${JOBS:-$(nproc)}
# OUT is rebuilt with rm -rf, so it must not overlap a source tree
for src in "$WOLFSSL_DIR" "$WOLFTPM_DIR"; do
    case "$OUT/" in "$src/"*) echo "OUT must not be inside $src"; exit 1 ;; esac
    case "$src/" in "$OUT/"*) echo "OUT must not contain $src"; exit 1 ;; esac
done

XC=$YOCTO_TMP/sysroots-components/x86_64
GCC=$XC/gcc-cross-riscv64/usr/bin/riscv64-mchp-linux/riscv64-mchp-linux-gcc
BINUTILS=$XC/binutils-cross-riscv64/usr/bin/riscv64-mchp-linux
if [ -z "$SYSROOT" ]; then
    SYSROOT=$(ls -d "$YOCTO_TMP"/work/riscv64-mchp-linux/wolfssl/*/recipe-sysroot 2>/dev/null | head -1)
fi
[ -z "$SYSROOT" ] || SYSROOT=$(realpath -m "$SYSROOT")
[ -x "$GCC" ] || { echo "riscv64 cross gcc not found under $XC"; exit 1; }
if [ -z "$SYSROOT" ] || [ ! -f "$SYSROOT/usr/lib/libc.a" ]; then
    echo "no riscv64 sysroot with static glibc (SYSROOT=$SYSROOT)"; exit 1
fi

# The Yocto cross gcc looks for unprefixed as/ld; give it a -B directory
mkdir -p "$OUT/xbin" "$OUT/bin"
for t in as ld ld.bfd ar ranlib nm strip; do
    ln -sf "$BINUTILS/riscv64-mchp-linux-$t" "$OUT/xbin/$t"
done
CC="$GCC -B$OUT/xbin/ --sysroot=$SYSROOT"
AR=$BINUTILS/riscv64-mchp-linux-ar
RANLIB=$BINUTILS/riscv64-mchp-linux-ranlib
STRIP=$BINUTILS/riscv64-mchp-linux-strip
PREFIX=$OUT/prefix
HOST=riscv64-unknown-linux-gnu

echo "== wolfSSL ($WOLFSSL_DIR)"
rm -rf "$OUT/wolfssl"
rsync -a --exclude .git "$WOLFSSL_DIR/" "$OUT/wolfssl/"
cd "$OUT/wolfssl"
[ -x configure ] || ./autogen.sh > /dev/null
make distclean > /dev/null 2>&1 || true
./configure --host=$HOST CC="$CC" AR="$AR" RANLIB="$RANLIB" \
    --prefix="$PREFIX" --enable-static --disable-shared \
    --enable-wolftpm --enable-keygen --disable-examples --disable-crypttests \
    > "$OUT/wolfssl-configure.log"
make -j"$JOBS" > "$OUT/wolfssl-make.log"
make install > /dev/null

echo "== wolfTPM ($WOLFTPM_DIR)"
rm -rf "$OUT/wolftpm"
rsync -a --exclude .git "$WOLFTPM_DIR/" "$OUT/wolftpm/"
cd "$OUT/wolftpm"
[ -x configure ] || ./autogen.sh > /dev/null
make distclean > /dev/null 2>&1 || true
./configure --host=$HOST CC="$CC" AR="$AR" RANLIB="$RANLIB" \
    --prefix="$PREFIX" --enable-static --disable-shared \
    --with-wolfcrypt="$PREFIX" --enable-advio --disable-autodetect \
    --disable-swtpm --disable-fwtpm --disable-examples \
    > "$OUT/wolftpm-configure.log"
make -j"$JOBS" src/libwolftpm.la > "$OUT/wolftpm-make.log"

echo "== examples"
# Same feature defines as the library (WOLFTPM_ADV_IO, WOLFTPM_EXAMPLE_HAL...)
TPM_DEFS=$(sed -n 's/^AM_CFLAGS = //p' "$OUT/wolftpm/Makefile")
case "$TPM_DEFS" in
    *-DWOLFTPM_ADV_IO*) ;;
    *) echo "could not read the wolfTPM feature defines from its Makefile"; exit 1 ;;
esac
CFLAGS="$TPM_DEFS -DHAVE_CONFIG_H -I$OUT/wolftpm/src -I$OUT/wolftpm -I$PREFIX/include"
LIBS="$OUT/wolftpm/src/.libs/libwolftpm.a $PREFIX/lib/libwolfssl.a -lm -lpthread"
$CC $CFLAGS -c "$HERE/tpm_io_mpfs_mbox.c" -o "$OUT/tpm_io_mpfs_mbox.o"
$CC $CFLAGS -c "$OUT/wolftpm/examples/tpm_test_keys.c" -o "$OUT/tpm_test_keys.o"
build_example() {
    name=$1; src=$2; extra=$3
    $CC $CFLAGS -static "$OUT/wolftpm/examples/$src" $extra \
        "$OUT/tpm_io_mpfs_mbox.o" $LIBS -o "$OUT/bin/$name"
    $STRIP "$OUT/bin/$name"
    echo "   $OUT/bin/$name"
}
build_example caps      wrap/caps.c
build_example getrandom wrap/getrandom.c
build_example extend    pcr/extend.c
build_example read_pcr  pcr/read_pcr.c
build_example keygen    keygen/keygen.c "$OUT/tpm_test_keys.o"
build_example quote     pcr/quote.c     "$OUT/tpm_test_keys.o"
