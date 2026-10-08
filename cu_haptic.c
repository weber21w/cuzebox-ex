/*
 *  Haptic Feedback Emulation(AKA Rumble)
 *
 *  Copyright (C) 2016 - 2025
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

#include "cu_haptic.h"
#include "cu_multitap.h"
#include "cu_vdev.h"
#include "cu_avr.h"
#include <string.h>

/* Detect SDL major version if headers are present */
#if defined(SDL_MAJOR_VERSION) && (SDL_MAJOR_VERSION >= 2)
# define CU_HAP_HAVE_SDL2 1
#else
# define CU_HAP_HAVE_SDL2 0
#endif

#define CU_HAP_HOST_CACHE_MAX 8U
#define CU_HAP_PORTS          2U
#define CU_HAP_DATA_P1        0x01U
#define CU_HAP_DATA_P2        0x02U
#define CU_HAP_LATCH          0x04U
#define CU_HAP_CLOCK          0x08U

#define CU_HAP_STOP      0U
#define CU_HAP_TX_START  1U
#define CU_HAP_TX_READY  2U

#define CU_HAP_HOLD_MS   60000U

typedef struct{
	boole             ena;
	auint             bound_index; /* Preferred SDL device index, -1U = auto */
#if CU_HAP_HAVE_SDL2
	SDL_GameController* gc;
	SDL_Joystick*       js;
	SDL_Haptic*         hp;
	boole               has_gc_rumble;
	boole               has_js_rumble;
	auint               opened_index;
#endif
	cu_state_hap_t      pub; /* Public state for dumps */
} cu_hap_slot_t;

typedef struct{
	auint state;
	auint clock;
	auint data_in;
	auint data_out;
	uint8 rx_buf[2];
	auint rx_count;
	boole payload_applied;
} cu_hap_bus_t;

static struct{
	boole         ena;
	cu_hap_slot_t slot[CU_HAP_MAX];
	cu_hap_slot_t host_slot[CU_HAP_HOST_CACHE_MAX];
	cu_hap_bus_t port[CU_HAP_PORTS];
} cu_hap_ctx;

static void slot_reset(cu_hap_slot_t* s)
{
	memset(s, 0, sizeof(*s));
	s->bound_index = (auint)(-1);
#if CU_HAP_HAVE_SDL2
	s->gc = 0;
	s->js = 0;
	s->hp = 0;
	s->has_gc_rumble = 0;
	s->has_js_rumble = 0;
	s->opened_index = (auint)(-1);
#endif
	s->pub.last_mask = 0U;
	s->pub.last_id = 0U;
	s->pub.last_state = CU_HAP_STATE_OFF;
}

static void port_reset(cu_hap_bus_t* p)
{
	memset(p, 0, sizeof(*p));
	p->state = CU_HAP_STOP;
	p->clock = 0U;
	p->data_in = 0U;
	p->data_out = 0U;
	p->rx_count = 0U;
	p->payload_applied = FALSE;
}

#if CU_HAP_HAVE_SDL2
static void slot_close_device(cu_hap_slot_t* s)
{
	if (s->hp){
		SDL_HapticClose(s->hp);
		s->hp = 0;
	}
	if (s->gc){
		SDL_GameControllerClose(s->gc);
		s->gc = 0;
		s->js = 0;
	}else if (s->js){
		SDL_JoystickClose(s->js);
		s->js = 0;
	}
	s->has_gc_rumble = 0;
	s->has_js_rumble = 0;
	s->opened_index = (auint)(-1);
}

