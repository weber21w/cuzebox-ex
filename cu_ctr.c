#include "cu_ctr.h"
#include "cu_kbd.h"
#include "cu_mouse.h"
#include "cu_multitap.h"
#include "cu_vdev.h"
#include "cu_gun.h"
#include "cu_haptic.h"
#include "cu_avr.h"
#include <string.h>

/* Fallback if header didn't define it */
#ifndef CU_MULTITAP_MAX_SLOTS
#define CU_MULTITAP_MAX_SLOTS 4U
#endif

/* SNES controller buttons */
#define CU_CTR_SNES_B        0U
#define CU_CTR_SNES_Y        1U
#define CU_CTR_SNES_SELECT   2U
#define CU_CTR_SNES_START    3U
#define CU_CTR_SNES_UP       4U
#define CU_CTR_SNES_DOWN     5U
#define CU_CTR_SNES_LEFT     6U
#define CU_CTR_SNES_RIGHT    7U
#define CU_CTR_SNES_A        8U
#define CU_CTR_SNES_X        9U
#define CU_CTR_SNES_LSH     10U
#define CU_CTR_SNES_RSH     11U


/* Current button state (low active) per port + slot */
static auint cu_ctr_buttons[2][CU_MULTITAP_MAX_SLOTS];

/* Latched controller shift state per port */
static uint64_t cu_ctr_blatch[2];
static auint    cu_ctr_shift_count[2];
static auint    cu_ctr_probe_inflight[2];
static boole    cu_ctr_override_enable[2][CU_MULTITAP_MAX_SLOTS];
static uint32   cu_ctr_override_packet[2][CU_MULTITAP_MAX_SLOTS];
static auint    cu_ctr_override_bits[2][CU_MULTITAP_MAX_SLOTS];


typedef struct{
	auint state;
	auint clock;
	auint tx_byte;
	auint rx_byte;
	auint dev_type;
	auint kbd_cmd_hold;
	auint payload_seen;
} cu_ctr_ubus_port_t;

#define CU_CTR_UBUS_STOP      0U
#define CU_CTR_UBUS_TX_START  1U
#define CU_CTR_UBUS_TX_READY  2U
#define CU_CTR_UBUS_DEV_NONE  0U
#define CU_CTR_UBUS_DEV_KBD   1U
#define CU_CTR_UBUS_DEV_HAP   2U

static cu_ctr_ubus_port_t cu_ctr_ubus[2];


void cu_ctr_get_state(cu_state_ctr_t* state)
{
	if (state == NULL){ return; }
	memset(state, 0, sizeof(*state));
	memcpy(state->buttons, cu_ctr_buttons, sizeof(cu_ctr_buttons));
	memcpy(state->blatch, cu_ctr_blatch, sizeof(cu_ctr_blatch));
	memcpy(state->shift_count, cu_ctr_shift_count, sizeof(cu_ctr_shift_count));
	memcpy(state->probe_inflight, cu_ctr_probe_inflight, sizeof(cu_ctr_probe_inflight));
	memcpy(state->override_enable, cu_ctr_override_enable, sizeof(cu_ctr_override_enable));
	memcpy(state->override_packet, cu_ctr_override_packet, sizeof(cu_ctr_override_packet));
	memcpy(state->override_bits, cu_ctr_override_bits, sizeof(cu_ctr_override_bits));
	memcpy(state->ubus, cu_ctr_ubus, sizeof(cu_ctr_ubus));
}

void cu_ctr_set_state(cu_state_ctr_t const* state)
{
	if (state == NULL){ return; }
	memcpy(cu_ctr_buttons, state->buttons, sizeof(cu_ctr_buttons));
	memcpy(cu_ctr_blatch, state->blatch, sizeof(cu_ctr_blatch));
	memcpy(cu_ctr_shift_count, state->shift_count, sizeof(cu_ctr_shift_count));
	memcpy(cu_ctr_probe_inflight, state->probe_inflight, sizeof(cu_ctr_probe_inflight));
	memcpy(cu_ctr_override_enable, state->override_enable, sizeof(cu_ctr_override_enable));
	memcpy(cu_ctr_override_packet, state->override_packet, sizeof(cu_ctr_override_packet));
	memcpy(cu_ctr_override_bits, state->override_bits, sizeof(cu_ctr_override_bits));
	memcpy(cu_ctr_ubus, state->ubus, sizeof(cu_ctr_ubus));
}

