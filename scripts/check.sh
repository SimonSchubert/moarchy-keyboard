#!/bin/bash
# Everything that can be checked without a phone, in one command.
#
#   docker run --rm --platform linux/arm64 -v "$PWD:/src" -w /src \
#     moarchy-keyboard-build ./scripts/check.sh
#
# What is deliberately NOT here: anything about typing, touch, memory or
# timing. Those need a real compositor and real hardware and live in tests/.
# This is the part that should never be allowed to break.
set -uo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BIN="$BUILD_DIR/moarchy-keyboard"
export QT_QPA_PLATFORM=offscreen

status=0
step() {
  echo
  echo "=== $1 ==="
  shift
  if "$@"; then echo "    ok"; else echo "    FAILED" >&2; status=1; fi
}

step "build (includes qmllint, which fails on any warning)" ./scripts/build.sh

step "layouts are internally consistent" "$BIN" --check-layouts

step "nothing typed reaches the log" "$BUILD_DIR/log-redaction-test"

step "the generated keymap compiles" bash -c "'$BIN' --dump-keymap >/dev/null"

step "ui.toml reads as moarchy's Ui.js reads it (AC 59)" ./scripts/check-ui.sh "$BIN"

THEMES="${THEMES:-/tmp/omarchy-themes}"
if [[ ! -d $THEMES ]]; then
  echo
  echo "=== fetching Omarchy themes for the contrast sweep ==="
  ./scripts/fetch-themes.sh "$THEMES" || echo "    (offline? skipping the sweep)"
fi
if [[ -d $THEMES ]]; then
  step "every theme is legible (WCAG AA)" "$BIN" --check-themes "$THEMES"
fi

echo
if [[ $status -eq 0 ]]; then
  echo "==> all offline checks passed"
else
  echo "==> SOMETHING FAILED" >&2
fi
exit $status
