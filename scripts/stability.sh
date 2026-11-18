#!/usr/bin/env bash
# Stability pass: swap 5 tracks, then a live run at 60 FPS, then a lock cycle.
set -uo pipefail
ROOT=/mnt/w/ai/agent/anbernic-rg-nano/chiptune-player
SYS=/opt/FunKey-sdk-2.3.0/arm-funkey-linux-musleabihf/sysroot
BIN=$ROOT/build/chiptune/chiptune
QEMU=/usr/bin/qemu-arm-static

cp "$BIN" /tmp/chip-test-bin
chmod +x /tmp/chip-test-bin
export HOME=/tmp/chip-test-home
mkdir -p "$HOME"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

echo "== swap test"
rm -rf /tmp/chip-swap-cfg; mkdir -p /tmp/chip-swap-cfg
CHIP_CONFIG_DIR=/tmp/chip-swap-cfg CHIP_MUSIC_DIR="$ROOT/KEYGENMUSiC" \
	CHIP_SWAP_TEST=1 timeout 40 "$QEMU" -L "$SYS" /tmp/chip-test-bin
echo "swap exit $?"

echo "== lock cycle"
rm -rf /tmp/chip-lock-cfg; mkdir -p /tmp/chip-lock-cfg
CHIP_CONFIG_DIR=/tmp/chip-lock-cfg CHIP_LOCK_TEST=1 \
	timeout 15 "$QEMU" -L "$SYS" /tmp/chip-test-bin
echo "lock exit $?"
cat /tmp/chip-lock-cfg/lock.log 2>/dev/null

echo "== live run, 6 s"
rm -rf /tmp/chip-live-cfg; mkdir -p /tmp/chip-live-cfg
cat > /tmp/chip-live-cfg/config.ini <<EOF
music_dir=$ROOT/KEYGENMUSiC
recursive=1
last_path=$ROOT/KEYGENMUSiC/ACME - GetSmile kg.xm
last_pos=5.00
last_valid=1
start_player=1
resume=1
theme=0
repeat=2
vis=2
smooth=1
quality=1
samplerate=44100
show_splash=0
hints=0
page_slide=1
lock_key=0
vis_style=7
EOF
CHIP_CONFIG_DIR=/tmp/chip-live-cfg CHIP_AUTOPLAY=1 \
	timeout 6 "$QEMU" -L "$SYS" /tmp/chip-test-bin
echo "live exit $?"
grep -E 'last_pos|fps|page_slide|lock_key|vis=' /tmp/chip-live-cfg/config.ini
