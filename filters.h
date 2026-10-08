/* filters.h */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	FILTER_SCALE_NONE = 0,
	FILTER_SCALE_SCALE2X,
	FILTER_SCALE_HQ2X,
	FILTER_SCALE_XBR2X
} filter_scale_mode_t;

typedef enum {
	FILTER_PRE_NONE = 0,
	FILTER_PRE_SOFT_RGB,
	FILTER_PRE_SVIDEO,
	FILTER_PRE_COMPOSITE,
	FILTER_PRE_RF_MOD,
	FILTER_PRE_BLACK_WHITE
} filter_pre_mode_t;

typedef enum {
	FILTER_CRT_NONE = 0,
	FILTER_CRT_SCANLINES_25,
	FILTER_CRT_SCANLINES_37,
	FILTER_CRT_SCANLINES_50,
	FILTER_CRT_APERTURE_GRILLE,
	FILTER_CRT_SHADOW_MASK,
	FILTER_CRT_CURVATURE,
	FILTER_CRT_COMPOSITE,
	FILTER_CRT_COMPOSITE_SCANLINES,
	FILTER_CRT_GRILLE_SCANLINES,
	FILTER_CRT_MASK_SCANLINES,
	FILTER_CRT_BUMP_MAP,
	FILTER_CRT_LUCID,
	FILTER_CRT_HEATWAVE
} filter_crt_mode_t;

typedef struct {
	uint32_t pal32[256];
	uint16_t dist[256 * 256];
	uint8_t similar[256 * 256];
} filter_ctx_t;

typedef struct {
	filter_crt_mode_t mode;
	uint8_t scanline_dark;		/* 0..255, 255 = unchanged */
	uint8_t mask_strength;		/* 0..255 */
	uint8_t curvature_strength;	/* 0..255 */
	uint8_t composite_strength;	/* 0..255 */
	uint8_t vignette_strength;	/* 0..255 */
} filter_crt_params_t;

void filter_init(filter_ctx_t* ctx);

void filter_scale_2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx,
	filter_scale_mode_t mode);

void filter_apply_pre(
	uint32_t* dst, int w, int h, int pitch,
	filter_pre_mode_t mode,
	const filter_crt_params_t* params);

void filter_apply_crt(
	uint32_t* dst, int w, int h, int pitch,
	const filter_crt_params_t* params);

void filter_scale_and_crt_2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx,
	filter_scale_mode_t scale_mode,
	const filter_crt_params_t* crt_params);

#ifdef __cplusplus
}
#endif