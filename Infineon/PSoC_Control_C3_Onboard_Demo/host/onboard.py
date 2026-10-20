#!/usr/bin/env python3
"""Read demo events from the board instead of spawning host programs.

When the acts run on the C3M6 the host no longer launches a wolfTPM example
and parses its output; it writes one command byte to the serial port and
reads back the same newline-delimited JSON. That makes this a drop-in source
of lines for the loop in server.py that already consumes a subprocess's
stdout.

A run ends when the board emits a terminating event, or when nothing arrives
for a while. Both matter: the board is a state machine that can be mid-act
when the host restarts, so a reader that waits forever for a sentinel will
hang on the first mismatched exchange.

Can be exercised without hardware by replaying a captured stream:

    python3 onboard.py --replay captured.jsonl s
    python3 onboard.py --port /dev/ttyACM2 s
"""
import argparse
import json
import sys
import time

# One per act, matching the switch in demo_acts.c.
COMMANDS = {
    "identity": b"i",
    "pcr": b"p",
    "pcr_extend": b"x",
    "pcr_reset": b"r",
    "sign": b"s",
    "tamper": b"t",
    # Upper case is the same act at ML-DSA-65 rather than 87.
    "sign65": b"S",
    "tamper65": b"T",
    "seal": b"l",
    # The same act, stopping at the release half.
    "seal_access": b"o",
    "ek": b"e",
    "spdm": b"d",
    # TLS identity held in the TPM: report the public key, and
    # sign one digest for a handshake running on this host.
    "boardpub": b"k",
    "boardsign": b"g",
}

# An act is finished when one of these arrives. Used only when the act is
# not known; a run started here waits for the one its own act owes.
TERMINAL_EVENTS = {"run.end", "pcr.end", "seal.end", "ek.end",
                   "spdm.end", "tpm.identity", "boardsign.end"}

# Which terminal event each act owes. A run that times out never emits one,
# and the caller has to send it or the page waits forever on a step that
# will not finish.
ACT_TERMINAL = {
    "identity": "tpm.identity",
    "pcr": "pcr.end",
    "pcr_extend": "pcr.end",
    "pcr_reset": "pcr.end",
    "sign": "run.end",
    "tamper": "run.end",
    "sign65": "run.end",
    "tamper65": "run.end",
    "seal": "seal.end",
    "seal_access": "seal.end",
    "ek": "ek.end",
    "spdm": "spdm.end",
    "boardpub": "boardsign.end",
    "boardsign": "boardsign.end",
}

# Long enough for ML-DSA key creation and signing, which together take a few
# seconds on this part and vary by more than a factor of two run to run.
DEFAULT_TIMEOUT = 30.0
# A gap this long with nothing arriving means the act is over or stuck.
DEFAULT_IDLE = 5.0

# The SPDM act runs a whole handshake and then unit tests inside the session,
# which takes about forty seconds with long quiet stretches while the part
# works. The defaults above would cut it off mid-handshake and release the
# port under it.
ACT_BUDGETS = {"spdm": (180.0, 30.0),
               # ML-DSA-87 is 4627 bytes, so the reply is about
               # 9 KB of hex: roughly a second of wire time on
               # top of the signing itself.
               "boardsign": (60.0, 20.0),
               "boardpub": (60.0, 20.0)}


