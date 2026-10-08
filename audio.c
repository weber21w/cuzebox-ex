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

#include "audio.h"

#include <math.h>

#ifndef SDL_AUDIO_ALLOW_CHANNELS_CHANGE
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE 0
#endif

/* Uzebox's audio frequency (that is, as the emulator does it, the line
** frequency of NTSC signal, in hertz) */
#define AUDIO_LFREQ    15734U

/* Whole for the audio sample increment, bits */
#define AUDIO_INC_W    17U

/* Maximum audio callback chunk size in output samples. Runtime latency
** presets choose a smaller power-of-two value within this range. */
#ifndef __EMSCRIPTEN__
#define AUDIO_OUT_SIZE_MAX 4096U
#else
#define AUDIO_OUT_SIZE_MAX 2048U
#endif

/* Ring buffer size, must be a power of 2 (15.7KHz Uzebox samples). Keep it
** comfortably larger than the largest callback size to allow safe presets. */
#ifndef __EMSCRIPTEN__
#define AUDIO_BUF_SIZE 8192U
#else
#define AUDIO_BUF_SIZE 8192U
#endif

#define AUDIO_MONITOR_DELAY_CAP 256U
#define AUDIO_REVERB_DELAY_CAP  16384U

/* Ring buffer of samples */
static uint8 audio_buf[AUDIO_BUF_SIZE];
static auint audio_buf_r;
static auint audio_buf_w;

/* Audio device */
static auint audio_dev = 0U;

/* Device properties actually in use */
static auint audio_out_freq = 48000U;
static auint audio_out_format = AUDIO_U8;
static auint audio_out_channels = 1U;
static auint audio_out_samples = 2048U;
static auint audio_fill_target = ((2048U / 3U) * 2U);

/* Requested configuration */
static boole audio_out_s16_req = TRUE;
static auint audio_out_rate_mode = AUDIO_OUTRATE_48000;
static auint audio_latency_mode = AUDIO_LATENCY_NORMAL;
static auint audio_resampler_mode = AUDIO_RESAMPLER_LINEAR;
static boole audio_dcblock_isena = TRUE;
static auint audio_lowpass_mode = AUDIO_LOWPASS_LIGHT;
static auint audio_lowpass_quality = AUDIO_LOWPASS_QUALITY_HQ;
static auint audio_monitor_mode = AUDIO_MONITOR_MONO;
static auint audio_monitor_width = 100U;
static auint audio_reverb_mode = AUDIO_REVERB_OFF;
static auint audio_master_volume = 100U;

/* Optional web/debug oscilloscope output capture. This state is completely
** off the normal SDL callback path unless explicitly armed. */
static boole audio_scope_output_capture = FALSE;
static sint16 audio_scope_out_l[AUDIO_SCOPE_OUTPUT_CAP];
static sint16 audio_scope_out_r[AUDIO_SCOPE_OUTPUT_CAP];
static auint audio_scope_out_w = 0U;
static auint audio_scope_out_count = 0U;
static uint64_t audio_scope_frames_total = 0ULL;
static uint64_t audio_scope_rail_l = 0ULL;
static uint64_t audio_scope_rail_r = 0ULL;
static auint audio_scope_peak_l = 0U;
static auint audio_scope_peak_r = 0U;
static void audio_callback_scope(void* dummy, Uint8* stream, int len);

/* Calculated increment fraction, depends on hardware audio frequency */
static auint audio_inc_p = 0U;

/* Read pointer increment fraction for audio output (used to scale frequency) */
static auint audio_inc;

/* Current pointer fraction */
static auint audio_frac;

/* Previous remaining samples counts for averaging */
static auint audio_pbrem[31];

/* Previous remaining samples average count for differential correction */
static auint audio_pbrav;

/* First run after reset mark (to reset in sync) */
static boole audio_frun;

/* Frequency scaling state: enabled or disabled */
static boole audio_fs_isena = TRUE;

/* Filter state */
static sint32 audio_lp_state_q15 = 0;
static sint32 audio_hp_prev_in = 0;
static sint32 audio_hp_prev_out_q15 = 0;
static double audio_bq_b0 = 1.0;
static double audio_bq_b1 = 0.0;
static double audio_bq_b2 = 0.0;
static double audio_bq_a1 = 0.0;
static double audio_bq_a2 = 0.0;
static double audio_bq_z1 = 0.0;
static double audio_bq_z2 = 0.0;
static sint32 audio_monitor_delay[AUDIO_MONITOR_DELAY_CAP];
static auint  audio_monitor_delay_len = 8U;
static auint  audio_monitor_delay_pos = 0U;
static sint16 audio_reverb_delay[AUDIO_REVERB_DELAY_CAP];
static auint  audio_reverb_delay_pos = 0U;
static auint  audio_reverb_tap1 = 624U;
static auint  audio_reverb_tap2 = 1008U;
static auint  audio_reverb_tap3 = 1488U;
static auint  audio_reverb_tap4 = 2256U;

static auint audio_request_freq(void)
{
	switch (audio_out_rate_mode){
		case AUDIO_OUTRATE_44100: return 44100U;
		case AUDIO_OUTRATE_96000: return 96000U;
		case AUDIO_OUTRATE_48000:
		default:                  return 48000U;
	}
}

