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

#include "cu_gun.h"
#include "guicore.h"
#include <string.h>

static auint  cu_gun_enabled[CU_GUN_MAX];
static auint  cu_gun_light[CU_GUN_MAX];
static sint32 cu_gun_x[CU_GUN_MAX];
static sint32 cu_gun_y[CU_GUN_MAX];
static auint  cu_gun_trigger[CU_GUN_MAX];
static auint  cu_gun_latched_data[CU_GUN_MAX];

void cu_gun_get_state(cu_state_gun_t* state)
{
 if (state == NULL){ return; }
 memset(state, 0, sizeof(*state));
 memcpy(state->enabled, cu_gun_enabled, sizeof(cu_gun_enabled));
 memcpy(state->light, cu_gun_light, sizeof(cu_gun_light));
 memcpy(state->trigger, cu_gun_trigger, sizeof(cu_gun_trigger));
 memcpy(state->x, cu_gun_x, sizeof(cu_gun_x));
 memcpy(state->y, cu_gun_y, sizeof(cu_gun_y));
 memcpy(state->latched, cu_gun_latched_data, sizeof(cu_gun_latched_data));
}

void cu_gun_set_state(cu_state_gun_t const* state)
{
 if (state == NULL){ return; }
 memcpy(cu_gun_enabled, state->enabled, sizeof(cu_gun_enabled));
 memcpy(cu_gun_light, state->light, sizeof(cu_gun_light));
 memcpy(cu_gun_trigger, state->trigger, sizeof(cu_gun_trigger));
 memcpy(cu_gun_x, state->x, sizeof(cu_gun_x));
 memcpy(cu_gun_y, state->y, sizeof(cu_gun_y));
 memcpy(cu_gun_latched_data, state->latched, sizeof(cu_gun_latched_data));
}

static auint cu_gun_trigger_mask(auint g)
{
 (void)g;
 return SDL_BUTTON_LMASK;
}

static int cu_gun_clampi(int v, int lo, int hi)
{
 if (v < lo){ return lo; }
 if (v > hi){ return hi; }
 return v;
}

static auint cu_gun_pixel_luma(uint32 pix)
{
 auint r  = (pix >> 16) & 0xFFU;
 auint gr = (pix >>  8) & 0xFFU;
 auint b  = (pix      ) & 0xFFU;

 return ((r * 54U) + (gr * 183U) + (b * 19U)) >> 8;
}

static auint cu_gun_sense_window_position(sint32 wx, sint32 wy)
{
 int   sx;
 int   sy;
 int   rectx;
 int   recty;
 auint rectw;
 auint recth;
 int   minx;
 int   maxx;
 int   miny;
 int   maxy;
 int   dx;
 int   dy;
 auint hits = 0U;

 if (!guicore_window_to_source((int)wx, (int)wy, &sx, &sy)){
  return 0U;
 }

 guicore_get_source_bounds(&rectx, &recty, &rectw, &recth);
 if ((rectw == 0U) || (recth == 0U)){
  return 0U;
 }

 minx = rectx;
 miny = recty;
 maxx = rectx + (int)rectw - 1;
 maxy = recty + (int)recth - 1;

 for (dy = -CU_GUN_APERTURE_RADIUS; dy <= CU_GUN_APERTURE_RADIUS; ++dy){
  int py = cu_gun_clampi(sy + dy, miny, maxy);
  for (dx = -CU_GUN_APERTURE_RADIUS; dx <= CU_GUN_APERTURE_RADIUS; ++dx){
   int px = cu_gun_clampi(sx + dx, minx, maxx);
   uint32 pix = guicore_get_source_pixel((auint)px, (auint)py);
   if (cu_gun_pixel_luma(pix) >= CU_GUN_LUMA_THRESHOLD){
    ++hits;
    if (hits >= CU_GUN_HIT_THRESHOLD){
     return 1U;
    }
   }
  }
 }

 return 0U;
}

auint cu_gun_any_enabled(void)
{
 auint g;

 for (g = 0U; g < CU_GUN_MAX; ++g){
  if (cu_gun_enabled[g] != 0U){
   return 1U;
  }
 }
 return 0U;
}

