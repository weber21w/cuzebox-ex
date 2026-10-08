############################
# Makefile - configuration #
############################
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
# Alter this file according to your system to build the thing
#
#
#
# Target operating system. This will define how the process will go
# according to the os's features. Currently supported:
#  linux
#  windows_mingw
#  macos   (native macOS build)
#  osx     (alias for macos)
#  emscripten
#
# You may also keep TSYS=linux and ask for cross-compiling / PortMaster
# packaging on the make command line:
#   make TARGET_LINUX_AARCH64=1
#   make TARGET_PORTMASTER=1 package-portmaster
#
TSYS=windows_mingw

#
# Cross-compile / packaging helper toggles. These are intended to be set on
# the make command line rather than edited here permanently.
#
# TARGET_LINUX_AARCH64=1  -> build a Linux AArch64 binary using CROSS_COMPILE
# TARGET_PORTMASTER=1     -> TARGET_LINUX_AARCH64 plus handheld-friendly
#                            defaults and PortMaster packaging helpers
#
TARGET_LINUX_AARCH64?=0
TARGET_PORTMASTER?=0
PORTMASTER_DIAG?=0
#
# Prefix for a Linux AArch64 cross toolchain, for example:
#   CROSS_COMPILE=aarch64-linux-gnu-
#
CROSS_COMPILE?=aarch64-linux-gnu-
#
# pkg-config command to use for dependency lookup. For cross builds it is often
# best to keep this as pkg-config and supply PKG_CONFIG_LIBDIR / SYSROOT via the
# environment or the make command line.
#
PKG_CONFIG?=pkg-config
#
# Optional explicit path to an SDL2 config helper. For native builds leave this
# empty to auto-detect. For cross builds it is normally better to leave it
# empty and rely on pkg-config from the target sysroot.
#
SDL2_CONFIG?=
#
# Optional explicit path to a curl-config helper. For cross builds it is also
# normally better to leave this empty and rely on pkg-config from the target
# sysroot.
#
CURL_CONFIG?=
#
#
# A few paths in case they would be necessary. Leave them alone unless
# it is necessary to modify.
#
# For a Windows build, you might need locating SDL2 here. When doing a cross
# compile from (Debian) Linux to 32 bit Windows, the followings might work
# assuming that a development library was downloaded from libsdl.org:
#
#CC_INC=SDL2-2.0.4/i686-w64-mingw32/include
#CC_LIB=SDL2-2.0.4/i686-w64-mingw32/lib
#CCOMP=i686-w64-mingw32-gcc
#CCNAT=gcc
#
# Note that for Emscripten builds you might also have to define these to
# compile assets necessary to build the emulator.
#
# For macOS, SDL2 is auto-detected in this order when TSYS=macos or osx:
#   1. sdl2-config
#   2. pkg-config sdl2
#   3. Homebrew (brew --prefix sdl2)
# You can still override by setting CC_INC / CC_LIB / CCOMP / CCNAT here.
#
CC_BIN=
CC_INC=
CC_LIB=
#
#
# Version number to use. It should be a BCD date of YYYYMMDD format.
#
VER_DATE=0x20260805
#
#
# In case a test build (debug) is necessary, give 'test' here. It enables
# extra assertions, and compiles the program with no optimizations, debug
# symbols enabled.
#
GO=
#
#
# An extra path to game controller config files for SDL2 builds. By default
# the emulator looks for such a file (gamecontrollerdb.txt) on the SDL app
# path (if there is any), you can supply an additional path here. You can get
# such a file from https://github.com/gabomdq/SDL_GameControllerDB , such a
# file may also be present on your system as part of some other application
# (for example gnome-games uses ~/.config/gnome-games/gamecontrollerdb.txt,
# the code won't recognize "~" though). You may provide that location here to
# "leech" that config, so CUzeBox's controller mappings update when you update
# that application.
#
PATH_GAMECONTROLLERDB=
#
#
# Should the video capture feature be built in? Note that it requires ffmpeg
# and it is not possible to have it in the Emscripten build.
#
FLAG_VCAP=1
#
#
# Should the input capture feature be built in? Note that it is not possible
# to have it in the Emscripten build.
#
FLAG_ICAP=1
#
#
# Should the input replay feature be built in? Note that it is not possible
# to have it in the Emscripten build. Requires FLAG_ICAP=1 above.
#
FLAG_IREP=1
#
#
# Initial display: Game only (1) or show the emulator interface (0) (currently
# memory occupation & sync signals). The F3 key may toggle it runtime.
#
FLAG_DISPLAY_GAMEONLY=0
#
#
# Initial display size: A "small" (1) display is faster (it can be resized,
# but quality is low). The F2 key may toggle it runtime.
#
FLAG_DISPLAY_SMALL=0
#
#
# Initial state of frame merging: Enabling merging (1) makes certain games
# flickering less (notably which use some sprite cycling algorithms to get
# around limitations) while making the emulation running somewhat slower.
# The F7 key may toggle it runtime.
#
FLAG_DISPLAY_FRAMEMERGE=1

#
# Build display post-processing filters (CRT / scanlines). Set to 0 to
# compile without the display filter code and hide filter controls from the UI.
#
FLAG_DISPLAY_FILTERS=1

