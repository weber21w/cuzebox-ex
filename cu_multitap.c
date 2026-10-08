/* cu_multitap.c */

#include "cu_multitap.h"
#include <string.h>

#define CU_MULTITAP_TIMEOUT_CYCLES 4000000U

#define CU_MTAP_DATA_P1   0x01U
#define CU_MTAP_DATA_P2   0x02U
#define CU_MTAP_DATA_MASK 0x03U
#define CU_MTAP_LATCH     0x04U
#define CU_MTAP_CLOCK     0x08U

#define CU_MTAP_CMD_PROBE   1U
#define CU_MTAP_CMD_SLOT0   2U
#define CU_MTAP_CMD_SLOT1   3U
#define CU_MTAP_CMD_SLOT2   4U
#define CU_MTAP_CMD_SLOT3   5U

typedef struct{
	auint tap_present;
	auint selected_slot;
	auint probe_pending;
	auint last_activity;
	auint slot_vdev[CU_MULTITAP_MAX_SLOTS];
} cu_multitap_port_t;

typedef enum{
	CU_MTCTRL_IDLE = 0,
	CU_MTCTRL_WAIT_ADDR,
	CU_MTCTRL_WAIT_CMD
} cu_multitap_ctrl_state_t;

static cu_multitap_port_t cu_multitap_port[CU_MULTITAP_MAX_PORTS];
static cu_multitap_ctrl_state_t cu_multitap_ctrl_state;
static auint cu_multitap_ctrl_arm_pulses;
static auint cu_multitap_ctrl_cmd_pulses;
static auint cu_multitap_ctrl_target_mask;

void cu_multitap_get_state(cu_state_multitap_t* state)
{
	if (state == NULL){ return; }
	memset(state, 0, sizeof(*state));
	memcpy(state->port, cu_multitap_port, sizeof(cu_multitap_port));
	state->ctrl_state = (auint)cu_multitap_ctrl_state;
	state->ctrl_arm_pulses = cu_multitap_ctrl_arm_pulses;
	state->ctrl_cmd_pulses = cu_multitap_ctrl_cmd_pulses;
	state->ctrl_target_mask = cu_multitap_ctrl_target_mask;
}

void cu_multitap_set_state(cu_state_multitap_t const* state)
{
	if (state == NULL){ return; }
	memcpy(cu_multitap_port, state->port, sizeof(cu_multitap_port));
	cu_multitap_ctrl_state = (cu_multitap_ctrl_state_t)(state->ctrl_state);
	cu_multitap_ctrl_arm_pulses = state->ctrl_arm_pulses;
	cu_multitap_ctrl_cmd_pulses = state->ctrl_cmd_pulses;
	cu_multitap_ctrl_target_mask = state->ctrl_target_mask;
}

static void cu_multitap_ctrl_reset(void)
{
	cu_multitap_ctrl_state = CU_MTCTRL_IDLE;
	cu_multitap_ctrl_arm_pulses = 0U;
	cu_multitap_ctrl_cmd_pulses = 0U;
	cu_multitap_ctrl_target_mask = 0U;
}

static void cu_multitap_ctrl_commit(auint tick)
{
	auint port;
	auint cmd = cu_multitap_ctrl_cmd_pulses;

	if (cmd == 0U){
		cu_multitap_ctrl_reset();
		return;
	}

	for (port = 0U; port < CU_MULTITAP_MAX_PORTS; ++port){
		auint bit = (1U << port);
		if ((cu_multitap_ctrl_target_mask & bit) == 0U){
			continue;
		}
		if (cu_multitap_port[port].tap_present == 0U){
			continue;
		}

		switch (cmd){
			case CU_MTAP_CMD_PROBE:
				cu_multitap_port[port].probe_pending = 1U;
				break;
			case CU_MTAP_CMD_SLOT0:
				cu_multitap_port[port].selected_slot = 0U;
				break;
			case CU_MTAP_CMD_SLOT1:
				cu_multitap_port[port].selected_slot = 1U;
				break;
			case CU_MTAP_CMD_SLOT2:
				cu_multitap_port[port].selected_slot = 2U;
				break;
			case CU_MTAP_CMD_SLOT3:
				cu_multitap_port[port].selected_slot = 3U;
				break;
			default:
				break;
		}

		if ((cmd >= CU_MTAP_CMD_PROBE) && (cmd <= CU_MTAP_CMD_SLOT3)){
			cu_multitap_port[port].last_activity = tick;
		}
	}

	cu_multitap_ctrl_reset();
}

