/* filters.c */
#include "filters.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline uint8_t filter_r8_from_332(uint8_t i){ return ((i >> 5) & 7) * 255 / 7; }
static inline uint8_t filter_g8_from_332(uint8_t i){ return ((i >> 2) & 7) * 255 / 7; }
static inline uint8_t filter_b8_from_332(uint8_t i){ return (i & 3) * 255 / 3; }

static inline int filter_clampi(int v, int lo, int hi){
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t filter_pack_argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b){
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static inline uint32_t filter_mix50(uint32_t a, uint32_t b){
	uint32_t ar = (a >> 16) & 255;
	uint32_t ag = (a >> 8) & 255;
	uint32_t ab = a & 255;
	uint32_t br = (b >> 16) & 255;
	uint32_t bg = (b >> 8) & 255;
	uint32_t bb = b & 255;
	return 0xFF000000u
		| (((ar + br) >> 1) << 16)
		| (((ag + bg) >> 1) << 8)
		| ((ab + bb) >> 1);
}

static inline uint32_t filter_mix75_25(uint32_t a, uint32_t b){
	uint32_t ar = (a >> 16) & 255;
	uint32_t ag = (a >> 8) & 255;
	uint32_t ab = a & 255;
	uint32_t br = (b >> 16) & 255;
	uint32_t bg = (b >> 8) & 255;
	uint32_t bb = b & 255;
	return 0xFF000000u
		| ((((ar * 3) + br) >> 2) << 16)
		| ((((ag * 3) + bg) >> 2) << 8)
		| (((ab * 3) + bb) >> 2);
}

static inline uint32_t filter_scale_chan(uint32_t v, uint32_t mul256){
	return (v * mul256) >> 8;
}

static inline uint32_t filter_mul_argb(uint32_t c, uint32_t mul256){
	uint32_t a = (c >> 24) & 255;
	uint32_t r = (c >> 16) & 255;
	uint32_t g = (c >> 8) & 255;
	uint32_t b = c & 255;
	r = filter_scale_chan(r, mul256);
	g = filter_scale_chan(g, mul256);
	b = filter_scale_chan(b, mul256);
	return filter_pack_argb(a, r, g, b);
}

#define FILTER_DIST(ctx,a,b)	((ctx)->dist[((((unsigned)(a)) & 255u) << 8) | (((unsigned)(b)) & 255u)])
#define FILTER_SIM(ctx,a,b)		((ctx)->similar[((((unsigned)(a)) & 255u) << 8) | (((unsigned)(b)) & 255u)])

void filter_init(filter_ctx_t* ctx){
	const int Wr = 26;
	const int Wg = 34;
	const int Wb = 10;
	const int similar_threshold = 18;

	for(int i = 0; i < 256; i++){
		uint8_t r = filter_r8_from_332((uint8_t)i);
		uint8_t g = filter_g8_from_332((uint8_t)i);
		uint8_t b = filter_b8_from_332((uint8_t)i);
		ctx->pal32[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}

	for(int a = 0; a < 256; a++){
		uint32_t ca = ctx->pal32[a];
		int ar = (ca >> 16) & 255;
		int ag = (ca >> 8) & 255;
		int ab = ca & 255;

		for(int b = 0; b < 256; b++){
			uint32_t cb = ctx->pal32[b];
			int br = (cb >> 16) & 255;
			int bg = (cb >> 8) & 255;
			int bb = cb & 255;

			int dr = ar - br;
			int dg = ag - bg;
			int db = ab - bb;
			int d = (Wr * dr * dr + Wg * dg * dg + Wb * db * db) >> 8;
			if(d < 2) d = 0;
			d = filter_clampi(d, 0, 65535);

			ctx->dist[(a << 8) | b] = (uint16_t)d;
			ctx->similar[(a << 8) | b] = (d <= similar_threshold) ? 1 : 0;
		}
	}
}

static void filter_scale_nearest_2x(
	const uint32_t* src, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx)
{
	(void)ctx;
	for(int y = 0; y < h; y++){
		const uint32_t* srow = src + y * src_pitch;
		uint32_t* drow0 = dst + (y << 1) * dst_pitch;
		uint32_t* drow1 = drow0 + dst_pitch;

		for(int x = 0; x < w; x++){
			uint32_t c = srow[x];
			int dx = x << 1;
			drow0[dx] = c;
			drow0[dx + 1] = c;
			drow1[dx] = c;
			drow1[dx + 1] = c;
		}
	}
}

static void filter_scale_scale2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx)
{
	(void)ctx;
	for(int y = 0; y < h; y++){
		int ym1 = (y > 0) ? (y - 1) : y;
		int yp1 = (y < h - 1) ? (y + 1) : y;

		for(int x = 0; x < w; x++){
			int xm1 = (x > 0) ? (x - 1) : x;
			int xp1 = (x < w - 1) ? (x + 1) : x;

			uint8_t B = keys[ym1 * src_pitch + x];
			uint8_t D = keys[y * src_pitch + xm1];
			uint8_t F = keys[y * src_pitch + xp1];
			uint8_t H = keys[yp1 * src_pitch + x];

			uint32_t cD = src[y * src_pitch + xm1];
			uint32_t cE = src[y * src_pitch + x];
			uint32_t cF = src[y * src_pitch + xp1];

			uint32_t E0 = cE;
			uint32_t E1 = cE;
			uint32_t E2 = cE;
			uint32_t E3 = cE;

			if(B != H && D != F){
				if(D == B) E0 = cD;
				if(B == F) E1 = cF;
				if(D == H) E2 = cD;
				if(H == F) E3 = cF;
			}

			int dx = x << 1;
			int dy = y << 1;
			dst[dy * dst_pitch + dx] = E0;
			dst[dy * dst_pitch + dx + 1] = E1;
			dst[(dy + 1) * dst_pitch + dx] = E2;
			dst[(dy + 1) * dst_pitch + dx + 1] = E3;
		}
	}
}

static inline uint32_t filter_hq_choose_shallow(
	uint32_t cE, uint8_t E,
	uint32_t c1, uint8_t n1,
	uint32_t c2, uint8_t n2,
	const filter_ctx_t* ctx)
{
	if(FILTER_DIST(ctx, E, n1) < FILTER_DIST(ctx, E, n2)){
		return filter_mix75_25(cE, c1);
	}
	return filter_mix75_25(cE, c2);
}

static inline uint8_t filter_hq_pattern8(
	uint8_t A, uint8_t B, uint8_t C,
	uint8_t D, uint8_t E, uint8_t F,
	uint8_t G, uint8_t H, uint8_t I,
	const filter_ctx_t* ctx)
{
	uint8_t p = 0;
	if(!FILTER_SIM(ctx, A, E)) p |= 1 << 0;
	if(!FILTER_SIM(ctx, B, E)) p |= 1 << 1;
	if(!FILTER_SIM(ctx, C, E)) p |= 1 << 2;
	if(!FILTER_SIM(ctx, D, E)) p |= 1 << 3;
	if(!FILTER_SIM(ctx, F, E)) p |= 1 << 4;
	if(!FILTER_SIM(ctx, G, E)) p |= 1 << 5;
	if(!FILTER_SIM(ctx, H, E)) p |= 1 << 6;
	if(!FILTER_SIM(ctx, I, E)) p |= 1 << 7;
	return p;
}

static void filter_scale_hq2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx)
{
	for(int y = 0; y < h; y++){
		int ym1 = (y > 0) ? (y - 1) : y;
		int yp1 = (y < h - 1) ? (y + 1) : y;

		for(int x = 0; x < w; x++){
			int xm1 = (x > 0) ? (x - 1) : x;
			int xp1 = (x < w - 1) ? (x + 1) : x;

			uint8_t A = keys[ym1 * src_pitch + xm1];
			uint8_t B = keys[ym1 * src_pitch + x];
			uint8_t C = keys[ym1 * src_pitch + xp1];
			uint8_t D = keys[y * src_pitch + xm1];
			uint8_t E = keys[y * src_pitch + x];
			uint8_t F = keys[y * src_pitch + xp1];
			uint8_t G = keys[yp1 * src_pitch + xm1];
			uint8_t H = keys[yp1 * src_pitch + x];
			uint8_t I = keys[yp1 * src_pitch + xp1];

			uint32_t cA = src[ym1 * src_pitch + xm1];
			uint32_t cB = src[ym1 * src_pitch + x];
			uint32_t cC = src[ym1 * src_pitch + xp1];
			uint32_t cD = src[y * src_pitch + xm1];
			uint32_t cE = src[y * src_pitch + x];
			uint32_t cF = src[y * src_pitch + xp1];
			uint32_t cG = src[yp1 * src_pitch + xm1];
			uint32_t cH = src[yp1 * src_pitch + x];
			uint32_t cI = src[yp1 * src_pitch + xp1];

			uint32_t p1 = cE;
			uint32_t p2 = cE;
			uint32_t p3 = cE;
			uint32_t p4 = cE;

			uint8_t pat = filter_hq_pattern8(A, B, C, D, E, F, G, H, I, ctx);

			if(!FILTER_SIM(ctx, B, E) && !FILTER_SIM(ctx, D, E) && FILTER_SIM(ctx, B, D)){
				p1 = filter_hq_choose_shallow(cE, E, cB, B, cD, D, ctx);
			}else if(!FILTER_SIM(ctx, B, E) && FILTER_SIM(ctx, D, E)){
				p1 = filter_mix75_25(cE, cB);
			}else if(FILTER_SIM(ctx, B, E) && !FILTER_SIM(ctx, D, E)){
				p1 = filter_mix75_25(cE, cD);
			}

			if(!FILTER_SIM(ctx, B, E) && !FILTER_SIM(ctx, F, E) && FILTER_SIM(ctx, B, F)){
				p2 = filter_hq_choose_shallow(cE, E, cB, B, cF, F, ctx);
			}else if(!FILTER_SIM(ctx, B, E) && FILTER_SIM(ctx, F, E)){
				p2 = filter_mix75_25(cE, cB);
			}else if(FILTER_SIM(ctx, B, E) && !FILTER_SIM(ctx, F, E)){
				p2 = filter_mix75_25(cE, cF);
			}

			if(!FILTER_SIM(ctx, H, E) && !FILTER_SIM(ctx, D, E) && FILTER_SIM(ctx, H, D)){
				p3 = filter_hq_choose_shallow(cE, E, cH, H, cD, D, ctx);
			}else if(!FILTER_SIM(ctx, H, E) && FILTER_SIM(ctx, D, E)){
				p3 = filter_mix75_25(cE, cH);
			}else if(FILTER_SIM(ctx, H, E) && !FILTER_SIM(ctx, D, E)){
				p3 = filter_mix75_25(cE, cD);
			}

			if(!FILTER_SIM(ctx, H, E) && !FILTER_SIM(ctx, F, E) && FILTER_SIM(ctx, H, F)){
				p4 = filter_hq_choose_shallow(cE, E, cH, H, cF, F, ctx);
			}else if(!FILTER_SIM(ctx, H, E) && FILTER_SIM(ctx, F, E)){
				p4 = filter_mix75_25(cE, cH);
			}else if(FILTER_SIM(ctx, H, E) && !FILTER_SIM(ctx, F, E)){
				p4 = filter_mix75_25(cE, cF);
			}

			if(!FILTER_SIM(ctx, A, E) && FILTER_SIM(ctx, B, E) && FILTER_SIM(ctx, D, E) && !FILTER_SIM(ctx, C, E) && !FILTER_SIM(ctx, G, E)){
				p1 = filter_mix50(cE, cA);
			}
			if(!FILTER_SIM(ctx, C, E) && FILTER_SIM(ctx, B, E) && FILTER_SIM(ctx, F, E) && !FILTER_SIM(ctx, A, E) && !FILTER_SIM(ctx, I, E)){
				p2 = filter_mix50(cE, cC);
			}
			if(!FILTER_SIM(ctx, G, E) && FILTER_SIM(ctx, H, E) && FILTER_SIM(ctx, D, E) && !FILTER_SIM(ctx, A, E) && !FILTER_SIM(ctx, I, E)){
				p3 = filter_mix50(cE, cG);
			}
			if(!FILTER_SIM(ctx, I, E) && FILTER_SIM(ctx, H, E) && FILTER_SIM(ctx, F, E) && !FILTER_SIM(ctx, C, E) && !FILTER_SIM(ctx, G, E)){
				p4 = filter_mix50(cE, cI);
			}

			if(pat == 0xFF){
				p1 = cE;
				p2 = cE;
				p3 = cE;
				p4 = cE;
			}

			int dx = x << 1;
			int dy = y << 1;
			dst[dy * dst_pitch + dx] = p1;
			dst[dy * dst_pitch + dx + 1] = p2;
			dst[(dy + 1) * dst_pitch + dx] = p3;
			dst[(dy + 1) * dst_pitch + dx + 1] = p4;
		}
	}
}