static auint audio_request_samples(void)
{
#ifndef __EMSCRIPTEN__
	switch (audio_latency_mode){
		case AUDIO_LATENCY_LOW:    return 1024U;
		case AUDIO_LATENCY_SAFE:   return 4096U;
		case AUDIO_LATENCY_NORMAL:
		default:                   return 2048U;
	}
#else
	switch (audio_latency_mode){
		case AUDIO_LATENCY_LOW:    return 512U;
		case AUDIO_LATENCY_SAFE:   return 2048U;
		case AUDIO_LATENCY_NORMAL:
		default:                   return 1024U;
	}
#endif
}

static auint audio_request_channels(void)
{
	if (audio_monitor_mode == AUDIO_MONITOR_MONO){
		return 1U;
	}
	return 2U;
}

static auint audio_calc_fill_target(auint out_samples)
{
#ifndef __EMSCRIPTEN__
	auint fill = ((out_samples / 3U) * 2U);
#else
	auint fill = ((out_samples / 3U) * 8U);
#endif
	if (fill >= AUDIO_BUF_SIZE){
		fill = AUDIO_BUF_SIZE - 1U;
	}
	if (fill == 0U){
		fill = 1U;
	}
	return fill;
}

static boole audio_format_supported(auint fmt)
{
	return ((fmt == AUDIO_U8) || (fmt == AUDIO_S16SYS));
}

static auint audio_clamp_u8(asint v)
{
	if (v <   0){ return   0U; }
	if (v > 255){ return 255U; }
	return (auint)v;
}

static sint16 audio_u8_to_s16(asint v)
{
	v = (asint)audio_clamp_u8(v);
	return (sint16)((v - 128) << 8);
}

static sint32 audio_clip_s32(sint32 v, sint32 lo, sint32 hi)
{
	if (v < lo){ return lo; }
	if (v > hi){ return hi; }
	return v;
}

static auint audio_lowpass_coeff_q15(void)
{
	switch (audio_lowpass_mode){
		case AUDIO_LOWPASS_LIGHT:  return 11469U; /* ~0.35 */
		case AUDIO_LOWPASS_MEDIUM: return  7864U; /* ~0.24 */
		case AUDIO_LOWPASS_STRONG: return  5243U; /* ~0.16 */
		default:                   return     0U;
	}
}

static double audio_lowpass_cutoff_hz(void)
{
	switch (audio_lowpass_mode){
		case AUDIO_LOWPASS_LIGHT:  return 7000.0;
		case AUDIO_LOWPASS_MEDIUM: return 6000.0;
		case AUDIO_LOWPASS_STRONG: return 5000.0;
		default:                   return 0.0;
	}
}

static auint audio_reverb_delay_ms(auint ms)
{
	uint64_t v = ((uint64_t)((audio_out_freq != 0U) ? audio_out_freq : audio_request_freq()) * (uint64_t)ms) / 1000ULL;
	if (v < 1ULL){
		v = 1ULL;
	}
	if (v >= (uint64_t)AUDIO_REVERB_DELAY_CAP){
		v = (uint64_t)AUDIO_REVERB_DELAY_CAP - 1ULL;
	}
	return (auint)v;
}

static auint audio_reverb_mix_percent(void)
{
	switch (audio_reverb_mode){
		case AUDIO_REVERB_LIGHT:  return 14U;
		case AUDIO_REVERB_MEDIUM: return 22U;
		case AUDIO_REVERB_STRONG: return 32U;
		default:                  return 0U;
	}
}

static auint audio_reverb_feedback_percent(void)
{
	switch (audio_reverb_mode){
		case AUDIO_REVERB_LIGHT:  return 22U;
		case AUDIO_REVERB_MEDIUM: return 30U;
		case AUDIO_REVERB_STRONG: return 40U;
		default:                  return 0U;
	}
}

static void audio_biquad_recalc(void)
{
	double fs = (audio_out_freq != 0U) ? (double)audio_out_freq : (double)audio_request_freq();
	double fc = audio_lowpass_cutoff_hz();
	double q = 0.7071067811865476;
	double w0;
	double cw;
	double sw;
	double alpha;
	double a0;

	audio_bq_b0 = 1.0;
	audio_bq_b1 = 0.0;
	audio_bq_b2 = 0.0;
	audio_bq_a1 = 0.0;
	audio_bq_a2 = 0.0;
	audio_bq_z1 = 0.0;
	audio_bq_z2 = 0.0;

	if ((audio_lowpass_mode == AUDIO_LOWPASS_OFF) ||
	    (audio_lowpass_quality != AUDIO_LOWPASS_QUALITY_HQ) ||
	    (fs <= 1.0) ||
	    (fc <= 0.0)){
		return;
	}
	if (fc > (fs * 0.45)){
		fc = fs * 0.45;
	}
	w0 = (2.0 * 3.14159265358979323846 * fc) / fs;
	cw = cos(w0);
	sw = sin(w0);
	alpha = sw / (2.0 * q);
	a0 = 1.0 + alpha;
	audio_bq_b0 = ((1.0 - cw) * 0.5) / a0;
	audio_bq_b1 = (1.0 - cw) / a0;
	audio_bq_b2 = audio_bq_b0;
	audio_bq_a1 = (-2.0 * cw) / a0;
	audio_bq_a2 = (1.0 - alpha) / a0;
}

