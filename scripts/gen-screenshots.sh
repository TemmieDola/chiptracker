#!/usr/bin/env bash
#
# Regenerate the screenshots in docs/ from the current ARM build.
# Run under WSL: bash scripts/gen-screenshots.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TRACK="ADMINCRACK - EmEditor 10.x 32-64 crk.xm"
INFO="ACME - GetSmile kg.xm"
WIDE="AGAiN - FairStars MP3 Recorderkg.XM"

CHIP_ORDER=2 CHIP_POS=12 CHIP_HINTS=2 OUT=docs/screenshot-player.png \
	bash scripts/preview.sh player "$TRACK"
OUT=docs/screenshot-library.png bash scripts/preview.sh library "$TRACK"
CHIP_SET_SEL=0 OUT=docs/screenshot-settings.png bash scripts/preview.sh settings
CHIP_SET_SEL=16 CHIP_SET_DESC=1 \
	OUT=docs/screenshot-settings-system.png bash scripts/preview.sh settings
CHIP_BATTERY=87 CHIP_STATS=1 OUT=docs/screenshot-stats.png \
	bash scripts/preview.sh player "$TRACK"
OUT=docs/screenshot-info.png bash scripts/preview.sh info "$INFO"
OUT=docs/screenshot-help.png bash scripts/preview.sh help
OUT=docs/screenshot-splash.png bash scripts/preview.sh splash
CHIP_LOCK_TEST=1 OUT=docs/screenshot-lock.png \
	bash scripts/preview.sh player

for s in 0 1 2 3 4 5 6; do
	case $s in
	0) name=vis-spectrum ;;
	1) name=vis-radial ;;
	2) name=vis-scope ;;
	3) name=vis-waterfall ;;
	4) name=vis-plasma ;;
	5) name=vis-tunnel ;;
	6) name=vis-stars ;;
	esac
	CHIP_VIS_STYLE=$s CHIP_HINTS=2 CHIP_POS=30 CHIP_BATTERY=87 \
		OUT="docs/screenshot-$name.png" \
		bash scripts/preview.sh vis "$TRACK"
done

CHIP_VIS_STYLE=7 CHIP_POS=30 OUT=docs/screenshot-vis-pattern.png \
	bash scripts/preview.sh vis "$WIDE"

CHIP_VIS_STYLE=0 CHIP_VIS_P2=1 CHIP_HINTS=2 CHIP_POS=30 \
	OUT=docs/screenshot-vis-sakura.png bash scripts/preview.sh vis "$TRACK"

echo "screenshots updated in docs/"