static inline int filter_xbr_edge_strength(uint8_t a, uint8_t b, uint8_t c, uint8_t d, const filter_ctx_t* ctx){
	return (int)FILTER_DIST(ctx, a, d) - (int)FILTER_DIST(ctx, b, c);
}

static inline uint32_t filter_xbr_pick_or_blend(
	uint32_t cE, uint8_t E,
	uint32_t ca, uint8_t a,
	uint32_t cb, uint8_t b,
	const filter_ctx_t* ctx, int hard_bias)
{
	int da = (int)FILTER_DIST(ctx, E, a);
	int db = (int)FILTER_DIST(ctx, E, b);

	if(da + hard_bias < db) return ca;
	if(db + hard_bias < da) return cb;
	if(da <= db) return filter_mix75_25(cE, ca);
	return filter_mix75_25(cE, cb);
}

static void filter_scale_xbr2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx)
{
	const int T0 = 3;
	const int T1 = 5;

	for(int y = 0; y < h; y++){
		int ym1 = (y > 0) ? (y - 1) : y;
		int yp1 = (y < h - 1) ? (y + 1) : y;

		for(int x = 0; x < w; x++){
			int xm1 = (x > 0) ? (x - 1) : x;
			int xp1 = (x < w - 1) ? (x + 1) : x;

			uint8_t B = keys[ym1 * src_pitch + x];
			uint8_t C = keys[ym1 * src_pitch + xp1];
			uint8_t D = keys[y * src_pitch + xm1];
			uint8_t E = keys[y * src_pitch + x];
			uint8_t F = keys[y * src_pitch + xp1];
			uint8_t G = keys[yp1 * src_pitch + xm1];
			uint8_t H = keys[yp1 * src_pitch + x];
			uint8_t I = keys[yp1 * src_pitch + xp1];

			uint32_t cB = src[ym1 * src_pitch + x];
			uint32_t cD = src[y * src_pitch + xm1];
			uint32_t cE = src[y * src_pitch + x];
			uint32_t cF = src[y * src_pitch + xp1];
			uint32_t cH = src[yp1 * src_pitch + x];

			uint32_t p1 = cE;
			uint32_t p2 = cE;
			uint32_t p3 = cE;
			uint32_t p4 = cE;

			int f_ne = filter_xbr_edge_strength(D, B, E, F, ctx);
			int f_nw = filter_xbr_edge_strength(C, E, B, D, ctx);
			int f_se = filter_xbr_edge_strength(H, F, I, E, ctx);
			int f_sw = filter_xbr_edge_strength(E, G, H, D, ctx);

			if(f_ne > 0 && FILTER_DIST(ctx, B, F) > T0 && E != B && E != F){
				p2 = filter_xbr_pick_or_blend(cE, E, cB, B, cF, F, ctx, T1);
			}
			if(f_nw > 0 && FILTER_DIST(ctx, B, D) > T0 && E != B && E != D){
				p1 = filter_xbr_pick_or_blend(cE, E, cB, B, cD, D, ctx, T1);
			}
			if(f_se > 0 && FILTER_DIST(ctx, H, F) > T0 && E != H && E != F){
				p4 = filter_xbr_pick_or_blend(cE, E, cH, H, cF, F, ctx, T1);
			}
			if(f_sw > 0 && FILTER_DIST(ctx, H, D) > T0 && E != H && E != D){
				p3 = filter_xbr_pick_or_blend(cE, E, cH, H, cD, D, ctx, T1);
			}

			int dx = x << 1;
			int dy = y << 1;
			dst[dy * dst_pitch + dx] = p1;
			dst[dy * dst_pitch + dx + 1] = p2;
			dst[(dy + 1) * dst_pitch + dx] = p3;
			dst[(dy + 1) * dst_pitch + dx + 1] = p4;
		}
	}
}

