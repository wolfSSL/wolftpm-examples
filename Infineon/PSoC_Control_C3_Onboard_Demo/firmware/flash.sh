#!/bin/bash
# Flash the demo firmware to the board, on Linux and macOS.
#
# The Windows equivalent is flash.ps1. Both write one image at 0x22000000:
# factory.bin holds wolfBoot at the start and the signed demo behind it.
# BUILD.md section 4 explains the three traps this avoids - the device name,
# the address alias, and a J-Link too old to know the part.
set -u

IMAGE=${1:-$(dirname "$0")/../factory.bin}
JLINK=${JLINK:-JLinkExe}
# A bare invocation attaches to whichever probe it finds first, which may
# belong to another board on the same bench.
SERIAL=${TPM_BOARD_SERIAL:-}

command -v "$JLINK" > /dev/null || {
    echo "$JLINK not found; install the SEGGER J-Link software"
    exit 1
}
[ -f "$IMAGE" ] || { echo "image not found: $IMAGE"; exit 1; }
echo "using $(command -v "$JLINK"), image $IMAGE"

script=$(mktemp)
trap 'rm -f "$script"' EXIT
# The device, interface and speed go in the script rather than on the command
# line: passed as arguments they can be dropped silently, and every target
# command then fails with "No valid device has been selected".
cat > "$script" <<JLINK_SCRIPT
si SWD
speed 4000
device PSC3xxF
connect
h
loadbin $IMAGE 0x22000000
r
go
q
JLINK_SCRIPT

sn=""
[ -n "$SERIAL" ] && sn="-SelectEmuBySN $SERIAL"

out=$("$JLINK" $sn -NoGui 1 -ExitOnError 1 -CommanderScript "$script" 2>&1)
rc=$?

case "$out" in
    *"is unknown"*|*"No valid device has been selected"*)
        echo "This J-Link does not know PSC3xxF; see BUILD.md section 4"
        exit 1
        ;;
esac
# An O.K. anywhere in the transcript belongs to whichever command printed it,
# and connect prints one before the image is touched. Require one that
# follows the download, so an earlier success cannot stand in for a failed
# load.
if [ "$rc" -ne 0 ] || ! printf '%s\n' "$out" | \
        sed -n '/Downloading file/,$p' | grep -q 'O\.K\.'; then
    printf '%s\n' "$out" | grep -E 'Error|Fail|Downloading|O\.K\.' | tail -5
    echo "FLASH FAILED (exit $rc)"
    exit 1
fi
echo "FLASH DONE: $IMAGE"
