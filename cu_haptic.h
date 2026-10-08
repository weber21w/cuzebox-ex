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

#ifndef CU_HAPTIC_H
#define CU_HAPTIC_H

#include "types.h"

/* UzeBus haptic payload byte: 0b01iiiMm0 */
#define CU_HAP_WIRE_CLASS_MASK  0xC0U
#define CU_HAP_WIRE_CLASS_VALUE 0x40U
#define CU_HAP_WIRE_ID_MASK     0x38U
#define CU_HAP_WIRE_STATE_MASK  0x06U

/* Haptic state bits (Mm) */
#define CU_HAP_STATE_OFF   0U
#define CU_HAP_STATE_LARGE 1U
#define CU_HAP_STATE_SMALL 2U
#define CU_HAP_STATE_BOTH  3U
#define CU_HAP_STATE_MASK  3U

/* Motor bit mask used by host rumble backends */
#define CU_HAP_MOTOR_LO 0x01U
#define CU_HAP_MOTOR_HI 0x02U

/* Logical ports (legacy direct binding helpers) */
typedef enum{
	CU_HAP_P1 = 0,
	CU_HAP_P2 = 1,
	CU_HAP_M0 = 2,
	CU_HAP_M1 = 3,
	CU_HAP_M2 = 4,
	CU_HAP_MAX = 5
} cu_hap_port_t;

/* Minimal public state (for emulator state dumps) */
typedef struct{
	auint last_mask;  /* Last host motor mask applied (bit0=LO, bit1=HI) */
	auint last_id;    /* Last UzeBus haptic id received (0..7) */
	auint last_state; /* Last decoded CU_HAP_STATE_* value */
} cu_state_hap_t;

/* Initializes haptic subsystem. If 'ena' is FALSE, functions become no-ops. */
void  cu_hap_init(boole ena);

/* Shuts down haptic subsystem and releases host handles if any. */
void  cu_hap_quit(void);

/* Resets per-port serial haptic sessions. */
void  cu_hap_reset_sessions(void);

/* Binds a CU port to a preferred host device index (SDL joystick index). */
void  cu_hap_bind(cu_hap_port_t port, auint host_index);

/* Starts a temporary rumble pulse on 'port' with 'motor_mask' (bits CU_HAP_MOTOR_*). */
void  cu_hap_start(cu_hap_port_t port, auint motor_mask, auint duration_ms);

/* Starts a temporary rumble pulse directly on a host controller / joystick index. */
void  cu_hap_start_host(auint host_index, auint motor_mask, auint duration_ms);

/* Applies persistent haptic state on 'port' from decoded UzeBus Mm bits. */
void  cu_hap_set_state(cu_hap_port_t port, auint bus_id, auint state);

/* Applies persistent haptic state directly on a host controller / joystick index. */
void  cu_hap_set_state_host(auint host_index, auint bus_id, auint state);

/* Stops rumble on 'port' immediately (best effort on host). */
void  cu_hap_stop(cu_hap_port_t port);

/* Consumes decoded UzeBus haptic bytes for a logical port. */
void  cu_hap_from_uzebus(cu_hap_port_t port, const uint8* data, auint len);

/* Selected-slot haptic helpers used by the shared UzeBus session path. */
boole cu_hap_slot_present(auint port);
void  cu_hap_route_payload_port(auint port, uint8 payload);

/* Legacy shim: selected-slot haptics now use the shared controller UzeBus path. */
auint cu_hap_process(auint prev, auint curr);
auint cu_hap_bus_active(auint port);

/* Returns pointer to public state block (for state dumps). */
cu_state_hap_t* cu_hap_get_state(cu_hap_port_t port);

/* Rebuild internal state according to current state (after editing dump). */
void  cu_hap_update(void);

#endif