static void slot_open_device(cu_hap_slot_t* s)
{
	if (s->gc || s->js){
		if ((s->bound_index == (auint)(-1)) || (s->opened_index == s->bound_index)){
			return;
		}
		slot_close_device(s);
	}

	int target = (int)s->bound_index;
	if ((target < 0) || (target >= SDL_NumJoysticks())){
		target = -1;
		for (int i = 0, n = SDL_NumJoysticks(); i < n; i++){
			if (SDL_IsGameController(i)){
				target = i;
				break;
			}
			if (target < 0){
				target = i;
			}
		}
	}
	if (target < 0){
		return;
	}

	if (SDL_IsGameController(target)){
		s->gc = SDL_GameControllerOpen(target);
		if (s->gc != NULL){
# if SDL_VERSION_ATLEAST(2,0,9)
			s->has_gc_rumble = SDL_GameControllerHasRumble(s->gc) ? 1 : 0;
# else
			s->has_gc_rumble = 1;
# endif
			s->js = SDL_GameControllerGetJoystick(s->gc);
			s->opened_index = (auint)target;
		}
	}
	if (s->gc == NULL){
		s->js = SDL_JoystickOpen(target);
		if (s->js != NULL){
# if SDL_VERSION_ATLEAST(2,0,9)
			s->has_js_rumble = SDL_JoystickHasRumble(s->js) ? 1 : 0;
# else
			s->has_js_rumble = 1;
# endif
			s->opened_index = (auint)target;
		}
	}

	if (s->js && SDL_JoystickIsHaptic(s->js)){
		SDL_Haptic* hp = SDL_HapticOpenFromJoystick(s->js);
		if (hp && (SDL_HapticRumbleSupported(hp) == 1)){
			if (SDL_HapticRumbleInit(hp) == 0){
				s->hp = hp;
			}else{
				SDL_HapticClose(hp);
			}
		}
	}
	if ((s->hp == NULL) && (SDL_NumHaptics() > 0)){
		SDL_Haptic* hp = SDL_HapticOpen(0);
		if (hp && (SDL_HapticRumbleSupported(hp) == 1)){
			if (SDL_HapticRumbleInit(hp) == 0){
				s->hp = hp;
			}else{
				SDL_HapticClose(hp);
			}
		}
	}
}
#endif

static void slot_apply_mask(cu_hap_slot_t* s, auint mask, auint hold_ms)
{
	if (!cu_hap_ctx.ena){
		return;
	}

	s->pub.last_mask = mask & (CU_HAP_MOTOR_LO | CU_HAP_MOTOR_HI);

#if CU_HAP_HAVE_SDL2
	slot_open_device(s);

	Uint16 lo = (s->pub.last_mask & CU_HAP_MOTOR_LO) ? 0xFFFFU : 0x0000U;
	Uint16 hi = (s->pub.last_mask & CU_HAP_MOTOR_HI) ? 0xFFFFU : 0x0000U;
	Uint32 ms = (Uint32)((hold_ms != 0U) ? hold_ms : 1U);

# if SDL_VERSION_ATLEAST(2,0,14)
	if (s->gc && s->has_gc_rumble){
		SDL_GameControllerRumble(s->gc, lo, hi, ms);
		return;
	}
# endif
# if SDL_VERSION_ATLEAST(2,0,9)
	if (s->js && s->has_js_rumble){
		SDL_JoystickRumble(s->js, lo, hi, ms);
		return;
	}
# endif
	if (s->hp){
		float st = (lo || hi) ? 1.0f : 0.0f;
		if (st > 0.0f){
			SDL_HapticRumblePlay(s->hp, st, ms);
		}else{
			SDL_HapticRumbleStop(s->hp);
		}
		return;
	}
#else
	(void)hold_ms;
#endif
}

static auint slot_mask_from_state(auint state)
{
	switch (state & CU_HAP_STATE_MASK){
		case CU_HAP_STATE_LARGE: return CU_HAP_MOTOR_LO;
		case CU_HAP_STATE_SMALL: return CU_HAP_MOTOR_HI;
		case CU_HAP_STATE_BOTH:  return CU_HAP_MOTOR_LO | CU_HAP_MOTOR_HI;
		default:                 return 0U;
	}
}

