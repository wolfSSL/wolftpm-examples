#!/usr/bin/env python3
#
# fwtpm_smoke.py
#
# End-to-end smoke test of the PolarFire SoC fwTPM IPC. Validates the
# shared-memory transport between the bare-metal fwTPM server on U54
# hart 4 and a Linux client on harts 1-3. Run as root on the target.
#
# Phase 1 (passive): read FWTPM_TIS_REGS magic + did_vid + rid through
#                    /dev/mem. Confirms hart 4 ran TIS init and the
#                    server -> Linux direction is observable.
#                    Expected: did_vid = 0x50544657 ("WFTP" LE).
#
# Phase 2 (active):  drive a TIS read of TPM_DID_VID (offset 0x0F00)
#                    via the mailbox, writing a nonce the server echoes
#                    back (a matching echo proves hart 4 saw the new
#                    write, not a stale line). Both phases pass under HSS
#                    AMP on the default LIM_CACHED build. Pass --xport to
#                    match the firmware's FWTPM_XPORT build (e.g.
#                    ddr_noncached relocates the live mailbox).
#
# The shared region lives in L2 LIM at 0x08000000 for every build except
# DDR_NONCACHED, which relocates the live mailbox to the 64-bit non-cached
# DDR alias (pass --xport ddr_noncached). HSS partitions the 2 MB L2 SRAM
# into 4 ways scratchpad + 8 ways cache + 4 ways LIM (= 512 KiB on the
# Video Kit); see the README for sizing details.
#
# Copyright (C) 2006-2026 wolfSSL Inc.
#
# This file is part of wolfTPM.
#
# wolfTPM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTPM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
#
import argparse
import mmap
import os
import struct
import sys
import time

# Live-mailbox base per fwTPM transport build (FWTPM_XPORT). All builds
# keep the mailbox in L2 LIM except DDR_NONCACHED, which relocates the
# live region to the 64-bit non-cached DDR alias. The boot/trap
# breadcrumbs always stay in LIM (read them with fwtpm_mbox_dump.py).
XPORT_BASE = {
    "lim_cached":    0x08000000,
    "l1d_off":       0x08000000,
    "ihc":           0x08000000,
    "ddr_noncached": 0x1400000000,
}

SHM_BASE     = 0x08000000   # overridden in main() from --xport/--base
SHM_LEN      = 0x4000
MBOX_OFF     = 0x0000
TIS_REGS_OFF = 0x1040

# FWTPM_MPFS_MAILBOX field offsets (from fwtpm_tis_mpfs.h)
MBOX_CMD_READY  = MBOX_OFF + 0x08
MBOX_RSP_READY  = MBOX_OFF + 0x0C
MBOX_ECHO_NONCE = MBOX_OFF + 0x1C
MBOX_XPORT_ID   = MBOX_OFF + 0x24

# FWTPM_TIS_REGS field offsets -- offsetof()-derived from the header
# with the build's struct packing, do not change without re-probing.
TIS_MAGIC        = TIS_REGS_OFF + 0
TIS_VERSION      = TIS_REGS_OFF + 4
TIS_REG_ADDR     = TIS_REGS_OFF + 8
TIS_REG_LEN      = TIS_REGS_OFF + 12
TIS_REG_IS_WRITE = TIS_REGS_OFF + 16
TIS_REG_DATA     = TIS_REGS_OFF + 17
TIS_DID_VID      = TIS_REGS_OFF + 104
TIS_RID          = TIS_REGS_OFF + 108

FWTPM_TIS_MAGIC = 0x57544953  # "WTIS" -- set by FWTPM_TIS_Init
FWTPM_DID_VID   = 0x50544657  # "WFTP" -- vendor-id reg value


def open_shm() -> mmap.mmap:
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    try:
        return mmap.mmap(fd, SHM_LEN, mmap.MAP_SHARED,
                         mmap.PROT_READ | mmap.PROT_WRITE, offset=SHM_BASE)
    finally:
        os.close(fd)


