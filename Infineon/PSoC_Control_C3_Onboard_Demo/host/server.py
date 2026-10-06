#!/usr/bin/env python3
"""OktoberTech 2026 demo server.

Runs the wolfTPM Hash-ML-DSA example on the attached TPM and streams its JSON
events to a browser over Server-Sent Events.

SSE rather than websockets on purpose: the event flow is one-way, Flask is
already installed system-wide on the bench Pi, and a booth demo should have as
few moving parts as possible. No pip install, no CDN, works with no network.

The server is deliberately agnostic about which TPM is fitted -- it identifies
the part at startup by running the `caps` example and parsing what it prints,
then only offers the parameter sets that part can actually do. That matters:
on one pre-production part, ML-DSA-65 reproducibly returns TPM_RC_FAILURE and
leaves the TPM in failure mode until the reset line is toggled, so a visitor
picking it from a dropdown would take the demo down for everyone behind them.
"""

try:
    import fcntl
except ImportError:          # Windows has no flock
    fcntl = None
import json
import os
import queue
import re
import shutil
import socket
import subprocess
import sys
import threading
import time

from flask import Flask, Response, jsonify, render_template, request

APP = Flask(__name__)

# Where the wolfTPM build lives. Override with env for a different worktree.
TPM_BUILD_DIR = os.environ.get(
    "TPM_BUILD_DIR", os.path.expanduser("~/wolfTPM"))
EXAMPLE = os.path.join(TPM_BUILD_DIR, "examples/pqc/mldsa_host_verify")
EK_CERTS = os.path.join(TPM_BUILD_DIR, "examples/endorsement/get_ek_certs")
SEAL_PCR = os.path.join(TPM_BUILD_DIR, "examples/seal/seal_pcr")
PCR_EXTEND = os.path.join(TPM_BUILD_DIR, "examples/pcr/extend")
PCR_RESET = os.path.join(TPM_BUILD_DIR, "examples/pcr/reset")
PCR_READ = os.path.join(TPM_BUILD_DIR, "examples/pcr/read_pcr")
PCR_COUNT = 24
SEAL_PCR_INDEX = os.environ.get("TPM_SEAL_PCR", "16")
SEAL_SECRET = os.environ.get("TPM_SEAL_SECRET", "OktoberTech2026")
SEAL_BLOB = os.environ.get("TPM_SEAL_BLOB", "/tmp/demo-sealblob.bin")
SEAL_WAIT_SEC = float(os.environ.get("TPM_SEAL_WAIT", "30"))

# SPDM. Two different things are shown, and they need two different trees:
# the fitted part is probed with the hardware build, while the working secured
# session runs against wolfTPM's own firmware TPM responder, which needs a
# separate build (--enable-fwtpm --enable-spdm --enable-tcg --enable-psk plus
# --enable-nuvoton --enable-nations, because the CLI dispatch is gated on the
# vendor macros even for the fwTPM runs).
SPDM_DIR = os.environ.get("TPM_SPDM_DIR",
                          os.path.expanduser("~/wolftpm-fwtpm-spdm"))
SPDM_MODE = os.environ.get("TPM_SPDM_MODE", "fwtpm-tcg")
SPDM_TIMEOUT = int(os.environ.get("TPM_SPDM_TIMEOUT", "300"))

# Post-quantum TLS. The fitted part cannot hold an ML-DSA TLS identity key -
# TLS CertificateVerify needs pure ML-DSA and the silicon implements only the
# pre-hash form, so CreatePrimary returns TPM_RC_TYPE. The handshake therefore
# runs against wolfTPM's firmware TPM, which is a socket TPM and so never
# touches the SPI bus: this act needs no bus lock and cannot disturb the
# attract loop.
TLS_DIR = os.environ.get("TPM_TLS_DIR", os.path.expanduser("~/wolftpm-tls-pqc"))
TLS_MLDSA = os.environ.get("TPM_TLS_MLDSA", "65")
TLS_GROUP = os.environ.get("TPM_TLS_GROUP", "ML_KEM_768")
TLS_PORT = int(os.environ.get("TPM_TLS_PORT", "11500"))
# Off the 2321/2322 default on purpose: any TPM simulator on the bench binds
# those, and the act would then silently talk to one with no ML-DSA support.
TLS_FWTPM_PORT = int(os.environ.get("TPM_TLS_FWTPM_PORT", "2381"))
TLS_FWTPM_PLAT_PORT = int(os.environ.get("TPM_TLS_FWTPM_PLAT_PORT", "2382"))
CAPS = os.path.join(TPM_BUILD_DIR, "examples/wrap/caps")

EXE = ".exe" if sys.platform == "win32" else ""


def tls_tool(rel):
    """Where a program of the PQ TLS tree ended up.

    The two ways of building wolfTPM put their output in different places:
    autotools leaves each program beside its source, CMake collects them in a
    build directory and Visual Studio adds a configuration below that. Look
    in all of them rather than making the layout part of the instructions.
    """
    name = os.path.basename(rel) + EXE
    for cand in (os.path.join(TLS_DIR, os.path.dirname(rel), name),
                 os.path.join(TLS_DIR, "build", name),
                 os.path.join(TLS_DIR, "build", "Release", name),
                 os.path.join(TLS_DIR, "build", "Debug", name)):
        if os.path.exists(cand):
            return cand
    return os.path.join(TLS_DIR, os.path.dirname(rel), name)

# On-board mode. With TPM_ONBOARD_PORT set the acts run on the MCU and this
# server only writes a command byte and republishes the events it reads back,
# instead of launching a wolfTPM example per act. Everything else - routes,
# attract loop, templates, the bus lock - is unchanged, because the board
# emits the same event grammar the examples do.
# 8080 is not always free - the booth machine already runs something on it.
HTTP_PORT = int(os.environ.get("TPM_HTTP_PORT", "8080"))
ONBOARD_PORT = os.environ.get("TPM_ONBOARD_PORT") or None


SEGGER_VID = 0x1366
# The probe soldered to the kit this demo was built against. A development
# bench has several J-Links on it at once and picking the wrong one looks
# exactly like a dead board, so a known serial always wins.
BOARD_SERIAL = os.environ.get("TPM_BOARD_SERIAL", "000591238971")


