#!/bin/bash
# fp5-jtag.sh - launch SoftConsole OpenOCD for a FlashPro5 / Embedded
# FlashPro5 behind the usbfilter LD_PRELOAD shim.
#
# Microchip's libfpcomm (fpServer and Libero/FlashPro Express) corrupts
# memory and segfaults during FlashPro port enumeration when the host
# exposes many FTDI-family USB serial devices. The shim hides every USB
# device except the FlashPro and its parent hub from directory
# enumeration; this wrapper computes the allowlist at run time (device
# numbers change on every re-enumeration) and execs OpenOCD. See
# usbfilter.c and ../fpga/README.md for details.
#
# For FlashPro Express fabric programming, run with FPEXPRESS_ENV=1 to
# print the export lines instead, then source them before xvfb-run
# FPExpress.
set -e
SC=${SC:-/opt/Microchip/SoftConsole-v2022.2-RISC-V-747}
DIR="$(cd "$(dirname "$0")" && pwd)"
SHIM="$DIR/usbfilter.so"
[ "$SHIM" -nt "$DIR/usbfilter.c" ] || gcc -shared -fPIC -O2 -o "$SHIM" "$DIR/usbfilter.c" -ldl -lpthread

# locate the FlashPro (VID 1514), its parent hub, and current devnums
FP=""
for d in /sys/bus/usb/devices/*; do
  [ -f "$d/idVendor" ] || continue
  [ "$(cat "$d/idVendor" 2>/dev/null)" = "1514" ] && FP=$(basename "$d") && break
done
[ -n "$FP" ] || { echo "no FlashPro (VID 1514) found" >&2; exit 1; }
BUS=$(cat "/sys/bus/usb/devices/$FP/busnum")
HUB=${FP%.*}
DEVS=$(printf %03d "$(cat "/sys/bus/usb/devices/usb$BUS/devnum")")
[ -f "/sys/bus/usb/devices/$HUB/devnum" ] && \
  DEVS="$DEVS,$(printf %03d "$(cat "/sys/bus/usb/devices/$HUB/devnum")")"
DEVS="$DEVS,$(printf %03d "$(cat "/sys/bus/usb/devices/$FP/devnum")")"
BUSD=$(printf %03d "$BUS")

if [ -n "$FPEXPRESS_ENV" ]; then
  printf 'export LD_PRELOAD=%q\n' "$SHIM"
  printf 'export USBFILTER_SYS=%q\n' "$HUB,$FP"
  printf 'export USBFILTER_DEV=%q\n' "$DEVS"
  printf 'export USBFILTER_BUS=%q\n' "$BUSD"
  exit 0
fi

echo "FlashPro at $FP (bus $BUS), allowing devices $DEVS" >&2
export LD_PRELOAD="$SHIM"
export USBFILTER_SYS="$HUB,$FP"
export USBFILTER_DEV="$DEVS"
export USBFILTER_BUS="$BUSD"
exec "$SC/openocd/bin/openocd" -s "$SC/openocd/share/openocd/scripts" \
     -f board/microsemi-riscv.cfg "$@"
