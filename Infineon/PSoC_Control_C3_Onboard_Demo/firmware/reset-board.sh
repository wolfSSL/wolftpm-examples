#!/bin/bash
# Reset the board, and the TPM with it, on Linux and macOS.
#
# The Windows equivalent is reset-board.ps1, and start-demo.cmd points the
# demo server at it. Elsewhere there is no launcher, so set the variable
# yourself before starting the server, or the Console tab's Reset button
# reports that the host has no reset mechanism:
#
#   export TPM_RESET_CMD="$PWD/../firmware/reset-board.sh"
#
# Reset type 2 drives the board's XRES, which the TPM's reset line is tied
# to; a plain core reset leaves the TPM running and will not clear its
# failure mode.
set -u

JLINK=${JLINK:-JLinkExe}
# A bare invocation attaches to whichever probe it finds first, which may
# belong to another board on the same bench.
SERIAL=${TPM_BOARD_SERIAL:-${1:-}}

command -v "$JLINK" > /dev/null || {
    echo "$JLINK not found; install the SEGGER J-Link software"
    exit 1
}

script=$(mktemp)
trap 'rm -f "$script"' EXIT
# The device, interface and speed go in the script rather than on the command
# line: passed as arguments they can be dropped silently, and every target
# command then fails with "No valid device has been selected".
cat > "$script" <<'JLINK_SCRIPT'
si SWD
speed 4000
device PSC3xxF
connect
RSetType 2
r
go
q
JLINK_SCRIPT

sn=""
[ -n "$SERIAL" ] && sn="-SelectEmuBySN $SERIAL"

# -ExitOnError 1 so a failed connect or reset sets the exit status; the demo
# server runs this with check=True and would otherwise take a failure for a
# reset.
out=$("$JLINK" $sn -NoGui 1 -ExitOnError 1 -CommanderScript "$script" 2>&1)
rc=$?

case "$out" in
    *"is unknown"*|*"No valid device has been selected"*)
        echo "This J-Link does not know PSC3xxF; see BUILD.md section 4"
        exit 1
        ;;
esac
if [ "$rc" -ne 0 ]; then
    echo "$out" | tail -5
    echo "RESET FAILED (exit $rc)"
    exit 1
fi
echo "RESET DONE"
