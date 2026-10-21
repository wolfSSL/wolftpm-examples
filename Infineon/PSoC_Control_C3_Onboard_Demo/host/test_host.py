#!/usr/bin/env python3
"""Tests for the host logic that a demo run cannot reach on demand.

Run with `python3 test_host.py`. No test framework: the demo's only
dependencies are Flask and pyserial, and a booth machine should not need a
third to check its own code.

The short-signature case is here because it happens about once in five
hundred handshakes. Waiting for it is not a test.
"""
import base64
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import onboard                                       # noqa: E402
import server                                        # noqa: E402
import shot                                          # noqa: E402
import spdm_pubkey                                   # noqa: E402
import x509_lite                                     # noqa: E402

failed = []


def check(name, got, want):
    if got == want:
        print("  ok   %s" % name)
    else:
        print("  FAIL %s: got %r, wanted %r" % (name, got, want))
        failed.append(name)


print("board_signature")
# A 4627-byte ML-DSA-87 signature is what every good run carries.
whole = "ab" * 4627
check("whole signature accepted",
      server.board_signature({"bytes": 4627, "hex": whole}), whole)
# The real failure: six bytes lost on the console, the JSON still parses.
check("six bytes short rejected",
      server.board_signature({"bytes": 4627, "hex": "ab" * 4621}), None)
check("one byte short rejected",
      server.board_signature({"bytes": 4627, "hex": whole[:-2]}), None)
check("one hex character short rejected",
      server.board_signature({"bytes": 4627, "hex": whole[:-1]}), None)
check("longer than declared rejected",
      server.board_signature({"bytes": 4627, "hex": whole + "cd"}), None)
check("empty hex rejected",
      server.board_signature({"bytes": 4627, "hex": ""}), None)
check("missing hex rejected",
      server.board_signature({"bytes": 4627}), None)
# No declared length is not a reason to refuse a signature outright.
check("no length declared is taken as is",
      server.board_signature({"hex": whole}), whole)
check("unparsable length rejected",
      server.board_signature({"bytes": "lots", "hex": whole}), None)

print("is_event_fragment")
check("bootloader line kept",
      server.is_event_fragment("wolfBoot HAL Init"), False)
check("partition line kept",
      server.is_event_fragment(
          "Boot partition: 0x12008000 (sz 197664, ver 0x1, type 0x201)"),
      False)
check("signature line kept",
      server.is_event_fragment("Verifying signature...done"), False)
# The real splice: the tail of one event joined to the head of the next.
check("spliced event tail dropped",
      server.is_event_fragment(
          'GhtjB2kP3SSulljH5fMw"run.start","host":"psoc_c3'), True)
check("whole event dropped",
      server.is_event_fragment('{"event":"run.end"}'), True)

print("spdm_pubkey.coordinates")
# A P-384 TPMT_PUBLIC. The algorithm detail before the coordinates is
# variable length, so a buffer of the right size with the wrong shape is the
# case that matters: it yields a key that builds and flashes and only fails
# at the handshake.
x = bytes([0x11]) * 48
y = bytes([0x22]) * 48


def tpmt(alg=0x0023, policy=b"", sym=0x0010, scheme=b"\x00\x18\x00\x0c",
         curve=0x0004, kdf=0x0010, xlen=48, ylen=48, tail=b""):
    out = alg.to_bytes(2, "big") + b"\x00\x0c" + b"\x00\x04\x00\x72"
    out += len(policy).to_bytes(2, "big") + policy
    out += sym.to_bytes(2, "big") + scheme + curve.to_bytes(2, "big")
    out += kdf.to_bytes(2, "big")
    if kdf != 0x0010:
        out += b"\x00\x0c"          # details.anyKdf.hashAlg
    out += xlen.to_bytes(2, "big") + x + ylen.to_bytes(2, "big") + y
    return out + tail


good = tpmt()
check("120 bytes for the SLB9678 shape", len(good), 120)
check("X and Y extracted in order", spdm_pubkey.coordinates(good)[0], x + y)
check("96 bytes out", len(spdm_pubkey.coordinates(good)[0]), 96)
check("no reason given when accepted", spdm_pubkey.coordinates(good)[1], None)
# A NULL scheme makes the structure two bytes shorter. A fixed offset reads
# the wrong 96 bytes here and still returns a key.
check("NULL scheme still read correctly",
      spdm_pubkey.coordinates(tpmt(scheme=b"\x00\x10"))[0], x + y)
check("non-empty authPolicy still read correctly",
      spdm_pubkey.coordinates(tpmt(policy=bytes(48)))[0], x + y)
check("non-NULL KDF still read correctly",
      spdm_pubkey.coordinates(tpmt(kdf=0x0020,
                                   scheme=b"\x00\x10"))[0], x + y)
check("short buffer refused", spdm_pubkey.coordinates(good[:-1])[0], None)
check("empty refused", spdm_pubkey.coordinates(b"")[0], None)
check("wrong Y length refused", spdm_pubkey.coordinates(tpmt(ylen=32))[0],
      None)
