/*
 *  Virtual device routing / topology scaffolding
 */

#include "cu_vdev.h"
#include "cu_ctr.h"
#include "ginput.h"
#include "cu_gun.h"
#include "cu_haptic.h"

#include <string.h>

static cu_vdev_t cu_vdev_list[CU_VDEV_MAX];

void cu_vdev_get_state(cu_state_vdev_t* state)
{
	if (state == NULL){ return; }
	memcpy(state->list, cu_vdev_list, sizeof(cu_vdev_list));
}

void cu_vdev_set_state(cu_state_vdev_t const* state)
{
	if (state == NULL){ return; }
	memcpy(cu_vdev_list, state->list, sizeof(cu_vdev_list));
	for (auint i = 0U; i < CU_VDEV_MAX; ++i){
		cu_vdev_list[i].name[CU_VDEV_NAME_MAX - 1U] = 0;
		if (cu_vdev_list[i].sm_scale_x_pct == 0U){ cu_vdev_list[i].sm_scale_x_pct = 100U; }
		if (cu_vdev_list[i].sm_scale_y_pct == 0U){ cu_vdev_list[i].sm_scale_y_pct = 100U; }
	}
}

auint cu_vdev_default_map_for_host_type(cu_vdev_host_type_t type, auint src_bit)
{
	switch (type){
		case CU_VDEV_HOST_KEYBOARD:
		case CU_VDEV_HOST_GAMECONTROLLER:
		case CU_VDEV_HOST_JOYSTICK:
			if (src_bit < 16U){ return (1U << src_bit); }
			return 0U;
		case CU_VDEV_HOST_MOUSE:
			switch (src_bit){
				case 0U: return CU_CTR_SNES_M_X;
				case 1U: return CU_CTR_SNES_M_A;
				case 2U: return CU_CTR_SNES_M_B;
				default: return 0U;
			}
		default:
			return 0U;
	}
}

static void cu_vdev_init_binding_maps(cu_vdev_binding_t* bind, cu_vdev_host_type_t type)
{
	auint i;
	if (bind == NULL){ return; }
	for (i = 0U; i < CU_VDEV_REMAP_SLOTS; ++i){
		bind->map_low16[i] = cu_vdev_default_map_for_host_type(type, i);
	}
}

static auint cu_vdev_apply_binding_map(cu_vdev_binding_t const* bind, auint raw_mask)
{
	auint i;
	auint out = 0U;
	if (bind == NULL){ return 0U; }
	for (i = 0U; i < CU_VDEV_REMAP_SLOTS; ++i){
		if ((raw_mask & (1U << i)) != 0U){
			out |= bind->map_low16[i] & 0xFFFFU;
		}
	}
	return out & 0xFFFFU;
}

static void cu_vdev_clear_slot(cu_vdev_t* v)
{
	auint i;

	memset(v, 0, sizeof(*v));
	v->type = CU_VDEV_TYPE_NONE;
	v->haptic_accept_any = TRUE;
	v->haptic_binding = CU_VDEV_INVALID;
	v->haptic_id = 0U;
	v->sm_scale_x_pct = 100U;
	v->sm_scale_y_pct = 100U;
	v->sm_deadzone = 0U;
	v->sm_invert_x = FALSE;
	v->sm_invert_y = FALSE;
	for (i = 0U; i < CU_VDEV_MAX_BINDINGS; ++i){
		v->binding[i].type = CU_VDEV_HOST_NONE;
		v->binding[i].host_index = CU_VDEV_INVALID;
		v->binding[i].flags = 0U;
		cu_vdev_init_binding_maps(&v->binding[i], CU_VDEV_HOST_NONE);
	}
}

static void cu_vdev_copy_name(cu_vdev_t* v, char const* name)
{
	if (name == NULL){
		v->name[0] = 0;
		return;
	}
	strncpy(v->name, name, CU_VDEV_NAME_MAX - 1U);
	v->name[CU_VDEV_NAME_MAX - 1U] = 0;
}

