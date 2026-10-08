#!/usr/bin/env python3
"""Host regression for deterministic opt-in SD fault injection."""
from __future__ import annotations
import os, pathlib, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL='''#ifndef SDL_H\n#define SDL_H\n#include <stdint.h>\ntypedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;\n#endif\n'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cu_spisd.h"
#include "cu_avr.h"
#include "cu_vfat.h"
static cu_state_cpu_t cpu;
cu_state_cpu_t* cu_avr_get_state(void){ return &cpu; }
auint cu_avr_get_video_beam_pulse(void){ return 0U; }
auint cu_avr_get_video_beam_cycle(void){ return 0U; }
void cu_avr_debug_request_break(void){}
void cu_vfat_reset(void){}
auint cu_vfat_read(auint addr){ return addr & 0xFFU; }
void cu_vfat_write(auint addr,auint data){ (void)addr; (void)data; }
#ifdef ENABLE_SD_TRACE
boole cu_vfat_debug_sector_snapshot(auint sector, boole backing_compare, cu_vfat_debug_sector_t* out){
 (void)backing_compare; if(out==NULL)return FALSE; memset(out,0,sizeof(*out)); out->sector=sector; out->role=CU_VFAT_DEBUG_ROLE_FILE; strcpy(out->source,"/data/FAULT.BIN"); return TRUE;
}
#endif
static void send_cmd_packet(auint cmd, auint arg, auint base){
 cu_spisd_send(0x40U|cmd,base); cu_spisd_send((arg>>24)&255U,base+10U); cu_spisd_send((arg>>16)&255U,base+20U);
 cu_spisd_send((arg>>8)&255U,base+30U); cu_spisd_send(arg&255U,base+40U); cu_spisd_send(0xFFU,base+50U);
}
static void avail(cu_state_spisd_t* s){ s->state=6U; s->ena=TRUE; s->cmd=0U; s->pstat=0U; s->crc=FALSE; s->r1=0U; s->data=0xFFU; }
static void arm(auint slot,auint kind,auint value,boole persist,auint cmd,boole cmdany,auint sec,boole secany){
 cu_spisd_fault_rule_t r; memset(&r,0,sizeof(r)); r.enabled=TRUE;r.persistent=persist;r.kind=kind;r.value=value;r.command=cmd;r.command_any=cmdany;r.sector=sec;r.sector_any=secany; assert(cu_spisd_fault_set(slot,&r));
}
int main(void){
 cu_state_spisd_t* s; cu_spisd_fault_rule_t r; cu_spisd_model_t m; auint i;
 assert(cu_spisd_fault_built()); assert(!cu_spisd_fault_active()); cu_spisd_reset(0U); s=cu_spisd_get_state();
 cu_spisd_model_preset(CU_SPISD_PRESET_NORMAL,&m); cu_spisd_model_set(&m);
 /* One-shot R1 delay applies only to CMD17 sector 2, then disarms. */
 avail(s); arm(0,CU_SPISD_FAULT_R1_DELAY,2U,FALSE,17U,FALSE,2U,FALSE); send_cmd_packet(17U,1024U,100U);
 cu_spisd_send(0xFFU,160U); assert(s->state==6U); cu_spisd_send(0xFFU,170U); assert(s->state==6U); cu_spisd_send(0xFFU,180U); assert(s->state==7U);
 assert(cu_spisd_fault_get(0,&r)&&!r.enabled&&r.fired==1U&&r.last_cycle==180U);
 /* Read token delay extends the configured wait without changing the model. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); avail(s); arm(1,CU_SPISD_FAULT_TOKEN_DELAY,3U,FALSE,17U,FALSE,2U,FALSE); send_cmd_packet(17U,1024U,300U); cu_spisd_send(0xFFU,360U);
 for(i=0U;i<5U;i++){cu_spisd_send(0xFFU,370U+i); assert(s->pstat==1U);} cu_spisd_send(0xFFU,380U); assert(s->pstat==2U); assert(cu_spisd_fault_get(1,&r)&&r.fired==1U&&!r.enabled);
 /* Reject can target a sector and prevents the normal data-state transition. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); avail(s); arm(2,CU_SPISD_FAULT_REJECT,0x20U,FALSE,17U,FALSE,3U,FALSE); send_cmd_packet(17U,1536U,500U); cu_spisd_send(0xFFU,560U); assert(s->state==6U); assert((s->r1&0x20U)!=0U);
 /* Forced command CRC is independent of the card's CRC-enable bit. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); avail(s); arm(3,CU_SPISD_FAULT_CMD_CRC,1U,FALSE,58U,FALSE,0U,TRUE); send_cmd_packet(58U,0U,700U); cu_spisd_send(0xFFU,760U); assert((s->r1&0x08U)!=0U);
 /* R1 bits can inject any legal error mask. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); avail(s); arm(4,CU_SPISD_FAULT_R1_BITS,0x40U,FALSE,16U,FALSE,0U,TRUE); send_cmd_packet(16U,512U,800U); cu_spisd_send(0xFFU,860U); assert((s->r1&0x40U)!=0U);
 /* Read CRC corruption changes the wire CRC only; calculated CRC remains intact. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); avail(s); arm(5,CU_SPISD_FAULT_DATA_CRC,1U,FALSE,17U,FALSE,4U,FALSE); send_cmd_packet(17U,2048U,900U); cu_spisd_send(0xFFU,960U); cu_spisd_send(0xFFU,970U);cu_spisd_send(0xFFU,971U);cu_spisd_send(0xFFU,972U); for(i=0;i<512U;i++)cu_spisd_send(0xFFU,1000U+i); {auint crc=s->cc16v; cu_spisd_send(0xFFU,1600U); assert((s->data&255U)!=((crc>>8)&255U));}
 /* Busy extension adds milliseconds only for the matched write sector. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); avail(s); s->state=9U;s->pstat=6U;s->ppos=1U;s->paddr=5U;s->cc16v=0;s->cc16c=0; arm(6,CU_SPISD_FAULT_BUSY_EXTEND,7U,FALSE,24U,FALSE,5U,FALSE); cu_spisd_send(0U,2000U); assert(s->pstat==10U); assert(s->next==(auint)(2000U+107U*28634U));
 /* Persistent rules stay armed and count repeated matches. */
 cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); arm(7,CU_SPISD_FAULT_REJECT,2U,TRUE,17U,FALSE,0U,TRUE); avail(s); send_cmd_packet(17U,0U,3000U);cu_spisd_send(0xFFU,3060U); avail(s);send_cmd_packet(17U,512U,3100U);cu_spisd_send(0xFFU,3160U); assert(cu_spisd_fault_get(7,&r)&&r.enabled&&r.fired==2U);
#ifdef ENABLE_SD_TRACE
 /* With history armed, an injected rule creates a sparse FAULT annotation. */
 { cu_spisd_trace_event_t e; uint32 q; boole saw=FALSE;
   cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); cu_spisd_trace_clear(); cu_spisd_trace_enable(TRUE); avail(s);
   arm(0,CU_SPISD_FAULT_REJECT,2U,FALSE,17U,FALSE,6U,FALSE); send_cmd_packet(17U,3072U,3300U); cu_spisd_send(0xFFU,3360U);
   q=cu_spisd_trace_first_seq(); while(cu_spisd_trace_get(q++,&e)){ if((e.kind==CU_SPISD_TRACE_KIND_FAULT)&&((e.flags&CU_SPISD_TRACE_FLAG_FAULT)!=0U)){saw=TRUE;break;} } assert(saw);
 }
#endif
 puts("SD fault injection regression: PASS"); return 0;
}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-sdfault-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS)
  for name,extra in (('fault-only',[]),('fault-trace',['-DENABLE_SD_TRACE=1'])):
   exe=d/name
   subprocess.run([cc,'-std=gnu99','-Wall','-Wextra','-DENABLE_DEBUGGER=1','-DENABLE_SD_FAULT=1',*extra,'-I',str(d),'-I',str(ROOT),str(ROOT/'cu_spisd.c'),str(d/'h.c'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