void cu_multitap_reset(void)
{
	auint i;
	auint s;

	for (i = 0U; i < CU_MULTITAP_MAX_PORTS; ++i){
		cu_multitap_port[i].tap_present = 0U;
		cu_multitap_port[i].selected_slot = 0U;
		cu_multitap_port[i].probe_pending = 0U;
		cu_multitap_port[i].last_activity = 0U;
		for (s = 0U; s < CU_MULTITAP_MAX_SLOTS; ++s){
			cu_multitap_port[i].slot_vdev[s] = CU_VDEV_INVALID;
		}
	}
	cu_multitap_ctrl_reset();
}

void cu_multitap_set_tap_present(auint port, auint present)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return; }
	cu_multitap_port[port].tap_present = (present != 0U) ? 1U : 0U;
	if (cu_multitap_port[port].tap_present == 0U){
		cu_multitap_port[port].selected_slot = 0U;
		cu_multitap_port[port].probe_pending = 0U;
	}
}

auint cu_multitap_get_tap_present(auint port)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return 0U; }
	return cu_multitap_port[port].tap_present;
}

void cu_multitap_set_active_slot(auint port, auint slot)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return; }
	cu_multitap_port[port].selected_slot = slot;
}

auint cu_multitap_get_active_slot(auint port)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return 0U; }
	if (cu_multitap_port[port].tap_present == 0U){
		return 0U;
	}
	return cu_multitap_port[port].selected_slot;
}

void cu_multitap_set_slot_vdev(auint port, auint slot, auint vdev)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return; }
	cu_multitap_port[port].slot_vdev[slot] = vdev;
}

auint cu_multitap_get_slot_vdev(auint port, auint slot)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return CU_VDEV_INVALID; }
	if (slot >= CU_MULTITAP_MAX_SLOTS){ return CU_VDEV_INVALID; }
	return cu_multitap_port[port].slot_vdev[slot];
}

void cu_multitap_set_probe_pending(auint port, auint pending)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return; }
	cu_multitap_port[port].probe_pending = (pending != 0U) ? 1U : 0U;
}

auint cu_multitap_get_probe_pending(auint port)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return 0U; }
	return cu_multitap_port[port].probe_pending;
}

uint8 cu_multitap_get_probe_byte(auint port)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return 0U; }
	if (cu_multitap_port[port].tap_present == 0U){ return 0U; }
	return (uint8)(0xA0U | (cu_multitap_port[port].selected_slot & 0x0FU));
}

void cu_multitap_touch_activity(auint port, auint tick)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return; }
	cu_multitap_port[port].last_activity = tick;
}

auint cu_multitap_get_last_activity(auint port)
{
	if (port >= CU_MULTITAP_MAX_PORTS){ return 0U; }
	return cu_multitap_port[port].last_activity;
}

auint cu_multitap_select_buttons(auint port, auint const* slot_buttons)
{
	auint slot = cu_multitap_get_active_slot(port);
	if (slot >= CU_MULTITAP_MAX_SLOTS) slot = 0U;
	return slot_buttons[slot];
}

/* --- Legacy UzeBus binding -------------------------------------------- */
#define UZEBUS_MTAP_CLASS_MASK   0xC0U
#define UZEBUS_MTAP_CLASS_VAL    0x80U  /* 10xxxxxx */

void cu_multitap_uzebus_cmd(auint cmd)
{
	auint port;
	auint slot;

	if ((cmd & UZEBUS_MTAP_CLASS_MASK) != UZEBUS_MTAP_CLASS_VAL) {
		return;
	}

	port = (cmd >> 3) & 0x07U;
	slot = (cmd >> 1) & 0x03U;

	cu_multitap_set_active_slot(port, slot);
}

