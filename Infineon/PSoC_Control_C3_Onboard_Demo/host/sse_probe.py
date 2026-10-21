"""Open the demo's event stream, trigger an act, print what arrives.

PowerShell's Invoke-WebRequest buffers an SSE stream and yields nothing when
the job is stopped, so it cannot see events from a run that never ends.
"""
import json
import os
import sys
import threading
import time
import urllib.request

BASE = os.environ.get("DEMO_BASE",
                      "http://127.0.0.1:%s" % os.environ.get("TPM_HTTP_PORT", "8080"))
act = sys.argv[1] if len(sys.argv) > 1 else "spdm"
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 60

seen = []


def reader():
    try:
        r = urllib.request.urlopen(BASE + "/api/stream", timeout=secs + 10)
        for raw in r:
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("data:"):
                seen.append(line[5:].strip())
    except Exception as exc:                    # stream is cut on purpose
        seen.append('{"event":"_reader","note":"%s"}' % exc)


t = threading.Thread(target=reader, daemon=True)
t.start()
time.sleep(1.5)

req = urllib.request.Request(BASE + "/api/" + act, method="POST",
                             data=b"", headers={"Content-Type": "application/json"})
urllib.request.urlopen(req, timeout=30).read()

# The endpoint name and the event prefix are not the same word for every
# act, so waiting for "<act>.end" silently burns the whole timeout.
TERMINAL = {"run": "run.end", "seal": "seal.end", "pcr": "pcr.end",
            "ekcerts": "ek.end", "spdm": "spdm.end", "tls": "tls.end"}
done_ev = TERMINAL.get(act, act + ".end")

deadline = time.time() + secs
while time.time() < deadline:
    if any('"%s"' % done_ev in s for s in seen):
        break
    time.sleep(0.5)

for s in seen:
    try:
        ev = json.loads(s)
    except ValueError:
        continue
    name = ev.get("event", "")
    if name.split(".")[0] in (act, done_ev.split(".")[0]) or \
            name in ("error", "log"):
        print(json.dumps(ev))
print("EVENTS SEEN: %d" % len(seen))
