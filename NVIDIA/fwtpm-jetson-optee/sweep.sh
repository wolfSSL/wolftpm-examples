#!/bin/bash
# Find the TA_DATA_SIZE and TA_STACK_SIZE floors for the fwTPM trusted
# application on a target board.
#
# Acceptance per size is native_test (RSA create/load/encrypt/decrypt, NV,
# AES-CFB) plus pqc/mldsa_sign, which between them exercise the heaviest
# allocation paths. A size passes only if both do.
#
# Always sweep until something FAILS. A sweep where every size passes has
# not measured a floor, it has only shown that the harness runs.
#
#   BOARD=orin@10.0.4.244 TA_DEV_KIT_DIR=... ./sweep.sh [stack_kb] [heap_kb ...]
#
# Environment:
#   BOARD           ssh destination for the target (required)
#   SUDO_PW         board sudo password; /dev/tee0 is root only
#   TA_DEV_KIT_DIR  OP-TEE TA devkit (export-ta_arm64)
#   CROSS_COMPILE   default aarch64-linux-gnu-
#   REMOTE_WOLFTPM  wolfTPM build on the board that holds the examples
#   WOLFSSL_PREFIX  wolfSSL install the examples link against
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
UUID=9013be0e-39b0-4ed4-80e0-1242c0daf382

BOARD="${BOARD:?set BOARD to the target ssh destination}"
TA_DEV_KIT_DIR="${TA_DEV_KIT_DIR:?set TA_DEV_KIT_DIR to the OP-TEE TA devkit}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
REMOTE_WOLFTPM="${REMOTE_WOLFTPM:-\$HOME/wolftpm-optee}"
WOLFSSL_PREFIX="${WOLFSSL_PREFIX:-/usr/local}"
SUDO_PW="${SUDO_PW:-}"

if [ -n "$SUDO_PW" ]; then
    SUDO="echo $SUDO_PW | sudo -S -p ''"
else
    SUDO="sudo"
fi

STACK_KB="${1:-32}"
shift || true
HEAPS="${*:-512 256 128 96 88 64}"

echo "stack ${STACK_KB} KB; heap sizes: $HEAPS"
echo

for kb in $HEAPS; do
    cat > "$HERE/ta/ta_sizes.h" <<EOF
#define TA_STACK_SIZE (${STACK_KB} * 1024)
#define TA_DATA_SIZE  (${kb} * 1024)
EOF
    rm -f "$HERE/ta/$UUID".*
    if ! make -s -C "$HERE/ta" TA_DEV_KIT_DIR="$TA_DEV_KIT_DIR" \
            CROSS_COMPILE="$CROSS_COMPILE" >/dev/null 2>&1; then
        printf '%6s KB  BUILD FAILED\n' "$kb"
        continue
    fi
    scp -q "$HERE/ta/$UUID.ta" "$BOARD:/tmp/" || {
        printf '%6s KB  SCP FAILED\n' "$kb"; continue; }
    ssh "$BOARD" "$SUDO install -m 0644 -o root -g root \
        /tmp/$UUID.ta /lib/optee_armtz/$UUID.ta" >/dev/null 2>&1

    out=$(ssh "$BOARD" "cd $REMOTE_WOLFTPM && \
        LL=$WOLFSSL_PREFIX/lib:\$PWD/src/.libs; \
        $SUDO env LD_LIBRARY_PATH=\$LL ./examples/native/native_test \
            >/dev/null 2>&1; echo nt=\$?; \
        $SUDO env LD_LIBRARY_PATH=\$LL ./examples/pqc/mldsa_sign \
            >/dev/null 2>&1; echo ml=\$?" 2>&1)
    nt=$(echo "$out" | sed -n 's/^nt=//p')
    ml=$(echo "$out" | sed -n 's/^ml=//p')

    if [ "$nt" = "0" ] && [ "$ml" = "0" ]; then
        printf '%6s KB  PASS\n' "$kb"
    else
        printf '%6s KB  FAIL (native_test=%s mldsa_sign=%s)\n' "$kb" "$nt" "$ml"
    fi
done

echo
echo "Restore ta/ta_sizes.h to the shipping defaults when done."
