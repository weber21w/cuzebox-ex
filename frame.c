/*
 *  Emulation frame renderer
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



#include "frame.h"
#include "guicore.h"
#include "filters.h"
#include "textgui.h"
#include "renderpath.h"
#include "cu_esp.h"



/* Maximal number of rows to process in a frame */
#define MAX_ROWS     268U

/* Start sync pulse of picture output */
#define RPULSE_START  19U

/* Number of sync pulses to render */
#define RPULSE_NO    225U

/* Target width of the displayed game image within the GUI buffer */
#define ROW_PIXELS             624U

/* Explicit active video extraction window in Uzebox CPU cycles */
#define ROW_ACTIVE_CYCLES      1440U
#define ROW_BEGINCY            299U

/* Original CUzeBox filtered extraction path (7 source cycles -> 3 output px) */
#define CLASSIC_ROW_BEGINCY    291U
#define CLASSIC_PREVS_WIDTH    ((((ROW_PIXELS / 3U) * 7U)) + 2U)

/* Prev. frame storage line width must cover the largest extraction mode */
#define PREVS_WIDTH            CLASSIC_PREVS_WIDTH


/* Previous frame storage (Uzebox cycles & palette) */
static uint8  frame_prev[RPULSE_NO * PREVS_WIDTH];

/* Collects audio data */
static uint8  frame_audio[MAX_ROWS];

/* Fade-out memory accesses */
static uint8  frame_memf[4096U];

/* Fade-out I/O accesses */
static uint8  frame_iof[256U];

/* Precomputed horizontal map for raw cycle-domain extraction */
static auint  frame_raw_xmap[ROW_PIXELS];
static boole  frame_raw_xmap_init = FALSE;

/* Frame counter to time memory access logic */
static auint  frame_ctr = 0U;

/* Last audio frame was zero? */
static boole  frame_isauz = TRUE;
#ifdef ENABLE_DEBUGGER
static boole  frame_break_hit = FALSE;
static auint  frame_break_addr = 0U;
#endif

/* Previous state of frame merging */
static boole  frame_pmerge = FALSE;


/* Sync: Good signal marker */
static const uint8 frame_syn_ok[5] = {0x00U, 0x00U, 0x80U, 0x00U, 0x00U};
/* Sync: Invalid signal */
static const uint8 frame_syn_in[5] = {0x7FU, 0x7FU, 0x7DU, 0x7FU, 0x7FU};
/* Sync: Left 1 */
static const uint8 frame_syn_l1[5] = {0x00U, 0x4FU, 0x4DU, 0x00U, 0x00U};
/* Sync: Left 2 */
static const uint8 frame_syn_l2[5] = {0x4FU, 0x4FU, 0x4DU, 0x00U, 0x00U};
/* Sync: Left 2+ */
static const uint8 frame_syn_l3[5] = {0x7FU, 0x4FU, 0x4DU, 0x00U, 0x00U};
/* Sync: Right 1 */
static const uint8 frame_syn_r1[5] = {0x00U, 0x00U, 0x4DU, 0x4FU, 0x00U};
/* Sync: Right 2 */
static const uint8 frame_syn_r2[5] = {0x00U, 0x00U, 0x4DU, 0x4FU, 0x4FU};
/* Sync: Right 2+ */
static const uint8 frame_syn_r3[5] = {0x00U, 0x00U, 0x4DU, 0x4FU, 0x7FU};
/* Sync: Good line count (as VSync separator) */
static const uint8 frame_syn_ng[5] = {0x80U, 0x80U, 0x80U, 0x80U, 0x80U};
/* Sync: Bad line count (as VSync separator) */
static const uint8 frame_syn_nf[5] = {0x4FU, 0x4FU, 0x4FU, 0x4FU, 0x4FU};



/*
** Clears a rectangular region to the given color
*/
static void frame_clear(uint32* pix, auint ptc,
                        auint x, auint y, auint w, auint h, uint32 col)
{
 auint i;
 auint j;
 auint p;

 for (j = 0U; j < h; j++){
  p = ((y + j) * ptc) + x;
  for (i = 0U; i < w; i++){
   pix[p + i] = col;
  }
 }
}


