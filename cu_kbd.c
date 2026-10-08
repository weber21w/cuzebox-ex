/*
 *  Keyboard Dongle Emulation
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


#include "cu_kbd.h"
#include "cu_mouse.h"
#include "cu_multitap.h"
#include "cu_vdev.h"
#include <string.h>

typedef struct{
	auint state;
	auint clock;
	auint data_in;
	auint data_out;
	auint data_hold;
} cu_kbd_port_t;

typedef struct{
	auint in;
	auint out;
	uint8 data[CU_KBD_QUEUE_SIZE];
} cu_kbd_queue_t;

auint cu_kbd_state;
auint cu_kbd_clock;
auint cu_kbd_data_in;
auint cu_kbd_data_out;
auint cu_kbd_enabled;
auint cu_kbd_bypassed; /* don't check for start condition, allows user to break out of keyboard passthrough mode and use emulator controls */

auint cu_kbd_queue_in;
auint cu_kbd_queue_out;
uint8 cu_kbd_queue[CU_KBD_QUEUE_SIZE];

auint cu_kbd_held_lctrl;

static cu_kbd_port_t  cu_kbd_port[CU_KBD_PORTS];
static cu_kbd_queue_t cu_kbd_port_queue[CU_KBD_PORTS];
static cu_kbd_queue_t cu_kbd_port_pending[CU_KBD_PORTS];
static boole          cu_kbd_port_active[CU_KBD_PORTS];
static cu_kbd_enqueue_hook_t cu_kbd_enqueue_hook = NULL;
static cu_kbd_visible_hook_t cu_kbd_visible_hook = NULL;

static void cu_kbd_sync_legacy_queue(void);


void cu_kbd_get_state(cu_state_kbd_t* state)
{
	if (state == NULL){ return; }
	memset(state, 0, sizeof(*state));
	state->legacy_state = cu_kbd_state;
	state->legacy_clock = cu_kbd_clock;
	state->legacy_data_in = cu_kbd_data_in;
	state->legacy_data_out = cu_kbd_data_out;
	state->enabled = cu_kbd_enabled;
	state->bypassed = cu_kbd_bypassed;
	state->legacy_queue_in = cu_kbd_queue_in;
	state->legacy_queue_out = cu_kbd_queue_out;
	memcpy(state->legacy_queue, cu_kbd_queue, sizeof(cu_kbd_queue));
	state->held_lctrl = cu_kbd_held_lctrl;
	memcpy(state->port, cu_kbd_port, sizeof(cu_kbd_port));
	memcpy(state->port_queue, cu_kbd_port_queue, sizeof(cu_kbd_port_queue));
	memcpy(state->port_pending, cu_kbd_port_pending, sizeof(cu_kbd_port_pending));
	memcpy(state->port_active, cu_kbd_port_active, sizeof(cu_kbd_port_active));
}

void cu_kbd_set_state(cu_state_kbd_t const* state)
{
	if (state == NULL){ return; }
	cu_kbd_state = state->legacy_state;
	cu_kbd_clock = state->legacy_clock;
	cu_kbd_data_in = state->legacy_data_in;
	cu_kbd_data_out = state->legacy_data_out;
	cu_kbd_enabled = state->enabled;
	cu_kbd_bypassed = state->bypassed;
	cu_kbd_queue_in = state->legacy_queue_in;
	cu_kbd_queue_out = state->legacy_queue_out;
	memcpy(cu_kbd_queue, state->legacy_queue, sizeof(cu_kbd_queue));
	cu_kbd_held_lctrl = state->held_lctrl;
	memcpy(cu_kbd_port, state->port, sizeof(cu_kbd_port));
	memcpy(cu_kbd_port_queue, state->port_queue, sizeof(cu_kbd_port_queue));
	memcpy(cu_kbd_port_pending, state->port_pending, sizeof(cu_kbd_port_pending));
	memcpy(cu_kbd_port_active, state->port_active, sizeof(cu_kbd_port_active));
	cu_kbd_sync_legacy_queue();
}

