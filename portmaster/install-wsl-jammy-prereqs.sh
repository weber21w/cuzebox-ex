#!/bin/sh
set -eu

if [ "$(id -u)" -eq 0 ]; then
	echo "Run this as your normal user, not root. It will use sudo where needed." >&2
	exit 1
fi

if [ ! -f /etc/os-release ]; then
	echo "This script expects Ubuntu WSL with /etc/os-release present." >&2
	exit 1
fi

. /etc/os-release

if [ "${ID:-}" != "ubuntu" ] || [ "${VERSION_ID:-}" != "22.04" ]; then
	echo "This script is written for Ubuntu 22.04 WSL. Detected: ${PRETTY_NAME:-unknown}" >&2
	echo "Refusing to rewrite apt sources automatically on this system." >&2
	exit 1
fi

echo "Adding arm64 foreign architecture if needed..."
sudo dpkg --add-architecture arm64 || true

echo "Backing up /etc/apt/sources.list..."
sudo cp /etc/apt/sources.list /etc/apt/sources.list.cuzebox-portmaster.bak

echo "Writing jammy amd64 + arm64 ports sources..."
sudo tee /etc/apt/sources.list >/dev/null <<'SRC'
# amd64 host packages

deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ jammy main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ jammy-updates main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ jammy-backports main restricted universe multiverse
deb [arch=amd64] http://security.ubuntu.com/ubuntu/ jammy-security main restricted universe multiverse

# arm64 cross packages

deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-updates main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-backports main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-security main restricted universe multiverse
SRC

echo "Updating apt indexes..."
sudo apt update

echo "Installing AArch64 cross-build prerequisites..."
sudo apt install -y \
	gcc-aarch64-linux-gnu \
	g++-aarch64-linux-gnu \
	binutils-aarch64-linux-gnu \
	pkg-config \
	libsdl2-dev:arm64 \
	zlib1g-dev:arm64 \
	libpng-dev:arm64

echo
 echo "Done."
 echo "libcurl4-openssl-dev:arm64 is intentionally not installed here,"
 echo "because it conflicts with host curl-config on typical WSL setups."
 echo "Use FLAG_REMOTE_ROMS_LIBCURL=0 for PortMaster builds unless you"
 echo "switch to a dedicated ARM64 sysroot or chroot."
