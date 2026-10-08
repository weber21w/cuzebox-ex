#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
APP_DIR="$SCRIPT_DIR/CUzeBox"
BIN="$APP_DIR/cuzebox"
ROM_PATH="${1:-}"

if [ ! -x "$BIN" ]; then
	echo "CUzeBox binary not found: $BIN" >&2
	exit 1
fi

export SDL_GAMECONTROLLERCONFIG_FILE="${SDL_GAMECONTROLLERCONFIG_FILE:-$APP_DIR/gamecontrollerdb.txt}"
export HOME="${HOME:-$APP_DIR}"
cd "$APP_DIR"

if [ -n "$ROM_PATH" ]; then
	exec "$BIN" "$ROM_PATH"
else
	exec "$BIN"
fi
