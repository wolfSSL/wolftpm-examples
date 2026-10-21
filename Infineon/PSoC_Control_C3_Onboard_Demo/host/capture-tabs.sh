#!/bin/bash
# Capture one screenshot per tab for sharing. Attract is disabled for the
# duration so the loop cannot switch tabs mid-capture, then put back to
# whatever it was, from an EXIT trap so a failure restores it too.
# pipefail as well as errexit: every capture is piped through tail,
# which succeeds whatever shot.py did, so without it a failed shot is
# invisible and the run ends with "done" over stale png files.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")" || exit 1
OUT="${SHOT_DIR:-$HOME/claude-spdm/shots}"
mkdir -p "$OUT"
# The demo may be on another machine; the board does not have to be here.
API="${DEMO_API:-http://127.0.0.1:8080}"

# uart-monitor is the sole reader for every board on the bench, so it holds
# the console and the server's identity probe cannot open it - the header
# then reads "no TPM detected" in every shot. Yield just this one port for
# the run and give it back however we exit.
DEV=""
YIELDED=0
if [ -n "${TPM_ONBOARD_PORT:-}" ] && command -v uart-monitor >/dev/null; then
    DEV="$(readlink -f "$TPM_ONBOARD_PORT")"
    if uart-monitor yield "$DEV" >/dev/null; then YIELDED=1; fi
fi

off() { curl -s -X POST -H 'Content-Type: application/json' \
          -d '{"on":false}' $API/api/attract >/dev/null; }
on()  { curl -s -X POST -H 'Content-Type: application/json' \
          -d '{"on":true}'  $API/api/attract >/dev/null; }

# Whatever the booth was set to before this run, so it can be put back.
# Forcing the loop on afterwards is as wrong as leaving it off: a capture
# run should not change how the booth behaves.
ATTRACT_WAS="$(curl -s $API/api/attract | grep -o '"on"[: ]*true' || true)"

# One EXIT trap for both: it also runs when a signal ends the script,
# whereas INT/TERM handlers would clean up and then carry on shooting.
cleanup() {
    if [ -n "$ATTRACT_WAS" ]; then on; else off; fi
    if [ "$YIELDED" = "1" ]; then
        uart-monitor reclaim "$DEV" >/dev/null
    fi
}
trap cleanup EXIT

shot() { # name  tab  extra-js  wait  height
    off
    # The acts share one serial port and open it per run, so a shot started
    # while the previous act still holds it fails with EBUSY.
    sleep 5
    python3 shot.py "$OUT/$1.png" --url "$API/" --width 1500 --height "$5" \
        --eval "fetch('$API/api/attract',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({on:false})}).then(function(){document.querySelector('.tab[data-tab=$2]').click();$3});" \
        --wait "$4" 2>&1 | tail -1
}

off; sleep 2
shot 1-sign  sign  "setTimeout(function(){document.getElementById('btn-run').click();},1500);" 14 1500
shot 2-seal  seal  ""  22 1150
shot 3-ekcerts ek  ""  25 1150
shot 4-measured-boot pcr "" 25 1250
shot 5-spdm  spdm  "setTimeout(function(){document.getElementById('btn-spdm').click();},1500);" 55 1250
shot 6-pq-tls tls  "setTimeout(function(){document.getElementById('btn-tls').click();},1500);" 70 1300
# The console carries whatever the board printed at its last boot, which the
# server replays to a page that loads afterwards. No button to press.
shot 7-console console "" 6 1150
# Attract goes back to what it was, from the EXIT trap, so it is restored
# on a failure or a signal too.
echo "done"; ls -la "$OUT"
