#!/usr/bin/env python3
#
# fwtpm_caps.py
#
# PolarFire SoC fwTPM caps demo and health check (read-only, run as root
# on the target). The fTPM server runs bare-metal on U54 hart 4 and, at
# startup, executes real TPM 2.0 commands itself -- TPM2_Startup,
# TPM2_GetCapability (manufacturer/vendor), TPM2_GetRandom (System
# Controller TRNG) -- printing the results to a shared-memory console ring.
# This client reads that ring and the mailbox status over /dev/mem and
# reports PASS/FAIL. It is the working analogue of the cross-compiled
# wolftpm/examples/wrap/caps; the interactive Linux -> hart 4 round-trip
# (driving commands from Linux) is still being hardened, so the caps are
# executed on the hart and observed here. The optional --roundtrip mode
# exercises that command path directly (opt-in, diagnostic).
#
# Usage (default transport is DDR_WCB, mailbox at 0xC0000000):
#   python3 fwtpm_caps.py            # caps test + PASS/FAIL (read-only)
#   python3 fwtpm_caps.py --dump     # also print the full ring + breadcrumbs
#   python3 fwtpm_caps.py --base 0x08000000   # a different FWTPM_XPORT build
#   python3 fwtpm_caps.py --roundtrip         # also drive a DID_VID round-trip
#                                             # (RW /dev/mem; DIAG, non-gating)
#   python3 fwtpm_caps.py --roundtrip-strict  # make the round-trip gate RESULT
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

# Live-mailbox base per FWTPM_XPORT build. The mailbox + console ring live
# here; the boot/trap breadcrumbs stay pinned in LIM (DBG_BASE).
XPORT_BASE = {
    "ddr_wcb":       0xC0000000,
    "lim_cached":    0x08000000,
    "ddr_noncached": 0x1400000000,
    # L1D_OFF and IHC keep the LIM base (fwtpm_tis_mpfs.h #else branch), so
    # the diagnostic set matches every FWTPM_XPORT the Makefile exposes.
    "l1d_off":       0x08000000,
    "ihc":           0x08000000,
}
DBG_BASE = 0x08000000
SHM_LEN  = 0x2000
TIS_OFF  = 0x1040

MBOX_MAGIC = 0x46575449   # "FWTI"
TIS_MAGIC  = 0x57544953   # "WTIS"
DID_VID    = 0x50544657   # "WFTP" -- wolfTPM fTPM vendor id
XPORT_NAME = {1: "LIM_CACHED", 2: "DDR_NONCACHED", 3: "L1D_OFF", 4: "IHC",
              5: "DDR_WCB"}

# Current-boot breadcrumbs (mailbox @ DBG_BASE, in LIM). startup.S re-clears
# these at every reset, so -- unlike the retained DDR mailbox fields -- they
# reflect THIS boot and cannot pass from a previous run.
PROG_SERVERLOOP = 0x7          # FWTPM_PROG_SERVERLOOP: in FWTPM_TIS_ServerLoop
TRAP_MAGIC      = 0xDEADC0DE   # FWTPM_TRAP_MAGIC: an M-mode trap fired


def map_region(base, writable=False):
    if writable:
        fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        prot = mmap.PROT_READ | mmap.PROT_WRITE
    else:
        fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
        prot = mmap.PROT_READ
    try:
        return mmap.mmap(fd, SHM_LEN, mmap.MAP_SHARED, prot, offset=base)
    finally:
        os.close(fd)


# Mailbox field offsets (FWTPM_MPFS_MAILBOX in fwtpm_tis_mpfs.h).
MBOX_CMD_READY  = 0x08   # client writes nonce, server clears
MBOX_RSP_READY  = 0x0C   # server sets 1, client clears
MBOX_ECHO_NONCE = 0x1C   # server echoes the client nonce

# TIS register block (FWTPM_TIS_REGS in wolftpm fwtpm_tis.h) at TIS_OFF.
TIS_REG_ADDR     = TIS_OFF + 8    # UINT32 reg_addr
TIS_REG_LEN      = TIS_OFF + 12   # UINT32 reg_len
TIS_REG_IS_WRITE = TIS_OFF + 16   # BYTE   reg_is_write
TIS_REG_DATA     = TIS_OFF + 17   # BYTE   reg_data[64]
TIS_DID_VID_ADDR = 0x0F00         # TPM_DID_VID register offset (locality 0)


