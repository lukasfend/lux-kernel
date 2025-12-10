#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
"$SCRIPT_DIR/build.sh"

SCALE="${QEMU_SCALE:-2}"
DISPLAY_BACKEND="${QEMU_DISPLAY_BACKEND:-gtk}"

if [ "$DISPLAY_BACKEND" = "gtk" ]; then
	export GDK_SCALE="$SCALE"
	QEMU_DISPLAY_OPTS="gtk,zoom-to-fit=on"
else
	QEMU_DISPLAY_OPTS="$DISPLAY_BACKEND"
fi

qemu-system-x86_64 \
	-drive format=raw,file="$SCRIPT_DIR/bin/os.bin" \
	-display "$QEMU_DISPLAY_OPTS" \
	"$@"