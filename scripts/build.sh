#!/usr/bin/env bash
#
# Build an application and pack it into an .opk for FunKey OS (RG Nano).
#
#   bash scripts/build.sh            # builds ./hello
#   APP=hello bash scripts/build.sh  # same, explicit
#
# Requires: FunKey SDK 2.3.0 (see scripts/setup-sdk.sh), squashfs-tools.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${APP:-app}"

if [ ! -d "$ROOT/$APP" ]; then
	echo "ERROR: no such app directory: $ROOT/$APP" >&2
	exit 1
fi

# ---------------------------------------------------------------- SDK ---
SDK="${FUNKEY_SDK:-$HOME/FunKey-sdk-2.3.0}"

if [ ! -d "$SDK" ] && [ -d /opt/FunKey-sdk ]; then
	SDK=/opt/FunKey-sdk
fi

if [ ! -d "$SDK" ]; then
	for candidate in /opt/FunKey-sdk-* "$HOME"/FunKey-sdk-*; do
		[ -d "$candidate" ] && SDK="$candidate" && break
	done
fi

if [ -d "$SDK" ]; then
	SDK="$(readlink -f "$SDK")"
fi

if [ ! -d "$SDK" ]; then
	echo "ERROR: FunKey SDK not found in '$SDK'." >&2
	echo "       Run: bash scripts/setup-sdk.sh" >&2
	echo "       or:  FUNKEY_SDK=/path/to/sdk bash scripts/build.sh" >&2
	exit 1
fi

ENV_SETUP="$(find "$SDK" -maxdepth 2 -name 'environment-setup*' -type f | head -n 1 || true)"

if [ -n "${ENV_SETUP:-}" ]; then
	echo "== environment: $ENV_SETUP"
	# shellcheck disable=SC1090
	. "$ENV_SETUP"
else
	echo "== WARNING: environment-setup not found, using PATH from $SDK" >&2
	export PATH="$SDK/usr/bin:$SDK/bin:$PATH"
fi

CC="${CC:-arm-funkey-linux-musleabihf-gcc}"
if ! command -v "${CC%% *}" >/dev/null 2>&1; then
	echo "ERROR: cross compiler '${CC%% *}' not found." >&2
	exit 1
fi

echo "== compiler: $($CC -dumpmachine) / gcc $($CC -dumpversion)"

# ---------------------------------------------------------- libopenmpt ---
if [ ! -f "$ROOT/vendor/lib/libopenmpt.a" ]; then
	echo "== building libopenmpt (first run, takes a couple of minutes)"
	bash "$ROOT/scripts/build-libopenmpt.sh"
fi

# -------------------------------------------------------------- build ---
make -C "$ROOT/$APP" -f Makefile.funkey clean >/dev/null
make -C "$ROOT/$APP" -f Makefile.funkey CC="$CC"

BIN="$ROOT/$APP/$APP"
file "$BIN" 2>/dev/null || true

# --------------------------------------------------------------- icon ---
if [ ! -f "$ROOT/$APP/opk/$APP.png" ]; then
	python3 "$ROOT/scripts/make_icon.py"
fi

# -------------------------------------------------------- stage + opk ---
STAGE="$ROOT/build/$APP"
rm -rf "$STAGE"
mkdir -p "$STAGE"

cp "$ROOT/$APP/opk/$APP.funkey-s.desktop" "$STAGE/"
cp "$ROOT/$APP/opk/$APP.png" "$STAGE/"
if [ -f "$ROOT/$APP/opk/readme.funkey-s.txt" ]; then
	cp "$ROOT/$APP/opk/readme.funkey-s.txt" "$STAGE/"
fi
for f in "$ROOT/$APP/opk/"LICENSE*; do
	[ -f "$f" ] && cp "$f" "$STAGE/"
done
cp "$BIN" "$STAGE/$APP"

if command -v arm-funkey-linux-musleabihf-strip >/dev/null 2>&1; then
	arm-funkey-linux-musleabihf-strip "$STAGE/$APP"
fi

# Bundle libmpg123: libopenmpt uses it for MP3-compressed MO3 samples.
# Keeping it next to the binary makes the OPK independent of the firmware
# package set.  The binary is linked with -rpath '$ORIGIN', so the loader
# looks in /opk first.  It is the unmodified shared library from the SDK.
SYSLIB="${FUNKEY_SYSROOT:-$SDK/arm-funkey-linux-musleabihf/sysroot}/usr/lib"
if [ -f "$SYSLIB/libmpg123.so.0" ]; then
	cp -L "$SYSLIB/libmpg123.so.0" "$STAGE/"
fi

# The launcher ignores a .desktop file that does not end with a newline.
LAST_BYTE="$(tail -c 1 "$STAGE/$APP.funkey-s.desktop" | od -An -tx1 | tr -d ' \n')"
if [ "$LAST_BYTE" != "0a" ]; then
	printf '\n' >>"$STAGE/$APP.funkey-s.desktop"
fi

mkdir -p "$ROOT/dist"
rm -f "$ROOT/dist/$APP.opk"

mksquashfs "$STAGE" "$ROOT/dist/$APP.opk" \
	-all-root -noappend -no-exports -no-xattrs >/dev/null

SIZE="$(stat -c%s "$ROOT/dist/$APP.opk")"

echo
echo "OK: $ROOT/dist/$APP.opk ($SIZE bytes)"
echo
echo "Copy the .opk to the console:"
echo "  USB -> MOUNT USB -> folder \"Native games\" (RetroFE)"
echo "  or a first-level folder that contains an \"apps\" subfolder (GMenu2X)"