def roundtrip(m_live, timeout_s=2.0):
    """Opt-in Linux -> hart 4 command round-trip over the DDR_WCB mailbox.

    Drives one TIS read of the DID_VID register through the nonce protocol
    the server implements (TisMpfsWaitRequest -> TisHandleRegAccess ->
    TisMpfsSignalResponse): write the register-read request, raise cmd_ready
    with a changing nonce, wait for rsp_ready, then confirm the server echoed
    the nonce (proves hart 4 saw THIS write, not a stale line) and returned
    DID_VID = WFTP. Exercises the default command path and the write-combine
    ordering workaround that the read-only checks cannot see.

    Returns (ok, detail). ok is None on timeout (interactive path still being
    hardened -- reported as DIAG rather than a hard failure).
    """
    # Nonzero, changing nonce (never a literal 1) so a stale echo cannot pass.
    nonce = (int(time.time() * 1000) & 0x7FFFFFFF) | 1

    # Request: read 4 bytes of DID_VID. Write the register fields first, then
    # raise cmd_ready last so the server never sees the nonce before the
    # request it gates. The 0xC0000000 window is non-cached, so these writes
    # reach DDR directly.
    m_live[TIS_REG_ADDR:TIS_REG_ADDR + 4] = struct.pack("<I", TIS_DID_VID_ADDR)
    m_live[TIS_REG_LEN:TIS_REG_LEN + 4]   = struct.pack("<I", 4)
    m_live[TIS_REG_IS_WRITE:TIS_REG_IS_WRITE + 1] = b"\x00"
    m_live[MBOX_RSP_READY:MBOX_RSP_READY + 4] = struct.pack("<I", 0)
    m_live[MBOX_CMD_READY:MBOX_CMD_READY + 4] = struct.pack("<I", nonce)
    try:
        m_live.flush()
    except OSError:
        pass

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if struct.unpack_from("<I", m_live, MBOX_RSP_READY)[0] == 1:
            break
        time.sleep(0.005)
    else:
        return None, ("no rsp_ready within %.1fs -- hart 4 not serving or "
                      "interactive path not yet coherent" % timeout_s)

    echo    = struct.unpack_from("<I", m_live, MBOX_ECHO_NONCE)[0]
    did_vid = struct.unpack_from("<I", m_live, TIS_REG_DATA)[0]
    m_live[MBOX_RSP_READY:MBOX_RSP_READY + 4] = struct.pack("<I", 0)  # ack

    if echo != nonce:
        return False, ("echo_nonce 0x%08x != sent 0x%08x (stale read)"
                       % (echo, nonce))
    if did_vid != DID_VID:
        return False, ("DID_VID 0x%08x != WFTP 0x%08x" % (did_vid, DID_VID))
    return True, "echo_nonce + DID_VID=WFTP round-trip ok (nonce 0x%08x)" % nonce


def console_text(m_live):
    """Reconstruct the console ring (overwrite-oldest, 64-bit positions)."""
    cons = bytes(m_live[0x40:0x40 + 32 + 4064])
    write_pos, _read_pos, size, _overflow = struct.unpack_from("<QQII", cons, 0)
    if size != 4064 or write_pos == 0:
        return ""
    data = cons[32:32 + size]
    if write_pos < size:
        text = data[:write_pos]
    else:
        wp = write_pos % size
        text = data[wp:] + data[:wp]
    return text.decode("utf-8", errors="replace")


def extract_caps(console):
    s = console.find("=== fTPM TPM2_GetCapability")
    if s < 0:
        return ""
    e = console.find("=== end capabilities ===", s)
    return console[s:e + len("=== end capabilities ===")] if e >= 0 \
        else console[s:]


