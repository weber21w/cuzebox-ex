/*
 *  Lightgun emulation
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

#ifndef CU_GUN_H
#define CU_GUN_H

#include "types.h"

/*
** The Uzebox lightgun uses the unused upper bits of the standard SNES serial
** controller bitmap. In logical active-high form:
**   bits 12..13 = trigger / light
**   bits 14..15 = gun signature (always 1,1 when a gun is attached)
*/

#define CU_GUN_MAX                2U
#define CU_GUN_APERTURE_RADIUS    1
#define CU_GUN_APERTURE_SIZE      ((CU_GUN_APERTURE_RADIUS * 2) + 1)
#define CU_GUN_LUMA_THRESHOLD     128U
#define CU_GUN_HIT_THRESHOLD      2U

#define CU_GUN_TRIGGER   ((auint)1U << 12)
#define CU_GUN_LIGHT     ((auint)1U << 13)
#define CU_GUN_SIG0      ((auint)1U << 14)
#define CU_GUN_SIG1      ((auint)1U << 15)
#define CU_GUN_SIGNATURE (CU_GUN_SIG0 | CU_GUN_SIG1)

typedef struct{
	auint enabled[CU_GUN_MAX];
	auint light[CU_GUN_MAX];
	auint trigger[CU_GUN_MAX];
	sint32 x[CU_GUN_MAX];
	sint32 y[CU_GUN_MAX];
	auint latched[CU_GUN_MAX];
} cu_state_gun_t;

void  cu_gun_get_state(cu_state_gun_t* state);
void  cu_gun_set_state(cu_state_gun_t const* state);

void  cu_gun_reset(void);
void  cu_gun_update(void);
void  cu_gun_latch_event(void);

auint cu_gun_any_enabled(void);
auint cu_gun_sense(auint g);
auint cu_gun_sense_position(sint32 wx, sint32 wy);
auint cu_gun_build_packet(sint32 wx, sint32 wy, auint trigger_down, auint* light_out);
auint cu_gun_get_enabled(auint g);
void  cu_gun_set_enabled(auint val, auint g);
auint cu_gun_get_trigger(auint g);
void  cu_gun_set_trigger(auint val, auint g);
auint cu_gun_get_light(auint g);
void  cu_gun_set_light(auint val, auint g);
void  cu_gun_get_position(sint32* mx, sint32* my, auint g);
void  cu_gun_set_position(sint32 mx, sint32 my, auint g);
auint cu_gun_get_x(auint g);
auint cu_gun_get_y(auint g);
auint cu_gun_get_latched(auint g);

#endif
