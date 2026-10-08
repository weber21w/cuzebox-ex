/*
 *  SD card peripheral (on SPI bus)
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



#ifndef CU_SPISD_H
#define CU_SPISD_H



#include "types.h"

/* Runtime SD timing model. NORMAL exactly matches the original hardcoded
** CUzeBox / Uzem-compatible timing constants. */
#define CU_SPISD_PRESET_CUSTOM 0U
#define CU_SPISD_PRESET_SLOW   1U
#define CU_SPISD_PRESET_NORMAL 2U
#define CU_SPISD_PRESET_FAST   3U

typedef struct{
 auint preset;
 auint init_ms;              /* Internal card initialization time */
 auint cmd_wait_bytes;       /* Stuff bytes before an R1 response (0..8 useful) */
 auint read_wait_bytes;      /* Read token / multiblock inter-sector wait bytes */
 auint write_busy_ms;        /* Busy time following writes */
 auint cs_high_ms;           /* Required CS-high time before native init */
 auint init_min_byte_cycles; /* Fastest accepted byte cadence while initializing */
 auint init_max_byte_cycles; /* Slowest accepted byte cadence while initializing */
}cu_spisd_model_t;

/* Fill a timing model from one of CU_SPISD_PRESET_* (CUSTOM returns NORMAL). */
void cu_spisd_model_preset(auint preset, cu_spisd_model_t* out);

/* Apply / inspect the current runtime timing model. This does not reset the
** card state; callers changing a live card should normally reset it. */
void cu_spisd_model_set(cu_spisd_model_t const* model);
cu_spisd_model_t const* cu_spisd_model_get(void);


/* SD card state structure. This isn't really meant to be edited, but it is
** necessary for emulator state dumps. Every value is at most 32 bits. */
typedef struct{
 boole ena;      /* Chip select state, TRUE: Enabled (CS low) */
 boole crc;      /* CRC checking state, TRUE: Enabled */
 auint cc7v;     /* CRC7 value for running CRC calculations */
 auint cc16v;    /* CRC16 value for running CRC calculations */
 auint cc16c;    /* CRC16 collected value for comparison */
 auint enac;     /* Cycle of last chip select toggle */
 auint state;    /* SD card state machine */
 auint next;     /* Next event's cycle. Actual interpretation depends on state. */
 auint recvc;    /* Last receive's cycle, used to determine bus speed where necessary */
 auint evcnt;    /* Event counter, used when a transition needs a certain number of events */
 auint cmd;      /* Command / Response state machine */
 auint crarg;    /* Command / Response argument */
 auint r1;       /* Command response (R1) (8 bits)*/
 auint data;     /* Data waiting to get on the output (8 bits) */
 auint pstat;    /* Packet transmission state machine */
 auint paddr;    /* Packet sector (512 byte units) address */
 auint ppos;     /* Packet transmission byte position */
}cu_state_spisd_t;

/* Optional debugger-only SD protocol transaction history. The declarations
** remain available when the feature is compiled out so API/UI code can report
** that state without carrying trace storage or hot-path hooks. */
#define CU_SPISD_TRACE_CAP              256U
#define CU_SPISD_TRACE_KIND_COMMAND       1U
#define CU_SPISD_TRACE_KIND_BLOCK         2U
#define CU_SPISD_TRACE_KIND_INIT_FAIL     3U
#define CU_SPISD_TRACE_KIND_DATA_CRC      4U
#define CU_SPISD_TRACE_KIND_LATENCY       5U
#define CU_SPISD_TRACE_KIND_ABORT         6U
#define CU_SPISD_TRACE_KIND_DATA_TOKEN    7U
#define CU_SPISD_TRACE_KIND_DATA_END      8U
#define CU_SPISD_TRACE_KIND_CRC_END       9U
#define CU_SPISD_TRACE_KIND_BUSY_END     10U
#define CU_SPISD_TRACE_KIND_FAULT        11U
#define CU_SPISD_TRACE_FLAG_BREAK       0x01U
#define CU_SPISD_TRACE_FLAG_CRC         0x02U
#define CU_SPISD_TRACE_FLAG_INIT_FAIL   0x04U
#define CU_SPISD_TRACE_FLAG_LATENCY     0x08U
#define CU_SPISD_TRACE_FLAG_FAULT       0x10U

