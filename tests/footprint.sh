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
  local pss rss anon dirty
  pss=$(awk '/^Pss:/ {print $2}' /proc/$pid/smaps_rollup)
  rss=$(awk '/^Rss:/ {print $2}' /proc/$pid/smaps_rollup)
  anon=$(awk '/^Pss_Anon:/ {print $2}' /proc/$pid/smaps_rollup)
  dirty=$(awk '/^Private_Dirty:/ {print $2}' /proc/$pid/smaps_rollup)
  printf "%-30s Pss %6d  PrivDirty %6d  Anon %6d  Rss %6d\n" \
    "$label" "$pss" "$dirty" "$anon" "$rss"
}

# Free the seat first. zwp_input_method_manager_v2 grants one input method per
# seat, so with squeekboard (or a leftover instance) running, every variant here
# starts, refuses and exits -- which the first run of this script reported as
# six identical "FAILED TO START" lines that said nothing about memory.
pkill -x squeekboard 2>/dev/null
pkill -x moarchy-keyboar 2>/dev/null
sleep 2

echo "quickshell running: $(pgrep -c -x quickshell)  (sharing depends on it)"
echo "seat free: $([ -z "$(pgrep -x squeekboard)" ] && echo yes || echo NO)"
echo

measure "baseline"            X=1
measure "atlas 256"           QSG_ATLAS_WIDTH=256 QSG_ATLAS_HEIGHT=256
measure "basic render loop"   QSG_RENDER_LOOP=basic
measure "no JS JIT"           QV4_FORCE_INTERPRETER=1
measure "transient images"    QSG_TRANSIENT_IMAGES=1
measure "atlas+basic+nojit"   QSG_ATLAS_WIDTH=256 QSG_ATLAS_HEIGHT=256 \
                              QSG_RENDER_LOOP=basic QV4_FORCE_INTERPRETER=1

echo
echo "squeekboard on this device 2026-09-05, for comparison:"
echo "  Pss 50822   PrivDirty 41458   Rss 74400"
echo
echo "NOTE ON THE METRIC. AC 35 is written against Pss, and Pss charges us half"
echo "of every library page we share with quickshell -- pages that are already"
echo "resident and would STAY resident if this process exited. Private_Dirty is"
echo "the honest measure of what adding the process actually costs the device."
echo "Report both; do not quietly switch to whichever one flatters the result."