void filter_scale_2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx,
	filter_scale_mode_t mode)
{
	switch(mode){
		case FILTER_SCALE_NONE:
			filter_scale_nearest_2x(src, w, h, src_pitch, dst, dst_pitch, ctx);
			break;
		case FILTER_SCALE_SCALE2X:
			filter_scale_scale2x(src, keys, w, h, src_pitch, dst, dst_pitch, ctx);
			break;
		case FILTER_SCALE_HQ2X:
			filter_scale_hq2x(src, keys, w, h, src_pitch, dst, dst_pitch, ctx);
			break;
		case FILTER_SCALE_XBR2X:
		default:
			filter_scale_xbr2x(src, keys, w, h, src_pitch, dst, dst_pitch, ctx);
			break;
	}
}

static void filter_apply_scanlines_custom(
	uint32_t* dst, int w, int h, int pitch, uint8_t dark)
{
	if(dark >= 255) return;

	for(int y = 1; y < h; y += 2){
		uint32_t* row = dst + y * pitch;
		for(int x = 0; x < w; x++){
			row[x] = filter_mul_argb(row[x], dark);
		}
	}
}

static void filter_apply_scanlines_preset(
	uint32_t* dst, int w, int h, int pitch, filter_crt_mode_t mode)
{
	uint8_t dark = 255;

	switch(mode){
		case FILTER_CRT_SCANLINES_25: dark = 192; break;
		case FILTER_CRT_SCANLINES_37: dark = 160; break;
		case FILTER_CRT_SCANLINES_50: dark = 128; break;
		default: return;
	}

	filter_apply_scanlines_custom(dst, w, h, pitch, dark);
}