/* Scan codes */
const auint cu_kbd_scan_codes[][2] = {
 {0x0d,SDLK_TAB},
 {0x0e,SDLK_BACKQUOTE},
 {0x11,SDLK_LALT},
 {0x12,SDLK_LSHIFT},
 {0x14,SDLK_LCTRL},
 {0x15,SDLK_q},
 {0x16,SDLK_1},
 {0x1a,SDLK_z},
 {0x1b,SDLK_s},
 {0x1c,SDLK_a},
 {0x1d,SDLK_w},
 {0x1e,SDLK_2},
 {0x21,SDLK_c},
 {0x22,SDLK_x},
 {0x23,SDLK_d},
 {0x24,SDLK_e},
 {0x25,SDLK_4},
 {0x26,SDLK_3},
 {0x29,SDLK_SPACE},
 {0x2a,SDLK_v},
 {0x2b,SDLK_f},
 {0x2c,SDLK_t},
 {0x2d,SDLK_r},
 {0x2e,SDLK_5},
 {0x31,SDLK_n},
 {0x32,SDLK_b},
 {0x33,SDLK_h},
 {0x34,SDLK_g},
 {0x35,SDLK_y},
 {0x36,SDLK_6},
 {0x3a,SDLK_m},
 {0x3b,SDLK_j},
 {0x3c,SDLK_u},
 {0x3d,SDLK_7},
 {0x3e,SDLK_8},
 {0x41,SDLK_COMMA},
 {0x42,SDLK_k},
 {0x43,SDLK_i},
 {0x44,SDLK_o},
 {0x45,SDLK_0},
 {0x46,SDLK_9},
 {0x49,SDLK_PERIOD},
 {0x4a,SDLK_SLASH},
 {0x4b,SDLK_l},
 {0x4c,SDLK_SEMICOLON},
 {0x4d,SDLK_p},
 {0x4e,SDLK_MINUS},
 {0x52,SDLK_QUOTE},
 {0x54,SDLK_LEFTBRACKET},
 {0x55,SDLK_EQUALS},
 {0x59,SDLK_RSHIFT},
 {0x5a,SDLK_RETURN},
 {0x5b,SDLK_RIGHTBRACKET},
 {0x5d,SDLK_BACKSLASH},
 {0x66,SDLK_BACKSPACE},
 {0x69,SDLK_KP_1},
 {0x6b,SDLK_KP_4},
 {0x6c,SDLK_KP_7},
 {0x70,SDLK_KP_0},
 {0x71,SDLK_KP_PERIOD},
 {0x72,SDLK_KP_2},
 {0x73,SDLK_KP_5},
 {0x74,SDLK_KP_6},
 {0x75,SDLK_KP_8},
 {0x79,SDLK_KP_PLUS},
 {0x7a,SDLK_KP_3},
 {0x7b,SDLK_KP_MINUS},
 {0x7c,SDLK_KP_MULTIPLY},
 {0x7d,SDLK_KP_9},
 {0,0}
};

const auint cu_kbd_ext_codes[][2] = {
 {0x69,SDLK_END},
 {0x6b,SDLK_LEFT},
 {0x6b,SDLK_HOME},
 {0x70,SDLK_INSERT},
 {0x71,SDLK_DELETE},
 {0x72,SDLK_DOWN},
 {0x74,SDLK_RIGHT},
 {0x75,SDLK_UP},
 {0x7a,SDLK_PAGEDOWN},
 {0x7c,SDLK_PRINTSCREEN},//SDLK_PRINT
 {0x7d,SDLK_PAGEUP},
 {0x7e,SDLK_STOP},//SDLK_BREAK
 {0,0}
};

static void cu_kbd_sync_legacy_queue(void)
{
	cu_kbd_queue_in = cu_kbd_port_queue[1U].in;
	cu_kbd_queue_out = cu_kbd_port_queue[1U].out;
	memcpy(cu_kbd_queue, cu_kbd_port_queue[1U].data, sizeof(cu_kbd_queue));
}

static void cu_kbd_sync_legacy(void)
{
	/* Legacy exported globals continue to mirror the direct P2 keyboard state. */
	cu_kbd_state = cu_kbd_port[1U].state;
	cu_kbd_clock = cu_kbd_port[1U].clock;
	cu_kbd_data_in = cu_kbd_port[1U].data_in;
	cu_kbd_data_out = cu_kbd_port[1U].data_out;
	cu_kbd_sync_legacy_queue();
}

