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

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define CU_KBD_PORTS 2U
#define CU_KBD_QUEUE_SIZE 512U

typedef struct{
	auint state;
	auint clock;
	auint data_in;
	auint data_out;
	auint data_hold;
} cu_kbd_port_state_t;

typedef struct{
	auint in;
	auint out;
	uint8 data[CU_KBD_QUEUE_SIZE];
} cu_kbd_queue_state_t;

typedef struct{
	auint legacy_state;
	auint legacy_clock;
	auint legacy_data_in;
	auint legacy_data_out;
	auint enabled;
	auint bypassed;
	auint legacy_queue_in;
	auint legacy_queue_out;
	uint8 legacy_queue[CU_KBD_QUEUE_SIZE];
	auint held_lctrl;
	cu_kbd_port_state_t port[CU_KBD_PORTS];
	cu_kbd_queue_state_t port_queue[CU_KBD_PORTS];
	cu_kbd_queue_state_t port_pending[CU_KBD_PORTS];
	boole port_active[CU_KBD_PORTS];
} cu_state_kbd_t;

void  cu_kbd_get_state(cu_state_kbd_t* state);
void  cu_kbd_set_state(cu_state_kbd_t const* state);

extern auint cu_kbd_state;
extern auint cu_kbd_clock;
extern auint cu_kbd_data_in;
extern auint cu_kbd_data_out;
extern auint cu_kbd_enabled;
extern auint cu_kbd_bypassed; /* don't check for start condition, allows user to break out of keyboard passthrough mode and use emulator controls */

extern auint cu_kbd_queue_in;
extern auint cu_kbd_queue_out;
extern uint8 cu_kbd_queue[512];

extern auint cu_kbd_held_lctrl;


/* Keyboard Dongle defines */
#define KBD_STOP      0
#define KBD_TX_START  1
#define KBD_TX_READY  2

#define KBD_SEND_KEY 0x00
#define KBD_SEND_END 0x01
#define KBD_SEND_DEVICE_ID 0x02
#define KBD_SEND_FIRMWARE_REV 0x03
#define KBD_RESET 0x7f

#define KBD_MODIFY_KEYBOARD  0x5E /* Ctrl+F1, disable/enable  keyboard */
#define KBD_MODIFY_MOUSE     0x5F /* Ctrl+F2, adjust mouse in emulator while in keyboard mode */
void cu_kbd_handle_key(SDL_Event const* ev);
auint cu_kbd_process(auint prev, auint curr);
auint cu_kbd_capture_active(void);
void  cu_kbd_reset_sessions(void);
void  cu_kbd_set_vdev_session(auint port, boole active);
typedef boole (*cu_kbd_enqueue_hook_t)(auint port, uint8 value);
typedef void (*cu_kbd_visible_hook_t)(auint port, uint8 value);
void  cu_kbd_set_enqueue_hook(cu_kbd_enqueue_hook_t hook);
void  cu_kbd_set_visible_hook(cu_kbd_visible_hook_t hook);
void  cu_kbd_debug_enqueue_port_byte(auint port, uint8 value);
void  cu_kbd_frame_boundary(void);
auint cu_kbd_response_for_command(auint cmd);
auint cu_kbd_response_for_port_command(auint port, auint cmd);




/*
** Set the keyboard status to enabled
*/
void  cu_kbd_set_enabled(uint8 val);



/*
** Get the keyboard enabled status
*/
auint  cu_kbd_get_enabled(void);


#endif /* KEYBOARD_H */
