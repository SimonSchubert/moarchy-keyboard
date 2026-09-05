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

# Wake the panel before anything else. swayidle blanks the output after ten
# minutes, and grim then BLOCKS FOREVER waiting for a frame that will never
# arrive -- it does not fail, it hangs, and so does the ssh session driving it.
# Three stuck grim processes and two dead sessions before this was noticed.
swaymsg "output * power on" >/dev/null 2>&1
sleep 1

BINARY=${BINARY:-/tmp/moarchy-keyboard}
pass=0; fail=0; manual=0
ok()   { echo "  PASS  $*"; pass=$((pass+1)); }
no()   { echo "  FAIL  $*"; fail=$((fail+1)); }
eye()  { echo "  EYE   $*"; manual=$((manual+1)); }

restart() {
  # squeekboard first, every time. zwp_input_method_manager_v2 grants one input
  # method per seat: with squeekboard up, ours starts, is told `unavailable`,
  # and exits -- correctly -- after which this suite would go on tapping
  # squeekboard's layout and reporting the results as ours. Something on this
  # phone restores squeekboard between runs, so killing it once at the top is
  # not enough.
  pkill -x squeekboard 2>/dev/null
  pkill -x moarchy-keyboar 2>/dev/null
  sleep 1.5
  QT_LOGGING_RULES="moarchy*=true" setsid "$BINARY" "$@" >/dev/null 2>&1 </dev/null &
  sleep 4

  if ! ours; then
    echo "  ABORT  could not get moarchy-keyboard onto the bus after restart" >&2
    journalctl -n 6 --no-pager 2>/dev/null | grep "moarchy-keyboard\[" | tail -3 >&2
    return 1
  fi
}
kbpid() { pgrep -x moarchy-keyboar | head -1; }

# Assert that the keyboard under test is the one on the bus, before believing
# anything a screenshot or a keystroke says.
#
# This is the most important line in the file. Without it the first run of this
# suite produced a page of confident results about squeekboard: moarchy-keyboard
# had exited, something restored squeekboard, and every subsequent section
# happily tapped squeekboard's layout, screenshotted squeekboard's keys and
# reported them as findings. AC 11 was recorded as a product failure on that
# basis. A test that cannot tell which program it is testing is worse than no
# test, because it produces evidence.
ours() {
  local mine owner
  mine=$(kbpid)
  if [[ -z $mine ]]; then
    echo "  ABORT  moarchy-keyboard is not running" >&2
    return 1
  fi
  owner=$(busctl --user status sm.puri.OSK0 2>/dev/null | sed -n 's/^PID=\([0-9]*\).*/\1/p')
  if [[ $owner != "$mine" ]]; then
    echo "  ABORT  sm.puri.OSK0 is owned by PID ${owner:-nobody}, not our $mine" >&2
    echo "         (something else is the on-screen keyboard -- results would be about it)" >&2
    return 1
  fi
  return 0
}

# Guard a section. Skips rather than reporting a result it cannot stand behind.
section() {
  echo
  echo "=============================================================="
  echo " $*"
  echo "=============================================================="
  if ! ours; then
    echo "  SKIP  not our keyboard on the bus"
    manual=$((manual+1))
    return 1
  fi
  return 0
}

# Leave the compositor with a real WINDOW focused, not merely a workspace.
#
# `swaymsg workspace N` reaches the workspace and does not necessarily land
# focus on anything inside it, and the resulting state -- workspace focused, no
# window focused -- is actively harmful rather than untidy: in it,
# `swaymsg '[app_id=...] focus'` returns success and changes nothing, and no
# toplevel reports itself activated over wlr-foreign-toplevel. That breaks the
# next test as surely as it breaks anyone else's, and this suite spends its time
# driving exactly the focus transitions that produce it.
# The probe reports through its window title, because Qt.application.name does
# not set the Wayland app_id -- that comes from the desktop file name at
# startup, so the window is always org.qt-project.qml and app_id selectors
# match nothing. Everything here goes by title instead.
probe_field() {
  swaymsg -t get_tree 2>/dev/null | python3 -c "
import json, sys, re
field = sys.argv[1]
found = []
def walk(n):
    name = n.get('name') or ''
    if name.startswith('moa probe'):
        m = re.search(field + r'=(\\S+)', name)
        if m: found.append(m.group(1))
    for k in n.get('nodes', []) + n.get('floating_nodes', []): walk(k)
walk(json.load(sys.stdin))
print(found[-1] if found else '')" "$1"
}