static void audio_monitor_reset(void)
{
	memset(audio_monitor_delay, 0, sizeof(audio_monitor_delay));
	audio_monitor_delay_pos = 0U;
	audio_monitor_delay_len = (audio_out_freq != 0U) ? (audio_out_freq / 750U) : 64U;
	if (audio_monitor_delay_len < 8U){
		audio_monitor_delay_len = 8U;
	}
	if (audio_monitor_delay_len >= AUDIO_MONITOR_DELAY_CAP){
		audio_monitor_delay_len = AUDIO_MONITOR_DELAY_CAP - 1U;
	}
}

static void audio_reverb_reset(void)
{
	memset(audio_reverb_delay, 0, sizeof(audio_reverb_delay));
	audio_reverb_delay_pos = 0U;
	audio_reverb_tap1 = audio_reverb_delay_ms(13U);
	audio_reverb_tap2 = audio_reverb_delay_ms(21U);
	audio_reverb_tap3 = audio_reverb_delay_ms(31U);
	audio_reverb_tap4 = audio_reverb_delay_ms(47U);
}

static void audio_reverb_process(sint32 dry_l, sint32 dry_r, sint32 dry_mono, sint32* out_l, sint32* out_r)
{
	if ((out_l == NULL) || (out_r == NULL)){
		return;
	}
	if (audio_reverb_mode == AUDIO_REVERB_OFF){
		*out_l = dry_l;
		*out_r = dry_r;
		return;
	}else{
		auint pos = audio_reverb_delay_pos;
		auint cap = AUDIO_REVERB_DELAY_CAP;
		sint32 t1 = (sint32)audio_reverb_delay[(pos + cap - audio_reverb_tap1) & (cap - 1U)];
		sint32 t2 = (sint32)audio_reverb_delay[(pos + cap - audio_reverb_tap2) & (cap - 1U)];
		sint32 t3 = (sint32)audio_reverb_delay[(pos + cap - audio_reverb_tap3) & (cap - 1U)];
		sint32 t4 = (sint32)audio_reverb_delay[(pos + cap - audio_reverb_tap4) & (cap - 1U)];
		auint mix = audio_reverb_mix_percent();
		auint fb = audio_reverb_feedback_percent();
		sint32 wet_l = ((3 * t1) + (2 * t3) + t4) / 6;
		sint32 wet_r = ((3 * t2) + (2 * t4) + t3) / 6;
		sint32 fbavg = (t1 + t2 + t3 + t4) / 4;
		sint32 store = audio_clip_s32(dry_mono + (sint32)(((int64_t)fbavg * (int64_t)fb) / 100LL), -32768, 32767);
		audio_reverb_delay[pos] = (sint16)store;
		audio_reverb_delay_pos = (pos + 1U) & (cap - 1U);
		*out_l = audio_clip_s32((sint32)((((int64_t)dry_l * (int64_t)(100U - mix)) + ((int64_t)wet_l * (int64_t)mix)) / 100LL), -32768, 32767);
		*out_r = audio_clip_s32((sint32)((((int64_t)dry_r * (int64_t)(100U - mix)) + ((int64_t)wet_r * (int64_t)mix)) / 100LL), -32768, 32767);
	}
}

static void audio_filter_reset(void)
{
	audio_lp_state_q15 = 0;
	audio_hp_prev_in = 0;
	audio_hp_prev_out_q15 = 0;
	audio_biquad_recalc();
	audio_monitor_reset();
	audio_reverb_reset();
}

static sint32 audio_filter_sample(sint32 sample)
{
	sint32 y = sample;

	if (audio_dcblock_isena){
		const sint32 r_q15 = 32604; /* ~0.995 */
		sint32 y_q15 = ((sample - audio_hp_prev_in) << 15) + (sint32)(((int64_t)audio_hp_prev_out_q15 * (int64_t)r_q15) >> 15);
		audio_hp_prev_in = sample;
		audio_hp_prev_out_q15 = y_q15;
		y = y_q15 >> 15;
	}

	if (audio_lowpass_mode != AUDIO_LOWPASS_OFF){
		if (audio_lowpass_quality == AUDIO_LOWPASS_QUALITY_HQ){
			double x = (double)y;
			double out = (audio_bq_b0 * x) + audio_bq_z1;
			audio_bq_z1 = (audio_bq_b1 * x) - (audio_bq_a1 * out) + audio_bq_z2;
			audio_bq_z2 = (audio_bq_b2 * x) - (audio_bq_a2 * out);
			y = (sint32)((out >= 0.0) ? (out + 0.5) : (out - 0.5));
		}else{
			auint a_q15 = audio_lowpass_coeff_q15();
			sint32 target_q15 = y << 15;
			audio_lp_state_q15 += (sint32)(((int64_t)(target_q15 - audio_lp_state_q15) * (int64_t)a_q15) >> 15);
			y = audio_lp_state_q15 >> 15;
		}
	}else{
		audio_lp_state_q15 = y << 15;
	}

	return audio_clip_s32(y, -32768, 32767);
}

