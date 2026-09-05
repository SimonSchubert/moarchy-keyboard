#!/bin/bash
# AC 35: measure PSS under different Qt Quick settings, to find where the memory
# actually goes before arguing about the target.
#
# The first measurement (48.8 MB PSS against squeekboard's 50.8) missed the
# 25 MB target badly, and the breakdown said the Qt libraries are NOT the
# problem -- they are shared with the running quickshell and cost ~12.6 MB PSS
# between them. The cost is ~24 MB of anonymous memory: 16.7 MB unnamed plus
# 7 MB heap. This script attributes that.
#
# Run each variant with the shell up, since sharing is the whole argument.
set -uo pipefail
. /tmp/moa-env.sh

BINARY=${BINARY:-/tmp/moarchy-keyboard}

measure() {
  local label="$1"; shift
  pkill -x moarchy-keyboar 2>/dev/null; sleep 1
  env "$@" setsid "$BINARY" >/dev/null 2>&1 </dev/null &
  sleep 6
  local pid; pid=$(pgrep -x moarchy-keyboar | head -1)
  if [[ -z $pid ]]; then echo "$label: FAILED TO START"; return; fi
  local pss rss anon
  pss=$(awk '/^Pss:/ {print $2}' /proc/$pid/smaps_rollup)
  rss=$(awk '/^Rss:/ {print $2}' /proc/$pid/smaps_rollup)
  anon=$(awk '/^Pss_Anon:/ {print $2}' /proc/$pid/smaps_rollup)
  printf "%-38s Pss %6d kB   Rss %6d kB   Anon %6d kB\n" "$label" "$pss" "$rss" "$anon"
}

echo "quickshell running: $(pgrep -c -x quickshell)  (sharing depends on it)"
echo

measure "baseline (defaults)"
measure "atlas 256"          QSG_ATLAS_WIDTH=256 QSG_ATLAS_HEIGHT=256
measure "basic render loop"  QSG_RENDER_LOOP=basic
measure "atlas 256 + basic"  QSG_ATLAS_WIDTH=256 QSG_ATLAS_HEIGHT=256 QSG_RENDER_LOOP=basic
measure "no qml disk cache"  QML_DISABLE_DISK_CACHE=1

echo
echo "for reference, squeekboard measured on this device 2026-09-05:"
echo "  Pss 50822 kB   Rss 74400 kB"
