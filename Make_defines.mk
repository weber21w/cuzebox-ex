######################
# Make - definitions #
######################
#
#  Copyright (C) 2016 - 2017
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
# This file holds general definitions used by any makefile, like compiler
# flags, optimization and such. OS - specific building schemes should also
# be written here.
#
#
include Make_config.mk

ifeq ($(TARGET_PORTMASTER),1)
TARGET_LINUX_AARCH64:=1
TSYS:=linux
ifeq ($(origin FLAG_GUI_VKEYBOARD_DEFAULT),file)
FLAG_GUI_VKEYBOARD_DEFAULT:=1
endif
ifeq ($(origin FLAG_REMOTE_ROMS_LIBCURL),file)
FLAG_REMOTE_ROMS_LIBCURL:=0
endif
endif

ifeq ($(TARGET_LINUX_AARCH64),1)
TSYS:=linux
endif

# Accept "osx" as an alias for the native macOS target.
ifeq ($(TSYS),osx)
TSYS:=macos
endif

CFLAGS=-std=gnu17
#
#
# The compiler
#
ifeq ($(TSYS),linux)
ifeq ($(TARGET_LINUX_AARCH64),1)
CCNAT?=gcc
CCOMP?=$(CROSS_COMPILE)gcc
STRIP?=$(CROSS_COMPILE)strip
else
CCNAT?=gcc
CCOMP?=$(CCNAT)
STRIP?=strip
endif
endif
ifeq ($(TSYS),windows_mingw)
CCNAT?=gcc
CCOMP?=$(CCNAT)
endif
ifeq ($(TSYS),macos)
CCNAT?=clang
CCOMP?=$(CCNAT)
endif
ifeq ($(TSYS),emscripten)
CCNAT?=gcc
CCOMP?=emcc
endif
#

#
# SDL2 auto-detection helpers
#
ifeq ($(strip $(SDL2_CONFIG)),)
ifeq ($(TARGET_LINUX_AARCH64),1)
SDL2_CONFIG_BIN:=
else
SDL2_CONFIG_BIN:=$(strip $(shell command -v sdl2-config 2>/dev/null))
endif
else
SDL2_CONFIG_BIN:=$(SDL2_CONFIG)
endif
SDL2_PKG_OK:=$(strip $(shell $(PKG_CONFIG) --exists sdl2 2>/dev/null && echo yes))
BREW_BIN:=$(strip $(shell command -v brew 2>/dev/null))
ifeq ($(BREW_BIN),)
SDL2_BREW_PREFIX:=
else
SDL2_BREW_PREFIX:=$(strip $(shell brew --prefix sdl2 2>/dev/null))
endif

#
# libcurl auto-detection for Remote ROMs
#
ifeq ($(strip $(CURL_CONFIG)),)
ifeq ($(TARGET_LINUX_AARCH64),1)
CURL_CONFIG_BIN:=
else
CURL_CONFIG_BIN:=$(strip $(shell command -v curl-config 2>/dev/null))
endif
else
CURL_CONFIG_BIN:=$(CURL_CONFIG)
endif
CURL_PKG_OK:=$(strip $(shell $(PKG_CONFIG) --exists libcurl 2>/dev/null && echo yes))
ifeq ($(CC_INC),)
CURL_HEADER_LOCAL:=
else
CURL_HEADER_LOCAL:=$(strip $(wildcard $(CC_INC)/curl/curl.h))
endif
CURL_CFLAGS:=
CURL_LIBS:=
HAVE_LIBCURL:=0
ifneq ($(CURL_CONFIG_BIN),)
HAVE_LIBCURL:=1
CURL_CFLAGS:=$(shell $(CURL_CONFIG_BIN) --cflags)
CURL_LIBS:=$(shell $(CURL_CONFIG_BIN) --libs)
else ifeq ($(CURL_PKG_OK),yes)
HAVE_LIBCURL:=1
CURL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libcurl)
CURL_LIBS:=$(shell $(PKG_CONFIG) --libs libcurl)
else ifneq ($(CURL_HEADER_LOCAL),)
HAVE_LIBCURL:=1
CURL_LIBS:=-lcurl
endif

