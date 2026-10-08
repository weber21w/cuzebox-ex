#include "debug_timing.h"
#include <string.h>

#define LINE_TARGET 1820U
#define IRQ_STACK_MAX 8U

static cu_timing_raster_t raster;
static boole raster_pending;
static cu_timing_event_t trace_buf[CU_TIMING_TRACE_CAP];
static uint32 trace_seq;
static auint trace_count;
static boole trace_on;
boole cu_debug_timing_trace_gate;
static uint32 trace_mask=CU_TIMING_TRACE_MASK_DEFAULT;
static uint16 irq_stack[IRQ_STACK_MAX];
static auint irq_depth;
static boole scan_on;
boole cu_debug_timing_instruction_gate;
boole cu_debug_timing_scan_gate;
boole cu_debug_timing_sync_gate;
static cu_timing_line_stat_t scan_cur[CU_TIMING_LINES];
static cu_timing_line_stat_t scan_last[CU_TIMING_LINES];
static uint32 scan_frame;
static boole scan_data_valid;
#if defined(ENABLE_BEAM_HISTORY)
static boole beam_history_on;
static cu_timing_beam_instruction_t beam_history[CU_TIMING_BEAM_HISTORY_CAP];
static uint32 beam_history_seq;
static auint beam_history_count;
#endif

static void timing_refresh_gates(void){
 boole raster_active=(raster.enabled && !raster.hit)?TRUE:FALSE;
 boole instruction_on=(scan_on || raster_active)?TRUE:FALSE;
#if defined(ENABLE_BEAM_HISTORY)
 if(beam_history_on)instruction_on=TRUE;
#endif
 cu_debug_timing_instruction_gate=instruction_on;
 cu_debug_timing_scan_gate=scan_on;
 cu_debug_timing_sync_gate=(raster_active && (raster.mode==CU_TIMING_RASTER_SYNC_RISE || raster.mode==CU_TIMING_RASTER_SYNC_FALL))?TRUE:FALSE;
}

void cu_debug_timing_reset(void){
 memset(&raster,0,sizeof(raster)); raster_pending=FALSE;
 trace_on=FALSE; cu_debug_timing_trace_gate=FALSE; trace_seq=0U; trace_count=0U; trace_mask=CU_TIMING_TRACE_MASK_DEFAULT;
 memset(irq_stack,0,sizeof(irq_stack)); irq_depth=0U;
 scan_on=FALSE; scan_frame=0U; scan_data_valid=FALSE;
#if defined(ENABLE_BEAM_HISTORY)
 beam_history_on=FALSE; beam_history_seq=0U; beam_history_count=0U;
#endif
 timing_refresh_gates();
}

void cu_debug_timing_raster_set(boole enabled, boole any_row, auint row, auint cycle){
 raster.enabled=enabled?TRUE:FALSE; raster.any_row=any_row?TRUE:FALSE;
 raster.row=row%CU_TIMING_LINES; raster.cycle=cycle; raster.mode=CU_TIMING_RASTER_CYCLE;
 raster.hit=FALSE; raster_pending=FALSE;
 timing_refresh_gates();
}

void cu_debug_timing_raster_set_event(boole enabled, boole any_row, auint row, auint mode){
 raster.enabled=enabled?TRUE:FALSE; raster.any_row=any_row?TRUE:FALSE;
 raster.row=row%CU_TIMING_LINES; raster.cycle=0U;
 if(mode!=CU_TIMING_RASTER_SYNC_RISE && mode!=CU_TIMING_RASTER_SYNC_FALL) mode=CU_TIMING_RASTER_SYNC_RISE;
 raster.mode=mode; raster.hit=FALSE; raster_pending=FALSE;
 timing_refresh_gates();
}

void cu_debug_timing_raster_get(cu_timing_raster_t* out, boole clear_hit){
 if(out)*out=raster;
 if(clear_hit){ raster.hit=FALSE; raster_pending=FALSE; timing_refresh_gates(); }
}

void cu_debug_timing_sync_edge(auint row, auint beam_cycle, boole rising){
 auint want;
 if(!cu_debug_timing_sync_gate || raster.hit)return;
 want=rising?CU_TIMING_RASTER_SYNC_RISE:CU_TIMING_RASTER_SYNC_FALL;
 if(raster.mode!=want)return;
 if(!raster.any_row && row!=raster.row)return;
 raster.hit=TRUE; raster_pending=TRUE; raster.hit_row=row; raster.hit_cycle=beam_cycle; raster.hit_pc=0U;
}