def u32(buf: mmap.mmap, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def write_u32(buf: mmap.mmap, off: int, val: int) -> None:
    struct.pack_into("<I", buf, off, val & 0xFFFFFFFF)


def write_u8(buf: mmap.mmap, off: int, val: int) -> None:
    struct.pack_into("<B", buf, off, val & 0xFF)


def phase1_passive(shm: mmap.mmap) -> bool:
    print("=== Phase 1: passive read of FWTPM_TIS_REGS ===")
    magic   = u32(shm, TIS_MAGIC)
    version = u32(shm, TIS_VERSION)
    didvid  = u32(shm, TIS_DID_VID)
    rid     = u32(shm, TIS_RID)
    ok = True
    print(f"  regs.magic   = 0x{magic:08x}  expect 0x{FWTPM_TIS_MAGIC:08x} (WTIS)"
          f"  {'OK' if magic == FWTPM_TIS_MAGIC else 'FAIL'}")
    if magic != FWTPM_TIS_MAGIC:
        ok = False
    print(f"  regs.version = 0x{version:08x}")
    print(f"  regs.did_vid = 0x{didvid:08x}  expect 0x{FWTPM_DID_VID:08x} (WFTP)"
          f"  {'OK' if didvid == FWTPM_DID_VID else 'FAIL'}")
    if didvid != FWTPM_DID_VID:
        ok = False
    print(f"  regs.rid     = 0x{rid:08x}")
    return ok


def phase2_active(shm: mmap.mmap) -> bool:
    print("\n=== Phase 2: drive TIS read of TPM_DID_VID via mailbox ===")
    # Nonce, not a literal 1: the server echoes it into echo_nonce, which
    # proves hart 4 observed THIS write rather than a stale cached line --
    # the discriminator for the coherency spike.
    nonce = (int(time.time()) & 0x7FFFFFFF) | 1

    write_u32(shm, TIS_REG_ADDR, 0x0F00)
    write_u32(shm, TIS_REG_LEN, 4)
    write_u8 (shm, TIS_REG_IS_WRITE, 0)
    for i in range(4):
        write_u8(shm, TIS_REG_DATA + i, 0)

    write_u32(shm, MBOX_ECHO_NONCE, 0)
    write_u32(shm, MBOX_RSP_READY, 0)
    write_u32(shm, MBOX_CMD_READY, nonce)

    deadline = time.monotonic() + 5.0
    rsp = 0
    while time.monotonic() < deadline:
        rsp = u32(shm, MBOX_RSP_READY)
        if rsp == 1:
            break
        time.sleep(0.001)
    elapsed_ms = (5.0 - (deadline - time.monotonic())) * 1000

    if rsp != 1:
        print(f"  TIMEOUT after 5s -- rsp_ready never set"
              f"  (cmd_ready={u32(shm, MBOX_CMD_READY)},"
              f" echo_nonce={u32(shm, MBOX_ECHO_NONCE):#x})")
        return False

    echoed = u32(shm, MBOX_ECHO_NONCE)
    data = bytes(shm[TIS_REG_DATA:TIS_REG_DATA + 4])
    val  = struct.unpack("<I", data)[0]
    print(f"  responded in {elapsed_ms:.1f} ms")
    print(f"  echo_nonce = 0x{echoed:08x}  expect 0x{nonce:08x}"
          f"  {'OK' if echoed == nonce else 'FAIL (stale cache line)'}")
    print(f"  reg_data = {' '.join(f'{b:02x}' for b in data)}"
          f"   -> 0x{val:08x}"
          f"  {'OK' if val == FWTPM_DID_VID else 'FAIL'}")

    write_u32(shm, MBOX_RSP_READY, 0)
    return rsp == 1 and echoed == nonce and val == FWTPM_DID_VID


XPORT_NAME = {1: "LIM_CACHED", 2: "DDR_NONCACHED", 3: "L1D_OFF", 4: "IHC"}


def main() -> int:
    global SHM_BASE
    ap = argparse.ArgumentParser(description="fwTPM PolarFire SoC smoke test")
    ap.add_argument("--xport", choices=sorted(XPORT_BASE.keys()),
                    default="lim_cached",
                    help="transport the firmware was built with "
                         "(selects the live-mailbox base; default lim_cached)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=None,
                    help="override the live-mailbox physical base address")
    args = ap.parse_args()
    SHM_BASE = args.base if args.base is not None else XPORT_BASE[args.xport]
    print(f"Live mailbox base: 0x{SHM_BASE:08x} (--xport {args.xport})")

    shm = open_shm()

    xport = u32(shm, MBOX_XPORT_ID)
    print(f"server xport_id = {xport} ({XPORT_NAME.get(xport, 'unknown')})")

    p1 = phase1_passive(shm)
    if not p1:
        print("\nPhase 1 failed -- TIS regs not initialized; skipping Phase 2.")
        return 1
    if xport == 4:
        print("\nIHC build: Linux->hart4 doorbell not wired yet; Phase 2 is"
              " expected to time out until the IHC backend lands.")
    p2 = phase2_active(shm)
    print(f"\nResult: phase1={'OK' if p1 else 'FAIL'} "
          f"phase2={'OK' if p2 else 'FAIL'}")
    return 0 if (p1 and p2) else 2


if __name__ == "__main__":
    sys.exit(main())