#
# Linux AArch64 / PortMaster cross-build validation
#
ifeq ($(TARGET_LINUX_AARCH64),1)
AARCH64_CC_OK:=$(strip $(shell command -v $(CCOMP) >/dev/null 2>&1 && echo yes))
PKG_CONFIG_OK:=$(strip $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes))
SDL2_PKG_CFLAGS:=$(strip $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null))
SDL2_PKG_LIBS:=$(strip $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null))
CURL_PKG_CFLAGS:=$(strip $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null))
CURL_PKG_LIBS:=$(strip $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null))
AARCH64_BADARCH_PATTERN:=x86_64|i686|amd64|mingw32
SDL2_PKG_BADARCH:=$(strip $(shell echo '$(SDL2_PKG_CFLAGS) $(SDL2_PKG_LIBS)' | grep -E '$(AARCH64_BADARCH_PATTERN)' >/dev/null 2>&1 && echo yes))
CURL_PKG_BADARCH:=$(strip $(shell echo '$(CURL_PKG_CFLAGS) $(CURL_PKG_LIBS)' | grep -E '$(AARCH64_BADARCH_PATTERN)' >/dev/null 2>&1 && echo yes))
ifeq ($(PORTMASTER_DIAG),0)
ifeq ($(AARCH64_CC_OK),)
$(error TARGET_LINUX_AARCH64=1 requires $(CCOMP) in PATH. Install a Linux AArch64 cross toolchain, or override CROSS_COMPILE/CCOMP.)
endif
ifeq ($(PKG_CONFIG_OK),)
$(error TARGET_LINUX_AARCH64=1 requires a working $(PKG_CONFIG) in PATH. Point PKG_CONFIG to a target-aware pkg-config or install pkg-config.)
endif
ifeq ($(FLAG_USE_SDL1),0)
ifeq ($(SDL2_PKG_OK),)
$(error TARGET_LINUX_AARCH64=1 requires target SDL2 development files discoverable via $(PKG_CONFIG). Set PKG_CONFIG_LIBDIR / PKG_CONFIG_SYSROOT_DIR for your AArch64 sysroot, or install libsdl2-dev:arm64.)
endif
ifeq ($(SDL2_PKG_BADARCH),yes)
$(error TARGET_LINUX_AARCH64=1 is picking up host/non-AArch64 SDL2 flags from $(PKG_CONFIG): '$(SDL2_PKG_CFLAGS) $(SDL2_PKG_LIBS)'. Fix PKG_CONFIG_LIBDIR / PKG_CONFIG_SYSROOT_DIR.)
endif
endif
ifneq ($(FLAG_REMOTE_ROMS_LIBCURL),0)
ifeq ($(FLAG_REMOTE_ROMS),0)
else
ifeq ($(CURL_PKG_OK),)
$(error TARGET_LINUX_AARCH64=1 with FLAG_REMOTE_ROMS_LIBCURL=1 requires target libcurl development files via $(PKG_CONFIG), or set FLAG_REMOTE_ROMS_LIBCURL=0.)
endif
ifeq ($(CURL_PKG_BADARCH),yes)
$(error TARGET_LINUX_AARCH64=1 is picking up host/non-AArch64 libcurl flags from $(PKG_CONFIG): '$(CURL_PKG_CFLAGS) $(CURL_PKG_LIBS)'. Fix PKG_CONFIG_LIBDIR / PKG_CONFIG_SYSROOT_DIR, or set FLAG_REMOTE_ROMS_LIBCURL=0.)
endif
endif
endif
endif
endif

