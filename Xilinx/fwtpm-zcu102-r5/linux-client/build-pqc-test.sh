#!/bin/bash
# build-pqc-test.sh -- cross-build fwtpm_pqc_test for aarch64 against
# sibling wolfssl + wolftpm checkouts. Produces a fully-static binary
# you can scp to /tmp on the ZCU102 PetaLinux rootfs (no need to add
# wolfssl/wolftpm recipes to the rootfs for a one-shot test).
#
# Why a custom configure: wolftpm 3.10's `--enable-pqc` combined with
# `--enable-fwtpm` forces WOLFTPM_FWTPM_HAL + WOLFTPM_ADV_IO, which
# switches TPM2HalIoCb to a 6-arg TIS-style signature incompatible
# with our simple rpmsg send/receive cb in fwtpm_pqc_test.c. The
# right combo for an rpmsg client is:
#
#   wolfssl  : --enable-mlkem --enable-dilithium --enable-experimental
#              --enable-keygen --enable-pkcallbacks --enable-sha3
#              --enable-shake256 + crypto-only ish (TLS staying enabled
#              keeps the wolfSSL_*Cb symbols wolftpm references).
#   wolftpm  : --enable-pqc --disable-fwtpm --disable-swtpm
#              --disable-devtpm --disable-winapi --disable-autodetect
#
# That combo leaves WOLFTPM_V185 on (PQC wrappers) and the simple
# 5-arg TPM2HalIoCb typedef active.
#
# EXPERIMENTAL: requires the firmware built with FWTPM_ENABLE_PQC, and
# will not complete over the current single-frame rpmsg transport (no
# fragmentation layer yet -- see ../README.md). Kept for that follow-on.
#
# Usage:
#   ./build-pqc-test.sh
#   # The board image ships no SSH and no sudo. Copy the binary onto the
#   # SD card rootfs during deploy, then run it as root on the console:
#   #   /tmp/fwtpm_pqc_test
#
# Copyright (C) 2006-2026 wolfSSL Inc.

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
WOLFSSL_SRC=${WOLFSSL_SRC:-$ROOT/wolfssl}
WOLFTPM_SRC=${WOLFTPM_SRC:-$ROOT/wolftpm}
BUILD=${BUILD:-/tmp/aarch64-build}
CROSS=${CROSS:-aarch64-linux-gnu}
OUT=${OUT:-/tmp/fwtpm_pqc_test}

[[ -d "$WOLFSSL_SRC" ]] || { echo "wolfssl not found at $WOLFSSL_SRC" >&2; exit 1; }
[[ -d "$WOLFTPM_SRC" ]] || { echo "wolftpm not found at $WOLFTPM_SRC" >&2; exit 1; }
command -v ${CROSS}-gcc >/dev/null || { echo "no ${CROSS}-gcc on PATH" >&2; exit 1; }

mkdir -p "$BUILD"

if [[ ! -f "$BUILD/wolfssl/src/.libs/libwolfssl.a" ]]; then
    echo "==> Cross-building wolfssl (${CROSS}, static, +ML-KEM +ML-DSA +SHA3)"
    rm -rf "$BUILD/wolfssl"
    cp -r "$WOLFSSL_SRC" "$BUILD/wolfssl"
    cd "$BUILD/wolfssl" && ./autogen.sh >/dev/null 2>&1
    ./configure --host=${CROSS} \
        --enable-static --disable-shared \
        --enable-wolftpm --enable-experimental --enable-keygen \
        --enable-pkcallbacks \
        --enable-mlkem --enable-dilithium --enable-sha3 --enable-shake256 \
        --disable-examples --disable-crypttests --disable-benchmark \
        --disable-asm \
        CC=${CROSS}-gcc >/dev/null
    make -j$(nproc) src/libwolfssl.la >/dev/null
fi

if [[ ! -f "$BUILD/wolftpm/src/.libs/libwolftpm.a" ]]; then
    echo "==> Cross-building wolftpm (${CROSS}, static, +PQC, simple 5-arg ioCb)"
    rm -rf "$BUILD/wolftpm"
    cp -r "$WOLFTPM_SRC" "$BUILD/wolftpm"
    cd "$BUILD/wolftpm" && ./autogen.sh >/dev/null 2>&1
    ./configure --host=${CROSS} \
        --enable-static --disable-shared --enable-pqc \
        --disable-examples --disable-tools --disable-wrapper-tests \
        --disable-fwtpm --disable-swtpm --disable-devtpm --disable-winapi \
        --disable-autodetect \
        CC=${CROSS}-gcc \
        CFLAGS="-I$BUILD/wolfssl" \
        LDFLAGS="-L$BUILD/wolfssl/src/.libs" \
        LIBS="-lwolfssl" >/dev/null
    make -j$(nproc) src/libwolftpm.la >/dev/null
fi

echo "==> Linking $OUT (static)"
${CROSS}-gcc -O2 -Wall -Wextra \
    -I$BUILD/wolfssl -I$BUILD/wolftpm -DWOLFSSL_USE_OPTIONS_H \
    -o "$OUT" "$HERE/fwtpm_pqc_test.c" \
    -L$BUILD/wolftpm/src/.libs -lwolftpm \
    -L$BUILD/wolfssl/src/.libs -lwolfssl \
    -static -lm

ls -la "$OUT"
file  "$OUT"
echo "==> OK. Copy $OUT onto the SD card rootfs, then run as root on the board console: /tmp/fwtpm_pqc_test"