#
# Build the MicroUI overlay / top bar.
#
FLAG_MICROUI=1
#
# Build the optional on-screen virtual keyboard for MicroUI text fields.
# When disabled, handheld-style GUI control still works, but text entry
# requires a real keyboard.
#
FLAG_GUI_VKEYBOARD=1
#
# Default runtime state for the virtual keyboard when it is compiled in.
# 0 = off by default (PC-style builds)
# 1 = on by default  (handheld-style builds)
#
FLAG_GUI_VKEYBOARD_DEFAULT=0

#
# Default PortMaster / handheld packaging name.
#
PORTMASTER_NAME=CUzeBox
PORTMASTER_DIR=cuzebox
#
# Build debugger support (UI + breakpoints + inspector). Disable for
# original-speed builds with no debugger hot-path cost.
#
FLAG_DEBUGGER=1

# Build historical SRAM/I/O memory tracing. This defaults to the debugger
# setting, but may be disabled independently to keep the normal debugger and
# watchpoints while removing the history ring and its API/UI entirely.
#
FLAG_MEMORY_TRACE=$(FLAG_DEBUGGER)

# Build the optional per-cycle PORTC beam capture buffer. Live raster position
# remains available to debugger/API builds even when this is disabled.
FLAG_BEAM_CAPTURE=$(FLAG_DEBUGGER)

# Build the optional instruction-to-beam history ring. Disable independently
# when the debugger is wanted but historical instruction correlation is not.
FLAG_BEAM_HISTORY=$(FLAG_DEBUGGER)

# Build the optional SD protocol transaction history and SD-specific break
# triggers. This defaults to the debugger setting, but may be removed while
# retaining ordinary debugger and SD timing/status support.
FLAG_SD_TRACE=$(FLAG_DEBUGGER)

# Build deterministic SD fault injection. Fault rules are explicitly armed at
# runtime and are independent of protocol-history capture.
FLAG_SD_FAULT=$(FLAG_DEBUGGER)

# Build deterministic SD interaction record/replay. Recording is explicitly
# armed and captures card-visible SPI transfer starts/ends, CS/reset events,
# and relative timing. Replay validates the AVR stream and substitutes the
# recorded card response.
FLAG_SD_REPLAY=$(FLAG_DEBUGGER)

# Build cycle-correlated native Uzebox DAC tracing and audio break triggers.
# The trace/break path is explicitly armed at runtime and is compiled out
# independently when only the rest of the debugger is wanted.
FLAG_AUDIO_TRACE=$(FLAG_DEBUGGER)

#
# Build the localhost automation API server. This is temporary default-on for
# development and should be switched off before release builds.
#
FLAG_API_SERVER=1
#
# Build Netplay support. Set to 0 to compile a build with all Netplay code
# stubbed out and the Netplay UI hidden.
#
FLAG_NETPLAY=1
#
# Build ESP8266 peripheral emulation at all. Set to 0 to compile a build
# with the ESP8266 UART/network peripheral stubbed out entirely. This also
# forces SoftAP/LAN overlay support off.
#
FLAG_ESP=1
#
# Build ESP8266 SoftAP / LAN overlay emulation. Set to 0 to keep the base
# ESP8266 peripheral while disabling the SoftAP / CWLAP / lobby-style overlay
# helpers. Ignored when FLAG_ESP=0.
#
FLAG_ESP_SOFTAP=1

#
# Build remote ROM browser / downloader support.
#
FLAG_REMOTE_ROMS=1

#
# Use libcurl for Remote ROMs HTTP requests. When disabled, the build falls
# back to the older shell curl helper path.
#
FLAG_REMOTE_ROMS_LIBCURL=1
#
#
# Perform a self-contained build without filesystem access. This can only be
# done for games which don't demand files from the SD card. It integrates the
# game (gamefile.uze) in the emulator executable, for Emscripten this is a
# smaller build.
#
FLAG_SELFCONT=0
#
#
# Disable all console output. On some systems for some reason console output
# can be very slow, this eliminates all such calls, also reducing the
# application size (but good bye, debug info!).
#
FLAG_NOCONSOLE=0
#
#
# For an Emscripten build, disable linking with a game (also keep
# FLAG_SELFCONT clear for this to work). If you do this, you will need to
# supply the game and any further files externally, injecting it into the
# Emscripten FS before starting the emulator. Note that the default html
# output doesn't include displaying the contents of the error channel, so you
# won't see any error! (Open the Web console, and check for logs / errors
# there to check the error output of the gamefile loading functions)
#
FLAG_NOGAMEFILE=0
#
#
# For an Emscripten build, request compiling with the AVR CPU opcode emulation
# intended for native use. This is likely slower for this target, but results
# in ~25Kb smaller output.
#
FLAG_NATIVE=0
#
#
# Force using SDL1 instead of SDL2. The latter is the default for all targets
# except Emscripten.
#
FLAG_USE_SDL1=0
#
#
# Should anything written to the whisper ports be printed to stdout? Setting
# this to 1 will automatically disable all other console output.
#
FLAG_PRINTF_WHISPER=1
#
#
# Run without a window, audio, input, or console output(except whisper) for
# use with running multiple scripted runs(Uzenet server, primarily). Also
# allows a game to terminate emulation by reading 0x3A, or to enter turbo
# mode(unlimited Uzebox cycles, primarily for Uzenet bots).
#
FLAG_HEADLESS=0
