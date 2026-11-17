#!/usr/bin/env bash
#
# One-time setup inside WSL (Ubuntu): build tools + official FunKey SDK 2.3.0.
#
#   bash scripts/setup-sdk.sh
#
# If a copy of FunKey-sdk-2.3.0.tar.gz was already downloaded on the Windows
# side to P:\temp\opencode, it is copied instead of downloaded again.

set -euo pipefail

SDK_VER="2.3.0"
SDK_DIR="$HOME/FunKey-sdk-$SDK_VER"
TARBALL="FunKey-sdk-$SDK_VER.tar.gz"
URL="https://github.com/FunKey-Project/FunKey-OS/releases/download/FunKey-OS-$SDK_VER/$TARBALL"

echo "== apt: build tools"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
	build-essential squashfs-tools file curl ca-certificates

if [ -d "$SDK_DIR" ]; then
	echo "== $SDK_DIR already exists, skipping unpack"
else
	if [ ! -f "$HOME/$TARBALL" ]; then
		WIN_COPY="/mnt/p/temp/opencode/$TARBALL"
		if [ -f "$WIN_COPY" ]; then
			echo "== copying $WIN_COPY"
			cp "$WIN_COPY" "$HOME/$TARBALL"
		else
			echo "== downloading $TARBALL (~223 MB)"
			curl -L --fail -o "$HOME/$TARBALL" "$URL"
		fi
	fi

	echo "== unpacking"
	tar xzf "$HOME/$TARBALL" -C "$HOME"
fi

RELOCATE="$(find "$SDK_DIR" -maxdepth 2 -name 'relocate-sdk.sh' | head -n 1 || true)"

if [ -n "$RELOCATE" ]; then
	echo "== $RELOCATE"
	(cd "$(dirname "$RELOCATE")" && ./relocate-sdk.sh)
fi

echo
echo "== done. SDK: $SDK_DIR"
ls "$SDK_DIR"
echo
echo "Next: bash scripts/build.sh"
