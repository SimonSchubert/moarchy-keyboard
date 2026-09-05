#!/bin/bash
# Refresh the vendored Wayland protocol XML from wlroots.
#
# These two are vendored rather than depended on because no Arch package ships
# them: `wlr-protocols` holds only the wlr-* prefixed protocols, and
# /usr/share/wayland-protocols has input-method *v1*, an unrelated protocol Sway
# does not advertise. squeekboard and wvkbd vendor the same two files.
#
# Re-run only deliberately: a protocol bump can change request signatures.
set -euo pipefail

BASE="https://gitlab.freedesktop.org/wlroots/wlroots/-/raw/master/protocol"
cd "$(dirname "$0")/../protocols"

for name in input-method-unstable-v2 virtual-keyboard-unstable-v1; do
  echo "==> $name"
  curl -fsSL --max-time 30 "$BASE/$name.xml" -o "$name.xml.new"
  head -1 "$name.xml.new" | grep -q '<?xml' || { echo "!! not XML, refusing" >&2; exit 1; }
  if cmp -s "$name.xml" "$name.xml.new"; then
    echo "    unchanged"
    rm "$name.xml.new"
  else
    mv "$name.xml.new" "$name.xml"
    echo "    UPDATED -- re-read the request signatures before trusting the build"
  fi
done