static void frame_prepare_classic_palette(uint32* dst, uint32 const* src)
{
 guicore_pixfmt_t pixfmt;
 auint i;
 auint r;
 auint g;
 auint b;
 uint32 col;

 guicore_getpixfmt(&pixfmt);
 for (i = 0U; i < 256U; ++i){
  col = src[i];
  r = ((col >> pixfmt.rsh) & 0xFFU) >> 4;
  g = ((col >> pixfmt.gsh) & 0xFFU) >> 4;
  b = ((col >> pixfmt.bsh) & 0xFFU) >> 4;
  dst[i] = (r << pixfmt.rsh) | (g << pixfmt.gsh) | (b << pixfmt.bsh);
 }
}



static void frame_init_raw_xmap(void)
{
 auint dp;
 auint sp;
 if (frame_raw_xmap_init){ return; }
 for (dp = 0U; dp < ROW_PIXELS; dp ++){
  sp = (((dp * ROW_ACTIVE_CYCLES) + (ROW_PIXELS >> 1)) / ROW_PIXELS);
  if (sp >= ROW_ACTIVE_CYCLES){
   sp = ROW_ACTIVE_CYCLES - 1U;
  }
  frame_raw_xmap[dp] = sp;
 }
 frame_raw_xmap_init = TRUE;
}

static auint frame_get_render_path(void)
{
 auint mode = guicore_get_render_path();
 if (mode > RENDER_PATH_STAGED){
  mode = RENDER_PATH_STAGED;
 }
 return mode;
}

static boole frame_use_classic_render(void)
{
 return RENDER_PATH_IS_CLASSIC(frame_get_render_path());
}

/*
** Renders a line by explicitly sampling the active video window in the
** cycle domain without applying any built-in blur / shrink filter.
** dest is the target line to fill; len is in target pixels.
*/
static void frame_line32_raw(uint32* dest,
                             uint8 const* src, auint off, auint len,
                             uint32 const* pal)
{
 auint dp;
 auint sp;

 frame_init_raw_xmap();
 for (dp = 0U; dp < len; dp ++){
  sp = off + frame_raw_xmap[dp];
  dest[dp] = pal[src[sp]];
 }
}

static void frame_line32_classic(uint32* dest,
                                 uint8 const* src, auint off, auint len,
                                 uint32 const* pal)
{
 auint  sp = off;
 auint  dp;
 auint  px;
 auint  t0, t1, t2, t3, t4;

 t0 = pal[src[sp - 1U]];
 t1 = pal[src[sp + 0U]];
 for (dp = 0U; dp < ((len / 3U) * 3U); dp += 3U)
 {
  t2 = pal[src[sp + 1U]];
  t3 = pal[src[sp + 2U]];
  t4 = pal[src[sp + 3U]];
  px = (t0 * 4U) + (t1 * 4U) + (t2 * 4U) + (t3 * 4U) + (t4 * 1U);
  dest [dp + 0U] = px;
  t0 = pal[src[sp + 4U]];
  t1 = pal[src[sp + 5U]];
  px = (t2 * 2U) + (t3 * 4U) + (t4 * 5U) + (t0 * 4U) + (t1 * 2U);
  dest [dp + 1U] = px;
  t2 = pal[src[sp + 6U]];
  t3 = pal[src[sp + 7U]];
  px = (t4 * 1U) + (t0 * 4U) + (t1 * 4U) + (t2 * 4U) + (t3 * 4U);
  dest [dp + 2U] = px;
  sp += 7U;
  t0 = t2;
  t1 = t3;
 }
}



/*
** Renders a line by explicitly sampling the active video window in the cycle
** domain while merging the previous frame at the same cycle positions.
** dest is the target line to fill; len is in target pixels.
*/
static void frame_line32_copy_raw(uint32* dest,
                                  uint8* dsu,
                                  uint8 const* src, auint off, auint len,
                                  uint32 const* pal)
{
 auint  dp;
 auint  sp;
 uint32 c0;
 uint32 c1;

 frame_init_raw_xmap();
 for (dp = 0U; dp < len; dp ++){
  sp = frame_raw_xmap[dp];
  c0 = pal[src[off + sp]];
  c1 = pal[dsu[sp]];
  dest[dp] = (((c0 & 0xFEFEFEFEU) >> 1) +
              ((c1 & 0xFEFEFEFEU) >> 1));
 }
 memcpy(dsu, src + off, ROW_ACTIVE_CYCLES);
}