void cu_multitap_apply_timeouts(auint tick)
{
	auint port;
	for (port = 0U; port < CU_MULTITAP_MAX_PORTS; ++port){
		if (cu_multitap_port[port].tap_present == 0U){
			continue;
		}
		if (cu_multitap_port[port].selected_slot == 0U){
			continue;
		}
		if (WRAP32(tick - cu_multitap_port[port].last_activity) >= CU_MULTITAP_TIMEOUT_CYCLES){
			cu_multitap_port[port].selected_slot = 0U;
		}
	}
}

auint cu_multitap_ctrl_active(void)
{
	return (cu_multitap_ctrl_state != CU_MTCTRL_IDLE) ? 1U : 0U;
}

void cu_multitap_process_ctrl(auint prev, auint curr, auint tick)
{
	auint rise = (prev ^ curr) & curr;
	auint fall = (prev ^ curr) & prev;
	auint clock_high = ((curr & CU_MTAP_CLOCK) != 0U) ? 1U : 0U;
	auint data_mask = curr & CU_MTAP_DATA_MASK;
	(void)tick;

	if ((curr & CU_MTAP_CLOCK) == 0U){
		if ((cu_multitap_ctrl_state == CU_MTCTRL_WAIT_ADDR) ||
		    (cu_multitap_ctrl_state == CU_MTCTRL_WAIT_CMD)){
			cu_multitap_ctrl_reset();
			return;
		}
		cu_multitap_ctrl_arm_pulses = 0U;
		return;
	}

	switch (cu_multitap_ctrl_state){
		default:
		case CU_MTCTRL_IDLE:
			if ((data_mask != 0U) || ((fall & CU_MTAP_CLOCK) != 0U)){
				cu_multitap_ctrl_arm_pulses = 0U;
				return;
			}
			if ((rise & CU_MTAP_LATCH) != 0U){
				cu_multitap_ctrl_arm_pulses ++;
				if (cu_multitap_ctrl_arm_pulses > 3U){
					cu_multitap_ctrl_arm_pulses = 1U;
				}
				return;
			}
			if (((fall & CU_MTAP_LATCH) != 0U) && (cu_multitap_ctrl_arm_pulses >= 3U) && clock_high){
				cu_multitap_ctrl_state = CU_MTCTRL_WAIT_ADDR;
				cu_multitap_ctrl_cmd_pulses = 0U;
				cu_multitap_ctrl_target_mask = 0U;
				if (data_mask != 0U){
					cu_multitap_ctrl_state = CU_MTCTRL_WAIT_CMD;
					cu_multitap_ctrl_target_mask = data_mask;
				}
				return;
			}
			break;

		case CU_MTCTRL_WAIT_ADDR:
			if ((rise & CU_MTAP_LATCH) != 0U){
				cu_multitap_ctrl_reset();
				return;
			}
			if ((curr & (CU_MTAP_CLOCK | CU_MTAP_LATCH)) == CU_MTAP_CLOCK){
				if (data_mask != 0U){
					cu_multitap_ctrl_state = CU_MTCTRL_WAIT_CMD;
					cu_multitap_ctrl_target_mask = data_mask;
					cu_multitap_ctrl_cmd_pulses = 0U;
				}
			}
			break;

		case CU_MTCTRL_WAIT_CMD:
			if (data_mask == 0U){
				if ((curr & (CU_MTAP_CLOCK | CU_MTAP_LATCH)) == CU_MTAP_CLOCK){
					cu_multitap_ctrl_commit(tick);
					return;
				}
				cu_multitap_ctrl_reset();
				return;
			}
			if (data_mask != cu_multitap_ctrl_target_mask){
				cu_multitap_ctrl_reset();
				return;
			}
			if ((rise & CU_MTAP_LATCH) != 0U){
				cu_multitap_ctrl_cmd_pulses ++;
			}
			break;
	}
}
