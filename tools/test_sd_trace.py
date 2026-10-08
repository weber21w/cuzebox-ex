#!/usr/bin/env python3
"""Host regression for opt-in SD protocol tracing and break triggers."""
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
static unsigned breaks;
cu_state_cpu_t* cu_avr_get_state(void){ return &cpu; }
auint cu_avr_get_video_beam_pulse(void){ return 77U; }
auint cu_avr_get_video_beam_cycle(void){ return 456U; }
void cu_avr_debug_request_break(void){ breaks++; }
void cu_vfat_reset(void){}
auint cu_vfat_read(auint addr){ return addr & 0xFFU; }
void cu_vfat_write(auint addr,auint data){ (void)addr; (void)data; }
boole cu_vfat_debug_sector_snapshot(auint sector, boole include_backing, cu_vfat_debug_sector_t* out){
 (void)include_backing; if(out==NULL)return FALSE; memset(out,0,sizeof(*out)); out->sector=sector; out->stream_valid=TRUE;
 if(sector==1U){out->role=CU_VFAT_DEBUG_ROLE_FAT1; strcpy(out->source,"VFAT FAT #1");}
 else {out->role=CU_VFAT_DEBUG_ROLE_FILE; out->owner_valid=TRUE; strcpy(out->source,"/data/GAME.UZE");}
 return TRUE;
}
static void send_cmd(auint cmd, auint arg, auint base){
 cu_spisd_send(0x40U | cmd, base + 0U);
 cu_spisd_send((arg >> 24) & 0xFFU, base + 10U);
 cu_spisd_send((arg >> 16) & 0xFFU, base + 20U);
 cu_spisd_send((arg >> 8) & 0xFFU, base + 30U);
 cu_spisd_send(arg & 0xFFU, base + 40U);
 cu_spisd_send(0xFFU, base + 50U);
}
static void make_inactive(void){
 cu_spisd_trace_enable(FALSE);
 cu_spisd_trace_break_commands_clear();
 cu_spisd_trace_break_sector_set(FALSE,0U);
 cu_spisd_trace_break_init_fail_set(FALSE);
 cu_spisd_trace_break_crc_set(FALSE);
 cu_spisd_trace_break_latency_set(FALSE,0U);
 cu_spisd_trace_break_fs_path_set("");
 cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_BOOT,FALSE); cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_FAT1,FALSE);
 cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_FAT2,FALSE); cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_ROOT,FALSE);
 cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_FILE,FALSE); cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_DIRECTORY,FALSE);
 cu_spisd_trace_break_fs_access_set(CU_SPISD_FS_ACCESS_READ|CU_SPISD_FS_ACCESS_WRITE);
}
int main(void){
 cu_state_spisd_t* s; cu_spisd_trace_event_t e; cu_spisd_model_t m;
 assert(cu_spisd_trace_built()); assert(!cu_spisd_trace_active());
 cu_spisd_reset(0U); s=cu_spisd_get_state();
 s->state=6U; s->ena=TRUE; cpu.pc=0x1234U;
 cu_spisd_trace_enable(TRUE); cu_spisd_trace_break_command_set(17U,TRUE);
 send_cmd(17U,1024U,100U); cu_spisd_send(0xFFU,160U);
 assert(breaks==1U); assert(cu_spisd_trace_count()==1U); assert(cu_spisd_trace_hit_get(FALSE,&e));
 assert(e.kind==CU_SPISD_TRACE_KIND_COMMAND && e.cmd==17U && e.sector==2U && e.pc==0x1234U);
 assert(e.transaction_id!=0U && e.row==77U && e.beam_cycle==456U);
 assert(e.start_cycle==100U && e.response_start_cycle==150U && e.command_crc==0xFFU);
 assert(e.command_byte_cycle[0]==100U && e.command_byte_cycle[1]==110U && e.command_byte_cycle[2]==120U);
 assert(e.command_byte_cycle[3]==130U && e.command_byte_cycle[4]==140U && e.command_byte_cycle[5]==150U);
 assert(e.command_byte_pc[0]==0x1234U && e.command_byte_row[5]==77U && e.command_byte_beam_cycle[5]==456U);
 assert((e.flags & CU_SPISD_TRACE_FLAG_BREAK)!=0U);
 assert(cu_spisd_trace_get(cu_spisd_trace_first_seq(),&e)); assert(e.seq==0U);

 /* A breakpoint can be armed with history completely off. */
 make_inactive(); cu_spisd_trace_clear(); cu_spisd_trace_hit_get(TRUE,&e);
 s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U; cpu.pc=0x1A2BU;
 cu_spisd_trace_break_sector_set(TRUE,3U);
 assert(cu_spisd_trace_active() && !cu_spisd_trace_enabled());
 send_cmd(24U,1536U,170U); cu_spisd_send(0xFFU,230U);
 assert(breaks==2U); assert(cu_spisd_trace_count()==0U); assert(cu_spisd_trace_hit_get(FALSE,&e));
 assert(e.kind==CU_SPISD_TRACE_KIND_COMMAND && e.cmd==24U && e.sector==3U && e.pc==0x1A2BU);
 assert(e.seq==0xFFFFFFFFU && (e.flags & CU_SPISD_TRACE_FLAG_BREAK));

 make_inactive(); cu_spisd_trace_clear(); cu_spisd_trace_hit_get(TRUE,&e);
 cu_spisd_model_preset(CU_SPISD_PRESET_NORMAL,&m); m.preset=CU_SPISD_PRESET_CUSTOM; m.cmd_wait_bytes=2U; cu_spisd_model_set(&m);
 s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U; cpu.pc=0x2222U;
 cu_spisd_trace_break_latency_set(TRUE,5U); cu_spisd_trace_enable(TRUE);
 send_cmd(58U,0U,200U); cu_spisd_send(0xFFU,260U);
 assert(breaks==3U); assert(cu_spisd_trace_hit_get(FALSE,&e)); assert(e.kind==CU_SPISD_TRACE_KIND_LATENCY);
 assert(e.cmd==58U && e.latency_cycles==10U && (e.flags & CU_SPISD_TRACE_FLAG_LATENCY));

 make_inactive(); cu_spisd_trace_hit_get(TRUE,&e); s->state=1U; s->ena=FALSE; s->cmd=0U; s->pstat=0U; s->recvc=300U; cpu.pc=0x3333U;
 cu_spisd_trace_break_init_fail_set(TRUE); cu_spisd_trace_enable(TRUE);
 cu_spisd_send(0x00U,400U);
 assert(breaks==4U); assert(cu_spisd_trace_hit_get(FALSE,&e)); assert(e.kind==CU_SPISD_TRACE_KIND_INIT_FAIL);
 assert((e.flags & CU_SPISD_TRACE_FLAG_INIT_FAIL)!=0U && e.pc==0x3333U);

 /* History reconstructs one block transaction from protocol boundaries, not
 ** 512 byte-level trace entries. */
 make_inactive(); cu_spisd_trace_clear(); cu_spisd_model_preset(CU_SPISD_PRESET_NORMAL,&m); cu_spisd_model_set(&m);
 s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U; s->crc=FALSE; cpu.pc=0x4444U; cu_spisd_trace_enable(TRUE);
 send_cmd(17U,2048U,500U); cu_spisd_send(0xFFU,560U);
 { auint i; cu_spisd_send(0xFFU,570U); cu_spisd_send(0xFFU,580U); cu_spisd_send(0xFFU,590U); for(i=0U;i<512U;i++)cu_spisd_send(0xFFU,600U+i); cu_spisd_send(0xFFU,1112U); cu_spisd_send(0xFFU,1113U); }
 assert(cu_spisd_trace_count()==4U);
 { uint32 q=cu_spisd_trace_first_seq(),tx=0U; assert(cu_spisd_trace_get(q++,&e)); assert(e.kind==CU_SPISD_TRACE_KIND_COMMAND); tx=e.transaction_id; assert(tx!=0U && e.command_crc==0xFFU && e.command_crc_expected!=0U); assert(cu_spisd_trace_get(q++,&e)); assert(e.kind==CU_SPISD_TRACE_KIND_DATA_TOKEN && e.transaction_id==tx && e.value==0xFEU); assert(cu_spisd_trace_get(q++,&e)); assert(e.kind==CU_SPISD_TRACE_KIND_DATA_END && e.transaction_id==tx); assert(cu_spisd_trace_get(q++,&e)); assert(e.kind==CU_SPISD_TRACE_KIND_CRC_END && e.transaction_id==tx && e.sector==4U); }

 /* CMD18 keeps one transaction ID across a completed block and the following
 ** sector boundary. CRC_END belongs to the old sector; BLOCK to the new one. */
 cu_spisd_trace_clear(); s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U; s->crc=FALSE;
 send_cmd(18U,4096U,1200U); cu_spisd_send(0xFFU,1260U);
 { auint i; cu_spisd_send(0xFFU,1270U); cu_spisd_send(0xFFU,1280U); cu_spisd_send(0xFFU,1290U); for(i=0U;i<512U;i++)cu_spisd_send(0xFFU,1300U+i); cu_spisd_send(0xFFU,1812U); cu_spisd_send(0xFFU,1813U); }
 assert(cu_spisd_trace_count()==5U);
 { uint32 q=cu_spisd_trace_first_seq(),tx=0U; auint saw_crc=0U,saw_block=0U; assert(cu_spisd_trace_get(q++,&e)); tx=e.transaction_id; while(cu_spisd_trace_get(q++,&e)){ assert(e.transaction_id==tx); if(e.kind==CU_SPISD_TRACE_KIND_CRC_END){assert(e.sector==8U);saw_crc++;} if(e.kind==CU_SPISD_TRACE_KIND_BLOCK){assert(e.sector==9U);saw_block++;} } assert(saw_crc==1U && saw_block==1U); }
 /* Filesystem-aware breakpoints resolve only at data command / sector boundaries. */
 make_inactive(); cu_spisd_trace_hit_get(TRUE,&e); s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U;
 cu_spisd_trace_break_fs_role_set(CU_VFAT_DEBUG_ROLE_FILE,TRUE); cu_spisd_trace_break_fs_access_set(CU_SPISD_FS_ACCESS_READ);
 send_cmd(17U,1024U,2000U); cu_spisd_send(0xFFU,2060U); assert(breaks==5U); assert(cu_spisd_trace_hit_get(TRUE,&e)); assert(e.sector==2U);
 make_inactive(); s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U; cu_spisd_trace_break_fs_path_set("game.uze"); cu_spisd_trace_break_fs_access_set(CU_SPISD_FS_ACCESS_WRITE);
 send_cmd(24U,1024U,2100U); cu_spisd_send(0xFFU,2160U); assert(breaks==6U); assert(cu_spisd_trace_hit_get(TRUE,&e));
 puts("SD protocol trace regression: PASS"); return 0;
}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-sdtrace-') as td:
  d=pathlib.Path(td); (d/'SDL2').mkdir(); (d/'SDL2'/'SDL.h').write_text(SDL); (d/'h.c').write_text(HARNESS)
  exe=d/'t'
  subprocess.run([cc,'-std=gnu99','-Wall','-Wextra','-DENABLE_DEBUGGER=1','-DENABLE_SD_TRACE=1','-I',str(d),'-I',str(ROOT),str(ROOT/'cu_spisd.c'),str(d/'h.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
