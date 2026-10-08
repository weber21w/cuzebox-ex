/*
 *  Text GUI elements
 *
 *  Copyright (C) 2016 - 2017
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



#include "textgui.h"
#include "guicore.h"
#include "conout.h"
#include "chars.h"

#include <stdarg.h>



/* Text GUI elements */
static textgui_struct_t textgui_elements;


/* Timer for console output */
static auint textgui_tim = 0U;



/* Text color (Uzebox color index) */
#define TEXT_COLOR ((0x2U << 6) + (0x4U << 3) + 0x4U)

/* On-screen transient message log */
#define TEXTGUI_LOG_MAX       8U
#define TEXTGUI_LOG_STR_MAX   48U
#define TEXTGUI_LOG_DEF_TTL   360U
#define TEXTGUI_LOG_FADE_TTL   60U
#define TEXTGUI_LOG_MARGIN_X    4U
#define TEXTGUI_LOG_MARGIN_Y    4U
#define TEXTGUI_LOG_LINE_H      8U



/* Text GUI character grid assistance functions */
#define TOP_X(x) (((x) * 6U) +  10U)
#define TOP_Y(y) (((y) * 6U) +   1U)
#define BOT_X(x) (((x) * 6U) +  10U)
#define BOT_Y(y) (((y) * 6U) + 267U)


/* Map of constant elements of the top part */
static uint8 const textgui_top[50U * 3U] = {
 'C', 'U', 'z', 'e', 'B', 'o', 'x', ' ', ' ', ' ',
 ' ', '%', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'P', ' ',
  0U,  1U, ' ', ' ', ' ', ' ', ' ', ' ',  3U, ' ',
 11U, ' ', ' ', ' ', '.', ' ', ' ', ' ',  6U,  7U,

 'G', 'a', 'm', 'e', ':', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 10U, ' ', ' ', ' ', '.', ' ', ' ', ' ',  4U,  5U,

 'A', 'u', 't', 'h', ':', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 12U, ' ', ' ', ' ', '.', ' ', ' ', ' ',  8U,  9U,
};

/* Map of constant elements of the bottom part */
static uint8 const textgui_bot[50U * 2U] = {
 'P', 'o', 'r', 't', '3', '9', ':', ' ', ' ', ' ',
 ' ', ';', ' ', '0', 'x', ' ', ' ', ';', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'b', ' ', ' ',
 ' ', ' ', 'W', 'D', 'R', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ',
 '0' + ((VER_DATE >> 28) & 0xFU),
 '0' + ((VER_DATE >> 24) & 0xFU),
 '0' + ((VER_DATE >> 20) & 0xFU),
 '0' + ((VER_DATE >> 16) & 0xFU),

 'P', 'o', 'r', 't', '3', 'A', ':', ' ', ' ', ' ',
 ' ', ';', ' ', '0', 'x', ' ', ' ', ';', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'b', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
 ' ', ' ', ' ', ' ', ' ', ' ',
 '0' + ((VER_DATE >> 12) & 0xFU),
 '0' + ((VER_DATE >>  8) & 0xFU),
 '0' + ((VER_DATE >>  4) & 0xFU),
 '0' + ((VER_DATE      ) & 0xFU)
};

/* Controller mappings */
static uint8 const textgui_kbsnes[] = {'S', 'N', 'E', 'S', 0U};
static uint8 const textgui_kbuzem[] = {'U', 'Z', 'E', 'M', 0U};

/* Structure to pass around parameters */
typedef struct{
 uint32*       pix;    /* Target pixel buffer */
 auint         ptc;    /* Pixel buffer's pitch */
 uint32 const* pal;    /* Palette to use for graphics render */
 boole         prmsg;  /* Whether to print on the console */
 boole         nogrf;  /* Whether to omit graphics output */
}textgui_rs_t;

typedef struct{
	auint ttl;
	auint life;
	uint8 text[TEXTGUI_LOG_STR_MAX + 1U];
}textgui_logmsg_t;

static textgui_logmsg_t textgui_logmsgs[TEXTGUI_LOG_MAX];
static auint textgui_logcount = 0U;
static boole textgui_log_enabled = TRUE;
static auint textgui_log_level = CU_LOG_INFO;
static auint textgui_log_window;
static auint textgui_log_burst;
static auint textgui_log_suppressed;
static struct { uint32 hash; auint tick; boole used; } textgui_log_recent[32];
static auint textgui_log_recent_next;