/*
** Filters out mechanically impossible combinations in a button set
*/
static auint cu_ctr_mfilt(auint buttons)
{
	if ((buttons & (CU_CTR_SNES_M_UP | CU_CTR_SNES_M_DOWN)) == 0U){
		buttons |= CU_CTR_SNES_M_DOWN;
	}
	if ((buttons & (CU_CTR_SNES_M_LEFT | CU_CTR_SNES_M_RIGHT)) == 0U){
		buttons |= CU_CTR_SNES_M_RIGHT;
	}
	return buttons;
}

static boole cu_ctr_build_slot_vdev_state(auint player, auint slot, uint64_t* latched, auint* bits)
{
	auint  vdev;
	auint  obits;
	uint32 packet;
	(void)player;

	if ((latched == NULL) || (bits == NULL)){
		return FALSE;
	}

	vdev = cu_multitap_get_slot_vdev(player, slot);
	if (vdev == CU_VDEV_INVALID){
		return FALSE;
	}
	if (!cu_vdev_build_low_active_packet(vdev, player, &packet, &obits)){
		return FALSE;
	}
	*latched = ~(uint64_t)0;
	*latched &= ~((uint64_t)0xFFFFFFFFULL);
	*latched |= (uint64_t)packet;
	*bits = obits;
	return TRUE;
}

static boole cu_ctr_get_slot_default_bits(auint player, auint slot, auint* bits)
{
	auint vdev;
	cu_vdev_t const* dev;
	if (bits == NULL){ return FALSE; }
	if (player > 1U){ return FALSE; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return FALSE; }
	vdev = cu_multitap_get_slot_vdev(player, slot);
	if (vdev != CU_VDEV_INVALID){
		dev = cu_vdev_get(vdev);
		if ((dev != NULL) && (dev->type == CU_VDEV_TYPE_SUPERMOUSE32)){
			*bits = 32U;
			return TRUE;
		}
	}
	*bits = 16U;
	return TRUE;
}

boole cu_ctr_getslot_packet_format(auint player, auint slot, auint* bits)
{
	return cu_ctr_get_slot_default_bits(player, slot, bits);
}

boole cu_ctr_getslot_packet_state(auint player, auint slot, auint* packet, auint* bits)
{
	uint64_t low_active64;
	auint out_bits;
	if (packet == NULL){ return FALSE; }
	if (player > 1U){ return FALSE; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return FALSE; }
	if (cu_ctr_build_slot_vdev_state(player, slot, &low_active64, &out_bits)){
		/* cu_ctr_build_slot_vdev_state only fills the low 32 bits. */
		*packet = (auint)(~((uint32)low_active64));
		if (out_bits <= 16U){
			*packet &= 0xFFFFU;
		}else{
			*packet &= 0xFFFFFFFFU;
		}
		if (bits != NULL){
			*bits = out_bits;
		}
		return TRUE;
	}
	*packet = ~cu_ctr_buttons[player][slot];
	if ((slot == 0U) && cu_gun_get_enabled(player)){
		*packet |= (cu_gun_get_latched(player) & 0xF000U);
	}
	*packet &= 0xFFFFU;
	if (bits != NULL){
		*bits = 16U;
	}
	return TRUE;
}

void cu_ctr_setslot_packet_override(auint player, auint slot, boole enable, auint packet, auint bits)
{
	if (player > 1U){ return; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return; }
	cu_ctr_override_enable[player][slot] = enable ? TRUE : FALSE;
	cu_ctr_override_packet[player][slot] = (uint32)packet;
	cu_ctr_override_bits[player][slot] = (bits > 16U) ? 32U : 16U;
}

