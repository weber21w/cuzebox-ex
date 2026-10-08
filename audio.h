/*
 *  Audio output
 *
 *  Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"

#define AUDIO_RESAMPLER_HOLD   0U
#define AUDIO_RESAMPLER_LINEAR 1U
#define AUDIO_RESAMPLER_CUBIC  2U

#define AUDIO_LOWPASS_OFF      0U
#define AUDIO_LOWPASS_LIGHT    1U
#define AUDIO_LOWPASS_MEDIUM   2U
#define AUDIO_LOWPASS_STRONG   3U

#define AUDIO_OUTRATE_44100    0U
#define AUDIO_OUTRATE_48000    1U
#define AUDIO_OUTRATE_96000    2U

#define AUDIO_LATENCY_LOW      0U
#define AUDIO_LATENCY_NORMAL   1U
#define AUDIO_LATENCY_SAFE     2U

#define AUDIO_MONITOR_MONO     0U
#define AUDIO_MONITOR_STEREO   1U
#define AUDIO_MONITOR_WIDE     2U

#define AUDIO_LOWPASS_QUALITY_SIMPLE 0U
#define AUDIO_LOWPASS_QUALITY_HQ     1U

#define AUDIO_REVERB_OFF       0U
#define AUDIO_REVERB_LIGHT     1U
#define AUDIO_REVERB_MEDIUM    2U
#define AUDIO_REVERB_STRONG    3U

#define AUDIO_SCOPE_OUTPUT_CAP 8192U

typedef struct{
	boole output_capture_enabled;
	auint source_rate;
	auint output_rate;
	auint output_format;
	auint output_channels;
	auint output_count;
	uint64_t output_frames_total;
	uint64_t output_rail_left;
	uint64_t output_rail_right;
	auint output_peak_left;
	auint output_peak_right;
} audio_scope_status_t;

/*
** Attempts to initialize audio. If it returns false, no sound will be
** generated.
*/
boole audio_init(void);

/*
** Resets audio, call it along with the start of emulation.
*/
void  audio_reset(void);

/*
** Immediately discards any queued emulator-side audio and replaces it with a
** short silence lead-in. This is useful at rollback or stall boundaries where
** previously queued samples no longer match the corrected emulated state.
*/
void  audio_flush(void);

/*
** Tears down audio (if initialization succeed)
*/
void  audio_quit(void);

/*
** Send a frame (unsigned 8 bit samples) to the audio device. If NULL is
** passed, then silence will be added.
*/
void  audio_sendframe(uint8 const* samples, auint len);

/*
** Returns current effective Uzebox source frequency after any dynamic scaling.
*/
auint audio_getfreq(void);

/*
** Returns current actual device output frequency.
*/
auint audio_get_output_freq(void);

/*
** Returns current actual device output format.
*/
auint audio_get_output_format(void);

/*
** Returns current actual device output channels.
*/
auint audio_get_output_channels(void);

/*
** Returns the actual device callback chunk size in output samples.
*/
auint audio_get_output_samples(void);

/*
** Enables or disables the DC blocking high-pass filter.
*/
void  audio_dcblock_ena(boole ena);

/*
** Returns whether DC blocking is enabled.
*/
boole audio_dcblock_get(void);

/*
** Sets the active low-pass filter strength (OFF / LIGHT / MEDIUM / STRONG).
*/
void  audio_lowpass_set(auint mode);

/*
** Returns the active low-pass filter mode.
*/
auint audio_lowpass_get(void);

/*
** Sets the active low-pass implementation quality. SIMPLE uses the original
** one-pole smoothing stage, HQ uses a biquad low-pass.
*/
void  audio_lowpass_quality_set(auint mode);

/*
** Returns the active low-pass implementation quality.
*/
auint audio_lowpass_quality_get(void);

/*
** Sets the requested device output rate mode (44100 / 48000 / 96000).
** Applied by reopening the device if audio is already initialized.
*/
void  audio_output_rate_set(auint mode);

/*
** Returns the requested device output rate mode.
*/
auint audio_output_rate_get(void);

/*
** Sets the requested latency preset (LOW / NORMAL / SAFE). Applied by
** reopening the device if audio is already initialized.
*/
void  audio_latency_set(auint mode);

/*
** Returns the requested latency preset.
*/
auint audio_latency_get(void);

/*
** Enables or disables 16-bit signed output. The setting is applied by
** reopening the device if audio is already initialized.
*/
void  audio_output_s16_ena(boole ena);

/*
** Returns whether 16-bit signed output is requested.
*/
boole audio_output_s16_get(void);

/*
** Sets the active resampler mode (HOLD / LINEAR / CUBIC). The setting is
** applied immediately.
*/
void  audio_resampler_set(auint mode);

/*
** Returns the active resampler mode.
*/
auint audio_resampler_get(void);

/*
** Sets the monitor output mode (MONO / STEREO / WIDE). Changing the mode may
** reopen the output device to switch channel count.
*/
void  audio_monitor_mode_set(auint mode);

/*
** Returns the active monitor output mode.
*/
auint audio_monitor_mode_get(void);

/*
** Sets the widening amount used by WIDE monitor mode in percent (0..200).
** 100 matches the original default strength, larger values intensify the
** effect. Applied immediately.
*/
void  audio_monitor_width_set(auint percent);

/*
** Returns the active widening amount used by WIDE monitor mode.
*/
auint audio_monitor_width_get(void);

/*
** Sets the monitor reverb mode (OFF / LIGHT / MEDIUM / STRONG). This is a
** host-side listening effect only and does not affect emulation timing.
*/
void  audio_reverb_set(auint mode);

/*
** Returns the active monitor reverb mode.
*/
auint audio_reverb_get(void);

/*
** Sets the master output volume in percent (0..200). Applied immediately.
*/
void  audio_master_volume_set(auint percent);

/*
** Returns the master output volume in percent.
*/
auint audio_master_volume_get(void);

/*
** Enables or disables frequency scaling. By default frequency scaling is
** enabled. When disabled, the long term PD controller is fixed at whatever
** frequency it determined last.
*/
void  audio_freqscale_ena(boole ena);


/*
** Audio oscilloscope/debug capture. The normal audio callback is used
** unchanged while output capture is disabled; enabling output capture
** reopens the SDL device with an instrumented wrapper callback. Therefore
** there is no per-sample/per-callback scope overhead in the normal path.
** Source samples are read from the existing Uzebox audio ring and require no
** instrumentation at all.
*/
void  audio_scope_output_enable(boole enable);
boole audio_scope_output_enabled(void);
void  audio_scope_clear(void);
void  audio_scope_get_status(audio_scope_status_t* out);
auint audio_scope_copy_source(uint8* out, auint count);
auint audio_scope_copy_output(sint16* left, sint16* right, auint count);

#endif