probe_taps() { probe_field taps; }

# Wait for a window to actually exist. Every fixed sleep in this file has been
# wrong at least once: this device is slow and its speed varies with what the
# suite itself is doing to it, so `sleep 4` after launching something is a coin
# toss that reads as a product failure when it loses.
wait_for_window() {
  local pattern=$1 tries=${2:-40}
  for _ in $(seq 1 "$tries"); do
    swaymsg -t get_tree 2>/dev/null | grep -q "$pattern" && return 0
    sleep 1
  done
  return 1
}
probe_present() { swaymsg -t get_tree 2>/dev/null | grep -q "moa probe"; }

# Stale probes from an earlier run stack up and the wrong one gets read.
probe_clear() {
  swaymsg '[title="^moa probe"] kill' >/dev/null 2>&1
  sleep 1
}

probe_start() {
  probe_clear
  # Launched directly rather than through `swaymsg exec`, so its output is
  # capturable. Through swaymsg the process is detached and anything it prints
  # is lost, which is why "the probe never opened" stayed unexplained across
  # three runs -- there was no way to see whether qml6 had failed or was merely
  # slow.
  setsid qml6 /tmp/touch-probe.qml > /tmp/moa-probe-out.log 2>&1 < /dev/null &

  # Poll rather than sleep a fixed time, and poll for a LONG time. Starting a
  # second QML engine on this A53, while the keyboard is also running one and
  # the suite is hammering the device, has taken over 20 s -- with qml6 printing
  # nothing at all, because it had not failed, it was simply still starting.
  # Three runs recorded "the probe never opened" on a 20 s budget.
  for _ in $(seq 1 60); do
    probe_present && break
    sleep 1
  done
  if ! probe_present; then
    echo "  probe failed to appear; qml6 said:" >&2
    sed 's/^/    /' /tmp/moa-probe-out.log 2>/dev/null | head -5 >&2
    return 1
  fi

  swaymsg '[title="^moa probe"] focus' >/dev/null 2>&1
  sleep 1
}

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
  swaymsg "[app_id=moa-cycle] kill"   >/dev/null 2>&1
  swaymsg '[title="^moa text"] kill' >/dev/null 2>&1
  swaymsg '[title="^moa probe"] kill' >/dev/null 2>&1
  swaymsg "[app_id=moa-pwtest] kill"  >/dev/null 2>&1
  swaymsg "[title=\"moa password test\"] kill" >/dev/null 2>&1
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
# Timed from INSIDE the process, not with date around an ssh-spawned command.
# The first attempt measured 1369 ms that way and the number included shell
# spawn, journald latency and a 50 ms polling granularity -- none of which is
# the keyboard. The binary now stamps each startup milestone against one
# stopwatch started at the top of main(), and reports FIRST FRAME, which is
# what "first paint" in AC 36 actually means.
S=$(since)
restart || true
sleep 3

echo "  startup timeline:"
log "$S" | grep "startup:" | sed 's/.*startup: /    /'

MS=$(log "$S" | grep "FIRST FRAME" | grep -oE "at [0-9]+ ms" | grep -oE "[0-9]+" | tail -1)
if [[ -z $MS ]]; then
  no "AC 36: no FIRST FRAME mark -- did the surface ever paint?"
else
  echo "  first frame: ${MS} ms"
  [[ $MS -le 800 ]] && ok "AC 36 (${MS} ms to first frame)" || no "AC 36 (${MS} ms > 800)"
fi

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