void textgui_log_set_level(auint level)
{
 textgui_log_level = (level <= CU_LOG_TRACE) ? level : CU_LOG_INFO;
 textgui_logcount = 0U;
 textgui_log_window = SDL_GetTicks();
 textgui_log_burst = 0U;
 textgui_log_suppressed = 0U;
 textgui_log_recent_next = 0U;
 memset(textgui_log_recent, 0, sizeof(textgui_log_recent));
}

auint textgui_log_get_level(void){ return textgui_log_level; }
auint textgui_log_get_count(void){ return textgui_logcount; }
auint textgui_log_get_suppressed(void){ return textgui_log_suppressed; }



/*
** Renders a character at a given position, the surface assumed to be
** 320 x 270 pixels (so character pixels are 2 actual pixels wide, about 53
** characters fit horizontally). No bound checks are performed, the character
** must be on the surface.
*/

static void textgui_putchar(auint x, auint y, auint chr,
                            textgui_rs_t const* rs)
{
 auint   i;
 auint   dp;
 auint   cp;
 auint   ch;
 uint32  tc;
 uint32* pix;
 auint   ptc;
 uint32 const* pal;

 if (!(rs->nogrf)){

  pix = rs->pix;
  ptc = rs->ptc;
  pal = rs->pal;
  dp  = (y * ptc) + (x * 2U);
  cp  = (chr & 0x7FU) * 6U;

  for (i = 0U; i < 6U; i++){
   ch = chars[cp];
   tc = pal[((ch & 0x80U) >> 7) * TEXT_COLOR];
   pix[dp +  0U] = tc;
   pix[dp +  1U] = tc;
   tc = pal[((ch & 0x40U) >> 6) * TEXT_COLOR];
   pix[dp +  2U] = tc;
   pix[dp +  3U] = tc;
   tc = pal[((ch & 0x20U) >> 5) * TEXT_COLOR];
   pix[dp +  4U] = tc;
   pix[dp +  5U] = tc;
   tc = pal[((ch & 0x10U) >> 4) * TEXT_COLOR];
   pix[dp +  6U] = tc;
   pix[dp +  7U] = tc;
   tc = pal[((ch & 0x08U) >> 3) * TEXT_COLOR];
   pix[dp +  8U] = tc;
   pix[dp +  9U] = tc;
   tc = pal[((ch & 0x04U) >> 2) * TEXT_COLOR];
   pix[dp + 10U] = tc;
   pix[dp + 11U] = tc;
   cp ++;
   dp += ptc;
  }

 }

 if (rs->prmsg){
  conout_addchr(chr & 0x7FU);
 }
}



/*
** Outputs a decimal number between 0 - 999. The number is truncated if it is
** larger, leading zeroes will be generated this case.
*/
static void textgui_putdec(auint x, auint y, auint val,
                           textgui_rs_t const* rs)
{
 auint n;

 n = (val / 100U) % 10U;
 if ((val >= 1000U) || (n != 0U)){
  textgui_putchar(x +  0U, y, n + '0', rs);
 }

 n = (val /  10U) % 10U;
 if ((val >=  100U) || (n != 0U)){
  textgui_putchar(x +  6U, y, n + '0', rs);
 }

 n = (val       ) % 10U;
 textgui_putchar(x + 12U, y, n + '0', rs);
}



/*
** Outputs a decimal number between 0 - 999999. The number is truncated if it
** is larger, leading zeroes will be generated this case.
*/
static void textgui_putdecx(auint x, auint y, auint val,
                            textgui_rs_t const* rs)
{
 auint n;

 n = (val / 100000U) % 10U;
 if ((val >= 1000000U) || (n != 0U)){
  textgui_putchar(x +  0U, y, n + '0', rs);
 }

 n = (val /  10000U) % 10U;
 if ((val >=  100000U) || (n != 0U)){
  textgui_putchar(x +  6U, y, n + '0', rs);
 }

 n = (val /   1000U) % 10U;
 if ((val >=   10000U) || (n != 0U)){
  textgui_putchar(x + 12U, y, n + '0', rs);
 }

 textgui_putdec(x + 18U, y, val, rs);
}



/*
** Outputs a string (zero or string size limit terminated)
*/
static void textgui_putstr(auint x, auint y, uint8 const* str,
                           textgui_rs_t const* rs)
{
 auint p = 0U;

 while ((str[p] != 0U) && (p < TEXTGUI_STR_MAX)){

  textgui_putchar(x + (p * 6U), y, str[p], rs);
  p ++;

 }
}



