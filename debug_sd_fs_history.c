#include <string.h>
#include "debug_sd_fs_history.h"
#include "cu_spisd.h"

#if defined(ENABLE_DEBUGGER) && defined(ENABLE_SD_TRACE) && !defined(FLAG_SELFCONT)

static boole fsop_is_data_cmd(auint cmd)
{
 return (cmd == 17U) || (cmd == 18U) || (cmd == 24U) || (cmd == 25U);
}

static auint fsop_access_for_cmd(auint cmd)
{
 return ((cmd == 24U) || (cmd == 25U)) ? CU_SD_FSOP_ACCESS_WRITE : CU_SD_FSOP_ACCESS_READ;
}

static void fsop_copy_source(char* dst, char const* src)
{
 if (src == NULL){ src = ""; }
 strncpy(dst, src, CU_SD_FSOP_SOURCE_SIZE - 1U);
 dst[CU_SD_FSOP_SOURCE_SIZE - 1U] = 0;
}

static boole fsop_same_owner(cu_sd_fsop_t const* op, cu_vfat_debug_sector_t const* fs, auint access)
{
 if ((op->access != access) || (op->role != fs->role)){ return FALSE; }
 return strcmp(op->source, fs->source) == 0;
}

static boole fsop_logically_contiguous(cu_sd_fsop_t const* op, cu_vfat_debug_sector_t const* fs)
{
 if ((fs->role == CU_VFAT_DEBUG_ROLE_FILE) || (fs->role == CU_VFAT_DEBUG_ROLE_DIRECTORY)){
  if (!op->file_offset_valid){ return FALSE; }
  return fs->file_offset == op->file_offset_end;
 }
 return fs->sector == (op->last_sector + 1U);
}

static void fsop_begin(cu_sd_fsop_t* op, cu_spisd_trace_event_t const* e,
                       cu_vfat_debug_sector_t const* fs, auint access)
{
 memset(op, 0, sizeof(*op));
 op->first_seq = e->seq; op->last_seq = e->seq;
 op->first_transaction_id = e->transaction_id; op->last_transaction_id = e->transaction_id;
 op->start_cycle = e->cycle; op->end_cycle = e->cycle;
 op->first_sector = fs->sector; op->last_sector = fs->sector;
 op->start_pc = e->pc; op->end_pc = e->pc;
 op->start_row = e->row; op->start_beam_cycle = e->beam_cycle;
 op->end_row = e->row; op->end_beam_cycle = e->beam_cycle;
 op->first_cluster = (uint16)(fs->cluster & 0xFFFFU); op->last_cluster = op->first_cluster;
 op->next_cluster = (uint16)(fs->next_cluster & 0xFFFFU);
 op->first_chain_index = (uint16)(fs->chain_index & 0xFFFFU); op->last_chain_index = op->first_chain_index;
 op->sectors_started = 1U; op->physical_runs = 1U;
 op->access = (uint8)access; op->role = (uint8)fs->role; op->r1 = e->r1;
 if (e->r1 != 0U){ op->flags |= CU_SD_FSOP_FLAG_R1_ERROR; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_BREAK) != 0U){ op->flags |= CU_SD_FSOP_FLAG_BREAK; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_CRC) != 0U){ op->flags |= CU_SD_FSOP_FLAG_CRC_ERROR; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_FAULT) != 0U){ op->flags |= CU_SD_FSOP_FLAG_FAULT; }
 if ((fs->role == CU_VFAT_DEBUG_ROLE_FILE) || (fs->role == CU_VFAT_DEBUG_ROLE_DIRECTORY)){
  op->file_offset_valid = TRUE;
  op->file_offset_start = fs->file_offset;
  op->file_offset_end = fs->file_offset + 512U;
 }
 fsop_copy_source(op->source, fs->source);
}

static void fsop_add_sector(cu_sd_fsop_t* op, cu_spisd_trace_event_t const* e,
                            cu_vfat_debug_sector_t const* fs)
{
 uint16 cl = (uint16)(fs->cluster & 0xFFFFU);
 if (fs->sector != (op->last_sector + 1U)){ op->physical_runs++; }
 if (cl != op->last_cluster){ op->cluster_transitions++; }
 op->last_seq = e->seq; op->last_transaction_id = e->transaction_id;
 op->end_cycle = e->cycle; op->last_sector = fs->sector;
 op->end_pc = e->pc; op->end_row = e->row; op->end_beam_cycle = e->beam_cycle;
 op->last_cluster = cl; op->next_cluster = (uint16)(fs->next_cluster & 0xFFFFU);
 op->last_chain_index = (uint16)(fs->chain_index & 0xFFFFU);
 op->sectors_started++;
 if (op->file_offset_valid){ op->file_offset_end = fs->file_offset + 512U; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_BREAK) != 0U){ op->flags |= CU_SD_FSOP_FLAG_BREAK; }
}