static uint8 audio_sample_rel(asint rel)
{
	auint avail = (audio_buf_w - audio_buf_r) & (AUDIO_BUF_SIZE - 1U);
	if (avail == 0U){
		return audio_buf[(audio_buf_r - 1U) & (AUDIO_BUF_SIZE - 1U)];
	}
	if (rel < 0){
		return audio_buf[(audio_buf_r - 1U) & (AUDIO_BUF_SIZE - 1U)];
	}
	if ((auint)rel >= avail){
		return audio_buf[(audio_buf_r + avail - 1U) & (AUDIO_BUF_SIZE - 1U)];
	}
	return audio_buf[(audio_buf_r + (auint)rel) & (AUDIO_BUF_SIZE - 1U)];
}

static asint audio_mix_sample(void)
{
	auint frac = audio_frac;
	double t = (double)frac / (double)(1U << AUDIO_INC_W);
	asint p0;
	asint p1;
	asint p2;
	asint p3;
	double y;

	p0 = (asint)audio_sample_rel(-1);
	p1 = (asint)audio_sample_rel(0);
	if (audio_resampler_mode == AUDIO_RESAMPLER_HOLD){
		return p1;
	}
	p2 = (asint)audio_sample_rel(1);
	if (audio_resampler_mode == AUDIO_RESAMPLER_LINEAR){
		y = ((double)p1 * (1.0 - t)) + ((double)p2 * t);
		return (asint)(y + 0.5);
	}
	p3 = (asint)audio_sample_rel(2);
	/* Catmull-Rom 4-point cubic */
	y = 0.5 * (
		(2.0 * (double)p1) +
		((double)(-p0 + p2) * t) +
		((double)((2 * p0) - (5 * p1) + (4 * p2) - p3) * t * t) +
		((double)(-p0 + (3 * p1) - (3 * p2) + p3) * t * t * t));
	return (asint)(y + 0.5);
}

static void audio_advance_frac(auint xinc)
{
	if (audio_buf_w == audio_buf_r){
		return;
	}
	audio_frac += xinc;
	while (audio_frac >= (1U << AUDIO_INC_W)){
		audio_frac -= (1U << AUDIO_INC_W);
		audio_buf_r = (audio_buf_r + 1U) & (AUDIO_BUF_SIZE - 1U);
		if (audio_buf_w == audio_buf_r){
			break;
		}
	}
}

static auint audio_scope_abs_s16(sint16 v)
{
	if (v == (sint16)-32768){ return 32768U; }
	return (v < 0) ? (auint)(-v) : (auint)v;
}

static void audio_scope_capture_stream(Uint8 const* stream, int len)
{
	auint i;
	auint frames;
	auint fmt = audio_out_format;
	auint channels = audio_out_channels;
	boole out_s16 = (fmt == AUDIO_S16SYS);
	sint16 const* in16 = (sint16 const*)stream;

	if ((stream == NULL) || (len <= 0)){ return; }
	frames = (auint)len;
	if (out_s16){ frames >>= 1; }
	if (channels > 1U){ frames /= channels; }
	for (i = 0U; i < frames; i++){
		auint base = i * channels;
		sint16 left;
		sint16 right;
		boole rail_l;
		boole rail_r;
		auint al;
		auint ar;
		if (out_s16){
			left = in16[base];
			right = (channels >= 2U) ? in16[base + 1U] : left;
			rail_l = ((left == (sint16)-32768) || (left == (sint16)32767));
			rail_r = ((right == (sint16)-32768) || (right == (sint16)32767));
		}else{
			auint lu = stream[base];
			auint ru = (channels >= 2U) ? stream[base + 1U] : lu;
			left = (sint16)(((asint)lu - 128) << 8);
			right = (sint16)(((asint)ru - 128) << 8);
			rail_l = ((lu == 0U) || (lu == 255U));
			rail_r = ((ru == 0U) || (ru == 255U));
		}
		audio_scope_out_l[audio_scope_out_w] = left;
		audio_scope_out_r[audio_scope_out_w] = right;
		audio_scope_out_w = (audio_scope_out_w + 1U) & (AUDIO_SCOPE_OUTPUT_CAP - 1U);
		if (audio_scope_out_count < AUDIO_SCOPE_OUTPUT_CAP){ audio_scope_out_count++; }
		audio_scope_frames_total++;
		if (rail_l){ audio_scope_rail_l++; }
		if (rail_r){ audio_scope_rail_r++; }
		al = audio_scope_abs_s16(left);
		ar = audio_scope_abs_s16(right);
		if (al > audio_scope_peak_l){ audio_scope_peak_l = al; }
		if (ar > audio_scope_peak_r){ audio_scope_peak_r = ar; }
	}
}

