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



#include "cu_spisd.h"
#include "cu_vfat.h"
#if defined(ENABLE_SD_TRACE) || defined(ENABLE_SD_REPLAY)
#include "cu_avr.h"
#endif
#ifdef ENABLE_SD_REPLAY
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif



/* SD card state */
static cu_state_spisd_t sd_state;

/* CRC7 table (for SD commands) */
static auint sd_crc7_table[256];

/* CRC16 table (for SD data) */
static auint sd_crc16_table[256];



/* 400KHz SPI transfer clocks */
#define SPI_400   ( 71U * 8U)
/* 100KHz SPI transfer clocks */
#define SPI_100   (287U * 8U)
/* 1ms clocks */
#define SPI_1MS   28634U

/* Original hardcoded timing values. CU_SPISD_PRESET_NORMAL is deliberately
** defined to reproduce these exactly. */
#define SD_NORMAL_INIF_HI   16U
#define SD_NORMAL_INIF_LO   SPI_100
#define SD_NORMAL_INICS_T   0U
#define SD_NORMAL_INIMS     500U
#define SD_NORMAL_CMD_N     0U
#define SD_NORMAL_READ_P_N  2U
#define SD_NORMAL_WRITE_T   100U

static cu_spisd_model_t sd_model = {
 CU_SPISD_PRESET_NORMAL, SD_NORMAL_INIMS, SD_NORMAL_CMD_N, SD_NORMAL_READ_P_N,
 SD_NORMAL_WRITE_T, SD_NORMAL_INICS_T, SD_NORMAL_INIF_HI, SD_NORMAL_INIF_LO
};

void cu_spisd_model_preset(auint preset, cu_spisd_model_t* out)
{
 if (out == NULL){ return; }
 switch (preset){
  case CU_SPISD_PRESET_SLOW:
   out->preset = CU_SPISD_PRESET_SLOW;
   out->init_ms = 1000U;
   out->cmd_wait_bytes = 4U;
   out->read_wait_bytes = 8U;
   out->write_busy_ms = 250U;
   out->cs_high_ms = 2U;
   out->init_min_byte_cycles = SD_NORMAL_INIF_HI;
   out->init_max_byte_cycles = SD_NORMAL_INIF_LO;
   break;
  case CU_SPISD_PRESET_FAST:
   out->preset = CU_SPISD_PRESET_FAST;
   out->init_ms = 50U;
   out->cmd_wait_bytes = 0U;
   out->read_wait_bytes = 0U;
   out->write_busy_ms = 10U;
   out->cs_high_ms = 0U;
   out->init_min_byte_cycles = SD_NORMAL_INIF_HI;
   out->init_max_byte_cycles = SD_NORMAL_INIF_LO;
   break;
  case CU_SPISD_PRESET_NORMAL:
  default:
   out->preset = CU_SPISD_PRESET_NORMAL;
   out->init_ms = SD_NORMAL_INIMS;
   out->cmd_wait_bytes = SD_NORMAL_CMD_N;
   out->read_wait_bytes = SD_NORMAL_READ_P_N;
   out->write_busy_ms = SD_NORMAL_WRITE_T;
   out->cs_high_ms = SD_NORMAL_INICS_T;
   out->init_min_byte_cycles = SD_NORMAL_INIF_HI;
   out->init_max_byte_cycles = SD_NORMAL_INIF_LO;
   break;
 }
}

void cu_spisd_model_set(cu_spisd_model_t const* model)
{
 if (model == NULL){ return; }
 sd_model = *model;
 if (sd_model.cmd_wait_bytes > 255U){ sd_model.cmd_wait_bytes = 255U; }
 if (sd_model.read_wait_bytes > 65535U){ sd_model.read_wait_bytes = 65535U; }
 if (sd_model.init_min_byte_cycles == 0U){ sd_model.init_min_byte_cycles = 1U; }
 if (sd_model.init_max_byte_cycles < sd_model.init_min_byte_cycles){
  sd_model.init_max_byte_cycles = sd_model.init_min_byte_cycles;
 }
}

cu_spisd_model_t const* cu_spisd_model_get(void)
{
 return &sd_model;
}


/* SD card state machine states */
/* Uninitialized: waiting for CS and DI going high */
#define STAT_UNINIT     0U
/* Native mode initializing: waiting for 74 pulses (10 data transmissions of 0xFF) */
#define STAT_NINIT      1U
/* Native mode */
#define STAT_NATIVE     2U
/* Idle state */
#define STAT_IDLE       3U
/* Verified state (a CMD8 was sent, so ACMD41 becomes enabled) */
#define STAT_VERIFIED   4U
/* Initializing */
#define STAT_IINIT      5U
/* Available for data transfer */
#define STAT_AVAIL      6U
/* Read single block */
#define STAT_CMD17      7U
/* Read multiple blocks */
#define STAT_CMD18      8U
/* Write single block */
#define STAT_CMD24      9U
/* Write multiple blocks */
#define STAT_CMD25     10U


/* Command state machine. The low 6 bits hold the command */
/* Receiving flag: If set, a command is under reception */
#define SCMD_R      0x100U
/* Application flag: If set, the next command will be an App Command */
#define SCMD_A      0x040U
/* Ticking out command response time */
#define SCMD_N      0x200U
/* Ticking out extra response bytes (beyond the R1) */
#define SCMD_X      0x400U

/* R1 Response flags */
#define R1_IDLE      0x01U
#define R1_ERES      0x02U
#define R1_ILL       0x04U
#define R1_CRC       0x08U
#define R1_ESEQ      0x10U
#define R1_ADDR      0x20U
#define R1_PAR       0x40U

/* Data transmission state machine */
/* Idle */
#define PSTAT_IDLE      0U
/* Read data preparation & data token */
#define PSTAT_RPREP     1U
/* Read data bytes (512) */
#define PSTAT_RDATA     2U
/* Read CRC bytes (2) */
#define PSTAT_RCRC      3U
/* CMD24 Write wait for data token */
#define PSTAT_W24PREP   4U
/* CMD24 Write accept data (512) */
#define PSTAT_W24DATA   5U
/* CMD24 Write accept CRC (2) */
#define PSTAT_W24CRC    6U
/* CMD25 Write wait for data token */
#define PSTAT_W25PREP   7U
/* CMD25 Write accept data (512) */
#define PSTAT_W25DATA   8U
/* CMD25 Write accept CRC (2) */
#define PSTAT_W25CRC    9U
/* Busy after the end of writes */
#define PSTAT_WBUSY    10U

#ifdef ENABLE_SD_TRACE
/* Debug-only SD protocol tracer. The emulator byte path consults exactly one
** shared flag; history and all break criteria are folded into that flag. */
static boole sd_debug_active = FALSE;
static boole sd_trace_on = FALSE;
static cu_spisd_trace_event_t sd_trace_buf[CU_SPISD_TRACE_CAP];
static uint32 sd_trace_seq = 0U;
static auint sd_trace_count = 0U;
static uint32 sd_break_cmd_lo = 0U;
static uint32 sd_break_cmd_hi = 0U;
static boole sd_break_sector_on = FALSE;
static auint sd_break_sector = 0U;
static boole sd_break_init_fail = FALSE;
static boole sd_break_crc = FALSE;
static boole sd_break_latency = FALSE;
static auint sd_break_latency_cycles = 0U;
static uint32 sd_break_fs_role_mask = 0U;
static char sd_break_fs_path[CU_VFAT_DEBUG_PATH_SIZE] = {0};
static auint sd_break_fs_access = CU_SPISD_FS_ACCESS_READ | CU_SPISD_FS_ACCESS_WRITE;
static boole sd_hit_valid = FALSE;
static cu_spisd_trace_event_t sd_hit;
static boole sd_break_notice_valid = FALSE;
static cu_spisd_trace_event_t sd_break_notice;

static auint sd_dbg_prev_cmd = 0U;
static auint sd_dbg_prev_state = STAT_UNINIT;
static auint sd_dbg_prev_pstat = PSTAT_IDLE;
static auint sd_dbg_prev_paddr = 0U;
static boole sd_dbg_cmd_active = FALSE;
static auint sd_dbg_cmd = 0U;
static boole sd_dbg_cmd_app = FALSE;
static auint sd_dbg_cmd_arg = 0U;
static auint sd_dbg_cmd_start = 0U;
static auint sd_dbg_cmd_response_start = 0U;
static auint sd_dbg_cmd_start_state = STAT_UNINIT;
static boole sd_dbg_latency_reported = FALSE;
static uint32 sd_dbg_next_transaction_id = 1U;
static uint32 sd_dbg_cmd_transaction_id = 0U;
static uint32 sd_dbg_data_transaction_id = 0U;
static auint sd_dbg_cmd_start_pc = 0U;
static auint sd_dbg_cmd_start_row = 0U;
static auint sd_dbg_cmd_start_beam_cycle = 0U;
static auint sd_dbg_cmd_response_pc = 0U;
static auint sd_dbg_cmd_response_row = 0U;
static auint sd_dbg_cmd_response_beam_cycle = 0U;
static auint sd_dbg_cmd_crc = 0U;
static auint sd_dbg_cmd_crc_expected = 0U;
static auint sd_dbg_cmd_byte_cycle[6];
static auint sd_dbg_cmd_byte_pc[6];
static auint sd_dbg_cmd_byte_row[6];
static auint sd_dbg_cmd_byte_beam_cycle[6];
static auint sd_dbg_data_cmd = 0U;
static boole sd_dbg_data_app = FALSE;
static auint sd_dbg_data_arg = 0U;
static auint sd_dbg_data_start_cycle = 0U;
static auint sd_dbg_data_start_pc = 0U;
static auint sd_dbg_data_start_row = 0U;
static auint sd_dbg_data_start_beam_cycle = 0U;

static void cu_spisd_debug_sync(void)
{
 auint i;
 sd_dbg_prev_cmd = sd_state.cmd;
 sd_dbg_prev_state = sd_state.state;
 sd_dbg_prev_pstat = sd_state.pstat;
 sd_dbg_prev_paddr = sd_state.paddr;
 sd_dbg_cmd_active = FALSE;
 sd_dbg_cmd = 0U;
 sd_dbg_cmd_app = FALSE;
 sd_dbg_cmd_arg = 0U;
 sd_dbg_cmd_start = 0U;
 sd_dbg_cmd_response_start = 0U;
 sd_dbg_cmd_start_state = sd_state.state;
 sd_dbg_latency_reported = FALSE;
 sd_dbg_cmd_transaction_id = 0U;
 sd_dbg_data_transaction_id = 0U;
 sd_dbg_cmd_start_pc = 0U;
 sd_dbg_cmd_start_row = 0U;
 sd_dbg_cmd_start_beam_cycle = 0U;
 sd_dbg_cmd_response_pc = 0U;
 sd_dbg_cmd_response_row = 0U;
 sd_dbg_cmd_response_beam_cycle = 0U;
 sd_dbg_cmd_crc = 0U;
 sd_dbg_cmd_crc_expected = 0U;
 for (i = 0U; i < 6U; ++i){ sd_dbg_cmd_byte_cycle[i]=0U; sd_dbg_cmd_byte_pc[i]=0U; sd_dbg_cmd_byte_row[i]=0U; sd_dbg_cmd_byte_beam_cycle[i]=0U; }
 sd_dbg_data_cmd = 0U;
 sd_dbg_data_app = FALSE;
 sd_dbg_data_arg = 0U;
 sd_dbg_data_start_cycle = 0U;
 sd_dbg_data_start_pc = 0U;
 sd_dbg_data_start_row = 0U;
 sd_dbg_data_start_beam_cycle = 0U;
}

static void cu_spisd_debug_refresh(void)
{
 boole was = sd_debug_active;
 sd_debug_active = sd_trace_on || (sd_break_cmd_lo != 0U) || (sd_break_cmd_hi != 0U) ||
                   sd_break_sector_on || sd_break_init_fail || sd_break_crc || sd_break_latency ||
                   (sd_break_fs_role_mask != 0U) || (sd_break_fs_path[0] != 0);
 if ((!was) && sd_debug_active){ cu_spisd_debug_sync(); }
}

static boole cu_spisd_debug_cmd_break(auint cmd)
{
 if (cmd < 32U){ return ((sd_break_cmd_lo & ((uint32)1U << cmd)) != 0U); }
 if (cmd < 64U){ return ((sd_break_cmd_hi & ((uint32)1U << (cmd - 32U))) != 0U); }
 return FALSE;
}

static boole cu_spisd_debug_data_cmd(auint cmd)
{
 return (cmd == 17U) || (cmd == 18U) || (cmd == 24U) || (cmd == 25U);
}

static boole cu_spisd_debug_path_contains_ci(char const* hay, char const* needle)
{
 auint i;
 if ((needle == NULL) || (needle[0] == 0)){ return FALSE; }
 if (hay == NULL){ return FALSE; }
 while (*hay != 0){
  for (i = 0U; needle[i] != 0; ++i){
   auint a = (auint)(uint8)hay[i], b = (auint)(uint8)needle[i];
   if (a == 0U){ return FALSE; }
   if ((a >= 'a') && (a <= 'z')){ a -= ('a' - 'A'); }
   if ((b >= 'a') && (b <= 'z')){ b -= ('a' - 'A'); }
   if (a != b){ break; }
  }
  if (needle[i] == 0){ return TRUE; }
  hay++;
 }
 return FALSE;
}

