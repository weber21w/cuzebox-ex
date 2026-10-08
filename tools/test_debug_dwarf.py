#!/usr/bin/env python3
"""Host regression for CUzeBox AVR DWARF variable/type evaluation."""
from __future__ import annotations
import os, pathlib, shutil, struct, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent

def uleb(v:int)->bytes:
    o=bytearray()
    while True:
        b=v&0x7f; v >>= 7
        if v:b|=0x80
        o.append(b)
        if not v:return bytes(o)

def sleb(v:int)->bytes:
    o=bytearray(); more=True
    while more:
        b=v&0x7f; sign=b&0x40; v >>= 7
        if (v==0 and not sign) or (v==-1 and sign): more=False
        else:b|=0x80
        o.append(b)
    return bytes(o)

def abbrev()->bytes:
    A=[]
    def e(code,tag,children,attrs):
        b=uleb(code)+uleb(tag)+bytes([children])
        for a,f in attrs:b+=uleb(a)+uleb(f)
        return b+b'\0\0'
    # attrs/forms constants inline
    A.append(e(1,0x11,1,[(0x03,0x08),(0x11,0x01),(0x12,0x01)])) # CU
    A.append(e(2,0x24,0,[(0x03,0x08),(0x0b,0x0b),(0x3e,0x0b)])) # base
    A.append(e(3,0x34,0,[(0x03,0x08),(0x49,0x13),(0x02,0x0a),(0x3b,0x0b)])) # var
    A.append(e(4,0x2e,1,[(0x03,0x08),(0x11,0x01),(0x12,0x01),(0x40,0x0a)])) # fn
    A.append(e(5,0x05,0,[(0x03,0x08),(0x49,0x13),(0x02,0x0a)])) # param
    A.append(e(6,0x34,0,[(0x03,0x08),(0x49,0x13),(0x02,0x0a)])) # local
    A.append(e(7,0x13,1,[(0x03,0x08),(0x0b,0x0b)])) # struct
    A.append(e(8,0x0d,0,[(0x03,0x08),(0x49,0x13),(0x38,0x0b)])) # member
    A.append(e(9,0x0f,0,[(0x49,0x13),(0x0b,0x0b)])) # pointer
    A.append(e(10,0x01,1,[(0x49,0x13)])) # array
    A.append(e(11,0x21,0,[(0x2f,0x0b)])) # subrange upper_bound
    A.append(e(12,0x04,1,[(0x03,0x08),(0x0b,0x0b)])) # enum
    A.append(e(13,0x28,0,[(0x03,0x08),(0x1c,0x0b)])) # enumerator
    return b''.join(A)+b'\0'

def make_info()->bytes:
    body=bytearray(struct.pack('<H',2)+struct.pack('<I',0)+b'\x04')
    # offsets are from CU start including 4-byte length. first DIE absolute offset 11.
    def die(code,payload=b''):
        off=4+len(body); body.extend(uleb(code)+payload); return off
    cu=die(1,b'fixture.c\0'+struct.pack('<I',0x10)+struct.pack('<I',0x30))
    u8=die(2,b'uint8_t\0'+b'\x01'+b'\x08')
    # global counter @ data address 0x120
    die(3,b'counter\0'+struct.pack('<I',u8)+bytes([5,0x03])+struct.pack('<I',0x800120)+b'\x03')
    point=die(7,b'Point\0'+b'\x02')
    die(8,b'x\0'+struct.pack('<I',u8)+b'\x00')
    die(8,b'y\0'+struct.pack('<I',u8)+b'\x01')
    body+=b'\0' # end struct
    # global struct pt @ 0x130
    die(3,b'pt\0'+struct.pack('<I',point)+bytes([5,0x03])+struct.pack('<I',0x800130)+b'\x04')
    ptr=die(9,struct.pack('<I',point)+b'\x02')
    die(3,b'pp\0'+struct.pack('<I',ptr)+bytes([5,0x03])+struct.pack('<I',0x800140)+b'\x05')
    arr=die(10,struct.pack('<I',u8)); die(11,b'\x02'); body+=b'\0'
    die(3,b'samples\0'+struct.pack('<I',arr)+bytes([5,0x03])+struct.pack('<I',0x800150)+b'\x06')
    en=die(12,b'State\0'+b'\x01'); die(13,b'OFF\0'+b'\x00'); die(13,b'ON\0'+b'\x01'); body+=b'\0'
    die(3,b'state\0'+struct.pack('<I',en)+bytes([5,0x03])+struct.pack('<I',0x800160)+b'\x07')
    fn=die(4,b'foo\0'+struct.pack('<I',0x10)+struct.pack('<I',0x20)+bytes([2,0x8c,0x00])) # breg28 +0 frame base
    die(5,b'p\0'+struct.pack('<I',u8)+bytes([1,0x68])) # reg24
    die(6,b'local\0'+struct.pack('<I',u8)+bytes([2,0x91])+sleb(-2)) # fbreg -2
    body+=b'\0' # end fn
    body+=b'\0' # end CU
    return struct.pack('<I',len(body))+body