static inline uint32_t filter_apply_grille_pixel(uint32_t c, int x, uint8_t strength){
	uint32_t a = (c >> 24) & 255;
	uint32_t r = (c >> 16) & 255;
	uint32_t g = (c >> 8) & 255;
	uint32_t b = c & 255;
	uint32_t weak = 256 - (strength >> 1);

	switch(x % 3){
		case 0:
			g = filter_scale_chan(g, weak);
			b = filter_scale_chan(b, weak);
			break;
		case 1:
			r = filter_scale_chan(r, weak);
			b = filter_scale_chan(b, weak);
			break;
		default:
			r = filter_scale_chan(r, weak);
			g = filter_scale_chan(g, weak);
			break;
	}

	return filter_pack_argb(a, r, g, b);
}

static void filter_apply_aperture_grille(
	uint32_t* dst, int w, int h, int pitch, uint8_t strength)
{
	for(int y = 0; y < h; y++){
		uint32_t* row = dst + y * pitch;
		for(int x = 0; x < w; x++){
			row[x] = filter_apply_grille_pixel(row[x], x, strength);
		}
	}
}

static inline uint32_t filter_apply_mask_pixel(uint32_t c, int x, int y, uint8_t strength){
	uint32_t a = (c >> 24) & 255;
	uint32_t r = (c >> 16) & 255;
	uint32_t g = (c >> 8) & 255;
	uint32_t b = c & 255;
	uint32_t weak = 256 - (strength >> 1);
	int phase = (x + ((y & 1) ? 1 : 0)) % 3;

	switch(phase){
		case 0:
			g = filter_scale_chan(g, weak);
			b = filter_scale_chan(b, weak);
			break;
		case 1:
			r = filter_scale_chan(r, weak);
			b = filter_scale_chan(b, weak);
			break;
		default:
			r = filter_scale_chan(r, weak);
			g = filter_scale_chan(g, weak);
			break;
	}

	return filter_pack_argb(a, r, g, b);
}