def find_board_ports():
    """Candidate consoles for the board, best guess first.

    A packaged demo lands on whatever laptop is in the room and the port is
    never the same twice, so a hardcoded COM3 is a support call. The board
    presents a SEGGER J-Link CDC UART.

    Never guesses between candidates. On a visitor's laptop there is one; on
    a bench there may be a dozen J-Links belonging to other boards, and
    silently attaching to one of those wastes an afternoon.
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        return None

    ports = list(list_ports.comports())
    named, other = [], []
    for p in ports:
        if p.vid != SEGGER_VID:
            continue
        if p.serial_number and BOARD_SERIAL and \
                BOARD_SERIAL.lstrip("0") in p.serial_number.lstrip("0"):
            named.append(p.device)
        else:
            other.append(p.device)
    return named + other


# Candidates only. Which one is actually the board is settled by asking it,
# in probe_identity, because a J-Link on the bench may belong to anything.
ONBOARD_CANDIDATES = []
if ONBOARD_PORT is None and os.environ.get("TPM_ONBOARD_AUTO", "1") != "0":
    ONBOARD_CANDIDATES = find_board_ports()
    if ONBOARD_CANDIDATES:
        ONBOARD_PORT = ONBOARD_CANDIDATES[0]
ONBOARD_BAUD = int(os.environ.get("TPM_ONBOARD_BAUD", "115200"))
ONBOARD_OPEN_WAIT = float(os.environ.get("TPM_ONBOARD_OPEN_WAIT", "20"))
ONBOARD_PROBE_TRIES = int(os.environ.get("TPM_ONBOARD_PROBE_TRIES", "4"))
ONBOARD_PROBE_WAIT = float(os.environ.get("TPM_ONBOARD_PROBE_WAIT", "6"))
PORT_LOG = os.environ.get("TPM_PORT_LOG", "") not in ("", "0")
LIB_PATH = ":".join([p for p in [
    os.path.join(TPM_BUILD_DIR, "src/.libs"),
    os.environ.get("WOLFSSL_LIB_DIR", ""),
    "/usr/local/lib",
] if p])

# A healthy ML-DSA-87 run is about three seconds and a wedged part fails fast.
# The case worth bounding is a part that is unpowered or on the wrong chip
# select, which produces no output at all rather than an error.
RUN_TIMEOUT = int(os.environ.get("TPM_RUN_TIMEOUT", "90"))
PROBE_TIMEOUT = int(os.environ.get("TPM_PROBE_TIMEOUT", "60"))

# TPM_!RESET on the Infineon Odin carrier is GPIO4 on the Pi 40-pin header.
# Toggling it is the only way out of TPM failure mode. Runs unprivileged --
# the bench account is in the `gpio` group.
RESET_GPIO = os.environ.get("TPM_RESET_GPIO", "4")
# How the TPM's reset line is reached. On the Pi carrier it is a GPIO driven
# with pinctrl; on the C3M6 the TPM's reset hangs off the board's XRES, so the
# debug probe drives it instead. Set TPM_RESET_CMD to that command and it is
# used in place of the GPIO.
RESET_CMD = os.environ.get("TPM_RESET_CMD", "")
# How long to listen after a reset. The bootloader has verified the image
# and handed over well inside this.
BOOT_CAPTURE_SEC = float(os.environ.get("TPM_BOOT_CAPTURE", "8"))

# Bus lock. Nothing in the stack arbitrates the SPI bus, and a second process
# talking to the part while a run is in flight is enough on its own to return
# TPM_RC_FAILURE and leave it not answering until a power cycle. Both this
# server and the tpm-cli wrapper take an exclusive flock on this path around
# every TPM access, so a command-line run started during a booth session
# queues behind the current cycle instead of corrupting it.
BUS_LOCK = os.environ.get("TPM_BUS_LOCK", "/tmp/tpm-bus.lock")
BUS_LOCK_WAIT = float(os.environ.get("TPM_BUS_LOCK_WAIT", "120"))

# Attract mode. A booth screen has to tell its story to someone who walks up
# mid-cycle with nobody standing next to it, so the demo runs itself and a
# visitor interrupts rather than starts. Server-side rather than in the page:
# two browsers open on the same Pi would otherwise both fire runs.
ATTRACT = os.environ.get("TPM_ATTRACT", "1") != "0"
ATTRACT_IDLE_SEC = float(os.environ.get("TPM_ATTRACT_IDLE", "25"))
# Dwell after each automatic cycle. Without it the loop restarts the moment a
# run ends and the screen is never still long enough to read - the result card
# and the mosaic both need a few seconds of nothing happening to land.
ATTRACT_HOLD_SEC = float(os.environ.get("TPM_ATTRACT_HOLD", "7"))

# Parameter sets each manufacturer's fitted part can actually be asked for.
# Anything unrecognised, or a failed probe, falls back to ML-DSA-87 only:
# 87 is the one set proven on every part that has been on this bench, so the
# safe default is the restrictive one, not the permissive one.
SAFE_PARAM_SETS = [87]
ALLOWED_PARAM_SETS = {
    # Both verified on FW45.91 with the bus quiet, 25 Sep 2026: 65 gives a
    # 1952 byte key and a 3309 byte signature, 87 gives 2592 and 4627, and
    # the part is healthy after either. The older "65 wedges the part" note
    # was wrong; what wedges it is on-TPM TPM2_VerifySignature, which no
    # longer happens here because verification is done by wolfCrypt on the
    # host. ML-DSA-44 is genuinely absent on this firmware.
    "IFX":  [87, 65],
    "SEAL": [87, 65, 44],    # SealSQ QVault: all three verified 22 Aug 2026
}

# Manual override, for a bench where the probe cannot run. Empty means probe.
TPM_LABEL_OVERRIDE = os.environ.get("TPM_LABEL", "")

# One queue per connected browser. A booth screen is usually one client, but
# the presenter's laptop often joins too.
_subscribers = []
_subscribers_lock = threading.Lock()

# Guards against two clicks launching two concurrent TPM runs; the TPM is a
# single resource and concurrent sessions produce confusing output. The
# identity probe and the reset take it too -- all three drive the same chip.
_run_lock = threading.Lock()
_attract_on = ATTRACT
_last_touch = 0.0
_attract_next = 0.0
_attract_idx = 0
_ek_cache = None
_ek_lock = threading.Lock()

# Cached result of the last identity probe.
_identity = {"state": "probing", "label": "probing…",
             "param_sets": SAFE_PARAM_SETS}
_identity_lock = threading.Lock()


def broadcast(event):
    """Push one event dict to every connected browser."""
    dead = []
    with _subscribers_lock:
        for q in _subscribers:
            try:
                q.put_nowait(event)
            except queue.Full:
                dead.append(q)
        for q in dead:
            _subscribers.remove(q)


_nolock_bus = threading.Lock()


class BusLock(object):
    """Exclusive advisory lock over the TPM bus, shared with tpm-cli.

    flock is released automatically if the holder dies, so a crashed run
    cannot wedge the booth."""

    def __init__(self):
        self.fh = None

    def __enter__(self):
        # Without flock the cross-process guarantee is gone, so fall back to
        # a lock within this process. That is the whole story on Windows,
        # where the booth runs the server alone and there is no tpm-cli to
        # collide with; on the bench it would not be enough.
        if fcntl is None:
            _nolock_bus.acquire()
            return self
        deadline = time.time() + BUS_LOCK_WAIT
        self.fh = open(BUS_LOCK, "a+")
        while True:
            try:
                fcntl.flock(self.fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except (IOError, OSError):
                if time.time() >= deadline:
                    self.fh.close()
                    self.fh = None
                    raise RuntimeError(
                        "TPM bus busy for %.0fs - another process is holding "
                        "it (see tpm-cli)" % BUS_LOCK_WAIT)
                time.sleep(0.25)

    def __exit__(self, *exc):
        if fcntl is None:
            _nolock_bus.release()
            return
        if self.fh is not None:
            try:
                fcntl.flock(self.fh, fcntl.LOCK_UN)
            finally:
                self.fh.close()
                self.fh = None
        return False


def looks_wedged(text):
    """Conditions a reset line pulse actually fixes.

    TPM_RC_FAILURE is the part in failure mode. TPM_RC_OBJECT_MEMORY matters
    just as much at a booth and used to be missed: transient handles are
    volatile but they are NOT released when a process is killed, so a few
    interrupted examples exhaust the object slots and several acts start
    failing at once - signing still works, but the ECDSA comparison, the
    sealed secret and anything else needing a fresh primary all break. A
    reset clears transient objects and leaves persistent ones alone.
    """
    return ("TPM_RC_FAILURE" in text or "0x101" in text
            or "TPM_RC_OBJECT_MEMORY" in text or "0x902" in text)


def child_env():
    """Environment for the wolfTPM examples, with the right library path."""
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = LIB_PATH
    return env


# One serial handle for the life of the server. Opening and closing per act
# looked tidier but Windows releases a serial handle lazily - measured at
# four seconds routinely and over twenty on occasion - so the next act found
# the port still busy and failed in front of a visitor. Access is already
# serialized by BusLock, so holding the handle open costs nothing and takes
# the whole failure mode away.
_reader = [None]


def _drop_reader():
    r, _reader[0] = _reader[0], None
    if r is not None:
        try:
            r.close()
        except OSError:
            pass
        _port_log("close")


def _get_reader(onboard):
    """The shared reader, opened on first use. Caller holds BusLock."""
    if _reader[0] is None:
        _reader[0] = _open_onboard_reader(onboard)
    return _reader[0]


def _port_log(what, detail=""):
    """Who held the board's port and when. Diagnostic for the intermittent
    access-denied between acts; quiet unless TPM_PORT_LOG is set."""
    if not PORT_LOG:
        return
    sys.stderr.write("[port] %.3f %-12s %-10s %s\n" % (
        time.time(), threading.current_thread().name, what, detail))
    sys.stderr.flush()


def _open_onboard_reader(onboard):
    """Open the board's console, retrying a short while.

    A server killed mid-run leaves its serial handle held until the OS
    finishes tearing the process down, and the next open fails with access
    denied for a second or two. Failing the act outright for that would
    black out the booth over something that clears itself.
    """
    last = None
    deadline = time.time() + ONBOARD_OPEN_WAIT
    tries = 0
    while True:
        try:
            r = onboard.OnboardReader(ONBOARD_PORT, ONBOARD_BAUD)
            _port_log("open", "tries=%d" % (tries + 1))
            return r
        except OSError as exc:
            last = exc
            tries += 1
            if time.time() >= deadline:
                _port_log("open-giveup", "tries=%d %s" % (tries, exc))
                raise last
            time.sleep(0.5)


def onboard_identity():
    """Ask the board what TPM it has. Shape matches the parsed caps output."""
    bad = {"state": "error", "label": "no TPM detected",
           "param_sets": SAFE_PARAM_SETS,
           "message": "board did not report an identity"}
    try:
        import onboard
    except ImportError as exc:
        return dict(bad, message="onboard reader: %s" % exc)

    try:
        with BusLock():
            reader = _get_reader(onboard)
            try:
                for line in reader.run("identity"):
                    if not line.startswith("{"):
                        continue
                    try:
                        ev = json.loads(line)
                    except ValueError:
                        continue
                    if ev.get("event") != "tpm.identity":
                        continue
                    vendor = ev.get("vendor", "")
                    return {"state": "ok", "mfg": ev.get("mfg", ""),
                            "vendor": vendor, "fw": ev.get("fw", ""),
                            "param_sets": ALLOWED_PARAM_SETS.get(
                                ev.get("mfg", ""), SAFE_PARAM_SETS),
                            "label": "%s - FW %s  (on %s)"
                                     % (vendor, ev.get("fw", ""),
                                        ev.get("on", "board"))}
            except OSError:
                _drop_reader()
                raise
    except (OSError, RuntimeError) as exc:
        return dict(bad, message="cannot open %s: %s" % (ONBOARD_PORT, exc))
    return bad


_act_count = [0]
_act_count_lock = threading.Lock()


def act_in_flight():
    with _act_count_lock:
        return _act_count[0] > 0


def onboard_run(act, transform=None):
    """Run an act on the board and republish its events.

    When the acts run on the MCU there is nothing to launch: the host writes
    one command byte and reads back the same JSON it used to parse out of a
    subprocess, so this replaces the spawn rather than wrapping it.

    A transform may rewrite an event, or return None to swallow it after
    broadcasting something of its own. The board reports raw material the
    page does not consume directly - one PCR at a time, a certificate as
    DER - and that is where it is turned into what the page expects.
    """
    if ONBOARD_PORT is None:
        return False
    try:
        import onboard
    except ImportError as exc:
        broadcast({"event": "error", "message": "onboard reader: %s" % exc})
        return True

    with _act_count_lock:
        _act_count[0] += 1
    try:
        return _onboard_run_locked(onboard, act, transform)
    finally:
        with _act_count_lock:
            _act_count[0] -= 1


def _onboard_run_locked(onboard, act, transform):
    with BusLock():
        try:
            reader = _get_reader(onboard)
        except (OSError, ImportError) as exc:
            broadcast({"event": "error",
                       "message": "cannot open %s: %s" % (ONBOARD_PORT, exc)})
            return True
        try:
            for line in reader.run(act):
                if not line.startswith("{"):
                    # Anything the board prints that is not an event: the
                    # bootloader's output, and wolfTPM's when a debug build
                    # is flashed. The page shows it on its own tab.
                    broadcast({"event": "console", "line": line})
                    continue
                try:
                    ev = json.loads(line)
                except ValueError:
                    continue
                if transform is not None:
                    ev = transform(ev)
                    if ev is None:
                        continue
                broadcast(ev)
        except OSError as exc:
            # A link that has gone away stays broken until it is reopened,
            # so do not keep handing the same dead handle to the next act.
            _drop_reader()
            broadcast({"event": "error",
                       "message": "board link lost: %s" % exc})
    return True


class OnboardPcrTable(object):
    """Collect the board's per-PCR events into the one table the page draws.

    Reading a PCR at a time is what the board can stream; the page wants a
    single snapshot so it can lay the banks out side by side.
    """

    def __init__(self, action):
        self.action = action
        self.bank = "SHA256"
        self.vals = []

    def __call__(self, ev):
        name = ev.get("event")
        if name == "pcr.begin":
            self.bank = ev.get("bank", self.bank)
            self.vals = []
        elif name == "pcr.value":
            self.vals.append({"index": ev.get("index"),
                              "digest": ev.get("digest", "")})
            return None
        elif name == "pcr.end":
            broadcast({"event": "pcr.table",
                       "banks": [{"name": self.bank, "allocated": True}],
                       "pcrs": self.vals,
                       "touched": int(SEAL_PCR_INDEX)
                       if self.action in ("extend", "reset") else -1})
        return ev


def ek_record_from_der(ev, pubkeys=None):
    """Build a row from the DER the board read out of NV.

    The host used to get these fields from get_ek_certs, which cannot run
    while the board owns the bus, so they come from the certificate itself
    instead. Parsed in-process rather than by shelling out to openssl, which
    is not present on a Windows machine and left every row empty there.
    """
    import base64
    import x509_lite

    rec = {"handle": ev.get("index", ""),
           "cert_bytes": ev.get("bytes", 0),
           "parsed": False}
    try:
        der = base64.b64decode(ev.get("b64", ""))
    except (ValueError, TypeError):
        return rec

    info = x509_lite.parse(der)
    if info is None:
        return rec

    rec["parsed"] = True
    for key in ("serial", "alg", "bits", "curve", "hash"):
        if info.get(key) is not None:
            rec[key] = info[key]
    if info.get("issuer", {}).get("CN"):
        rec["issuer_cn"] = info["issuer"]["CN"]
    if info.get("san"):
        rec["san"] = info["san"]
    # Pre-production parts carry test-CA certificates, which are deliberately
    # absent from the production roots; see the note in parse_ek_certs.
    rec["chain"] = "untrusted"

    # key_match stays absent unless the board reported the endorsement key
    # it derived. Proving the certificate belongs to this chip needs that
    # key, and reporting an unproven match as "NO" would say the opposite of
    # what is true.
    pub = (pubkeys or {}).get(str(rec.get("handle", "")).lower())
    if pub is not None and info.get("pubkey"):
        rec["key_match"] = (info["pubkey"] == pub)
    return rec


class OnboardEkCerts(object):
    """Turn the board's raw certificates into the rows the page expects."""

    def __init__(self):
        self.count = 0
        self.pubkeys = {}

    def __call__(self, ev):
        name = ev.get("event")
        if name == "ek.pub":
            # The endorsement key the TPM derived just now, as the
            # uncompressed point a certificate carries, so the two can be
            # compared byte for byte.
            try:
                point = (b"\x04" + bytes.fromhex(ev.get("x", ""))
                         + bytes.fromhex(ev.get("y", "")))
            except ValueError:
                return None
            self.pubkeys[str(ev.get("index", "")).lower()] = point
            return None
        if name == "ek.cert":
            self.count += 1
            return dict(ek_record_from_der(ev, self.pubkeys), event="ek.cert")
        if name == "ek.end":
            return {"event": "ek.end",
                    "count": ev.get("found", self.count)}
        return ev