/*
** Converts low 4 bits to hexadecimal digit (uppercase)
*/
static uint8 textgui_tohex(auint val)
{
 val = val & 0xFU;
 if (val < 10U){ return ((val + '0')      ); }
 else          { return ((val + 'A') - 10U); }
}




static uint32 textgui_blend(uint32 dst, uint32 src, auint alpha,
                            guicore_pixfmt_t const* pixfmt)
{
	auint ia = 255U - alpha;
	auint dr = (dst >> pixfmt->rsh) & 0xFFU;
	auint dg = (dst >> pixfmt->gsh) & 0xFFU;
	auint db = (dst >> pixfmt->bsh) & 0xFFU;
	auint sr = (src >> pixfmt->rsh) & 0xFFU;
	auint sg = (src >> pixfmt->gsh) & 0xFFU;
	auint sb = (src >> pixfmt->bsh) & 0xFFU;
	auint rr = ((dr * ia) + (sr * alpha) + 127U) / 255U;
	auint rg = ((dg * ia) + (sg * alpha) + 127U) / 255U;
	auint rb = ((db * ia) + (sb * alpha) + 127U) / 255U;

	return (dst & ~((((uint32)0xFFU) << pixfmt->rsh) |
	                 (((uint32)0xFFU) << pixfmt->gsh) |
	                 (((uint32)0xFFU) << pixfmt->bsh))) |
	       ((uint32)rr << pixfmt->rsh) |
	       ((uint32)rg << pixfmt->gsh) |
	       ((uint32)rb << pixfmt->bsh);
}


static void textgui_putchar_alpha(auint x, auint y, auint chr,
                                  uint32 col, auint alpha,
                                  guicore_pixfmt_t const* pixfmt,
                                  uint32* pix, auint ptc)
{
	auint i;
	auint cp;
	auint ch;
	auint dp;

	cp = (chr & 0x7FU) * 6U;
	dp = (y * ptc) + (x * 2U);

	for (i = 0U; i < 6U; i++){
		ch = chars[cp];
		if ((ch & 0x80U) != 0U){
			pix[dp +  0U] = textgui_blend(pix[dp +  0U], col, alpha, pixfmt);
			pix[dp +  1U] = textgui_blend(pix[dp +  1U], col, alpha, pixfmt);
		}
		if ((ch & 0x40U) != 0U){
			pix[dp +  2U] = textgui_blend(pix[dp +  2U], col, alpha, pixfmt);
			pix[dp +  3U] = textgui_blend(pix[dp +  3U], col, alpha, pixfmt);
		}
		if ((ch & 0x20U) != 0U){
			pix[dp +  4U] = textgui_blend(pix[dp +  4U], col, alpha, pixfmt);
			pix[dp +  5U] = textgui_blend(pix[dp +  5U], col, alpha, pixfmt);
		}
		if ((ch & 0x10U) != 0U){
			pix[dp +  6U] = textgui_blend(pix[dp +  6U], col, alpha, pixfmt);
			pix[dp +  7U] = textgui_blend(pix[dp +  7U], col, alpha, pixfmt);
		}
		if ((ch & 0x08U) != 0U){
			pix[dp +  8U] = textgui_blend(pix[dp +  8U], col, alpha, pixfmt);
			pix[dp +  9U] = textgui_blend(pix[dp +  9U], col, alpha, pixfmt);
		}
		if ((ch & 0x04U) != 0U){
			pix[dp + 10U] = textgui_blend(pix[dp + 10U], col, alpha, pixfmt);
			pix[dp + 11U] = textgui_blend(pix[dp + 11U], col, alpha, pixfmt);
		}
		cp ++;
		dp += ptc;
	}
}


static void textgui_putstr_alpha(auint x, auint y, uint8 const* str,
                                 uint32 col, auint alpha,
                                 guicore_pixfmt_t const* pixfmt,
                                 uint32* pix, auint ptc)
{
	auint p = 0U;

	while ((str[p] != 0U) && (p < TEXTGUI_LOG_STR_MAX)){
		textgui_putchar_alpha(x + (p * 6U), y, str[p], col, alpha, pixfmt, pix, ptc);
		p ++;
	}
}