static void frame_line32_copy_classic(uint32* dest,
                                      uint8* dsu,
                                      uint8 const* src, auint off, auint len,
                                      uint32 const* pal)
{
 auint  sp = off;
 auint  up = 1U;
 auint  dp;
 auint  px;
 auint  t0, t1, t2, t3, t4;
 auint  u0, u1, u2, u3, u4;

 t0 = pal[src[sp - 1U]];
 u0 = pal[dsu[up - 1U]];
 t1 = pal[src[sp + 0U]];
 u1 = pal[dsu[up + 0U]];
 dsu  [up - 1U] = src[sp - 1U];
 dsu  [up + 0U] = src[sp + 0U];
 for (dp = 0U; dp < ((len / 3U) * 3U); dp += 3U)
 {
  t2 = pal[src[sp + 1U]];
  u2 = pal[dsu[up + 1U]];
  t3 = pal[src[sp + 2U]];
  u3 = pal[dsu[up + 2U]];
  t4 = pal[src[sp + 3U]];
  u4 = pal[dsu[up + 3U]];
  px = (t0 * 2U) + (t1 * 2U) + (t2 * 2U) + (t3 * 2U) + (t4 * 1U) +
       (u0 * 2U) + (u1 * 2U) + (u2 * 2U) + (u3 * 2U) + (u4 * 0U);
  dest [dp + 0U] = px;
  dsu  [up + 1U] = src[sp + 1U];
  dsu  [up + 2U] = src[sp + 2U];
  dsu  [up + 3U] = src[sp + 3U];
  t0 = pal[src[sp + 4U]];
  u0 = pal[dsu[up + 4U]];
  t1 = pal[src[sp + 5U]];
  u1 = pal[dsu[up + 5U]];
  px = (t2 * 1U) + (t3 * 2U) + (t4 * 2U) + (t0 * 2U) + (t1 * 1U) +
       (u2 * 1U) + (u3 * 2U) + (u4 * 3U) + (u0 * 2U) + (u1 * 1U);
  dest [dp + 1U] = px;
  dsu  [up + 4U] = src[sp + 4U];
  dsu  [up + 5U] = src[sp + 5U];
  t2 = pal[src[sp + 6U]];
  u2 = pal[dsu[up + 6U]];
  t3 = pal[src[sp + 7U]];
  u3 = pal[dsu[up + 7U]];
  px = (t4 * 1U) + (t0 * 2U) + (t1 * 2U) + (t2 * 2U) + (t3 * 2U) +
       (u4 * 0U) + (u0 * 2U) + (u1 * 2U) + (u2 * 2U) + (u3 * 2U);
  dest [dp + 2U] = px;
  dsu  [up + 6U] = src[sp + 6U];
  dsu  [up + 7U] = src[sp + 7U];
  sp += 7U;
  up += 7U;
  t0 = t2;
  u0 = u2;
  t1 = t3;
  u1 = u3;
 }
}



/*
** Copies lines of the Uzebox source to the previous frame storage. Should be
** used in dropped frames to keep frame merging operational.
*/
static void frame_line_dropcopy_raw(uint8* dsu,
                                    uint8 const* src, auint off, auint len)
{
 (void)len;
 memcpy(dsu, src + off, ROW_ACTIVE_CYCLES);
}

static void frame_line_dropcopy_classic(uint8* dsu,
                                        uint8 const* src, auint off, auint len)
{
 auint  sp = off;
 auint  up = 1U;
 auint  dp;

 dsu  [up - 1U] = src[sp - 1U];
 dsu  [up + 0U] = src[sp + 0U];
 for (dp = 0U; dp < ((len / 3U) * 3U); dp += 3U)
 {
  dsu  [up + 1U] = src[sp + 1U];
  dsu  [up + 2U] = src[sp + 2U];
  dsu  [up + 3U] = src[sp + 3U];
  dsu  [up + 4U] = src[sp + 4U];
  dsu  [up + 5U] = src[sp + 5U];
  dsu  [up + 6U] = src[sp + 6U];
  dsu  [up + 7U] = src[sp + 7U];
  sp += 7U;
  up += 7U;
 }
}