static auint slot_state_from_mask(auint mask)
{
	mask &= (CU_HAP_MOTOR_LO | CU_HAP_MOTOR_HI);
	if (mask == (CU_HAP_MOTOR_LO | CU_HAP_MOTOR_HI)){
		return CU_HAP_STATE_BOTH;
	}
	if (mask == CU_HAP_MOTOR_LO){
		return CU_HAP_STATE_LARGE;
	}
	if (mask == CU_HAP_MOTOR_HI){
		return CU_HAP_STATE_SMALL;
	}
	return CU_HAP_STATE_OFF;
}

static void slot_start(cu_hap_slot_t* s, auint mask, auint duration_ms)
{
	if (duration_ms == 0U){
		duration_ms = 1U;
	}
	s->pub.last_id = 0U;
	s->pub.last_state = slot_state_from_mask(mask);
	slot_apply_mask(s, mask, duration_ms);
}

static void slot_set_state(cu_hap_slot_t* s, auint bus_id, auint state)
{
	s->pub.last_id = bus_id & 0x07U;
	s->pub.last_state = state & CU_HAP_STATE_MASK;
	slot_apply_mask(s, slot_mask_from_state(state), CU_HAP_HOLD_MS);
}

static void slot_stop(cu_hap_slot_t* s)
{
	if (!cu_hap_ctx.ena){
		return;
	}

	s->pub.last_id = 0U;
	s->pub.last_state = CU_HAP_STATE_OFF;
	slot_apply_mask(s, 0U, 1U);

#if CU_HAP_HAVE_SDL2
	if (s->hp){
		SDL_HapticRumbleStop(s->hp);
	}
#endif
}

static cu_vdev_t const* cu_hap_selected_vdev(auint port, auint* out_vdev)
{
	auint slot;
	auint vdev;
	cu_vdev_t const* dev;

	if (out_vdev != NULL){
		*out_vdev = CU_VDEV_INVALID;
	}
	if (port >= CU_HAP_PORTS){
		return NULL;
	}

	slot = cu_multitap_get_active_slot(port);
	vdev = cu_multitap_get_slot_vdev(port, slot);
	if (vdev == CU_VDEV_INVALID){
		return NULL;
	}
	dev = cu_vdev_get(vdev);
	if (dev == NULL){
		return NULL;
	}
	if (!dev->haptic_enabled){
		return NULL;
	}
	if (out_vdev != NULL){
		*out_vdev = vdev;
	}
	return dev;
}

boole cu_hap_slot_present(auint port)
{
	auint vdev;
	return (cu_hap_selected_vdev(port, &vdev) != NULL) ? TRUE : FALSE;
}

void cu_hap_route_payload_port(auint port, uint8 payload)
{
	auint vdev;
	if (port < CU_HAP_PORTS){
		cu_hap_ctx.slot[port].pub.last_id = (payload >> 3U) & 0x07U;
		cu_hap_ctx.slot[port].pub.last_state = (payload >> 1U) & CU_HAP_STATE_MASK;
		cu_hap_ctx.slot[port].pub.last_mask = slot_mask_from_state(cu_hap_ctx.slot[port].pub.last_state);
	}
	if (cu_hap_selected_vdev(port, &vdev) == NULL){
		return;
	}
	cu_vdev_haptic_from_frame(vdev, &payload, 1U);
	cu_multitap_touch_activity(port, cu_avr_getcycle());
}

void cu_hap_init(boole ena)
{
	auint i;

	cu_hap_ctx.ena = ena ? 1 : 0;
	for (i = 0U; i < (auint)CU_HAP_MAX; i++){
		slot_reset(&cu_hap_ctx.slot[i]);
	}
	for (i = 0U; i < CU_HAP_HOST_CACHE_MAX; i++){
		slot_reset(&cu_hap_ctx.host_slot[i]);
		cu_hap_ctx.host_slot[i].bound_index = i;
	}
	for (i = 0U; i < CU_HAP_PORTS; i++){
		port_reset(&cu_hap_ctx.port[i]);
	}
#if CU_HAP_HAVE_SDL2
	if (cu_hap_ctx.ena){
		SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
	}
#endif
}

