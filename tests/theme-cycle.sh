#!/bin/bash
# AC 23 + AC 24: the keyboard recolours on every theme change, not just the first.
#
# THREE changes, deliberately. omarchy-theme-set replaces colors.toml rather
# than editing it, so a watcher registered on the file alone fires once and then
# watches an inode nobody writes to again. With a single change that bug passes.
set -uo pipefail

. /tmp/moa-env.sh
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
