#!/usr/bin/env python3
"""Host regression for opt-in CUzeBox audio oscilloscope capture."""
from __future__ import annotations
import os,pathlib,shutil,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL=r'''#ifndef SDL_H
#define SDL_H
#include <stdint.h>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int16_t Sint16; typedef int32_t Sint32; typedef uint32_t SDL_AudioDeviceID;
typedef void (*SDL_AudioCallback)(void*,Uint8*,int);
typedef struct SDL_AudioSpec { int freq; Uint16 format; Uint8 channels; Uint8 silence; Uint16 samples; Uint16 padding; Uint32 size; SDL_AudioCallback callback; void* userdata; } SDL_AudioSpec;
#define AUDIO_U8 0x0008
#define AUDIO_S16SYS 0x8010
#define SDL_INIT_AUDIO 0x10U
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 1U
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE 2U
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE 4U
int SDL_InitSubSystem(Uint32 f); SDL_AudioDeviceID SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec*,SDL_AudioSpec*,int); void SDL_CloseAudioDevice(SDL_AudioDeviceID); void SDL_PauseAudioDevice(SDL_AudioDeviceID,int); void SDL_LockAudioDevice(SDL_AudioDeviceID); void SDL_UnlockAudioDevice(SDL_AudioDeviceID);
#endif
'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
static SDL_AudioCallback g_cb;
int SDL_InitSubSystem(Uint32 f){(void)f;return 0;}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*a,int b,const SDL_AudioSpec*d,SDL_AudioSpec*h,int flags){(void)a;(void)b;(void)flags;*h=*d;g_cb=d->callback;return 1U;}
void SDL_CloseAudioDevice(SDL_AudioDeviceID d){(void)d;}
void SDL_PauseAudioDevice(SDL_AudioDeviceID d,int p){(void)d;(void)p;}
void SDL_LockAudioDevice(SDL_AudioDeviceID d){(void)d;}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID d){(void)d;}
int main(void){
 uint8 in[16],got[16]; auint i; SDL_AudioCallback normal,armed; audio_scope_status_t st;
 for(i=0;i<16U;i++)in[i]=(uint8)(0x20U+i);
 assert(audio_init()); normal=g_cb; assert(normal!=0 && !audio_scope_output_enabled());
 audio_reset(); audio_sendframe(in,16U); assert(audio_scope_copy_source(got,16U)==16U); assert(memcmp(in,got,16U)==0);
 audio_scope_output_enable(TRUE); armed=g_cb; assert(armed!=0 && armed!=normal && audio_scope_output_enabled());
 audio_scope_get_status(&st); assert(st.output_capture_enabled);
 audio_scope_output_enable(FALSE); assert(g_cb==normal && !audio_scope_output_enabled());
 puts("audio scope opt-in regression: PASS"); return 0;
}
'''
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-audio-scope-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS)
  exe=d/'t';subprocess.run([cc,'-std=gnu99','-I',str(d),'-I',str(ROOT),str(ROOT/'audio.c'),str(d/'h.c'),'-lm','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 return 0
if __name__=='__main__': raise SystemExit(main())
