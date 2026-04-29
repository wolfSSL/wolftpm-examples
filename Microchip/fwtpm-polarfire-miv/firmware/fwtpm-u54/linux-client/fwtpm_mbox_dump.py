#!/usr/bin/env python3
#
# fwtpm_mbox_dump.py
#
# Decode the PolarFire SoC fwTPM mailbox + trap block from /dev/mem on
# the running Linux U54 harts. Run as root on the target.
#
# Layout (must match fwtpm_tis_mpfs.h FWTPM_MPFS_MAILBOX):
#     0x00 magic        u32  ("FWTI" once FWTPM_TIS_MPFS_SetHAL ran)
#     0x04 version      u32
#     0x08 cmd_ready    u32
#     0x0C rsp_ready    u32
#     0x10 server_alive u32
#     0x14 progress     u32  (FWTPM_PROG_*)
#     0x18 rc           u32
#     0x1C echo_nonce   u32  (server echoes client's cmd nonce)
#     0x20 trap_marker  u32  (0xDEADC0DE if M-mode trap fired on hart 4)
#     0x24 xport_id     u32  (FWTPM_XPORT_ID_* of the running build)
#     0x28 trap_mcause  u64
#     0x30 trap_mepc    u64
#     0x38 trap_mtval   u64
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

# Boot/trap breadcrumbs (progress + trap_*) are pinned to L2 LIM by
# startup.S and main.c so they stay visible even when the live mailbox
# relocates; always read those from here regardless of --xport.
DBG_BASE = 0x08000000

# Live-mailbox base per firmware FWTPM_XPORT build. The live mailbox
# fields (magic/cmd/rsp/server_alive/rc/echo_nonce/xport_id) and the
# console ring move to the non-cached DDR alias under DDR_NONCACHED;
# the other builds keep the LIM base. Mirrors fwtpm_smoke.py.
XPORT_BASE = {
    "lim_cached":    0x08000000,
    "ddr_noncached": 0x1400000000,
    "l1d_off":       0x08000000,
    "ihc":           0x08000000,
}

# Must cover the 64 B mailbox (0x00..0x40) plus the console ring that
# follows it: 32 B ring header (64-bit write_pos/read_pos + 32-bit
# size/overflow_cnt) + 4064 B data = up to 0x1040. Map 8 KB (two pages)
# so the m_live[0x40:0x1040] console-ring slice below stays in bounds.
SHM_LEN  = 0x2000

PROG_NAMES = {
    0x00:        "(zero / not written)",
    0x10:        "FWTPM_PROG_ENTRY     -- _start reached (asm)",
    0x100:       "FWTPM_PROG_MAIN      -- main() entered",
    0x2:         "FWTPM_PROG_UART      -- UART4 init done",
    0x3:         "FWTPM_PROG_PRINTF    -- first printf done",
    0x4:         "FWTPM_PROG_NV_CLOCK  -- NV + clock HAL registered",
    0x5:         "FWTPM_PROG_TIS_HAL   -- TIS HAL set",
    0x6:         "FWTPM_PROG_INIT_DONE -- FWTPM_Init returned",
    0x7:         "FWTPM_PROG_SERVERLOOP-- entered TIS server loop",
    0x8:         "FWTPM_PROG_LOOP_EXIT -- server loop returned (unexpected)",
    0xBADC0DE0:  "FWTPM_PROG_TRAPCAUGHT-- _trap_vector ran (see trap_*)",
}

# RV64 mcause exception codes (interrupt bit clear)
MCAUSE_EXCEPT = {
    0:  "instruction address misaligned",
    1:  "instruction access fault",
    2:  "illegal instruction",
    3:  "breakpoint",
    4:  "load address misaligned",
    5:  "load access fault",
    6:  "store/AMO address misaligned",
    7:  "store/AMO access fault",
    8:  "ecall from U-mode",
    9:  "ecall from S-mode",
    11: "ecall from M-mode",
    12: "instruction page fault",
    13: "load page fault",
    15: "store/AMO page fault",
}

