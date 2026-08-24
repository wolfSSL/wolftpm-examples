#!/usr/bin/env python3
# fwtpm_nv_persist_test.py
#
# TPM-level NV persistence test for the wolfTPM fwTPM. Defines an
# owner NV index, writes a marker value, and reads it back. Run once to define +
# write; reload the firmware (RAM is wiped, QSPI flash is not) and run again: the
# read returns the same value and prints PERSISTED, proving the TPM's own NV
# survived in the QSPI flash NV sectors (build with -DFWTPM_NV_QSPI).
#
# Usage: fwtpm_nv_persist_test.py [/dev/ttyUSBx | /path/to/pty] [hexvalue]
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+.

import sys
import struct
import serial

PORT  = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
VALUE = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0xDEADBEEF
BAUD  = 115200
# Bound the device-announced response size (>= the device FWTPM_MAX_COMMAND_SIZE)
# so a bogus 32-bit length cannot make the client block/allocate unboundedly.
MAX_RSP = 8192

TPM_ST_SESSIONS = 0x8002
TPM_ST_NO_SESS  = 0x8001
TPM_RH_OWNER    = 0x40000001
TPM_RS_PW       = 0x40000009
ALG_SHA256      = 0x000B
NV_INDEX        = 0x01000010

CC_STARTUP       = 0x00000144
CC_NV_DEFINE     = 0x0000012A
CC_NV_WRITE      = 0x00000137
CC_NV_READ       = 0x0000014E

# TPMA_NV: OWNERWRITE | OWNERREAD | NO_DA (non-orderly => NV-backed, persistent)
NVA_OWNERWRITE = 0x00000002
NVA_OWNERREAD  = 0x00020000
NVA_NO_DA      = 0x02000000
NV_ATTR        = NVA_OWNERWRITE | NVA_OWNERREAD | NVA_NO_DA

RC_SUCCESS = 0x000


def pw_auth():
    """Empty-password TPM_RS_PW session authorization area (9 bytes)."""
    return struct.pack(">I", TPM_RS_PW) + struct.pack(">H", 0) + \
           bytes([0x00]) + struct.pack(">H", 0)


def read_exact(ser, n):
    buf = b""
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            raise TimeoutError(f"timeout ({len(buf)}/{n})")
        buf += chunk
    return buf


def xfer(ser, tag, cc, handles=b"", auth=b"", params=b""):
    if tag == TPM_ST_SESSIONS:
        body = handles + struct.pack(">I", len(auth)) + auth + params
    else:
        body = handles + params
    pkt = struct.pack(">HII", tag, 10 + len(body), cc) + body
    ser.write(pkt)
    hdr = read_exact(ser, 10)
    rtag, size, rc = struct.unpack(">HII", hdr)
    if size < 10 or size > MAX_RSP:
        raise ValueError(f"bad response size {size} (max {MAX_RSP})")
    rbody = read_exact(ser, size - 10) if size > 10 else b""
    return rc, rtag, rbody


def resp_params(rtag, rbody):
    """Strip the leading parameterSize (present when the response is sessioned)."""
    if rtag == TPM_ST_SESSIONS and len(rbody) >= 4:
        (psize,) = struct.unpack(">I", rbody[:4])
        return rbody[4:4 + psize]
    return rbody


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
    print(f"[fwTPM NV persistence test on {PORT}]  index=0x{NV_INDEX:08X}")
    drain_banner(ser)

    rc, _, _ = xfer(ser, TPM_ST_NO_SESS, CC_STARTUP, params=struct.pack(">H", 0))
    print(f"TPM2_Startup        rc=0x{rc:08X} "
          f"({'OK' if rc in (0, 0x100) else 'FAIL'})")

    # First try to read the index (proves persistence if it already exists).
    handles = struct.pack(">I", TPM_RH_OWNER) + struct.pack(">I", NV_INDEX)
    params = struct.pack(">HH", 4, 0)  # readSize=4, offset=0
    rc, rtag, rbody = xfer(ser, TPM_ST_SESSIONS, CC_NV_READ,
                           handles, pw_auth(), params)

    if rc == RC_SUCCESS:
        p = resp_params(rtag, rbody)
        (dsz,) = struct.unpack(">H", p[:2])
        val = struct.unpack(">I", p[2:2 + dsz])[0]
        print(f"TPM2_NV_Read        rc=0x{rc:08X}  value=0x{val:08X}  "
              f"<= PERSISTED from a previous run")
        ser.close()
        return

    print(f"TPM2_NV_Read        rc=0x{rc:08X}  (index not defined yet - "
          f"defining and writing 0x{VALUE:08X})")

    # Define the index: TPM2B_AUTH (empty) + TPM2B_NV_PUBLIC.
    nvpub = struct.pack(">I", NV_INDEX) + struct.pack(">H", ALG_SHA256) + \
            struct.pack(">I", NV_ATTR) + struct.pack(">H", 0) + \
            struct.pack(">H", 4)  # authPolicy size 0, dataSize 4
    params = struct.pack(">H", 0) + struct.pack(">H", len(nvpub)) + nvpub
    rc, _, _ = xfer(ser, TPM_ST_SESSIONS, CC_NV_DEFINE,
                    struct.pack(">I", TPM_RH_OWNER), pw_auth(), params)
    print(f"TPM2_NV_DefineSpace rc=0x{rc:08X} "
          f"({'OK' if rc == 0 else 'FAIL'})")
    if rc != 0:
        ser.close()
        return

    # Write the marker value.
    params = struct.pack(">H", 4) + struct.pack(">I", VALUE) + struct.pack(">H", 0)
    rc, _, _ = xfer(ser, TPM_ST_SESSIONS, CC_NV_WRITE, handles, pw_auth(), params)
    print(f"TPM2_NV_Write       rc=0x{rc:08X} "
          f"({'OK' if rc == 0 else 'FAIL'})  value=0x{VALUE:08X}")

    # Read back in the same session.
    params = struct.pack(">HH", 4, 0)
    rc, rtag, rbody = xfer(ser, TPM_ST_SESSIONS, CC_NV_READ,
                           handles, pw_auth(), params)
    if rc == 0:
        p = resp_params(rtag, rbody)
        (dsz,) = struct.unpack(">H", p[:2])
        val = struct.unpack(">I", p[2:2 + dsz])[0]
        print(f"TPM2_NV_Read        rc=0x{rc:08X}  value=0x{val:08X}  "
              f"(reload the firmware and re-run to prove persistence)")
    else:
        print(f"TPM2_NV_Read        rc=0x{rc:08X}  FAIL")
    ser.close()


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        print(f"timeout: {e} - is the device running and serving TPM commands? "
              f"Reload the firmware and retry.")
        sys.exit(1)
