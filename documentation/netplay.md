# Netplay

This page starts with **how to use netplay** in the emulator, then moves into the developer-facing details.

Netplay in CUzeBox is built around rollback-style gameplay synchronization. The practical result is:

- both sides run the same game locally
- inputs are exchanged over the network
- short prediction windows are allowed
- if a correction is needed, gameplay state can be restored and resimulated

That makes netplay responsive, but it also means a few settings and debugging behaviors matter more than they would in a simpler lockstep design.

## Quick start

### Easiest path: Simple Netplay

The easiest flow is:

1. Load the ROM on the host side.
2. Open **Netplay**.
3. Choose **1. Simple Netplay**.
4. Enter a player name.
5. Either:
	- click **Host Game** to create a relay-backed room for the currently loaded game, or
	- enter a room code and click **Join Game**.
6. Open **Lobby**.
7. Mark both sides **Ready**.
8. Have the host start the match.

Important notes:

- **Host Game** requires a loaded ROM.
- **Join Game** can work even if the joining side has no ROM loaded yet, depending on ROM sync settings.
- If **Public Room** is enabled, the room can appear in **Browse Games**.
- If a room is private, the joiner needs the room code.

### Fastest rule of thumb

- Use **Simple Netplay** first.
- Use **Manual Online** only when you specifically need direct UDP host/join or want to inspect the transport setup in more detail.

## Main ways to connect

### 1. Relay room

This is usually the best first choice.

Use it when:

- you want the easiest setup
- you do not want to deal with manual UDP addressing first
- you want room codes and public room browsing

Current defaults used by the UI include:

- relay server host: `uzenet.us`
- relay server port: `43810`

Typical relay flow:

- host creates a relay room
- host gets a room code
- guest joins using the room code
- both sides enter the lobby
- seat assignments and readiness are coordinated there
- gameplay starts after the host begins the match

The relay path can also try to upgrade to a direct UDP path later if the rendezvous succeeds.

### 2. Direct UDP host/join

Use **2. Manual Online** when you want manual control.

The direct UDP controls expose:

- remote host
- remote UDP port
- local UDP port

Typical direct flow:

- on the host side, click **Host Game**
- on the guest side, enter the host address and UDP port, then click **Join Game**
- leave the local UDP port blank if you want the client side to auto-pick a port

The common default gameplay UDP port is `43800`.

Direct UDP is useful when:

- both sides can reach each other directly
- you want to test transport behavior without involving relay room management
- you are debugging lower-level connection issues

## Lobby flow

The lobby is where the pre-match session becomes concrete.

Things you can do there:

- set local display name
- set per-pad labels
- assign or request seats
- mark ready/unready
- start the match if you are the host
- exchange chat messages

Important behavior:

- the host is authoritative for seat assignment
- changing seats clears ready state
- both sides should be ready before the host starts the match

For multiplayer games with more than one owned pad per side, the lobby is where seat ownership and pad mapping become visible and editable.

## Settings that matter most

Open **Netplay -> Settings** for the main tuning controls.

### Rollback window

This is the number of frames that can be predicted ahead before the local side stalls.

In the UI this is described as:

> Frames predicted ahead before stall

Practical guidance:

- lower values reduce how far prediction can drift
- higher values can feel smoother over latency, but increase rollback work when corrections happen
- start conservative, then adjust upward only if you need it

### Input delay

This is a fixed local input delay in frames.

Practical guidance:

- lower delay feels more immediate
- higher delay can reduce how often prediction is needed
- for stable links, keep it small
- for harder network conditions, a small intentional delay can improve behavior

### ROM sync mode

The UI currently supports these modes:

- **Off**
- **Missing**
- **Mismatch**
- **Always**

Use cases:

- **Off**: both sides are expected to manage ROM loading themselves
- **Missing**: send the ROM only when the peer has none
- **Mismatch**: send when ROM identity does not match
- **Always**: always offer/supply ROM transfer

### Send ROM / Receive ROM

These control whether the local side is allowed to:

- send a ROM to the peer
- accept a ROM from the peer

This is useful when one side is the authoritative source for a known-good build.

### Network interface

If the machine has multiple interfaces, the settings page lets you choose the one netplay should use.

This is especially useful when:

- testing on a machine with VPNs or multiple adapters
- separating IPv4/IPv6 behavior
- forcing a specific interface during troubleshooting

## Typical usage recipes

### Hosting a normal session through relay