static void textgui_log_prune(void)
{
	auint src;
	auint dst;

	dst = 0U;
	for (src = 0U; src < textgui_logcount; src++){
		if (textgui_logmsgs[src].life != 0U){
			if (dst != src){
				memcpy(&(textgui_logmsgs[dst]), &(textgui_logmsgs[src]), sizeof(textgui_logmsgs[0]));
			}
			dst ++;
		}
	}
	textgui_logcount = dst;
}


void textgui_log_set_enabled(boole enable)
{
	textgui_log_enabled = enable ? TRUE : FALSE;
	if (!textgui_log_enabled){
		textgui_logcount = 0U;
		memset(&(textgui_logmsgs[0]), 0, sizeof(textgui_logmsgs));
	}
}


boole textgui_log_get_enabled(void)
{
	return textgui_log_enabled;
}


static void textgui_log_tick(void)
{
	auint i;

	for (i = 0U; i < textgui_logcount; i++){
		if (textgui_logmsgs[i].life != 0U){
			textgui_logmsgs[i].life --;
		}
	}
	textgui_log_prune();
}


static void textgui_log_push_line(uint8 const* line, auint ttl_frames)
{
	auint i;
	auint len;

	len = (auint)strlen((char const*)line);
	while ((len != 0U) && (line[len - 1U] == ' ')){
		len --;
	}
	if (len == 0U){
		return;
	}

	if (ttl_frames == 0U){
		ttl_frames = TEXTGUI_LOG_DEF_TTL;
	}

	for (i = 0U; i < textgui_logcount; i++){
		if (strcmp((char const*)line, (char const*)textgui_logmsgs[i].text) == 0){
			textgui_logmsgs[i].ttl = ttl_frames;
			textgui_logmsgs[i].life = ttl_frames;
			if (i + 1U < textgui_logcount){
				textgui_logmsg_t tmp;
				memcpy(&tmp, &(textgui_logmsgs[i]), sizeof(tmp));
				memmove(&(textgui_logmsgs[i]), &(textgui_logmsgs[i + 1U]), sizeof(textgui_logmsgs[0]) * (textgui_logcount - (i + 1U)));
				memcpy(&(textgui_logmsgs[textgui_logcount - 1U]), &tmp, sizeof(tmp));
			}
			return;
		}
	}

	if (textgui_logcount >= TEXTGUI_LOG_MAX){
		memmove(&(textgui_logmsgs[0]), &(textgui_logmsgs[1]), sizeof(textgui_logmsgs[0]) * (TEXTGUI_LOG_MAX - 1U));
		textgui_logcount = TEXTGUI_LOG_MAX - 1U;
	}

	memset(&(textgui_logmsgs[textgui_logcount]), 0, sizeof(textgui_logmsgs[0]));
	memcpy(&(textgui_logmsgs[textgui_logcount].text[0]), line, len);
	textgui_logmsgs[textgui_logcount].text[len] = 0U;
	textgui_logmsgs[textgui_logcount].ttl = ttl_frames;
	textgui_logmsgs[textgui_logcount].life = ttl_frames;
	textgui_logcount ++;
}


static void textgui_log_add_timed_raw(char const* msg, auint ttl_frames)
{
	uint8 line[TEXTGUI_LOG_STR_MAX + 1U];
	auint li;
	char c;

	if ((msg == NULL) || (!textgui_log_enabled)){
		return;
	}

	li = 0U;
	while (TRUE){
		c = *msg;
		if (li >= TEXTGUI_LOG_STR_MAX){
			line[li] = 0U;
			textgui_log_push_line(&(line[0]), ttl_frames);
			li = 0U;
			continue;
		}
		if ((c == 0) || (c == '\n') || (c == '\r')){
			line[li] = 0U;
			textgui_log_push_line(&(line[0]), ttl_frames);
			li = 0U;
			if (c == 0){
				break;
			}
			if ((c == '\r') && (msg[1] == '\n')){
				msg ++;
			}
			msg ++;
			continue;
		}
		if ((unsigned char)c < 32U){
			c = ' ';
		}
		line[li] = (uint8)c;
		li ++;
		msg ++;
	}
}


void textgui_log_add_timed(char const* msg, auint ttl_frames)
{
 if (textgui_log_level >= CU_LOG_INFO){ textgui_log_add_timed_raw(msg, ttl_frames); }
}

void textgui_log_add(char const* msg)
{
	textgui_log_add_timed(msg, TEXTGUI_LOG_DEF_TTL);
}