static void cu_kbd_queue_reset(cu_kbd_queue_t* q)
{
	if (q == NULL){
		return;
	}
	q->in = 0U;
	q->out = 0U;
}

static boole cu_kbd_queue_reserve(cu_kbd_queue_t* q, auint count)
{
	if (q == NULL){
		return FALSE;
	}
	if (q->in == q->out){
		q->in = 0U;
		q->out = 0U;
	}
	if ((q->in >= CU_KBD_QUEUE_SIZE) || (q->out >= CU_KBD_QUEUE_SIZE)){
		cu_kbd_queue_reset(q);
	}
	if (count > CU_KBD_QUEUE_SIZE){
		return FALSE;
	}
	if (q->in > (CU_KBD_QUEUE_SIZE - count)){
		return FALSE;
	}
	return TRUE;
}

static boole cu_kbd_queue_push_byte(cu_kbd_queue_t* q, uint8 value)
{
	if (!cu_kbd_queue_reserve(q, 1U)){
		return FALSE;
	}
	q->data[q->in++] = value;
	return TRUE;
}

static auint cu_kbd_queue_pop_port_byte(auint port)
{
	cu_kbd_queue_t* q;
	auint value = 0U;

	if (port >= CU_KBD_PORTS){
		return 0U;
	}
	q = &cu_kbd_port_queue[port];
	if (q->in == q->out){
		return 0U;
	}
	if ((q->in >= CU_KBD_QUEUE_SIZE) || (q->out >= CU_KBD_QUEUE_SIZE)){
		cu_kbd_queue_reset(q);
		if (port == 1U){
			cu_kbd_sync_legacy_queue();
		}
		return 0U;
	}
	value = q->data[q->out++];
	if (q->in == q->out){
		cu_kbd_queue_reset(q);
	}
	if (port == 1U){
		cu_kbd_sync_legacy_queue();
	}
	return value;
}


void cu_kbd_set_enqueue_hook(cu_kbd_enqueue_hook_t hook)
{
	cu_kbd_enqueue_hook = hook;
}

void cu_kbd_set_visible_hook(cu_kbd_visible_hook_t hook)
{
	cu_kbd_visible_hook = hook;
}

static boole cu_kbd_pending_push_byte(auint port, uint8 value)
{
	if (port >= CU_KBD_PORTS){
		return FALSE;
	}
	return cu_kbd_queue_push_byte(&cu_kbd_port_pending[port], value);
}

void cu_kbd_frame_boundary(void)
{
	auint port;
	for (port = 0U; port < CU_KBD_PORTS; ++port){
		cu_kbd_queue_t* src = &cu_kbd_port_pending[port];
		while (src->out != src->in){
			uint8 value;
			if ((src->in >= CU_KBD_QUEUE_SIZE) || (src->out >= CU_KBD_QUEUE_SIZE)){
				cu_kbd_queue_reset(src);
				break;
			}
			value = src->data[src->out++];
			if (!cu_kbd_queue_push_byte(&cu_kbd_port_queue[port], value)){
				break;
			}
				if (cu_kbd_visible_hook != NULL){
					cu_kbd_visible_hook(port, value);
				}
		}
		if (src->in == src->out){
			cu_kbd_queue_reset(src);
		}
	}
	cu_kbd_sync_legacy_queue();
}

