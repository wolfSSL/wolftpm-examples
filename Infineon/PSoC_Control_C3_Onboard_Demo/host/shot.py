#!/usr/bin/env python3
"""Screenshot the demo page headless, optionally after running some JS.

Exists because the booth demo has no other way to be checked without a human
looking at it: a regression in the layout or in a panel that only fills on an
event is invisible to curl. Speaks CDP over a hand-rolled websocket rather
than pulling in a dependency, so it runs on a bare Pi.

  ./shot.py out.png                            plain capture
  ./shot.py out.png --click btn-ek --wait 4    press a button, then capture
"""
import argparse
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request


def ws_connect(url):
    """Minimal RFC 6455 client handshake. Enough for localhost CDP."""
    assert url.startswith("ws://")
    rest = url[len("ws://"):]
    hostport, _, path = rest.partition("/")
    host, _, port = hostport.partition(":")
    port = int(port or 80)
    sock = socket.create_connection((host, port), timeout=30)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n" % (path, hostport, key))
    sock.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("websocket handshake closed early")
        buf += chunk
    if b"101" not in buf.split(b"\r\n")[0]:
        raise RuntimeError("websocket handshake failed: %r" % buf[:120])
    return sock, buf.split(b"\r\n\r\n", 1)[1]


def ws_send(sock, payload):
    data = payload.encode()
    mask = os.urandom(4)
    n = len(data)
    if n < 126:
        hdr = struct.pack("!BB", 0x81, 0x80 | n)
    elif n < (1 << 16):
        hdr = struct.pack("!BBH", 0x81, 0x80 | 126, n)
    else:
        hdr = struct.pack("!BBQ", 0x81, 0x80 | 127, n)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    sock.sendall(hdr + mask + masked)


class Reader(object):
    def __init__(self, sock, initial=b""):
        self.sock = sock
        self.buf = initial

    def _need(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("websocket closed")
            self.buf += chunk

    def frame(self):
        """Server-to-client frames are never masked."""
        while True:
            self._need(2)
            b0, b1 = self.buf[0], self.buf[1]
            ln = b1 & 0x7F
            off = 2
            if ln == 126:
                self._need(4)
                ln = struct.unpack("!H", self.buf[2:4])[0]
                off = 4
            elif ln == 127:
                self._need(10)
                ln = struct.unpack("!Q", self.buf[2:10])[0]
                off = 10
            self._need(off + ln)
            payload = self.buf[off:off + ln]
            self.buf = self.buf[off + ln:]
            if (b0 & 0x0F) == 0x01:
                return payload.decode()
            # ignore ping/pong/binary/close control frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--url", default="http://127.0.0.1:8080/")
    ap.add_argument("--click", default=None,
                    help="element id to click before capturing")
    ap.add_argument("--eval", default=None, help="JS to run before capturing")
    ap.add_argument("--wait", type=float, default=3.0,
                    help="seconds to wait after load and after the click")
    ap.add_argument("--width", type=int, default=1500)
    ap.add_argument("--height", type=int, default=1500)
    ap.add_argument("--port", type=int, default=0,
                    help="CDP port; 0 picks a free one")
    args = ap.parse_args()

    # A fixed port is a trap. If an earlier run was killed hard its browser
    # survives holding the port, the new browser cannot bind it, and this
    # script then drives the ORPHAN - which still shows the page as it was
    # when that run started. The shot looks plausible and is stale, so take
    # a free port and never attach to someone else's browser.
    if args.port == 0:
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        args.port = probe.getsockname()[1]
        probe.close()

    # The binary is named differently across distributions, so take the
    # first one present rather than assuming. SHOT_BROWSER overrides.
    browser = os.environ.get("SHOT_BROWSER")
    if not browser:
        for cand in ("chromium", "chromium-browser", "google-chrome",
                     "google-chrome-stable"):
            if shutil.which(cand):
                browser = cand
                break
    if not browser:
        raise SystemExit("no chromium or chrome found; set SHOT_BROWSER")

    # A throwaway profile per run. Without one the browser uses the default
    # profile, whose HTTP cache serves a stale page after the template is
    # edited - the shot then shows the previous build and looks like the
    # change did nothing. It also keeps this out of the user's own profile.
    profile = tempfile.mkdtemp(prefix="shot-profile-")
    chrome = subprocess.Popen(
        [browser, "--headless=new", "--disable-gpu", "--no-sandbox",
         "--hide-scrollbars", "--remote-debugging-port=%d" % args.port,
         "--user-data-dir=%s" % profile, "--disable-application-cache",
         "--window-size=%d,%d" % (args.width, args.height), args.url],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        target = None
        for _ in range(40):
            time.sleep(0.5)
            try:
                items = json.load(urllib.request.urlopen(
                    "http://127.0.0.1:%d/json" % args.port))
            except Exception:
                continue
            for it in items:
                if it.get("url", "").startswith("http"):
                    target = it
                    break
            if target:
                break
        if not target:
            print("no page target", file=sys.stderr)
            return 1

        sock, extra = ws_connect(target["webSocketDebuggerUrl"])
        rd = Reader(sock, extra)
        msg_id = [0]

        def call(method, params=None):
            msg_id[0] += 1
            mid = msg_id[0]
            ws_send(sock, json.dumps(
                {"id": mid, "method": method, "params": params or {}}))
            while True:
                res = json.loads(rd.frame())
                if res.get("id") == mid:
                    return res

        def settle(seconds):
            """Sleep, but keep the CDP connection warm.

            A long idle wait gets the websocket closed underneath us, which
            surfaces as "websocket closed" at capture time. Waiting for a
            40 second demo act is exactly when that happens."""
            end = time.time() + seconds
            while time.time() < end:
                time.sleep(min(4.0, max(0.0, end - time.time())))
                call("Runtime.evaluate", {"expression": "1"})

        call("Page.enable")
        call("Runtime.enable")
        settle(args.wait)
        if args.eval:
            call("Runtime.evaluate", {"expression": args.eval})
        if args.click:
            call("Runtime.evaluate", {
                "expression": "document.getElementById(%s).click()"
                              % json.dumps(args.click)})
        if args.eval or args.click:
            settle(args.wait)

        res = call("Page.captureScreenshot", {"captureBeyondViewport": True})
        data = res.get("result", {}).get("data")
        if not data:
            print("no screenshot data: %s" % json.dumps(res)[:200],
                  file=sys.stderr)
            return 1
        with open(args.out, "wb") as fh:
            fh.write(base64.b64decode(data))
        print("wrote %s (%d bytes)" % (args.out, os.path.getsize(args.out)))
        return 0
    finally:
        chrome.terminate()
        try:
            chrome.wait(timeout=10)
        except Exception:
            chrome.kill()
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