def make_elf(path:pathlib.Path):
    shstr=b'\0.shstrtab\0.debug_info\0.debug_abbrev\0'
    info=make_info(); ab=abbrev(); eh=52; shent=40; shnum=4
    o1=eh; o2=o1+len(shstr); o3=o2+len(info); shoff=(o3+len(ab)+3)&~3
    d=bytearray(shoff+shent*shnum)
    ident=b'\x7fELF'+bytes([1,1,1])+bytes(9)
    hdr=struct.pack('<16sHHIIIIIHHHHHH',ident,1,83,1,0,0,shoff,0,eh,0,0,shent,shnum,1)
    d[:len(hdr)]=hdr;d[o1:o1+len(shstr)]=shstr;d[o2:o2+len(info)]=info;d[o3:o3+len(ab)]=ab
    names={1:1,2:11,3:23}
    def sh(i,name,typ,off,size):
        b=shoff+i*shent;d[b:b+shent]=struct.pack('<IIIIIIIIII',name,typ,0,0,off,size,0,0,1,0)
    sh(1,names[1],3,o1,len(shstr));sh(2,names[2],1,o2,len(info));sh(3,names[3],1,o3,len(ab))
    path.write_bytes(d)

def main():
    cc=os.environ.get('CCNAT') or os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
    if not cc: raise SystemExit('No host compiler')
    with tempfile.TemporaryDirectory(prefix='cuzebox-dwarf-vars-') as td:
        td=pathlib.Path(td);elf=td/'fixture.elf';make_elf(elf)
        h=td/'harness.c'
        h.write_text(r'''
#include <stdio.h>
#include <string.h>
#include "debug_dwarf.h"
static unsigned char mem[4096];
static uint8_t rd(void* u,uint32_t a){(void)u;return mem[a&0xfff];}
int main(int argc,char**argv){
 uint8_t r[32]={0}; uint32_t id; cu_debug_dwarf_variable_info_t vi; cu_debug_dwarf_value_t v; cu_debug_dwarf_type_info_t ti; char t[128];
 if(argc!=2||!cu_debug_dwarf_load_elf(argv[1])) return 2;
 if(cu_debug_dwarf_type_count()<5||cu_debug_dwarf_global_count()!=5||cu_debug_dwarf_local_count(9)!=2) return 3;
 mem[0x120]=0x5a; mem[0x130]=7; mem[0x131]=9; mem[0x140]=0x30;mem[0x141]=1; mem[0x150]=1;mem[0x151]=2;mem[0x152]=3;mem[0x160]=1;
 r[24]=0x44;r[28]=0x00;r[29]=0x02;mem[0x1fe]=0x33;
 if(!cu_debug_dwarf_global_at(0,&id)||!cu_debug_dwarf_eval_variable(id,9,r,0x300,rd,0,&v)||v.value[0]!=0x5a||v.address!=0x120) return 4;
 if(!cu_debug_dwarf_global_at(1,&id)||!cu_debug_dwarf_variable_info(id,&vi)||!cu_debug_dwarf_type_info(vi.type_id,&ti)||ti.member_count!=2) return 5;
 if(!cu_debug_dwarf_eval_member(id,1,9,r,0x300,rd,0,&v,0)||v.value[0]!=9||v.address!=0x131) return 6;
 if(!cu_debug_dwarf_global_at(3,&id)||!cu_debug_dwarf_variable_info(id,&vi)||!cu_debug_dwarf_type_info(vi.type_id,&ti)||ti.kind!=CU_DBG_DWARF_TYPE_ARRAY||ti.element_count!=3) return 7;
 if(!cu_debug_dwarf_eval_element(id,2,9,r,0x300,rd,0,&v,0)||v.value[0]!=3||v.address!=0x152) return 8;
 if(!cu_debug_dwarf_global_at(4,&id)||!cu_debug_dwarf_variable_info(id,&vi)||!cu_debug_dwarf_type_info(vi.type_id,&ti)||ti.kind!=CU_DBG_DWARF_TYPE_ENUM||ti.member_count!=2) return 9;
 {cu_debug_dwarf_member_info_t em;if(!cu_debug_dwarf_member_info(vi.type_id,1,&em)||!em.is_enumerator||em.const_value!=1) return 10;}
 if(!cu_debug_dwarf_local_at(9,0,&id)||!cu_debug_dwarf_eval_variable(id,9,r,0x300,rd,0,&v)||v.location_kind!=CU_DBG_DWARF_LOC_REGISTER||v.value[0]!=0x44) return 11;
 if(!cu_debug_dwarf_local_at(9,1,&id)||!cu_debug_dwarf_eval_variable(id,9,r,0x300,rd,0,&v)||v.value[0]!=0x33||v.address!=0x1fe) return 12;
 if(!cu_debug_dwarf_variable_info(id,&vi)||!cu_debug_dwarf_type_format(vi.type_id,t,sizeof(t))) return 13;
 printf("%s\n%s\n",cu_debug_dwarf_status(),t); return 0;
}
''')
        exe=td/('test.exe' if os.name=='nt' else 'test')
        subprocess.run([str(cc),'-std=gnu99','-Wall','-Wextra','-I',str(ROOT),str(ROOT/'debug_dwarf.c'),str(h),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(elf)],check=True)
    print('debug DWARF variables/types regression: PASS')
if __name__=='__main__':main()
