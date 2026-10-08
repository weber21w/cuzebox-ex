#!/usr/bin/env python3
"""Regression for on-demand filesystem-operation reconstruction from SD trace history."""
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "debug_sd_fs_history.h"
#include "cu_spisd.h"

static cu_spisd_trace_event_t ev[16];
static auint evn;
static void add(auint kind,auint cmd,auint sector,auint tx,auint seq,auint cycle,auint r1){
 cu_spisd_trace_event_t*e=&ev[evn++];memset(e,0,sizeof(*e));e->kind=kind;e->cmd=cmd;e->sector=sector;e->transaction_id=tx;e->seq=seq;e->cycle=cycle;e->pc=0x100U+seq;e->row=20U+seq;e->beam_cycle=300U+seq;e->r1=r1;
}
boole cu_spisd_trace_built(void){return TRUE;}
auint cu_spisd_trace_count(void){return evn;}
uint32 cu_spisd_trace_first_seq(void){return 0U;}
boole cu_spisd_trace_get(uint32 seq,cu_spisd_trace_event_t*out){if(seq>=evn||!out)return FALSE;*out=ev[seq];return TRUE;}
boole cu_vfat_debug_sector_snapshot(auint sector,boole backing,cu_vfat_debug_sector_t*out){
 (void)backing;if(!out)return FALSE;memset(out,0,sizeof(*out));out->sector=sector;out->next_cluster=0xffffU;
 if(sector==513U){out->role=CU_VFAT_DEBUG_ROLE_ROOT;strcpy(out->source,"VFAT root directory");return TRUE;}
 if(sector==1U){out->role=CU_VFAT_DEBUG_ROLE_FAT1;strcpy(out->source,"VFAT FAT #1");return TRUE;}
 out->role=CU_VFAT_DEBUG_ROLE_FILE;out->owner_valid=TRUE;
 if(sector==600U){out->file_offset=0U;out->cluster=2U;out->chain_index=0U;strcpy(out->source,"GAME.UZE");return TRUE;}
 if(sector==700U){out->file_offset=512U;out->cluster=5U;out->chain_index=1U;strcpy(out->source,"GAME.UZE");return TRUE;}
 if(sector==701U){out->file_offset=1024U;out->cluster=5U;out->chain_index=1U;strcpy(out->source,"GAME.UZE");return TRUE;}
 out->file_offset=0U;out->cluster=9U;strcpy(out->source,"OTHER.BIN");return TRUE;
}
int main(void){
 cu_sd_fsop_t o[8];auint total=0,n;
 add(CU_SPISD_TRACE_KIND_COMMAND,17,513,1,0,100,0); add(CU_SPISD_TRACE_KIND_CRC_END,17,513,1,1,200,0);
 add(CU_SPISD_TRACE_KIND_COMMAND,17,1,2,2,300,0); add(CU_SPISD_TRACE_KIND_CRC_END,17,1,2,3,400,0);
 add(CU_SPISD_TRACE_KIND_COMMAND,17,600,3,4,500,0); add(CU_SPISD_TRACE_KIND_CRC_END,17,600,3,5,600,0);
 add(CU_SPISD_TRACE_KIND_COMMAND,17,700,4,6,700,0); add(CU_SPISD_TRACE_KIND_CRC_END,17,700,4,7,800,0);
 add(CU_SPISD_TRACE_KIND_COMMAND,24,701,5,8,900,0); add(CU_SPISD_TRACE_KIND_CRC_END,24,701,5,9,1000,0);
 add(CU_SPISD_TRACE_KIND_COMMAND,17,702,6,10,1100,4);
 n=cu_sd_fs_history_build(o,8,&total);assert(cu_sd_fs_history_built());assert(n==5U&&total==5U);
 assert(o[0].role==CU_VFAT_DEBUG_ROLE_ROOT&&o[0].access==CU_SD_FSOP_ACCESS_READ&&o[0].bytes_completed==512U);
 assert(o[1].role==CU_VFAT_DEBUG_ROLE_FAT1&&o[1].sectors_completed==1U);
 assert(o[2].role==CU_VFAT_DEBUG_ROLE_FILE&&o[2].access==CU_SD_FSOP_ACCESS_READ&&strcmp(o[2].source,"GAME.UZE")==0);
 assert(o[2].sectors_started==2U&&o[2].sectors_completed==2U&&o[2].bytes_completed==1024U);
 assert(o[2].file_offset_start==0U&&o[2].file_offset_end==1024U&&o[2].cluster_transitions==1U&&o[2].physical_runs==2U);
 assert(o[2].first_cluster==2U&&o[2].last_cluster==5U&&o[2].first_transaction_id==3U&&o[2].last_transaction_id==4U);
 assert(o[3].access==CU_SD_FSOP_ACCESS_WRITE&&o[3].file_offset_start==1024U&&o[3].bytes_completed==512U);
 assert(o[4].r1==4U&&(o[4].flags&CU_SD_FSOP_FLAG_R1_ERROR)&&(o[4].flags&CU_SD_FSOP_FLAG_INCOMPLETE)&&o[4].bytes_completed==0U);
 /* Tail retention returns the newest operations in chronological order. */
 memset(o,0,sizeof(o));n=cu_sd_fs_history_build(o,2,&total);assert(n==2U&&total==5U);assert(o[0].access==CU_SD_FSOP_ACCESS_WRITE);assert(strcmp(o[1].source,"OTHER.BIN")==0);
 puts("SD filesystem operation history regression: PASS");return 0;
}
'''
def main():
 cc=shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('no host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-sd-fs-history-') as td:
  d=Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text('#ifndef SDL_H\n#define SDL_H\n#include <stdint.h>\ntypedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;\n#endif\n');(d/'h.c').write_text(HARNESS);exe=d/'t'
  subprocess.run([cc,'-std=gnu99','-Wall','-Wextra','-DENABLE_DEBUGGER=1','-DENABLE_SD_TRACE=1','-I',str(d),'-I',str(ROOT),str(ROOT/'debug_sd_fs_history.c'),str(d/'h.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