void cu_vdev_reset(void)
{
	auint i;

	for (i = 0U; i < CU_VDEV_MAX; ++i){
		cu_vdev_clear_slot(&cu_vdev_list[i]);
	}
}

auint cu_vdev_create(cu_vdev_type_t type, char const* name)
{
	auint i;

	for (i = 0U; i < CU_VDEV_MAX; ++i){
		if (!cu_vdev_list[i].used){
			cu_vdev_clear_slot(&cu_vdev_list[i]);
			cu_vdev_list[i].used = TRUE;
			cu_vdev_list[i].type = type;
			cu_vdev_copy_name(&cu_vdev_list[i], name);
			return i;
		}
	}

	return CU_VDEV_INVALID;
}

boole cu_vdev_delete(auint id)
{
	if (id >= CU_VDEV_MAX){
		return FALSE;
	}
	cu_vdev_clear_slot(&cu_vdev_list[id]);
	return TRUE;
}

cu_vdev_t* cu_vdev_get_mut(auint id)
{
	if (id >= CU_VDEV_MAX){
		return NULL;
	}
	if (!cu_vdev_list[id].used){
		return NULL;
	}
	return &cu_vdev_list[id];
}

cu_vdev_t const* cu_vdev_get(auint id)
{
	if (id >= CU_VDEV_MAX){
		return NULL;
	}
	if (!cu_vdev_list[id].used){
		return NULL;
	}
	return &cu_vdev_list[id];
}

boole cu_vdev_set_name(auint id, char const* name)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){
		return FALSE;
	}
	cu_vdev_copy_name(v, name);
	return TRUE;
}

boole cu_vdev_set_binding(auint id, auint bind_index,
	cu_vdev_host_type_t host_type, auint host_index, auint flags)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	cu_vdev_binding_t* bind;
	if (v == NULL){
		return FALSE;
	}
	if (bind_index >= CU_VDEV_MAX_BINDINGS){
		return FALSE;
	}
	bind = &v->binding[bind_index];
	if (bind->type != host_type){
		cu_vdev_init_binding_maps(bind, host_type);
	}
	bind->type = host_type;
	bind->host_index = host_index;
	bind->flags = flags;
	return TRUE;
}

boole cu_vdev_set_packet_words(auint id, auint low16, auint high16)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){
		return FALSE;
	}
	v->low16 = low16 & 0xFFFFU;
	v->high16 = high16 & 0xFFFFU;
	return TRUE;
}

boole cu_vdev_reset_binding_map(auint id, auint bind_index)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){
		return FALSE;
	}
	if (bind_index >= CU_VDEV_MAX_BINDINGS){
		return FALSE;
	}
	cu_vdev_init_binding_maps(&v->binding[bind_index], v->binding[bind_index].type);
	return TRUE;
}

boole cu_vdev_reset_supermouse_tuning(auint id)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){
		return FALSE;
	}
	v->sm_scale_x_pct = 100U;
	v->sm_scale_y_pct = 100U;
	v->sm_deadzone = 0U;
	v->sm_invert_x = FALSE;
	v->sm_invert_y = FALSE;
	return TRUE;
}

auint cu_vdev_duplicate(auint id, char const* name)
{
	cu_vdev_t const* src = cu_vdev_get(id);
	auint dst;
	if (src == NULL){
		return CU_VDEV_INVALID;
	}
	dst = cu_vdev_create(src->type, (name != NULL) ? name : src->name);
	if (dst == CU_VDEV_INVALID){
		return CU_VDEV_INVALID;
	}
	cu_vdev_list[dst] = *src;
	cu_vdev_list[dst].used = TRUE;
	cu_vdev_list[dst].rel_x_accum = 0;
	cu_vdev_list[dst].rel_y_accum = 0;
	cu_vdev_list[dst].gun_x = 0;
	cu_vdev_list[dst].gun_y = 0;
	cu_vdev_list[dst].gun_light = 0U;
	cu_vdev_list[dst].gun_trigger = 0U;
	if (name != NULL){
		cu_vdev_copy_name(&cu_vdev_list[dst], name);
	}
	return dst;
}

