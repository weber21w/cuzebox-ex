/*
 *  Controller emulation
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



#ifndef CU_CTR_H
#define CU_CTR_H



#include "types.h"
#include "cu_multitap.h"


typedef struct{
	auint state;
	auint clock;
	auint tx_byte;
	auint rx_byte;
	auint dev_type;
	auint kbd_cmd_hold;
	auint payload_seen;
} cu_ctr_ubus_port_state_t;

typedef struct{
	auint buttons[2][CU_MULTITAP_MAX_SLOTS];
	uint64_t blatch[2];
	auint shift_count[2];
	auint probe_inflight[2];
	boole override_enable[2][CU_MULTITAP_MAX_SLOTS];
	uint32 override_packet[2][CU_MULTITAP_MAX_SLOTS];
	auint override_bits[2][CU_MULTITAP_MAX_SLOTS];
	cu_ctr_ubus_port_state_t ubus[2];
} cu_state_ctr_t;

void  cu_ctr_get_state(cu_state_ctr_t* state);
void  cu_ctr_set_state(cu_state_ctr_t const* state);


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

/* SNES controller buttons, masks */
#define CU_CTR_SNES_M_B        ((auint)(1U) << CU_CTR_SNES_B)
#define CU_CTR_SNES_M_Y        ((auint)(1U) << CU_CTR_SNES_Y)
#define CU_CTR_SNES_M_SELECT   ((auint)(1U) << CU_CTR_SNES_SELECT)
#define CU_CTR_SNES_M_START    ((auint)(1U) << CU_CTR_SNES_START)
#define CU_CTR_SNES_M_UP       ((auint)(1U) << CU_CTR_SNES_UP)
#define CU_CTR_SNES_M_DOWN     ((auint)(1U) << CU_CTR_SNES_DOWN)
#define CU_CTR_SNES_M_LEFT     ((auint)(1U) << CU_CTR_SNES_LEFT)
#define CU_CTR_SNES_M_RIGHT    ((auint)(1U) << CU_CTR_SNES_RIGHT)
#define CU_CTR_SNES_M_A        ((auint)(1U) << CU_CTR_SNES_A)
#define CU_CTR_SNES_M_X        ((auint)(1U) << CU_CTR_SNES_X)
#define CU_CTR_SNES_M_LSH      ((auint)(1U) << CU_CTR_SNES_LSH)
#define CU_CTR_SNES_M_RSH      ((auint)(1U) << CU_CTR_SNES_RSH)



/*
** Resets controllers to all inactive.
*/
void  cu_ctr_reset(void);


/*
** Accepting previous and currently written PORTA value, processes hardware
** related to the Uzebox's controllers. Returns the new PINA value.
*/
auint cu_ctr_process(auint prev, auint curr);


/*
** Sends current SNES controller state. The buttons are a combination of masks
** generated from the definitions.
*/
void  cu_ctr_setsnes(auint player, auint buttons);

/*
** Sends current SNES controller state for a specific multitap slot.
*/
void  cu_ctr_setsnes_slot(auint player, auint slot, auint buttons);


/*
** Retrieves current SNES controller state for the currently active slot.
*/
boole cu_ctr_getsnes_state(auint player, auint* buttons);

/*
** Retrieves current SNES controller state for a specific multitap slot.
*/
boole cu_ctr_getsnes_slot_state(auint player, auint slot, auint* buttons);

/*
** Retrieves the effective active-high packet state for a specific slot. This
** includes virtual-device packet formats (16/32-bit) but excludes one-shot tap
** probe prepends.
*/
boole cu_ctr_getslot_packet_state(auint player, auint slot, auint* packet, auint* bits);

/*
** Retrieves the expected packet width (16/32 bits) for a specific slot.
*/
boole cu_ctr_getslot_packet_format(auint player, auint slot, auint* bits);

/*
** Overrides the effective packet state for a specific slot, for example during
** netplay rollback input playback.
*/
void  cu_ctr_setslot_packet_override(auint player, auint slot, boole enable, auint packet, auint bits);
void  cu_ctr_clear_packet_overrides(void);



#ifdef ENABLE_ICAP
#ifndef ENABLE_IREP
/*
** Retrieves current SNES controller state. The buttons are a combination of
** masks generated from the definitions.
*/
void  cu_ctr_getsnes(auint player, auint* buttons);
#endif
#endif




/*
** Sends state of single SNES controller button. The button is a single
** (non-mask) definition.
*/
void  cu_ctr_setsnes_single(auint player, auint button, boole press);


#endif