section "AC 11 -- keycode path with NO input method at all" || true
if ours; then
  # foot cannot answer this: it speaks text-input-v3 even when running htop, so
  # a key sent to it arrives as a commit_string and the keycode path is never
  # exercised. The first two attempts at this AC recorded product failures on
  # that basis. The probe binds no text input at all, so the input method never
  # activates and `q` can only reach it as a keycode.
  restart --layout letters || true
  probe_start
  S=$(since)

  echo "  input method active (expect false -- no text field anywhere):"
  busctl --user get-property sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 Visible 2>&1 | sed 's/^/    Visible: /'
  busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true >/dev/null 2>&1
  sleep 2
  grim /tmp/moa-ac-11.png

  if ! probe_present; then
    no "AC 11: the probe never opened -- inconclusive"
  else
    # letters layout: 4 rows of 75 from y=420, so centres 457/532/607/682.
    # `q` is the first key of row 0, x = unit/2 = 18.
    # Say which layout is actually loaded. Three runs reported AC 11 as a
    # product failure while the mechanism was fine and the tap was simply
    # landing on a different key, because the keyboard was showing `terminal`
    # and the coordinates assumed `letters`.
    echo "  layout in use: $(log "$S" | grep -oE 'loaded "[a-z-]+"' | tail -1)"

    sudo -n python3 /tmp/tap.py --scale 2 --warmup 180,150 18,457 >/dev/null 2>&1
    sleep 3
    if probe_present; then
      no "AC 11: the probe survived; key seen was '$(probe_field key)' (empty means no key arrived at all)"
    else
      ok "AC 11 (the probe quit on a synthesised 'q' with no input method active)"
    fi
  fi
  probe_clear
fi

section "AC 5 -- never steals keyboard focus" || true
if ours; then
swaymsg exec "foot -a moa-focus cat -A" >/dev/null 2>&1
wait_for_window "moa-focus"
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
fi

section "AC 14 -- backspace deletes one character" || true
if ours; then
S=$(since)
# terminal layout: h(198,570) i(270,510) then backspace(333,630)
sudo python3 /tmp/tap.py --scale 2 198,570 270,510 >/dev/null 2>&1
sleep 1
grim /tmp/moa-ac-14-before.png
sudo python3 /tmp/tap.py --scale 2 333,630 >/dev/null 2>&1
sleep 1
grim /tmp/moa-ac-14-after.png
eye "AC 14: compare /tmp/moa-ac-14-before.png ('hi') with -after.png ('h')"
fi

section "AC 13 -- Ctrl+C interrupts" || true
if ours; then
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
fi

section "AC 16 -- password fields suppress previews" || true
if ours; then
S=$(since)
swaymsg exec "qml6 /tmp/password-field.qml" >/dev/null 2>&1 || \
  swaymsg exec "qml /tmp/password-field.qml" >/dev/null 2>&1
sleep 6
grim /tmp/moa-ac-16.png
echo "  content_type seen (purpose 8 = password):"
journalctl --since "$S" --no-pager 2>/dev/null | grep -c "moarchy" >/dev/null
eye "AC 16: /tmp/moa-ac-16.png -- long-press hints must be absent on the password field"
fi

echo
echo "=============================================================="
echo " AC 2 (full) -- 20 activate/deactivate cycles, still one surface"
echo "=============================================================="
pkill -x moarchy-keyboar 2>/dev/null; sleep 1
WAYLAND_DEBUG=1 setsid "$BINARY" >/tmp/moa-wl20.log 2>&1 </dev/null &
sleep 4

# Drives activate/deactivate by alternating between a window that HAS a text
# input and one that has none -- not by switching workspaces.
#
# Two earlier versions of this failed for the same underlying reason. The first
# cycled onto a window this run never creates, so every focus command failed
# silently and activates came out 0 while "1 surface, 0 destroys" read as a
# pass. The second switched to an empty workspace, which reaches a workspace
# without necessarily landing focus on anything in it: 20 cycles produced 3
# activates. It also churned workspace state another session's tests depend on.
#
# foot speaks text-input-v3, so focusing it activates the input method. The QML
# probe has no text field anywhere, so focusing it deactivates. Both are plain
# focus changes, neither touches a workspace, and a cycle that fails to move
# focus shows up as a missing activate rather than as a silent pass.
swaymsg exec "foot -a moa-cycle cat -A" >/dev/null 2>&1
wait_for_window "moa-cycle" || echo "  (moa-cycle never appeared)" >&2
probe_start
if ! wait_for_window "moa-cycle" 5 || ! probe_present; then
  no "AC 2: need both a text window and a no-text window; one did not open"
else
  for _ in $(seq 1 20); do
    swaymsg "[app_id=moa-cycle] focus" >/dev/null 2>&1; sleep 0.8
    swaymsg '[title="^moa probe"] focus' >/dev/null 2>&1; sleep 0.8
  done
