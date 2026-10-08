#!/usr/bin/env python3
"""Host regression for on-demand SD timing reconstruction from protocol history."""
from __future__ import annotations
import os, pathlib, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL='''#ifndef SDL_H\n#define SDL_H\n#include <stdint.h>\ntypedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;\n#endif\n'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "debug_sd_timing_analysis.h"
#include "cu_spisd.h"
static cu_spisd_trace_event_t ev[32]; static auint nev; static cu_spisd_model_t model;
static void add(uint8 kind,uint32 tid,uint8 cmd,uint32 sector,uint32 cycle){cu_spisd_trace_event_t*e=&ev[nev++];memset(e,0,sizeof(*e));e->seq=nev-1;e->kind=kind;e->transaction_id=tid;e->cmd=cmd;e->sector=sector;e->cycle=cycle;e->pc=(uint16)(0x100+nev);e->row=10;e->beam_cycle=20;}
boole cu_spisd_trace_built(void){return TRUE;} auint cu_spisd_trace_count(void){return nev;} uint32 cu_spisd_trace_first_seq(void){return 0;} boole cu_spisd_trace_get(uint32 seq,cu_spisd_trace_event_t*out){if(seq>=nev)return FALSE;*out=ev[seq];return TRUE;} cu_spisd_model_t const*cu_spisd_model_get(void){return &model;}
int main(void){debug_sd_timing_sample_t out[16];auint n,i;memset(&model,0,sizeof(model));model.cmd_wait_bytes=2;model.read_wait_bytes=3;model.write_busy_ms=1;
 add(CU_SPISD_TRACE_KIND_COMMAND,1,17,10,700);ev[0].start_cycle=100;ev[0].response_start_cycle=600;ev[0].arg=10U<<9;ev[0].start_pc=0x123;ev[0].start_row=7;ev[0].start_beam_cycle=99;ev[0].r1=0;ev[0].command_crc=0x55;ev[0].command_crc_expected=0x55;for(i=0;i<6;i++)ev[0].command_byte_cycle[i]=100U+i*100U;
 add(CU_SPISD_TRACE_KIND_DATA_TOKEN,1,17,10,1000);add(CU_SPISD_TRACE_KIND_DATA_END,1,17,10,5000);add(CU_SPISD_TRACE_KIND_CRC_END,1,17,10,5200);ev[3].crc_calculated=0xBEEF;
 add(CU_SPISD_TRACE_KIND_COMMAND,2,18,30,10000);ev[4].start_cycle=9400;ev[4].response_start_cycle=9900;ev[4].r1=0;ev[4].command_crc=1;ev[4].command_crc_expected=1;
 add(CU_SPISD_TRACE_KIND_DATA_TOKEN,2,18,30,10200);add(CU_SPISD_TRACE_KIND_DATA_END,2,18,30,14000);add(CU_SPISD_TRACE_KIND_BLOCK,2,18,31,14200);add(CU_SPISD_TRACE_KIND_CRC_END,2,18,30,14200);add(CU_SPISD_TRACE_KIND_DATA_TOKEN,2,18,31,14500);add(CU_SPISD_TRACE_KIND_DATA_END,2,18,31,18500);add(CU_SPISD_TRACE_KIND_CRC_END,2,18,31,18700);
 add(CU_SPISD_TRACE_KIND_COMMAND,3,24,40,20000);ev[12].start_cycle=19400;ev[12].response_start_cycle=19900;ev[12].r1=0;ev[12].command_crc=2;ev[12].command_crc_expected=2;
 add(CU_SPISD_TRACE_KIND_DATA_TOKEN,3,24,40,20300);add(CU_SPISD_TRACE_KIND_DATA_END,3,24,40,25000);add(CU_SPISD_TRACE_KIND_CRC_END,3,24,40,25200);ev[15].crc_calculated=0x1234;ev[15].crc_received=0x1234;add(CU_SPISD_TRACE_KIND_BUSY_END,3,24,40,54000);
 n=debug_sd_timing_collect(out,16);assert(n==4);
 assert(out[0].transaction_id==1&&out[0].first_block&&out[0].direction==1&&out[0].command_span_cycles==500&&out[0].response_wait_cycles==100&&out[0].token_wait_cycles==300&&out[0].data_cycles==4000&&out[0].crc_cycles==200&&out[0].complete&&out[0].command_gap_avg_cycles==100&&out[0].command_crc_ok);
 assert(out[1].transaction_id==2&&out[1].sector==30&&out[1].crc_end_cycle==14200&&out[1].complete);
 assert(out[2].transaction_id==2&&out[2].sector==31&&!out[2].first_block&&out[2].token_wait_cycles==300&&out[2].data_cycles==4000&&out[2].crc_cycles==200&&out[2].complete);
 assert(out[3].transaction_id==3&&out[3].direction==2&&out[3].busy_cycles==28800&&out[3].expected_busy_min_cycles==28634&&out[3].busy_early==0&&out[3].complete&&out[3].crc_calculated==0x1234&&out[3].crc_received==0x1234);
 /* Caller capacity drops the oldest reconstructed samples, not the newest. */ n=debug_sd_timing_collect(out,2);assert(n==2&&out[0].transaction_id==2&&out[0].sector==31&&out[1].transaction_id==3);
 puts("SD timing analysis regression: PASS");return 0;}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-sdtime-analysis-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS);exe=d/'t'
  subprocess.run([cc,'-std=gnu99','-Wall','-Wextra','-DENABLE_DEBUGGER=1','-DENABLE_SD_TRACE=1','-I',str(d),'-I',str(ROOT),str(ROOT/'debug_sd_timing_analysis.c'),str(d/'h.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