void cu_hap_quit(void)
{
#if CU_HAP_HAVE_SDL2
	auint i;
	for (i = 0U; i < (auint)CU_HAP_MAX; i++){
		slot_close_device(&cu_hap_ctx.slot[i]);
	}
	for (i = 0U; i < CU_HAP_HOST_CACHE_MAX; i++){
		slot_close_device(&cu_hap_ctx.host_slot[i]);
	}
#endif
	memset(&cu_hap_ctx, 0, sizeof(cu_hap_ctx));
}

void cu_hap_reset_sessions(void)
{
	auint i;
	for (i = 0U; i < CU_HAP_PORTS; ++i){
		port_reset(&cu_hap_ctx.port[i]);
	}
}

void cu_hap_bind(cu_hap_port_t port, auint host_index)
{
	if ((auint)port >= (auint)CU_HAP_MAX){
		return;
	}
	cu_hap_ctx.slot[port].bound_index = host_index;
}

void cu_hap_start(cu_hap_port_t port, auint motor_mask, auint duration_ms)
{
	if ((auint)port >= (auint)CU_HAP_MAX){
		return;
	}
	slot_start(&cu_hap_ctx.slot[port], motor_mask, duration_ms);
}

void cu_hap_start_host(auint host_index, auint motor_mask, auint duration_ms)
{
	if (host_index >= CU_HAP_HOST_CACHE_MAX){
		return;
	}
	cu_hap_ctx.host_slot[host_index].bound_index = host_index;
	slot_start(&cu_hap_ctx.host_slot[host_index], motor_mask, duration_ms);
}

void cu_hap_set_state(cu_hap_port_t port, auint bus_id, auint state)
{
	if ((auint)port >= (auint)CU_HAP_MAX){
		return;
	}
	slot_set_state(&cu_hap_ctx.slot[port], bus_id, state);
}

void cu_hap_set_state_host(auint host_index, auint bus_id, auint state)
{
	if (host_index >= CU_HAP_HOST_CACHE_MAX){
		return;
	}
	cu_hap_ctx.host_slot[host_index].bound_index = host_index;
	slot_set_state(&cu_hap_ctx.host_slot[host_index], bus_id, state);
}

void cu_hap_stop(cu_hap_port_t port)
{
	if ((auint)port >= (auint)CU_HAP_MAX){
		return;
	}
	slot_stop(&cu_hap_ctx.slot[port]);
}

void cu_hap_from_uzebus(cu_hap_port_t port, const uint8* data, auint len)
{
	auint payload;
	auint bus_id;
	auint state;

	if ((auint)port >= (auint)CU_HAP_MAX){
		return;
	}
	if ((data == NULL) || (len < 1U)){
		return;
	}
	payload = (auint)data[0];
	if ((payload & CU_HAP_WIRE_CLASS_MASK) != CU_HAP_WIRE_CLASS_VALUE){
		return;
	}
	bus_id = (payload >> 3U) & 0x07U;
	state = (payload >> 1U) & CU_HAP_STATE_MASK;
	slot_set_state(&cu_hap_ctx.slot[port], bus_id, state);
}

auint cu_hap_process(auint prev, auint curr)
{
	(void)prev;
	(void)curr;
	return 0U;
}

auint cu_hap_bus_active(auint port)
{
	(void)port;
	return 0U;
}

cu_state_hap_t* cu_hap_get_state(cu_hap_port_t port)
{
	if ((auint)port >= (auint)CU_HAP_MAX){
		return 0;
	}
	return &cu_hap_ctx.slot[port].pub;
}

void cu_hap_update(void)
{
	/* Nothing to rebuild. */
}