fi
swaymsg "[app_id=moa-cycle] kill" >/dev/null 2>&1
probe_clear
A=$(grep -c "zwp_input_method_v2#[0-9]*\.activate" /tmp/moa-wl20.log)
# Guard the guard: with zero activates the surface counts prove nothing.
[[ $A -lt 5 ]] && no "AC 2: only $A activates -- the cycle did not drive the input method"
C=$(grep -c "get_layer_surface" /tmp/moa-wl20.log)
D=$(grep -c "zwlr_layer_surface_v1#[0-9]*\.destroy" /tmp/moa-wl20.log)
echo "  activates=$A  layer surfaces created=$C  destroyed=$D"
if [[ $C -eq 1 && $D -eq 0 && $A -ge 5 ]]; then
  ok "AC 2 ($A cycles, 1 surface, 0 destroys)"
elif [[ $A -ge 5 ]]; then
  no "AC 2 (created $C, destroyed $D)"
fi
restore_focus

section "AC 4b -- retracted passes touches through, raised does not" || true
if ours; then
  # The nastiest failure of the retract path: the surface stays mapped (which is
  # the point -- see src/panel.h) but keeps its input region, so it swallows
  # every touch along the bottom third of the screen while drawing nothing.
  # Invisible, and indistinguishable from an unresponsive app.
  #
  # Tested both ways, because only one direction proves only half of it: down
  # must pass through, up must NOT. A keyboard that always passes touches
  # through cannot be typed on.
  probe_start

  # 180,600 is inside the panel's footprint (the panel occupies y 420..720).
  busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b false >/dev/null 2>&1
  sleep 2
  sudo python3 /tmp/tap.py --scale 2 --warmup 180,200 180,600 >/dev/null 2>&1
  sleep 1
  down=$(probe_taps)

  busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true >/dev/null 2>&1
  sleep 2
  sudo python3 /tmp/tap.py --scale 2 180,600 >/dev/null 2>&1
  sleep 1
  up=$(probe_taps)

  if ! probe_present; then
    no "AC 4b: the touch probe never opened -- inconclusive, not a product result"
    down=""; up=""
  fi
  echo "  taps reaching the app: ${down:-?} with the keyboard down, then ${up:-?} after one more with it up"
  # The warmup tap lands in the app area too, so `down` counts it as well.
  if [[ -z ${down:-} ]]; then
    : # already reported inconclusive
  elif [[ $down -ge 1 && $up -eq $down ]]; then
    ok "AC 4b (passes through when down, blocked when up)"
  elif [[ $down -lt 1 ]]; then
    no "AC 4b: the retracted keyboard swallowed the touch -- invisible wall"
  else
    no "AC 4b: the raised keyboard let a touch through to the app underneath"
  fi
  probe_clear
fi

# --- the touch criteria -----------------------------------------------------
#
# Driven against an ordinary text field, NOT a terminal. foot advertises
# content_purpose = terminal, so the keyboard correctly switches to its terminal
# layout -- five rows of 60 rather than four of 75 -- and coordinates worked out
# for the letters layout land on esc and tab instead. Three criteria were
# measured against the wrong keys before this was noticed, and two of them
# reported product failures on that basis.
#
# The probe puts its field contents in its window title, so these assert on a
# string rather than on a screenshot of a terminal.

text_probe_start() {
  swaymsg '[title="^moa text"] kill' >/dev/null 2>&1
  sleep 1
  setsid qml6 /tmp/text-probe.qml > /tmp/moa-textprobe.log 2>&1 < /dev/null &
  for _ in $(seq 1 60); do
    swaymsg -t get_tree 2>/dev/null | grep -q "moa text" && break
    sleep 1
  done
  swaymsg '[title="^moa text"] focus' >/dev/null 2>&1
  sleep 2
}

text_probe_content() {
  swaymsg -t get_tree 2>/dev/null | python3 -c "
import json, sys, re
found = []
def walk(n):
    name = n.get('name') or ''
    m = re.match(r'moa text \[(.*)\]$', name)
    if m: found.append(m.group(1))
    for k in n.get('nodes', []) + n.get('floating_nodes', []): walk(k)
walk(json.load(sys.stdin))
print(found[-1] if found else '<no probe>')"
}

