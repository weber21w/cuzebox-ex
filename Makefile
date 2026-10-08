############
# Makefile #
############
#
#  Copyright (C) 2016
#    Sandor Zsuga (Jubatian)
#  Uzem (the base of CUzeBox) is copyright (C)
#    David Etherton,
#    Eric Anderton,
#    Alec Bourque (Uze),
#    Filipe Rinaldi,
#    Sandor Zsuga (Jubatian),
#    Matt Pandina (Artcfox)
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#
#
# The main makefile of the program
#
#
# make all (or make): build the program
# make clean:         to clean up
#
#

include Make_defines.mk

# Keep the normal program build as the default goal even when generated-file
# rules (such as web_assets.h) are declared before the all target below.
.DEFAULT_GOAL := all

CFLAGS  += -flto=auto

OBJECTS  = $(OBD)/main.o
OBJECTS += $(OBD)/cu_ufile.o
OBJECTS += $(OBD)/cu_hfile.o
OBJECTS += $(OBD)/cu_avr.o
OBJECTS += $(OBD)/cu_avrc.o
OBJECTS += $(OBD)/cu_avrfg.o
OBJECTS += $(OBD)/cu_ctr.o
OBJECTS += $(OBD)/cu_kbd.o
OBJECTS += $(OBD)/cu_mouse.o
OBJECTS += $(OBD)/cu_gun.o
OBJECTS += $(OBD)/cu_multitap.o
OBJECTS += $(OBD)/cu_vdev.o
OBJECTS += $(OBD)/cu_spi.o
OBJECTS += $(OBD)/cu_spir.o
OBJECTS += $(OBD)/cu_esp_data.o
OBJECTS += $(OBD)/cu_uart.o
OBJECTS += $(OBD)/cu_esp_at.o
OBJECTS += $(OBD)/cu_esp_ap.o
OBJECTS += $(OBD)/cu_esp_net.o
OBJECTS += $(OBD)/midi.o
OBJECTS += $(OBD)/cu_haptic.o
ifeq ($(FLAG_SELFCONT), 0)
OBJECTS += $(OBD)/cu_vfat.o
OBJECTS += $(OBD)/cu_spisd.o
OBJECTS += $(OBD)/filesys.o
endif
OBJECTS += $(OBD)/eepdump.o
OBJECTS += $(OBD)/romdump.o
OBJECTS += $(OBD)/configcfg.o
ifneq ($(ENABLE_API_SERVER),0)
OBJECTS += $(OBD)/api_server.o
OBJECTS += $(OBD)/web_server.o
endif
ifneq ($(ENABLE_DEBUGGER),0)
OBJECTS += $(OBD)/debug_source.o
OBJECTS += $(OBD)/debug_dwarf.o
OBJECTS += $(OBD)/debug_timing.o
OBJECTS += $(OBD)/debug_sd_fs_history.o
OBJECTS += $(OBD)/debug_sd_timing_analysis.o
endif
OBJECTS += $(OBD)/cheats.o
OBJECTS += $(OBD)/remote_roms.o
ifneq ($(ENABLE_MICROUI),0)
OBJECTS += $(OBD)/microui.o
OBJECTS += $(OBD)/mui_filedialog.o
OBJECTS += $(OBD)/mui_integration.o
endif
OBJECTS += $(OBD)/guicore.o
ifneq ($(ENABLE_DISPLAY_FILTERS),0)
OBJECTS += $(OBD)/filters.o
endif
OBJECTS += $(OBD)/audio.o
OBJECTS += $(OBD)/ginput.o
OBJECTS += $(OBD)/frame.o
OBJECTS += $(OBD)/chars.o
OBJECTS += $(OBD)/textgui.o
OBJECTS += $(OBD)/conout.o
ifneq ($(ENABLE_VCAP), 0)
OBJECTS += $(OBD)/avconv.o
endif
ifneq ($(ENABLE_ICAP), 0)
OBJECTS += $(OBD)/capture.o
endif
OBJECTS += $(OBD)/savestate.o
OBJECTS += $(OBD)/rollback.o
OBJECTS += $(OBD)/netplay.o
ifeq ($(TSYS),windows_mingw)
OBJECTS += $(OBD)/winicon.o
endif
ifneq ($(FLAG_SELFCONT), 0)
OBJECTS += $(OBD)/gamefile.o
OBJECTS += $(OBD)/filesmin.o
endif
ifeq ($(TSYS),emscripten)
ifeq ($(FLAG_SELFCONT), 0)
ifeq ($(FLAG_NOGAMEFILE), 0)
ROMFILE  = gamefile.uze
else
ROMFILE  =
endif
else
ROMFILE  =
endif
else
ROMFILE  =
endif


