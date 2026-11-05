#!/usr/bin/env python3
"""Read the MEASURE=1 instrumentation (g_meas) from a running fwTPM over SWD.

Usage:
    python3 fwtpm_measure.py [--sn ST-LINK_SERIAL] [--elf fwtpm_stm32h5.elf]
                             [--wolftpm ../../../wolftpm]

Attaches with STM32_Programmer_CLI in hotplug mode (no reset), uploads
g_meas, and prints the totals and the per-command log. Run it after the host
test sequence; the log is a ring of the last MEAS_MAX_LOG commands, while the
peak totals cover everything since reset.

Copyright (C) 2006-2025 wolfSSL Inc.
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

HDR_FIELDS = ["magic", "ctxSize", "count", "stackPeak", "heapPeak",
              "sbrkPeak", "heapCur", "allocFail", "stackLimit", "heapLimit",
              "heapPeakAll", "allocFailSz"]
HDR_FMT = "<%dI" % len(HDR_FIELDS)
ENTRY_FMT = "<8I"
MEAS_MAGIC = 0x4D454153
CPU_HZ = 250000000


def cc_names(wolftpm_dir):
    names = {0: "FWTPM_Init (boot)"}
    path = os.path.join(wolftpm_dir, "wolftpm", "tpm2.h")
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                m = re.match(r"\s*TPM_CC_(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", line)
                if m:
                    names.setdefault(int(m.group(2), 16), m.group(1))
    return names


def find_symbol(elf, name):
    out = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True,
                         text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == name:
            return int(parts[0], 16), int(parts[1], 16)
    sys.exit("%s not found in %s (built with MEASURE=1?)" % (name, elf))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sn", help="ST-LINK serial (required with several probes)")
    ap.add_argument("--elf", default="fwtpm_stm32h5.elf")
    ap.add_argument("--wolftpm", default="../../../wolftpm")
    args = ap.parse_args()

    addr, size = find_symbol(args.elf, "g_meas")
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "g_meas.bin")
        conn = ["port=swd", "mode=HOTPLUG"]
        if args.sn:
            conn.append("sn=" + args.sn)
        subprocess.run(["STM32_Programmer_CLI", "-c"] + conn +
                       ["-u", hex(addr), str(size), out],
                       capture_output=True, text=True)
        if not os.path.exists(out):
            sys.exit("SWD upload of g_meas failed")
        with open(out, "rb") as f:
            data = f.read()

    hdr = dict(zip(HDR_FIELDS, struct.unpack_from(HDR_FMT, data, 0)))
    if hdr["magic"] != MEAS_MAGIC:
        sys.exit("bad magic 0x%08x" % hdr["magic"])
    off = struct.calcsize(HDR_FMT)
    esz = struct.calcsize(ENTRY_FMT)
    max_log = (size - off) // esz
    names = cc_names(args.wolftpm)

    print("FWTPM_CTX %d B, commands %d, stack peak %d / %d B, "
          "wolfSSL heap peak %d B, sbrk peak %d / %d B, alloc failures %d"
          % (hdr["ctxSize"], hdr["count"], hdr["stackPeak"],
             hdr["stackLimit"], hdr["heapPeakAll"], hdr["sbrkPeak"],
             hdr["heapLimit"], hdr["allocFail"]))
    if hdr["allocFail"]:
        print("last failed allocation: %d B" % hdr["allocFailSz"])

    count = hdr["count"]
    first = max(0, count - max_log)
    print("%5s %-28s %10s %7s %7s %8s %10s" %
          ("#", "command", "ms", "stack", "heap", "cmdSz", "rc"))
    for i in range(first, count):
        cc, cyc, ms, stk, heap, sbrk, rc, csz = struct.unpack_from(
            ENTRY_FMT, data, off + (i % max_log) * esz)
        t = ms if ms > 15000 else cyc * 1000.0 / CPU_HZ
        rcs = "-" if cc == 0 else "0x%x" % rc
        print("%5d %-28s %10.2f %7d %7d %8d %10s" %
              (i, names.get(cc, "0x%x" % cc), t, stk, heap, csz, rcs))


if __name__ == "__main__":
    main()
