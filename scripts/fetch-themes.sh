#!/bin/bash
# Fetch every Omarchy theme's colors.toml, so the contrast sweep can run
# without a phone.
#
#   ./scripts/fetch-themes.sh /tmp/omarchy-themes
#   QT_QPA_PLATFORM=offscreen ./build/moarchy-keyboard --check-themes /tmp/omarchy-themes
#
# Pinned to the same commit mobileomarchy vendors (v4.0.2), so the sweep checks
# the themes the phone will actually have.
set -uo pipefail

DEST="${1:-/tmp/omarchy-themes}"
PIN="${OMARCHY_PIN:-346e69e1cec6c4e8924531874af6ba010a1bc99e}"
BASE="https://raw.githubusercontent.com/basecamp/omarchy/$PIN/themes"

THEMES=(
  catppuccin catppuccin-latte ethereal everforest flexoki-light gruvbox
  hackerman kanagawa last-horizon lumon lupine matte-black miasma nord
  osaka-jade retro-82 ristretto rose-pine solitude tokyo-night vantablack white
)

mkdir -p "$DEST"
missing=0
for theme in "${THEMES[@]}"; do
  mkdir -p "$DEST/$theme"
  if ! curl -fsSL --max-time 20 "$BASE/$theme/colors.toml" -o "$DEST/$theme/colors.toml"; then
    echo "!! could not fetch $theme" >&2
    missing=$((missing+1))
  fi
done

echo "==> $(find "$DEST" -name colors.toml | wc -l | tr -d ' ') palettes in $DEST"
exit $(( missing > 0 ? 1 : 0 ))