def main():
    ap = argparse.ArgumentParser(
        description="PolarFire SoC fwTPM caps demo / health check "
                    "(read-only, run as root).")
    ap.add_argument("--xport", choices=sorted(XPORT_BASE), default="ddr_wcb",
                    help="firmware FWTPM_XPORT build (default ddr_wcb)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=None,
                    help="override the live-mailbox physical base address")
    ap.add_argument("--dump", action="store_true",
                    help="also print the full console ring + breadcrumbs")
    ap.add_argument("--roundtrip", action="store_true",
                    help="opt-in: drive a Linux -> hart 4 DID_VID command "
                         "round-trip (opens /dev/mem read-write); reported as "
                         "DIAG, does not fail the health check unless "
                         "--roundtrip-strict")
    ap.add_argument("--roundtrip-strict", action="store_true",
                    help="make a --roundtrip failure/timeout gate the RESULT")
    args = ap.parse_args()
    base = args.base if args.base is not None else XPORT_BASE[args.xport]
    do_rt = args.roundtrip or args.roundtrip_strict

    m_live = map_region(base, writable=do_rt)
    m_dbg  = m_live if base == DBG_BASE else map_region(DBG_BASE)

    magic        = struct.unpack_from("<I", m_live, 0x00)[0]
    server_alive = struct.unpack_from("<I", m_live, 0x10)[0]
    xport_id     = struct.unpack_from("<I", m_live, 0x24)[0]
    tis_magic    = struct.unpack_from("<I", m_live, TIS_OFF + 0)[0]
    did_vid      = struct.unpack_from("<I", m_live, TIS_OFF + 104)[0]
    progress     = struct.unpack_from("<I", m_dbg, 0x14)[0]
    trap_marker  = struct.unpack_from("<I", m_dbg, 0x20)[0]
    console      = console_text(m_live)
    caps         = extract_caps(console)

    print("Live mailbox base : 0x%08x (--xport %s)" % (base, args.xport))
    print("server_alive      : %d   xport_id %d (%s)"
          % (server_alive, xport_id, XPORT_NAME.get(xport_id, "unknown")))

    if caps:
        print("\n" + caps.strip())

    print("\nChecks:")
    results = []

    def check(name, cond):
        results.append(cond)
        print("  [%s] %s" % ("PASS" if cond else "FAIL", name))

    check("fTPM server alive (mailbox magic FWTI)",
          magic == MBOX_MAGIC and server_alive == 1)
    check("TIS register block live (magic WTIS)", tis_magic == TIS_MAGIC)
    check("fTPM identity did_vid = WFTP (0x%08x)" % did_vid,
          did_vid == DID_VID)
    check("on-hart TPM2_GetCapability ran (Manufacturer WOLF)",
          '"WOLF"' in console)
    # Success prints "TPM2_GetRandom(<n>) ="; failure prints "TPM2_GetRandom:
    # rc=...". Match the actual firmware output rather than a symbolic string
    # the firmware never emits.
    check("on-hart TPM2_GetRandom ok (live entropy, no error)",
          "TPM2_GetRandom(" in caps and "TPM2_GetRandom:" not in caps)
    # LIM breadcrumbs prove THIS boot is healthy: the retained DDR mailbox
    # fields above could match on stale data from a prior run that trapped
    # before C init cleared them.
    check("current boot healthy (progress=0x%x, no trap 0x%08x)"
          % (progress, trap_marker),
          trap_marker == 0 and progress == PROG_SERVERLOOP)

    if do_rt:
        ok, detail = roundtrip(m_live)
        if ok is None:
            # Interactive path still being hardened -- report but do not fail
            # the read-only health check (unless the caller asked to gate).
            tag = "FAIL" if args.roundtrip_strict else "DIAG"
            print("  [%s] interactive round-trip: %s" % (tag, detail))
            if args.roundtrip_strict:
                results.append(False)
        else:
            print("  [%s] interactive round-trip: %s"
                  % ("PASS" if ok else "FAIL", detail))
            if ok or args.roundtrip_strict:
                results.append(ok)

    if args.dump:
        print("\n--- full console ring ---")
        print(console)
        print("breadcrumbs @ 0x%08x: progress=0x%08x trap_marker=0x%08x"
              % (DBG_BASE, progress, trap_marker))
        if trap_marker == TRAP_MAGIC:
            # Decode the trap cause the deleted fwtpm_mbox_dump.py used to
            # print: mcause/mepc/mtval are 64-bit, written by startup.S.
            mcause = struct.unpack_from("<Q", m_dbg, 0x28)[0]
            mepc   = struct.unpack_from("<Q", m_dbg, 0x30)[0]
            mtval  = struct.unpack_from("<Q", m_dbg, 0x38)[0]
            print("  trap: mcause=0x%016x mepc=0x%016x mtval=0x%016x"
                  % (mcause, mepc, mtval))

    ok = all(results)
    print("\nRESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