def map_region(base: int, length: int) -> mmap.mmap:
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    try:
        return mmap.mmap(fd, length, mmap.MAP_SHARED, mmap.PROT_READ,
                         offset=base)
    finally:
        os.close(fd)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Decode the PolarFire SoC fwTPM mailbox + console ring "
                    "from /dev/mem (run as root on the target).")
    ap.add_argument("--xport", choices=sorted(XPORT_BASE.keys()),
                    default="lim_cached",
                    help="firmware FWTPM_XPORT build; selects the live-mailbox "
                         "base (default lim_cached)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=None,
                    help="override the live-mailbox physical base address")
    args = ap.parse_args()
    live_base = args.base if args.base is not None else XPORT_BASE[args.xport]

    # Live mailbox fields + console ring come from the selected base; the
    # boot/trap breadcrumbs (progress, trap_*) are pinned to LIM, so read
    # those from DBG_BASE regardless of --xport. The two coincide for every
    # build except DDR_NONCACHED.
    m_live = map_region(live_base, SHM_LEN)
    m_dbg  = m_live if live_base == DBG_BASE else map_region(DBG_BASE, SHM_LEN)

    raw  = bytes(m_live[0:0x40])
    bc   = bytes(m_dbg[0:0x40])
    (magic, version, cmd_ready, rsp_ready, server_alive,
     _prog_live, rc, echo_nonce,
     _trapm_live, xport_id,
     _mc_live, _me_live, _mt_live) = struct.unpack_from("<8I I I Q Q Q", raw, 0)
    (_mg, _ver, _cmd, _rsp, _alive,
     progress, _rc_dbg, _echo_dbg,
     trap_marker, _xport_dbg,
     trap_mcause, trap_mepc, trap_mtval) = struct.unpack_from("<8I I I Q Q Q",
                                                              bc, 0)

    print(f"=== fwTPM mailbox (live @ 0x{live_base:08x}, "
          f"breadcrumbs @ 0x{DBG_BASE:08x}) ===")
    print(f"hex (first 64B): {' '.join(f'{b:02x}' for b in raw)}")
    print()
    magic_str = "FWTI (set by server)" if magic == 0x46575449 else "(uninit/garbage)"
    print(f"  magic         = 0x{magic:08x}  {magic_str}")
    print(f"  version       = 0x{version:08x}")
    print(f"  cmd_ready     = 0x{cmd_ready:08x}")
    print(f"  rsp_ready     = 0x{rsp_ready:08x}")
    print(f"  server_alive  = 0x{server_alive:08x}")
    prog_label = PROG_NAMES.get(progress, "(unknown)")
    print(f"  progress      = 0x{progress:08x}  {prog_label}")
    print(f"  rc            = 0x{rc:08x}  ({rc if rc < 0x80000000 else rc - 0x100000000})")
    xport_names = {1: "LIM_CACHED", 2: "DDR_NONCACHED", 3: "L1D_OFF", 4: "IHC"}
    print(f"  echo_nonce    = 0x{echo_nonce:08x}")
    print(f"  xport_id      = {xport_id}  {xport_names.get(xport_id, '(unset/unknown)')}")
    print()

    print(f"=== console ring @ 0x{live_base + 0x40:08x} ===")
    cons = bytes(m_live[0x40:0x40 + 32 + 4064])
    # Header: write_pos/read_pos are 64-bit monotonic counters, then
    # size + overflow_cnt as 32-bit; data starts at offset 32.
    write_pos, read_pos, size, overflow = struct.unpack_from("<QQII", cons, 0)
    print(f"  write_pos = {write_pos}")
    print(f"  read_pos  = {read_pos}")
    print(f"  size      = {size}")
    print(f"  overflow  = {overflow}")
    if size in (4064,) and write_pos > 0:
        ring_data = cons[32:32 + size]
        if write_pos < size:
            text = ring_data[:write_pos]
        else:
            wp = write_pos % size
            text = ring_data[wp:] + ring_data[:wp]
        try:
            print("  --- output ---")
            print(text.decode("utf-8", errors="replace"))
            print("  --- end ---")
        except Exception as e:
            print(f"  decode error: {e}")
    else:
        print("  (ring not initialized)")
    print()

    if trap_marker == 0xDEADC0DE:
        is_intr = bool(trap_mcause >> 63)
        cause   = trap_mcause & 0x7FFFFFFFFFFFFFFF
        kind    = "interrupt" if is_intr else "exception"
        cause_label = "" if is_intr else MCAUSE_EXCEPT.get(cause, "(unknown exception)")
        print(f"  *** TRAP CAUGHT ***")
        print(f"  trap_marker   = 0x{trap_marker:08x} (DEADC0DE)")
        print(f"  trap_mcause   = 0x{trap_mcause:016x}  -> {kind} #{cause}  {cause_label}")
        print(f"  trap_mepc     = 0x{trap_mepc:016x}  (PC at trap)")
        print(f"  trap_mtval    = 0x{trap_mtval:016x}  (faulting addr/insn)")
        print()
        print(f"  Disassemble that PC range with:")
        print(f"     riscv64-unknown-elf-objdump -d fwtpm_u54.elf "
              f"| awk '/^{trap_mepc:016x}/{{p=1}} p; /^$/{{if(p)exit}}'")
    else:
        print(f"  trap_marker   = 0x{trap_marker:08x}  (no trap recorded)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