void cu_ctr_clear_packet_overrides(void)
{
	auint i, s;
	for (i = 0U; i < 2U; i++){
		for (s = 0U; s < CU_MULTITAP_MAX_SLOTS; s++){
			cu_ctr_override_enable[i][s] = FALSE;
			cu_ctr_override_packet[i][s] = 0U;
			cu_ctr_override_bits[i][s] = 16U;
		}
	}
}

static void cu_ctr_build_slot_state(auint player, auint slot,
	uint64_t* latched, auint* bits, auint* probe_inflight)
{
	uint64_t out = ~(uint64_t)0;
	auint    out_bits = 16U;
	auint    out_probe = 0U;
	auint    probe_byte;
	auint    probe_pending;

	if ((latched == NULL) || (bits == NULL) || (probe_inflight == NULL)){
		return;
	}

	if (cu_ctr_override_enable[player][slot]){
		out = ~(uint64_t)0;
		out &= ~((uint64_t)0xFFFFFFFFULL);
		out |= (((uint64_t)(~cu_ctr_override_packet[player][slot])) & 0xFFFFFFFFULL);
		out_bits = (cu_ctr_override_bits[player][slot] > 16U) ? 32U : 16U;
	}else if (!cu_ctr_build_slot_vdev_state(player, slot, &out, &out_bits)){
		out = ~(uint64_t)0;
		out &= ~((uint64_t)0xFFFFFFFFULL);
		out |= (uint64_t)(cu_ctr_mfilt(cu_ctr_buttons[player][slot]) & 0xFFFFFFFFU);
		out_bits = 16U;
		if ((slot == 0U) && cu_gun_get_enabled(player)){
			/* Gun data is active-high; controller shift state is low-active. */
			out &= ~((uint64_t)cu_gun_get_latched(player));
		}
	}

	probe_pending = cu_multitap_get_probe_pending(player);
	if ((cu_multitap_get_tap_present(player) != 0U) && (probe_pending != 0U)){
		probe_byte = cu_multitap_get_probe_byte(player);
		out <<= 8U;
		out &= ~((uint64_t)0xFFULL);
		out |= (uint64_t)(probe_byte & 0xFFU);
		if (out_bits <= 56U){
			out_bits += 8U;
		}else{
			out_bits = 64U;
		}
		out_probe = 1U;
	}

	*latched = out;
	*bits = out_bits;
	*probe_inflight = out_probe;
}


static void cu_ctr_ubus_reset_port(auint port)
{
	if (port > 1U){
		return;
	}
	if (cu_ctr_ubus[port].dev_type == CU_CTR_UBUS_DEV_KBD){
		cu_kbd_set_vdev_session(port, FALSE);
	}
	memset(&cu_ctr_ubus[port], 0, sizeof(cu_ctr_ubus[port]));
	cu_ctr_ubus[port].state = CU_CTR_UBUS_STOP;
	cu_ctr_ubus[port].dev_type = CU_CTR_UBUS_DEV_NONE;
}

static auint cu_ctr_ubus_bus_active(auint port)
{
	if (port > 1U){
		return 0U;
	}
	return (cu_ctr_ubus[port].state != CU_CTR_UBUS_STOP) ? 1U : 0U;
}

static boole cu_ctr_slot_is_haptic_vdev(auint player, auint slot)
{
	auint vdev;
	cu_vdev_t const* dev;

	vdev = cu_multitap_get_slot_vdev(player, slot);
	if (vdev == CU_VDEV_INVALID){
		return FALSE;
	}
	dev = cu_vdev_get(vdev);
	if (dev == NULL){
		return FALSE;
	}
	return dev->haptic_enabled ? TRUE : FALSE;
}

