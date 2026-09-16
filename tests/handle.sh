#!/bin/bash
# SPEC.md §4.10 on the device: moving the restore handle, and its corners.
#
#   scp tests/handle.sh tests/env.sh tests/tap.py build/moarchy-keyboard <phone>:/tmp/
#   ssh <phone> 'bash -lc /tmp/handle.sh'
#
# Separate from acceptance.sh so it fits a short slot on the shared phone; it
# uses the same helpers under the same names. Every coordinate is derived from
# the output sway reports, not written for one phone: the PinePhone is 360x720
# at scale 2, the Pixel 3a 360x740 at scale 3.
#
# Where the keyboard is, is read off the focused workspace's rect and never off
# `Visible`, which reports what the keyboard intends (moarchy's gestures.md
# G14a). A tap that raised it leaves the rect shorter by the reservation.
#
# Leaves the phone as it found it: ui.toml restored byte for byte, the handle's
# state file removed, and the packaged keyboard restarted in place of the one
# under test.
set -uo pipefail
if [[ -f "$(dirname "$0")/env.sh" ]]; then . "$(dirname "$0")/env.sh"
else echo "no env.sh next to $0" >&2; exit 1
fi

BINARY=${BINARY:-/tmp/moarchy-keyboard}
TAP=${TAP:-$(dirname "$0")/tap.py}
STATE=${XDG_STATE_HOME:-$HOME/.local/state}/moarchy-keyboard/handle
UI=${XDG_CONFIG_HOME:-$HOME/.config}/omarchy/ui.toml

pass=0; fail=0; manual=0
ok()  { echo "  PASS  $*"; pass=$((pass+1)); }
no()  { echo "  FAIL  $*"; fail=$((fail+1)); }
eye() { echo "  EYE   $*"; manual=$((manual+1)); }

kbpid()  { pgrep -x moarchy-keyboar | head -1; }
setvis() { busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b "$1" >/dev/null 2>&1; }
since()  { date "+%Y-%m-%d %H:%M:%S"; }
log()    { journalctl --since "$1" --no-pager 2>/dev/null | grep "moarchy-keyboard\["; }

ours() {
  local mine owner
  mine=$(kbpid)
  owner=$(busctl --user status sm.puri.OSK0 2>/dev/null | sed -n 's/^PID=\([0-9]*\).*/\1/p')
  [[ -n $mine && $owner == "$mine" && $(tr '\0' ' ' </proc/"$mine"/cmdline) == "$BINARY"* ]]
}

restart() {
  pkill -x moarchy-keyboar 2>/dev/null
  sleep 1.5
  QT_LOGGING_RULES="moarchy*=true" setsid "$BINARY" "$@" >/dev/null 2>&1 </dev/null &
  for _ in $(seq 1 20); do ours && break; sleep 0.5; done
  if ! ours; then
    echo "  ABORT  $BINARY is not the keyboard on sm.puri.OSK0" >&2
    return 1
  fi
  # The first frame, and QML's report of where it put the handle, come after
  # the bus name. A tap before them lands on a one-pixel input region.
  sleep 2
}

