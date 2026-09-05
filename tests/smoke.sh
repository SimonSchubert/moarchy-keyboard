#!/bin/bash
# End-to-end smoke test, run ON the phone.
#
# Exercises both input paths against a real compositor, because that is the only
# place they mean anything -- a mocked text-input client proves nothing about
# what Sway actually does with a commit_string.
#
#   scp tests/tap.py tests/smoke.sh alarm@<phone>:/tmp/
#   ssh alarm@<phone> /tmp/smoke.sh
#
# Reads the result with grim; look at /tmp/moa-smoke.png afterwards.
set -uo pipefail

# Prefer the copy deployed alongside these scripts; fall back to the one an
# earlier session may have left in /tmp. A reboot clears /tmp, and the failure
# when it is missing is silent and misleading -- see tests/env.sh.
if [[ -f "$(dirname "$0")/env.sh" ]]; then . "$(dirname "$0")/env.sh"
elif [[ -f /tmp/moa-env.sh ]]; then . /tmp/moa-env.sh
else echo "no env.sh next to $0 and none in /tmp" >&2; exit 1
fi

# Wake the panel before anything else. swayidle blanks the output after ten
# minutes, and grim then BLOCKS FOREVER waiting for a frame that will never
# arrive -- it does not fail, it hangs, and so does the ssh session driving it.
# Three stuck grim processes and two dead sessions before this was noticed.
swaymsg "output * power on" >/dev/null 2>&1
sleep 1

BINARY=${BINARY:-/tmp/moarchy-keyboard}

echo "==> restarting the keyboard"
pkill -x moarchy-keyboar 2>/dev/null
sleep 1
QT_LOGGING_RULES="moarchy*=true" setsid "$BINARY" > /tmp/moa.log 2>&1 < /dev/null &
sleep 3

echo "==> opening a terminal that shows every byte it receives"
pkill -f "moa-kbdtes[t]" 2>/dev/null
swaymsg exec "foot -a moa-kbdtest cat -A" >/dev/null 2>&1
sleep 4
swaymsg "[app_id=moa-kbdtest] focus" >/dev/null 2>&1
sleep 2

focused=$(swaymsg -t get_tree | python3 -c '
import json,sys
def walk(n):
    if n.get("focused"): print(n.get("app_id"))
    for k in n.get("nodes",[])+n.get("floating_nodes",[]): walk(k)
walk(json.load(sys.stdin))')
echo "    focused: $focused"

# Terminal layout: 5 rows of 60 logical px starting at y=420, unit = 36.
#   row 0 y450  esc36 tab72 ctrl108 alt144 |180 /216 ~252 up288 del324
#   row 1 y510  q18 w54 e90 r126 t162 y198 u234 i270 o306 p342
#   row 2 y570  a18 s54 d90 f126 g162 h198 j234 k270 l306 return342
#   row 3 y630  shift27 z72 x108 c144 v180 b216 n252 m288 backspace333
#   row 4 y690  abc27 -72 space144 left216 down252 right288 ?123-333
echo "==> typing: h e l l o, then Escape and Tab"
sudo python3 /tmp/tap.py --scale 2 --warmup 180,200 \
  198,570 90,510 306,570 306,570 306,510 \
  36,450 72,450

sleep 1
grim /tmp/moa-smoke.png
echo "==> screenshot at /tmp/moa-smoke.png"

echo "==> keyboard log"
journalctl -n 60 --no-pager 2>/dev/null | grep "moarchy-keyboard\[" | tail -12