static auint cu_ctr_ubus_process_port(auint port, auint prev, auint curr, boole present_kbd, boole present_hap)
{
	cu_ctr_ubus_port_t* up;
	auint data_bit;
	auint start_prev;
	auint start_curr;
	auint out = 0U;
	auint bytev;

	if (port > 1U){
		return 0U;
	}
	up = &cu_ctr_ubus[port];
	data_bit = (port == 0U) ? 0x01U : 0x02U;
	start_prev = prev & 0x0CU;
	start_curr = curr & 0x0CU;

	if (!present_kbd && !present_hap){
		if (up->state != CU_CTR_UBUS_STOP){
			cu_ctr_ubus_reset_port(port);
		}
		return 0U;
	}

	if (up->state == CU_CTR_UBUS_STOP){
		if ((start_curr == 0x04U) && (start_prev != 0x04U)){
			up->state = CU_CTR_UBUS_TX_START;
			up->dev_type = present_kbd ? CU_CTR_UBUS_DEV_KBD : (present_hap ? CU_CTR_UBUS_DEV_HAP : CU_CTR_UBUS_DEV_NONE);
			up->kbd_cmd_hold = 0U;
			up->payload_seen = 0U;
			if (up->dev_type == CU_CTR_UBUS_DEV_KBD){
				cu_kbd_set_vdev_session(port, TRUE);
			}
		}
		return 0U;
	}

	if (up->state == CU_CTR_UBUS_TX_START){
		if (start_curr == 0x08U){
			up->state = CU_CTR_UBUS_TX_READY;
			up->clock = 8U;
			up->tx_byte = (up->dev_type == CU_CTR_UBUS_DEV_KBD) ? (cu_kbd_response_for_port_command(port, up->kbd_cmd_hold) & 0xFFU) : 0U;
			up->rx_byte = 0U;
		}else if (start_curr != 0x04U){
			cu_ctr_ubus_reset_port(port);
		}
		return 0U;
	}

	if (((prev & 0x08U) != 0U) && ((curr & 0x08U) == 0U)){
		if (up->clock == 8U){
			up->rx_byte = 0U;
			if (up->dev_type == CU_CTR_UBUS_DEV_KBD){
				up->tx_byte = cu_kbd_response_for_port_command(port, up->kbd_cmd_hold) & 0xFFU;
			}else{
				up->tx_byte = 0U;
			}
		}

		up->rx_byte <<= 1U;
		if ((curr & 0x04U) != 0U){
			up->rx_byte |= 1U;
		}
		if ((up->tx_byte & 0x80U) != 0U){
			out |= data_bit;
		}
		up->tx_byte <<= 1U;
		up->clock --;

		if (up->clock == 0U){
			bytev = up->rx_byte & 0xFFU;
			if (up->dev_type == CU_CTR_UBUS_DEV_KBD){
				up->kbd_cmd_hold = bytev;
				cu_multitap_touch_activity(port, cu_avr_getcycle());
				if (bytev == KBD_SEND_END){
					cu_ctr_ubus_reset_port(port);
				}else{
					up->clock = 8U;
				}
			}else if (up->dev_type == CU_CTR_UBUS_DEV_HAP){
				if (up->payload_seen == 0U){
					if ((bytev & CU_HAP_WIRE_CLASS_MASK) == CU_HAP_WIRE_CLASS_VALUE){
						cu_hap_route_payload_port(port, (uint8)(bytev & 0xFFU));
						up->payload_seen = 1U;
						up->clock = 8U; /* tolerate trailing byte(s) from WIP kernel */
					}else{
						cu_ctr_ubus_reset_port(port);
					}
				}else{
					cu_ctr_ubus_reset_port(port);
				}
			}else{
				cu_ctr_ubus_reset_port(port);
			}
		}
	}else if (start_curr == 0U){
		/* Shared UzeBus session returns to idle when lines do. */
		cu_ctr_ubus_reset_port(port);
	}

	return out;
}

static boole cu_ctr_slot_is_keyboard_vdev(auint player, auint slot)
{
	auint vdev;
	cu_vdev_t const* dev;

	vdev = cu_multitap_get_slot_vdev(player, slot);
	if (vdev == CU_VDEV_INVALID){
		return FALSE;
	}
	dev = cu_vdev_get(vdev);
	if (dev == NULL){
		return FALSE;
	}
	return (dev->type == CU_VDEV_TYPE_KEYBOARD) ? TRUE : FALSE;
}

