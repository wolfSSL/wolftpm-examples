#!/usr/bin/env python3
"""Turn the SPDM act's discovery output into a pinnable responder key.

The SPDM act pins the public key of the part it expects to talk to, so a new
TPM module needs a new key. Build with the key empty, run the act, and it
prints what it read out of the part:

  {"event":"spdm.pubkey","bytes":120,"hex":"0023000C..."}

That is a TPMT_PUBLIC, not a raw point. This pulls the two 48-byte P-384
coordinates out of it and prints them in the form responder-key.txt wants.
Doing it by hand means counting bytes twice and getting a key that fails at
the handshake rather than at the parse, which is a bad afternoon.

  ./spdm_pubkey.py 0023000C...            the hex, or
  ./spdm_pubkey.py --json '{"event":...}' the whole event line
"""
import json
import re
import sys

COORD = 48
TPM_ALG_ECC = 0x0023
TPM_ALG_NULL = 0x0010
TPM_ALG_ECDAA = 0x001A
TPM_ECC_NIST_P384 = 0x0004


class _Short(Exception):
    """Ran off the end of the buffer."""


def _u16(raw, off):
    if off + 2 > len(raw):
        raise _Short()
    return int.from_bytes(raw[off:off + 2], "big"), off + 2


def coordinates(raw):
    """The X and Y of a TPMT_PUBLIC, or a reason it cannot be read.

    The structure is walked rather than indexed at a fixed offset. The
    algorithm detail before the coordinates is variable length - a non-NULL
    scheme or KDF, or a non-empty authPolicy, each move them - so a fixed
    offset silently reads the wrong 96 bytes out of a structure that is the
    right size, and that only fails later, during the handshake.
    """
    try:
        alg, off = _u16(raw, 0)
        if alg != TPM_ALG_ECC:
            return None, ("type is 0x%04x, not TPM_ALG_ECC (0x%04x)"
                          % (alg, TPM_ALG_ECC))
        _nameAlg, off = _u16(raw, off)
        off += 4                                  # objectAttributes
        policySz, off = _u16(raw, off)
        off += policySz                           # authPolicy
        sym, off = _u16(raw, off)                 # symmetric.algorithm
        if sym != TPM_ALG_NULL:
            off += 4                              # keyBits + mode
        scheme, off = _u16(raw, off)
        if scheme != TPM_ALG_NULL:
            off += 2                              # details.anySig.hashAlg
            if scheme == TPM_ALG_ECDAA:
                off += 2                          # details.ecdaa.count
        curve, off = _u16(raw, off)
        if curve != TPM_ECC_NIST_P384:
            return None, ("curve is 0x%04x, not P-384 (0x%04x)"
                          % (curve, TPM_ECC_NIST_P384))
        kdf, off = _u16(raw, off)
        if kdf != TPM_ALG_NULL:
            off += 2                              # details.anyKdf.hashAlg
        xlen, off = _u16(raw, off)
        if xlen != COORD:
            return None, ("the X coordinate says it is %d bytes, not %d"
                          % (xlen, COORD))
        x = raw[off:off + COORD]
        off += COORD
        ylen, off = _u16(raw, off)
        if ylen != COORD:
            return None, ("the Y coordinate says it is %d bytes, not %d"
                          % (ylen, COORD))
        y = raw[off:off + COORD]
        off += COORD
    except _Short:
        return None, ("the TPMT_PUBLIC ends early; got %d bytes"
                      % len(raw))
    if len(x) != COORD or len(y) != COORD:
        return None, ("the TPMT_PUBLIC ends early; got %d bytes"
                      % len(raw))
    if off != len(raw):
        return None, ("%d bytes left over after the public key - this is "
                      "not a bare TPMT_PUBLIC" % (len(raw) - off))
    return x + y, None


def main():
    args = [a for a in sys.argv[1:] if a != "--json"]
    if not args:
        sys.stderr.write(__doc__)
        return 2
    text = args[0]

    m = re.search(r'"hex"\s*:\s*"([0-9A-Fa-f]+)"', text)
    if m is not None:
        text = m.group(1)
    text = re.sub(r"[^0-9A-Fa-f]", "", text)
    try:
        raw = bytes.fromhex(text)
    except ValueError as exc:
        sys.stderr.write("not hex: %s\n" % exc)
        return 1

    key, why = coordinates(raw)
    if key is None:
        sys.stderr.write("%s\n" % why)
        return 1

    body = ",".join("0x%02x" % b for b in key)
    print("# %d bytes, X then Y. Save as responder-key.txt beside the"
          " Makefile." % len(key))
    print(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