/*
** Audio callback
*/
void audio_callback(void* dummy, Uint8* stream, int len)
{
#ifdef HEADLESS
	return;
#endif
	auint i;
	auint frames;
	auint brem;
	auint brav;
	auint bras;
	auint xinc;
	auint fmt = audio_out_format;
	auint channels = audio_out_channels;
	boole out_s16 = (fmt == AUDIO_S16SYS);
	sint16* out16 = (sint16*)stream;

	(void)dummy;

	if (audio_frun){
		audio_buf_r = 0U;
		audio_buf_w = audio_fill_target;
		audio_inc   = audio_inc_p;
		audio_frac  = 0U;
		audio_pbrav = audio_buf_w;
		for (i = 0U; i < 31U; i++){
			audio_pbrem[i] = audio_buf_w;
		}
		memset(&(audio_buf[0]), 0x80U, sizeof(audio_buf));
		audio_frun  = FALSE;
	}

	brem = (audio_buf_w - audio_buf_r) & (AUDIO_BUF_SIZE - 1U);
	brav = brem;
	bras = brem;
	for (i = 0U; i < 31U; i++){
		brav += audio_pbrem[i];
	}
	for (i = 0U; i < 7U; i++){
		bras += audio_pbrem[i];
	}
	brav = (brav + 15U) >> 5;
	bras = (bras +  3U) >> 3;

	if (audio_fs_isena){
		if (brav < audio_fill_target){
			if (audio_inc > (((audio_inc_p) * 50U) / 100U)){
				audio_inc --;
			}
		}else if (brav > audio_fill_target){
			if (audio_inc < (((audio_inc_p) * 105U) / 100U)){
				audio_inc ++;
			}
		}
		if (brav < audio_pbrav){
			audio_inc --;
		}else if (brav > audio_pbrav){
			audio_inc ++;
		}
	}

	xinc = audio_inc;
	if (bras < audio_fill_target){
		xinc -= (audio_fill_target - bras) >> 3;
		if (bras < ((audio_fill_target * 7U) / 8U)){
			xinc -= (((audio_fill_target * 7U) / 8U) - bras) >> 1;
		}
	}else{
		xinc += (bras - audio_fill_target) >> 3;
		if (bras > ((audio_fill_target * 9U) / 8U)){
			xinc += (bras - ((audio_fill_target * 9U) / 8U));
		}
		if (bras > ((audio_fill_target * 3U) / 2U)){
			xinc += (bras - ((audio_fill_target * 3U) / 2U)) << 3;
		}
	}

	frames = (auint)len;
	if (out_s16){
		frames >>= 1;
	}
	if (channels > 1U){
		frames /= channels;
	}

	for (i = 0U; i < frames; i++){
		auint base = i * channels;
		asint smp = audio_mix_sample();
		sint32 mono = audio_filter_sample((sint32)audio_u8_to_s16(smp));
		sint32 left = mono;
		sint32 right = mono;

		if ((channels >= 2U) && (audio_monitor_mode == AUDIO_MONITOR_WIDE)){
			sint32 delayed = audio_monitor_delay[audio_monitor_delay_pos];
			sint32 side = (sint32)(((int64_t)(mono - delayed) * (int64_t)audio_monitor_width) / 300LL);
			audio_monitor_delay[audio_monitor_delay_pos] = mono;
			audio_monitor_delay_pos ++;
			if (audio_monitor_delay_pos >= audio_monitor_delay_len){
				audio_monitor_delay_pos = 0U;
			}
			left = audio_clip_s32(mono + side, -32768, 32767);
			right = audio_clip_s32(mono - side, -32768, 32767);
		}

		audio_reverb_process(left, right, mono, &left, &right);

		left = audio_clip_s32((sint32)(((int64_t)left * (int64_t)audio_master_volume) / 100), -32768, 32767);
		right = audio_clip_s32((sint32)(((int64_t)right * (int64_t)audio_master_volume) / 100), -32768, 32767);

		if (out_s16){
			if (channels >= 2U){
				out16[base] = (sint16)left;
				out16[base + 1U] = (sint16)right;
			}else{
				out16[base] = (sint16)left;
			}
		}else{
			if (channels >= 2U){
				stream[base] = (Uint8)audio_clamp_u8((left >> 8) + 128);
				stream[base + 1U] = (Uint8)audio_clamp_u8((right >> 8) + 128);
			}else{
				stream[base] = (Uint8)audio_clamp_u8((left >> 8) + 128);
			}
		}
		audio_advance_frac(xinc);
	}

	for (i = 30U; i != 0U; i--){
		audio_pbrem[i] = audio_pbrem[i - 1U];
	}
	audio_pbrem[0] = brem;
	audio_pbrav = brav;
}

#ifndef USE_SDL1
#ifdef  TARGET_WINDOWS_MINGW
static void audio_wasapi_workaround(void)
{
	auint       didx   = SDL_GetNumAudioDrivers();
	boole       hasds  = FALSE;
	const char* drvname;
	const char* dsound = "directsound";
	auint       dslen  = strlen(dsound);

	while (didx != 0U){
		didx --;
		drvname = SDL_GetAudioDriver(didx);
		if (drvname != NULL){
			if (strncmp(dsound, drvname, dslen) == 0){
				hasds = TRUE;
			}
		}
	}

	if (hasds){
		SDL_setenv("SDL_AUDIODRIVER", dsound, 0);
	}
}
#endif
#endif