check("wrong X length refused", spdm_pubkey.coordinates(tpmt(xlen=32))[0],
      None)
check("a non-ECC type refused", spdm_pubkey.coordinates(tpmt(alg=0x0001))[0],
      None)
check("a curve other than P-384 refused",
      spdm_pubkey.coordinates(tpmt(curve=0x0003))[0], None)
check("trailing bytes refused",
      spdm_pubkey.coordinates(tpmt(tail=b"\x00"))[0], None)
# The old parser indexed at a fixed offset, so any 120-byte buffer with 0x0030
# in the right place was accepted. That buffer must now be refused.
check("a same-size buffer that is not a TPMT_PUBLIC refused",
      spdm_pubkey.coordinates(bytes(range(22)) + x + b"\x00\x30" + y)[0],
      None)
check("a reason is given when refused",
      isinstance(spdm_pubkey.coordinates(b"")[1], str), True)

print("x509_lite")
# The parser reads attacker-adjacent data off the part, so what matters is
# that malformed input returns None rather than raising out of the act or
# inventing fields. The endorsement tab exercises the happy path against the
# five real certificates every time it runs.
check("empty input refused", x509_lite.parse(b""), None)
check("truncated SEQUENCE refused", x509_lite.parse(b"\x30\x82\xff\xff"), None)
check("not a certificate refused", x509_lite.parse(b"\x30\x03\x02\x01\x00"),
      None)
check("length past the buffer refused",
      x509_lite.parse(b"\x30\x05\x30\x03\x02\x7f\x00"), None)
check("random bytes refused", x509_lite.parse(bytes(range(64))), None)


print("endorsement key match")
# ek_record_from_der decides the "key matches" column, which is the whole
# claim of the endorsement tab. Both branches are reachable: the part ships
# RSA and ECC certificates side by side.


