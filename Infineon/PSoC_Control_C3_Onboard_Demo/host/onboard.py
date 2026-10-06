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
    "ek": b"e",
    "spdm": b"d",
}

# An act is finished when one of these arrives. The board emits a specific
# one per act rather than a single generic marker, so they are all listened
# for rather than assuming which act is running.
TERMINAL_EVENTS = {"run.end", "pcr.end", "seal.end", "ek.end",
                   "spdm.end", "tpm.identity"}

# Long enough for ML-DSA key creation and signing, which together take a few
# seconds on this part and vary by more than a factor of two run to run.
DEFAULT_TIMEOUT = 30.0
# A gap this long with nothing arriving means the act is over or stuck.
DEFAULT_IDLE = 5.0

# The SPDM act runs a whole handshake and then unit tests inside the session,
# which takes about forty seconds with long quiet stretches while the part
# works. The defaults above would cut it off mid-handshake and release the
# port under it.
ACT_BUDGETS = {"spdm": (180.0, 30.0)}


class OnboardReader:
    """Serial link to the board's act dispatcher."""

    def __init__(self, port, baud=115200, timeout=DEFAULT_TIMEOUT,
                 idle=DEFAULT_IDLE):
        import serial  # imported here so --replay works without pyserial
        self.timeout = timeout
        self.idle = idle
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

    def run(self, act):
        """Trigger one act and yield its output lines as they arrive."""
        cmd = COMMANDS.get(act)
        if cmd is None:
            raise ValueError("unknown act %r" % act)
        budget = ACT_BUDGETS.get(act, (0.0, 0.0))
        self.drain()
        self.ser.write(cmd)
        self.ser.flush()
        yield from self._read_until_done(max(self.timeout, budget[0]),
                                         max(self.idle, budget[1]))

    def _read_until_done(self, timeout, idle):
        started = time.time()
        last = started
        buf = b""
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
                    if _is_terminal(line):
                        return
            elif (now - last) > idle:
                return
            if (now - started) > timeout:
                return


def _is_terminal(line):
    if not line.startswith("{"):
        return False
    try:
        name = json.loads(line).get("event")
    except ValueError:
        return False
    return name in TERMINAL_EVENTS


def replay(path, act=None):
    """Yield lines from a captured stream, for testing with no board."""
    with open(path, "r") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            yield line
            if _is_terminal(line):
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
