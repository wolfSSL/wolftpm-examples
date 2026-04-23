#!/usr/bin/env python3
"""
UART TPM 2.0 Test Client for fwTPM STM32 Port

Speaks the raw swtpm framing implemented by FwTPM_UartCommandLoop()
in main.c: a TPM command packet is sent verbatim over the UART (starting
with tag 0x8001 / 0x8002), and the device replies with the raw TPM
response packet. The 10-byte TPM response header (tag, size, rc) is
parsed to determine how many bytes to read.

Usage:
  python3 test_uart_tpm.py [/dev/ttyACM0]

Requires: pyserial (pip install pyserial)
"""

import sys
import struct
import serial
import time

# Default serial port
DEFAULT_PORT = "/dev/ttyACM0"
BAUD_RATE = 115200
TIMEOUT = 5  # seconds

# Sanity cap on TPM response size (matches FWTPM_MAX_COMMAND_SIZE order)
MAX_TPM_RSP_SIZE = 4096

# TPM 2.0 command codes
TPM_ST_NO_SESSIONS = 0x8001
TPM_CC_STARTUP = 0x00000144
TPM_CC_GET_CAPABILITY = 0x0000017A
TPM_CC_GET_RANDOM = 0x0000017B
TPM_CC_SELF_TEST = 0x00000143

# TPM capabilities
TPM_CAP_TPM_PROPERTIES = 0x00000006
TPM_PT_MANUFACTURER = 0x00000105
TPM_PT_FIRMWARE_VERSION_1 = 0x00000111


def build_tpm_cmd(cc, payload=b""):
    """Build a TPM 2.0 command with no sessions."""
    size = 10 + len(payload)  # tag(2) + size(4) + cc(4) + payload
    return struct.pack(">HII", TPM_ST_NO_SESSIONS, size, cc) + payload


def _read_exact(ser, nbytes):
    """Read exactly nbytes from ser, looping until full or timeout fires."""
    deadline = time.monotonic() + TIMEOUT
    buf = bytearray()
    while len(buf) < nbytes:
        chunk = ser.read(nbytes - len(buf))
        if chunk:
            buf.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            return bytes(buf)
    return bytes(buf)


def send_tpm_cmd(ser, cmd):
    """Send a raw TPM command over UART and receive the raw TPM response.
    The first 10 bytes of the response are the TPM header (tag, size, rc);
    `size` tells us how much more (size - 10) to read."""
    ser.write(cmd)
    ser.flush()

    # Read 10-byte TPM response header: tag(2) + size(4) + rc(4)
    rsp_hdr = _read_exact(ser, 10)
    if len(rsp_hdr) < 10:
        print(f"  ERROR: Timeout reading TPM response header "
              f"(got {len(rsp_hdr)} bytes)")
        return None

    rsp_size = struct.unpack(">I", rsp_hdr[2:6])[0]
    if rsp_size < 10 or rsp_size > MAX_TPM_RSP_SIZE:
        print(f"  ERROR: Invalid response size {rsp_size}")
        return None

    remaining = rsp_size - 10
    if remaining == 0:
        return rsp_hdr

    rsp_body = _read_exact(ser, remaining)
    if len(rsp_body) < remaining:
        print(f"  ERROR: Short response body "
              f"({len(rsp_body)}/{remaining} bytes)")
        return None

    return rsp_hdr + rsp_body


def parse_tpm_rsp(rsp):
    """Parse TPM response header: tag, size, rc."""
    if len(rsp) < 10:
        return None, None, None
    tag, size, rc = struct.unpack(">HII", rsp[:10])
    return tag, size, rc


def test_startup(ser):
    """Send TPM2_Startup(CLEAR)."""
    print("\n[1] TPM2_Startup(CLEAR)")
    cmd = build_tpm_cmd(TPM_CC_STARTUP, struct.pack(">H", 0))  # SU_CLEAR=0
    rsp = send_tpm_cmd(ser, cmd)
    if rsp is None:
        return False
    tag, size, rc = parse_tpm_rsp(rsp)
    if rc == 0:
        print(f"  OK (rc=0x{rc:08X})")
        return True
    elif rc == 0x100:  # TPM_RC_INITIALIZE (already started)
        print(f"  Already initialized (rc=0x{rc:08X}) - OK")
        return True
    else:
        print(f"  FAIL (rc=0x{rc:08X})")
        return False