void cu_gun_reset(void)
{
 auint g;
 for (g = 0U; g < CU_GUN_MAX; ++g){
  cu_gun_enabled[g] = 0U;
  cu_gun_light[g] = 0U;
  cu_gun_x[g] = 0;
  cu_gun_y[g] = 0;
  cu_gun_trigger[g] = 0U;
  cu_gun_latched_data[g] = 0U;
 }
}

auint cu_gun_get_enabled(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 return cu_gun_enabled[g];
}

void cu_gun_set_enabled(auint val, auint g)
{
 if (g >= CU_GUN_MAX){ return; }
 if ((g != 0U) && (val != 0U)){
  return;
 }
 cu_gun_enabled[g] = (val != 0U) ? 1U : 0U;
}

auint cu_gun_get_trigger(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 return cu_gun_trigger[g];
}

void cu_gun_set_trigger(auint val, auint g)
{
 if (g >= CU_GUN_MAX){ return; }
 cu_gun_trigger[g] = (val != 0U) ? 1U : 0U;
}

void cu_gun_set_light(auint val, auint g)
{
 if (g >= CU_GUN_MAX){ return; }
 cu_gun_light[g] = (val != 0U) ? 1U : 0U;
}

auint cu_gun_get_light(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 return cu_gun_light[g];
}

void cu_gun_get_position(sint32* mx, sint32* my, auint g)
{
 if (mx == NULL || my == NULL || g >= CU_GUN_MAX){ return; }
 *mx = cu_gun_x[g];
 *my = cu_gun_y[g];
}

void cu_gun_set_position(sint32 mx, sint32 my, auint g)
{
 if (g >= CU_GUN_MAX){ return; }
 cu_gun_x[g] = mx;
 cu_gun_y[g] = my;
}

auint cu_gun_get_x(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 return (auint)cu_gun_x[g];
}

auint cu_gun_get_y(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 return (auint)cu_gun_y[g];
}

auint cu_gun_get_latched(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 return cu_gun_latched_data[g];
}

void cu_gun_update(void)
{
 int   mx;
 int   my;
 Uint32 buttons;
 auint g;

 if (cu_gun_any_enabled() == 0U){
  return;
 }

 buttons = SDL_GetMouseState(&mx, &my);
 for (g = 0U; g < CU_GUN_MAX; ++g){
  if (!cu_gun_enabled[g]){ continue; }
  cu_gun_x[g] = (sint32)mx;
  cu_gun_y[g] = (sint32)my;
  cu_gun_trigger[g] = ((buttons & cu_gun_trigger_mask(g)) != 0U) ? 1U : 0U;
 }
}

auint cu_gun_sense_position(sint32 wx, sint32 wy)
{
 return cu_gun_sense_window_position(wx, wy);
}

auint cu_gun_build_packet(sint32 wx, sint32 wy, auint trigger_down, auint* light_out)
{
 auint light;
 auint packet;

 light = cu_gun_sense_window_position(wx, wy);
 packet = CU_GUN_SIGNATURE;
 if (trigger_down != 0U){
  packet |= CU_GUN_TRIGGER;
 }
 if (light != 0U){
  packet |= CU_GUN_LIGHT;
 }
 if (light_out != NULL){
  *light_out = light;
 }
 return packet;
}

auint cu_gun_sense(auint g)
{
 if (g >= CU_GUN_MAX){ return 0U; }
 if (!cu_gun_enabled[g]){ return 0U; }
 return cu_gun_sense_window_position(cu_gun_x[g], cu_gun_y[g]);
}

void cu_gun_latch_event(void)
{
 auint g;

 if (cu_gun_any_enabled() == 0U){
  return;
 }

 cu_gun_update();
 for (g = 0U; g < CU_GUN_MAX; ++g){
  if (!cu_gun_enabled[g]){
   cu_gun_light[g] = 0U;
   cu_gun_latched_data[g] = 0U;
   continue;
  }

  cu_gun_latched_data[g] = cu_gun_build_packet(cu_gun_x[g], cu_gun_y[g], cu_gun_trigger[g], &cu_gun_light[g]);
 }
}