# letters layout geometry, measured on the device rather than assumed: the
# gesture strip holds an exclusive zone at the bottom, so the panel sits at
# y 405..705 rather than 420..720. Four rows of 75 -> centres 442, 517, 592, 667.
# Columns are unit/2 + n*unit with unit = 36, so q=18, w=54, e=90; a=18 on row 1.

section "AC 30 -- two fingers at once" || true
if ours; then
  restart --layout letters || true
  text_probe_start
  before=$(text_probe_content)
  # q and w, the second landing while the first is still held. A single-touch
  # model drops the second, which is what dropped letters feel like at speed.
  sudo -n python3 /tmp/tap.py --scale 2 --warmup 180,150 --two-finger 18,442 54,442 >/dev/null 2>&1
  sleep 2
  after=$(text_probe_content)
  echo "  field: '$before' -> '$after'"
  if [[ ${#after} -ge 2 ]]; then
    ok "AC 30 (both fingers registered: '$after')"
  else
    no "AC 30 (two fingers produced '${after}')"
  fi
fi

section "AC 31 -- sliding off a key cancels it" || true
if ours; then
  before=$(text_probe_content)
  # Press q, drag to w, release there. Nothing may be emitted: not q, because
  # the finger left it, and not w, because the press did not begin there.
  sudo -n python3 /tmp/tap.py --scale 2 --slide 18,442 54,442 >/dev/null 2>&1
  sleep 2
  after=$(text_probe_content)
  echo "  field: '$before' -> '$after'"
  [[ "$before" == "$after" ]] && ok "AC 31 (nothing emitted)" \
                              || no "AC 31 (slide added '${after#"$before"}')"
fi

section "AC 32 -- long press selects an alternate" || true
if ours; then
  before=$(text_probe_content)
  # Hold `a` past the 400 ms threshold and release without moving. Its first
  # alternate is @, so @ is what must arrive -- not a.
  sudo -n python3 /tmp/tap.py --scale 2 --hold 0.9 18,517 >/dev/null 2>&1
  sleep 2
  after=$(text_probe_content)
  added=${after#"$before"}
  echo "  field: '$before' -> '$after'  (added '$added')"
  case "$added" in
    *@*) ok "AC 32 (long press gave the alternate)" ;;
    *a*) no "AC 32 (gave the base character, so the popup never opened)" ;;
    "")  no "AC 32 (nothing emitted at all)" ;;
    *)   no "AC 32 (gave '$added')" ;;
  esac
fi

section "AC 33 + AC 38 -- feedback and commit latency" || true
if ours; then
  S=$(since)
  sudo -n python3 /tmp/tap.py --scale 2 18,442 54,442 90,442 >/dev/null 2>&1
  sleep 2
  echo "  measured:"
  log "$S" | grep "latency:" | sed 's/.*latency: /    /' | head -8

  frame=$(log "$S" | grep "press to first frame" | grep -oE "frame [0-9]+ ms" | grep -oE "[0-9]+" | sort -n | tail -1)
  wire=$(log "$S" | grep "release to" | grep -oE "wire [0-9]+ ms" | grep -oE "[0-9]+" | sort -n | tail -1)

  # One frame at 60 Hz is 16.7 ms; 17 is that rounded up.
  if [[ -n $frame ]]; then
    [[ $frame -le 17 ]] && ok "AC 33 (worst press-to-frame ${frame} ms)" \
                        || no "AC 33 (worst press-to-frame ${frame} ms > 17)"
  else
    no "AC 33: no frame timing recorded"
  fi
  if [[ -n $wire ]]; then
    [[ $wire -le 50 ]] && ok "AC 38 (worst release-to-wire ${wire} ms)" \
                       || no "AC 38 (worst release-to-wire ${wire} ms > 50)"
  else
    no "AC 38: no wire timing recorded"
  fi
  swaymsg '[title="^moa text"] kill' >/dev/null 2>&1
fi

section "AC 19 -- a user layout overrides the shipped one" || true
if ours; then
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
fi

echo
echo "=============================================================="
echo " Summary"
echo "=============================================================="
echo "  passed: $pass   failed: $fail   needs an eye: $manual"
echo "  screenshots: /tmp/moa-ac-*.png"
