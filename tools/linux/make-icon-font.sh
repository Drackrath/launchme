#!/bin/bash
# Rebuilds res/fonts/launchme-icons.ttf from a pinned release of Fluent UI System Icons (MIT).
# Needs python3 with fontTools (Ubuntu: apt install python3-fonttools).
set -euo pipefail
VERSION="${FLUENT_ICONS_VERSION:-1.1.344}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
for style in Regular Filled; do
  curl -fsSL "https://raw.githubusercontent.com/microsoft/fluentui-system-icons/$VERSION/fonts/FluentSystemIcons-$style.ttf" -o "$WORK/$style.ttf"
done
mkdir -p "$REPO/res/fonts"
python3 "$HERE/make-icon-font.py" "$WORK/Regular.ttf" "$WORK/Filled.ttf" "$REPO/res/fonts/launchme-icons.ttf"
curl -fsSL "https://raw.githubusercontent.com/microsoft/fluentui-system-icons/$VERSION/LICENSE" -o "$REPO/res/fonts/launchme-icons-LICENSE.txt"
