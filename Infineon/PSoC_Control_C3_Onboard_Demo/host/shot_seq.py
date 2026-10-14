#!/usr/bin/env python3
"""Capture a running act as a sequence of frames, for an animated GIF.

`shot.py` takes one picture after the act has finished, which is the right
thing for checking a layout. This is for showing the act happening: it holds
one browser open, presses the button, and captures every second or so while
the steps light up.

Assembling the frames is left to ImageMagick, which is one line and avoids
carrying an encoder here:

  ./shot_seq.py /tmp/frames --click btn-tls --seconds 30
  convert -delay 100 -loop 0 /tmp/frames/*.png demo.gif

Chrome's own screencast would give smoother output, but a full-page capture
per frame is what matches the screenshots, and a demo act that takes half a
minute does not need a high frame rate to read.
"""
import argparse
import base64
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import Reader, ws_connect, ws_send          # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--url", default="http://127.0.0.1:8080/")
    ap.add_argument("--tab", default=None, help="tab to select first")
    ap.add_argument("--click", default=None, help="element id to press")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--settle", type=float, default=3.0,
                    help="seconds to wait after load before pressing")
    ap.add_argument("--width", type=int, default=1500)
    ap.add_argument("--height", type=int, default=1250)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    browser = os.environ.get("SHOT_BROWSER", "google-chrome")
    profile = tempfile.mkdtemp(prefix="shotseq-profile-")
    chrome = subprocess.Popen(
        [browser, "--headless=new", "--disable-gpu", "--no-sandbox",
         "--hide-scrollbars", "--remote-debugging-port=%d" % port,
         "--user-data-dir=%s" % profile, "--disable-application-cache",
         "--window-size=%d,%d" % (args.width, args.height), args.url],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        target = None
        for _ in range(40):
            time.sleep(0.5)
            try:
                items = json.load(urllib.request.urlopen(
                    "http://127.0.0.1:%d/json" % port))
            except Exception:
                continue
            for it in items:
                if it.get("url", "").startswith("http"):
                    target = it
                    break
            if target:
                break
        if not target:
            sys.stderr.write("no page target\n")
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

        call("Page.enable")
        call("Runtime.enable")
        time.sleep(args.settle)

        if args.tab:
            call("Runtime.evaluate", {"expression":
                 "document.querySelector('.tab[data-tab=%s]').click()"
                 % args.tab})
            time.sleep(1.0)
        if args.click:
            call("Runtime.evaluate", {"expression":
                 "document.getElementById(%s).click()" % json.dumps(args.click)})

        n = 0
        end = time.time() + args.seconds
        while time.time() < end:
            res = call("Page.captureScreenshot", {"captureBeyondViewport": True})
            data = res.get("result", {}).get("data")
            if data:
                n += 1
                with open(os.path.join(args.outdir, "f%04d.png" % n), "wb") as f:
                    f.write(base64.b64decode(data))
            time.sleep(args.interval)
        print("%d frames in %s" % (n, args.outdir))
        return 0
    finally:
        chrome.terminate()
        try:
            chrome.wait(timeout=10)
        except Exception:
            chrome.kill()


if __name__ == "__main__":
    sys.exit(main())
