#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <stdio.h>
#include <string.h>
#include "cu_vfat.h"
#include "filesys.h"
#define ROOT_BASE 0x040200U
#define ROOT_SECTOR (ROOT_BASE >> 9)
#define DATA_SECTOR (CU_VFAT_SYS_SIZE >> 9)
static auint root_slot(cu_state_vfat_t* st,char const* name){auint i;for(i=0;i<CU_VFAT_ROOT_SIZE;i++)if(strcmp(st->hnames[i],name)==0)return i;return 0xFFFFFFFFU;}
static auint entry_cluster(cu_state_vfat_t* st,auint slot){uint8*e=&st->sys[ROOT_BASE+slot*32U];return (auint)e[26]|((auint)e[27]<<8);}
int main(int argc,char**argv){
 cu_state_vfat_t*st;cu_vfat_debug_sector_t d;auint slot,cl,sec,pos,i;char path[2048];
 if(argc<2)return 2;snprintf(path,sizeof(path),"%s/",argv[1]);filesys_setpath(path,NULL,0);cu_vfat_reset();st=cu_vfat_get_state();
 if(!cu_vfat_debug_sector_snapshot(0U,FALSE,&d)||d.role!=CU_VFAT_DEBUG_ROLE_BOOT||!d.stream_valid||d.stream[510]!=0x55U||d.stream[511]!=0xAAU)return 3;
 if(!cu_vfat_debug_sector_snapshot(1U,FALSE,&d)||d.role!=CU_VFAT_DEBUG_ROLE_FAT1||d.fat_copy!=1U||d.fat_first_cluster!=0U)return 4;
 if(!cu_vfat_debug_sector_snapshot(0x101U,FALSE,&d)||d.role!=CU_VFAT_DEBUG_ROLE_FAT2||d.fat_copy!=2U||d.fat_first_cluster!=0U)return 5;
 if(!cu_vfat_debug_sector_snapshot(ROOT_SECTOR,FALSE,&d)||d.role!=CU_VFAT_DEBUG_ROLE_ROOT||!d.stream_valid)return 6;
 slot=root_slot(st,"TEST.BIN");if(slot==0xFFFFFFFFU)return 7;cl=entry_cluster(st,slot);if(cl<2U)return 8;sec=DATA_SECTOR+(cl-2U)*64U;pos=sec<<9;
 if(cu_vfat_isaccessed())return 9;
 if(!cu_vfat_debug_sector_snapshot(sec,TRUE,&d)||!d.backing_valid||d.stream_valid)return 10;
 if(d.role!=CU_VFAT_DEBUG_ROLE_FILE||!d.owner_valid||d.owner_is_dir||d.cluster!=cl||d.sector_in_cluster!=0U||d.start_cluster!=cl||d.entry_sector!=ROOT_SECTOR+(slot>>4)||d.entry_index!=(slot&15U)||strcmp(d.source,"TEST.BIN"))return 11;
 for(i=0;i<128U;i++)if(d.backing[i]!=(uint8)i)return 12;if(cu_vfat_isaccessed())return 13;
 if(cu_vfat_read(pos)!=0U)return 14;if(!cu_vfat_debug_sector_snapshot(sec,FALSE,&d)||d.stream_valid||d.backing_valid)return 15;if(cu_vfat_get_state()->rwbuf[66]!=66U)return 16;
 cu_vfat_write(pos,0xAAU);if(!cu_vfat_debug_sector_snapshot(sec,TRUE,&d)||d.stream_valid||!d.backing_valid)return 17;if(cu_vfat_get_state()->rwbuf[0]!=0xAAU||d.backing[0]!=0U)return 18;
 slot=root_slot(st,"SUB");if(slot==0xFFFFFFFFU)return 19;cl=entry_cluster(st,slot);sec=DATA_SECTOR+(cl-2U)*64U;
 if(!cu_vfat_debug_sector_snapshot(sec,FALSE,&d)||d.role!=CU_VFAT_DEBUG_ROLE_DIRECTORY||!d.owner_valid||!d.owner_is_dir||!d.stream_valid||d.cluster!=cl||strcmp(d.source,"SUB"))return 20;
 if(d.stream[0]!='.'||d.stream[32]!='.')return 21;
 puts("SD payload/filesystem snapshot regression: PASS");return 0;
}
'''
def main():
    cc=shutil.which('cc') or shutil.which('gcc')
    if not cc: raise SystemExit('no host C compiler')
    with tempfile.TemporaryDirectory(prefix='cuzebox-sd-payload-') as td:
        d=Path(td); (d/'SDL2').mkdir(); (d/'SDL2'/'SDL.h').write_text('#ifndef SDL_H\n#define SDL_H\n#endif\n')
        card=d/'card'; card.mkdir(); (card/'TEST.BIN').write_bytes(bytes(range(256))*2); (card/'SUB').mkdir(); (card/'SUB'/'INNER.BIN').write_bytes(b'inner')
        h=d/'harness.c'; h.write_text(HARNESS); exe=d/'test'
        subprocess.run([cc,'-std=gnu99','-DENABLE_DEBUGGER','-DHEADLESS=1','-DFLAG_NOCONSOLE=1','-I',str(d),'-I',str(ROOT),str(ROOT/'filesys.c'),str(ROOT/'cu_vfat.c'),str(h),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(card)],check=True)
    return 0
if __name__=='__main__': raise SystemExit(main())
