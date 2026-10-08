#ifndef DEBUG_SD_FS_HISTORY_H
#define DEBUG_SD_FS_HISTORY_H

#include "types.h"
#include "cu_vfat.h"

#define CU_SD_FSOP_SOURCE_SIZE 256U

#define CU_SD_FSOP_ACCESS_READ  0x01U
#define CU_SD_FSOP_ACCESS_WRITE 0x02U

#define CU_SD_FSOP_FLAG_BREAK      0x01U
#define CU_SD_FSOP_FLAG_CRC_ERROR  0x02U
#define CU_SD_FSOP_FLAG_FAULT      0x04U
#define CU_SD_FSOP_FLAG_ABORT      0x08U
#define CU_SD_FSOP_FLAG_R1_ERROR   0x10U
#define CU_SD_FSOP_FLAG_INCOMPLETE 0x20U

typedef struct{
 uint32 first_seq;
 uint32 last_seq;
 uint32 first_transaction_id;
 uint32 last_transaction_id;
 uint32 start_cycle;
 uint32 end_cycle;
 uint32 first_sector;
 uint32 last_sector;
 uint32 file_offset_start;
 uint32 file_offset_end;
 uint32 bytes_completed;
 uint16 start_pc;
 uint16 end_pc;
 uint16 start_row;
 uint16 start_beam_cycle;
 uint16 end_row;
 uint16 end_beam_cycle;
 uint16 first_cluster;
 uint16 last_cluster;
 uint16 next_cluster;
 uint16 first_chain_index;
 uint16 last_chain_index;
 uint16 sectors_started;
 uint16 sectors_completed;
 uint16 cluster_transitions;
 uint16 physical_runs;
 uint8 access;
 uint8 role;
 uint8 r1;
 uint8 flags;
 boole file_offset_valid;
 char source[CU_SD_FSOP_SOURCE_SIZE];
}cu_sd_fsop_t;

/* Reconstruct high-level filesystem operations from the already captured SD
** protocol ring. This performs no collection of its own and is intended to be
** called only by debugger/API presentation code. Returns the number of
** operations written (oldest to newest among the retained tail). */
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_SD_TRACE) && !defined(FLAG_SELFCONT)
auint cu_sd_fs_history_build(cu_sd_fsop_t* out, auint cap, auint* total_out);
boole cu_sd_fs_history_built(void);
#else
static inline auint cu_sd_fs_history_build(cu_sd_fsop_t* out, auint cap, auint* total_out)
{ (void)out; (void)cap; if (total_out != NULL){ *total_out = 0U; } return 0U; }
static inline boole cu_sd_fs_history_built(void){ return FALSE; }
#endif

#endif
