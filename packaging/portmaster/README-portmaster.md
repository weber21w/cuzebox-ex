# CUzeBox PortMaster staging

This directory is produced by `make TARGET_PORTMASTER=1 package-portmaster`.

What it does:
- builds a Linux AArch64 CUzeBox binary
- stages it into a PortMaster-style folder
- writes a zip containing the launch script, `port.json`, and app directory

Typical build examples:

```sh
make TARGET_LINUX_AARCH64=1 \
	PKG_CONFIG=aarch64-linux-gnu-pkg-config

make TARGET_PORTMASTER=1 \
	PKG_CONFIG=aarch64-linux-gnu-pkg-config \
	package-portmaster
```

If your toolchain does not provide `aarch64-linux-gnu-pkg-config`, keep
`PKG_CONFIG=pkg-config` and point it at the target sysroot with
`PKG_CONFIG_LIBDIR` / `PKG_CONFIG_SYSROOT_DIR`.

The generated zip does not bundle PortMaster runtime libraries. Stage any
additional `.so` files separately if your target runtime needs them.


Convenience handheld preset:

```sh
make show-portmaster-rg40xxh-config
make portmaster-rg40xxh-stage
make package-portmaster-rg40xxh
```

`package-portmaster-rg40xxh` is a thin wrapper over `TARGET_PORTMASTER=1`
with `FLAG_GUI_VKEYBOARD_DEFAULT=1`, so it keeps using the same Linux
AArch64 / PortMaster build path rather than introducing a separate one.


## AArch64 / PortMaster build notes

The PortMaster / RG40XX H preset is intentionally strict.

- It requires an AArch64 compiler in `PATH`.
- It requires `pkg-config` to resolve **target** SDL2 flags.
- It refuses to use host x86_64 SDL2 or libcurl flags in an AArch64 build.
- If you want Remote ROMs without libcurl on the target, build with `FLAG_REMOTE_ROMS_LIBCURL=0`.

Typical WSL example with a target sysroot:

```sh
make portmaster-rg40xxh 	PKG_CONFIG=pkg-config 	PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig 	PKG_CONFIG_SYSROOT_DIR=/
```


## Helpful build checks

Before a first RG40XX H / PortMaster build, run:

```sh
make portmaster-rg40xxh-doctor
```

This prints whether the AArch64 compiler, target pkg-config, SDL2, and libcurl
look sane for a cross-build, and suggests the usual WSL packages to install if
anything is missing.


## Helper scripts

There is also a separate top-level `portmaster/` directory with helper scripts for the
common Ubuntu 22.04 WSL -> RG40XX H workflow:

- `./portmaster/install-wsl-jammy-prereqs.sh`
- `./portmaster/status-wsl-jammy.sh`
- `./portmaster/doctor-rg40xxh.sh`
- `./portmaster/build-rg40xxh.sh`
- `./portmaster/package-rg40xxh.sh`

These do not replace the makefiles. They are just wrappers around the existing
PortMaster targets with sane cross-build defaults.