DEPS     = $(wildcard *.h microui/*.h) Makefile Make_defines.mk Make_config.mk
# These generated assets are checked into release/source archives.  Ordinary
# builds consume them directly; their explicit maintenance targets below are
# used only when intentionally regenerating them from source data.
DEPS     += bootloader_builtin.h web_assets.h

PYTHON ?= python3

WEB_ASSET_SOURCES := tools/web_debugger/index.html tools/web_debugger/controls.html tools/web_debugger/debugger.html tools/web_debugger/sd.html tools/web_debugger/audio.html tools/web_debugger/serial.html tools/web_debugger/network.html tools/web_debugger/common.css tools/web_debugger/common.js tools/web_assets_to_c.py
# web_assets.h is checked in just like bootloader_builtin.h.  Do not attach its
# generator to the normal dependency graph: some long-standing Windows CUzeBox
# build environments do not provide a usable `python3` command, and extracting
# an archive over an existing tree can also make the source timestamps newer
# than the generated header.  Regenerate it only when intentionally changing
# the embedded web UI.
.PHONY: regen-web-assets
regen-web-assets:
	$(PYTHON) tools/web_assets_to_c.py

BOOTLOADER_GENERATOR      := tools/bootloader_hex_to_c.py
BOOTLOADER_BUILTIN_HEADER := bootloader_builtin.h
# Normal trees use Bootloader.hex.  The alternate spellings make the automatic
# rule friendly to case-sensitive and case-insensitive source layouts.
BOOTLOADER_HEX ?= $(firstword $(wildcard Bootloader.hex bootloader.hex BOOTLOADER.hex BOOTLOADER.HEX))

WINDRES ?= windres
PREFIX  ?= /usr/local
INSTALL ?= install
DESKTOPDIR ?= $(PREFIX)/share/applications
ICONDIR ?= $(PREFIX)/share/icons/hicolor/32x32/apps

PORTMASTER_STAGE_ROOT ?= _portmaster
PORTMASTER_STAGE_DIR  ?= $(PORTMASTER_STAGE_ROOT)/$(PORTMASTER_NAME)
PORTMASTER_OUT_ZIP    ?= $(PORTMASTER_DIR)-portmaster.zip
PORTMASTER_SCRIPT_SRC ?= packaging/portmaster/$(PORTMASTER_NAME).sh
PORTMASTER_JSON_SRC   ?= packaging/portmaster/port.json
PORTMASTER_README_SRC ?= packaging/portmaster/README-portmaster.md

all: $(OUT)
clean:
	rm    -f $(OUT)
	rm    -f $(PORTMASTER_OUT_ZIP)
	rm -rf $(PORTMASTER_STAGE_ROOT)
	rm -rf $(OBD)
	rm    -f *.o microui/*.o
	rm    -f gamefile.c
	rm    -f assets/$(CHCONV)
	rm    -f assets/$(BINCONV)

$(ROMFILE):
	$(error You need to supply a gamefile.uze for Emscripten build)

$(OUT): $(OBD) $(OBJECTS) $(ROMFILE)
	$(CC) -o $(OUT) $(OBJECTS) $(CFSPD) $(LINK)

install-linux: $(OUT)
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin"
	$(INSTALL) -d "$(DESTDIR)$(DESKTOPDIR)"
	$(INSTALL) -d "$(DESTDIR)$(ICONDIR)"
	$(INSTALL) -m 755 $(OUT) "$(DESTDIR)$(PREFIX)/bin/cuzebox"
	$(INSTALL) -m 644 packaging/cuzebox.desktop "$(DESTDIR)$(DESKTOPDIR)/cuzebox.desktop"
	$(INSTALL) -m 644 icon.png "$(DESTDIR)$(ICONDIR)/cuzebox.png"


portmaster-stage: $(OUT)
	rm -rf "$(PORTMASTER_STAGE_DIR)"
	$(INSTALL) -d "$(PORTMASTER_STAGE_DIR)/$(PORTMASTER_NAME)"
	$(INSTALL) -m 755 $(OUT) "$(PORTMASTER_STAGE_DIR)/$(PORTMASTER_NAME)/cuzebox"
	$(INSTALL) -m 644 packaging/config.default.cfg "$(PORTMASTER_STAGE_DIR)/$(PORTMASTER_NAME)/config.cfg"
	$(INSTALL) -m 644 readme.md "$(PORTMASTER_STAGE_DIR)/$(PORTMASTER_NAME)/readme.md"
	@if [ -f icon.png ]; then $(INSTALL) -m 644 icon.png "$(PORTMASTER_STAGE_DIR)/$(PORTMASTER_NAME)/icon.png"; fi
	$(INSTALL) -m 755 "$(PORTMASTER_SCRIPT_SRC)" "$(PORTMASTER_STAGE_DIR)/$(PORTMASTER_NAME).sh"
	$(INSTALL) -m 644 "$(PORTMASTER_JSON_SRC)" "$(PORTMASTER_STAGE_DIR)/port.json"
	$(INSTALL) -m 644 "$(PORTMASTER_README_SRC)" "$(PORTMASTER_STAGE_DIR)/README-portmaster.md"

package-portmaster: portmaster-stage
	rm -f "$(PORTMASTER_OUT_ZIP)"
	cd "$(PORTMASTER_STAGE_ROOT)" && zip -qr "../$(PORTMASTER_OUT_ZIP)" "$(PORTMASTER_NAME)"

portmaster-clean:
	rm -rf "$(PORTMASTER_STAGE_ROOT)" "$(PORTMASTER_OUT_ZIP)"

show-portmaster-config:
	@echo TSYS=$(TSYS)
	@echo TARGET_LINUX_AARCH64=$(TARGET_LINUX_AARCH64)
	@echo TARGET_PORTMASTER=$(TARGET_PORTMASTER)
	@echo CCOMP=$(CCOMP)
	@echo PKG_CONFIG=$(PKG_CONFIG)
	@echo PKG_CONFIG_LIBDIR=$${PKG_CONFIG_LIBDIR}
	@echo PKG_CONFIG_SYSROOT_DIR=$${PKG_CONFIG_SYSROOT_DIR}
	@echo SDL2_PKG_CFLAGS=$(SDL2_PKG_CFLAGS)
	@echo SDL2_PKG_LIBS=$(SDL2_PKG_LIBS)
	@echo CURL_PKG_CFLAGS=$(CURL_PKG_CFLAGS)
	@echo CURL_PKG_LIBS=$(CURL_PKG_LIBS)
	@echo OUT=$(OUT)
	@echo PORTMASTER_STAGE_DIR=$(PORTMASTER_STAGE_DIR)

portmaster-doctor:
	@$(MAKE) --no-print-directory 		TARGET_PORTMASTER=1 		PORTMASTER_DIAG=1 		_portmaster-doctor-inner

_portmaster-doctor-inner:
	@$(MAKE) --no-print-directory 		TARGET_PORTMASTER=1 		PORTMASTER_DIAG=1 		show-portmaster-config
	@echo
	@echo "PortMaster / AArch64 build doctor"
	@echo "================================"
	@if [ "$(AARCH64_CC_OK)" = "yes" ]; then 		echo "[OK]     compiler found: $(CCOMP)"; 	else 		echo "[MISSING] compiler not found: $(CCOMP)"; 		echo "          install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu"; 	fi
	@if [ "$(PKG_CONFIG_OK)" = "yes" ]; then 		echo "[OK]     pkg-config found: $(PKG_CONFIG)"; 	else 		echo "[MISSING] pkg-config not found: $(PKG_CONFIG)"; 		echo "          install pkg-config or point PKG_CONFIG at a target-aware binary"; 	fi
	@if [ -n "$${PKG_CONFIG_LIBDIR}" ]; then 		echo "[INFO]   PKG_CONFIG_LIBDIR=$${PKG_CONFIG_LIBDIR}"; 	else 		echo "[WARN]   PKG_CONFIG_LIBDIR is empty"; 		echo "         for cross builds, set it to your AArch64 sysroot pkgconfig paths"; 	fi
	@if [ -n "$${PKG_CONFIG_SYSROOT_DIR}" ]; then 		echo "[INFO]   PKG_CONFIG_SYSROOT_DIR=$${PKG_CONFIG_SYSROOT_DIR}"; 	else 		echo "[WARN]   PKG_CONFIG_SYSROOT_DIR is empty"; 	fi
	@if [ -z "$${PKG_CONFIG_LIBDIR}" ]; then 		echo "[WARN]   without PKG_CONFIG_LIBDIR, any pkg-config hit may still be host-side"; 	fi
	@if [ "$(FLAG_USE_SDL1)" = "0" ]; then 		if [ "$(SDL2_PKG_OK)" = "yes" ]; then 			echo "[OK]     SDL2 pkg-config entry found"; 			echo "         cflags: $(SDL2_PKG_CFLAGS)"; 			echo "         libs:   $(SDL2_PKG_LIBS)"; 		else 			echo "[MISSING] SDL2 pkg-config entry not found for target"; 			echo "          install libsdl2-dev:arm64 or point PKG_CONFIG_LIBDIR / PKG_CONFIG_SYSROOT_DIR at your AArch64 sysroot"; 		fi; 		if [ "$(SDL2_PKG_BADARCH)" = "yes" ]; then 			echo "[BAD]    SDL2 flags look like host/non-AArch64 output"; 			echo "         $(SDL2_PKG_CFLAGS) $(SDL2_PKG_LIBS)"; 		fi; 	fi
	@if [ "$(FLAG_REMOTE_ROMS)" = "0" ]; then 		echo "[INFO]   Remote ROMs disabled, libcurl not required"; 	elif [ "$(FLAG_REMOTE_ROMS_LIBCURL)" = "0" ]; then 		echo "[INFO]   Remote ROMs use shell curl fallback, libcurl not required"; 	else 		if [ "$(CURL_PKG_OK)" = "yes" ]; then 			echo "[OK]     libcurl pkg-config entry found"; 			echo "         cflags: $(CURL_PKG_CFLAGS)"; 			echo "         libs:   $(CURL_PKG_LIBS)"; 		else 			echo "[MISSING] libcurl pkg-config entry not found for target"; 			echo "          install libcurl4-openssl-dev:arm64 or build with FLAG_REMOTE_ROMS_LIBCURL=0"; 		fi; 		if [ "$(CURL_PKG_BADARCH)" = "yes" ]; then 			echo "[BAD]    libcurl flags look like host/non-AArch64 output"; 			echo "         $(CURL_PKG_CFLAGS) $(CURL_PKG_LIBS)"; 		fi; 	fi
	@echo
	@echo "Suggested WSL packages:"
	@echo "  sudo apt update"
	@echo "  sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu pkg-config"
	@echo "  sudo dpkg --add-architecture arm64 && sudo apt update"
	@echo "  sudo apt install libsdl2-dev:arm64 libcurl4-openssl-dev:arm64 zlib1g-dev:arm64 libpng-dev:arm64"

portmaster-rg40xxh-doctor:
	@$(MAKE) --no-print-directory 		TARGET_PORTMASTER=1 		FLAG_GUI_VKEYBOARD_DEFAULT=1 		PORTMASTER_DIAG=1 		_portmaster-doctor-inner

show-portmaster-rg40xxh-config:
	@$(MAKE) --no-print-directory 		TARGET_PORTMASTER=1 		FLAG_GUI_VKEYBOARD_DEFAULT=1 		show-portmaster-config

portmaster-rg40xxh-stage:
	$(MAKE) 		TARGET_PORTMASTER=1 		FLAG_GUI_VKEYBOARD_DEFAULT=1 		portmaster-stage

package-portmaster-rg40xxh:
	$(MAKE) 		TARGET_PORTMASTER=1 		FLAG_GUI_VKEYBOARD_DEFAULT=1 		package-portmaster

portmaster-rg40xxh: package-portmaster-rg40xxh

portmaster-wsl-install-prereqs:
	./portmaster/install-wsl-jammy-prereqs.sh

portmaster-wsl-status:
	./portmaster/status-wsl-jammy.sh

$(OBD)/winicon.o: icon.rc icon.ico
	$(WINDRES) -i icon.rc -o $@

$(OBD):
	mkdir $(OBD)

# bootloader_builtin.h is deliberately checked in.  Do not regenerate it as an
# implicit prerequisite of a normal build: CUzeBox's long-standing Windows
# build environment does not necessarily provide a usable `python3` command.
# Maintainers can explicitly refresh/verify it with the targets below.
.PHONY: regen-bootloader check-bootloader
ifneq ($(strip $(BOOTLOADER_HEX)),)
regen-bootloader:
	$(PYTHON) $(BOOTLOADER_GENERATOR) --input "$(BOOTLOADER_HEX)" --output "$(BOOTLOADER_BUILTIN_HEADER)"

check-bootloader:
	$(PYTHON) $(BOOTLOADER_GENERATOR) --input "$(BOOTLOADER_HEX)" --output "$(BOOTLOADER_BUILTIN_HEADER)" --check
else
regen-bootloader:
	$(PYTHON) $(BOOTLOADER_GENERATOR) --search-root . --output "$(BOOTLOADER_BUILTIN_HEADER)"

check-bootloader:
	$(PYTHON) $(BOOTLOADER_GENERATOR) --search-root . --output "$(BOOTLOADER_BUILTIN_HEADER)" --check
endif

# Special: Generate character set from the charset data. On Windows, whatever
# may happen, so especially chars.c is never explicitly removed, and is
# included in a Git repository.
#
#chars.c: assets/$(CHCONV)
#	assets/$(CHCONV) >chars.c

assets/$(CHCONV): assets/chconv.c assets/charset.h
	$(CCNAT) $< -o $@ -Wall

# Special: Generate C source from a gamefile.uze for a self-contained build.

gamefile.c: assets/$(BINCONV) gamefile.uze
	assets/$(BINCONV) <gamefile.uze >gamefile.c

assets/$(BINCONV): assets/binconv.c
	$(CCNAT) $< -o $@ -Wall

# Objects

$(OBD)/main.o: main.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_ufile.o: cu_ufile.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_hfile.o: cu_hfile.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_avr.o: cu_avr.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_avrc.o: cu_avrc.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_avrfg.o: cu_avrfg.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_ctr.o: cu_ctr.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_kbd.o: cu_kbd.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_mouse.o: cu_mouse.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_gun.o: cu_gun.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_multitap.o: cu_multitap.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_vdev.o: cu_vdev.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/cu_spi.o: cu_spi.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_spisd.o: cu_spisd.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_spir.o: cu_spir.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_esp_data.o: cu_esp_data.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_uart.o: cu_uart.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_esp_at.o: cu_esp_at.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_esp_ap.o: cu_esp_ap.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_esp_net.o: cu_esp_net.c cu_link_net.inc $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/midi.o: midi.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_haptic.o: cu_haptic.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cu_vfat.o: cu_vfat.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/filesys.o: filesys.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/filesmin.o: filesmin.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/microui.o: microui/microui.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/mui_filedialog.o: microui/mui_filedialog.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/mui_integration.o: microui/mui_integration.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/remote_roms.o: remote_roms.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/guicore.o: guicore.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

ifneq ($(ENABLE_DISPLAY_FILTERS),0)
$(OBD)/filters.o: filters.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)
endif

$(OBD)/audio.o: audio.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/ginput.o: ginput.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/frame.o: frame.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/eepdump.o: eepdump.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/romdump.o: romdump.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/configcfg.o: configcfg.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/api_server.o: api_server.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/web_server.o: web_server.c web_server.h web_assets.h $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/debug_source.o: debug_source.c debug_source.h debug_dwarf.h Makefile Make_defines.mk Make_config.mk
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/debug_dwarf.o: debug_dwarf.c debug_dwarf.h Makefile Make_defines.mk Make_config.mk
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/debug_timing.o: debug_timing.c debug_timing.h Makefile Make_defines.mk Make_config.mk
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/debug_sd_fs_history.o: debug_sd_fs_history.c debug_sd_fs_history.h cu_spisd.h cu_vfat.h Makefile Make_defines.mk Make_config.mk
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/debug_sd_timing_analysis.o: debug_sd_timing_analysis.c debug_sd_timing_analysis.h cu_spisd.h Makefile Make_defines.mk Make_config.mk
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/cheats.o: cheats.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/avconv.o: avconv.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/capture.o: capture.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/savestate.o: savestate.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/rollback.o: rollback.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/netplay.o: netplay.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/textgui.o: textgui.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSPD)

$(OBD)/conout.o: conout.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/chars.o: chars.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)

$(OBD)/gamefile.o: gamefile.c $(DEPS)
	$(CC) -c $< -o $@ $(CFSIZ)


.PHONY: all clean

# Browser debugger frontend.  The HTTP bridge is intentionally an API client;
# it does not link against or inspect emulator state directly.
PYTHON ?= python3
.PHONY: web-tools web-controls web-debugger web-sd web-audio web-serial web-network web-debugger-no-browser
web-tools:
	$(PYTHON) tools/cuzebox_web_debugger.py --page home

web-controls:
	$(PYTHON) tools/cuzebox_web_debugger.py --page controls

web-debugger:
	$(PYTHON) tools/cuzebox_web_debugger.py --page debugger

web-sd:
	$(PYTHON) tools/cuzebox_web_debugger.py --page sd

web-audio:
	$(PYTHON) tools/cuzebox_web_debugger.py --page audio

web-serial:
	$(PYTHON) tools/cuzebox_web_debugger.py --page serial

web-network:
	$(PYTHON) tools/cuzebox_web_debugger.py --page network

web-debugger-no-browser:
	$(PYTHON) tools/cuzebox_web_debugger.py --page debugger --no-browser

.PHONY: web-debugger-check debug-source-check debug-dwarf-check api-multiclient-check embedded-web-check sd-timing-check sd-timing-analysis-check sd-stress-check sd-trace-check audio-scope-check audio-debug-check audio-analysis-check debug-timing-check uart-logic-check
web-debugger-check:
	$(PYTHON) -m py_compile tools/cuzebox_api.py tools/api_test_runner.py tools/cuzebox_web_debugger.py tools/test_web_debugger_bridge.py tools/test_debug_source.py tools/test_debug_dwarf.py tools/test_api_multiclient.py tools/test_uart_logic_capture.py tools/test_sd_trace.py tools/sd_stress_runner.py tools/test_sd_stress_runner.py tools/test_sd_timing_analysis.py
	$(PYTHON) tools/test_web_debugger_bridge.py

# Exercises the host-side ELF/DWARF2-4 line-table reader without SDL.
debug-source-check:
	$(PYTHON) tools/test_debug_source.py

# Exercises AVR-GCC-style DWARF variables, types, structs, registers and frame-relative locals.
debug-dwarf-check:
	$(PYTHON) tools/test_debug_dwarf.py

# Compiles the real API server against a small host harness and verifies that
# independent clients, waits, and reset cancellation can coexist.
api-multiclient-check:
	$(PYTHON) tools/test_api_multiclient.py

sd-timing-check:
	$(PYTHON) tools/test_sd_timing.py

sd-timing-analysis-check:
	$(PYTHON) tools/test_sd_timing_analysis.py

sd-stress-check:
	$(PYTHON) tools/test_sd_stress_runner.py

sd-trace-check:
	$(PYTHON) tools/test_sd_trace.py

embedded-web-check: web_assets.h
	$(PYTHON) tools/test_embedded_web_server.py

# Verifies that oscilloscope output capture swaps callbacks only when armed.
audio-scope-check:
	$(PYTHON) tools/test_audio_scope.py

# Verifies cycle-correlated OCR2A history/breaks compile out completely when disabled.
audio-debug-check:
	$(PYTHON) tools/test_audio_debug.py

# Verifies browser-side FFT/pitch/DC/discontinuity and DAC cadence analysis.
audio-analysis-check:
	$(PYTHON) tools/test_audio_analysis.py

# Verifies raster breakpoint crossing, opt-in event tracing and per-line timing stats.
debug-timing-check:
	$(PYTHON) tools/test_debug_timing.py

# Verifies release builds preprocess out the memory diagnostic gate/history and
# both AVR interpreter back ends route SRAM through the common access macros.
debug-memory-gate-check:
	$(PYTHON) tools/test_debug_memory_gate.py

# Verifies byte-metadata UART reconstruction and that the disabled path retains
# the pre-existing single serial-trace gate with no per-bit instrumentation.
uart-logic-check:
	$(PYTHON) tools/test_uart_logic_capture.py