typedef struct{
 uint32 seq;
 uint32 transaction_id;
 uint32 cycle;
 uint32 start_cycle;
 uint32 response_start_cycle;
 uint32 latency_cycles;
 uint32 arg;
 uint32 sector;
 uint32 command_byte_cycle[6];
 uint16 command_byte_pc[6];
 uint16 command_byte_row[6];
 uint16 command_byte_beam_cycle[6];
 uint16 pc;
 uint16 start_pc;
 uint16 response_start_pc;
 uint16 row;
 uint16 beam_cycle;
 uint16 start_row;
 uint16 start_beam_cycle;
 uint16 response_start_row;
 uint16 response_start_beam_cycle;
 uint16 crc_calculated;
 uint16 crc_received;
 uint8 kind;
 uint8 cmd;
 uint8 app;
 uint8 r1;
 uint8 state_before;
 uint8 state_after;
 uint8 packet_state;
 uint8 flags;
 uint8 command_crc;
 uint8 command_crc_expected;
 uint8 value;
}cu_spisd_trace_event_t;

void   cu_spisd_trace_enable(boole enable);
boole  cu_spisd_trace_built(void);
boole  cu_spisd_trace_enabled(void);
boole  cu_spisd_trace_active(void);
void   cu_spisd_trace_clear(void);
auint  cu_spisd_trace_count(void);
uint32 cu_spisd_trace_first_seq(void);
boole  cu_spisd_trace_get(uint32 seq, cu_spisd_trace_event_t* out);
void   cu_spisd_trace_break_command_set(auint cmd, boole enable);
void   cu_spisd_trace_break_commands_clear(void);
uint32 cu_spisd_trace_break_command_mask_lo(void);
uint32 cu_spisd_trace_break_command_mask_hi(void);
void   cu_spisd_trace_break_sector_set(boole enable, auint sector);
boole  cu_spisd_trace_break_sector_get(auint* out_sector);
void   cu_spisd_trace_break_init_fail_set(boole enable);
boole  cu_spisd_trace_break_init_fail_get(void);
void   cu_spisd_trace_break_crc_set(boole enable);
boole  cu_spisd_trace_break_crc_get(void);
void   cu_spisd_trace_break_latency_set(boole enable, auint cycles);
boole  cu_spisd_trace_break_latency_get(auint* out_cycles);
boole  cu_spisd_trace_hit_get(boole clear, cu_spisd_trace_event_t* out);
boole  cu_spisd_trace_break_notice_get(boole clear, cu_spisd_trace_event_t* out);

/* Filesystem-aware SD breakpoints. Role values are CU_VFAT_DEBUG_ROLE_*;
** access is a bitmask so reads and writes can be selected independently. */
#define CU_SPISD_FS_ACCESS_READ  0x01U
#define CU_SPISD_FS_ACCESS_WRITE 0x02U
void   cu_spisd_trace_break_fs_role_set(auint role, boole enable);
uint32 cu_spisd_trace_break_fs_role_mask(void);
void   cu_spisd_trace_break_fs_path_set(char const* path);
char const* cu_spisd_trace_break_fs_path_get(void);
void   cu_spisd_trace_break_fs_access_set(auint access);
auint  cu_spisd_trace_break_fs_access_get(void);

/* Deterministic debugger-only SD fault injection. Each slot represents one
** independently armed rule. A rule may be one-shot or persistent and may be
** filtered by command and/or sector. */
#define CU_SPISD_FAULT_CAP 8U
#define CU_SPISD_FAULT_NONE          0U
#define CU_SPISD_FAULT_R1_DELAY      1U /* value = extra response wait bytes */
#define CU_SPISD_FAULT_TOKEN_DELAY   2U /* value = extra read-token wait bytes */
#define CU_SPISD_FAULT_BUSY_EXTEND   3U /* value = extra write busy milliseconds */
#define CU_SPISD_FAULT_R1_BITS       4U /* value = R1 bits to OR */
#define CU_SPISD_FAULT_CMD_CRC       5U /* force R1 CRC error */
#define CU_SPISD_FAULT_DATA_CRC      6U /* corrupt read CRC / reject write CRC */
#define CU_SPISD_FAULT_REJECT        7U /* reject command; value defaults to R1_ERES */

typedef struct{
 boole enabled;
 boole persistent;
 boole command_any;
 boole sector_any;
 auint kind;
 auint command;
 auint sector;
 auint value;
 uint32 fired;
 uint32 last_cycle;
}cu_spisd_fault_rule_t;

boole cu_spisd_fault_built(void);
boole cu_spisd_fault_active(void);
void  cu_spisd_fault_clear(auint slot); /* slot >= CAP clears all */
boole cu_spisd_fault_set(auint slot, cu_spisd_fault_rule_t const* rule);
boole cu_spisd_fault_get(auint slot, cu_spisd_fault_rule_t* out);

