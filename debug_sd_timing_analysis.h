#ifndef DEBUG_SD_TIMING_ANALYSIS_H
#define DEBUG_SD_TIMING_ANALYSIS_H

#include "types.h"

#define DEBUG_SD_TIMING_MAX_SAMPLES 64U
#define DEBUG_SD_TIMING_FLAG_TRACE_BREAK  0x0001U
#define DEBUG_SD_TIMING_FLAG_CRC_ERROR    0x0002U
#define DEBUG_SD_TIMING_FLAG_INIT_FAIL    0x0004U
#define DEBUG_SD_TIMING_FLAG_LATENCY      0x0008U
#define DEBUG_SD_TIMING_FLAG_FAULT        0x0010U
#define DEBUG_SD_TIMING_FLAG_ABORT        0x0020U

typedef struct{
 uint32 transaction_id;
 uint32 first_seq;
 uint32 last_seq;
 uint32 sector;
 uint32 arg;
 uint32 command_start_cycle;
 uint32 command_end_cycle;
 uint32 response_cycle;
 uint32 block_start_cycle;
 uint32 token_cycle;
 uint32 data_end_cycle;
 uint32 crc_end_cycle;
 uint32 busy_end_cycle;
 uint32 end_cycle;
 uint32 command_span_cycles;
 uint32 response_wait_cycles;
 uint32 token_wait_cycles;
 uint32 data_cycles;
 uint32 crc_cycles;
 uint32 busy_cycles;
 uint32 total_cycles;
 uint32 command_gap_min_cycles;
 uint32 command_gap_max_cycles;
 uint32 command_gap_avg_cycles;
 uint32 expected_busy_min_cycles;
 uint16 start_pc;
 uint16 start_row;
 uint16 start_beam_cycle;
 uint16 crc_calculated;
 uint16 crc_received;
 uint16 flags;
 uint8 command;
 uint8 app;
 uint8 r1;
 uint8 first_block;
 uint8 direction; /* 0 none, 1 read, 2 write */
 uint8 command_crc;
 uint8 command_crc_expected;
 uint8 command_crc_ok;
 uint8 complete;
 uint8 busy_early;
}debug_sd_timing_sample_t;

/* Reconstructs recent timing samples from the already-armed SD protocol ring.
** No emulator-path state is maintained for this view. Returns samples oldest
** to newest, dropping oldest samples if the caller's capacity is exceeded. */
auint debug_sd_timing_collect(debug_sd_timing_sample_t* out, auint capacity);

#endif
