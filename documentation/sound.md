# Sound Options

This page describes the host-side audio options exposed by CUzeBox. These settings affect how the emulator presents sound to the host audio device. They do **not** change the logic of the emulated AVR program.

## Quick start

A good default starting point is:

- `AudioOutputRate=1` (`48000`)
- `AudioLatency=1` (`normal`)
- `AudioResampler=1` (`linear`)
- `AudioDcBlock=1`
- `AudioLowPass=1` (`light`)
- `AudioLowPassQuality=1` (`hq`)
- `AudioMonitorMode=0` (`mono`)
- `AudioReverb=0` (`off`)
- `AudioMasterVolume=100`

For the lowest burden during automated testing or netplay experiments, use simpler settings and consider muting or minimizing host-side effects.

## Output rate

`AudioOutputRate` selects the requested device rate:

- `0` = 44100 Hz
- `1` = 48000 Hz
- `2` = 96000 Hz

Higher rates can sound cleaner, but they may cost more host CPU and can change how forgiving some output devices are.

## Latency preset

`AudioLatency` selects the requested callback/queue target:

- `0` = low
- `1` = normal
- `2` = safe

General guidance:

- `low` feels more immediate, but is more likely to underrun on weak systems
- `normal` is a good general default
- `safe` is useful when stability matters more than responsiveness

## Output format

`AudioS16` selects whether the host output device is requested as 16-bit signed output.

- `0` = 8-bit output path requested
- `1` = 16-bit output path requested

In practice, 16-bit output is usually the better choice for listening.

## Resampler

`AudioResampler`:

- `0` = hold
- `1` = linear
- `2` = cubic

Tradeoffs:

- `hold` is cheapest but roughest
- `linear` is a strong default
- `cubic` is smoother but costs more

## DC blocking

`AudioDcBlock` enables a high-pass stage to remove DC offset from the output.

That is usually a good idea and is enabled by default.

## Low-pass filter

`AudioLowPass`:

- `0` = off
- `1` = light
- `2` = medium
- `3` = strong

`AudioLowPassQuality`:

- `0` = simple
- `1` = HQ

The simple path uses a lighter-weight smoothing stage. The HQ path uses a biquad low-pass.

If you want cleaner high-frequency behavior and do not mind a bit more processing, use HQ.

## Monitor mode

`AudioMonitorMode`:

- `0` = mono
- `1` = stereo
- `2` = wide

`AudioMonitorWidth` adjusts the widening amount used by `wide` mode.

Important: monitor widening and reverb are host-side listening effects. They do not change the emulated program's timing or internal audio generation.

## Reverb

`AudioReverb`:

- `0` = off
- `1` = light
- `2` = medium
- `3` = strong

This is intended as a listening effect, not a hardware-accuracy feature.

## Master volume

`AudioMasterVolume` is a percentage.

Useful values:

- `100` = normal
- `0` = silent
- values above `100` increase gain and can clip sooner

## Frequency scaling

`AudioFreqScale` controls whether the emulator's long-term controller is allowed to adapt the effective source playback rate.

This can help keep host output steady. If you are doing very controlled analysis, it can be useful to note whether frequency scaling was enabled during the run.

## Rollback and netplay note

Audio is special during rollback and stall handling.

Queued host audio cannot be truly "rolled back" once it has already been handed to the output device. The current strategy is to flush local queued emulator-side audio at rollback/stall boundaries so stale sound does not continue to trail behind corrected gameplay state.

That means small discontinuities are possible, but it is generally better than letting old buffered audio play long after the visual/game state has been corrected.

## Recommended presets

### General use

- 48000 Hz
- normal latency
- linear resampler
- DC block on
- light low-pass, HQ
- mono monitor
- no reverb

### Lower host burden

- 44100 Hz
- normal or safe latency
- hold or linear resampler
- low-pass off or light
- mono monitor
- reverb off

### Listening-focused

- 48000 or 96000 Hz
- normal latency
- cubic resampler
- DC block on
- low-pass light or medium, HQ
- stereo or wide monitor
- optional light reverb

## Relevant files

- `audio.c`
- `audio.h`
- `configcfg.c`
- `main.c`

These are the main places to check when you are documenting a sound-related change or debugging an option that seems not to take effect.