static boole cu_spisd_debug_fs_match(auint sector, boole write)
{
 cu_vfat_debug_sector_t fs;
 boole role_hit = FALSE, path_hit = FALSE;
 auint want = write ? CU_SPISD_FS_ACCESS_WRITE : CU_SPISD_FS_ACCESS_READ;
 if ((sd_break_fs_access & want) == 0U){ return FALSE; }
 if ((sd_break_fs_role_mask == 0U) && (sd_break_fs_path[0] == 0)){ return FALSE; }
 if (!cu_vfat_debug_sector_snapshot(sector, FALSE, &fs)){ return FALSE; }
 if ((fs.role < 32U) && ((sd_break_fs_role_mask & ((uint32)1U << fs.role)) != 0U)){ role_hit = TRUE; }
 if ((sd_break_fs_path[0] != 0) && cu_spisd_debug_path_contains_ci(fs.source, sd_break_fs_path)){ path_hit = TRUE; }
 return role_hit || path_hit;
}

static void cu_spisd_debug_stamp(cu_spisd_trace_event_t* e, auint cycle)
{
 e->cycle = cycle;
 e->pc = (uint16)(cu_avr_get_state()->pc & 0x7FFFU);
 e->row = (uint16)(cu_avr_get_video_beam_pulse() & 0xFFFFU);
 e->beam_cycle = (uint16)(cu_avr_get_video_beam_cycle() & 0xFFFFU);
}

static void cu_spisd_debug_apply_command_context(cu_spisd_trace_event_t* e)
{
 e->transaction_id = sd_dbg_cmd_transaction_id;
 e->start_cycle = sd_dbg_cmd_start;
 e->response_start_cycle = sd_dbg_cmd_response_start;
 e->start_pc = (uint16)(sd_dbg_cmd_start_pc & 0x7FFFU);
 e->response_start_pc = (uint16)(sd_dbg_cmd_response_pc & 0x7FFFU);
 e->start_row = (uint16)(sd_dbg_cmd_start_row & 0xFFFFU);
 e->start_beam_cycle = (uint16)(sd_dbg_cmd_start_beam_cycle & 0xFFFFU);
 e->response_start_row = (uint16)(sd_dbg_cmd_response_row & 0xFFFFU);
 e->response_start_beam_cycle = (uint16)(sd_dbg_cmd_response_beam_cycle & 0xFFFFU);
 { auint i; for (i = 0U; i < 6U; ++i){
  e->command_byte_cycle[i] = sd_dbg_cmd_byte_cycle[i];
  e->command_byte_pc[i] = (uint16)(sd_dbg_cmd_byte_pc[i] & 0x7FFFU);
  e->command_byte_row[i] = (uint16)(sd_dbg_cmd_byte_row[i] & 0xFFFFU);
  e->command_byte_beam_cycle[i] = (uint16)(sd_dbg_cmd_byte_beam_cycle[i] & 0xFFFFU);
 } }
 e->command_crc = (uint8)(sd_dbg_cmd_crc & 0xFFU);
 e->command_crc_expected = (uint8)(sd_dbg_cmd_crc_expected & 0xFFU);
}

static void cu_spisd_debug_apply_data_context(cu_spisd_trace_event_t* e)
{
 e->transaction_id = sd_dbg_data_transaction_id;
 e->start_cycle = sd_dbg_data_start_cycle;
 e->start_pc = (uint16)(sd_dbg_data_start_pc & 0x7FFFU);
 e->start_row = (uint16)(sd_dbg_data_start_row & 0xFFFFU);
 e->start_beam_cycle = (uint16)(sd_dbg_data_start_beam_cycle & 0xFFFFU);
 e->cmd = (uint8)(sd_dbg_data_cmd & 0x3FU);
 e->app = (uint8)(sd_dbg_data_app ? 1U : 0U);
 e->arg = sd_dbg_data_arg;
}

static void cu_spisd_debug_emit(cu_spisd_trace_event_t* e, boole request_break)
{
 if (request_break){
  e->flags |= CU_SPISD_TRACE_FLAG_BREAK;
  sd_hit = *e;
  sd_hit_valid = TRUE;
  sd_break_notice = *e;
  sd_break_notice_valid = TRUE;
  cu_avr_debug_request_break();
 }
 if (sd_trace_on){
  uint32 seq = sd_trace_seq++;
  e->seq = seq;
  sd_trace_buf[seq % CU_SPISD_TRACE_CAP] = *e;
  if (sd_trace_count < CU_SPISD_TRACE_CAP){ sd_trace_count++; }
  if (request_break){ sd_hit.seq = seq; sd_break_notice.seq = seq; }
 }else{
  e->seq = 0xFFFFFFFFU;
  if (request_break){ sd_hit.seq = 0xFFFFFFFFU; sd_break_notice.seq = 0xFFFFFFFFU; }
 }
}

static void cu_spisd_debug_fault(auint kind, auint cycle, auint flags, boole request_break)
{
 cu_spisd_trace_event_t e = {0};
 e.seq = 0xFFFFFFFFU;
 cu_spisd_debug_stamp(&e, cycle);
 if (sd_dbg_cmd_active){
  cu_spisd_debug_apply_command_context(&e);
  e.cmd = (uint8)sd_dbg_cmd;
  e.app = (uint8)(sd_dbg_cmd_app ? 1U : 0U);
  e.arg = sd_dbg_cmd_arg;
 }else if (sd_dbg_data_transaction_id != 0U){
  cu_spisd_debug_apply_data_context(&e);
 }else{
  e.start_cycle = cycle;
  e.start_pc = e.pc;
  e.start_row = e.row;
  e.start_beam_cycle = e.beam_cycle;
  e.cmd = (uint8)(sd_state.cmd & 0x3FU);
  e.app = (uint8)(((sd_state.cmd & SCMD_A) != 0U) ? 1U : 0U);
  e.arg = sd_state.crarg;
 }
 e.kind = (uint8)kind;
 e.sector = sd_state.paddr;
 e.r1 = (uint8)(sd_state.r1 & 0xFFU);
 e.state_before = (uint8)sd_dbg_prev_state;
 e.state_after = (uint8)sd_state.state;
 e.packet_state = (uint8)sd_state.pstat;
 e.flags = (uint8)flags;
 cu_spisd_debug_emit(&e, request_break);
}