static boole cu_kbd_queue_push_or_hook(auint port, uint8 value)
{
	if ((cu_kbd_enqueue_hook != NULL) && cu_kbd_enqueue_hook(port, value)){
		return TRUE;
	}
	return cu_kbd_pending_push_byte(port, value);
}
static void cu_kbd_queue_route_event(SDL_Event const* ev)
{
	boole route_port[CU_KBD_PORTS] = { FALSE, FALSE };
	uint16 i;
	auint port;

	if (ev == NULL){
		return;
	}
	for (port = 0U; port < CU_KBD_PORTS; ++port){
		if ((cu_kbd_port_active[port] != FALSE) || (cu_kbd_port[port].state != KBD_STOP)){
			route_port[port] = TRUE;
		}
	}
	if (cu_kbd_enabled != 0U){
		route_port[1U] = TRUE;
	}
	if (!route_port[0U] && !route_port[1U]){
		return;
	}

	if (ev->type == SDL_KEYUP){
		for (port = 0U; port < CU_KBD_PORTS; ++port){
			if (!route_port[port]){ continue; }
			(void)cu_kbd_queue_push_or_hook(port, 0xF0U);
		}
	}

	i = 0U;
	while (cu_kbd_scan_codes[i][1] && (cu_kbd_scan_codes[i][1] != (auint)ev->key.keysym.sym)){
		i++;
	}
	if (cu_kbd_scan_codes[i][1] == (auint)ev->key.keysym.sym){
		for (port = 0U; port < CU_KBD_PORTS; ++port){
			if (!route_port[port]){ continue; }
			(void)cu_kbd_queue_push_or_hook(port, (uint8)(cu_kbd_scan_codes[i][0] & 0xFFU));
		}
		cu_kbd_sync_legacy_queue();
		return;
	}

	i = 0U;
	while (cu_kbd_ext_codes[i][1] && (cu_kbd_ext_codes[i][1] != (auint)ev->key.keysym.sym)){
		i++;
	}
	if (cu_kbd_ext_codes[i][1] == (auint)ev->key.keysym.sym){
		for (port = 0U; port < CU_KBD_PORTS; ++port){
			if (!route_port[port]){ continue; }
			(void)cu_kbd_queue_push_or_hook(port, 0xE0U);
			(void)cu_kbd_queue_push_or_hook(port, (uint8)(cu_kbd_ext_codes[i][0] & 0xFFU));
		}
		cu_kbd_sync_legacy_queue();
		return;
	}

	if (ev->type == SDL_KEYDOWN){
		if (ev->key.keysym.sym == SDLK_LCTRL){
			cu_kbd_held_lctrl = 1U;
		}else if (ev->key.keysym.sym == SDLK_UP){
			for (port = 0U; port < CU_KBD_PORTS; ++port){
				if (!route_port[port]){ continue; }
				(void)cu_kbd_queue_push_or_hook(port, 0xE0U);
				(void)cu_kbd_queue_push_or_hook(port, 0x6BU);
			}
		}else if (cu_kbd_held_lctrl){
			if (ev->key.keysym.sym == SDLK_F1){
				cu_kbd_enabled = 0U;
				cu_kbd_bypassed = 255U;
				cu_kbd_reset_sessions();
				print_unf("KEYBOARD PASSTHROUGH ENABLED\n");
			}else if (ev->key.keysym.sym == SDLK_F6){
				cu_adjust_mouse_scale();
			}
		}
	}else if (ev->type == SDL_KEYUP){
		if (ev->key.keysym.sym == SDLK_LCTRL){
			cu_kbd_held_lctrl = 0U;
		}
	}
	cu_kbd_sync_legacy_queue();
}

void cu_kbd_reset_sessions(void)
{
	auint i;
	for (i = 0U; i < CU_KBD_PORTS; ++i){
		cu_kbd_port[i].state = KBD_STOP;
		cu_kbd_port[i].clock = 0U;
		cu_kbd_port[i].data_in = 0U;
		cu_kbd_port[i].data_out = 0U;
		cu_kbd_port[i].data_hold = 0U;
		cu_kbd_port_active[i] = FALSE;
		cu_kbd_queue_reset(&cu_kbd_port_queue[i]);
		cu_kbd_queue_reset(&cu_kbd_port_pending[i]);
	}
	cu_kbd_sync_legacy();
}

void cu_kbd_set_vdev_session(auint port, boole active)
{
	if (port >= CU_KBD_PORTS){
		return;
	}
	cu_kbd_port_active[port] = active ? TRUE : FALSE;
}

void cu_kbd_debug_enqueue_port_byte(auint port, uint8 value)
{
	if (port >= CU_KBD_PORTS){
		return;
	}
	(void)cu_kbd_queue_push_byte(&cu_kbd_port_queue[port], value);
	if (cu_kbd_visible_hook != NULL){
		cu_kbd_visible_hook(port, value);
	}
	if (port == 1U){
		cu_kbd_sync_legacy_queue();
	}
}