#
# Linux - specific
#
ifeq ($(TSYS),linux)
CFLAGS+= -DTARGET_LINUX
ifeq ($(TARGET_LINUX_AARCH64),1)
CFLAGS+= -DTARGET_LINUX_AARCH64
endif
ifeq ($(TARGET_PORTMASTER),1)
CFLAGS+= -DTARGET_PORTMASTER
endif
ifeq ($(FLAG_USE_SDL1),0)
ifneq ($(SDL2_CONFIG_BIN),)
CFLAGS+= $(shell $(SDL2_CONFIG_BIN) --cflags)
LINKB+= $(shell $(SDL2_CONFIG_BIN) --libs)
else ifeq ($(SDL2_PKG_OK),yes)
CFLAGS+= $(SDL2_PKG_CFLAGS)
LINKB+= $(SDL2_PKG_LIBS)
else ifneq ($(TARGET_LINUX_AARCH64),1)
ifneq ($(SDL2_BREW_PREFIX),)
CFLAGS+= -I$(SDL2_BREW_PREFIX)/include/SDL2 -D_THREAD_SAFE
LINKB+= -L$(SDL2_BREW_PREFIX)/lib -lSDL2
else
LINKB+= -lSDL2
endif
endif
else
CFLAGS+= -DUSE_SDL1
LINKB+= -lSDL
endif
LINKB+= -lasound
LINKB+= -lm
LINKB+= -ldl
LINKB+= -lpthread
ENABLE_VCAP=$(FLAG_VCAP)
ENABLE_ICAP=$(FLAG_ICAP)
ENABLE_IREP=$(FLAG_IREP)
ENABLE_DISPLAY_FILTERS=$(FLAG_DISPLAY_FILTERS)
ENABLE_MICROUI=$(FLAG_MICROUI)
ENABLE_DEBUGGER=$(FLAG_DEBUGGER)
ENABLE_MEMORY_TRACE=$(FLAG_MEMORY_TRACE)
ENABLE_BEAM_CAPTURE=$(FLAG_BEAM_CAPTURE)
ENABLE_BEAM_HISTORY=$(FLAG_BEAM_HISTORY)
ENABLE_SD_TRACE=$(FLAG_SD_TRACE)
ENABLE_SD_FAULT=$(FLAG_SD_FAULT)
ENABLE_SD_REPLAY=$(FLAG_SD_REPLAY)
ENABLE_AUDIO_TRACE=$(FLAG_AUDIO_TRACE)
ENABLE_API_SERVER=$(FLAG_API_SERVER)
ENABLE_NETPLAY=$(FLAG_NETPLAY)
ENABLE_ESP=$(FLAG_ESP)
ENABLE_REMOTE_ROMS=$(FLAG_REMOTE_ROMS)
ifeq ($(ENABLE_REMOTE_ROMS),0)
ENABLE_REMOTE_ROMS_LIBCURL=0
else
ENABLE_REMOTE_ROMS_LIBCURL=$(FLAG_REMOTE_ROMS_LIBCURL)
endif
ifeq ($(ENABLE_ESP),0)
ENABLE_ESP_SOFTAP=0
else
ENABLE_ESP_SOFTAP=$(FLAG_ESP_SOFTAP)
endif
endif
#
#
#
#
# macOS specific
#
ifeq ($(TSYS),macos)
OUT=cuzebox
CFLAGS+= -DTARGET_MACOS
ifeq ($(FLAG_USE_SDL1),0)
ifneq ($(SDL2_CONFIG_BIN),)
CFLAGS+= $(shell $(SDL2_CONFIG_BIN) --cflags)
LINKB+= $(shell $(SDL2_CONFIG_BIN) --libs)
else ifeq ($(SDL2_PKG_OK),yes)
CFLAGS+= $(shell $(PKG_CONFIG) --cflags sdl2)
LINKB+= $(shell $(PKG_CONFIG) --libs sdl2)
else ifneq ($(SDL2_BREW_PREFIX),)
CFLAGS+= -I$(SDL2_BREW_PREFIX)/include/SDL2 -D_THREAD_SAFE
LINKB+= -L$(SDL2_BREW_PREFIX)/lib -lSDL2
else
LINKB+= -lSDL2
endif
else
CFLAGS+= -DUSE_SDL1
LINKB+= -lSDL
endif
LINKB+= -framework CoreMIDI -framework CoreFoundation -framework CoreAudio
LINKB+= -lm
ENABLE_VCAP=$(FLAG_VCAP)
ENABLE_ICAP=$(FLAG_ICAP)
ENABLE_IREP=$(FLAG_IREP)
ENABLE_DISPLAY_FILTERS=$(FLAG_DISPLAY_FILTERS)
ENABLE_MICROUI=$(FLAG_MICROUI)
ENABLE_DEBUGGER=$(FLAG_DEBUGGER)
ENABLE_MEMORY_TRACE=$(FLAG_MEMORY_TRACE)
ENABLE_BEAM_CAPTURE=$(FLAG_BEAM_CAPTURE)
ENABLE_BEAM_HISTORY=$(FLAG_BEAM_HISTORY)
ENABLE_SD_TRACE=$(FLAG_SD_TRACE)
ENABLE_SD_FAULT=$(FLAG_SD_FAULT)
ENABLE_SD_REPLAY=$(FLAG_SD_REPLAY)
ENABLE_AUDIO_TRACE=$(FLAG_AUDIO_TRACE)
ENABLE_API_SERVER=$(FLAG_API_SERVER)
ENABLE_NETPLAY=$(FLAG_NETPLAY)
ENABLE_ESP=$(FLAG_ESP)
ENABLE_REMOTE_ROMS=$(FLAG_REMOTE_ROMS)
ifeq ($(ENABLE_REMOTE_ROMS),0)
ENABLE_REMOTE_ROMS_LIBCURL=0
else
ENABLE_REMOTE_ROMS_LIBCURL=$(FLAG_REMOTE_ROMS_LIBCURL)
endif
ifeq ($(ENABLE_ESP),0)
ENABLE_ESP_SOFTAP=0
else
ENABLE_ESP_SOFTAP=$(FLAG_ESP_SOFTAP)
endif
endif
#
#
# Windows - MinGW specific
#
ifeq ($(TSYS),windows_mingw)
OUT=cuzebox.exe
CFLAGS+= -DTARGET_WINDOWS_MINGW -Dmain=SDL_main
ifeq ($(FLAG_USE_SDL1),0)
LINKB= -lmingw32 -lSDL2main -lSDL2 -mwindows -lws2_32 -liphlpapi -licmp -lwinmm
else
CFLAGS+= -DUSE_SDL1
LINKB= -lmingw32 -lSDL2main -lSDL2 -mwindows -lws2_32 -liphlpapi -licmp -lwinmm
endif
ENABLE_VCAP=$(FLAG_VCAP)
ENABLE_ICAP=$(FLAG_ICAP)
ENABLE_IREP=$(FLAG_IREP)
ENABLE_DISPLAY_FILTERS=$(FLAG_DISPLAY_FILTERS)
ENABLE_MICROUI=$(FLAG_MICROUI)
ENABLE_DEBUGGER=$(FLAG_DEBUGGER)
ENABLE_MEMORY_TRACE=$(FLAG_MEMORY_TRACE)
ENABLE_BEAM_CAPTURE=$(FLAG_BEAM_CAPTURE)
ENABLE_BEAM_HISTORY=$(FLAG_BEAM_HISTORY)
ENABLE_SD_TRACE=$(FLAG_SD_TRACE)
ENABLE_SD_FAULT=$(FLAG_SD_FAULT)
ENABLE_SD_REPLAY=$(FLAG_SD_REPLAY)
ENABLE_AUDIO_TRACE=$(FLAG_AUDIO_TRACE)
ENABLE_API_SERVER=$(FLAG_API_SERVER)
ENABLE_NETPLAY=$(FLAG_NETPLAY)
ENABLE_ESP=$(FLAG_ESP)
ENABLE_REMOTE_ROMS=$(FLAG_REMOTE_ROMS)
ifeq ($(ENABLE_REMOTE_ROMS),0)
ENABLE_REMOTE_ROMS_LIBCURL=0
else
ENABLE_REMOTE_ROMS_LIBCURL=$(FLAG_REMOTE_ROMS_LIBCURL)
endif
ifeq ($(ENABLE_ESP),0)
ENABLE_ESP_SOFTAP=0
else
ENABLE_ESP_SOFTAP=$(FLAG_ESP_SOFTAP)
endif
CHCONV=chconv.exe
BINCONV=binconv.exe
endif
#
#
# Emscripten - specific
#
ifeq ($(TSYS),emscripten)
OUT=cuzebox.html
CFLAGS+= -DTARGET_EMSCRIPTEN -DUSE_SDL1 -s USE_SDL=1 -s NO_EXIT_RUNTIME=1 -s NO_DYNAMIC_EXECUTION=1
#add stuff for websockets(requires about:config in browser: javascript.options.shared_memory true
CFLAGS+= -lwebsocket.js -s PROXY_POSIX_SOCKETS -s USE_PTHREADS=1 -s PROXY_TO_PTHREAD=1 -s PTHREAD_POOL_SIZE=2
ENABLE_VCAP=0
ENABLE_ICAP=0
ENABLE_IREP=0
ENABLE_DISPLAY_FILTERS=0
ENABLE_MICROUI=0
ENABLE_DEBUGGER=0
ENABLE_MEMORY_TRACE=0
ENABLE_BEAM_CAPTURE=0
ENABLE_BEAM_HISTORY=0
ENABLE_SD_TRACE=0
ENABLE_SD_FAULT=0
ENABLE_SD_REPLAY=0
ENABLE_AUDIO_TRACE=0
ENABLE_API_SERVER=0
ENABLE_NETPLAY=0
ENABLE_ESP=0
ENABLE_ESP_SOFTAP=0
ENABLE_REMOTE_ROMS=0
ENABLE_REMOTE_ROMS_LIBCURL=0
ifeq ($(FLAG_SELFCONT),0)
ifeq ($(FLAG_NOGAMEFILE),0)
LINKB= --preload-file gamefile.uze --preload-file TELNET.DAT --preload-file DISK.CFG --preload-file CPMDISK0.DSK --preload-file CPMDISK1.DSK --preload-file CPMDISK2.DSK
endif
endif
endif
# SD deterministic replay relies on the debugger event-stop path. Keep an
# explicitly overridden FLAG_SD_REPLAY from creating an invalid non-debug build.
ifeq ($(ENABLE_DEBUGGER),0)
ENABLE_SD_REPLAY=0
ENABLE_AUDIO_TRACE=0
endif
#
#
# When asking for debug edit
#
ifeq ($(GO),test)
CFSPD=-O0 -g
CFSIZ=-O0 -g
CFLAGS+= -DTARGET_DEBUG
endif
#
#
# 'Production' edit
#
ifeq ($(TSYS),emscripten)
CFSPD?=-O3 --llvm-lto 3 -s ASSERTIONS=0 -s AGGRESSIVE_VARIABLE_ELIMINATION=1
CFSIZ?=-Os --llvm-lto 3 -s ASSERTIONS=0
else
CFSPD?=-O3 -s -flto
CFSIZ?=-Os -s -flto
endif
#
#
# Now on the way...
#

