# Input topology and virtual devices

This page documents the newer input architecture in CUzeBox: optional UzeTap topology per port, slot-based virtual devices, selected-slot UzeBus traffic, and the netplay rules that keep this deterministic.

## High-level model

Each Uzebox controller port can be modeled in one of two ways:

- **direct**: one device is attached directly to the port
- **tap present**: a UzeTap-style selector is present and one of four slots is currently active

The emulator treats this as a topology problem first, then a device problem.

## Port topology

Per port, the runtime keeps track of:

- whether a tap is present
- the currently selected slot
- whether a one-shot tap probe is pending
- timeout state for automatically returning to slot 0
- slot-to-virtual-device assignment for slots 0..3

A port without a tap simply behaves as though slot 0 is the attached device.

## Virtual devices

A slot points to a **virtual device** rather than directly to a host input device. This allows one emulator-side device to be composed from one or two real host inputs.

Current virtual device categories are:

- `Pad16`
- `SuperMouse32`
- `Keyboard`
- haptic-capable selected-slot devices

### Why virtual devices exist

Virtual devices allow combinations such as:

- gamepad buttons + mouse motion -> one `SuperMouse32` device
- mouse buttons + light-sense handling -> lightgun-style device
- a named keyboard dongle device assigned to a nonzero tap slot

This keeps the slot model stable even if host input devices change.

## Packet-style devices

For most gameplay input, a slot behaves as a packet device.

### `Pad16`

- produces the standard 16-bit pad packet
- higher bits idle high after the defined packet width

### `SuperMouse32`

This is the general packet format used for:

- SNES mouse
- super mouse
- lightgun-style packet devices

In practice this means:

- lower 16 bits carry digital buttons and signature bits
- upper 16 bits carry relative axis deltas when applicable
- lightgun support may use the lower 16-bit region for trigger/light-sense semantics while still sharing the same broad packet format

### SuperMouse tuning

Each `SuperMouse32` virtual device now has local tuning for relative mouse motion:

- X scale percentage
- Y scale percentage
- deadzone
- invert X
- invert Y

These are applied before the emulator encodes the upper 16-bit relative delta packet. This makes it possible to tune one host mouse for different in-game expectations without changing the slot/tap/netplay model.

## Keyboard dongle devices

Keyboard devices are not packet devices. They use the selected slot’s UzeBus session path.

Important details:

- keyboard queues are maintained per port
- selected-slot keyboard sessions use the same UzeBus start condition as other UzeBus devices
- the selected slot decides what the payload bytes mean
- the old direct-P2 keyboard fallback still exists for compatibility

### Frame-boundary visibility

Keyboard bytes are not injected into the visible dongle queue immediately when the host OS sends a key event. Instead, local host keyboard bytes are staged in a pending per-port queue and become visible only at the next frame boundary.

This matches the netplay rule for remote keyboard bytes, which are also injected only at frame boundaries. The result is:

- a game cannot see a newly arrived host keyboard byte halfway through a frame
- local and remote keyboard visibility follow the same frame-based rule
- multiple polls within one frame see a stable queue state except for bytes the game itself consumes

This is intentionally less immediate than raw asynchronous injection, but it avoids a large class of rollback desyncs.

## UzeTap control path

Tap control is separate from selected-slot device traffic.

### Tap control

Tap control uses a latch-only side channel with `CLOCK` held high. Conceptually, it supports:

- select slot
- one-shot probe on next read

The emulator models the decoded result of those control actions. It does **not** try to make netplay reproduce the exact raw pulse stream.

### Probe behavior

A pending probe causes the next normal read to prepend a one-shot tap identifier byte as byte 1, then shift selected-device data after it.

## UzeBus device traffic

After a slot is selected, the tap returns to transparent mode and the selected device sees ordinary traffic for that slot.

For UzeBus-style devices this means:

- keyboard and haptic devices use the same start condition/signal
- dispatch happens from the payload bytes after the session starts
- the emulator does not use a separate start condition for haptics

## Haptics

The kernel-side haptic engine uses its own internal RLE timing and sends a bus-level payload of the form:

- `0b01iiiMm0`

where:

- `iii` is the bus/device ID
- `Mm` is the motor state

The emulator reacts to that decoded bus-level state and routes it to the configured host rumble binding. It does **not** emulate the kernel’s internal RLE stream directly.

Remote haptics are currently omitted from netplay because they are local side effects and do not affect gameplay state.

## Netplay model

### Packet seats

Rollback/netplay packet seats are extended to 8 logical seats so both P1 and P2 may each expose four slots:

- P1/S0
- P2/S0
- P1/S1
- P2/S1
- P1/S2
- P2/S2
- P1/S3
- P2/S3

Packet devices synchronize as effective slot packet state, not merely as the older direct button array.

### Decoded device events

Non-packet device actions are synchronized as decoded events, not as raw electrical traffic. That includes:

- tap select
- tap probe
- keyboard bytes

These events carry an explicit target frame and are replayed during rollback resimulation.

### Why keyboard bytes are frame-staged

If a game branches on whether a keyboard byte is present during a particular frame, visibility on different frames across peers can cause a deterministic desync.

To prevent that, keyboard bytes become visible only at frame boundaries on both local and remote peers.

## Known practical limits