def der(tag, body):
    if len(body) < 0x80:
        return bytes([tag, len(body)]) + body
    n = len(body).to_bytes((len(body).bit_length() + 7) // 8, "big")
    return bytes([tag, 0x80 | len(n)]) + n + body


def oid(dotted):
    parts = [int(p) for p in dotted.split(".")]
    body = bytes([parts[0] * 40 + parts[1]])
    for value in parts[2:]:
        chunk = [value & 0x7F]
        value >>= 7
        while value:
            chunk.insert(0, (value & 0x7F) | 0x80)
            value >>= 7
        body += bytes(chunk)
    return der(0x06, body)


def cert(spki):
    """The smallest certificate x509_lite will describe. It reads the fields
    by position, so every one ahead of the key has to be there."""
    sigalg = der(0x30, oid("1.2.840.113549.1.1.11"))
    empty = der(0x30, b"")
    tbs = der(0x30, der(0xA0, der(0x02, b"\x02")) + der(0x02, b"\x01") +
              sigalg + empty + empty + empty + spki)
    return der(0x30, tbs + sigalg + der(0x03, b"\x00"))


def rsa_cert(modulus):
    key = der(0x30, der(0x02, modulus) + der(0x02, b"\x01\x00\x01"))
    return cert(der(0x30,
                    der(0x30, oid("1.2.840.113549.1.1.1") + der(0x05, b"")) +
                    der(0x03, b"\x00" + key)))


def ecc_cert(x, y):
    return cert(der(0x30,
                    der(0x30, oid("1.2.840.10045.2.1") +
                        oid("1.2.840.10045.3.1.7")) +
                    der(0x03, b"\x00\x04" + x + y)))


def record(der_bytes, pub):
    ev = {"index": "0x01C00002", "bytes": len(der_bytes),
          "b64": base64.b64encode(der_bytes).decode()}
    return server.ek_record_from_der(ev, {"0x01c00002": pub})


# A real RSA modulus has its top bit set, so DER prefixes a zero byte that
# the board's own report does not carry.
MOD = b"\xc1" + b"\x7f" * 255
check("RSA modulus parsed without the DER leading zero",
      x509_lite.parse(rsa_cert(b"\x00" + MOD)).get("rsa_modulus"), MOD)
check("RSA key size taken from the modulus",
      x509_lite.parse(rsa_cert(b"\x00" + MOD)).get("bits"), 2048)
check("RSA key matches",
      record(rsa_cert(b"\x00" + MOD), ("rsa", MOD)).get("key_match"), True)
check("RSA key with a padded board modulus still matches",
      record(rsa_cert(b"\x00" + MOD), ("rsa", b"\x00" + MOD)).get("key_match"),
      True)
check("RSA key mismatch reported",
      record(rsa_cert(b"\x00" + MOD), ("rsa", b"\x7f" + MOD[1:]))
      .get("key_match"), False)

EX = b"\x00" + b"\x11" * 31
EY = b"\x22" * 32
check("ECC coordinates compared as numbers",
      record(ecc_cert(EX, EY), ("ecc", EX.lstrip(b"\x00"), EY))
      .get("key_match"), True)
check("ECC coordinate mismatch reported",
      record(ecc_cert(EX, EY), ("ecc", EX, b"\x33" * 32)).get("key_match"),
      False)
# An RSA certificate paired with an ECC report proves nothing, and the ECC
# branch would read a coordinate the RSA report does not have.
check("key kind mismatch leaves the column empty",
      record(rsa_cert(b"\x00" + MOD), ("ecc", EX, EY)).get("key_match"), None)
check("ECC certificate with an RSA report leaves the column empty",
      record(ecc_cert(EX, EY), ("rsa", MOD)).get("key_match"), None)

print("shot websocket frames")


class FakeSock:
    """Hands out one recorded chunk per recv, as a real socket would."""

    def __init__(self, data):
        self.data = data

    def recv(self, _n):
        if not self.data:
            raise RuntimeError("no more data")
        chunk, self.data = self.data[:1], self.data[1:]
        return chunk[0]


def text_frame(payload, fin=True, cont=False):
    b0 = (0x80 if fin else 0x00) | (0x00 if cont else 0x01)
    return bytes([b0, len(payload)]) + payload


def read_one(chunks):
    ws = shot.Reader.__new__(shot.Reader)
    ws.sock = FakeSock(chunks)
    ws.buf = b""
    return ws.frame()


# A CDP reply big enough to be fragmented is the case that broke capture:
# the first piece parses as JSON only by accident, and usually not at all.
check("single frame returned whole",
      read_one([text_frame(b'{"id":1}')]), '{"id":1}')
check("fragmented message reassembled",
      read_one([text_frame(b'{"id":', fin=False),
                text_frame(b'1}', cont=True)]), '{"id":1}')
check("three fragments reassembled",
      read_one([text_frame(b'{"i', fin=False),
                text_frame(b'd":', fin=False, cont=True),
                text_frame(b'1}', cont=True)]), '{"id":1}')
# A ping can arrive between fragments and must not end the message.
check("ping between fragments ignored",
      read_one([text_frame(b'{"id":', fin=False),
                bytes([0x89, 0x00]),
                text_frame(b'1}', cont=True)]), '{"id":1}')


print("onboard terminal correlation")
check("the act's own terminal ends it",
      onboard._is_terminal('{"event":"run.end"}', "run.end"), True)
check("another act's terminal does not",
      onboard._is_terminal('{"event":"pcr.end"}', "run.end"), False)
check("any terminal ends an act that named none",
      onboard._is_terminal('{"event":"pcr.end"}', None), True)
check("an ordinary event never ends one",
      onboard._is_terminal('{"event":"sign.digest"}', "run.end"), False)
check("every act names a terminal",
      sorted(onboard.ACT_TERMINAL), sorted(onboard.COMMANDS))


class FakeBoard:
    """A board on the end of a serial port, for the reader alone.

    It answers a command byte with canned lines, and can start out still
    emitting the output of an act the host gave up on, which is the case
    the correlation exists for. reset_input_buffer does nothing on purpose:
    the leftovers that matter have not arrived yet.
    """

    def __init__(self, replies, leftover=()):
        self.replies = replies
        self.queue = [line.encode() + b"\n" for line in leftover]
        self.written = b""

    def read(self, _n):
        return self.queue.pop(0) if self.queue else b""

    def write(self, data):
        self.written += data
        for byte in data:
            for line in self.replies.get(bytes([byte]), ()):
                self.queue.append(line.encode() + b"\n")

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass


def read_act(act, board, stale=False):
    """Run one act against a FakeBoard without opening a serial port."""
    r = object.__new__(onboard.OnboardReader)
    r.timeout = 2.0
    r.idle = 0.2
    r.completed = False
    r.timed_out = None
    r.stale = stale
    r.ser = board
    lines = list(r.run(act))
    return lines, r.completed, r.stale


# A different act's terminal arriving mid-run is the host-restart case the
# reader is documented to expect.
board = FakeBoard({b"s": ['{"event":"pcr.end"}',
                          '{"event":"sign.digest"}',
                          '{"event":"run.end"}']})
lines, completed, stale = read_act("sign", board)
check("a stale pcr.end does not finish a sign", len(lines), 3)
check("the sign still completes on its own terminal", completed, True)
check("a completed act leaves nothing to resynchronise", stale, False)

# An act that never gets its terminal must say so, and must leave the next
# one knowing the board is still talking.
board = FakeBoard({b"s": ['{"event":"pcr.end"}']})
lines, completed, stale = read_act("sign", board)
check("a sign with no run.end does not complete", completed, False)
check("an incomplete act marks the port stale", stale, True)

# Two runs of the same act cannot be told apart by event name, so the
# leftovers have to be consumed before the next command goes out.
board = FakeBoard({b"s": ['{"event":"sign.digest"}',
                          '{"event":"run.end"}']},
                  leftover=['{"event":"run.end"}'])
lines, completed, stale = read_act("sign", board, stale=True)
check("the previous run's terminal is consumed first", len(lines), 2)
check("the new run completes on its own", completed, True)

print()
if failed:
    print("%d test(s) failed" % len(failed))
    sys.exit(1)
print("all tests passed")