1. Load the ROM.
2. Open **Netplay -> Simple Netplay**.
3. Enter your name.
4. Choose whether the room should be public.
5. Click **Host Game**.
6. Share the room code with the other player if needed.
7. Open **Lobby**.
8. Check seat ownership and ready state.
9. Start the match.

### Joining a relay room

1. Open **Netplay -> Simple Netplay**.
2. Enter your name.
3. Enter the room code.
4. Click **Join Game**.
5. Wait for the room and assets to synchronize.
6. Open **Lobby**.
7. Mark ready.

### Testing direct UDP manually

1. Open **Netplay -> Manual Online**.
2. On one side, click **Host Game** and note the local UDP port.
3. On the other side, enter the host address and that UDP port.
4. Leave local blank for auto-port unless you are deliberately testing fixed port behavior.
5. Click **Join Game**.
6. Check **Status** and **Lobby**.

## What to look at while debugging a session

Useful windows:

- **Netplay -> Status**
- **Lobby**
- **Settings**
- the message log / status text in the main UI

Things worth checking first:

- are both sides on the same ROM or expected ROM sync behavior?
- did both sides reach the lobby?
- are both sides ready?
- did the session start?
- is transport direct UDP, relay, or relay-assisted direct?
- are rollback window and input delay set sensibly?

## Troubleshooting from a user point of view

### "We connected, but the match will not start"

Check:

- both sides are in the lobby
- seat assignments are valid
- both sides are ready
- the host actually clicked start

### "The guest joined with no ROM"

That can be valid if ROM sync is enabled and the host is allowed to send the current game.

Check:

- send/receive ROM settings
- ROM sync mode
- ROM TCP port / transfer allowance

### "One side stalls and then catches up"

That is expected behavior in a rollback-oriented model.

The local side may predict ahead until it reaches the rollback window. After that it waits for missing remote input.

### "The music seems behind after a stall"

That is a known consequence of audio not being truly rollback-safe.

CUzeBox now intentionally flushes queued local audio at rollback-correction and stall-entry boundaries so stale audio does not keep trailing behind corrected gameplay state.

That behavior is the least-wrong compromise because audio already handed toward the host device cannot be reliably unplayed.

### "The relay room exists, but direct transport never becomes active"

That can still be a valid session.

Relay mode may continue to carry the gameplay traffic if direct UDP punch-through is unavailable or unsuitable.

## Developer details

## Mental model

The useful split is:

- **gameplay state** can be rolled back and resimulated
- **host audio output** cannot be truly rolled back after the host device has begun consuming queued samples

That one distinction explains a lot of the implementation behavior.

## Runtime pieces to keep in mind

Useful concepts exposed by the runtime and UI include:

- local frame
- remote frame/input state
- rollback window
- input delay
- predicted vs stalled state
- resimulation range
- lobby readiness and seat ownership
- ROM identity and ROM transfer state
- relay mode vs direct transport

The emulator also tracks compatibility-related identity such as:

- ROM CRC
- build ID
- feature flags
- local/peer player mask

## Rollback behavior

At a high level:

1. local emulation advances using local input and available remote input
2. if remote input for a predicted frame later arrives and differs from the prediction, a correction is applied
3. an older checkpoint is restored
4. affected frames are resimulated up to the current point

The current runtime UI also exposes whether:

- any prediction occurred
- a stall occurred
- a resimulation was applied
- what frame range was resimulated

## Audio behavior during rollback and stalls

The current development behavior is intentionally conservative.

On rollback-correction or stall-entry boundaries, emulator-side queued audio is flushed so stale audio does not continue playing after corrected gameplay state has advanced.

Why this is done:

- the host audio device consumes samples asynchronously
- the emulator cannot reliably know exactly which queued samples have already been played
- trying to "roll back audio" after host consumption starts is more error-prone than flushing and resuming from the corrected state

So the policy is:

- keep gameplay state authoritative
- discard queued local audio when a correction boundary makes that necessary
- accept a discontinuity as less wrong than letting stale music or sound effects trail behind

## Relay and direct path details

The relay flow is not just a dumb tunnel.

The runtime tracks whether:

- relay mode is active
- a room is established
- a peer is present
- a direct UDP candidate exists
- a direct path was successfully established

So a relay-backed session can move through states like:

- relay room created or joined
- peer discovered through relay
- direct UDP punch attempted
- direct path established
- or relay transport retained if direct path is not available

That is why a session may still function correctly even if it never upgrades to direct transport.

## ROM synchronization details

ROM synchronization is a separate concern from gameplay packet exchange.

The runtime tracks:

