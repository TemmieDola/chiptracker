#!/usr/bin/env bash
# Fix the musl loader symlink inside the SDK sysroot so qemu-user can follow
# it (it points to the absolute guest path /lib/libc.so, which qemu cannot
# resolve on the host).
set -uo pipefail

SYS=/opt/FunKey-sdk-2.3.0/arm-funkey-linux-musleabihf/sysroot
L="$SYS/lib/ld-musl-armhf.so.1"

echo "== before"
ls -l "$L" 2>&1
ls -l "$SYS/lib/libc.so" 2>&1

if [ -f "$SYS/lib/libc.so" ] && [ -L "$L" ]; then
	rm -f "$L"
	cp "$SYS/lib/libc.so" "$L"
	echo "== symlink replaced with a real copy"
fi

echo "== after"
ls -l "$L" 2>&1
file "$L" 2>&1