static void filter_apply_shadow_mask(
	uint32_t* dst, int w, int h, int pitch, uint8_t strength)
{
	for(int y = 0; y < h; y++){
		uint32_t* row = dst + y * pitch;
		for(int x = 0; x < w; x++){
			row[x] = filter_apply_mask_pixel(row[x], x, y, strength);
		}
	}
}

static void filter_apply_analog_prefilter(
	uint32_t* dst, int w, int h, int pitch,
	int blur, int mix_rb, int mix_g, int skew_r, int skew_b, int ring)
{
	uint32_t* tmp = (uint32_t*)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
	if(!tmp) return;

	for(int y = 0; y < h; y++){
		memcpy(tmp + y * w, dst + y * pitch, (size_t)w * sizeof(uint32_t));
	}

	if(blur < 1){ blur = 1; }
	if(mix_rb < 0){ mix_rb = 0; }
	if(mix_rb > 28){ mix_rb = 28; }
	if(mix_g < 0){ mix_g = 0; }
	if(mix_g > 24){ mix_g = 24; }
	if(ring < 0){ ring = 0; }

	for(int y = 0; y < h; y++){
		int phase = y & 1;

		for(int x = 0; x < w; x++){
			int xl = x - blur;
			int xr = x + blur;
			if(xl < 0) xl = 0;
			if(xr >= w) xr = w - 1;

			uint32_t cl = tmp[y * w + xl];
			uint32_t cc = tmp[y * w + x];
			uint32_t cr = tmp[y * w + xr];

			uint32_t a = (cc >> 24) & 255U;

			int rl = (int)((cl >> 16) & 255U);
			int gl = (int)((cl >> 8) & 255U);
			int bl = (int)(cl & 255U);
			int rc = (int)((cc >> 16) & 255U);
			int gc = (int)((cc >> 8) & 255U);
			int bc = (int)(cc & 255U);
			int rr = (int)((cr >> 16) & 255U);
			int gr = (int)((cr >> 8) & 255U);
			int br = (int)(cr & 255U);

			int r = ((rc * (64 - mix_rb)) + ((rl + rr) * (mix_rb >> 1)) + 32) >> 6;
			int g = ((gc * (64 - mix_g)) + ((gl + gr) * (mix_g >> 1)) + 32) >> 6;
			int b = ((bc * (64 - mix_rb)) + ((bl + br) * (mix_rb >> 1)) + 32) >> 6;

			if(ring != 0){
				r += (((rc << 1) - rl - rr) * ring) >> 6;
				g += (((gc << 1) - gl - gr) * (ring >> 1)) >> 6;
				b += (((bc << 1) - bl - br) * ring) >> 6;
			}

			if(phase){
				r += (rr - rl) >> skew_r;
				b -= (rr - rl) >> skew_b;
			}else{
				b += (br - bl) >> skew_b;
				r -= (br - bl) >> skew_r;
			}

			r = filter_clampi(r, 0, 255);
			g = filter_clampi(g, 0, 255);
			b = filter_clampi(b, 0, 255);

			dst[y * pitch + x] = filter_pack_argb(a, (uint32_t)r, (uint32_t)g, (uint32_t)b);
		}
	}

	free(tmp);
}

