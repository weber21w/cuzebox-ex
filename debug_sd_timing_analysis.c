#include "debug_sd_timing_analysis.h"
#include "cu_spisd.h"
#include <string.h>

#define SD_CPU_CYCLES_PER_MS 28634U

static boole timing_data_command(auint cmd)
{
 return (cmd == 17U) || (cmd == 18U) || (cmd == 24U) || (cmd == 25U);
}

static auint timing_direction(auint cmd)
{
 if ((cmd == 17U) || (cmd == 18U)){ return 1U; }
 if ((cmd == 24U) || (cmd == 25U)){ return 2U; }
 return 0U;
}

static debug_sd_timing_sample_t* timing_append(debug_sd_timing_sample_t* out, auint capacity, auint* count)
{
 debug_sd_timing_sample_t* s;
 if ((out == NULL) || (capacity == 0U) || (count == NULL)){ return NULL; }
 if (*count >= capacity){
  if (capacity > 1U){ memmove(out, out + 1, (capacity - 1U) * sizeof(*out)); }
  *count = capacity - 1U;
 }
 s = &out[*count];
 memset(s, 0, sizeof(*s));
 (*count)++;
 return s;
}

static debug_sd_timing_sample_t* timing_find(debug_sd_timing_sample_t* out, auint count, uint32 tid, uint32 sector)
{
 auint i = count;
 while (i != 0U){
  debug_sd_timing_sample_t* s = &out[--i];
  if ((s->transaction_id == tid) && (s->sector == sector)){ return s; }
 }
 return NULL;
}

static debug_sd_timing_sample_t* timing_find_tid(debug_sd_timing_sample_t* out, auint count, uint32 tid)
{
 auint i = count;
 while (i != 0U){
  debug_sd_timing_sample_t* s = &out[--i];
  if (s->transaction_id == tid){ return s; }
 }
 return NULL;
}

static void timing_apply_trace_flags(debug_sd_timing_sample_t* s, uint8 flags, uint8 kind)
{
 if (s == NULL){ return; }
 if ((flags & CU_SPISD_TRACE_FLAG_BREAK) != 0U){ s->flags |= DEBUG_SD_TIMING_FLAG_TRACE_BREAK; }
 if ((flags & CU_SPISD_TRACE_FLAG_CRC) != 0U){ s->flags |= DEBUG_SD_TIMING_FLAG_CRC_ERROR; }
 if ((flags & CU_SPISD_TRACE_FLAG_INIT_FAIL) != 0U){ s->flags |= DEBUG_SD_TIMING_FLAG_INIT_FAIL; }
 if ((flags & CU_SPISD_TRACE_FLAG_LATENCY) != 0U){ s->flags |= DEBUG_SD_TIMING_FLAG_LATENCY; }
 if ((flags & CU_SPISD_TRACE_FLAG_FAULT) != 0U){ s->flags |= DEBUG_SD_TIMING_FLAG_FAULT; }
 if (kind == CU_SPISD_TRACE_KIND_ABORT){ s->flags |= DEBUG_SD_TIMING_FLAG_ABORT; }
}

static void timing_finalize(debug_sd_timing_sample_t* s, cu_spisd_model_t const* m)
{
 if (s == NULL){ return; }
 if ((s->command_end_cycle != 0U) && (s->command_start_cycle != 0U)){
  s->command_span_cycles = s->command_end_cycle - s->command_start_cycle;
 }
 if ((s->response_cycle != 0U) && (s->command_end_cycle != 0U)){
  s->response_wait_cycles = s->response_cycle - s->command_end_cycle;
 }
 if ((s->token_cycle != 0U) && (s->block_start_cycle != 0U)){
  s->token_wait_cycles = s->token_cycle - s->block_start_cycle;
 }
 if ((s->data_end_cycle != 0U) && (s->token_cycle != 0U)){
  s->data_cycles = s->data_end_cycle - s->token_cycle;
 }
 if ((s->crc_end_cycle != 0U) && (s->data_end_cycle != 0U)){
  s->crc_cycles = s->crc_end_cycle - s->data_end_cycle;
 }
 if ((s->busy_end_cycle != 0U) && (s->crc_end_cycle != 0U)){
  s->busy_cycles = s->busy_end_cycle - s->crc_end_cycle;
 }
 if (s->end_cycle == 0U){
  s->end_cycle = s->busy_end_cycle ? s->busy_end_cycle : (s->crc_end_cycle ? s->crc_end_cycle :
                 (s->data_end_cycle ? s->data_end_cycle : (s->token_cycle ? s->token_cycle : s->response_cycle)));
 }
 if (s->end_cycle != 0U){
  uint32 base = s->first_block && s->command_start_cycle ? s->command_start_cycle : s->block_start_cycle;
  if (base != 0U){ s->total_cycles = s->end_cycle - base; }
 }
 /* expected_busy_min_cycles is a model floor; actual release is observed on
 ** the next polling byte, so a conforming card can remain busy longer. */
 if ((m != NULL) && (s->direction == 2U)){
  uint64_t w = (uint64_t)m->write_busy_ms * (uint64_t)SD_CPU_CYCLES_PER_MS;
  s->expected_busy_min_cycles = (w > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32)w;
  if ((s->busy_cycles != 0U) && (s->busy_cycles < s->expected_busy_min_cycles)){ s->busy_early = 1U; }
 }
 if (!timing_data_command(s->command)){
  s->complete = (s->response_cycle != 0U) ? 1U : 0U;
 }else if (s->direction == 1U){
  s->complete = (s->crc_end_cycle != 0U) ? 1U : 0U;
 }else if (s->command == 24U){
  s->complete = (s->busy_end_cycle != 0U) ? 1U : 0U;
 }else{
  s->complete = (s->crc_end_cycle != 0U) ? 1U : 0U;
 }
}