static void cu_spisd_debug_after_send(auint sent_data, auint cycle)
{
 auint cur_cmd = sd_state.cmd;
 auint cur_state = sd_state.state;
 auint cur_pstat = sd_state.pstat;
 auint cur_paddr = sd_state.paddr;
 auint prev_pstat = sd_dbg_prev_pstat;
 auint prev_state = sd_dbg_prev_state;
 boole sector_emitted = FALSE;

 /* Native-mode startup pulse/cadence failure. */
 if ((sd_dbg_prev_state == STAT_NINIT) && (cur_state == STAT_UNINIT)){
  cu_spisd_debug_fault(CU_SPISD_TRACE_KIND_INIT_FAIL, cycle, CU_SPISD_TRACE_FLAG_INIT_FAIL, sd_break_init_fail);
 }

 /* Detect the first command byte. */
 if (((sd_dbg_prev_cmd & SCMD_R) == 0U) && ((cur_cmd & SCMD_R) != 0U) && ((cur_cmd & SCMD_N) == 0U)){
  sd_dbg_cmd_active = TRUE;
  sd_dbg_cmd = cur_cmd & 0x3FU;
  sd_dbg_cmd_app = ((cur_cmd & SCMD_A) != 0U) ? TRUE : FALSE;
  sd_dbg_cmd_arg = 0U;
  sd_dbg_cmd_start = cycle;
  sd_dbg_cmd_response_start = 0U;
  sd_dbg_cmd_start_state = cur_state;
  sd_dbg_latency_reported = FALSE;
  sd_dbg_cmd_transaction_id = sd_dbg_next_transaction_id++;
  if (sd_dbg_next_transaction_id == 0U){ sd_dbg_next_transaction_id = 1U; }
  sd_dbg_cmd_start_pc = cu_avr_get_state()->pc & 0x7FFFU;
  sd_dbg_cmd_start_row = cu_avr_get_video_beam_pulse();
  sd_dbg_cmd_start_beam_cycle = cu_avr_get_video_beam_cycle();
  sd_dbg_cmd_response_pc = 0U;
  sd_dbg_cmd_response_row = 0U;
  sd_dbg_cmd_response_beam_cycle = 0U;
  sd_dbg_cmd_crc = 0U;
  sd_dbg_cmd_crc_expected = 0U;
  { auint i; for (i = 0U; i < 6U; ++i){ sd_dbg_cmd_byte_cycle[i]=0U; sd_dbg_cmd_byte_pc[i]=0U; sd_dbg_cmd_byte_row[i]=0U; sd_dbg_cmd_byte_beam_cycle[i]=0U; } }
  sd_dbg_cmd_byte_cycle[0] = cycle;
  sd_dbg_cmd_byte_pc[0] = sd_dbg_cmd_start_pc;
  sd_dbg_cmd_byte_row[0] = sd_dbg_cmd_start_row;
  sd_dbg_cmd_byte_beam_cycle[0] = sd_dbg_cmd_start_beam_cycle;
 }

 /* Capture exact argument-byte boundaries while the command parser advances. */
 if (sd_dbg_cmd_active && ((cur_cmd & SCMD_N) == 0U) && ((cur_cmd & SCMD_R) != 0U) &&
     (sd_state.evcnt >= 1U) && (sd_state.evcnt <= 4U)){
  auint bi = sd_state.evcnt;
  if (sd_dbg_cmd_byte_cycle[bi] == 0U){
   sd_dbg_cmd_byte_cycle[bi] = cycle;
   sd_dbg_cmd_byte_pc[bi] = cu_avr_get_state()->pc & 0x7FFFU;
   sd_dbg_cmd_byte_row[bi] = cu_avr_get_video_beam_pulse();
   sd_dbg_cmd_byte_beam_cycle[bi] = cu_avr_get_video_beam_cycle();
  }
 }

 /* CRC byte accepted: argument is complete and response latency begins. */
 if (sd_dbg_cmd_active && ((cur_cmd & SCMD_N) != 0U) && ((sd_dbg_prev_cmd & SCMD_N) == 0U)){
  sd_dbg_cmd_arg = sd_state.crarg;
  sd_dbg_cmd_crc = sent_data & 0xFFU;
  sd_dbg_cmd_crc_expected = ((sd_state.cc7v << 1U) | 1U) & 0xFFU;
  sd_dbg_cmd_byte_cycle[5] = cycle;
  sd_dbg_cmd_byte_pc[5] = cu_avr_get_state()->pc & 0x7FFFU;
  sd_dbg_cmd_byte_row[5] = cu_avr_get_video_beam_pulse();
  sd_dbg_cmd_byte_beam_cycle[5] = cu_avr_get_video_beam_cycle();
  sd_dbg_cmd_response_start = cycle;
  sd_dbg_cmd_response_pc = cu_avr_get_state()->pc & 0x7FFFU;
  sd_dbg_cmd_response_row = cu_avr_get_video_beam_pulse();
  sd_dbg_cmd_response_beam_cycle = cu_avr_get_video_beam_cycle();
 }

 /* While a response is still pending, an excessive latency breakpoint can
 ** fire before the eventual R1 byte is returned. */
 if (sd_dbg_cmd_active && sd_break_latency && (!sd_dbg_latency_reported) &&
     (sd_dbg_cmd_response_start != 0U) && ((cur_cmd & SCMD_N) != 0U) &&
     (WRAP32(cycle - sd_dbg_cmd_response_start) > sd_break_latency_cycles)){
  cu_spisd_trace_event_t e = {0};
  e.seq = 0xFFFFFFFFU;
  cu_spisd_debug_stamp(&e, cycle);
  cu_spisd_debug_apply_command_context(&e);
  e.latency_cycles = WRAP32(cycle - sd_dbg_cmd_response_start);
  e.arg = sd_dbg_cmd_arg;
  e.sector = cu_spisd_debug_data_cmd(sd_dbg_cmd) ? (sd_dbg_cmd_arg >> 9) : sd_state.paddr;
  e.kind = CU_SPISD_TRACE_KIND_LATENCY;
  e.cmd = (uint8)sd_dbg_cmd;
  e.app = (uint8)(sd_dbg_cmd_app ? 1U : 0U);
  e.r1 = (uint8)(sd_state.r1 & 0xFFU);
  e.state_before = (uint8)sd_dbg_cmd_start_state;
  e.state_after = (uint8)cur_state;
  e.packet_state = (uint8)cur_pstat;
  e.flags = CU_SPISD_TRACE_FLAG_LATENCY;
  sd_dbg_latency_reported = TRUE;
  cu_spisd_debug_emit(&e, TRUE);
 }

 /* R1 response is being delivered. This is the natural transaction-history
 ** boundary: command, argument, result, state transition, sector and latency
 ** are all known at this point. */
 if (sd_dbg_cmd_active && ((sd_dbg_prev_cmd & SCMD_N) != 0U) && ((cur_cmd & SCMD_N) == 0U)){
  cu_spisd_trace_event_t e = {0};
  boole init_fail;
  boole crc_fail;
  boole latency_fail;
  boole sector_hit;
  boole fs_hit;
  boole do_break;
  e.seq = 0xFFFFFFFFU;
  cu_spisd_debug_stamp(&e, cycle);
  cu_spisd_debug_apply_command_context(&e);
  e.latency_cycles = (sd_dbg_cmd_response_start != 0U) ? WRAP32(cycle - sd_dbg_cmd_response_start) : 0U;
  e.arg = sd_dbg_cmd_arg;
  e.sector = cu_spisd_debug_data_cmd(sd_dbg_cmd) ? (sd_dbg_cmd_arg >> 9) : sd_state.paddr;
  e.kind = CU_SPISD_TRACE_KIND_COMMAND;
  e.cmd = (uint8)sd_dbg_cmd;
  e.app = (uint8)(sd_dbg_cmd_app ? 1U : 0U);
  e.r1 = (uint8)(sd_state.r1 & 0xFFU);
  e.state_before = (uint8)sd_dbg_cmd_start_state;
  e.state_after = (uint8)cur_state;
  e.packet_state = (uint8)cur_pstat;
  init_fail = (sd_dbg_cmd_start_state <= STAT_IINIT) && ((sd_state.r1 & (R1_ERES | R1_ILL | R1_CRC | R1_ESEQ | R1_ADDR | R1_PAR)) != 0U);
  crc_fail = ((sd_state.r1 & R1_CRC) != 0U);
  latency_fail = sd_break_latency && (e.latency_cycles > sd_break_latency_cycles);
  sector_hit = sd_break_sector_on && cu_spisd_debug_data_cmd(sd_dbg_cmd) && (e.sector == sd_break_sector);
  fs_hit = cu_spisd_debug_data_cmd(sd_dbg_cmd) && (e.r1 == 0U) &&
           cu_spisd_debug_fs_match(e.sector, (sd_dbg_cmd == 24U) || (sd_dbg_cmd == 25U));
  if (init_fail){ e.flags |= CU_SPISD_TRACE_FLAG_INIT_FAIL; }
  if (crc_fail){ e.flags |= CU_SPISD_TRACE_FLAG_CRC; }
  if (latency_fail){ e.flags |= CU_SPISD_TRACE_FLAG_LATENCY; }
  do_break = cu_spisd_debug_cmd_break(sd_dbg_cmd) || sector_hit || fs_hit ||
             (sd_break_init_fail && init_fail) || (sd_break_crc && crc_fail) || latency_fail;
  /* If latency already fired while waiting, keep the command in history but
  ** don't request a second stop for the same transaction. */
  if (sd_dbg_latency_reported && latency_fail){ do_break = cu_spisd_debug_cmd_break(sd_dbg_cmd) || sector_hit || fs_hit ||
                                                (sd_break_init_fail && init_fail) || (sd_break_crc && crc_fail); }
  cu_spisd_debug_emit(&e, do_break);
  sector_emitted = sector_hit;
  if (cu_spisd_debug_data_cmd(sd_dbg_cmd) && (e.r1 == 0U)){
   sd_dbg_data_transaction_id = sd_dbg_cmd_transaction_id;
   sd_dbg_data_cmd = sd_dbg_cmd;
   sd_dbg_data_app = sd_dbg_cmd_app;
   sd_dbg_data_arg = sd_dbg_cmd_arg;
   sd_dbg_data_start_cycle = cycle;
   sd_dbg_data_start_pc = e.pc;
   sd_dbg_data_start_row = e.row;
   sd_dbg_data_start_beam_cycle = e.beam_cycle;
  }
  sd_dbg_cmd_active = FALSE;
 }

 /* Multi-block reads advance sectors without a new command. Expose those
 ** boundaries too, and make sector-N breakpoints useful inside CMD18. */
 if ((cur_paddr != sd_dbg_prev_paddr) && (cur_state == sd_dbg_prev_state) &&
     ((cur_state == STAT_CMD18) || (cur_state == STAT_CMD25))){
  cu_spisd_trace_event_t e = {0};
  boole sector_hit = sd_break_sector_on && (cur_paddr == sd_break_sector);
  boole fs_hit = cu_spisd_debug_fs_match(cur_paddr, cur_state == STAT_CMD25);
  e.seq = 0xFFFFFFFFU;
  cu_spisd_debug_stamp(&e, cycle);
  cu_spisd_debug_apply_data_context(&e);
  e.sector = cur_paddr;
  e.kind = CU_SPISD_TRACE_KIND_BLOCK;
  e.cmd = (uint8)((cur_state == STAT_CMD18) ? 18U : 25U);
  e.state_before = (uint8)sd_dbg_prev_state;
  e.state_after = (uint8)cur_state;
  e.packet_state = (uint8)cur_pstat;
  if (!sector_emitted){ cu_spisd_debug_emit(&e, sector_hit || fs_hit); }
 }

 /* History-only block phase boundaries. These are deliberately not used for
 ** trigger evaluation: when only an SD breakpoint is armed, no extra history
 ** event is constructed. */
 if (sd_trace_on && (sd_dbg_data_transaction_id != 0U)){
  cu_spisd_trace_event_t e = {0};
  boole emit = FALSE;
  e.seq = 0xFFFFFFFFU;
  cu_spisd_debug_stamp(&e, cycle);
  cu_spisd_debug_apply_data_context(&e);
  e.sector = ((prev_pstat == PSTAT_RCRC) && (cur_paddr != sd_dbg_prev_paddr)) ? sd_dbg_prev_paddr : cur_paddr;
  e.r1 = (uint8)(sd_state.r1 & 0xFFU);
  e.state_before = (uint8)prev_state;
  e.state_after = (uint8)cur_state;
  e.packet_state = (uint8)cur_pstat;
  e.crc_calculated = (uint16)(sd_state.cc16v & 0xFFFFU);
  e.crc_received = (uint16)(sd_state.cc16c & 0xFFFFU);
  e.value = (uint8)(sd_state.data & 0xFFU);
  if (((prev_pstat == PSTAT_RPREP) && (cur_pstat == PSTAT_RDATA)) ||
      ((prev_pstat == PSTAT_W24PREP) && (cur_pstat == PSTAT_W24DATA)) ||
      ((prev_pstat == PSTAT_W25PREP) && (cur_pstat == PSTAT_W25DATA))){
   e.kind = CU_SPISD_TRACE_KIND_DATA_TOKEN;
   if ((prev_pstat == PSTAT_W24PREP) || (prev_pstat == PSTAT_W25PREP)){ e.value = (uint8)(sent_data & 0xFFU); }
   emit = TRUE;
  }else if (((prev_pstat == PSTAT_RDATA) && (cur_pstat == PSTAT_RCRC)) ||
            ((prev_pstat == PSTAT_W24DATA) && (cur_pstat == PSTAT_W24CRC)) ||
            ((prev_pstat == PSTAT_W25DATA) && (cur_pstat == PSTAT_W25CRC))){
   e.kind = CU_SPISD_TRACE_KIND_DATA_END; emit = TRUE;
  }else if (((prev_pstat == PSTAT_RCRC) && ((cur_pstat == PSTAT_IDLE) || (cur_pstat == PSTAT_RPREP))) ||
            ((prev_pstat == PSTAT_W24CRC) && (cur_pstat == PSTAT_WBUSY)) ||
            ((prev_pstat == PSTAT_W25CRC) && (cur_pstat == PSTAT_W25PREP))){
   e.kind = CU_SPISD_TRACE_KIND_CRC_END; emit = TRUE;
  }else if ((prev_pstat == PSTAT_WBUSY) && (cur_pstat == PSTAT_IDLE)){
   e.kind = CU_SPISD_TRACE_KIND_BUSY_END; emit = TRUE;
  }
  if (emit){ cu_spisd_debug_emit(&e, FALSE); }
 }

 /* A rejected write CRC is reported in the data-response token rather than
 ** R1, so detect it from the packet-state transition. */
 if ((((sd_dbg_prev_pstat == PSTAT_W24CRC) && (cur_pstat == PSTAT_WBUSY)) ||
      ((sd_dbg_prev_pstat == PSTAT_W25CRC) && (cur_pstat == PSTAT_W25PREP))) &&
     ((sd_state.data & 0x1FU) == 0x0BU)){
  cu_spisd_debug_fault(CU_SPISD_TRACE_KIND_DATA_CRC, cycle, CU_SPISD_TRACE_FLAG_CRC, sd_break_crc);
 }

 if ((sd_dbg_data_transaction_id != 0U) &&
     (((sd_dbg_data_cmd == 17U) && (cur_state != STAT_CMD17)) ||
      ((sd_dbg_data_cmd == 18U) && (cur_state != STAT_CMD18)) ||
      ((sd_dbg_data_cmd == 24U) && (cur_state != STAT_CMD24)) ||
      ((sd_dbg_data_cmd == 25U) && (cur_state != STAT_CMD25)))){
  sd_dbg_data_transaction_id = 0U;
 }

 sd_dbg_prev_cmd = cur_cmd;
 sd_dbg_prev_state = cur_state;
 sd_dbg_prev_pstat = cur_pstat;
 sd_dbg_prev_paddr = cur_paddr;
}

static void cu_spisd_debug_command_abort(auint cycle)
{
 if (sd_dbg_cmd_active){
  cu_spisd_trace_event_t e = {0};
  e.seq = 0xFFFFFFFFU;
  cu_spisd_debug_stamp(&e, cycle);
  cu_spisd_debug_apply_command_context(&e);
  e.arg = sd_dbg_cmd_arg;
  e.kind = CU_SPISD_TRACE_KIND_ABORT;
  e.cmd = (uint8)sd_dbg_cmd;
  e.app = (uint8)(sd_dbg_cmd_app ? 1U : 0U);
  e.state_before = (uint8)sd_dbg_cmd_start_state;
  e.state_after = (uint8)sd_state.state;
  e.packet_state = (uint8)sd_state.pstat;
  cu_spisd_debug_emit(&e, FALSE);
  sd_dbg_cmd_active = FALSE;
 }
 sd_dbg_prev_cmd = sd_state.cmd;
}
#endif


/*
** Running CRC7 calculation for a byte.
*/
static auint cu_spisd_crc7_byte(auint crcval, auint byte)
{
 return sd_crc7_table[(byte ^ (crcval << 1)) & 0xFFU];
}



/*
** Running CRC16 calculation for a byte.
*/
static auint cu_spisd_crc16_byte(auint crcval, auint byte)
{
 return (sd_crc16_table[(byte ^ (crcval >> 8)) & 0xFFU] ^ (crcval << 8)) & 0xFFFFU;
}



void cu_spisd_trace_enable(boole enable)
{
#ifdef ENABLE_SD_TRACE
 sd_trace_on = enable;
 cu_spisd_debug_refresh();
#else
 (void)enable;
#endif
}

boole cu_spisd_trace_built(void)
{
#ifdef ENABLE_SD_TRACE
 return TRUE;
#else
 return FALSE;
#endif
}

boole cu_spisd_trace_enabled(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_trace_on;
#else
 return FALSE;
#endif
}

boole cu_spisd_trace_active(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_debug_active;
#else
 return FALSE;
#endif
}

void cu_spisd_trace_clear(void)
{
#ifdef ENABLE_SD_TRACE
 sd_trace_seq = 0U;
 sd_trace_count = 0U;
#endif
}

auint cu_spisd_trace_count(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_trace_count;
#else
 return 0U;
#endif
}

uint32 cu_spisd_trace_first_seq(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_trace_seq - sd_trace_count;
#else
 return 0U;
#endif
}

boole cu_spisd_trace_get(uint32 seq, cu_spisd_trace_event_t* out)
{
#ifdef ENABLE_SD_TRACE
 uint32 first = sd_trace_seq - sd_trace_count;
 if ((seq < first) || (seq >= sd_trace_seq)){ return FALSE; }
 if (out != NULL){ *out = sd_trace_buf[seq % CU_SPISD_TRACE_CAP]; }
 return TRUE;
#else
 (void)seq; (void)out;
 return FALSE;
#endif
}

void cu_spisd_trace_break_command_set(auint cmd, boole enable)
{
#ifdef ENABLE_SD_TRACE
 uint32 bit;
 if (cmd >= 64U){ return; }
 if (cmd < 32U){
  bit = (uint32)1U << cmd;
  if (enable){ sd_break_cmd_lo |= bit; }else{ sd_break_cmd_lo &= ~bit; }
 }else{
  bit = (uint32)1U << (cmd - 32U);
  if (enable){ sd_break_cmd_hi |= bit; }else{ sd_break_cmd_hi &= ~bit; }
 }
 cu_spisd_debug_refresh();
#else
 (void)cmd; (void)enable;
#endif
}