static void filter_apply_prefilter_soft_rgb(
	uint32_t* dst, int w, int h, int pitch)
{
	filter_apply_analog_prefilter(dst, w, h, pitch, 1, 6, 4, 8, 8, 0);
}

static void filter_apply_prefilter_svideo(
	uint32_t* dst, int w, int h, int pitch)
{
	filter_apply_analog_prefilter(dst, w, h, pitch, 1, 10, 6, 5, 6, 0);
}

static void filter_apply_prefilter_composite(
	uint32_t* dst, int w, int h, int pitch, uint8_t strength)
{
	int mix_rb = 8 + ((int)strength >> 3);
	if(mix_rb > 20){ mix_rb = 20; }
	filter_apply_analog_prefilter(dst, w, h, pitch, 1, mix_rb, mix_rb >> 1, 3, 4, 2);
}

static void filter_apply_prefilter_rfmod(
	uint32_t* dst, int w, int h, int pitch, uint8_t strength)
{
	int mix_rb = 16 + ((int)strength >> 3);
	if(mix_rb > 28){ mix_rb = 28; }
	filter_apply_analog_prefilter(dst, w, h, pitch, 1, mix_rb, (mix_rb >> 1) + 2, 2, 3, 5);
}

static void filter_apply_curvature(
	uint32_t* dst, int w, int h, int pitch, uint8_t strength, uint8_t vignette)
{
	uint32_t* tmp = (uint32_t*)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
	if(!tmp) return;

	for(int y = 0; y < h; y++){
		memcpy(tmp + y * w, dst + y * pitch, (size_t)w * sizeof(uint32_t));
	}

	float k = (float)strength / 4096.0f;
	float cx = (float)(w - 1) * 0.5f;
	float cy = (float)(h - 1) * 0.5f;
	float invcx = (cx != 0.0f) ? (1.0f / cx) : 0.0f;
	float invcy = (cy != 0.0f) ? (1.0f / cy) : 0.0f;

	for(int y = 0; y < h; y++){
		for(int x = 0; x < w; x++){
			float nx = ((float)x - cx) * invcx;
			float ny = ((float)y - cy) * invcy;
			float r2 = nx * nx + ny * ny;

			float sx = nx * (1.0f + k * r2);
			float sy = ny * (1.0f + k * r2);

			int ix = (int)(sx * cx + cx + 0.5f);
			int iy = (int)(sy * cy + cy + 0.5f);

			uint32_t c;
			if(ix < 0 || iy < 0 || ix >= w || iy >= h){
				c = 0xFF000000u;
			}else{
				c = tmp[iy * w + ix];
			}

			if(vignette){
				float vx = 1.0f - (nx * nx + ny * ny) * ((float)vignette / 255.0f) * 0.35f;
				if(vx < 0.0f) vx = 0.0f;
				if(vx > 1.0f) vx = 1.0f;

				uint32_t a = (c >> 24) & 255;
				uint32_t r = (uint32_t)(((c >> 16) & 255) * vx);
				uint32_t g = (uint32_t)(((c >> 8) & 255) * vx);
				uint32_t b = (uint32_t)((c & 255) * vx);
				c = filter_pack_argb(a, r, g, b);
			}

			dst[y * pitch + x] = c;
		}
	}

	free(tmp);
}



static inline int filter_luma(uint32_t c){
	return (int)((((c >> 16) & 255) * 77) + (((c >> 8) & 255) * 150) + ((c & 255) * 29)) >> 8;
}

static void filter_apply_bump_map(
	uint32_t* dst, int w, int h, int pitch)
{
	uint32_t* tmp = (uint32_t*)malloc((size_t)pitch * (size_t)h * sizeof(uint32_t));
	if(!tmp){ return; }
	memcpy(tmp, dst, (size_t)pitch * (size_t)h * sizeof(uint32_t));

	for(int y = 0; y < h; y++){
		for(int x = 0; x < w; x++){
			int xl = (x > 0) ? (x - 1) : x;
			int xr = (x + 1 < w) ? (x + 1) : x;
			int yu = (y > 0) ? (y - 1) : y;
			int yd = (y + 1 < h) ? (y + 1) : y;
			uint32_t c = tmp[y * pitch + x];
			int dx = filter_luma(tmp[y * pitch + xr]) - filter_luma(tmp[y * pitch + xl]);
			int dy = filter_luma(tmp[yd * pitch + x]) - filter_luma(tmp[yu * pitch + x]);
			int shade = filter_clampi(160 + ((dx * 3 - dy * 4) >> 2), 32, 255);
			int rim = filter_clampi(96 + ((dx + dy) >> 1), 0, 255);
			int r = (int)((c >> 16) & 255);
			int g = (int)((c >> 8) & 255);
			int b = (int)(c & 255);
			r = filter_clampi(((r * shade) >> 8) + (rim >> 3), 0, 255);
			g = filter_clampi(((g * shade) >> 8) + (rim >> 3), 0, 255);
			b = filter_clampi(((b * shade) >> 8) + (rim >> 2), 0, 255);
			dst[y * pitch + x] = filter_pack_argb((c >> 24) & 255, (uint32_t)r, (uint32_t)g, (uint32_t)b);
		}
	}

	free(tmp);
}