static boole audio_open_requested(boole want_s16)
{
	SDL_AudioSpec desired;
#ifdef USE_SDL1
	SDL_AudioSpec have;
#else
	SDL_AudioSpec have;
#endif

	memset(&desired, 0, sizeof(desired));
	memset(&have, 0, sizeof(have));
	desired.freq     = (int)audio_request_freq();
	desired.format   = want_s16 ? AUDIO_S16SYS : AUDIO_U8;
	desired.callback = audio_scope_output_capture ? audio_callback_scope : audio_callback;
	desired.channels = (Uint8)audio_request_channels();
	desired.samples  = (Uint16)audio_request_samples();

#ifdef USE_SDL1
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	audio_dev = 1U;
	if (SDL_OpenAudio(&desired, &have) < 0){
		if (desired.channels > 1U){
			desired.channels = 1U;
			if (SDL_OpenAudio(&desired, &have) < 0){
				audio_dev = 0U;
				return FALSE;
			}
		}else{
			audio_dev = 0U;
			return FALSE;
		}
	}
#else
#ifdef TARGET_WINDOWS_MINGW
	audio_wasapi_workaround();
#endif
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	audio_dev = SDL_OpenAudioDevice(NULL, 0, &desired, &have,
		SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
		SDL_AUDIO_ALLOW_FORMAT_CHANGE |
		SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if (audio_dev == 0U){
		if (desired.channels > 1U){
			desired.channels = 1U;
			audio_dev = SDL_OpenAudioDevice(NULL, 0, &desired, &have,
				SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
				SDL_AUDIO_ALLOW_FORMAT_CHANGE |
				SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
			if (audio_dev == 0U){
				return FALSE;
			}
		}else{
			return FALSE;
		}
	}
#endif

	if (!audio_format_supported(have.format)){
#ifdef USE_SDL1
		SDL_CloseAudio();
#else
		SDL_CloseAudioDevice(audio_dev);
#endif
		audio_dev = 0U;
		return FALSE;
	}

	audio_out_freq = (have.freq != 0) ? (auint)have.freq : audio_request_freq();
	audio_out_format = (have.format != 0) ? (auint)have.format : (want_s16 ? AUDIO_S16SYS : AUDIO_U8);
	audio_out_channels = (have.channels != 0U) ? (auint)have.channels : audio_request_channels();
	if ((audio_out_channels != 1U) && (audio_out_channels != 2U)){
		audio_out_channels = audio_request_channels();
	}
	audio_out_samples = (have.samples != 0U) ? (auint)have.samples : audio_request_samples();
	if (audio_out_samples > AUDIO_OUT_SIZE_MAX){
		audio_out_samples = AUDIO_OUT_SIZE_MAX;
	}
	audio_fill_target = audio_calc_fill_target(audio_out_samples);
	audio_inc_p = (((auint)(AUDIO_LFREQ) << AUDIO_INC_W) / audio_out_freq);
	audio_biquad_recalc();
	audio_monitor_reset();
	audio_reverb_reset();
	return TRUE;
}

/*
** Attempts to initialize audio. If it returns false, no sound will be
** generated.
*/
boole audio_init(void)
{
#ifdef HEADLESS
	return 0U;
#endif
	if (audio_dev != 0U){
		return TRUE;
	}
	audio_frun = TRUE;
	audio_filter_reset();
	if (!audio_open_requested(audio_out_s16_req)){
		if (audio_out_s16_req){
			if (!audio_open_requested(FALSE)){
				return FALSE;
			}
		}else{
			return FALSE;
		}
	}
#ifdef USE_SDL1
	SDL_PauseAudio(1);
#else
	SDL_PauseAudioDevice(audio_dev, 1);
#endif
	return TRUE;
}

/*
** Resets audio, call it along with the start of emulation.
*/
void  audio_reset(void)
{
	audio_frun = TRUE;
	audio_filter_reset();
#ifdef USE_SDL1
	if (audio_dev != 0U){ SDL_PauseAudio(0); }
#else
	if (audio_dev != 0U){ SDL_PauseAudioDevice(audio_dev, 0); }
#endif
}

/*
** Drops any queued audio immediately. Netplay rollback and stall handling use
** this because once samples are handed to the host audio callback we no longer
** know exactly which emulated frames have already been consumed. The least
** wrong behavior is to discard emulator-side backlog and resume from the
** corrected state rather than letting stale music / SFX drain out first.
*/
void  audio_flush(void)
{
	auint i;

	audio_filter_reset();

#ifdef USE_SDL1
	if (audio_dev != 0U){ SDL_LockAudio(); }
#else
	if (audio_dev != 0U){ SDL_LockAudioDevice(audio_dev); }
#endif

	audio_buf_r = 0U;
	audio_buf_w = audio_fill_target;
	audio_inc   = audio_inc_p;
	audio_frac  = 0U;
	audio_pbrav = audio_buf_w;
	for (i = 0U; i < 31U; i++){
		audio_pbrem[i] = audio_buf_w;
	}
	memset(&(audio_buf[0]), 0x80U, sizeof(audio_buf));
	audio_frun = FALSE;

#ifdef USE_SDL1
	if (audio_dev != 0U){ SDL_UnlockAudio(); }
#else
	if (audio_dev != 0U){ SDL_UnlockAudioDevice(audio_dev); }
#endif
}

/*
** Tears down audio (if initialization succeed)
*/
void  audio_quit(void)
{
	if (audio_dev != 0U){
#ifdef USE_SDL1
		SDL_CloseAudio();
#else
		SDL_CloseAudioDevice(audio_dev);
#endif
		audio_dev = 0U;
	}
}

/* Instrumented callback used only while the web/audio scope is explicitly
** armed. The normal callback above remains byte-for-byte on the inactive path. */
static void audio_callback_scope(void* dummy, Uint8* stream, int len)
{
	audio_callback(dummy, stream, len);
	audio_scope_capture_stream(stream, len);
}

static void audio_reconfigure(void)
{
	boole was_open = (audio_dev != 0U);
	if (!was_open){
		return;
	}
	audio_quit();
	if (audio_init()){
		audio_reset();
	}
}

static void audio_scope_lock(void)
{
#ifndef HEADLESS
#ifdef USE_SDL1
	if (audio_dev != 0U){ SDL_LockAudio(); }
#else
	if (audio_dev != 0U){ SDL_LockAudioDevice(audio_dev); }
#endif
#endif
}

static void audio_scope_unlock(void)
{
#ifndef HEADLESS
#ifdef USE_SDL1
	if (audio_dev != 0U){ SDL_UnlockAudio(); }
#else
	if (audio_dev != 0U){ SDL_UnlockAudioDevice(audio_dev); }
#endif
#endif
}

static void audio_scope_clear_unlocked(void)
{
	audio_scope_out_w = 0U;
	audio_scope_out_count = 0U;
	audio_scope_frames_total = 0ULL;
	audio_scope_rail_l = 0ULL;
	audio_scope_rail_r = 0ULL;
	audio_scope_peak_l = 0U;
	audio_scope_peak_r = 0U;
	memset(audio_scope_out_l, 0, sizeof(audio_scope_out_l));
	memset(audio_scope_out_r, 0, sizeof(audio_scope_out_r));
}

void audio_scope_clear(void)
{
	audio_scope_lock();
	audio_scope_clear_unlocked();
	audio_scope_unlock();
}

void audio_scope_output_enable(boole enable)
{
	enable = enable ? TRUE : FALSE;
	if (audio_scope_output_capture == enable){ return; }
	audio_scope_output_capture = enable;
	audio_scope_clear();
	/* SDL fixes the callback pointer when a device is opened. Reopen only when
	** the user explicitly arms/disarms capture. In normal operation the device
	** points directly at audio_callback(), with no scope branch at all. */
	audio_reconfigure();
	audio_scope_clear();
}

boole audio_scope_output_enabled(void)
{
	return audio_scope_output_capture;
}

void audio_scope_get_status(audio_scope_status_t* out)
{
	if (out == NULL){ return; }
	audio_scope_lock();
	out->output_capture_enabled = audio_scope_output_capture;
	out->source_rate = audio_getfreq();
	out->output_rate = audio_out_freq;
	out->output_format = audio_out_format;
	out->output_channels = audio_out_channels;
	out->output_count = audio_scope_out_count;
	out->output_frames_total = audio_scope_frames_total;
	out->output_rail_left = audio_scope_rail_l;
	out->output_rail_right = audio_scope_rail_r;
	out->output_peak_left = audio_scope_peak_l;
	out->output_peak_right = audio_scope_peak_r;
	audio_scope_unlock();
}

auint audio_scope_copy_source(uint8* out, auint count)
{
	auint i;
	auint start;
	if ((out == NULL) || (count == 0U)){ return 0U; }
	if (count > AUDIO_BUF_SIZE){ count = AUDIO_BUF_SIZE; }
	audio_scope_lock();
	start = (audio_buf_w - count) & (AUDIO_BUF_SIZE - 1U);
	for (i = 0U; i < count; i++){
		out[i] = audio_buf[(start + i) & (AUDIO_BUF_SIZE - 1U)];
	}
	audio_scope_unlock();
	return count;
}

auint audio_scope_copy_output(sint16* left, sint16* right, auint count)
{
	auint i;
	auint start;
	if ((left == NULL) || (right == NULL) || (count == 0U)){ return 0U; }
	audio_scope_lock();
	if (count > audio_scope_out_count){ count = audio_scope_out_count; }
	start = (audio_scope_out_w - count) & (AUDIO_SCOPE_OUTPUT_CAP - 1U);
	for (i = 0U; i < count; i++){
		auint p = (start + i) & (AUDIO_SCOPE_OUTPUT_CAP - 1U);
		left[i] = audio_scope_out_l[p];
		right[i] = audio_scope_out_r[p];
	}
	audio_scope_unlock();
	return count;
}

/*
** Send a frame (unsigned 8 bit samples) to the audio device. If NULL is
** passed, then silence will be added.
*/
void  audio_sendframe(uint8 const* samples, auint len)
{
	auint i;
	auint brem = (audio_buf_r - audio_buf_w) & (AUDIO_BUF_SIZE - 1U);

	if ((brem != 0U) && (brem < len)){
		return;
	}

	if (samples != NULL){
		for (i = 0U; i < len; i++){
			audio_buf[audio_buf_w] = samples[i];
			audio_buf_w = (audio_buf_w + 1U) & (AUDIO_BUF_SIZE - 1U);
		}
	}else{
		for (i = 0U; i < len; i++){
			audio_buf[audio_buf_w] = 0x80U;
			audio_buf_w = (audio_buf_w + 1U) & (AUDIO_BUF_SIZE - 1U);
		}
	}
}

/*
** Returns current effective Uzebox source frequency.
*/
auint audio_getfreq(void)
{
	auint inc = audio_inc;
	if (inc == 0U){
		inc = audio_inc_p;
	}
	return (auint)((((uint64_t)audio_out_freq) * (uint64_t)inc) >> AUDIO_INC_W);
}

auint audio_get_output_freq(void)
{
	return audio_out_freq;
}

auint audio_get_output_format(void)
{
	return audio_out_format;
}

auint audio_get_output_channels(void)
{
	return audio_out_channels;
}

auint audio_get_output_samples(void)
{
	return audio_out_samples;
}

void audio_output_rate_set(auint mode)
{
	if (mode > AUDIO_OUTRATE_96000){
		mode = AUDIO_OUTRATE_48000;
	}
	if (audio_out_rate_mode == mode){
		return;
	}
	audio_out_rate_mode = mode;
	audio_reconfigure();
}

auint audio_output_rate_get(void)
{
	return audio_out_rate_mode;
}

void audio_latency_set(auint mode)
{
	if (mode > AUDIO_LATENCY_SAFE){
		mode = AUDIO_LATENCY_NORMAL;
	}
	if (audio_latency_mode == mode){
		return;
	}
	audio_latency_mode = mode;
	audio_reconfigure();
}

auint audio_latency_get(void)
{
	return audio_latency_mode;
}

void audio_output_s16_ena(boole ena)
{
	boole want = ena ? TRUE : FALSE;
	if (audio_out_s16_req == want){
		return;
	}
	audio_out_s16_req = want;
	audio_reconfigure();
}

boole audio_output_s16_get(void)
{
	return audio_out_s16_req;
}

void audio_resampler_set(auint mode)
{
	if (mode > AUDIO_RESAMPLER_CUBIC){
		mode = AUDIO_RESAMPLER_LINEAR;
	}
	audio_resampler_mode = mode;
}

auint audio_resampler_get(void)
{
	return audio_resampler_mode;
}

void audio_dcblock_ena(boole ena)
{
	audio_dcblock_isena = ena ? TRUE : FALSE;
	audio_filter_reset();
}

boole audio_dcblock_get(void)
{
	return audio_dcblock_isena;
}

void audio_lowpass_set(auint mode)
{
	if (mode > AUDIO_LOWPASS_STRONG){
		mode = AUDIO_LOWPASS_LIGHT;
	}
	audio_lowpass_mode = mode;
	audio_filter_reset();
}

auint audio_lowpass_get(void)
{
	return audio_lowpass_mode;
}

void audio_lowpass_quality_set(auint mode)
{
	if (mode > AUDIO_LOWPASS_QUALITY_HQ){
		mode = AUDIO_LOWPASS_QUALITY_HQ;
	}
	if (audio_lowpass_quality == mode){
		return;
	}
	audio_lowpass_quality = mode;
	audio_filter_reset();
}

auint audio_lowpass_quality_get(void)
{
	return audio_lowpass_quality;
}

void audio_monitor_mode_set(auint mode)
{
	if (mode > AUDIO_MONITOR_STEREO){
		mode = AUDIO_MONITOR_STEREO;
	}
	if (audio_monitor_mode == mode){
		return;
	}
	audio_monitor_mode = mode;
	audio_reconfigure();
}

auint audio_monitor_mode_get(void)
{
	return audio_monitor_mode;
}

void audio_monitor_width_set(auint percent)
{
	if (percent > 200U){
		percent = 200U;
	}
	audio_monitor_width = percent;
}

auint audio_monitor_width_get(void)
{
	return audio_monitor_width;
}

void audio_reverb_set(auint mode)
{
	if (mode > AUDIO_REVERB_STRONG){
		mode = AUDIO_REVERB_OFF;
	}
	if (audio_reverb_mode == mode){
		return;
	}
	audio_reverb_mode = mode;
	audio_reverb_reset();
}

auint audio_reverb_get(void)
{
	return audio_reverb_mode;
}

void audio_master_volume_set(auint percent)
{
	if (percent > 200U){
		percent = 200U;
	}
	audio_master_volume = percent;
}

auint audio_master_volume_get(void)
{
	return audio_master_volume;
}

/*
** Enables or disables frequency scaling. By default frequency scaling is
** enabled. When disabled, the long term PD controller is fixed at whatever
** frequency it determined last.
*/
void  audio_freqscale_ena(boole ena)
{
	audio_fs_isena = ena;
}