/*
** Plots a pixel box for the frameinfo display. Uses the Uzebox palette.
*/
static void frame_pixbox(uint32* pix, auint ptc,
                         auint x, auint y, uint8 col,
                         uint32 const* pal)
{
 auint  pos = (x << 1) + (y * ptc);
 uint32 c32 = ((pal[col] & 0xF8F8F8F8U) >> 3) * 6U;

 pix[pos + 0U] = c32;
 pix[pos + 1U] = c32;
}



/*
** Plots a sync signal notification (5 pixel boxes wide)
*/
static void frame_pixsyn(uint32* pix, auint ptc,
                         auint x, auint y, uint8 const* col,
                         uint32 const* pal)
{
 auint i;
 for (i = 0U; i < 5U; i++){
  frame_pixbox(pix, ptc, x + i, y, col[i], pal);
 }
}



/*
** Plots a sync signal notification by the report
*/
static void frame_synrep(uint32* pix, auint ptc,
                         auint x, auint y, auint rep,
                         uint32 const* pal)
{
 if       (rep == 0U){
  frame_pixsyn(pix, ptc, x, y, frame_syn_ok, pal);
 }else if (rep == CU_NOSYNC){
  frame_pixsyn(pix, ptc, x, y, frame_syn_in, pal);
 }else if ((asint)(rep) == -1){
  frame_pixsyn(pix, ptc, x, y, frame_syn_l1, pal);
 }else if ((asint)(rep) == -2){
  frame_pixsyn(pix, ptc, x, y, frame_syn_l2, pal);
 }else if ((asint)(rep) <  -2){
  frame_pixsyn(pix, ptc, x, y, frame_syn_l3, pal);
 }else if ((asint)(rep) ==  1){
  frame_pixsyn(pix, ptc, x, y, frame_syn_r1, pal);
 }else if ((asint)(rep) ==  2){
  frame_pixsyn(pix, ptc, x, y, frame_syn_r2, pal);
 }else{
  frame_pixsyn(pix, ptc, x, y, frame_syn_r3, pal);
 }
}




