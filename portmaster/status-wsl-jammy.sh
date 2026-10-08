#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

printf 'Ubuntu release:\n'
cat /etc/os-release
printf '\nForeign architectures:\n'
dpkg --print-foreign-architectures || true
printf '\nAArch64 package candidates:\n'
apt-cache policy \
	libsdl2-dev:arm64 \
	libcurl4-openssl-dev:arm64 \
	zlib1g-dev:arm64 \
	libpng-dev:arm64 || true
printf '\nCUzeBox PortMaster doctor:\n\n'
"$SCRIPT_DIR/doctor-rg40xxh.sh"
