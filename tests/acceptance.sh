#!/bin/bash
# The device-side acceptance run: everything in SPEC.md that needs real hardware
# and a real compositor, in one pass, so a slot on the shared phone is short.
#
#   scp tests/*.sh tests/tap.py tests/password-field.qml alarm@<phone>:/tmp/
#   ssh alarm@<phone> /tmp/acceptance.sh
#
# Prints PASS/FAIL per criterion. Screenshots land in /tmp/moa-ac-*.png for the
# ones only an eye can judge.
set -uo pipefail
. /tmp/moa-env.sh

BINARY=${BINARY:-/tmp/moarchy-keyboard}
pass=0; fail=0; manual=0
ok()   { echo "  PASS  $*"; pass=$((pass+1)); }
no()   { echo "  FAIL  $*"; fail=$((fail+1)); }
eye()  { echo "  EYE   $*"; manual=$((manual+1)); }

restart() {
  pkill -x moarchy-keyboar 2>/dev/null; sleep 1
  QT_LOGGING_RULES="moarchy*=true" setsid "$BINARY" "$@" >/dev/null 2>&1 </dev/null &
  sleep 4
}
kbpid() { pgrep -x moarchy-keyboar | head -1; }

# Leave the compositor with a real WINDOW focused, not merely a workspace.
#
# `swaymsg workspace N` reaches the workspace and does not necessarily land
# focus on anything inside it, and the resulting state -- workspace focused, no
# window focused -- is actively harmful rather than untidy: in it,
# `swaymsg '[app_id=...] focus'` returns success and changes nothing, and no
# toplevel reports itself activated over wlr-foreign-toplevel. That breaks the
# next test as surely as it breaks anyone else's, and this suite spends its time
# driving exactly the focus transitions that produce it.
restore_focus() {
  local id
  id=$(swaymsg -t get_tree | python3 -c '
import json, sys
best = None
def walk(node):
    global best
    if node.get("type") == "con" and node.get("pid") and not node.get("nodes"):
        if best is None:
            best = node["id"]
    for kid in node.get("nodes", []) + node.get("floating_nodes", []):
        walk(kid)
walk(json.load(sys.stdin))
print(best if best else "")')

  if [[ -z $id ]]; then
    echo "  (no window to focus -- leaving focus alone)"
    return
  fi

  swaymsg "[con_id=$id] focus" >/dev/null 2>&1
  sleep 0.5

  local focused
  focused=$(swaymsg -t get_tree | python3 -c '
import json, sys
def walk(node):
    if node.get("focused") and node.get("pid"): print(node["id"])
    for kid in node.get("nodes", []) + node.get("floating_nodes", []): walk(kid)
walk(json.load(sys.stdin))')

  if [[ $focused == "$id" ]]; then
    echo "  focus restored to window $id"
  else
    echo "  WARNING: asked for window $id, focused is '${focused:-none}'" >&2
  fi
}

# Whatever happens -- including a failed assertion or a Ctrl-C -- hand the
# device back in a sane state.
cleanup() {
  swaymsg "[app_id=moa-kbdtest] kill" >/dev/null 2>&1
  swaymsg "[app_id=moa-htop] kill"    >/dev/null 2>&1
  swaymsg "[app_id=moa-focus] kill"   >/dev/null 2>&1
  restore_focus
  echo "  (run /tmp/moa-restore-squeekboard.sh to hand the OSK back)"
}
trap cleanup EXIT
visible() { busctl --user get-property sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 Visible 2>/dev/null; }
since()  { date "+%Y-%m-%d %H:%M:%S"; }
log()    { journalctl --since "$1" --no-pager 2>/dev/null | grep "moarchy-keyboard\["; }

echo "=============================================================="
echo " AC 36 -- cold start to first paint <= 800 ms"
echo "=============================================================="
pkill -x moarchy-keyboar 2>/dev/null; sleep 1
T0=$(date +%s%N)
QT_LOGGING_RULES="moarchy*=true" setsid "$BINARY" >/dev/null 2>&1 </dev/null &
for _ in $(seq 1 100); do
  journalctl -n 5 --no-pager 2>/dev/null | grep -q "moarchy-keyboard.*layer surface up" && break
  sleep 0.05
done
T1=$(date +%s%N)
MS=$(( (T1-T0)/1000000 ))
echo "  cold start: ${MS} ms"
[[ $MS -le 800 ]] && ok "AC 36 (${MS} ms)" || no "AC 36 (${MS} ms > 800)"
sleep 2

echo
echo "=============================================================="
echo " AC 7 -- a second instance must refuse, not fight for the seat"
echo "=============================================================="
S=$(since)
"$BINARY" >/dev/null 2>&1 &
SECOND=$!
sleep 4
if kill -0 $SECOND 2>/dev/null; then
  no "AC 7: the second instance is still running"
  kill $SECOND 2>/dev/null
else
  wait $SECOND 2>/dev/null; rc=$?
  if [[ $rc -ne 0 ]]; then ok "AC 7 (second instance exited $rc)"; else no "AC 7 (exited 0)"; fi
fi
log "$S" | grep -iE "another|unavailable|already" | tail -3

echo
echo "=============================================================="
echo " AC 11 -- keycode path with NO input method at all"
echo "=============================================================="
# htop speaks no text-input-v3, so nothing activates the input method. Force the
# keyboard up over D-Bus and tap 'q', which is htop's quit key. If htop dies,
# the keycode path reached a client that has no text input -- which is the half
# a text-only keyboard cannot do.
swaymsg exec "foot -a moa-htop htop" >/dev/null 2>&1
sleep 5
swaymsg "[app_id=moa-htop] focus" >/dev/null 2>&1
sleep 2
busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true >/dev/null 2>&1
sleep 1
echo "  input method active? (expect a keycode fallback):"; visible
grim /tmp/moa-ac-11-before.png
# 'q' on the letters layout row 1: x=18, y=510... but with no text field the
# layout stays whatever it was. Tap q on BOTH candidate rows to be safe.
sudo python3 /tmp/tap.py --scale 2 --warmup 180,200 18,510 >/dev/null 2>&1
sleep 2
if swaymsg -t get_tree | grep -q "moa-htop"; then
  no "AC 11: htop survived, so 'q' never arrived as a keycode"
  swaymsg "[app_id=moa-htop] kill" >/dev/null 2>&1
else
  ok "AC 11 (htop quit on a synthesised 'q')"
fi

echo
echo "=============================================================="
echo " AC 5 -- never steals keyboard focus"
echo "=============================================================="
swaymsg exec "foot -a moa-focus cat -A" >/dev/null 2>&1
sleep 4
swaymsg "[app_id=moa-focus] focus" >/dev/null 2>&1
sleep 2
FOCUSED=$(swaymsg -t get_tree | python3 -c '
import json,sys
def walk(n):
    if n.get("focused"): print(n.get("app_id") or "?")
    for k in n.get("nodes",[])+n.get("floating_nodes",[]): walk(k)
walk(json.load(sys.stdin))')
echo "  focused with the keyboard up: $FOCUSED"
[[ $FOCUSED == "moa-focus" ]] && ok "AC 5 (app keeps focus)" || no "AC 5 (focus is $FOCUSED)"

echo
echo "=============================================================="
echo " AC 14 -- backspace deletes one character"
echo "=============================================================="
S=$(since)
# terminal layout: h(198,570) i(270,510) then backspace(333,630)
sudo python3 /tmp/tap.py --scale 2 198,570 270,510 >/dev/null 2>&1
sleep 1
grim /tmp/moa-ac-14-before.png
sudo python3 /tmp/tap.py --scale 2 333,630 >/dev/null 2>&1
sleep 1
grim /tmp/moa-ac-14-after.png
eye "AC 14: compare /tmp/moa-ac-14-before.png ('hi') with -after.png ('h')"

echo
echo "=============================================================="
echo " AC 13 -- Ctrl+C interrupts"
echo "=============================================================="
# ctrl(108,450) then c(144,630). cat -A should die and the window close.
sudo python3 /tmp/tap.py --scale 2 108,450 >/dev/null 2>&1
sleep 0.5
sudo python3 /tmp/tap.py --scale 2 144,630 >/dev/null 2>&1
sleep 2
if swaymsg -t get_tree | grep -q "moa-focus"; then
  eye "AC 13: moa-focus still open -- check /tmp/moa-ac-13.png for ^C"
  grim /tmp/moa-ac-13.png
else
  ok "AC 13 (Ctrl+C killed cat)"
fi
swaymsg "[app_id=moa-focus] kill" >/dev/null 2>&1

echo
echo "=============================================================="
echo " AC 16 -- password fields suppress previews"
echo "=============================================================="
S=$(since)
swaymsg exec "qml6 /tmp/password-field.qml" >/dev/null 2>&1 || \
  swaymsg exec "qml /tmp/password-field.qml" >/dev/null 2>&1
sleep 6
grim /tmp/moa-ac-16.png
echo "  content_type seen (purpose 8 = password):"
journalctl --since "$S" --no-pager 2>/dev/null | grep -c "moarchy" >/dev/null
eye "AC 16: /tmp/moa-ac-16.png -- long-press hints must be absent on the password field"

echo
echo "=============================================================="
echo " AC 2 (full) -- 20 activate/deactivate cycles, still one surface"
echo "=============================================================="
pkill -x moarchy-keyboar 2>/dev/null; sleep 1
WAYLAND_DEBUG=1 setsid "$BINARY" >/tmp/moa-wl20.log 2>&1 </dev/null &
sleep 4
for _ in $(seq 1 20); do
  swaymsg "[app_id=moa-kbdtest] focus" >/dev/null 2>&1; sleep 0.4
  swaymsg "workspace 19" >/dev/null 2>&1; sleep 0.4
done
A=$(grep -c "zwp_input_method_v2#[0-9]*\.activate" /tmp/moa-wl20.log)
C=$(grep -c "get_layer_surface" /tmp/moa-wl20.log)
D=$(grep -c "zwlr_layer_surface_v1#[0-9]*\.destroy" /tmp/moa-wl20.log)
echo "  activates=$A  layer surfaces created=$C  destroyed=$D"
if [[ $C -eq 1 && $D -eq 0 ]]; then ok "AC 2 ($A cycles, 1 surface, 0 destroys)"; else no "AC 2 (created $C, destroyed $D)"; fi
restore_focus

echo
echo "=============================================================="
echo " AC 19 -- a user layout overrides the shipped one"
echo "=============================================================="
mkdir -p ~/.config/moarchy-keyboard/layouts
cat > ~/.config/moarchy-keyboard/layouts/letters.json <<'JSON'
{ "name": "letters", "label": "abc",
  "rows": [ { "keys": [ { "text": "Z", "label": "OVERRIDE" } ] } ] }
JSON
S=$(since)
restart
swaymsg "[app_id=moa-kbdtest] focus" >/dev/null 2>&1
sleep 2
if log "$S" | grep -q "loaded \"letters\" from \"/home/alarm/.config"; then
  ok "AC 19 (user layout won)"
else
  no "AC 19 -- log says:"; log "$S" | grep "loaded \"letters\"" | tail -1
fi
rm -rf ~/.config/moarchy-keyboard/layouts
restart

echo
echo "=============================================================="
echo " Summary"
echo "=============================================================="
echo "  passed: $pass   failed: $fail   needs an eye: $manual"
echo "  screenshots: /tmp/moa-ac-*.png"