auint debug_sd_timing_collect(debug_sd_timing_sample_t* out, auint capacity)
{
 uint32 seq, end;
 auint count = 0U;
 cu_spisd_model_t const* m = cu_spisd_model_get();
 if ((out == NULL) || (capacity == 0U) || (!cu_spisd_trace_built())){ return 0U; }
 seq = cu_spisd_trace_first_seq();
 end = seq + (uint32)cu_spisd_trace_count();
 while (seq < end){
  cu_spisd_trace_event_t e;
  debug_sd_timing_sample_t* s = NULL;
  if (!cu_spisd_trace_get(seq, &e)){ seq++; continue; }
  if (e.kind == CU_SPISD_TRACE_KIND_COMMAND){
   s = timing_append(out, capacity, &count);
   if (s != NULL){
    auint i; uint32 sum = 0U, minv = 0xFFFFFFFFU, maxv = 0U, ngap = 0U;
    s->transaction_id = e.transaction_id; s->first_seq = e.seq; s->last_seq = e.seq;
    s->sector = e.sector; s->arg = e.arg; s->command = e.cmd; s->app = e.app; s->r1 = e.r1;
    s->first_block = 1U; s->direction = (uint8)timing_direction(e.cmd);
    s->command_start_cycle = e.start_cycle; s->command_end_cycle = e.response_start_cycle;
    s->response_cycle = e.cycle; s->block_start_cycle = e.cycle; s->end_cycle = e.cycle;
    s->start_pc = e.start_pc; s->start_row = e.start_row; s->start_beam_cycle = e.start_beam_cycle;
    s->command_crc = e.command_crc; s->command_crc_expected = e.command_crc_expected;
    s->command_crc_ok = (e.command_crc == e.command_crc_expected) ? 1U : 0U;
    for (i = 1U; i < 6U; ++i){
     if ((e.command_byte_cycle[i] != 0U) && (e.command_byte_cycle[i - 1U] != 0U)){
      uint32 g = e.command_byte_cycle[i] - e.command_byte_cycle[i - 1U];
      if (g < minv){ minv = g; } if (g > maxv){ maxv = g; } sum += g; ngap++;
     }
    }
    if (ngap != 0U){ s->command_gap_min_cycles = minv; s->command_gap_max_cycles = maxv; s->command_gap_avg_cycles = sum / ngap; }
    timing_apply_trace_flags(s, e.flags, e.kind);
   }
  }else if (e.kind == CU_SPISD_TRACE_KIND_BLOCK){
   s = timing_append(out, capacity, &count);
   if (s != NULL){
    s->transaction_id = e.transaction_id; s->first_seq = e.seq; s->last_seq = e.seq;
    s->sector = e.sector; s->arg = e.arg; s->command = e.cmd; s->app = e.app; s->r1 = e.r1;
    s->direction = (uint8)timing_direction(e.cmd); s->block_start_cycle = e.cycle; s->end_cycle = e.cycle;
    s->start_pc = e.pc; s->start_row = e.row; s->start_beam_cycle = e.beam_cycle;
    timing_apply_trace_flags(s, e.flags, e.kind);
   }
  }else if (e.transaction_id != 0U){
   s = timing_find(out, count, e.transaction_id, e.sector);
   if (s == NULL){ s = timing_find_tid(out, count, e.transaction_id); }
   if (s != NULL){
    s->last_seq = e.seq; s->end_cycle = e.cycle; s->r1 = e.r1;
    timing_apply_trace_flags(s, e.flags, e.kind);
    if (e.kind == CU_SPISD_TRACE_KIND_DATA_TOKEN){ s->token_cycle = e.cycle; }
    else if (e.kind == CU_SPISD_TRACE_KIND_DATA_END){ s->data_end_cycle = e.cycle; }
    else if (e.kind == CU_SPISD_TRACE_KIND_CRC_END){ s->crc_end_cycle = e.cycle; s->crc_calculated = e.crc_calculated; s->crc_received = e.crc_received; }
    else if (e.kind == CU_SPISD_TRACE_KIND_BUSY_END){ s->busy_end_cycle = e.cycle; }
   }
  }
  seq++;
 }
 { auint i; for (i = 0U; i < count; ++i){ timing_finalize(&out[i], m); } }
 return count;
}
