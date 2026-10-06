"""Just enough X.509 to describe a TPM endorsement certificate.

The demo used to shell out to openssl for this. A Windows 11 laptop does not
have openssl, so on the booth machine every certificate came back unparsed and
the tab printed empty columns. Asking whoever runs the demo to install openssl
is not reasonable for a package, and pulling in a crypto library for five
fields is worse, so the handful of fields the page shows are read here.

This does not validate anything. It reads a certificate the TPM itself handed
us, to display it. Signature checking and chain building are not done, and
nothing here should be used as though they were.
"""

# Object identifiers, by what the page calls them.
_SIG_HASH = {
    "1.2.840.113549.1.1.11": "SHA256",
    "1.2.840.113549.1.1.12": "SHA384",
    "1.2.840.113549.1.1.13": "SHA512",
    "1.2.840.10045.4.3.2": "SHA256",
    "1.2.840.10045.4.3.3": "SHA384",
    "1.2.840.10045.4.3.4": "SHA512",
}
_PUBKEY_ALG = {
    "1.2.840.113549.1.1.1": "RSA",
    "1.2.840.10045.2.1": "ECC",
}
# Name and key size per curve. The size is not the coordinate length: P-521
# carries 66-byte coordinates for a 521-bit key, so deriving it from the
# point would report 528.
_CURVE = {
    "1.2.840.10045.3.1.7": ("prime256v1", 256),
    "1.3.132.0.34": ("secp384r1", 384),
    "1.3.132.0.35": ("secp521r1", 521),
}
_RDN = {"2.5.4.3": "CN", "2.5.4.10": "O", "2.5.4.11": "OU", "2.5.4.6": "C"}
_SAN_OID = "2.5.29.17"
_TPM_MANUFACTURER = "2.23.133.2.1"
_TPM_MODEL = "2.23.133.2.2"
_TPM_VERSION = "2.23.133.2.3"


class _Err(Exception):
    pass


def _tlv(buf, off):
    """One DER element: (tag, value_start, value_end, next_offset)."""
    if off + 2 > len(buf):
        raise _Err("truncated")
    tag = buf[off]
    n = buf[off + 1]
    off += 2
    if n & 0x80:
        count = n & 0x7F
        if count == 0 or count > 4 or off + count > len(buf):
            raise _Err("bad length")
        n = int.from_bytes(buf[off:off + count], "big")
        off += count
    end = off + n
    if end > len(buf):
        raise _Err("length past end")
    return tag, off, end, end


def _children(buf, start, end):
    off = start
    while off < end:
        tag, vs, ve, nxt = _tlv(buf, off)
        yield tag, vs, ve
        off = nxt


