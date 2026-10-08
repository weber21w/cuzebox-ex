#!/usr/bin/env python3
"""Host regression for deterministic SD byte/CS/reset recording and strict replay."""
from __future__ import annotations
import os, pathlib, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL='''#ifndef SDL_H\n#define SDL_H\n#include <stdint.h>\ntypedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;\n#endif\n'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cu_spisd.h"
#include "cu_vfat.h"
#include "cu_avr.h"
static cu_state_cpu_t cpu;
static unsigned breaks,reads,writes,resets;
cu_state_cpu_t* cu_avr_get_state(void){return &cpu;}
auint cu_avr_get_video_beam_pulse(void){return 0U;}
auint cu_avr_get_video_beam_cycle(void){return 0U;}
void cu_avr_debug_request_break(void){breaks++;}
void cu_vfat_reset(void){resets++;}
auint cu_vfat_read(auint addr){reads++;return (addr*7U+3U)&0xFFU;}
void cu_vfat_write(auint addr,auint data){writes++;(void)addr;(void)data;}
boole cu_vfat_debug_sector_snapshot(auint sector,boole backing,cu_vfat_debug_sector_t*out){(void)sector;(void)backing;(void)out;return FALSE;}
static auint xfer(auint tx,auint start,auint end){auint rx=cu_spisd_recv(start);cu_spisd_send(tx,end);return rx;}
static void send_cmd17(auint base){
 xfer(0x51U,base+0U,base+8U);xfer(0,base+16U,base+24U);xfer(0,base+32U,base+40U);
 xfer(0,base+48U,base+56U);xfer(0,base+64U,base+72U);xfer(0xFFU,base+80U,base+88U);
 xfer(0xFFU,base+96U,base+104U);xfer(0xFFU,base+112U,base+120U);xfer(0xFFU,base+128U,base+136U);
 xfer(0xFFU,base+144U,base+152U);xfer(0xFFU,base+160U,base+168U);xfer(0xFFU,base+176U,base+184U);
}
int main(int argc,char**argv){
 cu_state_spisd_t*s;cu_spisd_replay_status_t st;cu_spisd_replay_event_t e;uint32 i;unsigned before_reads,before_writes,before_resets;auint replay_base=5000U;
 assert(argc==2);assert(cu_spisd_replay_built());cu_spisd_reset(0U);s=cu_spisd_get_state();
 s->state=6U;s->ena=FALSE;s->cmd=0U;s->pstat=0U;s->crc=FALSE;s->data=0xFFU;s->recvc=1000U;
 assert(cu_spisd_replay_record_start(1000U));cu_spisd_cs_set(TRUE,1010U);send_cmd17(1020U);cu_spisd_cs_set(FALSE,1220U);cu_spisd_replay_stop();
 cu_spisd_replay_status(&st);assert(st.mode==CU_SPISD_REPLAY_MODE_OFF);assert(st.count==26U);assert(!st.overflow);
 assert(cu_spisd_replay_get(0U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_CS&&e.cs_enabled==1U&&e.cycle_delta==10U);
 assert(cu_spisd_replay_get(1U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_BYTE_START&&e.miso==0xFFU&&e.recv_cycle_delta==20U);
 assert(cu_spisd_replay_get(2U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_BYTE_END&&e.mosi==0x51U&&e.send_cycle_delta==28U);
 assert(cu_spisd_replay_get(st.count-1U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_CS&&e.cs_enabled==0U);
 assert(cu_spisd_replay_save(argv[1]));cu_spisd_replay_clear();cu_spisd_replay_status(&st);assert(st.count==0U);assert(cu_spisd_replay_load(argv[1]));
 cu_spisd_replay_status(&st);assert(st.count==26U&&st.mode==CU_SPISD_REPLAY_MODE_OFF);before_reads=reads;before_writes=writes;before_resets=resets;breaks=0U;
 assert(cu_spisd_replay_start(replay_base,0U));
 for(i=0U;i<st.count;i++){
  assert(cu_spisd_replay_get(i,&e));
  if(e.kind==CU_SPISD_REPLAY_EVENT_CS){cu_spisd_cs_set(e.cs_enabled?TRUE:FALSE,replay_base+e.cycle_delta);}
  else if(e.kind==CU_SPISD_REPLAY_EVENT_RESET){cu_spisd_reset(replay_base+e.cycle_delta);}
  else if(e.kind==CU_SPISD_REPLAY_EVENT_BYTE_START){auint rx=cu_spisd_recv(replay_base+e.recv_cycle_delta);assert(rx==e.miso);}
  else if(e.kind==CU_SPISD_REPLAY_EVENT_BYTE_END){cu_spisd_send(e.mosi,replay_base+e.send_cycle_delta);}
  else assert(0);
 }
 cu_spisd_replay_status(&st);assert(st.mode==CU_SPISD_REPLAY_MODE_COMPLETE&&st.cursor==st.count&&st.error==CU_SPISD_REPLAY_ERR_NONE);assert(breaks==1U);
 assert(reads==before_reads&&writes==before_writes&&resets==before_resets); /* replay bypasses VFAT */
 /* Exact mismatch stops on the delivering SD byte and requests a debugger break. */
 breaks=0U;assert(cu_spisd_replay_start(9000U,0U));assert(cu_spisd_replay_get(0U,&e));cu_spisd_cs_set(TRUE,9000U+e.cycle_delta);assert(cu_spisd_replay_get(1U,&e));(void)cu_spisd_recv(9000U+e.recv_cycle_delta);assert(cu_spisd_replay_get(2U,&e));cu_spisd_send(e.mosi^1U,9000U+e.send_cycle_delta);
 cu_spisd_replay_status(&st);assert(st.mode==CU_SPISD_REPLAY_MODE_ERROR&&st.error==CU_SPISD_REPLAY_ERR_MOSI&&st.mismatch_index==2U&&breaks==1U);
 /* Timing tolerance can deliberately accept a small deterministic scheduling offset. */
 assert(cu_spisd_replay_start(12000U,2U));assert(cu_spisd_replay_get(0U,&e));cu_spisd_cs_set(TRUE,12000U+e.cycle_delta+2U);assert(cu_spisd_replay_get(1U,&e));assert(cu_spisd_recv(12000U+e.recv_cycle_delta+2U)==e.miso);assert(cu_spisd_replay_get(2U,&e));cu_spisd_send(e.mosi,12000U+e.send_cycle_delta+2U);cu_spisd_replay_status(&st);assert(st.mode==CU_SPISD_REPLAY_MODE_REPLAY&&st.cursor==3U);
 /* CS may legally change between the MISO sample and MOSI delivery. The split
 ** BYTE_START/BYTE_END format must preserve that exact ordering. */
 cu_spisd_replay_clear();s->state=6U;s->ena=TRUE;s->data=0xA5U;s->recvc=20000U;assert(cu_spisd_replay_record_start(20000U));
 assert(cu_spisd_recv(20010U)==0xA5U);cu_spisd_cs_set(FALSE,20014U);cu_spisd_send(0x3CU,20018U);cu_spisd_replay_stop();cu_spisd_replay_status(&st);assert(st.count==3U);
 assert(cu_spisd_replay_get(0U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_BYTE_START&&e.recv_cycle_delta==10U&&e.miso==0xA5U);
 assert(cu_spisd_replay_get(1U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_CS&&e.cycle_delta==14U&&!e.cs_enabled);
 assert(cu_spisd_replay_get(2U,&e)&&e.kind==CU_SPISD_REPLAY_EVENT_BYTE_END&&e.send_cycle_delta==18U&&e.mosi==0x3CU);
 breaks=0U;assert(cu_spisd_replay_start(30000U,0U));assert(cu_spisd_recv(30010U)==0xA5U);cu_spisd_cs_set(FALSE,30014U);cu_spisd_send(0x3CU,30018U);cu_spisd_replay_status(&st);assert(st.mode==CU_SPISD_REPLAY_MODE_COMPLETE&&st.cursor==3U&&st.error==CU_SPISD_REPLAY_ERR_NONE&&breaks==1U);
 puts("SD deterministic record/replay regression: PASS");return 0;
}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-sdreplay-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS);exe=d/'t';rec=d/'capture.sdr'
  subprocess.run([cc,'-std=gnu99','-Wall','-Wextra','-DENABLE_DEBUGGER=1','-DENABLE_SD_REPLAY=1','-I',str(d),'-I',str(ROOT),str(ROOT/'cu_spisd.c'),str(d/'h.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(rec)],check=True)
  assert rec.stat().st_size>100
 return 0
if __name__=='__main__': raise SystemExit(main())
