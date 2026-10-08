/* cu_multitap.h */

#ifndef CU_MULTITAP_H
#define CU_MULTITAP_H

#include "types.h"
#include "cu_vdev.h"

#define CU_MULTITAP_MAX_PORTS  2U   /* P1, P2 */
#define CU_MULTITAP_MAX_SLOTS  4U   /* up to 4 devices per multitap */

typedef struct{
	auint tap_present;
	auint selected_slot;
	auint probe_pending;
	auint last_activity;
	auint slot_vdev[CU_MULTITAP_MAX_SLOTS];
} cu_multitap_port_state_t;

typedef struct{
	cu_multitap_port_state_t port[CU_MULTITAP_MAX_PORTS];
	auint ctrl_state;
	auint ctrl_arm_pulses;
	auint ctrl_cmd_pulses;
	auint ctrl_target_mask;
} cu_state_multitap_t;

void  cu_multitap_get_state(cu_state_multitap_t* state);
void  cu_multitap_set_state(cu_state_multitap_t const* state);

void  cu_multitap_reset(void);

void  cu_multitap_set_tap_present(auint port, auint present);
auint cu_multitap_get_tap_present(auint port);

/* Slot currently exposed by the tap. If no tap is present, reads as slot 0. */
void  cu_multitap_set_active_slot(auint port, auint slot);
auint cu_multitap_get_active_slot(auint port);

void  cu_multitap_set_slot_vdev(auint port, auint slot, auint vdev);
auint cu_multitap_get_slot_vdev(auint port, auint slot);

void  cu_multitap_set_probe_pending(auint port, auint pending);
auint cu_multitap_get_probe_pending(auint port);
uint8 cu_multitap_get_probe_byte(auint port);
void  cu_multitap_touch_activity(auint port, auint tick);
auint cu_multitap_get_last_activity(auint port);

/* Helper: given per-slot button states, pick the one mapped to the port */
auint cu_multitap_select_buttons(auint port, auint const* slot_buttons);

/* Legacy UzeBus slot-select helper (kept for compatibility with older stubs) */
void  cu_multitap_uzebus_cmd(auint cmd);

/*
** Latch-only UzeTap control path.
**
** Protocol implemented here:
**  - ARM:    3 latch pulses with CLOCK held high and both DATA lines low.
**  - ADDR:   host drives one or both DATA lines high and keeps them high.
**            Bit0 targets P1, bit1 targets P2.
**  - CMD:    while addressed DATA is still held high, host emits latch pulses:
**              1 pulse = probe next read
**              2 pulse = select slot 0
**              3 pulse = select slot 1
**              4 pulse = select slot 2
**              5 pulse = select slot 3
**  - COMMIT: host releases DATA low again while CLOCK is high and LATCH is low.
*/
void  cu_multitap_process_ctrl(auint prev, auint curr, auint tick);
void  cu_multitap_apply_timeouts(auint tick);
auint cu_multitap_ctrl_active(void);

#endif