def get_identity():
    """Current cached TPM identity, as a plain dict."""
    with _identity_lock:
        return dict(_identity)


def set_identity(ident):
    with _identity_lock:
        _identity.clear()
        _identity.update(ident)


def parse_caps(text):
    """Pull manufacturer, vendor string and firmware version out of `caps`.

    The line looks like:
      Mfg AAA (1), Vendor <vendor string>, Fw 1.23 (0x4567), FIPS 140-3, CC-EAL4 0
    """
    m = re.search(r"Mfg\s+(\S+)\s+\((\d+)\),\s*Vendor\s+(.+?),\s*Fw\s+(\S+)",
                  text)
    if not m:
        return None
    return {"mfg": m.group(1), "vendor": m.group(3).strip(), "fw": m.group(4)}


def probe_identity():
    """Run `caps` and work out what is fitted and what it may be asked for.

    Called at startup and after a reset. Never guesses from a build flag: the
    runbook's standing rule is to identify the fitted part before assuming,
    after an Odin/Freya power-up sequence was once run against a QVault.
    """
    global ONBOARD_PORT

    if TPM_LABEL_OVERRIDE:
        ident = {"state": "override", "label": TPM_LABEL_OVERRIDE,
                 "mfg": "", "vendor": TPM_LABEL_OVERRIDE, "fw": "",
                 "param_sets": SAFE_PARAM_SETS}
        set_identity(ident)
        broadcast(dict(ident, event="tpm.identity"))
        return ident

    if ONBOARD_PORT is not None:
        # Detection only narrows it to the J-Link ports present, so ask each
        # in turn. Retry, and never replace a good identity with a failed one:
        # a probe that loses a race with an act would otherwise leave "no TPM
        # detected" on the header while every act still works.
        ident = None
        for attempt in range(ONBOARD_PROBE_TRIES):
            for cand in (ONBOARD_CANDIDATES or [ONBOARD_PORT]):
                if cand != ONBOARD_PORT:
                    _drop_reader()    # the cached handle is the old port
                ONBOARD_PORT = cand
                ident = onboard_identity()
                if ident.get("state") == "ok":
                    break
            if ident.get("state") == "ok":
                break
            if attempt + 1 < ONBOARD_PROBE_TRIES:
                time.sleep(ONBOARD_PROBE_WAIT)
        if ident.get("state") != "ok" and \
                get_identity().get("state") == "ok":
            return get_identity()
        set_identity(ident)
        broadcast(dict(ident, event="tpm.identity"))
        return ident

    if not os.path.exists(CAPS):
        ident = {"state": "error", "label": "caps not built",
                 "param_sets": SAFE_PARAM_SETS,
                 "message": "not built: %s" % CAPS}
        set_identity(ident)
        broadcast(dict(ident, event="tpm.identity"))
        return ident

    with _run_lock:
        try:
            with BusLock():
                out = subprocess.run(
                    [CAPS], cwd=TPM_BUILD_DIR, env=child_env(),
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, timeout=PROBE_TIMEOUT).stdout
        except subprocess.TimeoutExpired:
            out = ""
        except (OSError, RuntimeError) as exc:
            out = "launch failed: %s" % exc

    caps = parse_caps(out)
    if caps is None:
        # No answer at all is the signature of an unpowered part or the wrong
        # chip select -- indistinguishable from a dead one, so say both.
        ident = {"state": "error", "label": "no TPM detected",
                 "param_sets": SAFE_PARAM_SETS,
                 "message": "TPM did not answer - check power, seating and "
                            "chip select"}
    else:
        sets = ALLOWED_PARAM_SETS.get(caps["mfg"], SAFE_PARAM_SETS)
        ident = dict(caps, state="ok", param_sets=sets,
                     label="%s - FW %s" % (caps["vendor"], caps["fw"]))

    set_identity(ident)
    broadcast(dict(ident, event="tpm.identity"))
    return ident