static void textgui_log_draw(boole noovr)
{
	guicore_pixfmt_t pixfmt;
	uint32*          pix;
	auint            ptc;
	uint32           colfg;
	uint32           colsh;
	int              sx;
	int              sy;
	auint            sw;
	auint            sh;
	int              y;
	auint            i;
	auint            alpha;

	if (noovr){
		return;
	}
	if (!textgui_log_enabled){
		return;
	}
	if (textgui_logcount == 0U){
		return;
	}

	guicore_get_source_bounds(&sx, &sy, &sw, &sh);
	if ((sw < 24U) || (sh < 12U)){
		return;
	}

	guicore_getpixfmt(&pixfmt);
	pix = guicore_getpixbuf();
	ptc = guicore_getpitch();
	colfg = guicore_packrgb(255U, 255U, 255U);
	colsh = guicore_packrgb(0U, 0U, 0U);
	y = sy + (int)sh - (int)TEXTGUI_LOG_MARGIN_Y - 6;

	for (i = textgui_logcount; i != 0U; i--){
		textgui_logmsg_t const* msg = &(textgui_logmsgs[i - 1U]);
		if (msg->life == 0U){
			continue;
		}
		if (msg->life >= TEXTGUI_LOG_FADE_TTL){
			alpha = 255U;
		}else{
			alpha = (msg->life * 255U) / TEXTGUI_LOG_FADE_TTL;
			if (alpha == 0U){
				continue;
			}
		}
		textgui_putstr_alpha((auint)sx + TEXTGUI_LOG_MARGIN_X + 1U, (auint)y + 1U,
		                    &(msg->text[0]), colsh, (alpha * 3U) / 5U,
		                    &pixfmt, pix, ptc);
		textgui_putstr_alpha((auint)sx + TEXTGUI_LOG_MARGIN_X, (auint)y,
		                    &(msg->text[0]), colfg, alpha,
		                    &pixfmt, pix, ptc);
		y -= (int)TEXTGUI_LOG_LINE_H;
		if (y < (sy + (int)TEXTGUI_LOG_MARGIN_Y)){
			break;
		}
	}
}


/* Bound routine output by wall time, not emulated frames (fast flashing may
** execute hundreds of frames per second). Trace deliberately bypasses this.
** Errors are deduplicated but distinct failures are never burst-limited. */
static boole textgui_log_accept(auint level, char const* msg)
{
 auint tick = SDL_GetTicks();
 auint i;
 uint32 hash = 2166136261U ^ level;
 unsigned char const* p = (unsigned char const*)msg;
 if (textgui_log_level == CU_LOG_TRACE){ return TRUE; }
 if ((tick - textgui_log_window) >= 1000U){
  if (textgui_log_suppressed != 0U){
   fprintf(stdout, "LOG: %u repeated/burst messages suppressed\n", textgui_log_suppressed);
  }
  textgui_log_window = tick;
  textgui_log_burst = 0U;
  textgui_log_suppressed = 0U;
 }
 while (*p != 0U){ hash = (hash ^ *p++) * 16777619U; }
 for (i = 0U; i < 32U; i++){
  if (textgui_log_recent[i].used && textgui_log_recent[i].hash == hash &&
      (tick - textgui_log_recent[i].tick) < 1000U){
   textgui_log_suppressed++;
   return FALSE;
  }
 }
 if (level != CU_LOG_ERROR && textgui_log_burst >= ((textgui_log_level >= CU_LOG_DEBUG) ? 16U : 4U)){
  textgui_log_suppressed++;
  return FALSE;
 }
 i = textgui_log_recent_next;
 textgui_log_recent[i].hash = hash;
 textgui_log_recent[i].tick = tick;
 textgui_log_recent[i].used = TRUE;
 textgui_log_recent_next = (i + 1U) % 32U;
 if (level != CU_LOG_ERROR){ textgui_log_burst++; }
 return TRUE;
}

static void cu_logv(auint level, char const* fmt, va_list ap)
{
 char buf[512];
 if (level == CU_LOG_OFF || level > textgui_log_level){ return; }
 vsnprintf(buf, sizeof(buf), fmt, ap);
 if (!textgui_log_accept(level, buf)){ return; }
 fputs(buf, level == CU_LOG_ERROR ? stderr : stdout);
 textgui_log_add_timed_raw(buf, TEXTGUI_LOG_DEF_TTL);
}