static void scan_add_pc(cu_timing_line_stat_t* s, auint pc, auint cyc){
 auint i, min_i=0U;
 for(i=0;i<CU_TIMING_TOP_PC;i++){
  if(s->top_cycles[i] && s->top_pc[i]==(pc&0x7FFFU)){ s->top_cycles[i]+=cyc; return; }
  if(s->top_cycles[i]==0U){ s->top_pc[i]=(uint16)(pc&0x7FFFU); s->top_cycles[i]=cyc; return; }
  if(s->top_cycles[i]<s->top_cycles[min_i]) min_i=i;
 }
 /* Space-saving heavy hitter approximation. */
 if(cyc>=s->top_cycles[min_i]){ s->top_pc[min_i]=(uint16)(pc&0x7FFFU); s->top_cycles[min_i]=cyc; }
 else s->top_cycles[min_i]-=cyc;
}

void cu_debug_timing_row_boundary(auint old_row, auint old_cycles, auint new_row){
 if(!scan_on)return;
 if(new_row==0U && old_row!=0U){ memcpy(scan_last,scan_cur,sizeof(scan_last)); memset(scan_cur,0,sizeof(scan_cur)); scan_frame++; }
 if(old_row<CU_TIMING_LINES){
  cu_timing_line_stat_t* s=&scan_cur[old_row];
  s->line_cycles=(uint16)((old_cycles>65535U)?65535U:old_cycles);
  s->slack_cycles=(sint16)((sint32)LINE_TARGET-(sint32)old_cycles);
  s->overrun=(old_cycles>LINE_TARGET)?1U:0U; s->valid=1U;
 }
}

#if defined(ENABLE_BEAM_HISTORY)
static void beam_history_store(auint start_row, auint start_cycle, auint end_row, auint end_cycle, auint pc, auint cycles, auint abs_end){
 cu_timing_beam_instruction_t* e; uint32 seq;
 if(!beam_history_on)return;
 seq=beam_history_seq++; e=&beam_history[seq%CU_TIMING_BEAM_HISTORY_CAP];
 e->seq=seq; e->abs_end=abs_end; e->abs_start=abs_end-cycles;
 e->start_row=(uint16)start_row; e->start_cycle=(uint16)((start_cycle>65535U)?65535U:start_cycle);
 e->end_row=(uint16)end_row; e->end_cycle=(uint16)((end_cycle>65535U)?65535U:end_cycle);
 e->pc=(uint16)(pc&0x7FFFU); e->cycles=(uint16)((cycles>65535U)?65535U:cycles);
 if(beam_history_count<CU_TIMING_BEAM_HISTORY_CAP)beam_history_count++;
}

#endif

boole cu_debug_timing_after_instruction(auint start_row, auint start_cycle, auint end_row, auint end_cycle, auint pc, auint cycles, auint abs_end){
#if defined(ENABLE_BEAM_HISTORY)
 beam_history_store(start_row,start_cycle,end_row,end_cycle,pc,cycles,abs_end);
#else
 (void)start_row; (void)start_cycle; (void)abs_end;
#endif
 if(scan_on && end_row<CU_TIMING_LINES){ cu_timing_line_stat_t* s=&scan_cur[end_row]; s->instructions++; s->instruction_cycles+=cycles; scan_add_pc(s,pc,cycles); }
 if(raster_pending){ raster_pending=FALSE; raster.hit_pc=pc&0x7FFFU; timing_refresh_gates(); return TRUE; }
 if(raster.enabled && raster.mode==CU_TIMING_RASTER_CYCLE && !raster.hit && (raster.any_row || end_row==raster.row) && end_cycle>=raster.cycle){
  raster.hit=TRUE; raster.hit_row=end_row; raster.hit_cycle=end_cycle; raster.hit_pc=pc&0x7FFFU; timing_refresh_gates(); return TRUE;
 }
 return FALSE;
}