static void filter_apply_lucid(
	uint32_t* dst, int w, int h, int pitch)
{
	uint32_t* tmp = (uint32_t*)malloc((size_t)pitch * (size_t)h * sizeof(uint32_t));
	if(!tmp){ return; }
	memcpy(tmp, dst, (size_t)pitch * (size_t)h * sizeof(uint32_t));

	for(int y = 0; y < h; y++){
		for(int x = 0; x < w; x++){
			int xl = (x > 0) ? (x - 1) : x;
			int xr = (x + 1 < w) ? (x + 1) : x;
			int yu = (y > 0) ? (y - 1) : y;
			int yd = (y + 1 < h) ? (y + 1) : y;
			uint32_t c = tmp[y * pitch + x];
			uint32_t cl = tmp[y * pitch + xl];
			uint32_t cr = tmp[y * pitch + xr];
			uint32_t cu = tmp[yu * pitch + x];
			uint32_t cd = tmp[yd * pitch + x];
			int glowr = (int)(((cl >> 16) & 255) + ((cr >> 16) & 255) + ((cu >> 16) & 255) + ((cd >> 16) & 255));
			int glowg = (int)(((cl >> 8) & 255) + ((cr >> 8) & 255) + ((cu >> 8) & 255) + ((cd >> 8) & 255));
			int glowb = (int)((cl & 255) + (cr & 255) + (cu & 255) + (cd & 255));
			int r = (int)((((cr >> 16) & 255) * 3) + (((c >> 16) & 255) * 5) + (((cu >> 16) & 255) * 2) + (glowr >> 2));
			int g = (int)((((c >> 8) & 255) * 6) + (((cl >> 8) & 255) * 2) + (((cr >> 8) & 255) * 2) + (glowg >> 2));
			int b = (int)((( (cl & 255) * 3) + ((c & 255) * 5) + ((cd & 255) * 2) + (glowb >> 2)));
			r = filter_clampi(r >> 3, 0, 255);
			g = filter_clampi(g >> 3, 0, 255);
			b = filter_clampi(b >> 3, 0, 255);
			r = filter_clampi(r + 18, 0, 255);
			g = filter_clampi(g + 10, 0, 255);
			b = filter_clampi(b + 22, 0, 255);
			dst[y * pitch + x] = filter_pack_argb((c >> 24) & 255, (uint32_t)r, (uint32_t)g, (uint32_t)b);
		}
	}

	free(tmp);
}

static void filter_apply_heatwave(
	uint32_t* dst, int w, int h, int pitch)
{
	uint32_t* tmp = (uint32_t*)malloc((size_t)pitch * (size_t)h * sizeof(uint32_t));
	if(!tmp){ return; }
	memcpy(tmp, dst, (size_t)pitch * (size_t)h * sizeof(uint32_t));

	for(int y = 0; y < h; y++){
		float fy = (float)y;
		int shift = (int)(sinf(fy * 0.17f) * 2.0f) + (int)(sinf((fy * 0.05f) + 0.8f) * 1.0f);
		int band = (int)(12.0f * (sinf((fy * 0.11f) + 0.6f) + 1.0f));
		for(int x = 0; x < w; x++){
			int sx = filter_clampi(x + shift, 0, w - 1);
			uint32_t c = tmp[y * pitch + sx];
			int r = (int)((c >> 16) & 255);
			int g = (int)((c >> 8) & 255);
			int b = (int)(c & 255);
			r = filter_clampi(r + (band >> 1), 0, 255);
			g = filter_clampi(g + (band >> 3), 0, 255);
			b = filter_clampi(b - (band >> 2), 0, 255);
			dst[y * pitch + x] = filter_pack_argb((c >> 24) & 255, (uint32_t)r, (uint32_t)g, (uint32_t)b);
		}
	}

	free(tmp);
}