void cu_log(auint level, char const* fmt, ...)
{
 va_list ap;
 va_start(ap, fmt);
 cu_logv(level, fmt, ap);
 va_end(ap);
}

void cu_message(char const* fmt, ...)
{
 va_list ap;
 va_start(ap, fmt);
 cu_logv(CU_LOG_DEBUG, fmt, ap);
 va_end(ap);
}

void cu_error(char const* fmt, ...)
{
 va_list ap;
 va_start(ap, fmt);
 cu_logv(CU_LOG_ERROR, fmt, ap);
 va_end(ap);
}

void cu_unf(char const* str)
{
 cu_log(CU_LOG_DEBUG, "%s", str);
}


/*
** Redraws text GUI elements. If nohud is set, no legacy on-screen HUD graphics
** are generated. If noovr is set, the transient overlay log isn't rendered.
** Terminal output is generated on every 30th call (1/2 secs).
*/
void textgui_draw(boole nohud, boole noovr)
{
#ifdef HEADLESS
 //return;
#endif
 auint          i;
 auint          j;
 auint          t;
 textgui_rs_t   rs;
 textgui_rs_t   rsnoprn;
 boole          prmsg = (textgui_tim == 0U);

 /* Prepare */

 rs.pal   = guicore_getpalette();
 rs.pix   = guicore_getpixbuf();
 rs.ptc   = guicore_getpitch();
 rs.nogrf = nohud;
 rs.prmsg = prmsg;

 memcpy(&rsnoprn, &rs, sizeof(rs));
 rsnoprn.prmsg = FALSE;

 textgui_tim ++;
 if (textgui_tim == 60U){ textgui_tim = 0U; }

 textgui_log_tick();

 if ((nohud) && (!prmsg) && ((noovr) || (textgui_logcount == 0U))){ return; } /* No output at all */

 /* Place static stuff */

 for (j = 0U; j < 3U; j++){
  for (i = 0U; i < 50U; i++){
   textgui_putchar(TOP_X(i), TOP_Y(j), textgui_top[(j * 50U) + i], &rsnoprn);
  }
 }

 for (j = 0U; j < 2U; j++){
  for (i = 0U; i < 50U; i++){
   textgui_putchar(BOT_X(i), BOT_Y(j), textgui_bot[(j * 50U) + i], &rsnoprn);
  }
 }

 /* Get speed percentage from CPU frequency (which should be 28636400Hz) */

 i = (textgui_elements.cpufreq + (286364U / 2U)) / 286364U;
 if (prmsg){ conout_addstr("CUzeBox: "); }
 textgui_putdec(TOP_X( 8U), TOP_Y(0U), i, &rs);
 if (prmsg){ conout_addstr("% "); }

 /* Output frequencies */

 textgui_putdec(TOP_X(41U), TOP_Y(1U), textgui_elements.cpufreq / 1000000U, &rs);
 if (prmsg){ conout_addchr('.'); }
 textgui_putdec(TOP_X(45U), TOP_Y(1U), textgui_elements.cpufreq /    1000U, &rs);
 if (prmsg){ conout_addstr("MHz; "); }
 textgui_putdec(TOP_X(41U), TOP_Y(0U), textgui_elements.dispfreq /   1000U, &rs);
 textgui_putdec(TOP_X(45U), TOP_Y(0U), textgui_elements.dispfreq,           &rsnoprn);
 if (prmsg){ conout_addstr("Fps; Audio: "); }
 textgui_putdec(TOP_X(41U), TOP_Y(2U), textgui_elements.aufreq /     1000U, &rs);
 if (prmsg){ conout_addchr('.'); }
 textgui_putdec(TOP_X(45U), TOP_Y(2U), textgui_elements.aufreq,             &rs);
 if (prmsg){ conout_addstr("KHz; "); }

 /* SNES / UZEM keyboard mapping */

 if (textgui_elements.kbuzem){
  textgui_putstr(TOP_X(32U), TOP_Y(0U), &(textgui_kbuzem[0]), &rsnoprn);
  if (prmsg){ conout_addstr("Keys: UZEM "); }
 }else{
  textgui_putstr(TOP_X(32U), TOP_Y(0U), &(textgui_kbsnes[0]), &rsnoprn);
  if (prmsg){ conout_addstr("Keys: SNES "); }
 }

 /* 1 player / 2 players */

 if (textgui_elements.player2){
  textgui_putchar(TOP_X(27U), TOP_Y(0U), '2', &rsnoprn);
  if (prmsg){ conout_addstr("2P "); }
 }else{
  textgui_putchar(TOP_X(27U), TOP_Y(0U), '1', &rsnoprn);
  if (prmsg){ conout_addstr("1P "); }
 }

 /* Frame merging */

 if (textgui_elements.merge){
  textgui_putchar(TOP_X(37U), TOP_Y(0U),  2U, &rsnoprn);
  if (prmsg){ conout_addstr("FrameMerge "); }
 }else{
  textgui_putchar(TOP_X(37U), TOP_Y(0U), ' ', &rsnoprn);
 }

 /* Video capturing */

 if (textgui_elements.capture){
  textgui_putchar(TOP_X(24U), TOP_Y(0U), 13U, &rsnoprn);
  textgui_putchar(TOP_X(25U), TOP_Y(0U), 14U, &rsnoprn);
  if (prmsg){ conout_addstr("Capture "); }
 }else{
  textgui_putchar(TOP_X(24U), TOP_Y(0U), ' ', &rsnoprn);
  textgui_putchar(TOP_X(25U), TOP_Y(0U), ' ', &rsnoprn);
 }

 /* Game & Author name */

 textgui_putstr(TOP_X(6U), TOP_Y(1U), &(textgui_elements.game[0]), &rsnoprn);
 textgui_putstr(TOP_X(6U), TOP_Y(2U), &(textgui_elements.auth[0]), &rsnoprn);


 /* Whisper ports */

 for (i = 0U; i < 2U; i++){
  t = textgui_elements.ports[i];
  textgui_putdec (BOT_X( 8U), BOT_Y(i), t, &rsnoprn);
  textgui_putchar(BOT_X(15U), BOT_Y(i), textgui_tohex(t >> 4), &rsnoprn);
  textgui_putchar(BOT_X(16U), BOT_Y(i), textgui_tohex(     t), &rsnoprn);
  for (j = 0U; j < 8U; j++){
   textgui_putchar(BOT_X(19U + j), BOT_Y(i), ((t >> (7U - j)) & 1U) + '0', &rsnoprn);
  }
 }

 /* WDR interval debug counter */

 textgui_putdecx(BOT_X(36U), BOT_Y(0), textgui_elements.wdrint, &rsnoprn);
 t = textgui_elements.wdrbeg << 1;
 textgui_putchar(BOT_X(32U), BOT_Y(1), textgui_tohex(t >> 12), &rsnoprn);
 textgui_putchar(BOT_X(33U), BOT_Y(1), textgui_tohex(t >>  8), &rsnoprn);
 textgui_putchar(BOT_X(34U), BOT_Y(1), textgui_tohex(t >>  4), &rsnoprn);
 textgui_putchar(BOT_X(35U), BOT_Y(1), textgui_tohex(t >>  0), &rsnoprn);
 t = textgui_elements.wdrend << 1;
 textgui_putchar(BOT_X(38U), BOT_Y(1), textgui_tohex(t >> 12), &rsnoprn);
 textgui_putchar(BOT_X(39U), BOT_Y(1), textgui_tohex(t >>  8), &rsnoprn);
 textgui_putchar(BOT_X(40U), BOT_Y(1), textgui_tohex(t >>  4), &rsnoprn);
 textgui_putchar(BOT_X(41U), BOT_Y(1), textgui_tohex(t >>  0), &rsnoprn);

 textgui_log_draw(noovr);

 if (prmsg){ conout_send(); }
}



/*
** Returns a pointer to the text GUI elements, can be used to update them for
** a later textgui_draw() call.
*/
textgui_struct_t* textgui_getelementptr(void)
{
 return &textgui_elements;
}



/*
** Resets text GUI element values.
*/
void textgui_reset(void)
{
 textgui_elements.cpufreq  = 28636400U;
 textgui_elements.aufreq   = 15734U;
 textgui_elements.dispfreq = 60000U;
 textgui_elements.kbuzem   = FALSE;
 textgui_elements.merge    = FALSE;
 textgui_elements.capture  = FALSE;
 textgui_elements.led      = FALSE;
 textgui_elements.game[0]  = 0U;
 textgui_elements.auth[0]  = 0U;
 textgui_elements.ports[0] = 0U;
 textgui_elements.ports[1] = 0U;
 textgui_logcount = 0U;
 memset(&(textgui_logmsgs[0]), 0, sizeof(textgui_logmsgs));
}