boole cu_debug_timing_beam_history_built(void){
#if defined(ENABLE_BEAM_HISTORY)
 return TRUE;
#else
 return FALSE;
#endif
}
void cu_debug_timing_beam_history_enable(boole enabled){
#if defined(ENABLE_BEAM_HISTORY)
 beam_history_on=enabled?TRUE:FALSE;
#else
 (void)enabled;
#endif
 timing_refresh_gates();
}
boole cu_debug_timing_beam_history_enabled(void){
#if defined(ENABLE_BEAM_HISTORY)
 return beam_history_on;
#else
 return FALSE;
#endif
}
void cu_debug_timing_beam_history_clear(void){
#if defined(ENABLE_BEAM_HISTORY)
 beam_history_seq=0U; beam_history_count=0U;
#endif
}
auint cu_debug_timing_beam_history_count(void){
#if defined(ENABLE_BEAM_HISTORY)
 return beam_history_count;
#else
 return 0U;
#endif
}
uint32 cu_debug_timing_beam_history_first_seq(void){
#if defined(ENABLE_BEAM_HISTORY)
 return beam_history_seq-(uint32)beam_history_count;
#else
 return 0U;
#endif
}
boole cu_debug_timing_beam_history_get(uint32 seq, cu_timing_beam_instruction_t* out){
#if defined(ENABLE_BEAM_HISTORY)
 uint32 first=cu_debug_timing_beam_history_first_seq();
 if(seq<first||seq>=beam_history_seq)return FALSE;
 if(out)*out=beam_history[seq%CU_TIMING_BEAM_HISTORY_CAP];
 return TRUE;
#else
 (void)seq; (void)out; return FALSE;
#endif
}

static void trace_store(cu_timing_event_t const* src){
 cu_timing_event_t* e; uint32 seq;
 if(!trace_on || src==NULL)return;
 if(src->type>=32U || (trace_mask & CU_TIMING_MASK_EVENT(src->type))==0U)return;
 seq=trace_seq++; e=&trace_buf[seq%CU_TIMING_TRACE_CAP]; *e=*src; e->seq=seq;
 if(trace_count<CU_TIMING_TRACE_CAP)trace_count++;
}

void cu_debug_timing_trace_enable(boole enabled){
 trace_on=enabled?TRUE:FALSE;
 cu_debug_timing_trace_gate=trace_on;
 if(trace_on){ memset(irq_stack,0,sizeof(irq_stack)); irq_depth=0U; }
}
boole cu_debug_timing_trace_enabled(void){ return trace_on; }
void cu_debug_timing_trace_clear(void){ trace_seq=0U; trace_count=0U; memset(irq_stack,0,sizeof(irq_stack)); irq_depth=0U; }
void cu_debug_timing_trace_set_mask(uint32 mask){ trace_mask=mask & CU_TIMING_TRACE_MASK_ALL; }
uint32 cu_debug_timing_trace_get_mask(void){ return trace_mask; }
boole cu_debug_timing_trace_event_enabled(auint type){ return (trace_on && type<32U && (trace_mask & CU_TIMING_MASK_EVENT(type))!=0U)?TRUE:FALSE; }

void cu_debug_timing_trace_event(auint type, auint value, auint aux, auint row, auint beam_cycle, auint abs_cycle){
 cu_timing_event_t e;
 if(!cu_debug_timing_trace_event_enabled(type))return;
 memset(&e,0,sizeof(e)); e.abs_cycle=abs_cycle; e.row=(uint16)row; e.beam_cycle=(uint16)beam_cycle; e.type=(uint8)type; e.value=(uint8)value; e.aux=(uint16)aux;
 trace_store(&e);
}

void cu_debug_timing_trace_spi(auint tx, auint rx, auint duration, auint flags, auint row, auint beam_cycle, auint abs_cycle){
 cu_timing_event_t e;
 if(!cu_debug_timing_trace_event_enabled(CU_TIMING_EVT_SPI))return;
 memset(&e,0,sizeof(e)); e.abs_cycle=abs_cycle; e.row=(uint16)row; e.beam_cycle=(uint16)beam_cycle; e.type=CU_TIMING_EVT_SPI; e.value=(uint8)tx; e.value2=(uint8)rx; e.flags=(uint8)(flags & (CU_TIMING_SPI_CPOL|CU_TIMING_SPI_CPHA|CU_TIMING_SPI_DORD)); e.aux=(uint16)duration;
 trace_store(&e);
}