def _oid(buf, start, end):
    """Dotted form of an OID body."""
    if start >= end:
        return ""
    first = buf[start]
    parts = [str(first // 40), str(first % 40)]
    value = 0
    for b in buf[start + 1:end]:
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            parts.append(str(value))
            value = 0
    return ".".join(parts)


def _text(buf, start, end):
    return bytes(buf[start:end]).decode("utf-8", "replace").strip()


def _name(buf, start, end):
    """RDNSequence to {"CN": ..., "O": ...}, keeping only what is shown."""
    out = {}
    for _t, sv, se in _children(buf, start, end):          # RDN SETs
        for _t2, av, ae in _children(buf, sv, se):         # AttributeTypeValue
            kids = list(_children(buf, av, ae))
            if len(kids) < 2:
                continue
            key = _RDN.get(_oid(buf, kids[0][1], kids[0][2]))
            if key:
                out[key] = _text(buf, kids[1][1], kids[1][2])
    return out


def _san_tpm_fields(buf, start, end):
    """The TPM identity carried in a directoryName inside subjectAltName.

    The three OIDs hold the manufacturer, the part and the firmware version.
    A manufacturer reads as four ASCII bytes in hex, which is how the TCG
    specifies it and why it looks like "id:49465800" in a certificate dump.
    """
    out = {}
    for tag, dv, de in _children(buf, start, end):
        if tag != 0xA4:        # [4] directoryName
            continue
        for _t, sv, se in _children(buf, dv, de):          # RDNSequence
            for _t2, rv, re_ in _children(buf, sv, se):    # RDN SET
                for _t3, av, ae in _children(buf, rv, re_):
                    kids = list(_children(buf, av, ae))
                    if len(kids) < 2:
                        continue
                    oid = _oid(buf, kids[0][1], kids[0][2])
                    val = _text(buf, kids[1][1], kids[1][2])
                    if oid == _TPM_MANUFACTURER:
                        out["manufacturer"] = val
                    elif oid == _TPM_MODEL:
                        out["model"] = val
                    elif oid == _TPM_VERSION:
                        out["version"] = val
    return out


def parse(der):
    """Describe a certificate, or return None if it does not parse.

    Keys: serial, issuer{CN,O,...}, alg, bits, curve, hash, san{...},
    pubkey (the SubjectPublicKeyInfo BIT STRING contents).
    """
    try:
        return _parse(der)
    except (_Err, IndexError, ValueError):
        return None


def _parse(der):
    buf = memoryview(bytes(der))
    _t, cs, ce, _n = _tlv(buf, 0)                 # Certificate
    kids = list(_children(buf, cs, ce))
    if not kids:
        raise _Err("empty certificate")
    _tbs_tag, tbs_s, tbs_e = kids[0]
    out = {}

    # signatureAlgorithm sits beside tbsCertificate and names the digest.
    if len(kids) > 1:
        alg = list(_children(buf, kids[1][1], kids[1][2]))
        if alg:
            out["hash"] = _SIG_HASH.get(_oid(buf, alg[0][1], alg[0][2]))

    fields = list(_children(buf, tbs_s, tbs_e))
    i = 0
    if fields and fields[0][0] == 0xA0:           # [0] version, optional
        i = 1
    if i < len(fields):
        raw = bytes(buf[fields[i][1]:fields[i][2]]).lstrip(b"\x00")
        out["serial"] = "0x" + (raw.hex() or "0")
        i += 1
    i += 1                                        # signature, same as above
    if i < len(fields):
        out["issuer"] = _name(buf, fields[i][1], fields[i][2])
        i += 1
    i += 2                                        # validity, subject

    if i < len(fields):                           # subjectPublicKeyInfo
        spki = list(_children(buf, fields[i][1], fields[i][2]))
        if len(spki) >= 2:
            algid = list(_children(buf, spki[0][1], spki[0][2]))
            if algid:
                out["alg"] = _PUBKEY_ALG.get(_oid(buf, algid[0][1],
                                                  algid[0][2]))
            if len(algid) > 1:
                named = _CURVE.get(_oid(buf, algid[1][1], algid[1][2]))
                if named is not None:
                    out["curve"], out["bits"] = named
            # BIT STRING: first byte is the count of unused trailing bits.
            key = bytes(buf[spki[1][1]:spki[1][2]])
            if key[:1] == b"\x00":
                key = key[1:]
            out["pubkey"] = key
            if out.get("alg") == "ECC" and "bits" not in out:
                # Unnamed curve: fall back to the uncompressed point, which
                # is 0x04 then X and Y of equal length.
                if key[:1] == b"\x04":
                    out["bits"] = ((len(key) - 1) // 2) * 8
            elif out.get("alg") == "RSA":
                # RSAPublicKey ::= SEQUENCE { modulus INTEGER, exponent }
                try:
                    kv = memoryview(key)
                    _t2, rs, re2, _n2 = _tlv(kv, 0)
                    mod_s, mod_e = list(_children(kv, rs, re2))[0][1:3]
                    out["bits"] = len(
                        bytes(kv[mod_s:mod_e]).lstrip(b"\x00")) * 8
                except (_Err, IndexError):
                    pass
        i += 1

    for tag, ev, ee in _children(buf, tbs_s, tbs_e):
        if tag != 0xA3:                           # [3] extensions
            continue
        for _t, ls, le in _children(buf, ev, ee):          # SEQUENCE OF
            for _t2, xs, xe in _children(buf, ls, le):     # Extension
                kids2 = list(_children(buf, xs, xe))
                if len(kids2) < 2:
                    continue
                if _oid(buf, kids2[0][1], kids2[0][2]) != _SAN_OID:
                    continue
                # Last element is the OCTET STRING wrapping the real value.
                ov, oe = kids2[-1][1], kids2[-1][2]
                inner = list(_children(buf, ov, oe))
                if inner:
                    san = _san_tpm_fields(buf, inner[0][1], inner[0][2])
                    if san:
                        out["san"] = san
    return out