auint cu_kbd_capture_active(void)
{
	auint i;
	if (cu_kbd_enabled != 0U){
		return 1U;
	}
	for (i = 0U; i < CU_KBD_PORTS; ++i){
		if (cu_kbd_port_active[i] != FALSE){
			return 1U;
		}
		if (cu_kbd_port[i].state != KBD_STOP){
			return 1U;
		}
	}
	return 0U;
}

void cu_kbd_handle_key(SDL_Event const* ev){
	cu_kbd_queue_route_event(ev);
}

auint cu_kbd_response_for_port_command(auint port, auint cmd)
{
	switch (cmd & 0xFFU){
		case KBD_SEND_DEVICE_ID:    return 0xCCU;
		case KBD_SEND_FIRMWARE_REV: return 0x11U;
		case KBD_RESET:             return 0xFAU;
		case KBD_SEND_KEY:
		default:
			return cu_kbd_queue_pop_port_byte(port);
	}
}

auint cu_kbd_response_for_command(auint cmd)
{
	return cu_kbd_response_for_port_command(1U, cmd);
}

static auint cu_kbd_process_port(auint port, auint prev, auint curr, boole present, boole legacy_direct)
{
	cu_kbd_port_t* kp;
	auint data_bit;
	auint start_prev;
	auint start_curr;
	auint out = 0U;
	auint response = 0U;

	if (port >= CU_KBD_PORTS){
		return 0U;
	}
	kp = &cu_kbd_port[port];
	data_bit = (port == 0U) ? 0x01U : 0x02U;
	start_prev = prev & 0x0CU;
	start_curr = curr & 0x0CU;

	if (!present){
		if (kp->state != KBD_STOP){
			kp->state = KBD_STOP;
			kp->clock = 0U;
			kp->data_in = 0U;
			kp->data_out = 0U;
			kp->data_hold = 0U;
		}
		return 0U;
	}

	if (cu_kbd_bypassed){
		return 0U;
	}

	if (kp->state == KBD_STOP){
		if ((start_curr == 0x04U) && (start_prev != 0x04U)){
			if (legacy_direct && !cu_kbd_enabled){
				print_unf("ROM tested for dongle: keyboard emulation enabled(P2 disabled)\n");
				cu_kbd_enabled = 1U;
			}
			kp->state = KBD_TX_START;
		}
		return 0U;
	}

	if (kp->state == KBD_TX_START){
		if (start_curr == 0x08U){
			kp->state = KBD_TX_READY;
			kp->clock = 8U;
			kp->data_hold = 0U;
		}
		return 0U;
	}

	/* KBD_TX_READY */
	if ((prev & 0x08U) != 0U && (curr & 0x08U) == 0U){ /* clock went low */
		if (kp->clock == 8U){
			kp->data_out = 0U;
			response = cu_kbd_response_for_port_command(port, kp->data_hold);
			kp->data_in = response & 0xFFU;
		}

		kp->data_out <<= 1U; /* shift data out to keyboard, latch pin is used as "Data Out" */
		if(curr & 0x04U){
			kp->data_out |= 1U;
		}

		if (kp->data_in & 0x80U){
			out |= data_bit;
		}
		kp->data_in <<= 1U;

		kp->clock--;
		if (kp->clock == 0U){
			kp->data_hold = kp->data_out & 0xFFU;
			if (kp->data_out == KBD_SEND_END){
				kp->state = KBD_STOP;
				kp->clock = 0U;
			}else{
				kp->clock = 8U;
			}
		}
	}

	return out;
}

auint cu_kbd_process(auint prev, auint curr)
{
	auint out = 0U;
	boole legacy_direct = FALSE;

	if (cu_multitap_get_tap_present(1U) == 0U){
		legacy_direct = TRUE;
	}

	out |= cu_kbd_process_port(1U, prev, curr, legacy_direct, legacy_direct);

	cu_kbd_sync_legacy();
	return out;
}


auint  cu_kbd_get_enabled(void){
 return cu_kbd_enabled;
}


void cu_kbd_set_enabled(uint8 val){
 cu_kbd_enabled = val;
 if (!val){
	cu_kbd_reset_sessions();
 }
}