void cu_spisd_trace_break_commands_clear(void)
{
#ifdef ENABLE_SD_TRACE
 sd_break_cmd_lo = 0U;
 sd_break_cmd_hi = 0U;
 cu_spisd_debug_refresh();
#endif
}

uint32 cu_spisd_trace_break_command_mask_lo(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_cmd_lo;
#else
 return 0U;
#endif
}

uint32 cu_spisd_trace_break_command_mask_hi(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_cmd_hi;
#else
 return 0U;
#endif
}

void cu_spisd_trace_break_sector_set(boole enable, auint sector)
{
#ifdef ENABLE_SD_TRACE
 sd_break_sector_on = enable;
 sd_break_sector = sector & 0x007FFFFFU;
 cu_spisd_debug_refresh();
#else
 (void)enable; (void)sector;
#endif
}

boole cu_spisd_trace_break_sector_get(auint* out_sector)
{
#ifdef ENABLE_SD_TRACE
 if (out_sector != NULL){ *out_sector = sd_break_sector; }
 return sd_break_sector_on;
#else
 if (out_sector != NULL){ *out_sector = 0U; }
 return FALSE;
#endif
}

void cu_spisd_trace_break_init_fail_set(boole enable)
{
#ifdef ENABLE_SD_TRACE
 sd_break_init_fail = enable;
 cu_spisd_debug_refresh();
#else
 (void)enable;
#endif
}

boole cu_spisd_trace_break_init_fail_get(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_init_fail;
#else
 return FALSE;
#endif
}

void cu_spisd_trace_break_crc_set(boole enable)
{
#ifdef ENABLE_SD_TRACE
 sd_break_crc = enable;
 cu_spisd_debug_refresh();
#else
 (void)enable;
#endif
}

boole cu_spisd_trace_break_crc_get(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_crc;
#else
 return FALSE;
#endif
}

void cu_spisd_trace_break_latency_set(boole enable, auint cycles)
{
#ifdef ENABLE_SD_TRACE
 sd_break_latency = enable;
 sd_break_latency_cycles = cycles;
 cu_spisd_debug_refresh();
#else
 (void)enable; (void)cycles;
#endif
}

boole cu_spisd_trace_break_latency_get(auint* out_cycles)
{
#ifdef ENABLE_SD_TRACE
 if (out_cycles != NULL){ *out_cycles = sd_break_latency_cycles; }
 return sd_break_latency;
#else
 if (out_cycles != NULL){ *out_cycles = 0U; }
 return FALSE;
#endif
}

boole cu_spisd_trace_hit_get(boole clear, cu_spisd_trace_event_t* out)
{
#ifdef ENABLE_SD_TRACE
 boole ret = sd_hit_valid;
 if (out != NULL){ *out = sd_hit; }
 if (clear){ sd_hit_valid = FALSE; }
 return ret;
#else
 (void)clear; (void)out;
 return FALSE;
#endif
}


boole cu_spisd_trace_break_notice_get(boole clear, cu_spisd_trace_event_t* out)
{
#ifdef ENABLE_SD_TRACE
 boole ret = sd_break_notice_valid;
 if (out != NULL){ *out = sd_break_notice; }
 if (clear){ sd_break_notice_valid = FALSE; }
 return ret;
#else
 (void)clear; (void)out;
 return FALSE;
#endif
}

void cu_spisd_trace_break_fs_role_set(auint role, boole enable)
{
#ifdef ENABLE_SD_TRACE
 uint32 bit;
 if ((role == 0U) || (role >= 32U)){ return; }
 bit = (uint32)1U << role;
 if (enable){ sd_break_fs_role_mask |= bit; }else{ sd_break_fs_role_mask &= ~bit; }
 cu_spisd_debug_refresh();
#else
 (void)role; (void)enable;
#endif
}

uint32 cu_spisd_trace_break_fs_role_mask(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_fs_role_mask;
#else
 return 0U;
#endif
}

void cu_spisd_trace_break_fs_path_set(char const* path)
{
#ifdef ENABLE_SD_TRACE
 auint i = 0U;
 if (path != NULL){
  while ((path[i] != 0) && (i + 1U < CU_VFAT_DEBUG_PATH_SIZE)){ sd_break_fs_path[i] = path[i]; i++; }
 }
 sd_break_fs_path[i] = 0;
 cu_spisd_debug_refresh();
#else
 (void)path;
#endif
}

char const* cu_spisd_trace_break_fs_path_get(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_fs_path;
#else
 return "";
#endif
}

void cu_spisd_trace_break_fs_access_set(auint access)
{
#ifdef ENABLE_SD_TRACE
 sd_break_fs_access = access & (CU_SPISD_FS_ACCESS_READ | CU_SPISD_FS_ACCESS_WRITE);
 cu_spisd_debug_refresh();
#else
 (void)access;
#endif
}

auint cu_spisd_trace_break_fs_access_get(void)
{
#ifdef ENABLE_SD_TRACE
 return sd_break_fs_access;
#else
 return 0U;
#endif
}

#ifdef ENABLE_SD_FAULT
static cu_spisd_fault_rule_t sd_fault_rules[CU_SPISD_FAULT_CAP];
static boole sd_fault_any = FALSE;

static void cu_spisd_fault_refresh(void)
{
 auint i;
 sd_fault_any = FALSE;
 for (i = 0U; i < CU_SPISD_FAULT_CAP; ++i){ if (sd_fault_rules[i].enabled){ sd_fault_any = TRUE; break; } }
}

static boole cu_spisd_fault_is_data_cmd(auint cmd)
{
 return (cmd == 17U) || (cmd == 18U) || (cmd == 24U) || (cmd == 25U);
}

static boole cu_spisd_fault_match(cu_spisd_fault_rule_t const* r, auint kind, auint cmd, auint sector, boole sector_known)
{
 if ((!r->enabled) || (r->kind != kind)){ return FALSE; }
 if ((!r->command_any) && ((r->command & 0x3FU) != (cmd & 0x3FU))){ return FALSE; }
 if (!r->sector_any){ if ((!sector_known) || ((r->sector & 0x007FFFFFU) != (sector & 0x007FFFFFU))){ return FALSE; } }
 return TRUE;
}

static auint cu_spisd_fault_sum(auint kind, auint cmd, auint sector, boole sector_known)
{
 auint i, total = 0U;
 for (i = 0U; i < CU_SPISD_FAULT_CAP; ++i){
  if (cu_spisd_fault_match(&sd_fault_rules[i], kind, cmd, sector, sector_known)){
   auint v = sd_fault_rules[i].value;
   if ((0xFFFFFFFFU - total) < v){ total = 0xFFFFFFFFU; }else{ total += v; }
  }
 }
 return total;
}

static auint cu_spisd_fault_bits(auint kind, auint cmd, auint sector, boole sector_known)
{
 auint i, bits = 0U;
 for (i = 0U; i < CU_SPISD_FAULT_CAP; ++i){
  if (cu_spisd_fault_match(&sd_fault_rules[i], kind, cmd, sector, sector_known)){ bits |= sd_fault_rules[i].value; }
 }
 return bits;
}

static void cu_spisd_fault_fire(auint kind, auint cmd, auint sector, boole sector_known, auint cycle)
{
 auint i;
 boole changed = FALSE;
 for (i = 0U; i < CU_SPISD_FAULT_CAP; ++i){
  cu_spisd_fault_rule_t* r = &sd_fault_rules[i];
  if (cu_spisd_fault_match(r, kind, cmd, sector, sector_known)){
   r->fired++;
   r->last_cycle = cycle;
   if (!r->persistent){ r->enabled = FALSE; changed = TRUE; }
  }
 }
 if (changed){ cu_spisd_fault_refresh(); }
#ifdef ENABLE_SD_TRACE
 if (sd_trace_on){ cu_spisd_debug_fault(CU_SPISD_TRACE_KIND_FAULT, cycle, CU_SPISD_TRACE_FLAG_FAULT, FALSE); }
#endif
}

static auint cu_spisd_fault_response_wait(auint cmd, auint arg)
{
 boole known = cu_spisd_fault_is_data_cmd(cmd);
 auint sector = known ? (arg >> 9) : 0U;
 return cu_spisd_fault_sum(CU_SPISD_FAULT_R1_DELAY, cmd, sector, known);
}

static auint cu_spisd_fault_token_wait(auint cmd, auint sector)
{
 return cu_spisd_fault_sum(CU_SPISD_FAULT_TOKEN_DELAY, cmd, sector, TRUE);
}

static auint cu_spisd_fault_current_data_cmd(void)
{
 switch (sd_state.state){
  case STAT_CMD17: return 17U;
  case STAT_CMD18: return 18U;
  case STAT_CMD24: return 24U;
  case STAT_CMD25: return 25U;
  default: return 0U;
 }
}

static auint cu_spisd_fault_busy_extra(auint cmd, auint sector, auint cycle)
{
 auint v = cu_spisd_fault_sum(CU_SPISD_FAULT_BUSY_EXTEND, cmd, sector, TRUE);
 if (v != 0U){ cu_spisd_fault_fire(CU_SPISD_FAULT_BUSY_EXTEND, cmd, sector, TRUE, cycle); }
 return v;
}

static void cu_spisd_fault_command_end(auint cmd, auint cycle)
{
 auint c = cmd & 0x3FU;
 boole known = cu_spisd_fault_is_data_cmd(c);
 auint sector = known ? (sd_state.crarg >> 9) : 0U;
 auint bits;
 if (!sd_fault_any){ return; }
 bits = cu_spisd_fault_bits(CU_SPISD_FAULT_R1_BITS, c, sector, known) & 0x7FU;
 if (bits != 0U){ sd_state.r1 |= bits; cu_spisd_fault_fire(CU_SPISD_FAULT_R1_BITS, c, sector, known, cycle); }
 if (cu_spisd_fault_sum(CU_SPISD_FAULT_CMD_CRC, c, sector, known) != 0U){
  sd_state.r1 |= R1_CRC; cu_spisd_fault_fire(CU_SPISD_FAULT_CMD_CRC, c, sector, known, cycle);
 }
 if (cu_spisd_fault_sum(CU_SPISD_FAULT_REJECT, c, sector, known) != 0U){
  auint rb = cu_spisd_fault_bits(CU_SPISD_FAULT_REJECT, c, sector, known) & 0x7FU;
  sd_state.r1 |= (rb != 0U) ? rb : R1_ERES;
  cu_spisd_fault_fire(CU_SPISD_FAULT_REJECT, c, sector, known, cycle);
 }
 if (cu_spisd_fault_sum(CU_SPISD_FAULT_R1_DELAY, c, sector, known) != 0U){
  cu_spisd_fault_fire(CU_SPISD_FAULT_R1_DELAY, c, sector, known, cycle);
 }
}
#endif

boole cu_spisd_fault_built(void)
{
#ifdef ENABLE_SD_FAULT
 return TRUE;
#else
 return FALSE;
#endif
}

boole cu_spisd_fault_active(void)
{
#ifdef ENABLE_SD_FAULT
 return sd_fault_any;
#else
 return FALSE;
#endif
}

void cu_spisd_fault_clear(auint slot)
{
#ifdef ENABLE_SD_FAULT
 auint i;
 if (slot < CU_SPISD_FAULT_CAP){
  sd_fault_rules[slot].enabled = FALSE;
  sd_fault_rules[slot].kind = CU_SPISD_FAULT_NONE;
 }else{
  for (i = 0U; i < CU_SPISD_FAULT_CAP; ++i){ sd_fault_rules[i].enabled = FALSE; sd_fault_rules[i].kind = CU_SPISD_FAULT_NONE; }
 }
 cu_spisd_fault_refresh();
#else
 (void)slot;
#endif
}

boole cu_spisd_fault_set(auint slot, cu_spisd_fault_rule_t const* rule)
{
#ifdef ENABLE_SD_FAULT
 if ((slot >= CU_SPISD_FAULT_CAP) || (rule == NULL) || (rule->kind == CU_SPISD_FAULT_NONE) || (rule->kind > CU_SPISD_FAULT_REJECT)){ return FALSE; }
 sd_fault_rules[slot] = *rule;
 sd_fault_rules[slot].command &= 0x3FU;
 sd_fault_rules[slot].sector &= 0x007FFFFFU;
 sd_fault_rules[slot].fired = 0U;
 sd_fault_rules[slot].last_cycle = 0U;
 cu_spisd_fault_refresh();
 return TRUE;
#else
 (void)slot; (void)rule;
 return FALSE;
#endif
}

boole cu_spisd_fault_get(auint slot, cu_spisd_fault_rule_t* out)
{
#ifdef ENABLE_SD_FAULT
 if (slot >= CU_SPISD_FAULT_CAP){ return FALSE; }
 if (out != NULL){ *out = sd_fault_rules[slot]; }
 return TRUE;
#else
 (void)slot; (void)out;
 return FALSE;
#endif
}