/* Optional deterministic debugger-only SD interaction recording/replay. The
** recording is a linear card-visible stream: CS transitions, SD resets, SPI
** BYTE_START events (MISO sampled by the AVR), and BYTE_END events (MOSI
** delivered to the card), each with exact relative timing. Splitting transfers
** preserves CS/reset ordering even if it changes while a byte is in flight.
** Replay restores the captured initial card state,
** validates that the AVR repeats the same interaction, and substitutes the
** captured card response without touching VFAT or the normal SD state machine. */
#define CU_SPISD_REPLAY_EVENT_BYTE_START 1U
#define CU_SPISD_REPLAY_EVENT_BYTE_END   2U
#define CU_SPISD_REPLAY_EVENT_CS         3U
#define CU_SPISD_REPLAY_EVENT_RESET      4U

#define CU_SPISD_REPLAY_MODE_OFF       0U
#define CU_SPISD_REPLAY_MODE_RECORD    1U
#define CU_SPISD_REPLAY_MODE_REPLAY    2U
#define CU_SPISD_REPLAY_MODE_COMPLETE  3U
#define CU_SPISD_REPLAY_MODE_ERROR     4U

#define CU_SPISD_REPLAY_ERR_NONE          0U
#define CU_SPISD_REPLAY_ERR_EVENT_KIND    1U
#define CU_SPISD_REPLAY_ERR_MOSI          2U
#define CU_SPISD_REPLAY_ERR_RECV_CYCLE    3U
#define CU_SPISD_REPLAY_ERR_SEND_CYCLE    4U
#define CU_SPISD_REPLAY_ERR_CS_STATE      5U
#define CU_SPISD_REPLAY_ERR_CS_CYCLE      6U
#define CU_SPISD_REPLAY_ERR_RESET_CYCLE   7U
#define CU_SPISD_REPLAY_ERR_END            8U
#define CU_SPISD_REPLAY_ERR_OVERFLOW       9U
#define CU_SPISD_REPLAY_ERR_ALLOC         10U
#define CU_SPISD_REPLAY_ERR_FILE          11U
#define CU_SPISD_REPLAY_ERR_FORMAT        12U

#define CU_SPISD_REPLAY_MAX_EVENTS 131072U

typedef struct{
 uint32 seq;
 uint32 recv_cycle_delta;
 uint32 send_cycle_delta;
 uint32 cycle_delta;
 uint32 sector;
 uint32 packet_pos;
 uint8 kind;
 uint8 mosi;
 uint8 miso;
 uint8 cs_enabled;
 uint8 state;
 uint8 packet_state;
 uint8 command;
 uint8 r1;
}cu_spisd_replay_event_t;

typedef struct{
 boole built;
 boole strict_timing;
 boole break_on_end;
 boole overflow;
 auint mode;
 auint error;
 uint32 count;
 uint32 capacity;
 uint32 cursor;
 uint32 origin_cycle;
 uint32 replay_origin_cycle;
 uint32 tolerance_cycles;
 uint32 mismatch_index;
 uint32 mismatch_expected;
 uint32 mismatch_actual;
}cu_spisd_replay_status_t;

boole  cu_spisd_replay_built(void);
void   cu_spisd_replay_clear(void);
boole  cu_spisd_replay_record_start(auint cycle);
void   cu_spisd_replay_stop(void);
boole  cu_spisd_replay_start(auint cycle, auint tolerance_cycles);
void   cu_spisd_replay_break_on_end_set(boole enable);
void   cu_spisd_replay_status(cu_spisd_replay_status_t* out);
boole  cu_spisd_replay_get(uint32 index, cu_spisd_replay_event_t* out);
boole  cu_spisd_replay_save(char const* path);
boole  cu_spisd_replay_load(char const* path);


/*
** Resets SD card peripheral. Cycle is the CPU cycle when it happens which
** might be used for emulating timing constraints.
*/
void  cu_spisd_reset(auint cycle);


/*
** Sets chip select's state, TRUE to enable, FALSE to disable.
*/
void  cu_spisd_cs_set(boole ena, auint cycle);


/*
** Sends a byte of data to the SD card. The passed cycle corresponds the cycle
** when it was clocked out of the AVR.
*/
void  cu_spisd_send(auint data, auint cycle);


/*
** Receives a byte of data from the SD card. The passed cycle corresponds the
** cycle when it must start to clock into the AVR. 0xFF is sent when the card
** tri-states the line.
*/
auint cu_spisd_recv(auint cycle);


/*
** Returns SD card state. It may be written, then the cu_spisd_update()
** function has to be called to rebuild any internal state depending on it.
*/
cu_state_spisd_t* cu_spisd_get_state(void);


/*
** Rebuild internal state according to the current state. Call after writing
** the SD card state.
*/
void  cu_spisd_update(void);


#endif
