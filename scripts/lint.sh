#!/bin/bash
# qmllint over every QML file, failing on any warning.
#
# Worth enforcing rather than glancing at, because the failure this catches is
# invisible at runtime: an unresolvable name in QML evaluates to `undefined`,
# `undefined` assigned to a `color` property is #000000, and the result is a
# black glyph on a black key with nothing in the log. That is why Colors, Router
# and Layouts are declared C++ singletons instead of context properties -- a
# context property cannot be resolved by the linter at all.
#
# Needs the build directory, for the generated qmldir and .qmltypes.
set -uo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
LINT="${QMLLINT:-/usr/lib/qt6/bin/qmllint}"

[[ -x $LINT ]] || LINT=$(command -v qmllint6 || command -v qmllint) || {
  echo "qmllint not found; skipping" >&2; exit 0; }

[[ -d $BUILD_DIR ]] || { echo "no $BUILD_DIR -- run scripts/build.sh first" >&2; exit 1; }

status=0
for file in qml/*.qml; do
  # `|| true` because grep exits 1 on no match, which under `set -e` would kill
  # the script on the files that are clean -- the opposite of what is wanted.
  output=$("$LINT" -I "$BUILD_DIR" "$file" 2>&1) || true
  count=$(printf '%s\n' "$output" | grep -c '^Warning:' || true)
  if [[ $count -gt 0 ]]; then
    printf '%-30s %s warning(s)\n' "$file" "$count"
    printf '%s\n' "$output"
    status=1
  else
    printf '%-30s clean\n' "$file"
  fi
done

if [[ $status -ne 0 ]]; then
  echo
  echo "qmllint found warnings. They are not cosmetic here -- see the header." >&2
fi
exit $status