def tpm_reset():
    """Toggle TPM_!RESET to bring a part out of failure mode.

    Once the TPM reports TPM_RC_FAILURE every later command fails, including
    TPM2_Startup, until the reset line is pulsed. Doing that from the screen
    is the difference between a five-second recovery and an ssh session with a
    visitor watching.
    """
    if RESET_CMD:
        broadcast({"event": "tpm.reset", "via": "command"})
        try:
            subprocess.run(RESET_CMD, shell=True, check=True, timeout=60)
        except (subprocess.SubprocessError, OSError) as exc:
            broadcast({"event": "error", "message": "reset failed: %s" % exc})
            return False
        time.sleep(1.0)
        return True

    if shutil.which("pinctrl") is None:
        broadcast({"event": "error",
                   "message": "no reset mechanism on this host - set "
                              "TPM_RESET_CMD or reset the TPM manually"})
        return False

    broadcast({"event": "tpm.reset", "gpio": RESET_GPIO})
    try:
        subprocess.run(["pinctrl", "set", RESET_GPIO, "op", "dl"],
                       check=True, timeout=10)
        time.sleep(0.3)
        subprocess.run(["pinctrl", "set", RESET_GPIO, "op", "dh"],
                       check=True, timeout=10)
    except (subprocess.SubprocessError, OSError) as exc:
        broadcast({"event": "error", "message": "reset failed: %s" % exc})
        return False

    # The part needs a moment before it answers again.
    time.sleep(1.0)
    return True


def reset_and_capture(secs=None):
    """Reset, and publish what the board prints as it comes back up.

    The order matters. The console is opened and drained first, then the
    reset is triggered, then the output is read: resetting first and reading
    afterwards returns whatever was already buffered, interleaved with the
    boot it was meant to capture.

    The reset line is shared with the board, so this restarts the MCU as well
    as the TPM, and the bootloader's verification of the image is what the
    console tab then shows.
    """
    if ONBOARD_PORT is None:
        return tpm_reset()
    try:
        import onboard
    except ImportError:
        return tpm_reset()
    if secs is None:
        secs = BOOT_CAPTURE_SEC

    ok = [False]

    def trigger():
        ok[0] = tpm_reset()

    with BusLock():
        try:
            reader = _get_reader(onboard)
            reader.drain()
            worker = threading.Thread(target=trigger, daemon=True)
            worker.start()
            for line in reader.listen(secs):
                if line.startswith("{"):
                    try:
                        broadcast(json.loads(line))
                        continue
                    except ValueError:
                        pass
                broadcast({"event": "console", "line": line})
            worker.join(timeout=5)
        except OSError as exc:
            _drop_reader()
            broadcast({"event": "error",
                       "message": "console lost over the reset: %s" % exc})
            return False
    return ok[0]


def recover_and_reprobe():
    """Reset the TPM and re-identify it, then say so on the event stream."""
    if not reset_and_capture():
        return
    ident = probe_identity()
    broadcast({"event": "tpm.recovered", "ok": ident.get("state") == "ok",
               "label": ident.get("label", "")})


