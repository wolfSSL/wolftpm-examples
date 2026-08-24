#!/usr/bin/env python3
# swtpm_uart_bridge.py
#
# Bridges wolfTPM's swtpm socket transport (TCP, mssim framing) to the device
# fwTPM's UART command loop, so the standard wolfTPM example suite (wrap_test,
# caps, ...) can drive the device. wolfTPM connects to localhost:2321 and speaks
# the Microsoft-simulator framing; this forwards each frame to/from the serial
# port, which the device already speaks.
#
# Usage: swtpm_uart_bridge.py [serial-device] [tcp-port]
#   defaults: /dev/ttyUSB0  2321
#
# Copyright (C) 2006-2026 wolfSSL Inc. GPLv2+.

import socket
import struct
import sys
import time
import serial

DEV  = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2321

# Reject frames larger than the device command buffer (FWTPM_MAX_COMMAND_SIZE,
# with headroom) so a bogus 32-bit size cannot make the single-threaded bridge
# block on a body read that never completes.
MAX_CMD  = 8192
# Also bound the device-announced response size, so a malfunctioning device
# cannot make the bridge block on a serial read that never completes.
MAX_RSP  = 8192
# Once a frame has started, bound how long to wait for the rest of it; an
# incomplete frame then closes the connection instead of blocking forever.
FRAME_TO = 60

# mssim platform command codes
SEND_COMMAND = 8
POWER_ON     = 1
POWER_OFF    = 2
NV_ON        = 11
RESET        = 17
SESSION_END  = 20
STOP         = 21

# Moderate per-read timeout; ser_read() below tolerates long silences (a slow
# TPM keygen can compute for minutes with no UART traffic) via a cumulative
# deadline, so an in-progress command is never mistaken for a dead link.
ser = serial.Serial(DEV, 115200, timeout=5)
RESP_DEADLINE = 900  # seconds to wait for a full response (RSA keygen is slow)
time.sleep(0.3)
ser.reset_input_buffer()
# drain any boot banner still queued on the port
ser.timeout = 0.5
while ser.read(4096):
    pass
ser.timeout = 5


def ser_read(n):
    buf = b""
    t0 = time.time()
    while len(buf) < n:
        c = ser.read(n - len(buf))
        if c:
            buf += c
            t0 = time.time()      # reset deadline on any progress
            continue
        if time.time() - t0 > RESP_DEADLINE:
            raise IOError("serial timeout (%d/%d after %ds)"
                          % (len(buf), n, RESP_DEADLINE))
    return buf


def sock_read(conn, n):
    buf = b""
    while len(buf) < n:
        c = conn.recv(n - len(buf))
        if not c:
            return None
        buf += c
    return buf


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("localhost", PORT))
srv.listen(1)
print("swtpm-UART bridge: localhost:%d <-> %s" % (PORT, DEV), flush=True)

cmdno = 0
while True:
    conn, _ = srv.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    try:
        while True:
            conn.settimeout(None)              # block for the next command
            hdr = sock_read(conn, 4)
            if hdr is None:
                break
            cmd = struct.unpack(">I", hdr)[0]
            conn.settimeout(FRAME_TO)          # bound the rest of this frame

            if cmd == SEND_COMMAND:
                loc = sock_read(conn, 1)
                szb = sock_read(conn, 4)
                if loc is None or szb is None:
                    break                      # client disconnected mid-frame
                sz = struct.unpack(">I", szb)[0]
                if sz < 10 or sz > MAX_CMD:
                    print("reject frame with bad size %d (max %d)"
                          % (sz, MAX_CMD), flush=True)
                    break
                body = sock_read(conn, sz)
                if body is None:
                    break
                cc = struct.unpack(">I", body[6:10])[0] if sz >= 10 else 0
                cmdno += 1
                t0 = time.time()
                ser.write(hdr + loc + szb + body)
                ser.flush()
                rspb = ser_read(4)
                rsp_sz = struct.unpack(">I", rspb)[0]
                if rsp_sz > MAX_RSP:
                    print("device announced oversize response %d (max %d)"
                          % (rsp_sz, MAX_RSP), flush=True)
                    break
                rsp = ser_read(rsp_sz) if rsp_sz else b""
                ack = ser_read(4)
                rc = struct.unpack(">I", rsp[6:10])[0] if rsp_sz >= 10 else 0xFFFFFFFF
                dt = time.time() - t0
                print("#%-3d cc=0x%03X -> rc=0x%08X (%dB, %.1fs)"
                      % (cmdno, cc, rc, rsp_sz, dt), flush=True)
                conn.sendall(rspb + rsp + ack)

            elif cmd in (POWER_ON, POWER_OFF, NV_ON, RESET, STOP):
                ser.write(hdr)
                ser.flush()
                conn.sendall(ser_read(4))
                if cmd == STOP:
                    break

            elif cmd == SESSION_END:
                ser.write(hdr)
                ser.flush()
                # device sends no response to SESSION_END

            else:
                ser.write(hdr)
                ser.flush()
                conn.sendall(ser_read(4))
    except Exception as e:
        print("conn closed: %s" % e, flush=True)
    finally:
        conn.close()
