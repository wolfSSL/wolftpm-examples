#!/bin/bash
# Capture one screenshot per tab for sharing. Attract is disabled for the
# duration so the loop cannot switch tabs mid-capture, then restored.
set -u
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
if [ -n "${TPM_ONBOARD_PORT:-}" ] && command -v uart-monitor >/dev/null; then
    DEV="$(readlink -f "$TPM_ONBOARD_PORT")"
    # EXIT alone: it also runs when a signal ends the script, whereas an
    # INT/TERM handler would give the port back and then carry on shooting.
    uart-monitor yield "$DEV" >/dev/null && \
        trap 'uart-monitor reclaim "$DEV" >/dev/null' EXIT
fi

off() { curl -s -X POST -H 'Content-Type: application/json' \
          -d '{"on":false}' $API/api/attract >/dev/null; }
on()  { curl -s -X POST -H 'Content-Type: application/json' \
          -d '{"on":true}'  $API/api/attract >/dev/null; }

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
shot 6-pq-tls tls  ""  50 1200
# Leave attract as the demo was configured, rather than forcing it on: a
# capture run should not change how the booth behaves afterwards.
if [ "${RESTORE_ATTRACT:-0}" = "1" ]; then on; fi
echo "done"; ls -la "$OUT"