boole cu_vdev_restore(auint id, cu_vdev_t const* src)
{
	if ((id >= CU_VDEV_MAX) || (src == NULL)){
		return FALSE;
	}
	cu_vdev_clear_slot(&cu_vdev_list[id]);
	if (!src->used){
		return TRUE;
	}
	cu_vdev_list[id] = *src;
	cu_vdev_list[id].used = TRUE;
	if (cu_vdev_list[id].sm_scale_x_pct == 0U){ cu_vdev_list[id].sm_scale_x_pct = 100U; }
	if (cu_vdev_list[id].sm_scale_y_pct == 0U){ cu_vdev_list[id].sm_scale_y_pct = 100U; }
	cu_vdev_list[id].rel_x_accum = 0;
	cu_vdev_list[id].rel_y_accum = 0;
	cu_vdev_list[id].gun_x = 0;
	cu_vdev_list[id].gun_y = 0;
	cu_vdev_list[id].gun_light = 0U;
	cu_vdev_list[id].gun_trigger = 0U;
	cu_vdev_list[id].name[CU_VDEV_NAME_MAX - 1U] = 0;
	return TRUE;
}

static uint8 cu_vdev_encode_delta(sint32 d)
{
	uint8 result;
	if (d < 0){
		result = 0U;
		d = -d;
	}else{
		result = 1U;
	}
	if (d > 127){ d = 127; }
	if (!(d & 64)){ result |= 2U; }
	if (!(d & 32)){ result |= 4U; }
	if (!(d & 16)){ result |= 8U; }
	if (!(d & 8)){  result |= 16U; }
	if (!(d & 4)){  result |= 32U; }
	if (!(d & 2)){  result |= 64U; }
	if (!(d & 1)){  result |= 128U; }
	return result;
}

static sint32 cu_vdev_apply_axis_tuning(cu_vdev_t const* v, sint32 d, boole invert, auint scale_pct)
{
	sint32 mag;
	if (invert){
		d = -d;
	}
	mag = (d < 0) ? -d : d;
	if (mag <= (sint32)(v->sm_deadzone & 0x7FU)){
		return 0;
	}
	if (scale_pct == 0U){
		scale_pct = 100U;
	}
	if (scale_pct != 100U){
		if (d >= 0){
			d = (sint32)(((int64_t)d * (int64_t)scale_pct + 50LL) / 100LL);
		}else{
			d = (sint32)(-(((int64_t)(-d) * (int64_t)scale_pct + 50LL) / 100LL));
		}
	}
	if (d > 127){ return 127; }
	if (d < -127){ return -127; }
	return d;
}

static auint cu_vdev_gun_trigger_from_binding(cu_vdev_binding_t const* bind, auint mouse_buttons)
{
	if (bind == NULL){
		return 0U;
	}
	if ((bind->flags & CU_VDEV_BIND_TRIGGER) == 0U){
		return 0U;
	}
	return ((mouse_buttons & (1U << 0)) != 0U) ? 1U : 0U;
}

