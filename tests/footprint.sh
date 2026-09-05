#!/bin/bash
# AC 35: what does adding this keyboard actually cost the device?
#
# Rewritten after two measurements of the same binary disagreed by 33 MB PSS
# (48.8 then 81.5). Both were single samples with nothing controlled, so neither
# was worth anything. Three things move these numbers on this phone and none of
# them is the program allocating:
#
#   PSS is a SHARE. Every other Qt client running divides the shared Qt and font
#   pages further, so our PSS falls when someone else starts a Qt app and rises
#   when they quit -- with no allocation either way. quickshell alone is not the
#   whole story: a KDE app was running during the first measurement.
#
#   CACHES FILL. quickshell was measured at 315 MB after a restart and 351 MB
#   after a session of use, same process, purely icon and texture caches. A
#   sample taken 8 s after start is not the steady state.
#
#   VISIBILITY MATTERS. Retracted, the root item is invisible and the scene
#   graph has nothing to draw; shown, it does.
#
# So: record the Qt process count with every sample, take cold and warm samples
# of the same process, and measure hidden and shown separately. And report
# Private_Dirty alongside PSS -- it is immune to the sharing effect and is the
# honest answer to "what does adding this process cost", because shared clean
# library pages are already resident and stay resident when it exits.
set -uo pipefail
. /tmp/moa-env.sh

BINARY=${BINARY:-/tmp/moarchy-keyboard}

qt_clients() {
  # Anything mapping libQt6Core shares our library pages.
  local n=0
  for p in /proc/[0-9]*; do
    grep -qs libQt6Core "$p/maps" && n=$((n+1))
  done
  echo "$n"
}

sample() {
  local pid=$1 label=$2
  [[ -d /proc/$pid ]] || { printf "%-34s (process gone)\n" "$label"; return; }
  printf "%-34s Pss %6s  PrivDirty %6s  Anon %6s  Rss %6s   qt=%s\n" "$label" \
    "$(awk '/^Pss:/{print $2}' /proc/$pid/smaps_rollup)" \
    "$(awk '/^Private_Dirty:/{print $2}' /proc/$pid/smaps_rollup)" \
    "$(awk '/^Pss_Anon:/{print $2}' /proc/$pid/smaps_rollup)" \
    "$(awk '/^Rss:/{print $2}' /proc/$pid/smaps_rollup)" \
    "$(qt_clients)"
}

# Show/hide cycles and real keystrokes across several layouts, to fill whatever
# caches are going to fill: glyph atlases for each layout's labels, scene graph
# nodes, GL buffers.
exercise() {
  local pid=$1
  for _ in 1 2 3 4 5 6; do
    show true;  sleep 1
    show false; sleep 1
  done
  show true; sleep 1
  # Every layout, so each one's glyphs get rasterised at least once.
  sudo python3 /tmp/tap.py --scale 2 --warmup 180,200 \
    18,457 90,457 162,457 234,457 306,457 \
    18,532 90,532 162,532 \
    27,682 >/dev/null 2>&1
  sleep 2
}

show() { busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b "$1" >/dev/null 2>&1; }

run() {
  local label="$1"; shift

  # One input method per seat: without this every variant starts, is told
  # unavailable, and exits -- which the first version of this script reported as
  # six identical "FAILED TO START" lines that said nothing about memory.
  pkill -x squeekboard 2>/dev/null
  pkill -x moarchy-keyboar 2>/dev/null
  sleep 2

  env "$@" setsid "$BINARY" >/dev/null 2>&1 </dev/null &
  sleep 10
  local pid; pid=$(pgrep -x moarchy-keyboar | head -1)
  if [[ -z $pid ]]; then
    printf "%-34s FAILED TO START\n" "$label"
    journalctl -n 4 --no-pager 2>/dev/null | grep "moarchy-keyboard\[" | tail -2
    return
  fi

  show false; sleep 3;  sample "$pid" "$label  cold, hidden"
  show true;  sleep 3;  sample "$pid" "$label  cold, shown"

  # Then USE it, because the hypothesis under test is that the spread is caches
  # filling rather than a real difference. quickshell measures 315 MB right
  # after a restart and 351 MB after a session of use -- same process, same
  # code, a 36 MB spread that looks a great deal like the 33 MB spread between
  # this keyboard's two contradictory readings. If that is what this is, neither
  # endpoint is the answer and the plateau is.
  exercise "$pid"

  show false; sleep 3;  sample "$pid" "$label  after use, hidden"
  show true;  sleep 3;  sample "$pid" "$label  after use, shown"

  sleep 120
  show true;  sleep 3;  sample "$pid" "$label  plateau (+2 min idle), shown"
  echo
}

echo "quickshell running: $(pgrep -c -x quickshell)   Qt clients at start: $(qt_clients)"
echo

run "baseline"
run "transient images"  QSG_TRANSIENT_IMAGES=1
run "basic loop+transient+nojit" QSG_TRANSIENT_IMAGES=1 QSG_RENDER_LOOP=basic QV4_FORCE_INTERPRETER=1

echo "=== squeekboard, measured the same way, for a fair comparison ==="
pkill -x moarchy-keyboar 2>/dev/null; sleep 1
setsid squeekboard >/dev/null 2>&1 </dev/null &
sleep 10
sq=$(pgrep -x squeekboard | head -1)
if [[ -n $sq ]]; then
  sample "$sq" "squeekboard  cold"
  sleep 50
  sample "$sq" "squeekboard  warm"
else
  echo "squeekboard did not start"
fi

echo
echo "Private_Dirty is the number to compare. PSS moves with how many other Qt"
echo "clients happen to be running and says as much about them as about us."