static void cu_ctr_latch_port(auint player, auint slot)
{
	uint64_t latched;
	auint bits;
	auint probe_inflight;

	cu_ctr_build_slot_state(player, slot, &latched, &bits, &probe_inflight);
	cu_ctr_blatch[player] = latched;
	cu_ctr_shift_count[player] = 0U;
	cu_ctr_probe_inflight[player] = probe_inflight;
	(void)bits;
}

static void cu_ctr_shift_port(auint player)
{
	cu_ctr_blatch[player] >>= 1;
	if (cu_ctr_shift_count[player] < 63U){
		cu_ctr_shift_count[player] ++;
	}
	if ((cu_ctr_probe_inflight[player] != 0U) && (cu_ctr_shift_count[player] >= 8U)){
		cu_multitap_set_probe_pending(player, 0U);
		cu_ctr_probe_inflight[player] = 0U;
	}
}


/*
** Resets controllers to all inactive.
*/
void cu_ctr_reset(void)
{
	auint i;
	auint s;

	/* Controller buttons are low active: 1 = released, 0 = pressed */
	for (i = 0U; i < 2U; i++){
		for (s = 0U; s < CU_MULTITAP_MAX_SLOTS; s++){
			cu_ctr_buttons[i][s] = 0xFFFFFFFFU;
		}
		cu_ctr_blatch[i] = ~(uint64_t)0;
		cu_ctr_shift_count[i] = 0U;
		cu_ctr_probe_inflight[i] = 0U;
		for (s = 0U; s < CU_MULTITAP_MAX_SLOTS; s++){
			cu_ctr_override_enable[i][s] = FALSE;
			cu_ctr_override_packet[i][s] = 0U;
			cu_ctr_override_bits[i][s] = 16U;
		}
		cu_ctr_ubus_reset_port(i);
	}
	cu_vdev_reset();
	cu_multitap_reset();
	cu_kbd_reset_sessions();
	cu_hap_reset_sessions();
	cu_gun_reset();
}


/*
** Accepting previous and currently written PORTA value, processes hardware
** related to the Uzebox's controllers. Returns the new PINA value.
**
** Notes:
**  - Keyboard & mouse routing logic is unchanged.
**  - When (kbd_enabled || mouse_enabled) gates a port, that port still uses
**    the active multitap slot (slot 0 by default).
*/
auint cu_ctr_process(auint prev, auint curr)
{
	auint chg  = prev ^ curr;
	auint rise = chg  & curr;
	auint i;
	auint ret = 0U;
	auint tick = cu_avr_getcycle();
	uint8 kbd_enabled   = cu_kbd_get_enabled();
	uint8 mouse_enabled = cu_mouse_get_enabled();
	uint8 gun_enabled   = (uint8)cu_gun_any_enabled();
	auint slot[2];
	boole port_use_ctr[2];
	boole port_kbd_vdev[2];
	boole port_hap_vdev[2];
	auint ubus_bits[2];

	cu_multitap_apply_timeouts(tick);
	cu_multitap_process_ctrl(prev, curr, tick);

	for (i = 0U; i < 2U; ++i){
		slot[i] = cu_multitap_get_active_slot(i);
		if (slot[i] >= CU_MULTITAP_MAX_SLOTS){
			slot[i] = 0U;
		}
		port_kbd_vdev[i] = cu_ctr_slot_is_keyboard_vdev(i, slot[i]);
		port_hap_vdev[i] = cu_ctr_slot_is_haptic_vdev(i, slot[i]);
		ubus_bits[i] = cu_ctr_ubus_process_port(i, prev, curr, port_kbd_vdev[i], port_hap_vdev[i]);
		port_use_ctr[i] = TRUE;
	}

	/* Legacy direct devices still own their historical ports. */
	if (mouse_enabled){
		port_use_ctr[0U] = FALSE;
	}
	if (kbd_enabled){
		port_use_ctr[1U] = FALSE;
	}

	/* A selected keyboard virtual device suppresses controller packet output on that port. */
	for (i = 0U; i < 2U; ++i){
		if (port_kbd_vdev[i]){
			port_use_ctr[i] = FALSE;
		}
		if (cu_ctr_ubus_bus_active(i) != 0U){
			port_use_ctr[i] = FALSE;
		}
	}

	ret |= ubus_bits[0U];
	ret |= ubus_bits[1U];
	if (port_use_ctr[0U]){
		ret |= (auint)(cu_ctr_blatch[0] & 1ULL);
	}
	if (port_use_ctr[1U]){
		ret |= ((auint)(cu_ctr_blatch[1] & 1ULL) << 1);
	}

	if (rise == 0x04U){  /* Latch command */
		if (gun_enabled != 0U){
			cu_gun_latch_event();
		}
		for (i = 0U; i < 2U; ++i){
			if (!port_use_ctr[i]){
				continue;
			}
			cu_ctr_latch_port(i, slot[i]);
		}
	}else if (rise == 0x08U){ /* Clock command */
		for (i = 0U; i < 2U; ++i){
			if (!port_use_ctr[i]){
				continue;
			}
			cu_ctr_shift_port(i);
		}
	}

	return ret;
}