- one shared host keyboard is realistic; truly separate physical keyboards are not currently modeled
- one shared host mouse is realistic; truly separate physical mice are not currently modeled
- many host gamepads/controllers are realistic
- generic arbitrary UzeBus-over-netplay is still less complete than packet-state sync plus decoded keyboard/tap events

## Recommended mental model

A good way to think about the system is:

1. choose a topology per port
2. assign virtual devices to slots
3. route gameplay packets from packet devices
4. route selected-slot UzeBus traffic for keyboard/haptic devices
5. in netplay, synchronize packets plus decoded device events, not raw line activity

## Lightgun notes

A lightgun-capable `SuperMouse32` virtual device now samples light directly from
its mouse/window position when building the vdev packet. Mouse button bits may
still populate the normal lower 16-bit `SuperMouse32` region, and the gun
signature / trigger / light nibble in `F000` is built as part of the normal
vdev packet path used by slot routing and packet netplay capture.

The trigger bit is no longer split by port (P1 left / P2 right). A mouse-backed
lightgun trigger now comes from an explicit mouse binding with the `TRIGGER`
flag, using the normal left mouse button. Configuring two separate lightgun
virtual devices against one shared host mouse is considered unsupported.

The older direct lightgun toggle still exists as a fallback for non-vdev usage,
but it is intentionally treated as a single-mouse / single-gun convenience
path. The preferred model is a `SuperMouse32` virtual device with `LIGHT
SENSE` enabled.

## Netplay and UzeTap

For netplay, CUzeBox treats UzeTap control as decoded frame events rather than as raw LATCH/CLOCK/DATA signaling. Tap slot selection and one-shot probe requests are scheduled to deterministic frame boundaries, just like keyboard byte injection. This prevents slot state or probe visibility from diverging between peers due to packet arrival timing.


## Topology matching

Netplay now requires both peers to agree on the effective input topology used for emulation:

- tap present on P1/P2
- active slot on each port
- slot-to-virtual-device assignment
- virtual-device emulation type/options/packet defaults

Host bindings do not need to match, but the emulated topology must. If the peers disagree, CUzeBox will reject the session as an input-topology mismatch.


## Low16 remap

Virtual devices now support per-binding low16 remap. For digital bindings, each host source can be mapped to a specific lower-16-bit emulated button bit (or left unmapped). This happens locally before packet generation, so netplay still synchronizes the final packet and does not need to understand the user's local remap choices.


## Input Trace Window

CUzeBox now includes an **INPUT TRACE** tool window. It records per-frame packet snapshots for all emulated seats plus applied decoded device events such as tap select/probe and keyboard byte visibility. This is intended for debugging topology, rollback timing, and keyboard/tap behavior without reading raw controller line traffic. The trace window now also supports basic **filtering** (frames/tap/keyboard, P1/P2) and **export** of either the filtered view or the full captured buffer to text files.


## Save states and rollback checkpoints

CUzeBox save states now carry an input-state extension block in addition to the CPU / SPI RAM / SD / VFAT / ESP core state. The extension restores the runtime state that can affect deterministic input behavior, including:

- UzeTap presence, active slot, probe-pending flag, timeout activity, and slot-to-vdev assignment
- controller latched shift state, packet overrides, and selected-slot UzeBus session state
- keyboard dongle port queues and pending frame-boundary queues
- legacy/direct lightgun state
- virtual-device library contents and runtime accumulators

This keeps manual save/load and rollback checkpoints much closer to the live input topology, especially for tap-selected slots, keyboard devices, and frame-boundary keyboard visibility.

State that is intentionally not preserved in this extension includes host-device handles, host rumble backends, and UI-only trace buffers.


## Binding learn mode

Virtual-device bindings now include a **LEARN** action for low16 remap. Pick a **LEARN TARGET** bit in the binding editor, press **LEARN**, then press the next host button on that binding. The pressed source is remapped to the selected low16 target and the learn mode exits.

Notes:
- Learn currently supports host keyboard, host mouse, and SDL game controller bindings.
- Joystick-only bindings without live raw-button support do not participate in learn mode.
- Learn clears the same target from other sources in the same binding before assigning the newly learned source.

- Virtual-device editor quality-of-life helpers now include **DUPLICATE**, **COPY A->B / B->A** for host bindings, and **RESET TUNING** for SuperMouse profiles.


## Topology presets

The DEVICES window now includes quick topology presets for common setups such as `2 PADS`, `P1 TAP 4P`, `DUAL 8P`, `KBD P1/S1`, and `GUN+PAD`. These presets reset the current virtual-device library and rebuild a fresh topology using reusable virtual devices and default bindings. They are meant as a fast starting point; host bindings, remaps, and tuning can still be edited afterward.

## Per-ROM input profiles

CUzeBox can now save and reload a **ROM-specific input profile** keyed to the current ROM name and CRC.

These profiles capture the live input topology subset:
- tap present / active slot / slot assignment
- virtual-device library
- host bindings
- low16 remap tables
- SuperMouse tuning
- input and mouse toggles relevant to device routing

They do **not** replace the global `config.cfg`. They are an overlay applied only when a matching ROM-specific profile exists.

The DEVICES page exposes:
- **SAVE PROFILE**
- **LOAD PROFILE**

Profiles are also auto-saved on ROM switch/exit when the input topology was changed, and auto-loaded when the ROM is loaded again.