static void fsop_finish_event(cu_sd_fsop_t* op, cu_spisd_trace_event_t const* e)
{
 op->last_seq = e->seq; op->end_cycle = e->cycle; op->end_pc = e->pc;
 op->end_row = e->row; op->end_beam_cycle = e->beam_cycle;
 if (e->kind == CU_SPISD_TRACE_KIND_CRC_END){
  if (op->sectors_completed < op->sectors_started){ op->sectors_completed++; op->bytes_completed += 512U; }
 }
 if (e->kind == CU_SPISD_TRACE_KIND_ABORT){ op->flags |= CU_SD_FSOP_FLAG_ABORT; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_CRC) != 0U){ op->flags |= CU_SD_FSOP_FLAG_CRC_ERROR; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_FAULT) != 0U){ op->flags |= CU_SD_FSOP_FLAG_FAULT; }
 if ((e->flags & CU_SPISD_TRACE_FLAG_BREAK) != 0U){ op->flags |= CU_SD_FSOP_FLAG_BREAK; }
}

static void fsop_store(cu_sd_fsop_t* out, auint cap, auint* used, auint* total, cu_sd_fsop_t const* op)
{
 cu_sd_fsop_t tmp = *op;
 (*total)++;
 if (tmp.sectors_completed < tmp.sectors_started){ tmp.flags |= CU_SD_FSOP_FLAG_INCOMPLETE; }
 if ((out == NULL) || (cap == 0U)){ return; }
 if (*used < cap){ out[*used] = tmp; (*used)++; }
 else{
  memmove(&out[0], &out[1], (cap - 1U) * sizeof(out[0]));
  out[cap - 1U] = tmp;
 }
}

boole cu_sd_fs_history_built(void)
{
 return cu_spisd_trace_built();
}

auint cu_sd_fs_history_build(cu_sd_fsop_t* out, auint cap, auint* total_out)
{
 uint32 seq = cu_spisd_trace_first_seq();
 auint n = cu_spisd_trace_count();
 auint i, used = 0U, total = 0U;
 boole have = FALSE;
 uint32 pending_fault_tx = 0U;
 uint8 pending_fault_flags = 0U;
 cu_sd_fsop_t cur;
 for (i = 0U; i < n; ++i, ++seq){
  cu_spisd_trace_event_t e;
  if (!cu_spisd_trace_get(seq, &e)){ continue; }
  if ((e.kind == CU_SPISD_TRACE_KIND_FAULT) && (e.transaction_id != 0U) && (!have || (e.transaction_id != cur.last_transaction_id))){
   pending_fault_tx = e.transaction_id; pending_fault_flags |= CU_SD_FSOP_FLAG_FAULT;
   if ((e.flags & CU_SPISD_TRACE_FLAG_CRC) != 0U){ pending_fault_flags |= CU_SD_FSOP_FLAG_CRC_ERROR; }
   continue;
  }
  if (((e.kind == CU_SPISD_TRACE_KIND_COMMAND) || (e.kind == CU_SPISD_TRACE_KIND_BLOCK)) && fsop_is_data_cmd(e.cmd)){
   cu_vfat_debug_sector_t fs;
   auint access = fsop_access_for_cmd(e.cmd);
   if (!cu_vfat_debug_sector_snapshot(e.sector, FALSE, &fs)){ continue; }
   if (have && fsop_same_owner(&cur, &fs, access) && fsop_logically_contiguous(&cur, &fs) &&
       ((cur.flags & (CU_SD_FSOP_FLAG_R1_ERROR | CU_SD_FSOP_FLAG_ABORT)) == 0U) && (e.r1 == 0U)){
    fsop_add_sector(&cur, &e, &fs);
   }else{
    if (have){ fsop_store(out, cap, &used, &total, &cur); }
    fsop_begin(&cur, &e, &fs, access); have = TRUE;
   }
   if ((pending_fault_tx != 0U) && (pending_fault_tx == e.transaction_id)){ cur.flags |= pending_fault_flags; pending_fault_tx = 0U; pending_fault_flags = 0U; }
   /* An R1 error means no data phase follows this command. */
   if (e.r1 != 0U){ fsop_store(out, cap, &used, &total, &cur); have = FALSE; }
   continue;
  }
  if (have && (e.transaction_id != 0U) && (e.transaction_id == cur.last_transaction_id)){
   if ((e.kind == CU_SPISD_TRACE_KIND_CRC_END) || (e.kind == CU_SPISD_TRACE_KIND_ABORT) ||
       (e.kind == CU_SPISD_TRACE_KIND_DATA_CRC) || (e.kind == CU_SPISD_TRACE_KIND_FAULT) ||
       (e.kind == CU_SPISD_TRACE_KIND_BUSY_END)){
    fsop_finish_event(&cur, &e);
   }
  }
 }
 if (have){ fsop_store(out, cap, &used, &total, &cur); }
 if (total_out != NULL){ *total_out = total; }
 return used;
}

#endif
