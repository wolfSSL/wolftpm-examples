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
# executed on the hart and observed here.
#
# Usage (default transport is DDR_WCB, mailbox at 0xC0000000):
#   python3 fwtpm_caps.py            # caps test + PASS/FAIL
#   python3 fwtpm_caps.py --dump     # also print the full ring + breadcrumbs
#   python3 fwtpm_caps.py --base 0x08000000   # a different FWTPM_XPORT build
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

# Live-mailbox base per FWTPM_XPORT build. The mailbox + console ring live
# here; the boot/trap breadcrumbs stay pinned in LIM (DBG_BASE).
XPORT_BASE = {
    "ddr_wcb":       0xC0000000,
    "lim_cached":    0x08000000,
    "ddr_noncached": 0x1400000000,
}
DBG_BASE = 0x08000000
SHM_LEN  = 0x2000
TIS_OFF  = 0x1040

MBOX_MAGIC = 0x46575449   # "FWTI"
TIS_MAGIC  = 0x57544953   # "WTIS"
DID_VID    = 0x50544657   # "WFTP" -- wolfTPM fTPM vendor id
XPORT_NAME = {1: "LIM_CACHED", 2: "DDR_NONCACHED", 3: "L1D_OFF", 4: "IHC",
              5: "DDR_WCB"}


def map_region(base):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        return mmap.mmap(fd, SHM_LEN, mmap.MAP_SHARED, mmap.PROT_READ,
                         offset=base)
    finally:
        os.close(fd)


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
    args = ap.parse_args()
    base = args.base if args.base is not None else XPORT_BASE[args.xport]

    m_live = map_region(base)
    m_dbg  = m_live if base == DBG_BASE else map_region(DBG_BASE)

    magic        = struct.unpack_from("<I", m_live, 0x00)[0]
    server_alive = struct.unpack_from("<I", m_live, 0x10)[0]
    xport_id     = struct.unpack_from("<I", m_live, 0x24)[0]
    tis_magic    = struct.unpack_from("<I", m_live, TIS_OFF + 0)[0]
    did_vid      = struct.unpack_from("<I", m_live, TIS_OFF + 104)[0]
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
    check("on-hart TPM2_GetRandom ok (live entropy, not RC_INITIALIZE)",
          "TPM2_GetRandom(" in caps and "TPM_RC_INITIALIZE" not in caps)

    if args.dump:
        print("\n--- full console ring ---")
        print(console)
        progress    = struct.unpack_from("<I", m_dbg, 0x14)[0]
        trap_marker = struct.unpack_from("<I", m_dbg, 0x20)[0]
        print("breadcrumbs @ 0x%08x: progress=0x%08x trap_marker=0x%08x"
              % (DBG_BASE, progress, trap_marker))

    ok = all(results)
    print("\nRESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