- local and peer ROM CRC
- local and peer ROM size
- ROM TCP port capability
- whether transfer is needed
- whether the current ROM is present and sendable

The practical point is:

- compatibility decisions can happen before gameplay really begins
- one side may be allowed to bootstrap the other side into the correct ROM image depending on policy

## Suggested debugging order for developers

When diagnosing a netplay problem, check in this order:

1. **Build compatibility**
	- same feature set?
	- same ROM identity?
	- same expected player mask/max players?
2. **Transport state**
	- connected?
	- direct UDP, relay, or relay-assisted direct?
	- correct interface selection?
3. **Lobby state**
	- seat ownership valid?
	- both sides ready?
	- host started the match?
4. **Runtime rollback state**
	- stalling?
	- resim ranges large?
	- input delay and rollback window reasonable?
5. **Audio symptoms**
	- gameplay desync, or only audio behavior after correction?

## Useful future documentation additions

Good follow-up additions for this page would be:

- screenshots of the Simple Netplay, Settings, Status, and Lobby windows
- a complete annotated host/join example
- a dedicated page for relay room browsing and public room flow
- API examples for automated netplay testing
- a developer note on checkpoint cadence and rollback cost


## Deterministic smart-device input

Traditional rollback netplay only needs per-frame controller state. CUzeBox now also has a second class of deterministic inputs for selected-slot smart devices such as keyboard dongles and tap control.

The important rules are:

- packet devices still synchronize as per-seat packet state
- tap slot selection and probe are synchronized as decoded events, not as raw LATCH/CLOCK/DATA wiggles
- keyboard bytes are synchronized as decoded events with an explicit target simulation frame
- haptic commands are **not** synchronized because they are local side effects and do not affect gameplay

### Frame-boundary keyboard visibility

Keyboard bytes are visible only at frame boundaries. This applies to both:

- local host keyboard input
- remote keyboard bytes received over netplay

That rule is deliberate. It prevents a game from seeing a keyboard byte in the middle of a frame on one peer but only at the next frame boundary on another peer. Even if game code polls the dongle multiple times in one frame, the visible queue content is stable for that whole frame.

The practical behavior is:

1. host key activity is converted into keyboard bytes
2. those bytes are staged for a specific simulation frame
3. at the start of that frame, the bytes become visible in the emulated dongle queue
4. the game may then consume them any number of times during that frame

This is less cycle-accurate than allowing immediate mid-frame visibility, but it is much safer for deterministic rollback.

### Why decoded events are used instead of raw bus replay

Raw multitap or UzeBus edge traffic is not synchronized across the network. Instead, netplay exchanges the meaningfully decoded results:

- tap select
- tap probe
- keyboard bytes
- packet state

That keeps the protocol smaller, easier to validate, and easier to replay during rollback resimulation.

### Current limits

- full packet-seat netplay is extended to 8 logical seats so both P1 and P2 can each have a 4-slot tap
- keyboard bytes are deterministic at frame boundaries
- remote haptics are intentionally omitted
- raw generic UzeBus traffic is not yet treated as a completely general synchronized device channel

## Tap and selected-slot device events

CUzeBox does not synchronize raw controller-line activity for UzeTap or selected-slot UzeBus devices. Instead, netplay synchronizes decoded events at deterministic frame boundaries:

- tap select (`TAP_SELECT`)
- tap probe-next-read (`TAP_PROBE`)
- keyboard byte injection (`KBD_BYTES`)

These events are stamped to a target simulation frame and injected at frame start during live simulation and rollback resimulation. This keeps tap-selected slot state, probe visibility, and keyboard queue visibility deterministic across peers.

Remote haptic events are intentionally not synchronized because they do not affect gameplay state; haptics remain local-only side effects.


## Topology matching

Netplay now requires both peers to agree on the effective input topology used for emulation:

- tap present on P1/P2
- active slot on each port
- slot-to-virtual-device assignment
- virtual-device emulation type/options/packet defaults

Host bindings do not need to match, but the emulated topology must. If the peers disagree, CUzeBox will reject the session as an input-topology mismatch.


## Input Trace Window

CUzeBox now includes an **INPUT TRACE** tool window. It records per-frame packet snapshots for all emulated seats plus applied decoded device events such as tap select/probe and keyboard byte visibility. This is intended for debugging topology, rollback timing, and keyboard/tap behavior without reading raw controller line traffic. The trace window now also supports basic **filtering** (frames/tap/keyboard, P1/P2) and **export** of either the filtered view or the full captured buffer to text files.
