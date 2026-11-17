#!/usr/bin/env bash
#
# Smoke test of an ARM binary under qemu (no console needed).
#
#   bash scripts/smoke-test.sh [binary]
#
# Runs the binary for 3 seconds with SDL's dummy video driver and prints
# whatever it writes to stdout plus its log file.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$ROOT/build/chiptune/chiptune}"
SYS="${FUNKEY_SYSROOT:-/opt/FunKey-sdk-2.3.0/arm-funkey-linux-musleabihf/sysroot}"
QEMU="/usr/bin/qemu-arm-static"

if [ ! -x "$QEMU" ]; then
	echo "qemu-arm-static not found; install with: apt-get install -y qemu-user-static" >&2
	exit 1
fi

cp "$BIN" /tmp/hello-test
chmod +x /tmp/hello-test
rm -f /tmp/hello_test.log

echo "== running $BIN under qemu (3 s, dummy video)"
( cd /tmp && HOME=/tmp SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	CHIP_CONFIG_DIR=/tmp/chip-smoke-cfg timeout 3 "$QEMU" -L "$SYS" /tmp/hello-test )
RC=$?

case $RC in
	0)   echo "== exit: 0 (program finished by itself)" ;;
	124) echo "== exit: timeout (still running after 3 s, expected)" ;;
	*)   echo "== exit: $RC (crash?)" ;;
esac

echo "== log file"
cat /tmp/hello_test.log 2>/dev/null || echo "(no log written)"
