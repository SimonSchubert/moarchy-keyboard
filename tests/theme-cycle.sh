#!/bin/bash
# AC 23 + AC 24: the keyboard recolours on every theme change, not just the first.
#
# THREE changes, deliberately. omarchy-theme-set replaces colors.toml rather
# than editing it, so a watcher registered on the file alone fires once and then
# watches an inode nobody writes to again. With a single change that bug passes.
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
export OMARCHY_PATH="$HOME/.local/share/omarchy"
export PATH="$OMARCHY_PATH/bin:$PATH"

START=$(date "+%Y-%m-%d %H:%M:%S")

for theme in "$@"; do
  echo "--> $theme"
  omarchy-theme-set "$theme" >/dev/null 2>&1
  sleep 8
done

echo
echo "=== palette reloads the keyboard logged ==="
journalctl --since "$START" --no-pager 2>/dev/null \
  | grep "moarchy-keyboard\[" | grep -E "palette|could not read"

count=$(journalctl --since "$START" --no-pager 2>/dev/null \
  | grep "moarchy-keyboard\[" | grep -c "palette")
echo
echo "expected $# reloads, saw $count"
[[ $count -ge $# ]] && echo "PASS" || echo "FAIL"
