# PortMaster helper scripts

This directory is separate from the normal makefiles on purpose.

The makefiles still own the actual build logic. These scripts are just thin
helpers for the common Ubuntu 22.04 WSL -> PortMaster / RG40XX H workflow.

## Scripts

- `install-wsl-jammy-prereqs.sh`
	- sets up Ubuntu 22.04 WSL for CUzeBox PortMaster cross-builds
	- adds `arm64`
	- rewrites `/etc/apt/sources.list` to use normal amd64 Ubuntu archives plus
	  `ports.ubuntu.com` for `arm64`
	- installs the common AArch64 compiler and SDL2 / zlib / libpng target dev packages
	- intentionally leaves target libcurl out, because it usually conflicts with
	  host `curl-config` on WSL

- `status-wsl-jammy.sh`
	- shows Ubuntu release info
	- shows foreign architectures
	- shows `apt-cache policy` for the key `arm64` packages
	- runs the CUzeBox PortMaster doctor

- `doctor-rg40xxh.sh`
	- runs `make portmaster-rg40xxh-doctor` with the expected cross pkg-config environment

- `build-rg40xxh.sh`
	- builds the Linux AArch64 binary with handheld defaults

- `package-rg40xxh.sh`
	- builds and packages the PortMaster zip

## Defaults

These scripts default to:

- `PKG_CONFIG=pkg-config`
- `PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig`
- `PKG_CONFIG_SYSROOT_DIR=/`
- `FLAG_REMOTE_ROMS_LIBCURL=0`
- `FLAG_GUI_VKEYBOARD_DEFAULT=1`

You can override any of those per invocation. Example:

```sh
FLAG_REMOTE_ROMS_LIBCURL=1 ./portmaster/doctor-rg40xxh.sh
```

## Typical flow

```sh
./portmaster/install-wsl-jammy-prereqs.sh
./portmaster/status-wsl-jammy.sh
./portmaster/package-rg40xxh.sh
```
