#!/usr/bin/env bash
#
# Load and render every module in the music folder under qemu.
# This is a decoder/format smoke test, not a speed benchmark.
#
#   bash scripts/test-modules.sh [music_dir]
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/chiptune/chiptune}"
SYS="${FUNKEY_SYSROOT:-/opt/FunKey-sdk-2.3.0/arm-funkey-linux-musleabihf/sysroot}"
QEMU="${QEMU:-/usr/bin/qemu-arm-static}"
MUSIC="${1:-$ROOT/KEYGENMUSiC}"

if [ ! -x "$BIN" ]; then
	echo "ERROR: binary not found: $BIN" >&2
	exit 1
fi

rm -rf /tmp/chip-test-cfg
mkdir -p /tmp/chip-test-cfg
cp "$BIN" /tmp/chip-test-bin
chmod +x /tmp/chip-test-bin

export HOME=/tmp/chip-test-home
mkdir -p "$HOME"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export CHIP_CONFIG_DIR=/tmp/chip-test-cfg
export CHIP_MUSIC_DIR="$MUSIC"
export CHIP_TEST_ALL=1

timeout 900 "$QEMU" -L "$SYS" /tmp/chip-test-bin
