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



#ifndef FRAME_H
#define FRAME_H



#include "cu_avr.h"



/*
** Attempts to run a frame of emulation (262 display rows ending with a
** CU_GET_FRAME result). If no proper sync was generated, it still attempts
** to get about a frame worth of lines. If drop is TRUE, the render of the
** frame is dropped which can be used to help slow targets. If merge is TRUE,
** frames following each other are averaged (weak motion blur, cancels out
** flickers). Returns the number of rows generated, which is also the number
** of samples within the audio buffer (normally 262).
*/
auint frame_run(boole drop, boole merge);
/* Discard shifted previous-frame samples after a display timing change. */
void frame_invalidate_merge(void);
void frame_set_area_weighted(boole enabled);
/* Latest completed cycle-domain field, prior to sampling, filters or blending.
 * Each DAC clock is one column; progressive rows repeat five times. Woven
 * interlaced rows use nearest-neighbor 5/2 scaling to retain the same aspect.
 * The caller owns the returned native-pixel-format buffer. */
boole frame_capture_cycle_pixels(uint32** pixels,auint* width,auint* height);
/* Configurable cycle export: width 1..2880, Y 1..10, sampling
 * 0=nearest, 1=area RGB average, 2=linear. Rotation follows scaling. */
boole frame_capture_cycle_pixels_ex(uint32** pixels,auint* width,auint* height,auint x_width,auint y_scale,auint sampling);
/* Bounds in pulse-counter rows; a reset/no completed field has valid=FALSE. */
void frame_capture_geometry(auint* first_row,auint* rows,boole* valid,boole* interlaced);
void frame_capture_reset(void);


/*
** Returns whether execution stopped due to a debugger breakpoint during the
** last frame_run() call. Passing TRUE clears the latched state.
*/
boole frame_breakpoint_hit(boole clear, auint* out_word_addr);


/*
** Returns the received unsigned audio samples of the frame (normally 262).
*/
uint8 const* frame_getaudio(void);


#endif