#ifdef ENABLE_SD_REPLAY
/* The replay stream is intentionally linear, not a ring: wrapping would make
** an apparently valid recording impossible to replay from its captured initial
** state. Storage is allocated only when recording/loading is explicitly used. */
#define SD_REPLAY_STATE_WORDS 17U
#define SD_REPLAY_GROW        4096U
#define SD_REPLAY_FILE_VER       1U

#define RST_ENA    0U
#define RST_CRC    1U
#define RST_CC7V   2U
#define RST_CC16V  3U
#define RST_CC16C  4U
#define RST_ENAC   5U
#define RST_STATE  6U
#define RST_NEXT   7U
#define RST_RECVC  8U
#define RST_EVCNT  9U
#define RST_CMD   10U
#define RST_CRARG 11U
#define RST_R1    12U
#define RST_DATA  13U
#define RST_PSTAT 14U
#define RST_PADDR 15U
#define RST_PPOS  16U

typedef struct{ uint32 v[SD_REPLAY_STATE_WORDS]; } cu_spisd_replay_state_i_t;
typedef struct{
 uint32 kind;
 uint32 cycle_a;
 uint32 cycle_b;
 uint32 value_a;
 uint32 value_b;
 cu_spisd_replay_state_i_t post;
}cu_spisd_replay_event_i_t;

static cu_spisd_replay_event_i_t* sd_replay_events = NULL;
static uint32 sd_replay_count = 0U;
static uint32 sd_replay_alloc = 0U;
static uint32 sd_replay_cursor = 0U;
static auint sd_replay_mode = CU_SPISD_REPLAY_MODE_OFF;
static auint sd_replay_error = CU_SPISD_REPLAY_ERR_NONE;
static auint sd_replay_origin = 0U;
static auint sd_replay_run_origin = 0U;
static auint sd_replay_tolerance = 0U;
static boole sd_replay_break_end = TRUE;
static boole sd_replay_overflow = FALSE;
static uint32 sd_replay_mismatch_index = 0U;
static uint32 sd_replay_mismatch_expected = 0U;
static uint32 sd_replay_mismatch_actual = 0U;
static cu_spisd_replay_state_i_t sd_replay_initial;

static void cu_spisd_replay_state_pack(cu_spisd_replay_state_i_t* d, cu_state_spisd_t const* s, auint origin)
{
 d->v[RST_ENA] = s->ena; d->v[RST_CRC] = s->crc; d->v[RST_CC7V] = s->cc7v;
 d->v[RST_CC16V] = s->cc16v; d->v[RST_CC16C] = s->cc16c;
 d->v[RST_ENAC] = WRAP32(s->enac - origin); d->v[RST_STATE] = s->state;
 d->v[RST_NEXT] = WRAP32(s->next - origin); d->v[RST_RECVC] = WRAP32(s->recvc - origin);
 d->v[RST_EVCNT] = s->evcnt; d->v[RST_CMD] = s->cmd; d->v[RST_CRARG] = s->crarg;
 d->v[RST_R1] = s->r1; d->v[RST_DATA] = s->data; d->v[RST_PSTAT] = s->pstat;
 d->v[RST_PADDR] = s->paddr; d->v[RST_PPOS] = s->ppos;
}

static void cu_spisd_replay_state_unpack(cu_state_spisd_t* d, cu_spisd_replay_state_i_t const* s, auint origin)
{
 d->ena = (boole)s->v[RST_ENA]; d->crc = (boole)s->v[RST_CRC]; d->cc7v = s->v[RST_CC7V];
 d->cc16v = s->v[RST_CC16V]; d->cc16c = s->v[RST_CC16C];
 d->enac = WRAP32(origin + s->v[RST_ENAC]); d->state = s->v[RST_STATE];
 d->next = WRAP32(origin + s->v[RST_NEXT]); d->recvc = WRAP32(origin + s->v[RST_RECVC]);
 d->evcnt = s->v[RST_EVCNT]; d->cmd = s->v[RST_CMD]; d->crarg = s->v[RST_CRARG];
 d->r1 = s->v[RST_R1]; d->data = s->v[RST_DATA]; d->pstat = s->v[RST_PSTAT];
 d->paddr = s->v[RST_PADDR]; d->ppos = s->v[RST_PPOS];
}

static boole cu_spisd_replay_reserve(uint32 need)
{
 cu_spisd_replay_event_i_t* n;
 uint32 cap;
 if (need <= sd_replay_alloc){ return TRUE; }
 if (need > CU_SPISD_REPLAY_MAX_EVENTS){ return FALSE; }
 cap = sd_replay_alloc;
 if (cap == 0U){ cap = SD_REPLAY_GROW; }
 while (cap < need){
  if (cap >= CU_SPISD_REPLAY_MAX_EVENTS - SD_REPLAY_GROW){ cap = CU_SPISD_REPLAY_MAX_EVENTS; break; }
  cap += SD_REPLAY_GROW;
 }
 n = (cu_spisd_replay_event_i_t*)realloc(sd_replay_events, (size_t)cap * sizeof(*n));
 if (n == NULL){ return FALSE; }
 sd_replay_events = n; sd_replay_alloc = cap; return TRUE;
}

static void cu_spisd_replay_runtime_fail(auint err, uint32 expected, uint32 actual)
{
 sd_replay_error = err; sd_replay_mismatch_index = sd_replay_cursor;
 sd_replay_mismatch_expected = expected; sd_replay_mismatch_actual = actual;
 sd_replay_mode = CU_SPISD_REPLAY_MODE_ERROR;
 cu_avr_debug_request_break();
}

static boole cu_spisd_replay_append(auint kind, auint cycle_a, auint cycle_b, auint value_a, auint value_b)
{
 cu_spisd_replay_event_i_t* e;
 if (sd_replay_count >= CU_SPISD_REPLAY_MAX_EVENTS){
  sd_replay_overflow = TRUE; cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_OVERFLOW, CU_SPISD_REPLAY_MAX_EVENTS, sd_replay_count); return FALSE;
 }
 if (!cu_spisd_replay_reserve(sd_replay_count + 1U)){
  sd_replay_overflow = TRUE; cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_ALLOC, sd_replay_count + 1U, sd_replay_alloc); return FALSE;
 }
 e = &sd_replay_events[sd_replay_count++];
 e->kind = kind; e->cycle_a = WRAP32(cycle_a - sd_replay_origin); e->cycle_b = WRAP32(cycle_b - sd_replay_origin);
 e->value_a = value_a; e->value_b = value_b; cu_spisd_replay_state_pack(&e->post, &sd_state, sd_replay_origin);
 return TRUE;
}

static uint32 cu_spisd_replay_cycle_distance(uint32 a, uint32 b)
{
 uint32 d = WRAP32(a - b); if (d >= 0x80000000U){ d = WRAP32(0U - d); } return d;
}

static boole cu_spisd_replay_cycle_ok(uint32 actual, uint32 expected)
{
 return (cu_spisd_replay_cycle_distance(actual, expected) <= sd_replay_tolerance);
}

static void cu_spisd_replay_advance(void)
{
 sd_replay_cursor++;
 if (sd_replay_cursor >= sd_replay_count){
  sd_replay_mode = CU_SPISD_REPLAY_MODE_COMPLETE;
  if (sd_replay_break_end){ cu_avr_debug_request_break(); }
 }
}

/* The AVR samples MISO when an SPI transfer begins. Keeping that as a
** separate event from BYTE_END is necessary because CS can change while the
** byte is in flight. Replay therefore validates the exact observable order:
** BYTE_START -> optional CS/reset activity -> BYTE_END. */
static boole cu_spisd_replay_recv_byte(auint cycle, auint* out)
{
 cu_spisd_replay_event_i_t const* e;
 uint32 rel;
 if (sd_replay_mode != CU_SPISD_REPLAY_MODE_REPLAY){ return FALSE; }
 if (sd_replay_cursor >= sd_replay_count){
  cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_END, sd_replay_count, sd_replay_cursor);
  if (out != NULL){ *out = 0xFFU; }
  return TRUE;
 }
 e = &sd_replay_events[sd_replay_cursor];
 if (e->kind != CU_SPISD_REPLAY_EVENT_BYTE_START){
  cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_EVENT_KIND, CU_SPISD_REPLAY_EVENT_BYTE_START, e->kind);
  if (out != NULL){ *out = 0xFFU; }
  return TRUE;
 }
 rel = WRAP32(cycle - sd_replay_run_origin);
 if (!cu_spisd_replay_cycle_ok(rel, e->cycle_a)){
  cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_RECV_CYCLE, e->cycle_a, rel);
  if (out != NULL){ *out = e->value_b & 0xFFU; }
  return TRUE;
 }
 cu_spisd_replay_state_unpack(&sd_state, &e->post, sd_replay_run_origin);
 if (out != NULL){ *out = e->value_b & 0xFFU; }
 cu_spisd_replay_advance();
 return TRUE;
}

static boole cu_spisd_replay_send_byte(auint data, auint cycle)
{
 cu_spisd_replay_event_i_t const* e;
 uint32 rel;
 if (sd_replay_mode != CU_SPISD_REPLAY_MODE_REPLAY){ return FALSE; }
 if (sd_replay_cursor >= sd_replay_count){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_END, sd_replay_count, sd_replay_cursor); return TRUE; }
 e = &sd_replay_events[sd_replay_cursor];
 if (e->kind != CU_SPISD_REPLAY_EVENT_BYTE_END){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_EVENT_KIND, CU_SPISD_REPLAY_EVENT_BYTE_END, e->kind); return TRUE; }
 if ((data & 0xFFU) != (e->value_a & 0xFFU)){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_MOSI, e->value_a & 0xFFU, data & 0xFFU); return TRUE; }
 rel = WRAP32(cycle - sd_replay_run_origin);
 if (!cu_spisd_replay_cycle_ok(rel, e->cycle_b)){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_SEND_CYCLE, e->cycle_b, rel); return TRUE; }
 cu_spisd_replay_state_unpack(&sd_state, &e->post, sd_replay_run_origin);
 cu_spisd_replay_advance(); return TRUE;
}

static boole cu_spisd_replay_cs_event(boole ena, auint cycle)
{
 cu_spisd_replay_event_i_t const* e;
 uint32 rel;
 if (sd_replay_mode != CU_SPISD_REPLAY_MODE_REPLAY){ return FALSE; }
 if (sd_replay_cursor >= sd_replay_count){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_END, sd_replay_count, sd_replay_cursor); return TRUE; }
 e = &sd_replay_events[sd_replay_cursor];
 if (e->kind != CU_SPISD_REPLAY_EVENT_CS){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_EVENT_KIND, CU_SPISD_REPLAY_EVENT_CS, e->kind); return TRUE; }
 if ((auint)ena != (e->value_a ? 1U : 0U)){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_CS_STATE, e->value_a ? 1U : 0U, ena ? 1U : 0U); return TRUE; }
 rel = WRAP32(cycle - sd_replay_run_origin);
 if (!cu_spisd_replay_cycle_ok(rel, e->cycle_a)){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_CS_CYCLE, e->cycle_a, rel); return TRUE; }
 cu_spisd_replay_state_unpack(&sd_state, &e->post, sd_replay_run_origin);
 cu_spisd_replay_advance(); return TRUE;
}

static boole cu_spisd_replay_reset_event(auint cycle)
{
 cu_spisd_replay_event_i_t const* e;
 uint32 rel;
 if (sd_replay_mode != CU_SPISD_REPLAY_MODE_REPLAY){ return FALSE; }
 if (sd_replay_cursor >= sd_replay_count){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_END, sd_replay_count, sd_replay_cursor); return TRUE; }
 e = &sd_replay_events[sd_replay_cursor];
 if (e->kind != CU_SPISD_REPLAY_EVENT_RESET){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_EVENT_KIND, CU_SPISD_REPLAY_EVENT_RESET, e->kind); return TRUE; }
 rel = WRAP32(cycle - sd_replay_run_origin);
 if (!cu_spisd_replay_cycle_ok(rel, e->cycle_a)){ cu_spisd_replay_runtime_fail(CU_SPISD_REPLAY_ERR_RESET_CYCLE, e->cycle_a, rel); return TRUE; }
 cu_spisd_replay_state_unpack(&sd_state, &e->post, sd_replay_run_origin);
 cu_spisd_replay_advance(); return TRUE;
}

static boole cu_spisd_replay_write_u32(FILE* f, uint32 v)
{
 uint8 b[4]; b[0]=(uint8)v; b[1]=(uint8)(v>>8); b[2]=(uint8)(v>>16); b[3]=(uint8)(v>>24); return (fwrite(b,1,4,f)==4U);
}
static boole cu_spisd_replay_read_u32(FILE* f, uint32* v)
{
 uint8 b[4]; if (fread(b,1,4,f)!=4U){ return FALSE; } *v=(uint32)b[0]|((uint32)b[1]<<8)|((uint32)b[2]<<16)|((uint32)b[3]<<24); return TRUE;
}
#endif

boole cu_spisd_replay_built(void)
{
#ifdef ENABLE_SD_REPLAY
 return TRUE;
#else
 return FALSE;
#endif
}