static void filter_apply_prefilter_black_white(
	uint32_t* dst, int w, int h, int pitch)
{
	int x;
	int y;
	for(y = 0; y < h; ++y){
		for(x = 0; x < w; ++x){
			uint32_t c = dst[y * pitch + x];
			uint32_t a = (c >> 24) & 255u;
			uint32_t r = (c >> 16) & 255u;
			uint32_t g = (c >> 8) & 255u;
			uint32_t b = c & 255u;
			uint32_t l = ((77u * r) + (150u * g) + (29u * b) + 128u) >> 8;
			dst[y * pitch + x] = filter_pack_argb(a, l, l, l);
		}
	}
}

void filter_apply_pre(
	uint32_t* dst, int w, int h, int pitch,
	filter_pre_mode_t mode,
	const filter_crt_params_t* params)
{
	filter_crt_params_t def;

	if(!params){
		def.mode = FILTER_CRT_NONE;
		def.scanline_dark = 192;
		def.mask_strength = 56;
		def.curvature_strength = 24;
		def.composite_strength = 40;
		def.vignette_strength = 40;
		params = &def;
	}

	switch(mode){
		case FILTER_PRE_SOFT_RGB:
			filter_apply_prefilter_soft_rgb(dst, w, h, pitch);
			break;
		case FILTER_PRE_SVIDEO:
			filter_apply_prefilter_svideo(dst, w, h, pitch);
			break;
		case FILTER_PRE_COMPOSITE:
			filter_apply_prefilter_composite(dst, w, h, pitch,
				params->composite_strength ? params->composite_strength : 40);
			break;
		case FILTER_PRE_RF_MOD:
			filter_apply_prefilter_rfmod(dst, w, h, pitch,
				params->composite_strength ? params->composite_strength : 40);
			break;
		case FILTER_PRE_BLACK_WHITE:
			filter_apply_prefilter_black_white(dst, w, h, pitch);
			break;
		case FILTER_PRE_NONE:
		default:
			break;
	}
}

void filter_apply_crt(
	uint32_t* dst, int w, int h, int pitch,
	const filter_crt_params_t* params)
{
	filter_crt_params_t def;

	if(!params){
		def.mode = FILTER_CRT_NONE;
		def.scanline_dark = 192;
		def.mask_strength = 56;
		def.curvature_strength = 24;
		def.composite_strength = 40;
		def.vignette_strength = 40;
		params = &def;
	}

	switch(params->mode){
		case FILTER_CRT_NONE:
			break;

		case FILTER_CRT_SCANLINES_25:
		case FILTER_CRT_SCANLINES_37:
		case FILTER_CRT_SCANLINES_50:
			filter_apply_scanlines_preset(dst, w, h, pitch, params->mode);
			break;

		case FILTER_CRT_APERTURE_GRILLE:
			filter_apply_aperture_grille(dst, w, h, pitch,
				params->mask_strength ? params->mask_strength : 56);
			break;

		case FILTER_CRT_SHADOW_MASK:
			filter_apply_shadow_mask(dst, w, h, pitch,
				params->mask_strength ? params->mask_strength : 72);
			break;

		case FILTER_CRT_CURVATURE:
			filter_apply_curvature(dst, w, h, pitch,
				params->curvature_strength ? params->curvature_strength : 24,
				params->vignette_strength);
			break;

		case FILTER_CRT_COMPOSITE:
		case FILTER_CRT_COMPOSITE_SCANLINES:
			break;

		case FILTER_CRT_GRILLE_SCANLINES:
			filter_apply_aperture_grille(dst, w, h, pitch,
				params->mask_strength ? params->mask_strength : 56);
			filter_apply_scanlines_custom(dst, w, h, pitch,
				params->scanline_dark ? params->scanline_dark : 192);
			break;

		case FILTER_CRT_MASK_SCANLINES:
			filter_apply_shadow_mask(dst, w, h, pitch,
				params->mask_strength ? params->mask_strength : 72);
			filter_apply_scanlines_custom(dst, w, h, pitch,
				params->scanline_dark ? params->scanline_dark : 192);
			break;

		case FILTER_CRT_BUMP_MAP:
			filter_apply_bump_map(dst, w, h, pitch);
			break;

		case FILTER_CRT_LUCID:
			filter_apply_lucid(dst, w, h, pitch);
			break;

		case FILTER_CRT_HEATWAVE:
			filter_apply_heatwave(dst, w, h, pitch);
			break;

		default:
			break;
	}
}

void filter_scale_and_crt_2x(
	const uint32_t* src, const uint8_t* keys, int w, int h, int src_pitch,
	uint32_t* dst, int dst_pitch,
	const filter_ctx_t* ctx,
	filter_scale_mode_t scale_mode,
	const filter_crt_params_t* crt_params)
{
	filter_scale_2x(src, keys, w, h, src_pitch, dst, dst_pitch, ctx, scale_mode);
	filter_apply_crt(dst, w << 1, h << 1, dst_pitch, crt_params);
}