#!/usr/bin/env python3
"""Host regression for raster breakpoints, event trace/waveform metadata, and scanline profiler."""
from __future__ import annotations
import os,pathlib,shutil,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL='''#ifndef SDL_H\n#define SDL_H\n#include <stdint.h>\ntypedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int16_t Sint16; typedef int32_t Sint32;\n#endif\n'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include "debug_timing.h"
int main(void){
 cu_timing_raster_t r; cu_timing_event_t e; cu_timing_line_stat_t s; cu_timing_beam_instruction_t bi; uint32 seq;
 cu_debug_timing_reset();
 assert(!cu_debug_timing_trace_gate && !cu_debug_timing_instruction_gate && !cu_debug_timing_scan_gate && !cu_debug_timing_sync_gate);
 assert(cu_debug_timing_trace_get_mask()==CU_TIMING_TRACE_MASK_DEFAULT);
 assert((cu_debug_timing_trace_get_mask() & CU_TIMING_MASK_EVENT(CU_TIMING_EVT_PORTC))==0U);
 cu_debug_timing_raster_set(TRUE,FALSE,12U,299U);
 assert(cu_debug_timing_instruction_gate);
 assert(!cu_debug_timing_after_instruction(12U,296U,12U,298U,0x100U,2U,1000U));
 assert(cu_debug_timing_after_instruction(12U,298U,12U,301U,0x101U,3U,1003U));
 assert(!cu_debug_timing_instruction_gate && !cu_debug_timing_sync_gate);
 cu_debug_timing_raster_get(&r,FALSE); assert(r.hit && r.hit_row==12U && r.hit_cycle==301U && r.hit_pc==0x101U);
 cu_debug_timing_raster_get(&r,TRUE); assert(cu_debug_timing_instruction_gate);
 cu_debug_timing_raster_set_event(TRUE,TRUE,0U,CU_TIMING_RASTER_SYNC_RISE); assert(cu_debug_timing_sync_gate && cu_debug_timing_instruction_gate);
 cu_debug_timing_sync_edge(44U,136U,TRUE); assert(cu_debug_timing_after_instruction(44U,134U,44U,136U,0x155U,2U,2000U));
 assert(!cu_debug_timing_instruction_gate && !cu_debug_timing_sync_gate);
 cu_debug_timing_raster_get(&r,FALSE); assert(r.hit && r.mode==CU_TIMING_RASTER_SYNC_RISE && r.hit_row==44U && r.hit_cycle==136U && r.hit_pc==0x155U);
 cu_debug_timing_raster_set(FALSE,FALSE,0U,0U);
 cu_debug_timing_beam_history_enable(TRUE); assert(cu_debug_timing_instruction_gate);
 assert(!cu_debug_timing_after_instruction(20U,100U,20U,104U,0x220U,4U,3004U));
 assert(cu_debug_timing_beam_history_count()==1U); seq=cu_debug_timing_beam_history_first_seq();
 assert(cu_debug_timing_beam_history_get(seq,&bi)); assert(bi.pc==0x220U && bi.start_row==20U && bi.start_cycle==100U && bi.end_cycle==104U && bi.abs_start==3000U && bi.abs_end==3004U);
 cu_debug_timing_beam_history_enable(FALSE); assert(!cu_debug_timing_instruction_gate);
 cu_debug_timing_trace_enable(TRUE);
 assert(cu_debug_timing_trace_gate);
 /* PORTC is intentionally filtered by the default mask. */
 cu_debug_timing_trace_event(CU_TIMING_EVT_PORTC,0x55U,0U,12U,301U,12345U);
 assert(cu_debug_timing_trace_count()==0U);
 cu_debug_timing_trace_set_mask(CU_TIMING_TRACE_MASK_ALL);
 cu_debug_timing_trace_event(CU_TIMING_EVT_PORTC,0x55U,0U,12U,301U,12345U);
 cu_debug_timing_trace_spi(0xA5U,0x3CU,16U,CU_TIMING_SPI_CPHA,12U,302U,12346U);
 cu_debug_timing_trace_irq_enter(0x1AU,12U,303U,12347U);
 cu_debug_timing_trace_irq_exit(12U,311U,12355U);
 cu_debug_timing_trace_event(CU_TIMING_EVT_CTR_CLOCK,1U,0U,12U,312U,12356U);
 cu_debug_timing_trace_uart(CU_TIMING_EVT_UART_TX,0x55U,0U,248U,8U,1U,0U,0U,0U,12400U,12357U);
 cu_debug_timing_trace_uart(CU_TIMING_EVT_UART_RX,0xA3U,0x10U,248U,8U,1U,2U,0U,0U,12500U,12358U);
 assert(cu_debug_timing_trace_count()==7U);
 seq=cu_debug_timing_trace_first_seq();
 assert(cu_debug_timing_trace_get(seq,&e)); assert(e.type==CU_TIMING_EVT_PORTC && e.value==0x55U && e.beam_cycle==301U);
 assert(cu_debug_timing_trace_get(seq+1U,&e)); assert(e.type==CU_TIMING_EVT_SPI && e.value==0xA5U && e.value2==0x3CU && e.aux==16U && (e.flags&CU_TIMING_SPI_CPHA));
 assert(cu_debug_timing_trace_get(seq+2U,&e)); assert(e.type==CU_TIMING_EVT_IRQ_ENTER && e.aux==0x1AU && e.value==1U);
 assert(cu_debug_timing_trace_get(seq+3U,&e)); assert(e.type==CU_TIMING_EVT_IRQ_EXIT && e.aux==0x1AU && e.value==1U);
 assert(cu_debug_timing_trace_get(seq+5U,&e)); assert(e.type==CU_TIMING_EVT_UART_TX && e.value==0x55U && e.aux==248U && e.detail==12400U && (e.flags&CU_TIMING_UART_DATA_MASK)==3U);
 assert(cu_debug_timing_trace_get(seq+6U,&e)); assert(e.type==CU_TIMING_EVT_UART_RX && e.value==0xA3U && e.value2==0x10U && e.detail==12500U && ((e.flags&CU_TIMING_UART_PARITY_MASK)>>CU_TIMING_UART_PARITY_SHIFT)==2U);
 /* IRQ bookkeeping must survive a custom mask that records EXIT only. */
 cu_debug_timing_trace_clear();
 cu_debug_timing_trace_set_mask(CU_TIMING_MASK_EVENT(CU_TIMING_EVT_IRQ_EXIT));
 cu_debug_timing_trace_irq_enter(0x1CU,12U,320U,12364U);
 cu_debug_timing_trace_irq_exit(12U,330U,12374U);
 assert(cu_debug_timing_trace_count()==1U);
 seq=cu_debug_timing_trace_first_seq();
 assert(cu_debug_timing_trace_get(seq,&e)); assert(e.type==CU_TIMING_EVT_IRQ_EXIT && e.aux==0x1CU && e.value==1U);
 cu_debug_timing_raster_set(FALSE,FALSE,0U,0U); assert(!cu_debug_timing_instruction_gate);
 cu_debug_timing_scan_enable(TRUE); assert(cu_debug_timing_scan_gate && cu_debug_timing_instruction_gate); cu_debug_timing_after_instruction(12U,396U,12U,400U,0x200U,4U,4000U); cu_debug_timing_after_instruction(12U,400U,12U,404U,0x200U,4U,4004U); cu_debug_timing_row_boundary(12U,1811U,13U); assert(cu_debug_timing_scan_get(12U,&s)); assert(s.valid && s.line_cycles==1811U && s.slack_cycles==9 && s.instructions==2U && s.instruction_cycles==8U);
 puts("debug timing regression: PASS"); return 0;
}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-timing-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS)
  exe=d/'t';subprocess.run([cc,'-std=gnu99','-DENABLE_BEAM_HISTORY=1','-I',str(d),'-I',str(ROOT),str(ROOT/'debug_timing.c'),str(d/'h.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