LINKB?=
LINK= $(LINKB)
OUT?=cuzebox
CHCONV?=chconv
BINCONV?=binconv
CC=$(CCOMP)
CFLAGS+= -DVER_DATE=$(VER_DATE)
ifneq ($(PATH_GAMECONTROLLERDB),)
CFLAGS+= -DPATH_GAMECONTROLLERDB="\"$(PATH_GAMECONTROLLERDB)\""
endif
ifneq ($(ENABLE_VCAP),0)
CFLAGS+= -DENABLE_VCAP=1
endif
ifneq ($(ENABLE_ICAP),0)
CFLAGS+= -DENABLE_ICAP=1
endif
ifneq ($(ENABLE_IREP),0)
CFLAGS+= -DENABLE_IREP=1
endif
ifneq ($(FLAG_DISPLAY_GAMEONLY),0)
CFLAGS+= -DFLAG_DISPLAY_GAMEONLY=1
endif
ifneq ($(FLAG_DISPLAY_SMALL),0)
CFLAGS+= -DFLAG_DISPLAY_SMALL=1
endif
ifneq ($(FLAG_DISPLAY_FRAMEMERGE),0)
CFLAGS+= -DFLAG_DISPLAY_FRAMEMERGE=1
endif
ifneq ($(ENABLE_DISPLAY_FILTERS),0)
CFLAGS+= -DENABLE_DISPLAY_FILTERS=1
endif
ifneq ($(FLAG_SELFCONT),0)
CFLAGS+= -DFLAG_SELFCONT=1
endif
ifneq ($(FLAG_NOCONSOLE),0)
CFLAGS+= -DFLAG_NOCONSOLE=1
endif
ifneq ($(FLAG_NATIVE),0)
CFLAGS+= -DFLAG_NATIVE=1
endif
ifneq ($(FLAG_HEADLESS),0)
CFLAGS+= -DHEADLESS=1
endif
ifneq ($(ENABLE_MICROUI),0)
CFLAGS+= -DENABLE_MICROUI=1
endif
ifneq ($(FLAG_GUI_VKEYBOARD),0)
CFLAGS+= -DENABLE_GUI_VKEYBOARD=1
endif
CFLAGS+= -DFLAG_GUI_VKEYBOARD_DEFAULT=$(FLAG_GUI_VKEYBOARD_DEFAULT)
ifneq ($(ENABLE_DEBUGGER),0)
CFLAGS+= -DENABLE_DEBUGGER=1
ifneq ($(ENABLE_MEMORY_TRACE),0)
CFLAGS+= -DENABLE_MEMORY_TRACE=1
endif
ifneq ($(ENABLE_BEAM_CAPTURE),0)
CFLAGS+= -DENABLE_BEAM_CAPTURE=1
endif
ifneq ($(ENABLE_BEAM_HISTORY),0)
CFLAGS+= -DENABLE_BEAM_HISTORY=1
endif
ifneq ($(ENABLE_SD_TRACE),0)
CFLAGS+= -DENABLE_SD_TRACE=1
endif
ifneq ($(ENABLE_SD_FAULT),0)
CFLAGS+= -DENABLE_SD_FAULT=1
endif
ifneq ($(ENABLE_SD_REPLAY),0)
CFLAGS+= -DENABLE_SD_REPLAY=1
endif
ifneq ($(ENABLE_AUDIO_TRACE),0)
CFLAGS+= -DENABLE_AUDIO_TRACE=1
endif
endif
ifneq ($(ENABLE_API_SERVER),0)
CFLAGS+= -DENABLE_API_SERVER=1
endif
ifneq ($(ENABLE_ESP),0)
CFLAGS+= -DENABLE_ESP=1
endif
ifneq ($(ENABLE_NETPLAY),0)
CFLAGS+= -DENABLE_NETPLAY=1
endif
REMOTE_ROMS_LIBCURL_ACTIVE:=0
ifneq ($(ENABLE_REMOTE_ROMS),0)
ifneq ($(ENABLE_REMOTE_ROMS_LIBCURL),0)
ifneq ($(TARGET_LINUX_AARCH64),0)
REMOTE_ROMS_LIBCURL_ACTIVE:=1
else ifneq ($(HAVE_LIBCURL),0)
REMOTE_ROMS_LIBCURL_ACTIVE:=1
else
$(info libcurl development files not found; Remote ROMs will use the shell curl fallback)
endif
endif
endif

ifneq ($(ENABLE_REMOTE_ROMS),0)
CFLAGS+= -DENABLE_REMOTE_ROMS=1
endif
ifneq ($(REMOTE_ROMS_LIBCURL_ACTIVE),0)
CFLAGS+= -DENABLE_REMOTE_ROMS_LIBCURL=1
ifeq ($(TARGET_LINUX_AARCH64),1)
CFLAGS+= $(CURL_PKG_CFLAGS)
LINKB+= $(CURL_PKG_LIBS)
else
CFLAGS+= $(CURL_CFLAGS)
LINKB+= $(CURL_LIBS)
endif
endif
ifneq ($(ENABLE_ESP_SOFTAP),0)
CFLAGS+= -DENABLE_ESP_SOFTAP=1
endif

OBD=_obj_

CFLAGS+= -Wall -pipe -pedantic -Wno-variadic-macros
ifneq ($(CC_BIN),)
CFLAGS+= -B$(CC_BIN)
endif
ifneq ($(CC_LIB),)
CFLAGS+= -L$(CC_LIB)
endif
ifneq ($(CC_INC),)
CFLAGS+= -I$(CC_INC)
endif

CFSPD+= $(CFLAGS)
CFSIZ+= $(CFLAGS)