void cu_debug_timing_trace_uart(auint type, auint byte, auint error_flags, auint bit_cycles, auint data_bits, auint stop_bits, auint parity, auint synchronous, auint double_speed, auint wire_start_cycle, auint observed_cycle){
 cu_timing_event_t e; auint flags=0U;
 if(type!=CU_TIMING_EVT_UART_TX && type!=CU_TIMING_EVT_UART_RX)return;
 if(!cu_debug_timing_trace_event_enabled(type))return;
 if(data_bits<5U){ data_bits=5U; }
 if(data_bits>8U){ data_bits=8U; }
 flags|=(data_bits-5U)&CU_TIMING_UART_DATA_MASK;
 if(stop_bits>=2U)flags|=CU_TIMING_UART_STOP2;
 flags|=(parity&3U)<<CU_TIMING_UART_PARITY_SHIFT;
 if(synchronous)flags|=CU_TIMING_UART_SYNC;
 if(double_speed)flags|=CU_TIMING_UART_U2X;
 memset(&e,0,sizeof(e)); e.abs_cycle=observed_cycle; e.row=0xFFFFU; e.beam_cycle=0xFFFFU; e.type=(uint8)type; e.value=(uint8)byte; e.value2=(uint8)error_flags; e.flags=(uint8)flags;
 e.aux=(uint16)((bit_cycles>65535U)?65535U:bit_cycles); e.detail=(uint32)wire_start_cycle;
 trace_store(&e);
}

void cu_debug_timing_trace_irq_enter(auint vector, auint row, auint beam_cycle, auint abs_cycle){
 cu_timing_event_t e;
 /* Maintain nesting state whenever the analyzer is armed, even if ENTER is
 ** masked out. Otherwise an EXIT-only custom mask would lose context. */
 if(!trace_on)return;
 if(irq_depth<IRQ_STACK_MAX)irq_stack[irq_depth]=(uint16)vector;
 if(irq_depth<255U)irq_depth++;
 if(!cu_debug_timing_trace_event_enabled(CU_TIMING_EVT_IRQ_ENTER))return;
 memset(&e,0,sizeof(e)); e.abs_cycle=abs_cycle; e.row=(uint16)row; e.beam_cycle=(uint16)beam_cycle; e.type=CU_TIMING_EVT_IRQ_ENTER; e.value=(uint8)((irq_depth>255U)?255U:irq_depth); e.aux=(uint16)vector;
 trace_store(&e);
}

void cu_debug_timing_trace_irq_exit(auint row, auint beam_cycle, auint abs_cycle){
 cu_timing_event_t e; auint vector=0U, depth=irq_depth;
 if(!trace_on)return;
 if(irq_depth>0U){ auint i=(irq_depth>IRQ_STACK_MAX)?(IRQ_STACK_MAX-1U):(irq_depth-1U); vector=irq_stack[i]; irq_depth--; }
 if(!cu_debug_timing_trace_event_enabled(CU_TIMING_EVT_IRQ_EXIT))return;
 memset(&e,0,sizeof(e)); e.abs_cycle=abs_cycle; e.row=(uint16)row; e.beam_cycle=(uint16)beam_cycle; e.type=CU_TIMING_EVT_IRQ_EXIT; e.value=(uint8)((depth>255U)?255U:depth); e.aux=(uint16)vector;
 trace_store(&e);
}

auint cu_debug_timing_trace_count(void){ return trace_count; }
uint32 cu_debug_timing_trace_first_seq(void){ return trace_seq-(uint32)trace_count; }
boole cu_debug_timing_trace_get(uint32 seq, cu_timing_event_t* out){ uint32 first=cu_debug_timing_trace_first_seq(); if(seq<first||seq>=trace_seq)return FALSE; if(out)*out=trace_buf[seq%CU_TIMING_TRACE_CAP]; return TRUE; }

void cu_debug_timing_scan_enable(boole enabled){
 scan_on=enabled?TRUE:FALSE;
 if(scan_on && !scan_data_valid){ memset(scan_cur,0,sizeof(scan_cur)); memset(scan_last,0,sizeof(scan_last)); scan_frame=0U; scan_data_valid=TRUE; }
 timing_refresh_gates();
}
boole cu_debug_timing_scan_enabled(void){ return scan_on; }
void cu_debug_timing_scan_clear(void){ memset(scan_cur,0,sizeof(scan_cur)); memset(scan_last,0,sizeof(scan_last)); scan_frame=0U; scan_data_valid=TRUE; }
uint32 cu_debug_timing_scan_frame(void){ return scan_frame; }
boole cu_debug_timing_scan_get(auint row, cu_timing_line_stat_t* out){
 if(row>=CU_TIMING_LINES)return FALSE;
 if(!scan_data_valid){ if(out)memset(out,0,sizeof(*out)); return TRUE; }
 if(out){ *out=scan_last[row].valid?scan_last[row]:scan_cur[row]; }
 return TRUE;
}