def run_demo(param_set, tamper, compare):
    """Run the TPM example, forwarding each JSON line as it appears.

    Runs in a background thread so the HTTP request returns immediately and
    the browser sees events stream in live rather than all at the end.
    """
    if not _run_lock.acquire(blocking=False):
        broadcast({"event": "error", "message": "a run is already in progress"})
        return
    try:
        # The board takes the parameter set as part of the act, so the
        # selector reaches it rather than being overwritten by the reply.
        act = "tamper" if tamper else "sign"
        if param_set == 65:
            act += "65"
        if onboard_run(act):
            return
    finally:
        if ONBOARD_PORT is not None:
            _run_lock.release()

    failed = False
    bus = None
    try:
        if not os.path.exists(EXAMPLE):
            broadcast({"event": "error",
                       "message": "example not built: %s" % EXAMPLE})
            return

        cmd = [EXAMPLE, "-mldsa=%d" % param_set, "-json"]
        if tamper:
            cmd.append("-tamper")
        if compare:
            cmd.append("-compare")

        broadcast({"event": "run.start",
                   "param_set": param_set,
                   "tamper": bool(tamper),
                   "cmd": " ".join(os.path.basename(c) for c in cmd)})

        started = time.time()
        try:
            bus = BusLock().__enter__()
        except RuntimeError as exc:
            bus = None
            broadcast({"event": "error", "message": str(exc)})
            return
        try:
            proc = subprocess.Popen(
                cmd, cwd=TPM_BUILD_DIR, env=child_env(),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1)
        except OSError as exc:
            broadcast({"event": "error", "message": "launch failed: %s" % exc})
            return

        lines = 0
        wedged = False
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            lines += 1
            # TPM_RC_FAILURE (0x101) means the part is now in failure mode and
            # will refuse everything until the reset line is toggled.
            if looks_wedged(line):
                wedged = True
            if line.startswith("{"):
                try:
                    broadcast(json.loads(line))
                    continue
                except ValueError:
                    pass
            # Anything not JSON is diagnostic output; surface it rather than
            # swallowing it, so a failing TPM is visible instead of silent.
            broadcast({"event": "log", "message": line})

        # A hung TPM must not wedge the booth. Bound the wait and report.
        try:
            rc = proc.wait(timeout=RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            proc.kill()
            if lines == 0:
                broadcast({"event": "error",
                           "message": "TPM not responding - check power, "
                                      "seating and chip select"})
            else:
                broadcast({"event": "error", "message": "TPM run timed out"})
            failed = True
            return

        broadcast({"event": "run.end",
                   "exit_code": rc,
                   "wall_ms": round((time.time() - started) * 1000.0, 1)})
        failed = wedged or rc != 0
    finally:
        if bus is not None:
            bus.__exit__(None, None, None)
        _run_lock.release()
        # Outside both locks: recovery re-probes and takes them again.
        if failed:
            recover_and_reprobe()


# --- Post-quantum TLS -----------------------------------------------------

TLS_KEY_RE = re.compile(
    r"TPM ML-DSA device key: handle (0x[0-9a-fA-F]+), pub (\d+) bytes")
TLS_CERT_RE = re.compile(
    r"Wrote (\S+) \((\d+) bytes\) and (\S+) \((\d+) bytes\)")
TLS_HS_RE = re.compile(
    r"Handshake: (.+?), group (\S+) \(ML-DSA identity signed on TPM\)")


def _port_listening_netstat(port):
    """The same question on Windows, which has no /proc."""
    try:
        out = subprocess.run(["netstat", "-an", "-p", "tcp"],
                             stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL,
                             text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        return False
    want = ":%d" % port
    for line in out.splitlines():
        col = line.split()
        if len(col) >= 4 and col[0].upper() == "TCP" and \
                col[1].endswith(want) and col[3].upper() == "LISTENING":
            return True
    return False


def _port_listening(port):
    """Is something listening, without connecting to it?

    Deliberately reads the kernel's own table rather than opening a socket.
    tls_server accepts exactly one connection, so a connect-based readiness
    probe IS the client: the server services the probe, the probe closes, and
    the real client then finds nothing. That cost an afternoon.
    """
    if sys.platform == "win32":
        return _port_listening_netstat(port)
    want = "%04X" % port
    for path in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            with open(path) as fh:
                next(fh, None)
                for line in fh:
                    col = line.split()
                    if len(col) < 4:
                        continue
                    # state 0A is TCP_LISTEN
                    if col[3] == "0A" and col[1].split(":")[-1] == want:
                        return True
        except (OSError, StopIteration):
            continue
    return False


def tls_env():
    """Environment for the PQC TLS tree.

    That tree builds its own libwolftpm, so it needs its own library path
    rather than the one belonging to whichever tree the acts come from.
    Windows resolves DLLs through PATH and ignores LD_LIBRARY_PATH, and the
    candidate directories differ by build system the same way the programs
    do, so every plausible one is listed.
    """
    env = dict(os.environ)
    dirs = [
        os.path.join(TLS_DIR, "src", ".libs"),
        os.path.join(TLS_DIR, "build"),
        os.path.join(TLS_DIR, "build", "Release"),
        os.path.join(TLS_DIR, "build", "Debug"),
        os.environ.get("WOLFSSL_PQTLS_LIB_DIR",
                       os.path.expanduser("~/Projects/Infineon/"
                                          "wolfssl-pqtls/lib")),
        LIB_PATH,
    ]
    if sys.platform == "win32":
        env["PATH"] = os.pathsep.join(dirs + [os.environ.get("PATH", "")])
    else:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(dirs)
    env["TPM2_SWTPM_PORT"] = str(TLS_FWTPM_PORT)
    return env


def _tls_run(args, timeout=240):
    return subprocess.run(
        args, cwd=TLS_DIR, env=tls_env(),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=timeout).stdout


def fwtpm_responder():
    """Name whatever answers the firmware TPM port, or None.

    A listening socket is not proof the fwTPM is behind it. Any other
    simulator refuses an ML-DSA key with TPM_RC_TYPE, which reads like a
    wolfTPM bug rather than the port collision it is, so ask the responder
    who it is: the fwTPM manufacturer is WOLF.
    """
    caps = tls_tool("examples/wrap/caps")
    if not os.path.exists(caps):
        return None
    try:
        out = _tls_run([caps], timeout=20)
    except Exception:
        return None
    m = re.search(r"Mfg (\S+)", out)
    return m.group(1) if m is not None else None


def ensure_fwtpm():
    """Start the firmware TPM if it is not already listening.

    Returns None on success, otherwise a string saying what went wrong.
    """
    if _port_listening(TLS_FWTPM_PORT):
        who = fwtpm_responder()
        if who == "WOLF":
            return None
        return ("port %d is held by %s, not the firmware TPM"
                % (TLS_FWTPM_PORT, who or "an unidentified responder"))
    server = tls_tool("src/fwtpm/fwtpm_server")
    if not os.path.exists(server):
        return "fwtpm_server not built in %s" % TLS_DIR
    subprocess.Popen(
        [server, "--port", str(TLS_FWTPM_PORT),
         "--platform-port", str(TLS_FWTPM_PLAT_PORT)],
        cwd=TLS_DIR, env=tls_env(),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=True)
    for _ in range(40):
        time.sleep(0.25)
        if _port_listening(TLS_FWTPM_PORT):
            return None
    return "firmware TPM did not start on port %d" % TLS_FWTPM_PORT


def run_tls_demo():
    # Same reasoning as the SPDM act: ~30 s, longer than the idle window.
    if not _run_lock.acquire(timeout=SEAL_WAIT_SEC):
        broadcast({"event": "error", "message": "TPM busy - try again"})
        return
    try:
        _run_tls_demo()
    finally:
        _run_lock.release()


def _run_tls_demo():
    broadcast({"event": "tls.begin", "mldsa": TLS_MLDSA, "group": TLS_GROUP})
    if not os.path.isdir(TLS_DIR):
        broadcast({"event": "error",
                   "message": "no PQC TLS tree at %s - build one and point "
                              "TPM_TLS_DIR at it, see host/README.md"
                              % TLS_DIR})
        broadcast({"event": "tls.end", "ok": False})
        return

    srv = None
    ok = False
    try:
        broadcast({"event": "tls.step", "n": 1, "state": "running"})
        why = ensure_fwtpm()
        if why is not None:
            broadcast({"event": "error", "message": why})
            broadcast({"event": "tls.step", "n": 1, "state": "fail",
                       "detail": why})
            broadcast({"event": "tls.end", "ok": False})
            return
        broadcast({"event": "tls.step", "n": 1, "state": "pass",
                   "detail": "port %d" % TLS_FWTPM_PORT})

        broadcast({"event": "tls.step", "n": 2, "state": "running"})
        out = _tls_run([tls_tool("examples/pqc/gen_pqc_certs"),
                        "-mldsa=" + TLS_MLDSA])
        m = TLS_KEY_RE.search(out)
        if m is not None:
            broadcast({"event": "tls.key", "handle": m.group(1),
                       "pub_bytes": int(m.group(2))})
        c = TLS_CERT_RE.search(out)
        if c is not None:
            broadcast({"event": "tls.certs",
                       "ca_bytes": int(c.group(2)),
                       "server_bytes": int(c.group(4))})
        broadcast({"event": "tls.step", "n": 2,
                   "state": "pass" if c is not None else "fail",
                   "detail": (("%s B CA, %s B server" %
                               (c.group(2), c.group(4))) if c else "failed")})
        if c is None:
            broadcast({"event": "log", "message": out.strip()[-300:]})
            broadcast({"event": "tls.end", "ok": False})
            return

        broadcast({"event": "tls.step", "n": 3, "state": "running"})
        srv = subprocess.Popen(
            [tls_tool("examples/tls/tls_server"),
             "-p=%d" % TLS_PORT, "-mldsa=" + TLS_MLDSA],
            cwd=TLS_DIR, env=tls_env(),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        listening = False
        for _ in range(80):
            time.sleep(0.25)
            if _port_listening(TLS_PORT):
                listening = True
                break
        broadcast({"event": "tls.step", "n": 3,
                   "state": "pass" if listening else "fail",
                   "detail": "listening on %d" % TLS_PORT if listening
                             else "did not start"})
        if not listening:
            broadcast({"event": "tls.end", "ok": False})
            return

        broadcast({"event": "tls.step", "n": 4, "state": "running"})
        cli = _tls_run([tls_tool("examples/tls/tls_client"),
                        "-p=%d" % TLS_PORT, "-mldsa",
                        "-group=" + TLS_GROUP], timeout=120)
        srv_out = ""
        try:
            srv_out = srv.communicate(timeout=30)[0] or ""
        except subprocess.TimeoutExpired:
            srv.kill()
            srv_out = srv.communicate()[0] or ""
        srv = None

        h = TLS_HS_RE.search(srv_out)
        ok = h is not None
        if ok:
            broadcast({"event": "tls.handshake", "cipher": h.group(1).strip(),
                       "group": h.group(2).strip()})
        else:
            broadcast({"event": "log",
                       "message": (cli.strip()[-200:] + " | " +
                                   srv_out.strip()[-200:])})
        broadcast({"event": "tls.step", "n": 4,
                   "state": "pass" if ok else "fail",
                   "detail": h.group(1).strip() if ok else "handshake failed"})
    except subprocess.TimeoutExpired:
        broadcast({"event": "error", "message": "TLS run timed out"})
    except OSError as exc:
        broadcast({"event": "error", "message": str(exc)})
    finally:
        if srv is not None:
            srv.kill()
    broadcast({"event": "tls.end", "ok": ok})


@APP.route("/api/tls", methods=["POST"])
def api_tls():
    touch()
    threading.Thread(target=run_tls_demo, daemon=True).start()
    return jsonify({"ok": True})


# --- Measured boot --------------------------------------------------------
# A PCR cannot be written, only extended: new = H(old || measurement). That is
# what makes the value a record of everything that has been measured, in
# order, and why a changed measurement can never be walked back. Reading all
# 24 costs one process each at about 22 ms, which is cheap enough not to need
# a dedicated helper.

PCR_BANK_RE = re.compile(r"^\s+(SHA\w+):\s*(.*)$")


def read_pcr_banks(out):
    """Which banks the part has actually allocated, from caps output."""
    banks = []
    started = False
    for line in out.splitlines():
        if "Assigned PCR" in line:
            started = True
            continue
        if not started:
            continue
        m = PCR_BANK_RE.match(line)
        if m is None:
            break
        banks.append({"name": m.group(1),
                      "allocated": bool(m.group(2).strip())})
    return banks


def pcr_snapshot():
    """Every PCR in the allocated bank, plus which banks exist."""
    vals = []
    banks = []
    with BusLock():
        try:
            caps = subprocess.run(
                [CAPS], cwd=TPM_BUILD_DIR, env=child_env(),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, timeout=PROBE_TIMEOUT).stdout
            banks = read_pcr_banks(caps)
        except (OSError, subprocess.TimeoutExpired):
            banks = []
        for i in range(PCR_COUNT):
            try:
                out = subprocess.run(
                    [PCR_READ, str(i)], cwd=TPM_BUILD_DIR, env=child_env(),
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, timeout=30).stdout
            except (OSError, subprocess.TimeoutExpired):
                out = ""
            vals.append({"index": i, "digest": _pcr_digest(out)})
    return banks, vals


ONBOARD_PCR_ACTS = {"extend": "pcr_extend", "reset": "pcr_reset"}


def run_pcr(action):
    if onboard_run(ONBOARD_PCR_ACTS.get(action, "pcr"),
                   OnboardPcrTable(action)):
        return
    broadcast({"event": "pcr.begin", "action": action})
    if not os.path.exists(PCR_READ):
        broadcast({"event": "error", "message": "read_pcr not built"})
        broadcast({"event": "pcr.end"})
        return
    if not _run_lock.acquire(timeout=SEAL_WAIT_SEC):
        broadcast({"event": "error", "message": "TPM busy - try again"})
        broadcast({"event": "pcr.end"})
        return
    try:
        if action in ("extend", "reset"):
            with BusLock():
                if action == "extend":
                    _seal_run([PCR_EXTEND, SEAL_PCR_INDEX, SEAL_BLOB])
                else:
                    _seal_run([PCR_RESET, SEAL_PCR_INDEX])
        banks, vals = pcr_snapshot()
        broadcast({"event": "pcr.table", "banks": banks, "pcrs": vals,
                   "touched": int(SEAL_PCR_INDEX)
                   if action in ("extend", "reset") else -1})
    except (OSError, RuntimeError) as exc:
        broadcast({"event": "error", "message": str(exc)})
    finally:
        _run_lock.release()
    broadcast({"event": "pcr.end"})


@APP.route("/api/pcr", methods=["POST"])
def api_pcr():
    body = request.get_json(silent=True) or {}
    action = body.get("action", "read")
    if action not in ("read", "extend", "reset"):
        return jsonify({"ok": False, "error": "bad action"}), 400
    touch()
    threading.Thread(target=run_pcr, args=(action,), daemon=True).start()
    return jsonify({"ok": True})


# --- SPDM -----------------------------------------------------------------
# The SPI bus between a host and a TPM is plaintext; SPDM negotiates a session
# key so it carries ciphertext instead. The fitted part negotiates but cannot
# be provisioned on this firmware, so the working session is demonstrated
# against wolfTPM's firmware TPM responder using the same client code. That
# contrast is the point of the panel, and pretending otherwise would be
# caught by the first person who asked.

SPDM_STEP_RE = re.compile(r"^\[(\d+)\]\s+(.*)$")
SPDM_SESSION_RE = re.compile(
    r"Session established \(([^,]+), SessionID: (0x[0-9a-fA-F]+)\)")
SPDM_PSK_SESSION_RE = re.compile(
    r"PSK session established \(SessionID: (0x[0-9a-fA-F]+)\)")
SPDM_HANDSHAKE_RE = re.compile(r"Handshake:\s+(.*)$")
SPDM_RESULTS_RE = re.compile(
    r"Results: (\d+) total, (\d+) passed, (\d+) failed")
SPDM_UNIT_RE = re.compile(r"\bPassed\s*$")


def probe_spdm_silicon():
    """What the fitted part reports. Read-only and quick."""
    ctrl = os.path.join(TPM_BUILD_DIR, "examples/spdm/spdm_ctrl")
    if not os.path.exists(ctrl):
        return {"available": False,
                "message": "spdm_ctrl not built in the hardware tree"}
    try:
        with BusLock():
            out = subprocess.run(
                [ctrl, "--vendor=infineon", "--status"], cwd=TPM_BUILD_DIR,
                env=child_env(), stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, timeout=60).stdout
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        return {"available": False, "message": str(exc)}

    provisionable = "GET_STS failed" not in out
    return {
        "available": True,
        "provisionable": provisionable,
        "raw": out.strip().splitlines()[-3:],
        "message": ("SPDM 1.3 negotiates over the TCG binding. Provisioning "
                    "a PSK needs TPM_CAP_CONFIGS, which this pre-production "
                    "firmware does not implement yet, so a secured session "
                    "cannot be established on the part today. The client side "
                    "is ready and proven - the same code below drives a full "
                    "session against wolfTPM's firmware TPM responder.")
        if not provisionable else "SPDM is provisionable on this part.",
    }


def run_spdm_demo():
    # Holds the run slot for the whole act. It takes ~41 s, well past the
    # attract idle window, so without this the loop starts a signing run
    # partway through and switches the tab out from under the visitor.
    if not _run_lock.acquire(timeout=SEAL_WAIT_SEC):
        broadcast({"event": "error", "message": "TPM busy - try again"})
        return
    try:
        _run_spdm_demo()
    finally:
        _run_lock.release()


def _run_spdm_demo():
    # The board does the whole handshake itself when it is there. Only fall
    # back to the host responder when no board is attached.
    if onboard_run("spdm"):
        return
    broadcast({"event": "spdm.begin", "mode": SPDM_MODE})
    broadcast(dict(probe_spdm_silicon(), event="spdm.silicon"))

    script = os.path.join(SPDM_DIR, "examples/spdm/spdm_test.sh")
    ctrl = os.path.join(SPDM_DIR, "examples/spdm/spdm_ctrl")
    if not os.path.exists(script) or not os.path.exists(ctrl):
        broadcast({"event": "error",
                   "message": "SPDM responder tree not built at %s" % SPDM_DIR})
        broadcast({"event": "spdm.end", "total": 0, "passed": 0, "failed": 0})
        return

    units = 0
    step = None
    try:
        proc = subprocess.Popen(
            ["bash", script, "./examples/spdm/spdm_ctrl", SPDM_MODE],
            cwd=SPDM_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1)
    except OSError as exc:
        broadcast({"event": "error", "message": "spdm launch failed: %s" % exc})
        broadcast({"event": "spdm.end", "total": 0, "passed": 0, "failed": 0})
        return

    for line in proc.stdout:
        line = line.rstrip()
        m = SPDM_STEP_RE.match(line)
        if m is not None:
            step = int(m.group(1))
            broadcast({"event": "spdm.step", "n": step,
                       "title": m.group(2).strip(), "state": "running"})
            continue
        m = SPDM_HANDSHAKE_RE.search(line)
        if m is not None:
            broadcast({"event": "spdm.handshake", "n": step,
                       "chain": m.group(1).strip()})
            continue
        m = SPDM_SESSION_RE.search(line)
        if m is not None:
            broadcast({"event": "spdm.session", "n": step,
                       "cipher": m.group(1).strip(), "id": m.group(2)})
            continue
        m = SPDM_PSK_SESSION_RE.search(line)
        if m is not None:
            broadcast({"event": "spdm.session", "n": step,
                       "cipher": "AES-256-GCM (PSK)", "id": m.group(1)})
            continue
        if SPDM_UNIT_RE.search(line) and line.startswith("Test "):
            units += 1
            continue
        if line.strip() == "PASS" or line.strip() == "FAIL":
            broadcast({"event": "spdm.step", "n": step, "state":
                       line.strip().lower(), "units": units})
            continue
        m = SPDM_RESULTS_RE.search(line)
        if m is not None:
            broadcast({"event": "spdm.results", "total": int(m.group(1)),
                       "passed": int(m.group(2)), "failed": int(m.group(3)),
                       "units": units})

    try:
        proc.wait(timeout=SPDM_TIMEOUT)
    except subprocess.TimeoutExpired:
        proc.kill()
        broadcast({"event": "error", "message": "SPDM run timed out"})
    broadcast({"event": "spdm.end", "units": units})


@APP.route("/api/spdm", methods=["POST"])
def api_spdm():
    touch()
    threading.Thread(target=run_spdm_demo, daemon=True).start()
    return jsonify({"ok": True})


# --- Sealed secret bound to measured state --------------------------------
# PCR 16 is the debug PCR and is resettable, which is what makes this
# repeatable on a booth loop: reset, seal, unseal, extend, refuse, reset.
# The whole sequence runs under one bus-lock hold - if anything else touched
# the TPM between the seal and the unseal the PCR could move underneath us and
# the demo would report a failure that means nothing.

PCR_DIGEST_RE = re.compile(r"digest:\s*\n\s*([0-9A-Fa-f]{16,})")
SEAL_BLOB_RE = re.compile(r"Created sealed blob \(pub (\d+), priv (\d+) bytes\)")
UNSEAL_OK_RE = re.compile(r"Unsealed secret \(\d+ bytes\): (.*)")
UNSEAL_ERR_RE = re.compile(r"TPM2_Unseal failed (0x[0-9a-fA-F]+): (\S+)")


def _seal_run(cmd):
    return subprocess.run(
        cmd, cwd=TPM_BUILD_DIR, env=child_env(), stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, timeout=RUN_TIMEOUT).stdout


def _pcr_digest(text):
    m = PCR_DIGEST_RE.search(text)
    return m.group(1) if m is not None else ""


def run_seal_demo():
    """Seal a secret to the current measurement, release it, change the
    measurement, and watch the same TPM refuse the same blob."""
    if onboard_run("seal"):
        return
    idx = SEAL_PCR_INDEX
    broadcast({"event": "seal.begin", "pcr": int(idx), "secret": SEAL_SECRET})
    if not os.path.exists(SEAL_PCR):
        broadcast({"event": "error",
                   "message": "seal_pcr not built in %s" % TPM_BUILD_DIR})
        broadcast({"event": "seal.end", "pass": False})
        return

    passed = False
    needs_reset = False
    if not _run_lock.acquire(timeout=SEAL_WAIT_SEC):
        broadcast({"event": "error",
                   "message": "TPM busy - try again in a moment"})
        broadcast({"event": "seal.end", "pass": False})
        return
    try:
        with BusLock():
            out = _seal_run([PCR_RESET, idx])
            broadcast({"event": "seal.reset", "pcr": int(idx),
                       "digest": _pcr_digest(out)})

            out = _seal_run([SEAL_PCR, "-pcr=" + idx, "-seal",
                             "-secretstr=" + SEAL_SECRET,
                             "-sealblob=" + SEAL_BLOB])
            m = SEAL_BLOB_RE.search(out)
            broadcast({"event": "seal.create",
                       "pub_bytes": int(m.group(1)) if m else 0,
                       "priv_bytes": int(m.group(2)) if m else 0,
                       "ok": m is not None})
            if m is None:
                broadcast({"event": "log", "message": out.strip()[-300:]})
                needs_reset = looks_wedged(out)
                raise RuntimeError("sealing failed")

            out = _seal_run([SEAL_PCR, "-pcr=" + idx, "-unseal",
                             "-sealblob=" + SEAL_BLOB])
            m = UNSEAL_OK_RE.search(out)
            broadcast({"event": "seal.unseal", "stage": "before",
                       "ok": m is not None,
                       "secret": m.group(1).strip() if m else ""})
            released = m is not None

            out = _seal_run([PCR_EXTEND, idx, SEAL_BLOB])
            broadcast({"event": "seal.extend", "pcr": int(idx),
                       "digest": _pcr_digest(out)})

            out = _seal_run([SEAL_PCR, "-pcr=" + idx, "-unseal",
                             "-sealblob=" + SEAL_BLOB])
            m = UNSEAL_ERR_RE.search(out)
            refused = m is not None
            broadcast({"event": "seal.unseal", "stage": "after",
                       "ok": not refused,
                       "rc": m.group(1) if m else "",
                       "rc_name": m.group(2) if m else ""})

            # Leave the PCR as we found it, so a later run starts clean and
            # nothing else on the bench inherits a changed measurement.
            _seal_run([PCR_RESET, idx])
            passed = released and refused
    except subprocess.TimeoutExpired:
        broadcast({"event": "error", "message": "seal sequence timed out"})
    except (OSError, RuntimeError) as exc:
        broadcast({"event": "error", "message": str(exc)})
    finally:
        _run_lock.release()
    if needs_reset:
        recover_and_reprobe()

    broadcast({"event": "seal.end", "pass": passed})


@APP.route("/api/seal", methods=["POST"])
def api_seal():
    touch()
    threading.Thread(target=run_seal_demo, daemon=True).start()
    return jsonify({"ok": True})


# --- Endorsement certificates ---------------------------------------------
# The EK certificates are burned in at manufacture and never change, so the
# whole result is cached after the first read. Re-reading costs five EK
# derivations on the part, which is far too slow to sit behind a booth button.

EK_HANDLE_RE = re.compile(r"TCG Handle (0x[0-9a-fA-F]+)")
EK_ALG_RE = re.compile(r"EK (RSA|ECC), Hash: (\S+),")
EK_BITS_RE = re.compile(r"KeyBits: (\d+)")
EK_CURVE_RE = re.compile(r"CurveID (\S+)")
EK_SIZE_RE = re.compile(r"Parsing certificate \((\d+) bytes\)")
EK_SERIAL_RE = re.compile(r"Serial Number: \d+ \((0x[0-9a-fA-F]+)\)")
EK_CN_RE = re.compile(r"CN\s*=\s*([^,\n]+)")
EK_O_RE = re.compile(r"\bO\s*=\s*([^,\n]+)")
EK_SAN_RE = re.compile(r"2\.23\.133\.2\.(\d)=([^/\n]+)")


def openssl_fields(pem):
    """Issuer and subject-alt-name detail the example does not print.

    The TCG OIDs in the SAN are the interesting part for a demo: 2.23.133.2.1
    is the TPM manufacturer, .2 the model and .3 the firmware version, all
    asserted by the issuing CA rather than by the chip talking about itself.
    """
    out = {}
    try:
        txt = subprocess.run(
            ["openssl", "x509", "-noout", "-issuer", "-enddate",
             "-ext", "subjectAltName"],
            input=pem, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return out

    for line in txt.splitlines():
        if line.startswith("issuer="):
            m = EK_CN_RE.search(line)
            if m:
                out["issuer_cn"] = m.group(1).strip()
            m = EK_O_RE.search(line)
            if m:
                out["issuer_o"] = m.group(1).strip()
        elif line.startswith("notAfter="):
            out["not_after"] = line.split("=", 1)[1].strip()

    san = {}
    for num, val in EK_SAN_RE.findall(txt):
        san[{"1": "manufacturer", "2": "model", "3": "version"}.get(num, num)] \
            = val.strip()
    if san:
        out["san"] = san
    return out


def parse_ek_certs(text):
    """Split get_ek_certs output into one record per TCG NV handle."""
    certs = []
    blocks = text.split("TCG Handle ")
    for blk in blocks[1:]:
        blk = "TCG Handle " + blk
        rec = {}
        m = EK_HANDLE_RE.search(blk)
        if m is None:
            continue
        rec["handle"] = m.group(1)
        m = EK_ALG_RE.search(blk)
        if m is not None:
            rec["alg"] = m.group(1)
            rec["hash"] = m.group(2)
        m = EK_BITS_RE.search(blk)
        if m is not None:
            rec["bits"] = int(m.group(1))
        m = EK_CURVE_RE.search(blk)
        if m is not None:
            rec["curve"] = m.group(1)
        m = EK_SIZE_RE.search(blk)
        if m is not None:
            rec["cert_bytes"] = int(m.group(1))
        m = EK_SERIAL_RE.search(blk)
        if m is not None:
            rec["serial"] = m.group(1)
        rec["key_match"] = "Cert public key and EK public match" in blk
        rec["parsed"] = "Successfully parsed" in blk
        # The example checks the chain against its 20 bundled production roots.
        # Pre-production parts are issued by a test CA that is deliberately not
        # among them, so "untrusted" here is the expected answer and not a
        # defect - the issuer name below is what says so.
        rec["chain"] = "trusted" if "EK Certificate is VALID" in blk \
            else "untrusted"

        start = blk.find("-----BEGIN CERTIFICATE-----")
        end = blk.find("-----END CERTIFICATE-----")
        if start != -1 and end != -1:
            pem = blk[start:end + len("-----END CERTIFICATE-----")] + "\n"
            rec.update(openssl_fields(pem))
        certs.append(rec)
    return certs


def probe_ek_certs():
    global _ek_cache
    with _ek_lock:
        if _ek_cache is not None:
            return _ek_cache
        if not os.path.exists(EK_CERTS):
            return {"error": "get_ek_certs not built in %s" % TPM_BUILD_DIR}
        try:
            with BusLock():
                out = subprocess.run(
                    [EK_CERTS], cwd=TPM_BUILD_DIR, env=child_env(),
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, timeout=PROBE_TIMEOUT).stdout
        except subprocess.TimeoutExpired:
            return {"error": "get_ek_certs timed out"}
        except (OSError, RuntimeError) as exc:
            return {"error": str(exc)}
        _ek_cache = {"certs": parse_ek_certs(out)}
        return _ek_cache


def run_ek_certs():
    if onboard_run("ek", OnboardEkCerts()):
        return
    broadcast({"event": "ek.begin"})
    res = probe_ek_certs()
    if "error" in res:
        broadcast({"event": "error", "message": res["error"]})
        broadcast({"event": "ek.end", "count": 0})
        return
    for rec in res["certs"]:
        broadcast(dict(rec, event="ek.cert"))
    broadcast({"event": "ek.end", "count": len(res["certs"])})


@APP.route("/api/ekcerts", methods=["POST"])
def api_ekcerts():
    touch()
    threading.Thread(target=run_ek_certs, daemon=True).start()
    return jsonify({"ok": True})


def touch():
    """Record visitor activity, so attract mode backs off while someone is
    actually driving the demo."""
    global _last_touch
    _last_touch = time.time()


def attract_script(allowed):
    """The unattended running order.

    A clean run then a tampered run for each parameter set, so the mosaic
    visibly changes size between pairs and a visitor who watches two cycles
    sees both the claim and the failure. Then the two slower acts, once per
    lap each, so the whole story tells itself without anyone pressing
    anything - but not so often that they crowd out the signing that is the
    headline.
    """
    steps = []
    for param in allowed:
        steps.append(("sign", param, False))
        steps.append(("sign", param, True))
    steps.append(("seal", None, False))
    steps.append(("pcr", None, False))
    steps.append(("ek", None, False))
    return steps


def attract_loop():
    global _attract_next, _attract_idx
    while True:
        time.sleep(1.0)
        now = time.time()
        if not _attract_on:
            continue
        if _run_lock.locked() or act_in_flight():
            continue
        # Two separate waits, and both have to pass. Idle keeps the loop out
        # of the way while a visitor is pressing buttons; hold is the dwell
        # that lets the finished screen actually be read.
        if now - _last_touch < ATTRACT_IDLE_SEC:
            continue
        if now < _attract_next:
            continue
        allowed = get_identity().get("param_sets", SAFE_PARAM_SETS)
        if not allowed:
            continue

        steps = attract_script(allowed)
        kind, param, tamper = steps[_attract_idx % len(steps)]
        _attract_idx = (_attract_idx + 1) % len(steps)

        broadcast({"event": "attract", "on": True, "act": kind,
                   "tamper": tamper, "param_set": param})
        if kind == "sign":
            run_demo(param, tamper, True)
        elif kind == "seal":
            run_seal_demo()
        elif kind == "pcr":
            run_pcr("read")
        elif kind == "ek":
            run_ek_certs()
        _attract_next = time.time() + ATTRACT_HOLD_SEC


@APP.route("/api/touch", methods=["POST"])
def api_touch():
    """Record that a visitor is interacting without running anything.

    Switching tabs is interaction. Without this the attract loop advances on
    its own schedule and switches the tab out from under someone who just
    chose one, which reads as the page fighting you."""
    touch()
    return jsonify({"ok": True})


@APP.route("/api/attract", methods=["POST"])
def api_attract():
    """Let the operator stop the loop to talk over a frozen screen."""
    global _attract_on
    body = request.get_json(silent=True) or {}
    _attract_on = bool(body.get("on", True))
    touch()
    broadcast({"event": "attract", "on": _attract_on})
    return jsonify({"ok": True, "on": _attract_on})


@APP.route("/")
def index():
    return render_template("index.html")


@APP.route("/api/tpm")
def api_tpm():
    return jsonify(get_identity())


@APP.route("/api/run", methods=["POST"])
def api_run():
    body = request.get_json(silent=True) or {}
    try:
        param_set = int(body.get("param_set", 87))
    except (TypeError, ValueError):
        return jsonify({"ok": False, "error": "bad param_set"}), 400

    # The dropdown is cosmetic; this is the guard that matters. A browser tab
    # left open from a session with a different part fitted can still POST a
    # parameter set that would put this one into failure mode.
    allowed = get_identity().get("param_sets", SAFE_PARAM_SETS)
    if param_set not in allowed:
        msg = ("ML-DSA-%d is not offered on the fitted part (%s); allowed: %s"
               % (param_set, get_identity().get("label", "unknown"),
                  ", ".join(str(s) for s in allowed)))
        broadcast({"event": "error", "message": msg})
        return jsonify({"ok": False, "error": msg}), 400

    tamper = bool(body.get("tamper", False))
    compare = bool(body.get("compare", True))
    touch()

    threading.Thread(target=run_demo,
                     args=(param_set, tamper, compare),
                     daemon=True).start()
    return jsonify({"ok": True})


@APP.route("/api/reset", methods=["POST"])
def api_reset():
    if _run_lock.locked():
        return jsonify({"ok": False,
                        "error": "a run is in progress"}), 409
    touch()
    threading.Thread(target=recover_and_reprobe, daemon=True).start()
    return jsonify({"ok": True})


@APP.route("/api/stream")
def api_stream():
    def gen():
        q = queue.Queue(maxsize=1000)
        with _subscribers_lock:
            _subscribers.append(q)
        try:
            # Tell the page it is connected, so a stale screen is obvious, and
            # hand it the identity so a browser that joins late is not blank.
            yield "data: %s\n\n" % json.dumps({"event": "connected"})
            yield "data: %s\n\n" % json.dumps(
                dict(get_identity(), event="tpm.identity"))
            yield "data: %s\n\n" % json.dumps(
                {"event": "attract", "on": _attract_on})
            while True:
                try:
                    ev = q.get(timeout=15)
                    yield "data: %s\n\n" % json.dumps(ev)
                except queue.Empty:
                    # Comment frame keeps proxies and the browser from
                    # dropping an idle connection between visitors.
                    yield ": keepalive\n\n"
        finally:
            with _subscribers_lock:
                if q in _subscribers:
                    _subscribers.remove(q)

    return Response(gen(), mimetype="text/event-stream", headers={
        "Cache-Control": "no-cache",
        "X-Accel-Buffering": "no",
    })


def port_taken(port):
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(1.0)
    try:
        probe.connect(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        probe.close()


def is_this_demo(port):
    """Whether what answers on a port is another copy of this server."""
    try:
        import urllib.request
        with urllib.request.urlopen(
                "http://127.0.0.1:%d/api/tpm" % port, timeout=1.5) as r:
            return "param_sets" in r.read(400).decode("utf-8", "replace")
    except Exception:
        return False


def choose_http_port():
    """The port to serve on, and whether a duplicate is already up.

    Windows does not stop a second bind: werkzeug sets SO_REUSEADDR, which
    there means "take the port", so a duplicate start silently steals the
    listener while the first copy keeps running - and keeps opening the
    board's serial port. The visible symptom is an act failing with access
    denied on a machine that looks idle. Stopping the scheduled task does not
    kill the python child, so this is easy to reach by accident.

    When the port was asked for explicitly, honour it and refuse to be the
    second copy. When it was not, step past whatever else is on the machine -
    a packaged demo should not need the person to know what 8080 is.
    """
    if os.environ.get("TPM_HTTP_PORT"):
        if port_taken(HTTP_PORT):
            return HTTP_PORT, True
        return HTTP_PORT, False

    for port in range(HTTP_PORT, HTTP_PORT + 20):
        if not port_taken(port):
            return port, False
        if is_this_demo(port):
            return port, True
    return HTTP_PORT, True


if __name__ == "__main__":
    HTTP_PORT, duplicate = choose_http_port()
    if duplicate:
        sys.stderr.write(
            "another demo server is already answering on port %d.\n"
            "Stop it first - stopping the scheduled task is not enough, the\n"
            "python process outlives it:\n"
            "  Get-Process python* | Stop-Process -Force\n" % HTTP_PORT)
        raise SystemExit(1)
    sys.stderr.write("\n  Demo at http://127.0.0.1:%d\n" % HTTP_PORT)
    if ONBOARD_PORT:
        sys.stderr.write("  Looking for the board on %s\n\n" % ONBOARD_PORT)
    else:
        sys.stderr.write("  No J-Link port found - set TPM_ONBOARD_PORT\n\n")
    sys.stderr.flush()
    # Probe in the background so a dead or unpowered part delays the identity
    # rather than the whole server coming up.
    threading.Thread(target=probe_identity, daemon=True).start()
    threading.Thread(target=attract_loop, daemon=True).start()
    # threaded=True so SSE streams do not block the run endpoint.
    APP.run(host="0.0.0.0", port=HTTP_PORT, threaded=True, debug=False)