# --- geometry, from the compositor ---------------------------------------
swaymsg "output * power on" >/dev/null 2>&1
sleep 1
read -r W H S < <(swaymsg -t get_outputs | python3 -c '
import json, sys
o = [o for o in json.load(sys.stdin) if o.get("active")][0]
print(o["rect"]["width"], o["rect"]["height"], o["scale"])')
[[ -n ${W:-} && -n ${H:-} && -n ${S:-} ]] || { echo "no active output" >&2; exit 1; }

# Main.qml: 54x30, 12 from the right edge, 10 above the strip's 24.
HX=$((W - 12 - 27))
REST_Y=$((H - 24 - 10 - 15))
MID_Y=$((H / 2))
TOP_Y=$((36 + 15))               # the top of the travel, AC 56

ws_h() {
  swaymsg -t get_workspaces | python3 -c '
import json, sys
print([w for w in json.load(sys.stdin) if w["focused"]][0]["rect"]["height"])'
}

touch_() {
  sudo python3 "$TAP" --scale "$S" --width "$(python3 -c "print(round($W*$S))")" \
    --height "$(python3 -c "print(round($H*$S))")" "$@" >/dev/null
}
tap()   { touch_ "$1,$2"; sleep 1.2; }
slide() { touch_ --slide "$1,$2" "$3,$4"; sleep 1.2; }

# Down, and wait until the rect says so. A test that started with the keyboard
# up would read every tap as a failure to raise it.
down() {
  setvis false
  for _ in $(seq 1 20); do [[ $(ws_h) -ge $BASE ]] && return 0; sleep 0.3; done
  echo "  (the keyboard did not go down)" >&2
}
raised() { [[ $(ws_h) -le $((BASE - 100)) ]]; }

# Corner pixel against edge pixel, both inside the handle at rest (AC 59).
# grim takes a logical rect and returns it at the output's scale.
corner_matches_edge() {
  grim -g "$((W - 12 - 54)),$((H - 34 - 30)) 54x30" -t ppm /tmp/moa-handle.ppm 2>/dev/null
  python3 - "$S" <<'PY'
import sys
scale = float(sys.argv[1])
data = open("/tmp/moa-handle.ppm", "rb").read()
parts, i = [], 0
while len(parts) < 4:
    while data[i:i+1].isspace(): i += 1
    j = i
    while not data[j:j+1].isspace(): j += 1
    parts.append(data[i:j]); i = j
i += 1
w = int(parts[1])
def px(x, y):
    o = i + (y * w + x) * 3
    return data[o:o+3]
corner = px(2, 2)
edge = px(2, int(15 * scale))
print("corner", corner.hex(), "edge", edge.hex(), file=sys.stderr)
sys.exit(0 if corner == edge else 1)
PY
}

cp -p "$UI" /tmp/moa-ui.toml.bak 2>/dev/null; had_ui=$?
cleanup() {
  if [[ $had_ui -eq 0 ]]; then cp -p /tmp/moa-ui.toml.bak "$UI"; else rm -f "$UI"; fi
  rm -f "$STATE"
  pkill -x moarchy-keyboar 2>/dev/null
  sleep 1
  swaymsg exec moarchy-keyboard >/dev/null 2>&1
}
trap cleanup EXIT

echo "output ${W}x${H} @${S}; handle rest centre $HX,$REST_Y"

# --- AC 54, 55 -------------------------------------------------------------
echo; echo "== AC 54, 55 -- a drag moves it, and raises nothing"
rm -f "$STATE"
restart || exit 1
setvis false; sleep 1.5
BASE=$(ws_h)
echo "  workspace height with the keyboard down: $BASE"

S0=$(since)
slide "$HX" "$REST_Y" "$HX" "$MID_Y"
h=$(ws_h)
shows=$(log "$S0" | grep -c "showing")
if [[ $h -eq $BASE && $shows -eq 0 ]]; then
  ok "AC 55 (a drag raised nothing: rect $h, showing lines $shows)"
else
  no "AC 55: after a drag the rect is $h (was $BASE), showing lines $shows"
fi
down

tap "$HX" "$REST_Y"
if raised; then no "AC 54: a tap where the handle used to be still raised the keyboard"
else ok "AC 54 (its old place passes the tap through)"; fi
down

tap "$HX" "$MID_Y"
if raised; then ok "AC 54 (a tap on its new place raises the keyboard)"
else no "AC 54: a tap at $HX,$MID_Y did not raise it -- rect $(ws_h)"; fi
down

# --- AC 58 -------------------------------------------------------------------
echo; echo "== AC 58 -- raising and lowering keep its place"
setvis true; sleep 1.5
down; sleep 0.5
tap "$HX" "$MID_Y"
if raised; then ok "AC 58 (back where it was put after an up/down)"
else no "AC 58: after SetVisible true/false a tap at its place did nothing"; fi
down

# The half a tap cannot see: no frame of keys away from the bottom. A margin
# that reached sway in a commit of its own would move whatever buffer was
# already attached -- a retraction's last frame of keys, lifted to where the
# handle lives. So every set_margin has to share its commit with a new buffer.
# The protocol log shows which commit each one rode; what that buffer holds is
# the frame drawn after the state changed, which only an eye can confirm.
pkill -x moarchy-keyboar 2>/dev/null
sleep 1.5
WAYLAND_DEBUG=client setsid "$BINARY" >/dev/null 2>/tmp/moa-wl.log </dev/null &
for _ in $(seq 1 20); do ours && break; sleep 0.5; done
sleep 2
setvis false; sleep 1
slide "$HX" "$MID_Y" "$HX" "$((MID_Y - 60))"
# And back, so AC 57 below finds it where AC 54 put it.
slide "$HX" "$((MID_Y - 60))" "$HX" "$MID_Y"
setvis true; sleep 1.5
setvis false; sleep 1.5
read -r margins lone < <(python3 - <<'PY'
import re
margins = lone = 0
started = pending = attached = False
for line in open("/tmp/moa-wl.log", errors="replace"):
    if " -> " not in line:
        continue
    # The layer surface's first commit carries no buffer by protocol, and the
    # margins set before it are the initial state, not a move.
    if re.search(r"wl_surface#\d+\.attach\(", line):
        started = True
        attached = True
    elif not started:
        continue
    elif re.search(r"zwlr_layer_surface_v1#\d+\.set_margin\(", line):
        margins += 1
        pending, attached = True, False
    elif pending and re.search(r"wl_surface#\d+\.commit\(", line):
        lone += 0 if attached else 1
        pending = False
print(margins, lone)
PY
)
echo "  set_margin after mapping: ${margins:-?}, committed without a new buffer: ${lone:-?}"
if [[ ${margins:-0} -ge 3 && ${lone:-1} -eq 0 ]]; then
  ok "AC 58 (every margin move rode a commit with a new buffer)"
else
  no "AC 58: $margins margin moves, $lone committed on the old buffer"
fi
setvis false
eye "AC 58: that buffer is the new state's -- wf-recorder -o DSI-1 over up/down cycles"

# --- AC 57 -------------------------------------------------------------------
echo; echo "== AC 57 -- it stays where it was put"
stored=$(cat "$STATE" 2>/dev/null)
echo "  state file holds: ${stored:-nothing}"
restart || exit 1
setvis false; sleep 1
tap "$HX" "$MID_Y"
if [[ -n $stored ]] && raised; then ok "AC 57 (survives a keyboard restart)"
else no "AC 57: after a restart a tap at its place did nothing (stored '${stored}')"; fi
down

# The shell's gesture strip reserves the bottom of the screen from the same
# Overlay layer the retracted handle is on, and a restarted shell maps it again,
# after the keyboard. That reordering is what would move a handle placed inside
# the strip's reservation rather than against the screen edge.
#
# From the seat, through sway: restarted over ssh the shell lands seatless.
old_qs=$(pgrep -x quickshell | head -1)
swaymsg exec moarchy-restart-shell >/dev/null 2>&1
new_qs=
for _ in $(seq 1 60); do
  new_qs=$(pgrep -x quickshell | head -1)
  [[ -n $new_qs && $new_qs != "$old_qs" ]] && break
  sleep 0.5
done
# Back when the strip reserves its band again: the rect returns to BASE.
for _ in $(seq 1 60); do [[ $(ws_h) -eq $BASE ]] && break; sleep 0.5; done
sleep 2
if [[ -z $new_qs || $new_qs == "$old_qs" || $(ws_h) -ne $BASE ]]; then
  no "AC 57: the shell did not come back (pid $old_qs -> ${new_qs:-none}, rect $(ws_h) vs $BASE)"
else
  tap "$HX" "$MID_Y"
  if raised; then ok "AC 57 (survives a shell restart, pid $old_qs -> $new_qs)"
  else no "AC 57: after the shell restarted a tap at its place did nothing"; fi
  down
fi

echo junk >"$STATE"
restart || exit 1
setvis false; sleep 1
tap "$HX" "$REST_Y"
if raised; then ok "AC 57 (junk in the file rests it at the bottom)"
else no "AC 57: with junk stored, a tap at rest did nothing"; fi
down

# --- AC 56 -------------------------------------------------------------------
echo; echo "== AC 56 -- the travel has two ends"
rm -f "$STATE"
restart || exit 1
setvis false; sleep 1
# Past the top: the finger goes to y=0 and the handle has to stop at 36. If the
# clamp were missing its centre would be near 15 and a tap at 51 would miss.
slide "$HX" "$REST_Y" "$HX" 0
down
tap "$HX" "$TOP_Y"
if raised; then ok "AC 56 (a drag past the top leaves it at the top of its travel)"
else no "AC 56: a tap at $HX,$TOP_Y missed after dragging to y=0"; fi
down
slide "$HX" "$TOP_Y" "$HX" "$((H - 1))"
down
tap "$HX" "$REST_Y"
if raised; then ok "AC 56 (a drag past the bottom leaves it at rest)"
else no "AC 56: a tap at rest missed after dragging to the bottom edge"; fi
down

# --- AC 59, 60 -----------------------------------------------------------------
echo; echo "== AC 59, 60 -- corners follow ui.toml, live"
rm -f "$STATE" "$UI"
restart || exit 1
setvis false; sleep 1.5
if corner_matches_edge; then no "AC 59: no ui.toml, but the handle's corner is filled like its edge"
else ok "AC 59 (no ui.toml is large: the corner is rounded off)"; fi

round=0
for style in square large square; do
  round=$((round + 1))
  moarchy-ui corners "$style" >/dev/null
  sleep 1
  if [[ $style == square ]]; then
    if corner_matches_edge; then ok "AC 60 (change $round: $style, corner filled)"
    else no "AC 60: change $round to $style left the corner rounded"; fi
  else
    if corner_matches_edge; then no "AC 60: change $round to $style left the corner square"
    else ok "AC 60 (change $round: $style, corner rounded)"; fi
  fi
done

echo
echo "  passed: $pass   failed: $fail   needs an eye: $manual"
(( pass > 0 && fail == 0 ))
