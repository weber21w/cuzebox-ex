# Overview

CUzeBox is an emulator and development environment oriented toward Uzebox software, hardware-adjacent workflows, and development tooling.

This documentation set is organized around the parts of the project that matter most during active development:

- building the emulator
- serial/UART and endpoint integration
- netplay and rollback behavior
- debugger features
- the automation API

## Current documentation priorities

The current focus of the docs is on features that are especially useful for development:

- TCP serial client and server modes
- UART profiles and serial trace
- debugger annotations, symbol import, watches, watchpoints, and per-ROM profiles
- per-ROM input profiles for topology, bindings, remaps, and SuperMouse tuning
- API-driven automation and testing

## Project areas

## Core emulator

The emulator provides the AVR/Uzebox execution environment, rendering, audio, input, storage, and system integration.


## SPI RAM and large-memory workflows

External SPI RAM size and behavior matter for tools, video modes, asset-heavy programs, and debugging memory assumptions.

See [SPI RAM](spiram.md).

## Display filters

CUzeBox includes a staged host-side filter pipeline for analog-style prefilters, scaling filters, and CRT-style post effects.

See [Video Filters](video-filters.md).

## Sound

Audio output can be tuned for lower latency, lower host cost, or more polished listening, and rollback/netplay behavior has some important caveats.

See [Sound Options](sound.md).

## Cheats

CUzeBox also includes a simple per-ROM cheat system for SRAM patching and testing workflows.

See [Cheats](cheats.md).

## Serial and peripheral development

Recent work has expanded serial/UART support beyond the virtual ESP8266 model to include additional endpoints and better observability.

See [Serial and Endpoints](serial.md).

For the virtual ESP8266 model specifically, see [ESP8266 Emulation](esp8266-emulation.md).

## Netplay and rollback

The project supports rollback-oriented workflows and development around multiplayer synchronization. Audio behavior during stalls and rollback needs special handling because already-queued host audio cannot be truly "rolled back."

See [Netplay](netplay.md). That page now starts with practical host/join usage, settings, and troubleshooting before moving into rollback and implementation details.

## Input topology, UzeTap, UzeBus, and virtual devices

Recent work adds a richer input model than simple direct pads. Each Uzebox port can optionally host a UzeTap-style slot selector, and each slot can be assigned a virtual device such as a pad, super mouse/lightgun-style packet device, keyboard dongle, or haptic-capable device.

See [Input Topology and Virtual Devices](input-topology.md). That page also documents the most important netplay rule for smart devices: gameplay-visible keyboard bytes are staged at frame boundaries on both local and remote peers so they do not appear mid-frame on one side but not the other.

## Debugger

The debugger is moving beyond a basic inspector and into a full development tool, with annotations, symbols, watchpoints, per-ROM saved state, and navigation controls.

See [Debugger](debugger.md).

## Automation API

The API is intended to support automated testing, scripting, and external tooling while keeping overhead minimal when disabled.

See [API Overview](api/index.md).

## Documentation style

A few guidelines will keep these docs consistent:

- keep each page focused on one subject
- include command and file path examples
- document defaults and planned pre-release changes clearly
- call out what is stable versus what is still evolving