void cu_spisd_replay_clear(void)
{
#ifdef ENABLE_SD_REPLAY
 free(sd_replay_events); sd_replay_events = NULL; sd_replay_count = 0U; sd_replay_alloc = 0U; sd_replay_cursor = 0U;
 sd_replay_mode = CU_SPISD_REPLAY_MODE_OFF; sd_replay_error = CU_SPISD_REPLAY_ERR_NONE; sd_replay_overflow = FALSE;
 sd_replay_mismatch_index = 0U; sd_replay_mismatch_expected = 0U; sd_replay_mismatch_actual = 0U;
#endif
}

boole cu_spisd_replay_record_start(auint cycle)
{
#ifdef ENABLE_SD_REPLAY
 cu_spisd_replay_clear(); sd_replay_origin = cycle; sd_replay_run_origin = 0U; sd_replay_tolerance = 0U;
 cu_spisd_replay_state_pack(&sd_replay_initial, &sd_state, sd_replay_origin); sd_replay_mode = CU_SPISD_REPLAY_MODE_RECORD; return TRUE;
#else
 (void)cycle; return FALSE;
#endif
}

void cu_spisd_replay_stop(void)
{
#ifdef ENABLE_SD_REPLAY
 if ((sd_replay_mode == CU_SPISD_REPLAY_MODE_RECORD) || (sd_replay_mode == CU_SPISD_REPLAY_MODE_REPLAY)){
  sd_replay_mode = CU_SPISD_REPLAY_MODE_OFF;
 }
#endif
}

boole cu_spisd_replay_start(auint cycle, auint tolerance_cycles)
{
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_count == 0U){ return FALSE; }
 sd_replay_cursor = 0U; sd_replay_run_origin = cycle; sd_replay_tolerance = tolerance_cycles;
 sd_replay_error = CU_SPISD_REPLAY_ERR_NONE; sd_replay_mismatch_index = 0U; sd_replay_mismatch_expected = 0U; sd_replay_mismatch_actual = 0U;
 cu_spisd_replay_state_unpack(&sd_state, &sd_replay_initial, sd_replay_run_origin);
 sd_replay_mode = CU_SPISD_REPLAY_MODE_REPLAY; return TRUE;
#else
 (void)cycle; (void)tolerance_cycles; return FALSE;
#endif
}

void cu_spisd_replay_break_on_end_set(boole enable)
{
#ifdef ENABLE_SD_REPLAY
 sd_replay_break_end = enable;
#else
 (void)enable;
#endif
}

void cu_spisd_replay_status(cu_spisd_replay_status_t* out)
{
 if (out == NULL){ return; }
 *out = (cu_spisd_replay_status_t){0};
#ifdef ENABLE_SD_REPLAY
 out->built = TRUE; out->strict_timing = TRUE; out->break_on_end = sd_replay_break_end; out->overflow = sd_replay_overflow;
 out->mode = sd_replay_mode; out->error = sd_replay_error; out->count = sd_replay_count; out->capacity = CU_SPISD_REPLAY_MAX_EVENTS;
 out->cursor = sd_replay_cursor; out->origin_cycle = sd_replay_origin; out->replay_origin_cycle = sd_replay_run_origin;
 out->tolerance_cycles = sd_replay_tolerance; out->mismatch_index = sd_replay_mismatch_index;
 out->mismatch_expected = sd_replay_mismatch_expected; out->mismatch_actual = sd_replay_mismatch_actual;
#endif
}

boole cu_spisd_replay_get(uint32 index, cu_spisd_replay_event_t* out)
{
#ifdef ENABLE_SD_REPLAY
 cu_spisd_replay_event_i_t const* e;
 if ((index >= sd_replay_count) || (out == NULL)){ return FALSE; }
 e = &sd_replay_events[index]; memset(out, 0, sizeof(*out)); out->seq = index; out->kind = (uint8)e->kind;
 if (e->kind == CU_SPISD_REPLAY_EVENT_BYTE_START){ out->recv_cycle_delta=e->cycle_a; out->miso=(uint8)e->value_b; }
 else if (e->kind == CU_SPISD_REPLAY_EVENT_BYTE_END){ out->send_cycle_delta=e->cycle_b; out->mosi=(uint8)e->value_a; }
 else { out->cycle_delta=e->cycle_a; if(e->kind==CU_SPISD_REPLAY_EVENT_CS){out->cs_enabled=(uint8)(e->value_a?1U:0U);} }
 out->state=(uint8)e->post.v[RST_STATE]; out->packet_state=(uint8)e->post.v[RST_PSTAT]; out->command=(uint8)(e->post.v[RST_CMD]&0xFFU);
 out->r1=(uint8)e->post.v[RST_R1]; out->sector=e->post.v[RST_PADDR]; out->packet_pos=e->post.v[RST_PPOS]; return TRUE;
#else
 (void)index; (void)out; return FALSE;
#endif
}

boole cu_spisd_replay_save(char const* path)
{
#ifdef ENABLE_SD_REPLAY
 static uint8 const magic[8]={'C','U','S','D','R','P','0','1'};
 FILE* f; uint32 i,j;
 if ((path==NULL)||(path[0]==0)||(sd_replay_count==0U)){ return FALSE; }
 f=fopen(path,"wb"); if(f==NULL){sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;}
 if ((fwrite(magic,1,8,f)!=8U)||(!cu_spisd_replay_write_u32(f,SD_REPLAY_FILE_VER))||(!cu_spisd_replay_write_u32(f,sd_replay_count))){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;}
 for(j=0U;j<SD_REPLAY_STATE_WORDS;++j){if(!cu_spisd_replay_write_u32(f,sd_replay_initial.v[j])){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;}}
 for(i=0U;i<sd_replay_count;++i){cu_spisd_replay_event_i_t const*e=&sd_replay_events[i];uint32 h[5]={e->kind,e->cycle_a,e->cycle_b,e->value_a,e->value_b};for(j=0U;j<5U;++j){if(!cu_spisd_replay_write_u32(f,h[j])){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;}}for(j=0U;j<SD_REPLAY_STATE_WORDS;++j){if(!cu_spisd_replay_write_u32(f,e->post.v[j])){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;}}}
 if(fclose(f)!=0){sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;} sd_replay_error=CU_SPISD_REPLAY_ERR_NONE; return TRUE;
#else
 (void)path; return FALSE;
#endif
}

boole cu_spisd_replay_load(char const* path)
{
#ifdef ENABLE_SD_REPLAY
 static uint8 const magic[8]={'C','U','S','D','R','P','0','1'};
 uint8 got[8]; FILE*f; uint32 ver,count,i,j; cu_spisd_replay_event_i_t*tmp=NULL; cu_spisd_replay_state_i_t initial;
 if((path==NULL)||(path[0]==0)){return FALSE;} f=fopen(path,"rb");if(f==NULL){sd_replay_error=CU_SPISD_REPLAY_ERR_FILE;return FALSE;}
 if((fread(got,1,8,f)!=8U)||(memcmp(got,magic,8)!=0)||(!cu_spisd_replay_read_u32(f,&ver))||(ver!=SD_REPLAY_FILE_VER)||(!cu_spisd_replay_read_u32(f,&count))||(count>CU_SPISD_REPLAY_MAX_EVENTS)){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FORMAT;return FALSE;}
 for(j=0U;j<SD_REPLAY_STATE_WORDS;++j){if(!cu_spisd_replay_read_u32(f,&initial.v[j])){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FORMAT;return FALSE;}}
 if(count!=0U){tmp=(cu_spisd_replay_event_i_t*)malloc((size_t)count*sizeof(*tmp));if(tmp==NULL){fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_ALLOC;return FALSE;}}
 for(i=0U;i<count;++i){uint32 h[5];for(j=0U;j<5U;++j){if(!cu_spisd_replay_read_u32(f,&h[j])){free(tmp);fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FORMAT;return FALSE;}}tmp[i].kind=h[0];tmp[i].cycle_a=h[1];tmp[i].cycle_b=h[2];tmp[i].value_a=h[3];tmp[i].value_b=h[4];if((tmp[i].kind<CU_SPISD_REPLAY_EVENT_BYTE_START)||(tmp[i].kind>CU_SPISD_REPLAY_EVENT_RESET)){free(tmp);fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FORMAT;return FALSE;}for(j=0U;j<SD_REPLAY_STATE_WORDS;++j){if(!cu_spisd_replay_read_u32(f,&tmp[i].post.v[j])){free(tmp);fclose(f);sd_replay_error=CU_SPISD_REPLAY_ERR_FORMAT;return FALSE;}}}
 fclose(f); free(sd_replay_events); sd_replay_events=tmp; sd_replay_count=count; sd_replay_alloc=count; sd_replay_cursor=0U; sd_replay_initial=initial;
 sd_replay_mode=CU_SPISD_REPLAY_MODE_OFF;sd_replay_error=CU_SPISD_REPLAY_ERR_NONE;sd_replay_overflow=FALSE;sd_replay_mismatch_index=0U;sd_replay_mismatch_expected=0U;sd_replay_mismatch_actual=0U;sd_replay_origin=0U;sd_replay_run_origin=0U;return TRUE;
#else
 (void)path; return FALSE;
#endif
}


/*
** Resets SD card peripheral. Cycle is the CPU cycle when it happens which
** might be used for emulating timing constraints.
*/
void  cu_spisd_reset(auint cycle)
{
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_mode == CU_SPISD_REPLAY_MODE_REPLAY){ (void)cu_spisd_replay_reset_event(cycle); return; }
#endif
 auint byt;
 auint bit;
 auint crc;

 sd_state.ena   = FALSE;
 sd_state.crc   = TRUE;
 sd_state.enac  = cycle;
 sd_state.state = STAT_UNINIT;
 sd_state.next  = cycle;
 sd_state.recvc = cycle;
 sd_state.evcnt = 0U;
 sd_state.cmd   = 0U;
 sd_state.crarg = 0U;
 sd_state.r1    = 0U;
 sd_state.data  = 0xFFU;
 sd_state.pstat = PSTAT_IDLE;

 /* Generate CRC7 table */

 for (byt = 0U; byt < 256U; byt ++){
  crc = byt;
  if ((crc & 0x80U) != 0U){ crc ^= 0x89U; }
  for (bit = 1U; bit < 8U; bit ++){
   crc <<= 1;
   if ((crc & 0x80U) != 0U){ crc ^= 0x89U; }
  }
  sd_crc7_table[byt] = (crc & 0x7FU);
 }

 /* Generate CRC16 table */

 for (byt = 0U; byt < 256U; byt ++){
  crc = byt << 8;
  for (bit = 0U; bit < 8U; bit ++){
   crc <<= 1;
   if ((crc & 0x10000U) != 0U){ crc ^= 0x1021U; }
  }
  sd_crc16_table[byt] = (crc & 0xFFFFU);
 }

 cu_vfat_reset();
#ifdef ENABLE_SD_TRACE
 sd_hit_valid = FALSE;
 sd_break_notice_valid = FALSE;
 if (sd_debug_active){ cu_spisd_debug_sync(); }
#endif
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_mode == CU_SPISD_REPLAY_MODE_RECORD){ (void)cu_spisd_replay_append(CU_SPISD_REPLAY_EVENT_RESET, cycle, cycle, 0U, 0U); }
#endif
}



/*
** Sets chip select's state, TRUE to enable, FALSE to disable.
*/
void  cu_spisd_cs_set(boole ena, auint cycle)
{
 if ( (sd_state.ena && (!ena)) ||
      ((!sd_state.ena) && ena) ){
#ifdef ENABLE_SD_REPLAY
  if (sd_replay_mode == CU_SPISD_REPLAY_MODE_REPLAY){ (void)cu_spisd_replay_cs_event(ena, cycle); return; }
#endif
  sd_state.ena  = ena;
  sd_state.enac = cycle;
  if (sd_state.ena == FALSE){ /* Kill any command if CS goes away */
   sd_state.cmd &= SCMD_A;    /* (Except for the app. command flag which indicates waiting for such a command) */
  }
#ifdef ENABLE_SD_TRACE
  if (sd_debug_active){
   if (!ena){ cu_spisd_debug_command_abort(cycle); }
   sd_dbg_prev_cmd = sd_state.cmd;
  }
#endif
#ifdef ENABLE_SD_REPLAY
  if (sd_replay_mode == CU_SPISD_REPLAY_MODE_RECORD){ (void)cu_spisd_replay_append(CU_SPISD_REPLAY_EVENT_CS, cycle, cycle, ena ? 1U : 0U, 0U); }
#endif
 }
}