def test_self_test(ser):
    """Send TPM2_SelfTest(fullTest=YES)."""
    print("\n[2] TPM2_SelfTest(fullTest=YES)")
    cmd = build_tpm_cmd(TPM_CC_SELF_TEST, struct.pack(">B", 1))
    rsp = send_tpm_cmd(ser, cmd)
    if rsp is None:
        return False
    tag, size, rc = parse_tpm_rsp(rsp)
    print(f"  rc=0x{rc:08X} {'OK' if rc == 0 else 'FAIL'}")
    return rc == 0


def test_get_random(ser, num_bytes=16):
    """Send TPM2_GetRandom and display random bytes."""
    print(f"\n[3] TPM2_GetRandom({num_bytes} bytes)")
    cmd = build_tpm_cmd(TPM_CC_GET_RANDOM, struct.pack(">H", num_bytes))
    rsp = send_tpm_cmd(ser, cmd)
    if rsp is None:
        return False
    tag, size, rc = parse_tpm_rsp(rsp)
    if rc != 0:
        print(f"  FAIL (rc=0x{rc:08X})")
        return False

    # Parse TPM2B_DIGEST: size(2) + data
    rand_size = struct.unpack(">H", rsp[10:12])[0]
    rand_data = rsp[12:12 + rand_size]
    print(f"  OK: {rand_data.hex()}")

    # Sanity: random data should not be all zeros
    if rand_data == b'\x00' * len(rand_data):
        print("  WARNING: All zeros (RNG may not be working)")
        return False
    return True


def test_get_capability(ser):
    """Send TPM2_GetCapability for manufacturer info."""
    print("\n[4] TPM2_GetCapability(TPM_PT_MANUFACTURER)")
    payload = struct.pack(">III",
        TPM_CAP_TPM_PROPERTIES,  # capability
        TPM_PT_MANUFACTURER,     # property
        8)                       # propertyCount
    cmd = build_tpm_cmd(TPM_CC_GET_CAPABILITY, payload)
    rsp = send_tpm_cmd(ser, cmd)
    if rsp is None:
        return False
    tag, size, rc = parse_tpm_rsp(rsp)
    if rc != 0:
        print(f"  FAIL (rc=0x{rc:08X})")
        return False

    # Parse response: moreData(1) + cap(4) + count(4) + properties...
    offset = 10
    more_data = rsp[offset]; offset += 1
    cap = struct.unpack(">I", rsp[offset:offset+4])[0]; offset += 4
    count = struct.unpack(">I", rsp[offset:offset+4])[0]; offset += 4

    print(f"  Properties (count={count}):")
    for i in range(min(count, 8)):
        if offset + 8 > len(rsp):
            break
        prop, val = struct.unpack(">II", rsp[offset:offset+8])
        offset += 8
        # Decode manufacturer as 4-char string
        if prop == TPM_PT_MANUFACTURER:
            mfr = struct.pack(">I", val).decode('ascii', errors='replace')
            print(f"    0x{prop:08X} = 0x{val:08X} ({mfr})")
        else:
            print(f"    0x{prop:08X} = 0x{val:08X}")
    return True


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT

    print(f"=== fwTPM UART Test Client ===")
    print(f"Port: {port} @ {BAUD_RATE} baud")

    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=TIMEOUT)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {port}: {e}")
        sys.exit(1)

    # Small delay for connection
    time.sleep(0.5)

    # Flush any pending data
    ser.reset_input_buffer()

    passed = 0
    failed = 0

    for test_fn in [test_startup, test_self_test, test_get_random,
                    test_get_capability]:
        try:
            if test_fn(ser):
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"  EXCEPTION: {e}")
            failed += 1

    print(f"\n=== Results: {passed} passed, {failed} failed ===")
    ser.close()
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
