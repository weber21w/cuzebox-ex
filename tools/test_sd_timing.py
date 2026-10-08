#!/usr/bin/env python3
"""Host regression for the runtime CUzeBox SD timing model."""
from __future__ import annotations
import os, pathlib, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL='''#ifndef SDL_H
#define SDL_H
#include <stdint.h>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;
#endif
'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include "cu_spisd.h"
void cu_vfat_reset(void){}
auint cu_vfat_read(auint addr){return addr&0xFFU;}
void cu_vfat_write(auint addr,auint data){(void)addr;(void)data;}
int main(void){
 cu_spisd_model_t m; cu_state_spisd_t* s;
 cu_spisd_model_preset(CU_SPISD_PRESET_NORMAL,&m);
 assert(m.preset==CU_SPISD_PRESET_NORMAL && m.init_ms==500U && m.cmd_wait_bytes==0U && m.read_wait_bytes==2U && m.write_busy_ms==100U && m.cs_high_ms==0U && m.init_min_byte_cycles==16U && m.init_max_byte_cycles==2296U);
 cu_spisd_model_preset(CU_SPISD_PRESET_SLOW,&m); assert(m.init_ms==1000U && m.cmd_wait_bytes==4U && m.read_wait_bytes==8U && m.write_busy_ms==250U && m.cs_high_ms==2U);
 cu_spisd_model_preset(CU_SPISD_PRESET_FAST,&m); assert(m.init_ms==50U && m.cmd_wait_bytes==0U && m.read_wait_bytes==0U && m.write_busy_ms==10U && m.cs_high_ms==0U);
 cu_spisd_model_preset(CU_SPISD_PRESET_NORMAL,&m); m.preset=CU_SPISD_PRESET_CUSTOM; m.read_wait_bytes=5U; cu_spisd_model_set(&m); cu_spisd_reset(0U);
 s=cu_spisd_get_state(); s->state=8U; s->pstat=1U; s->ppos=0U; s->data=0xFFU;
 cu_spisd_send(0xFFU,100U); assert(s->pstat==1U && s->ppos==1U);
 cu_spisd_send(0xFFU,200U); assert(s->pstat==1U && s->ppos==2U);
 cu_spisd_send(0xFFU,300U); assert(s->pstat==1U && s->ppos==3U);
 cu_spisd_send(0xFFU,400U); assert(s->pstat==1U && s->ppos==4U);
 cu_spisd_send(0xFFU,500U); assert(s->pstat==1U && s->ppos==5U);
 cu_spisd_send(0xFFU,600U); assert(s->pstat==2U && s->data==0xFEU);
 puts("SD timing model regression: PASS"); return 0;
}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-sd-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS)
  exe=d/'sdtest'; subprocess.run([cc,'-std=gnu99','-I',str(d),'-I',str(ROOT),str(ROOT/'cu_spisd.c'),str(d/'h.c'),'-o',str(exe)],check=True); subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