/*
** Sends a byte of data to the SD card. The passed cycle corresponds the cycle
** when it was clocked out of the AVR.
*/
void  cu_spisd_send(auint data, auint cycle)
{
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_mode == CU_SPISD_REPLAY_MODE_REPLAY){ (void)cu_spisd_replay_send_byte(data, cycle); return; }
#endif
 boole atend = FALSE;   /* Mark that a command ended, need to form a response */
 boole write = FALSE;   /* In writing (to block command processing) */
 auint cmd   = sd_state.cmd; /* Save for command processing */

 sd_state.data = 0xFFU; /* Default data out */

 /* Data transmission state machine (lowest priority in determining output
 ** data bytes) */

 switch (sd_state.pstat){

  case PSTAT_IDLE:      /* Idle */
   break;

  case PSTAT_RPREP:     /* Read data preparation & data token */
   {
    auint read_wait = sd_model.read_wait_bytes;
#ifdef ENABLE_SD_FAULT
    auint fault_cmd = cu_spisd_fault_current_data_cmd();
    auint fault_extra = sd_fault_any ? cu_spisd_fault_token_wait(fault_cmd, sd_state.paddr) : 0U;
    if ((0xFFFFFFFFU - read_wait) < fault_extra){ read_wait = 0xFFFFFFFFU; }else{ read_wait += fault_extra; }
#endif
    if (sd_state.ppos >= read_wait){
     sd_state.data  = 0xFEU; /* Read data token */
     sd_state.pstat = PSTAT_RDATA;
     sd_state.ppos  = 0U;
     sd_state.cc16v = 0x0000U;
#ifdef ENABLE_SD_FAULT
     if (fault_extra != 0U){ cu_spisd_fault_fire(CU_SPISD_FAULT_TOKEN_DELAY, fault_cmd, sd_state.paddr, TRUE, cycle); }
#endif
    }else{
     sd_state.ppos  ++;
    }
   }
   break;

  case PSTAT_RDATA:     /* Read data bytes (512) */
   sd_state.data  = cu_vfat_read((sd_state.paddr << 9) + sd_state.ppos);
   sd_state.cc16v = cu_spisd_crc16_byte(sd_state.cc16v, sd_state.data);
   sd_state.ppos  ++;
   if (sd_state.ppos == 512U){
    sd_state.pstat = PSTAT_RCRC;
    sd_state.ppos  = 0U;
   }
   break;

  case PSTAT_RCRC:      /* Read CRC bytes (2) */
   if       (sd_state.ppos == 0U){
    sd_state.data  = (sd_state.cc16v >> 8) & 0xFFU;
#ifdef ENABLE_SD_FAULT
    if (sd_fault_any){
     auint fault_cmd = cu_spisd_fault_current_data_cmd();
     if (cu_spisd_fault_sum(CU_SPISD_FAULT_DATA_CRC, fault_cmd, sd_state.paddr, TRUE) != 0U){
      sd_state.data ^= 0x01U;
      cu_spisd_fault_fire(CU_SPISD_FAULT_DATA_CRC, fault_cmd, sd_state.paddr, TRUE, cycle);
     }
    }
#endif
   }else{
    sd_state.data  = (sd_state.cc16v     ) & 0xFFU;
    sd_state.pstat = PSTAT_IDLE;
   }
   sd_state.ppos  ++;
   break;

  case PSTAT_W24PREP:   /* CMD24 Write wait for data token */
   if (sd_state.ppos != 0U){
    if (data == 0xFEU){ /* Data token */
     sd_state.pstat = PSTAT_W24DATA;
     sd_state.ppos  = 0U;
     sd_state.cc16v = 0x0000U;
    }
   }else{
    sd_state.ppos ++;
   }
   write = TRUE;
   break;

  case PSTAT_W24DATA:   /* CMD24 Write accept data (512) */
   cu_vfat_write((sd_state.paddr << 9) + sd_state.ppos, data);
   sd_state.cc16v = cu_spisd_crc16_byte(sd_state.cc16v, data);
   sd_state.ppos  ++;
   if (sd_state.ppos == 512U){
    sd_state.pstat = PSTAT_W24CRC;
    sd_state.ppos  = 0U;
   }
   write = TRUE;
   break;

  case PSTAT_W24CRC:    /* CMD24 Write accept CRC (2) */
   if       (sd_state.ppos == 0U){
    sd_state.cc16c = data << 8;
   }else{
    sd_state.cc16c |= data;
    sd_state.pstat = PSTAT_WBUSY;
    {
     auint busy_ms = sd_model.write_busy_ms;
#ifdef ENABLE_SD_FAULT
     if (sd_fault_any){
      auint extra = cu_spisd_fault_busy_extra(24U, sd_state.paddr, cycle);
      if ((0xFFFFFFFFU - busy_ms) < extra){ busy_ms = 0xFFFFFFFFU; }else{ busy_ms += extra; }
     }
#endif
     sd_state.next  = WRAP32(cycle + (busy_ms * SPI_1MS));
    }
    if ((sd_state.crc) && (sd_state.cc16c != sd_state.cc16v)){
     /* Note: Actual rejection is not implemented (data is still written) */
     sd_state.data  = 0x0BU; /* Data response token: Rejected due to CRC. */
    }else{
     sd_state.data  = 0x05U; /* Data response token: Accepted. */
    }
#ifdef ENABLE_SD_FAULT
    if (sd_fault_any && (cu_spisd_fault_sum(CU_SPISD_FAULT_DATA_CRC, 24U, sd_state.paddr, TRUE) != 0U)){
     sd_state.data = 0x0BU;
     cu_spisd_fault_fire(CU_SPISD_FAULT_DATA_CRC, 24U, sd_state.paddr, TRUE, cycle);
    }
#endif
   }
   sd_state.ppos  ++;
   write = TRUE;
   break;

  case PSTAT_W25PREP:   /* CMD25 Write wait for data token */
   if (sd_state.ppos != 0U){
    if (data == 0xFCU){    /* Data token */
     sd_state.pstat = PSTAT_W25DATA;
     sd_state.ppos  = 0U;
     sd_state.cc16v = 0x0000U;
    }else{
     if (data == 0xFDU){   /* Stop transmission */
      auint busy_ms = sd_model.write_busy_ms;
      sd_state.pstat = PSTAT_WBUSY;
#ifdef ENABLE_SD_FAULT
      if (sd_fault_any){
       auint extra = cu_spisd_fault_busy_extra(25U, sd_state.paddr, cycle);
       if ((0xFFFFFFFFU - busy_ms) < extra){ busy_ms = 0xFFFFFFFFU; }else{ busy_ms += extra; }
      }
#endif
      sd_state.next  = WRAP32(cycle + (busy_ms * SPI_1MS));
     }
    }
   }else{
    sd_state.ppos ++;
   }
   write = TRUE;
   break;

  case PSTAT_W25DATA:   /* CMD25 Write accept data (512) */
   cu_vfat_write((sd_state.paddr << 9) + sd_state.ppos, data);
   sd_state.cc16v = cu_spisd_crc16_byte(sd_state.cc16v, data);
   sd_state.ppos  ++;
   if (sd_state.ppos == 512U){
    sd_state.pstat = PSTAT_W25CRC;
    sd_state.ppos  = 0U;
   }
   write = TRUE;
   break;

  case PSTAT_W25CRC:    /* CMD25 Write accept CRC (2) */
   if       (sd_state.ppos == 0U){
    sd_state.cc16c = data << 8;
    sd_state.ppos  ++;
   }else{
    sd_state.cc16c |= data;
    sd_state.pstat = PSTAT_W25PREP;
    sd_state.ppos  = 0U;    /* Skip being busy here */
    if ((sd_state.crc) && (sd_state.cc16c != sd_state.cc16v)){
     /* Note: Actual rejection is not implemented (data is still written) */
     sd_state.data  = 0x0BU; /* Data response token: Rejected due to CRC. */
    }else{
     sd_state.data  = 0x05U; /* Data response token: Accepted. */
    }
#ifdef ENABLE_SD_FAULT
    if (sd_fault_any && (cu_spisd_fault_sum(CU_SPISD_FAULT_DATA_CRC, 25U, sd_state.paddr, TRUE) != 0U)){
     sd_state.data = 0x0BU;
     cu_spisd_fault_fire(CU_SPISD_FAULT_DATA_CRC, 25U, sd_state.paddr, TRUE, cycle);
    }
#endif
   }
   write = TRUE;
   break;

  case PSTAT_WBUSY:     /* Busy after the end of writes */
   if (WRAP32(sd_state.next - cycle) >= 0x80000000U){ /* Done */
    sd_state.pstat = PSTAT_IDLE;
   }else{
    if (sd_state.ena){  /* Only pulls it low if Chip Select is enabled */
     sd_state.data = 0x00U;
    }
   }
   break;

  default:
   break;

 }

 /* Generic SD command processing */

 if ((sd_state.ena) && (!write)){

  if ( (sd_state.state != STAT_UNINIT) &&
       (sd_state.state != STAT_NINIT) ){

   if       ((sd_state.cmd & SCMD_X) != 0U){ /* Returning extra response bytes */

    sd_state.cmd = SCMD_X;    /* Just make sure only SCMD_X is present (see hack below) */
    if (sd_state.evcnt < 4U){ /* Data bytes */
     sd_state.data = (sd_state.crarg >> ((3U - sd_state.evcnt) * 8U)) & 0xFFU;
     sd_state.evcnt ++;
    }else{                    /* End of response */
     sd_state.cmd = 0U;
    }

   }

   /* Normally this below should be in an "else if". This is non-standard as
   ** it allows breaking a response to interpret a new command. This bypass is
   ** added to support Tempest which doesn't respect the R1b response, thus
   ** allowing that game to bypass it (that game only works on such SD cards
   ** which send very few "busy" bytes in an R1b, that is, less than 4). */

   if       ((sd_state.cmd & SCMD_R) == 0U){ /* Waiting for command */

    if ((data & 0xC0U) == 0x40U){            /* Valid command byte */
     sd_state.cmd = (sd_state.cmd & SCMD_A) |
                    (data & 0x3FU) | /* Receiving */
                    SCMD_R;          /* While retaining SCMD_A if it was there */
     sd_state.crarg = 0U;
     sd_state.evcnt = 0U;
     sd_state.r1    = 0U;
     sd_state.cc7v  = cu_spisd_crc7_byte(0x00U, data);
    }

   }else if ((sd_state.cmd & SCMD_N) == 0U){ /* Processing command */

    if (sd_state.evcnt < 4U){ /* Data bytes */
     sd_state.crarg |= data << ((3U - sd_state.evcnt) * 8U);
     sd_state.cc7v  = cu_spisd_crc7_byte(sd_state.cc7v, data);
     sd_state.evcnt ++;
    }else{                    /* CRC byte */
     sd_state.cmd  |= SCMD_N;
     sd_state.evcnt = 0U;
     if (sd_state.crc && (((sd_state.cc7v << 1) | 0x01U) != data)){
      sd_state.r1 |= R1_CRC;  /* CRC error detected (Only if CRC is ON) */
     }
     sd_state.data  = 0xFFU;  /* Forced stuff byte (overriding transmission data if any) */
    }

   }else{                                    /* Command response waits */

    {
     auint cmd_wait = sd_model.cmd_wait_bytes;
#ifdef ENABLE_SD_FAULT
     if (sd_fault_any){
      auint extra = cu_spisd_fault_response_wait(sd_state.cmd & 0x3FU, sd_state.crarg);
      if ((0xFFFFFFFFU - cmd_wait) < extra){ cmd_wait = 0xFFFFFFFFU; }else{ cmd_wait += extra; }
     }
#endif
    if (sd_state.evcnt >= cmd_wait){
     if ((sd_state.cmd & 0x3FU) == 55U){ /* App. command */
#ifdef ENABLE_SD_FAULT
      if (sd_fault_any){ cu_spisd_fault_command_end(sd_state.cmd, cycle); }
#endif
      sd_state.cmd = SCMD_A;  /* No command end mark since this will be an app. command */
      if ( (sd_state.state == STAT_NATIVE) ||
           (sd_state.state == STAT_IDLE) ||
           (sd_state.state == STAT_VERIFIED) ||
           (sd_state.state == STAT_IINIT) ){
       sd_state.r1 |= R1_IDLE;
      }
      sd_state.data = sd_state.r1; /* Create an R1 response for it (will not be processed otherwise) */
     }else{
      sd_state.cmd = 0U;
      atend = TRUE;
     }
    }else{
     sd_state.data  = 0xFFU;  /* Forced stuff byte (overriding transmission data if any) */
     sd_state.evcnt ++;
    }
    }

   }

  }

 }

 /* Fault injection is applied after a command packet has been completely
 ** accepted, but before the normal state machine consumes the resulting R1. */
#ifdef ENABLE_SD_FAULT
 if (atend && sd_fault_any){ cu_spisd_fault_command_end(cmd, cycle); }
