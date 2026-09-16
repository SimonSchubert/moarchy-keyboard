#!/bin/bash
# AC 59, offline: the handle reads ui.toml the way moarchy's shell does.
#
#   ./scripts/check-ui.sh build/moarchy-keyboard
#
# The expected column is not this parser's own output copied back. It is what
# moarchy's default/omarchy/plugins/moarchy.common/Ui.js returned for the same
# text, run under node on 2026-09-16 -- the file is the contract, and this one
# can only mirror it. If a preset or an alias moves there, re-run it there and
# move the row here.
set -uo pipefail

BIN=${1:?usage: $0 <moarchy-keyboard binary>}
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

checked=0
failed=0
expect() {
  local want=$1 text=$2 got
  printf '%b' "$text" >"$work/ui.toml"
  got=$("$BIN" --check-ui "$work/ui.toml" 2>/dev/null)
  checked=$((checked + 1))
  if [[ $got != "$want" ]]; then
    printf '    %-28s want %s, got %s\n' "$text" "$want" "${got:-nothing}"
    failed=$((failed + 1))
  fi
}

expect "corners=large tile=20"  'corners = "large"\n'
expect "corners=modest tile=8"  'corners = "modest"\n'
expect "corners=square tile=0"  'corners = "square"\n'
expect "corners=square tile=0"  'corners = "none"\n'
expect "corners=modest tile=8"  'corners = "soft"\n'
expect "corners=large tile=20"  'corners = "bogus"\n'
expect "corners=modest tile=5"  '# c\n[x]\ncorners = modest # trailing\ntile = 5\n'
expect "corners=square tile=0"  'corners = "square"\ntile = -3\n'
expect "corners=large tile=20"  'tile = abc\n'
expect "corners=large tile=20"  ''

# And a file that is not there at all, which is the phone that has never
# picked a corner style.
got=$("$BIN" --check-ui "$work/absent.toml" 2>/dev/null)
checked=$((checked + 1))
[[ $got == "corners=large tile=20" ]] || {
  echo "    missing file: want corners=large tile=20, got ${got:-nothing}"
  failed=$((failed + 1))
}

echo "    $checked ui.toml readings, $failed wrong"
# A run that checked nothing has not passed.
(( checked == 11 && failed == 0 ))