/*
** Attempts to run a frame of emulation (262 display rows ending with a
** CU_GET_FRAME result). If no proper sync was generated, it still attempts
** to get about a frame worth of lines. If drop is TRUE, the render of the
** frame is dropped which can be used to help slow targets. If merge is TRUE,
** frames following each other are averaged (weak motion blur, cancels out
** flickers). Returns the number of rows generated, which is also the number
** of samples within the audio buffer (normally 262).
*/
auint frame_run(boole drop, boole merge)
{
 /* Keep host endpoint queues moving independently of AVR UART polling. */
 cu_esp_endpoint_host_tick();
 auint           ret;
 uint32 const*   pal = guicore_getpalette();
 uint32          pal_classic[256];
 uint32*         pix = guicore_getpixbuf();
 auint           ptc = guicore_getpitch();
 auint           row;
 uint8*          mem;
 auint           i;
 auint           j;
 auint           k;
 auint           adr;
 auint           col;
 boole           gonly = ((guicore_getflags() & GUICORE_GAMEONLY) != 0U);
 boole           use_classic = frame_use_classic_render();
 cu_row_t const* erowd;
 cu_frameinfo_t const* finfo;

 if (use_classic){
  frame_prepare_classic_palette(pal_classic, pal);
 }

 /* Clear buffer. Maybe later will implement some more optimal solution here
 ** to avoid this full clear, but it is not known which lines the emulator
 ** would produce. */

 if (!drop){
  if (!gonly){
   frame_clear(pix, ptc,  0U,  0U, 640U, 280U, pal[0]);
  }else{
   frame_clear(pix, ptc, 10U, 18U, 620U, 230U, pal[0]);
  }
 }

 /* Shift whole display a little down to give more clearance to the text info
 ** on the top */

 pix = pix + (1U * ptc);

 /* Build the frame */

#ifdef ENABLE_DEBUGGER
 frame_break_hit = FALSE;
 frame_break_addr = 0U;
#endif
 row = 0U;

 while (row < MAX_ROWS){

  ret = cu_avr_run();

#ifdef ENABLE_DEBUGGER
  if ((ret & CU_BREAK) != 0U){
   frame_break_hit = TRUE;
   frame_break_addr = cu_avr_get_state()->pc & 0x7FFFU;
   break;
  }
#endif

  if ((ret & CU_GET_ROW) != 0U){

   erowd = cu_avr_get_row();
   frame_audio[row] = (uint8)(erowd->sample);
   if ( ((erowd->pno) >=  (RPULSE_START            )) &&
        ((erowd->pno) <   (RPULSE_START + RPULSE_NO)) ){

    if (!merge){

     if (!drop){
      if (use_classic){
       frame_line32_classic(
           pix + (((erowd->pno) * ptc) + 8U),
           &(erowd->pixels[0]),
           CLASSIC_ROW_BEGINCY,
           ROW_PIXELS,
           pal_classic);
      }else{
       frame_line32_raw(
           pix + (((erowd->pno) * ptc) + 8U),
           &(erowd->pixels[0]),
           ROW_BEGINCY,
           ROW_PIXELS,
           pal);
      }
     }

    }else{

     if (!frame_pmerge){ /* Merging is just turned on: buffer uninitialized */

      if (!drop){
       if (use_classic){
        frame_line32_classic(
            pix + (((erowd->pno) * ptc) + 8U),
            &(erowd->pixels[0]),
            CLASSIC_ROW_BEGINCY,
            ROW_PIXELS,
            pal_classic);
       }else{
        frame_line32_raw(
            pix + (((erowd->pno) * ptc) + 8U),
            &(erowd->pixels[0]),
            ROW_BEGINCY,
            ROW_PIXELS,
            pal);
       }
      }
      if (use_classic){
       frame_line_dropcopy_classic(
           &(frame_prev[((erowd->pno) - RPULSE_START) * PREVS_WIDTH]),
           &(erowd->pixels[0]),
           CLASSIC_ROW_BEGINCY,
           ROW_PIXELS);
      }else{
       frame_line_dropcopy_raw(
           &(frame_prev[((erowd->pno) - RPULSE_START) * PREVS_WIDTH]),
           &(erowd->pixels[0]),
           ROW_BEGINCY,
           ROW_PIXELS);
      }

     }else{              /* Previous frame buffer is present */

      if (!drop){
       if (use_classic){
        frame_line32_copy_classic(
            pix + (((erowd->pno) * ptc) + 8U),
            &(frame_prev[((erowd->pno) - RPULSE_START) * PREVS_WIDTH]),
            &(erowd->pixels[0]),
            CLASSIC_ROW_BEGINCY,
            ROW_PIXELS,
            pal_classic);
       }else{
        frame_line32_copy_raw(
            pix + (((erowd->pno) * ptc) + 8U),
            &(frame_prev[((erowd->pno) - RPULSE_START) * PREVS_WIDTH]),
            &(erowd->pixels[0]),
            ROW_BEGINCY,
            ROW_PIXELS,
            pal);
       }
      }else{
       if (use_classic){
        frame_line_dropcopy_classic(
            &(frame_prev[((erowd->pno) - RPULSE_START) * PREVS_WIDTH]),
            &(erowd->pixels[0]),
            CLASSIC_ROW_BEGINCY,
            ROW_PIXELS);
       }else{
        frame_line_dropcopy_raw(
            &(frame_prev[((erowd->pno) - RPULSE_START) * PREVS_WIDTH]),
            &(erowd->pixels[0]),
            ROW_BEGINCY,
            ROW_PIXELS);
       }
      }

     }

    }

   }
   row ++;

  }

  if ((ret & CU_GET_FRAME) != 0U){ break; }

 }

 ret = row;
 frame_pmerge = merge;

 /* Manage the audio result */

 /* If there was a sync error due to producing too few rows, pad the audio
 ** buffer so it remains tolerable */

 if (row != 0U){
  i = frame_audio[row - 1U];
 }else{
  i = 0U;
 }
 while (row < MAX_ROWS){
  frame_audio[row] = i;
  row ++;
 }

 /* Check if the entire audio buffer is zero. If so, assume that the game
 ** didn't produce any audio yet. */

 i = 0U;
 for (row = 0U; row < MAX_ROWS; row ++){
  i += frame_audio[row];
 }
 if (i == 0U){ frame_isauz = TRUE; }

 /* If no audio was produced, replace all 0x00U samples to 0x80U to prevent
 ** audio distortion on the user's side (by applying that constant DC on the
 ** output). */

 if (frame_isauz){
  for (row = 0U; row < MAX_ROWS; row ++){
   if (frame_audio[row] != 0U){ break; } /* Audio output starts */
   frame_audio[row] = 0x80U;
  }
 }
 if (i != 0U){ frame_isauz = FALSE; } /* There was valid audio in the original contents */

 /* Fill in other areas providing info (only if they are displayed) */

 if (!gonly){

  /* Produce sync info sidebar */

  if (!drop){
   finfo = cu_avr_get_frameinfo();

   for (row =   0U; row < 252U; row ++){ /* Display pulses */
    frame_synrep(pix, ptc,   0U, row,      finfo->pulse[row].rise, pal);
    frame_synrep(pix, ptc, 315U, row,      finfo->pulse[row].fall, pal);
   }
   if (finfo->rowcdif == 0U){            /* Row count separator */
    frame_pixsyn(pix, ptc,   0U, 252U, frame_syn_ng, pal);
    frame_pixsyn(pix, ptc, 315U, 252U, frame_syn_ng, pal);
   }else{
    frame_pixsyn(pix, ptc,   0U, 252U, frame_syn_nf, pal);
    frame_pixsyn(pix, ptc, 315U, 252U, frame_syn_nf, pal);
   }
   for (row = 252U; row < 271U; row ++){ /* VSync pulses */
    frame_synrep(pix, ptc,   0U, row + 1U, finfo->pulse[row].rise, pal);
    frame_synrep(pix, ptc, 315U, row + 1U, finfo->pulse[row].fall, pal);
   }
  }

  /* Produce memory access info blocks */

  mem = cu_avr_get_meminfo();

  for (k = 0U; k < 16U; k ++){
   for (j = 0U; j < 16U; j ++){
    for (i = 0U; i < 16U; i ++){
     adr = (((k + 1U) & 0xFU) << 8) |
           (j << 4) |
           (i     );
     if ((frame_ctr & 0x7U) == 0U){
      if ((frame_memf[adr] & 0x07U) != 0U){ frame_memf[adr] -= 0x01U; }
      if ((frame_memf[adr] & 0x38U) != 0U){ frame_memf[adr] -= 0x08U; }
      if ((mem[adr]) & CU_MEM_R){ frame_memf[adr] |= 0x38U; }
      if ((mem[adr]) & CU_MEM_W){ frame_memf[adr] |= 0x07U; }
      mem[adr] = 0U; /* Clear cell's flags */
     }
     if (!drop){
      col = 0x80U | frame_memf[adr];
      frame_pixbox(pix, ptc, 36U + (k * 17U) + j, 248U + i, col, pal);
     }
    }
   }
  }

  /* Produce I/O access info blocks */

  mem = cu_avr_get_ioinfo();

  for (j = 0U; j < 16U; j ++){
   for (i = 0U; i < 16U; i ++){
    adr = (j << 4) |
          (i     );
    if ((frame_ctr & 0x7U) == 0U){
     if ((frame_iof[adr] & 0x07U) != 0U){ frame_iof[adr] -= 0x01U; }
     if ((frame_iof[adr] & 0x38U) != 0U){ frame_iof[adr] -= 0x08U; }
     if ((mem[adr]) & CU_MEM_R){ frame_iof[adr] |= 0x38U; }
     if ((mem[adr]) & CU_MEM_W){ frame_iof[adr] |= 0x07U; }
     mem[adr] = 0U; /* Clear cell's flags */
    }
    if (!drop){
     col = 0x80U | frame_iof[adr];
     frame_pixbox(pix, ptc, 12U + j, 248U + i, col, pal);
    }
   }
  }

 }

 frame_ctr ++;

 /* Add text GUI elements */

 textgui_draw(drop || gonly, drop);

 return ret;
}



/*
** Returns whether execution stopped due to a debugger breakpoint during the
** last frame_run() call.
*/
boole frame_breakpoint_hit(boole clear, auint* out_word_addr)
{
#ifdef ENABLE_DEBUGGER
 boole ret = frame_break_hit;
 if (out_word_addr != NULL){ *out_word_addr = frame_break_addr; }
 if (clear){
  frame_break_hit = FALSE;
  frame_break_addr = 0U;
 }
 return ret;
#else
 (void)clear;
 if (out_word_addr != NULL){ *out_word_addr = 0U; }
 return FALSE;
#endif
}


/*
** Returns the received unsigned audio samples of the frame (normally 262).
*/
uint8 const* frame_getaudio(void)
{
 return &(frame_audio[0]);
}
