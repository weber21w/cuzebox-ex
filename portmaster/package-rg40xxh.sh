#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

run_make portmaster-rg40xxh \
	FLAG_REMOTE_ROMS_LIBCURL="$FLAG_REMOTE_ROMS_LIBCURL" \
	PKG_CONFIG="$PKG_CONFIG" \
	PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR" \
	PKG_CONFIG_SYSROOT_DIR="$PKG_CONFIG_SYSROOT_DIR"