boole cu_vdev_build_low_active_packet(auint id, auint port, uint32* packet, auint* bits)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	uint32 out;
	auint low16;
	auint high16;
	auint i;
	boole mouse_delta_used = FALSE;
	boole gun_mouse_state_captured = FALSE;
	sint32 mdx = 0;
	sint32 mdy = 0;
	auint mouse_buttons = 0U;
	auint gun_packet = 0U;

	if ((v == NULL) || (packet == NULL) || (bits == NULL)){
		return FALSE;
	}
	(void)port;

	low16 = v->low16 & 0xFFFFU;
	high16 = v->high16 & 0xFFFFU;
	v->gun_trigger = 0U;

	for (i = 0U; i < CU_VDEV_MAX_BINDINGS; ++i){
		cu_vdev_binding_t const* bind = &v->binding[i];
		auint host_buttons = 0U;
		if (bind->type == CU_VDEV_HOST_NONE){
			continue;
		}

		if (bind->type == CU_VDEV_HOST_GAMECONTROLLER){
			if (ginput_host_get_controller_buttons(bind->host_index, &host_buttons) && (bind->flags & CU_VDEV_BIND_BUTTONS)){
				low16 |= cu_vdev_apply_binding_map(bind, host_buttons);
			}
		}else if (bind->type == CU_VDEV_HOST_KEYBOARD){
			if (bind->flags & CU_VDEV_BIND_BUTTONS){
				low16 |= cu_vdev_apply_binding_map(bind, ginput_host_get_keyboard_buttons());
			}
		}else if (bind->type == CU_VDEV_HOST_MOUSE){
			ginput_host_get_mouse_buttons(&mouse_buttons);
			if ((bind->flags & CU_VDEV_BIND_BUTTONS) != 0U){
				low16 |= cu_vdev_apply_binding_map(bind, mouse_buttons);
			}
			if ((bind->flags & CU_VDEV_BIND_AXES) != 0U){
				if (!mouse_delta_used){
					ginput_host_consume_mouse_delta(&mdx, &mdy);
					mouse_delta_used = TRUE;
				}
				if (v->type == CU_VDEV_TYPE_SUPERMOUSE32){
					sint32 tx = cu_vdev_apply_axis_tuning(v, mdx, v->sm_invert_x, v->sm_scale_x_pct);
					sint32 ty = cu_vdev_apply_axis_tuning(v, mdy, v->sm_invert_y, v->sm_scale_y_pct);
					high16 = (((auint)cu_vdev_encode_delta(tx)) << 8) | ((auint)cu_vdev_encode_delta(ty));
					v->rel_x_accum += tx;
					v->rel_y_accum += ty;
				}
			}
			if ((v->type == CU_VDEV_TYPE_SUPERMOUSE32) && ((v->options & CU_VDEV_OPT_LIGHT_SENSE) != 0U)){
				int mx;
				int my;
				auint gun_trigger;
				(void)SDL_GetMouseState(&mx, &my);
				gun_trigger = cu_vdev_gun_trigger_from_binding(bind, mouse_buttons);
				v->gun_x = (sint32)mx;
				v->gun_y = (sint32)my;
				v->gun_trigger |= gun_trigger;
				gun_packet = cu_gun_build_packet(v->gun_x, v->gun_y, v->gun_trigger, &v->gun_light);
				gun_mouse_state_captured = TRUE;
			}
		}
	}

	if ((v->type == CU_VDEV_TYPE_SUPERMOUSE32) && ((v->options & CU_VDEV_OPT_LIGHT_SENSE) != 0U)){
		int mx;
		int my;
		(void)SDL_GetMouseState(&mx, &my);
		v->gun_x = (sint32)mx;
		v->gun_y = (sint32)my;
		if (!gun_mouse_state_captured){
			v->gun_trigger = 0U;
		}
		gun_packet = cu_gun_build_packet(v->gun_x, v->gun_y, v->gun_trigger, &v->gun_light);
		low16 |= gun_packet & 0xF000U;
	}

	v->low16 = low16 & 0xFFFFU;
	v->high16 = high16 & 0xFFFFU;

	if (v->type == CU_VDEV_TYPE_PAD16){
		out = ((uint32)(~(low16 & 0xFFFFU))) & 0xFFFFU;
		out |= 0xFFFF0000UL;
		*packet = out;
		*bits = 16U;
		return TRUE;
	}

	if (v->type == CU_VDEV_TYPE_SUPERMOUSE32){
		out  = ((uint32)(~(low16  & 0xFFFFU))) & 0xFFFFU;
		out |= (((uint32)(~(high16 & 0xFFFFU))) & 0xFFFFU) << 16;
		*packet = out;
		*bits = 32U;
		return TRUE;
	}

	return FALSE;
}

