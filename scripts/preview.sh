#!/usr/bin/env bash
#
# Render one screen headlessly under qemu and save a PNG.
#
#   bash scripts/preview.sh                          # player screen
#   bash scripts/preview.sh library                  # library
#   bash scripts/preview.sh settings
#   bash scripts/preview.sh info "ACME - GetSmile kg.xm"
#   bash scripts/preview.sh firstrun
#   CHIP_THEME=BLACK bash scripts/preview.sh player
#
# Output: preview.png (override with OUT=...).
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/chiptune/chiptune}"
SYS="${FUNKEY_SYSROOT:-/opt/FunKey-sdk-2.3.0/arm-funkey-linux-musleabihf/sysroot}"
QEMU="${QEMU:-/usr/bin/qemu-arm-static}"

SCREEN="${CHIP_SCREEN:-${1:-player}}"
TRACK="${2:-${CHIP_TRACK:-}}"
OUT="${OUT:-$ROOT/preview.png}"
CFG=/tmp/chip-preview-cfg
HOME_DIR=/tmp/chip-preview-home

if [ ! -x "$BIN" ]; then
	echo "ERROR: binary not found: $BIN (build first)" >&2
	exit 1
fi

rm -rf "$CFG" "$HOME_DIR"
mkdir -p "$CFG" "$HOME_DIR"
cp "$BIN" /tmp/chip-preview-bin
chmod +x /tmp/chip-preview-bin

export HOME="$HOME_DIR"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export CHIP_CONFIG_DIR="$CFG"
export CHIP_MUSIC_DIR="${CHIP_MUSIC_DIR:-$ROOT/KEYGENMUSiC}"
export CHIP_SCREEN="$SCREEN"
export CHIP_SHOT=/tmp/chip-shot.ppm
[ -n "$TRACK" ] && export CHIP_TRACK="$TRACK"

rm -f /tmp/chip-shot.ppm
timeout 15 "$QEMU" -L "$SYS" /tmp/chip-preview-bin
RC=$?
if [ ! -f /tmp/chip-shot.ppm ]; then
	echo "ERROR: no screenshot produced (exit $RC)" >&2
	exit 1
fi

python3 "$ROOT/scripts/ppm2png.py" /tmp/chip-shot.ppm "$OUT"
echo "wrote $OUT"