/*
** INTERNAL SLOT-SELECTOR
** Helper: pick the currently active slot index for a port, clamped to range.
*/
static auint cu_ctr_get_active_slot_clamped(auint player)
{
	auint slot = cu_multitap_get_active_slot(player);
	if (slot >= CU_MULTITAP_MAX_SLOTS){
		slot = 0U;
	}
	return slot;
}


/*
** Sends current SNES controller state for a specific slot.
** The buttons are a combination of masks generated from the definitions.
**
** Note: buttons are active-high in the API, but stored inverted (low-active)
**       internally to match hardware.
*/
void cu_ctr_setsnes_slot(auint player, auint slot, auint buttons)
{
	if (player > 1U){ return; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return; }
	cu_ctr_buttons[player][slot] = ~buttons;
}


/*
** Backwards-compatible API: sets buttons for slot 0 on the given player.
*/
void cu_ctr_setsnes(auint player, auint buttons)
{
	cu_ctr_setsnes_slot(player, 0U, buttons);
}

boole cu_ctr_getsnes_state(auint player, auint* buttons)
{
	auint slot;

	if (player > 1U){ return FALSE; }
	if (buttons == 0){ return FALSE; }

	slot = cu_ctr_get_active_slot_clamped(player);
	*buttons = ~cu_ctr_buttons[player][slot];
	return TRUE;
}

boole cu_ctr_getsnes_slot_state(auint player, auint slot, auint* buttons)
{
	if (player > 1U){ return FALSE; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return FALSE; }
	if (buttons == 0){ return FALSE; }
	*buttons = ~cu_ctr_buttons[player][slot];
	return TRUE;
}


#ifdef ENABLE_ICAP
#ifndef ENABLE_IREP
/*
** Retrieves current SNES controller state. The buttons are a combination of
** masks generated from the definitions, for the currently active slot.
*/
void cu_ctr_getsnes(auint player, auint* buttons)
{
	auint slot;

	if (player > 1U){ return; }
	if (buttons == 0){ return; }

	slot = cu_ctr_get_active_slot_clamped(player);
	*buttons = ~cu_ctr_buttons[player][slot];
}
#endif
#endif


/*
** Sends state of single SNES controller button for a specific slot.
** The button is a single (non-mask) definition (bit index 0..11).
*/
void cu_ctr_setsnes_single_slot(auint player, auint slot, auint button, boole press)
{
	auint binv;

	if (player > 1U){ return; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return; }
	if (button >= 32U){ return; } /* safety */

	binv = cu_ctr_buttons[player][slot];

	if (press){
		binv &= ~((auint)(1U) << button);
	}else{
		binv |=  ((auint)(1U) << button);
	}

	cu_ctr_buttons[player][slot] = binv;
}


/*
** Backwards-compatible API: single button state for slot 0.
*/
void cu_ctr_setsnes_single(auint player, auint button, boole press)
{
	cu_ctr_setsnes_single_slot(player, 0U, button, press);
}