#endif

 /* SD state machine */

 switch (sd_state.state){

  case STAT_UNINIT:        /* Uninitialized: Waiting for CS high and Data high */

   sd_state.pstat = PSTAT_IDLE;
   if ( (WRAP32(cycle - sd_state.recvc) >= sd_model.init_min_byte_cycles) && /* SPI timing constraint OK */
        (WRAP32(cycle - sd_state.recvc) <= sd_model.init_max_byte_cycles) && /* SPI timing constraint OK */
        (data == 0xFFU) &&                             /* Data high satisfied */
        (!sd_state.ena) &&                             /* CS high (card is deselected) satisfied */
        (WRAP32(cycle - sd_state.enac)  >= (sd_model.cs_high_ms * SPI_1MS)) ){ /* At least the required init time passed */
    sd_state.state = STAT_NINIT;
    sd_state.evcnt = 1U;   /* Initializing native mode, 1 data byte already got */
    sd_state.data  = 0xFFU;
   }
   break;

  case STAT_NINIT:         /* Wait for init pulses */

   sd_state.pstat = PSTAT_IDLE;
   if ( (WRAP32(cycle - sd_state.recvc) >= sd_model.init_min_byte_cycles) && /* SPI timing constraint OK */
        (WRAP32(cycle - sd_state.recvc) <= sd_model.init_max_byte_cycles) && /* SPI timing constraint OK */
        (data == 0xFFU) &&                             /* Data high satisfied */
        (!sd_state.ena) ){                             /* CS high (card is deselected) satisfied */
    sd_state.evcnt ++;
    if (sd_state.evcnt == 10U){ /* 80 pulses got, so enter Native mode */
     sd_state.state = STAT_NATIVE;
     sd_state.cmd   = 0U;
     sd_state.crc   = TRUE;     /* CRC is turned ON by this transition */
    }
   }else{
    sd_state.state = STAT_UNINIT;
   }
   break;

  case STAT_NATIVE:        /* Native mode: Waiting for a CMD0 */

   sd_state.pstat = PSTAT_IDLE;
   if ( (WRAP32(cycle - sd_state.recvc) >= sd_model.init_min_byte_cycles) && /* SPI timing constraint OK */
        (WRAP32(cycle - sd_state.recvc) <= sd_model.init_max_byte_cycles) ){ /* SPI timing constraint OK */

    if ((atend) && (sd_state.r1 == 0x00U)){ /* No error yet */
     switch (cmd & (0x3FU | SCMD_A)){

      case  0U: /* Go idle state */
       sd_state.r1   |= R1_IDLE;
       sd_state.state = STAT_IDLE;
       sd_state.crc   = FALSE; /* CRC is turned OFF by this command */
       break;

      default:  /* Other commands are not supported in native mode */
       sd_state.r1 |= R1_ILL;
       break;
     }
    }
    if (atend){
     sd_state.data  = sd_state.r1;
    }

   }
   break;

  case STAT_IDLE:          /* Idle state, waiting for some initialization */
  case STAT_VERIFIED:      /* Verified state, same */

   sd_state.pstat = PSTAT_IDLE;
   if ( (WRAP32(cycle - sd_state.recvc) >= sd_model.init_min_byte_cycles) && /* SPI timing constraint OK */
        (WRAP32(cycle - sd_state.recvc) <= sd_model.init_max_byte_cycles) ){ /* SPI timing constraint OK */
    /* Here the SD card should accept the followings:
    ** CMD0
    ** CMD1
    ** CMD8 (enters Verified state)
    ** CMD58
    ** CMD59
    ** ACMD41 (only in Verified state)
    */
    if ((atend) && (sd_state.r1 == 0x00U)){ /* No error yet */
     switch (cmd & (0x3FU | SCMD_A)){

      case  0U: /* Go idle state */
       sd_state.state = STAT_IDLE;
       break;

      case  1U: /* Initiate initialization (bypassing CMD8 - ACMD41) */
       sd_state.state = STAT_IINIT;
       sd_state.next  = WRAP32(cycle + (SPI_1MS * sd_model.init_ms));
       break;

      case  8U: /* Send Interface Condition */
       if (sd_state.crarg != 0x000001AAU){
        sd_state.r1    = 0xFFU; /* Bad argument, reject */
       }else{
        sd_state.crarg = 0x000001AAU;
        sd_state.cmd   = SCMD_X;
        sd_state.evcnt = 0U;
        sd_state.state = STAT_VERIFIED;
       }
       break;

      case 58U: /* Read OCR */
       sd_state.crarg = 0x00FF0000U; /* Report as an SDSC card & Not finished power up */
       sd_state.cmd   = SCMD_X;
       sd_state.evcnt = 0U;
       break;

      case 59U: /* Toggle CRC checks */
       sd_state.crc = ((sd_state.crarg & 1U) != 0U);
       break;

      case (41U | SCMD_A): /* Initiate initialization */
       if ( (sd_state.state == STAT_VERIFIED) &&
            ((sd_state.crarg & 0xBF00FFFFU) == 0U) ){
        sd_state.state = STAT_IINIT;
        sd_state.next  = WRAP32(cycle + (SPI_1MS * sd_model.init_ms));
        sd_state.crarg = 0x00FF0000U; /* Report as an SDSC card & Not finished power up */
        sd_state.cmd   = SCMD_X;
        sd_state.evcnt = 0U;
       }else{
        sd_state.r1 |= R1_ILL;
       }
       break;

      default:  /* Other commands are not supported in idle state */
       sd_state.r1 |= R1_ILL;
       break;
     }
    }
    if (atend){
     sd_state.r1   |= R1_IDLE;
     sd_state.data  = sd_state.r1;
    }
   }
   break;

  case STAT_IINIT:         /* Initializing */

   sd_state.pstat = PSTAT_IDLE;
   if ( (WRAP32(cycle - sd_state.recvc) >= sd_model.init_min_byte_cycles) && /* SPI timing constraint OK */
        (WRAP32(cycle - sd_state.recvc) <= sd_model.init_max_byte_cycles) ){ /* SPI timing constraint OK */
    /* Here the SD card should accept the followings:
    ** CMD0
    ** CMD1
    ** ACMD41
    */
    if ((atend) && (sd_state.r1 == 0x00U)){ /* No error yet */
     switch (cmd & (0x3FU | SCMD_A)){

      case  0U: /* Go idle state */
       sd_state.state = STAT_IDLE;
       sd_state.r1 |= R1_IDLE;
       break;

      case  1U: /* Initiate initialization */
      case (41U | SCMD_A):
       sd_state.crarg = 0x00FF0000U; /* Report as an SDSC card & Not finished power up */
       sd_state.cmd   = SCMD_X;
       sd_state.evcnt = 0U;
       if (WRAP32(sd_state.next - cycle) >= 0x80000000U){ /* Initialized */
        sd_state.state = STAT_AVAIL;
        sd_state.crarg |= 0x80000000U; /* Powered up */
       }else{
        sd_state.r1 |= R1_IDLE;
       }
       break;

      default:  /* Other commands are not supported during init */
       sd_state.r1 |= R1_ILL;
       sd_state.r1 |= R1_IDLE;
       break;
     }
    }
    if (atend){
     sd_state.data  = sd_state.r1;
    }
   }
   break;

  case STAT_AVAIL:         /* Available for data transfer (at any SPI rate) */
   /* Here the SD card should accept the followings (at least... to function
   ** as an useful card):
   ** CMD0  (Go idle state - for re-initializing)
   ** CMD16 (Set block length, just accept it and ignore, 512 is the only sensible value)
   ** CMD17 (Read single block)
   ** CMD18 (Read multiple blocks)
   ** CMD24 (Write block)
   ** CMD25 (Write multiple blocks)
   ** CMD58 (Read OCR - some init methods might do it here to get card type)
   ** CMD59 (Toggle CRC)
   ** ACMD23 (Blocks to pre-erase, just accept and ignore)
   */
   if ((atend) && (sd_state.r1 == 0x00U)){ /* No error yet */
    switch (cmd & (0x3FU | SCMD_A)){

     case  0U: /* Go idle state */
      sd_state.state = STAT_IDLE;
      sd_state.r1 |= R1_IDLE;
      break;

     case 13U: /* Send Status */
      sd_state.r1    = 0U;     /* A bit of hack to produce a zero status */
      sd_state.crarg = 0U;     /* (R2 response, 2 bytes, first byte is produced */
      sd_state.cmd   = SCMD_X; /* by the R1, second byte by the highest byte of */
      sd_state.evcnt = 3U;     /* the normally 32 bit argument) */
      break;

     case 16U: /* Set block length */
      break;   /* Accept but ignore (assuming it is just used to set 512 bytes) */

     case 17U: /* Read single block */
      sd_state.state = STAT_CMD17;
      sd_state.ppos  = 0U;
      sd_state.pstat = PSTAT_RPREP;
      sd_state.paddr = sd_state.crarg >> 9; /* SDSC card, byte argument */
      break;

     case 18U: /* Read multiple blocks */
      sd_state.state = STAT_CMD18;
      sd_state.ppos  = 0U;
      sd_state.pstat = PSTAT_RPREP;
      sd_state.paddr = sd_state.crarg >> 9; /* SDSC card, byte argument */
      break;

     case 24U: /* Write block */
      sd_state.state = STAT_CMD24;
      sd_state.ppos  = 0U;
      sd_state.pstat = PSTAT_W24PREP;
      sd_state.paddr = sd_state.crarg >> 9; /* SDSC card, byte argument */
      break;

     case 25U: /* Write multiple blocks */
      sd_state.state = STAT_CMD25;
      sd_state.ppos  = 0U;
      sd_state.pstat = PSTAT_W25PREP;
      sd_state.paddr = sd_state.crarg >> 9; /* SDSC card, byte argument */
      break;

     case 58U: /* Read OCR */
      sd_state.crarg = 0x80FF0000U; /* Report as an SDSC card & Finished power up */
      sd_state.cmd   = SCMD_X;
      sd_state.evcnt = 0U;
      break;

     case 59U: /* Toggle CRC checks */
      sd_state.crc = ((sd_state.crarg & 1U) != 0U);
      break;

     case (23U | SCMD_A): /* Blocks to pre-erase */
      break;   /* Accept but ignore (pre-erased blocks would have undefined state anyway) */

     default:
      sd_state.r1 |= R1_ILL;
      break;
    }
   }
   if (atend){
    sd_state.data  = sd_state.r1;
   }
   break;

  case STAT_CMD17:         /* CMD17: Read single block */
  case STAT_CMD18:         /* CMD18: Read multiple blocks */
   /* Only a CMD12 might be used to terminate a read */
   if (sd_state.pstat == PSTAT_IDLE){
    if (sd_state.state == STAT_CMD17){
     sd_state.state = STAT_AVAIL;
    }else{
     sd_state.ppos  = 0U;
     sd_state.pstat = PSTAT_RPREP;
     sd_state.paddr = (sd_state.paddr + 1U) & 0x007FFFFFU; /* Still an SDSC card... */
    }
   }
   if (atend){
    if ((cmd & (0x3FU | SCMD_A)) == 12U){ /* Stop transmission */
     sd_state.state = STAT_AVAIL;
     sd_state.pstat = PSTAT_IDLE;
     sd_state.data  = sd_state.r1;
     sd_state.crarg = 0x00000000U; /* Generate 4 busy bytes (a bit of hack to get a "complete" R1b) */
     sd_state.cmd   = SCMD_X;
     sd_state.evcnt = 0U;
    }
   }
   break;

  case STAT_CMD24:         /* CMD24: Write single block */
  case STAT_CMD25:         /* CMD25: Write multiple blocks */
   if (sd_state.pstat == PSTAT_IDLE){
    sd_state.state = STAT_AVAIL; /* Simply wait until the end of the transfers */
   }
   break;

  default:                 /* State machine error, shouldn't happen */
   sd_state.state = STAT_UNINIT;
   break;

 }

/* printf("%08X (r: %08X); SD: Ena: %u, Byte: %02X, %02X, Stat: %u, PSta: %u, Cmd: %03X, PPos: %3u\n",
**     cycle, sd_state.recvc, (auint)(sd_state.ena), data, sd_state.data, sd_state.state, sd_state.pstat, sd_state.cmd, sd_state.ppos);
*/
#ifdef ENABLE_SD_TRACE
 if (sd_debug_active){ cu_spisd_debug_after_send(data, cycle); }
#endif
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_mode == CU_SPISD_REPLAY_MODE_RECORD){ (void)cu_spisd_replay_append(CU_SPISD_REPLAY_EVENT_BYTE_END, cycle, cycle, data & 0xFFU, 0U); }
#endif

}



/*
** Receives a byte of data from the SD card. The passed cycle corresponds the
** cycle when it must start to clock into the AVR. 0xFF is sent when the card
** tri-states the line.
*/
auint cu_spisd_recv(auint cycle)
{
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_mode == CU_SPISD_REPLAY_MODE_REPLAY){
  auint data = 0xFFU;
  if (cu_spisd_replay_recv_byte(cycle, &data)){ return data; }
 }
#endif
 sd_state.recvc = cycle;
#ifdef ENABLE_SD_REPLAY
 if (sd_replay_mode == CU_SPISD_REPLAY_MODE_RECORD){
  auint data = sd_state.data & 0xFFU;
  (void)cu_spisd_replay_append(CU_SPISD_REPLAY_EVENT_BYTE_START, cycle, cycle, 0U, data);
  return data;
 }
#endif
 return sd_state.data;
}



/*
** Returns SD card state. It may be written, then the cu_spisd_update()
** function has to be called to rebuild any internal state depending on it.
*/
cu_state_spisd_t* cu_spisd_get_state(void)
{
 return &sd_state;
}



/*
** Rebuild internal state according to the current state. Call after writing
** the SD card state.
*/
void  cu_spisd_update(void)
{
}