static void cu_vdev_haptic_set_binding_state(cu_vdev_binding_t const* bind, auint bus_id, auint state)
{
	if (bind == NULL){
		return;
	}
	if ((bind->flags & CU_VDEV_BIND_HAPTIC) == 0U){
		return;
	}
	if ((bind->type != CU_VDEV_HOST_GAMECONTROLLER) &&
	    (bind->type != CU_VDEV_HOST_JOYSTICK)){
		return;
	}
	if (bind->host_index == CU_VDEV_INVALID){
		return;
	}
	cu_hap_set_state_host(bind->host_index, bus_id, state);
}

static void cu_vdev_haptic_start_binding(cu_vdev_binding_t const* bind, auint motor_mask, auint duration_ms)
{
	if (bind == NULL){
		return;
	}
	if ((bind->flags & CU_VDEV_BIND_HAPTIC) == 0U){
		return;
	}
	if ((bind->type != CU_VDEV_HOST_GAMECONTROLLER) &&
	    (bind->type != CU_VDEV_HOST_JOYSTICK)){
		return;
	}
	if (bind->host_index == CU_VDEV_INVALID){
		return;
	}
	cu_hap_start_host(bind->host_index, motor_mask, duration_ms);
}

void cu_vdev_haptic_pulse(auint id, auint motor_mask, auint duration_ms)
{
	cu_vdev_t const* v = cu_vdev_get(id);
	if (v == NULL){
		return;
	}
	if (!v->haptic_enabled){
		return;
	}

	switch ((v->haptic_binding <= 3U) ? v->haptic_binding : 0U){
		case 1U:
			cu_vdev_haptic_start_binding(&v->binding[0], motor_mask, duration_ms);
			break;
		case 2U:
			cu_vdev_haptic_start_binding(&v->binding[1], motor_mask, duration_ms);
			break;
		case 3U:
			cu_vdev_haptic_start_binding(&v->binding[0], motor_mask, duration_ms);
			cu_vdev_haptic_start_binding(&v->binding[1], motor_mask, duration_ms);
			break;
		default:
			break;
	}
}

void cu_vdev_haptic_set_state(auint id, auint bus_id, auint state)
{
	cu_vdev_t const* v = cu_vdev_get(id);
	if (v == NULL){
		return;
	}
	if (!v->haptic_enabled){
		return;
	}
	if ((!v->haptic_accept_any) && ((bus_id & 0x07U) != (v->haptic_id & 0x07U))){
		return;
	}

	switch ((v->haptic_binding <= 3U) ? v->haptic_binding : 0U){
		case 1U:
			cu_vdev_haptic_set_binding_state(&v->binding[0], bus_id, state);
			break;
		case 2U:
			cu_vdev_haptic_set_binding_state(&v->binding[1], bus_id, state);
			break;
		case 3U:
			cu_vdev_haptic_set_binding_state(&v->binding[0], bus_id, state);
			cu_vdev_haptic_set_binding_state(&v->binding[1], bus_id, state);
			break;
		default:
			break;
	}
}

void cu_vdev_haptic_from_frame(auint id, const uint8* data, auint len)
{
	auint payload;
	auint bus_id;
	auint state;

	if ((data == NULL) || (len < 1U)){
		return;
	}
	payload = (auint)data[0];
	if ((payload & CU_HAP_WIRE_CLASS_MASK) != CU_HAP_WIRE_CLASS_VALUE){
		return;
	}
	bus_id = (payload >> 3U) & 0x07U;
	state = (payload >> 1U) & CU_HAP_STATE_MASK;
	cu_vdev_haptic_set_state(id, bus_id, state);
}

char const* cu_vdev_type_name(cu_vdev_type_t type)
{
	switch (type){
		case CU_VDEV_TYPE_NONE:         return "None";
		case CU_VDEV_TYPE_PAD16:        return "Pad16";
		case CU_VDEV_TYPE_SUPERMOUSE32: return "SuperMouse32";
		case CU_VDEV_TYPE_KEYBOARD:     return "Keyboard";
		default:                        return "Unknown";
	}
}
