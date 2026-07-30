#!/usr/bin/env python3
# fwtpm_uart_test.py
#
# Host-side driver for the Mi-V fwTPM (firmware/fwtpm). Speaks the raw swtpm
# framing that FwTPM_UartCommandLoop() implements: send a TPM 2.0 command
# packet (tag 0x8001) over the serial port; the device replies with the raw
# TPM response packet. Exercises GetCapability, PCR_Read and GetRandom.
#
# Usage: fwtpm_uart_test.py [/dev/ttyUSBx | /path/to/pty]
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+.

import sys
import struct
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
BAUD = 115200
ST_NO_SESSIONS = 0x8001

# command codes
CC_STARTUP        = 0x00000144
CC_GET_CAPABILITY = 0x0000017A
CC_PCR_READ       = 0x0000017E
CC_GET_RANDOM     = 0x0000017B

# capabilities / properties
TPM_CAP_TPM_PROPERTIES   = 0x00000006
PT_MANUFACTURER          = 0x00000105
PT_FIRMWARE_VERSION_1    = 0x0000010B
PT_FIRMWARE_VERSION_2    = 0x0000010C
ALG_SHA256               = 0x000B


def cmd(cc, payload=b""):
    return struct.pack(">HII", ST_NO_SESSIONS, 10 + len(payload), cc) + payload


def read_exact(ser, n):
    buf = b""
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            raise TimeoutError(f"timeout ({len(buf)}/{n} bytes)")
        buf += chunk
    return buf


def xfer(ser, c):
    ser.write(c)
    hdr = read_exact(ser, 10)
    tag, size, rc = struct.unpack(">HII", hdr)
    body = read_exact(ser, size - 10) if size > 10 else b""
    return rc, body


def drain_banner(ser):
    """Discard the firmware boot banner / self-test text still queued on the
    port before the first TPM exchange, so it is not mis-parsed as a response."""
    saved = ser.timeout
    ser.timeout = 0.5
    ser.reset_input_buffer()
    while ser.read(4096):
        pass
    ser.timeout = saved


def main():
    ser = serial.Serial(PORT, BAUD, timeout=5)
    print(f"[fwTPM UART client on {PORT}]")
    drain_banner(ser)

    rc, _ = xfer(ser, cmd(CC_STARTUP, struct.pack(">H", 0x0000)))  # SU_CLEAR
    print(f"TPM2_Startup            rc=0x{rc:08X} "
          f"({'OK' if rc in (0, 0x100) else 'FAIL'})")  # 0x100=already started

    # GetCapability: manufacturer
    p = struct.pack(">III", TPM_CAP_TPM_PROPERTIES, PT_MANUFACTURER, 1)
    rc, b = xfer(ser, cmd(CC_GET_CAPABILITY, p))
    man = b[-4:] if rc == 0 and len(b) >= 4 else b""
    print(f"TPM2_GetCapability MAN  rc=0x{rc:08X}  manufacturer="
          f"{man!r} ({man.hex()})")

    # GetCapability: firmware version
    for pt, nm in ((PT_FIRMWARE_VERSION_1, "FW1"), (PT_FIRMWARE_VERSION_2, "FW2")):
        p = struct.pack(">III", TPM_CAP_TPM_PROPERTIES, pt, 1)
        rc, b = xfer(ser, cmd(CC_GET_CAPABILITY, p))
        val = struct.unpack(">I", b[-4:])[0] if rc == 0 and len(b) >= 4 else 0
        print(f"TPM2_GetCapability {nm}  rc=0x{rc:08X}  value=0x{val:08X}")

    # PCR_Read PCR0 (SHA-256): TPML_PCR_SELECTION{count=1,{alg,sizeSel=3,sel}}
    sel = struct.pack(">I", 1) + struct.pack(">HB", ALG_SHA256, 3) + bytes([0x01, 0, 0])
    rc, b = xfer(ser, cmd(CC_PCR_READ, sel))
    print(f"TPM2_PCR_Read PCR0      rc=0x{rc:08X}  ({len(b)} bytes)"
          f"{'  '+b.hex() if rc==0 else ''}")

    # GetRandom 16
    rc, b = xfer(ser, cmd(CC_GET_RANDOM, struct.pack(">H", 16)))
    rnd = b[2:] if rc == 0 and len(b) >= 2 else b""
    print(f"TPM2_GetRandom 16       rc=0x{rc:08X}  {rnd.hex()}")

    ser.close()


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        print(f"timeout: {e} - is the device running and serving TPM commands? "
              f"Reload the firmware and retry.")
        sys.exit(1)
