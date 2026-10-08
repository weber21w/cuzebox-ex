#!/bin/sh
set -eu

PORTMASTER_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_DIR="$(CDPATH= cd -- "$PORTMASTER_DIR/.." && pwd)"

: "${PKG_CONFIG:=pkg-config}"
: "${PKG_CONFIG_LIBDIR:=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig}"
: "${PKG_CONFIG_SYSROOT_DIR:=/}"
: "${FLAG_REMOTE_ROMS_LIBCURL:=0}"
: "${FLAG_GUI_VKEYBOARD_DEFAULT:=1}"

export PKG_CONFIG
export PKG_CONFIG_LIBDIR
export PKG_CONFIG_SYSROOT_DIR
export FLAG_REMOTE_ROMS_LIBCURL
export FLAG_GUI_VKEYBOARD_DEFAULT

run_make()
{
	cd "$REPO_DIR"
	make "$@"
}