class OnboardReader:
    """Serial link to the board's act dispatcher."""

    def __init__(self, port, baud=115200, timeout=DEFAULT_TIMEOUT,
                 idle=DEFAULT_IDLE):
        import serial  # imported here so --replay works without pyserial
        self.timeout = timeout
        self.idle = idle
        self.completed = False
        self.timed_out = None
        # An act that did not finish is still producing output, which the
        # next run must not read as its own.
        self.stale = False
        self.ser = serial.Serial(port, baud, timeout=0.2)
        # Leave DTR and RTS alone: on many USB-serial bridges they are wired
        # to the target's reset, and asserting them would restart the board
        # every time the demo connects.
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except (OSError, AttributeError):
            pass

    def close(self):
        try:
            self.ser.close()
        except OSError:
            pass

    def drain(self):
        """Discard anything already buffered, so a run starts clean."""
        try:
            self.ser.reset_input_buffer()
        except OSError:
            pass

    def quiesce(self, idle=None, limit=None):
        """Read and discard until the port has been silent for a while.

        Draining the buffer only removes what has already arrived. An act
        abandoned on a timeout keeps emitting, terminal event included, and
        two runs of the same act cannot be told apart by event name, so the
        leftovers have to be consumed before the next command goes out.
        """
        idle = self.idle if idle is None else idle
        limit = (idle * 4.0) if limit is None else limit
        end = time.time() + limit
        last = time.time()
        while time.time() < end:
            chunk = self.ser.read(4096)
            now = time.time()
            if chunk:
                last = now
            elif (now - last) > idle:
                return True
        return False

    def listen(self, secs):
        """Yield whatever arrives for a while, without sending anything.

        Used to catch the bootloader's output after a reset, which belongs to
        no act and would otherwise be discarded before anything reads it."""
        end = time.time() + secs
        buf = b""
        while time.time() < end:
            chunk = self.ser.read(4096)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                if line:
                    yield line

    def run(self, act, payload=b""):
        """Trigger one act and yield its output lines as they arrive.

        An act that takes an argument gets it straight after the command byte,
        newline terminated. The board reads exactly what it expects and times
        out rather than blocking, so a short write cannot wedge the dispatcher.

        Sets self.completed to say whether the act's terminal event arrived.
        A generator cannot hand a value back through a for loop, and the
        caller has to know: a timed-out run looks identical to a finished one
        from the outside, and reporting it as finished leaves the page on a
        step that never resolves.
        """
        cmd = COMMANDS.get(act)
        if cmd is None:
            raise ValueError("unknown act %r" % act)
        budget = ACT_BUDGETS.get(act, (0.0, 0.0))
        self.completed = False
        self.timed_out = None
        if self.stale:
            self.quiesce()
        self.drain()
        self.ser.write(cmd)
        if payload:
            self.ser.write(payload + b"\n")
        self.ser.flush()
        yield from self._read_until_done(max(self.timeout, budget[0]),
                                         max(self.idle, budget[1]),
                                         ACT_TERMINAL.get(act))

    def _read_until_done(self, timeout, idle, expect=None):
        started = time.time()
        last = started
        buf = b""
        self.stale = True
        while True:
            chunk = self.ser.read(4096)
            now = time.time()
            if chunk:
                last = now
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip()
                    if not line:
                        continue
                    yield line
                    if _is_terminal(line, expect):
                        self.completed = True
                        self.stale = False
                        return
            elif (now - last) > idle:
                self.timed_out = "idle"
                return
            if (now - started) > timeout:
                self.timed_out = "total"
                return


def _is_terminal(line, expect=None):
    """Whether this line ends the act.

    With an act's own terminal named, nothing else counts: a stale event
    left over from an abandoned run would otherwise end the current one,
    which reports a timed-out act as finished and leaves the rest of its
    output to be read as the next act's.
    """
    if not line.startswith("{"):
        return False
    try:
        name = json.loads(line).get("event")
    except ValueError:
        return False
    if expect is not None:
        return name == expect
    return name in TERMINAL_EVENTS


def replay(path, act=None):
    """Yield lines from a captured stream, for testing with no board."""
    expect = ACT_TERMINAL.get(act) if act else None
    with open(path, "r") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            yield line
            if _is_terminal(line, expect):
                return


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("act", choices=sorted(COMMANDS), help="which act to run")
    ap.add_argument("--port", help="serial port the board is on")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--replay", help="read from a captured stream instead")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    ap.add_argument("--raw", action="store_true",
                    help="print the board's lines verbatim, nothing elided")
    args = ap.parse_args()

    if args.replay:
        lines = replay(args.replay, args.act)
    elif args.port:
        lines = OnboardReader(args.port, args.baud, args.timeout).run(args.act)
    else:
        ap.error("one of --port or --replay is required")

    count = 0
    for line in lines:
        count += 1
        if args.raw:
            print(line)
            continue
        if line.startswith("{"):
            try:
                ev = json.loads(line)
            except ValueError:
                print("  (unparsed) %s" % line[:100])
                continue
            name = ev.pop("event", "?")
            # The signature is the one field not worth printing in full.
            if "b64" in ev:
                ev["b64"] = "<%d chars>" % len(ev["b64"])
            print("  %-18s %s" % (name, json.dumps(ev)))
        else:
            print("  (text) %s" % line[:100])
    print("%d lines" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
