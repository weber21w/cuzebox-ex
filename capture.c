/*
 *  Versioned input movies (legacy Uzem captures are not supported)
 *
 *  Copyright (C) 2022
      Matt Pandina (Artcfox)
 *  CUzeBox is Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "capture.h"
#include "cu_avr.h"
#include "cu_ctr.h"
#include "cu_multitap.h"
#include "cu_vdev.h"
#include "cu_mouse.h"
#include "cu_kbd.h"
#include "cu_esp.h"
#include "cu_spisd.h"
#include "cu_vfat.h"
#include "savestate.h"
#include "mainui.h"
#include <string.h>
#include <stdlib.h>

/* All movie integers are explicitly little endian. Snapshot payloads use the
 * existing savestate ABI and require a matching build/layout (META). */
#define MOVIE_VERSION 1U
#define MOVIE_MAX_CHUNK (32U * 1024U * 1024U)
#define MOVIE_MAX_SECTORS 16384U
#define MOVIE_META_SIZE 104U
static FILE *capture_file;
static boole capture_playing, capture_failed, capture_sd_busy;
static auint capture_saved_mouse, capture_saved_keyboard;
static cu_spisd_model_t capture_saved_model;
static boole capture_restore_settings;
static cu_state_vdev_t capture_saved_vdev;
static cu_state_multitap_t capture_saved_taps;
#define MOVIE_TOPO_CONFIG_SIZE (12U + CU_VDEV_MAX * 8U)
#define MOVIE_TOPO_SIZE (MOVIE_TOPO_CONFIG_SIZE + 40U)
static boole capture_topology_dirty;
static void topology_reset(void){ if(capture_file && !capture_playing)capture_topology_dirty=TRUE; }
static uint8 capture_topology[MOVIE_TOPO_SIZE];
static void topology_sync(void);
static char capture_name[4096] = "input.cic";
static char const *capture_status = "Input movie idle.";
static uint32 capture_cycle;
static uint64_t capture_time, capture_frames;
static uint32 capture_sd_digest;
static uint8 *capture_next;
static uint32 capture_next_size;
static uint32 capture_next_capacity;
static char capture_next_type[4];
typedef struct movie_sector {
 uint32 number;
 uint8 data[512];
 struct movie_sector *next;
} movie_sector;
static movie_sector *capture_sectors[1024];
static unsigned capture_sector_count;

static void put32(uint8 *p, uint32 v){ unsigned i; for(i=0;i<4;i++) p[i]=(uint8)(v>>(8*i)); }
static uint32 get32(uint8 const *p){ return (uint32)p[0]|((uint32)p[1]<<8)|((uint32)p[2]<<16)|((uint32)p[3]<<24); }
static void put64(uint8 *p, uint64_t v){ put32(p,(uint32)v);put32(p+4,(uint32)(v>>32)); }
static uint64_t get64(uint8 const *p){ return get32(p)|((uint64_t)get32(p+4)<<32); }
/* Reflected IEEE CRC32, two nibbles per byte. A small constant table avoids
 * both the bit loop and mutable initialization state, including in lean builds. */
static uint32 crc_bytes(uint32 crc, uint8 const *p, size_t n)
{
 static uint32 const table[16] = {
  0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
  0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
  0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
  0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
 };
 size_t i;
 for (i = 0; i < n; i++){
  crc ^= p[i];
  crc = (crc >> 4) ^ table[crc & 15U];
  crc = (crc >> 4) ^ table[crc & 15U];
 }
 return crc;
}
static uint32 crc(uint8 const *p, size_t n){ return ~crc_bytes(0xFFFFFFFFU,p,n); }
static uint64_t movie_clock(void){
 uint32 now=(uint32)cu_avr_getcycle();
 capture_time+=(uint32)(now-capture_cycle);capture_cycle=now;return capture_time;
}
static void fail(char const *message){ capture_failed=TRUE;capture_status=message; }
static boole write_chunk(char const *type, uint8 const *data, uint32 size){
 if (capture_failed){ return FALSE; }
 uint8 h[12];memcpy(h,type,4);put32(h+4,size);put32(h+8,crc(data,size));
 if(fwrite(h,1,12,capture_file)!=12 || (size && fwrite(data,1,size,capture_file)!=size)){
  fail("Input movie write failed (disk full or I/O error).");return FALSE;
 }
 return TRUE;
}
/* Caller owns the buffer, including on failure. Retain it between chunks so
 * ordinary latch/keyboard events do not allocate and free for every packet. */
static boole read_chunk(FILE *f, char *type, uint8 **data, uint32 *size,
                        uint32 *capacity)
{
 uint8 header[12];
 uint32 length;
 *size = 0;
 if (fread(header, 1, sizeof(header), f) != sizeof(header)){ return FALSE; }
 length = get32(header + 4);
 if (length > MOVIE_MAX_CHUNK){ return FALSE; }
 if (length > *capacity){
  uint8 *buffer = (uint8*)realloc(*data, length);
  if (!buffer){ return FALSE; }
  *data = buffer;
  *capacity = length;
 }
 if (length && fread(*data, 1, length, f) != length){ return FALSE; }
 if (crc(*data, length) != get32(header + 8)){ return FALSE; }
 memcpy(type, header, 4);
 *size = length;
 return TRUE;
}
static boole next_chunk(void){
 if(!read_chunk(capture_file,capture_next_type,&capture_next,&capture_next_size,&capture_next_capacity)){
  fail("Input movie is truncated or corrupt.");return FALSE;
 }
 return TRUE;
}
static boole expect(char const *type, uint32 size){
 if(capture_failed) return FALSE;
 topology_sync();
 if(capture_failed) return FALSE;
 if(memcmp(capture_next_type,type,4) || capture_next_size!=size || size<8 || get64(capture_next)!=movie_clock()){
  fail("Input replay desynchronized (event type, timing or length).");return FALSE;
 }
 return TRUE;
}
static uint32 abi_signature(void){
 uint8 b[40];uint16 endian=1;
 put32(b,sizeof(cu_state_cpu_t));put32(b+4,sizeof(cu_state_esp_t));
 put32(b+8,sizeof(cu_state_ctr_t));put32(b+12,sizeof(cu_state_vdev_t));
 put32(b+16,sizeof(cu_state_multitap_t));put32(b+20,sizeof(auint));
 put32(b+24,sizeof(void*));put32(b+28,*(uint8*)&endian);
 put32(b+32,sizeof(cu_state_kbd_t));put32(b+36,sizeof(cu_state_spisd_t));
 return crc(b,sizeof(b));
}
static boole identity(uint32 *sum, uint64_t *size){
 char const *path=mainui_get_loaded_rom_path();FILE *f;uint8 b[8192];size_t n;
 *sum=0xFFFFFFFFU;*size=0;
 if(!path || !*path){*sum=crc(cu_avr_get_state()->crom,65536);return TRUE;}
 f=fopen(path,"rb");if(!f) return FALSE;
 while((n=fread(b,1,sizeof(b),f))!=0){*sum=crc_bytes(*sum,b,n);*size+=n;}
 if(ferror(f)){fclose(f);return FALSE;}fclose(f);*sum=~*sum;return TRUE;
}
/* Stable emulated addresses never depend on host controller enumeration. */
static void topology_get(uint8 *b)
{
 unsigned p,s,id;cu_state_multitap_t taps;
 memset(b,0,MOVIE_TOPO_SIZE);
 b[0]=(uint8)cu_multitap_get_tap_present(0);b[1]=(uint8)cu_multitap_get_tap_present(1);
 b[2]=(uint8)!!cu_kbd_get_enabled();b[3]=(uint8)!!cu_mouse_get_enabled();
 for(p=0;p<2;p++)for(s=0;s<4;s++){id=cu_multitap_get_slot_vdev(p,s);b[4+p*4+s]=(uint8)(id<CU_VDEV_MAX?id:255);}
 for(id=0;id<CU_VDEV_MAX;id++){
  cu_vdev_t const *d=cu_vdev_get(id);uint8 *q=b+12+id*8;
  if(d){q[0]=1;q[1]=(uint8)d->type;put32(q+2,(uint32)d->options);q[6]=(uint8)d->haptic_enabled;q[7]=(uint8)d->haptic_id;}
 }
 cu_multitap_get_state(&taps);
 for(p=0;p<2;p++){
  put32(b+140+p*12,taps.port[p].selected_slot);put32(b+144+p*12,taps.port[p].probe_pending);put32(b+148+p*12,taps.port[p].last_activity);
 }
 put32(b+164,taps.ctrl_state);put32(b+168,taps.ctrl_arm_pulses);put32(b+172,taps.ctrl_cmd_pulses);put32(b+176,taps.ctrl_target_mask);
}
static boole topology_valid(uint8 const *b)
{
 unsigned i;
 if(get32(b+140)>3 || get32(b+152)>3 || get32(b+164)>2 || get32(b+176)>3)return FALSE;
 if(b[0]>1||b[1]>1||b[2]>1||b[3]!=0)return FALSE;
 for(i=0;i<8;i++)if(b[4+i]>=CU_VDEV_MAX && b[4+i]!=255)return FALSE;
 for(i=0;i<CU_VDEV_MAX;i++)if(b[12+i*8]>1||b[13+i*8]>CU_VDEV_TYPE_KEYBOARD)return FALSE;
 return TRUE;
}
static void topology_apply(uint8 const *b)
{
 cu_state_vdev_t devices;cu_state_multitap_t taps;unsigned id,p,s;
 cu_vdev_get_state(&devices);
 for(id=0;id<CU_VDEV_MAX;id++){
  uint8 const *q=b+12+id*8;cu_vdev_t *d=&devices.list[id];
  if(!q[0] || !d->used || d->type!=q[1]){
   memset(d,0,sizeof(*d));d->sm_scale_x_pct=100;d->sm_scale_y_pct=100;d->haptic_binding=CU_VDEV_INVALID;
  }
  d->used=q[0];d->type=(cu_vdev_type_t)q[1];d->options=get32(q+2);d->haptic_enabled=q[6];d->haptic_id=q[7];
 }
 cu_vdev_set_state(&devices);
 for(p=0;p<2;p++){
  if(cu_multitap_get_tap_present(p)!=b[p])cu_multitap_set_tap_present(p,b[p]);
  for(s=0;s<4;s++)cu_multitap_set_slot_vdev(p,s,b[4+p*4+s]==255?CU_VDEV_INVALID:b[4+p*4+s]);
 }
 cu_multitap_get_state(&taps);
 for(p=0;p<2;p++){
  taps.port[p].selected_slot=get32(b+140+p*12);taps.port[p].probe_pending=get32(b+144+p*12);taps.port[p].last_activity=get32(b+148+p*12);
 }
 taps.ctrl_state=get32(b+164);taps.ctrl_arm_pulses=get32(b+168);taps.ctrl_cmd_pulses=get32(b+172);taps.ctrl_target_mask=get32(b+176);
 cu_multitap_set_state(&taps);
 if(!!cu_kbd_get_enabled()!=b[2])cu_kbd_set_enabled(b[2]);
}
static void topology_sync(void)
{
 uint8 b[16+MOVIE_TOPO_SIZE];
 if(!capture_file || capture_failed)return;
 if(capture_playing){
  while(!memcmp(capture_next_type,"TOPO",4)){
   if(capture_next_size!=sizeof(b)||get64(capture_next)!=movie_clock()||get64(capture_next+8)!=capture_frames||!topology_valid(capture_next+16)){
    fail("Input replay desynchronized (device connection change).");return;
   }
   topology_apply(capture_next+16);memcpy(capture_topology,capture_next+16,MOVIE_TOPO_SIZE);
   if(!next_chunk())return;
  }
 }else{
  topology_get(b+16);
  if(capture_topology_dirty || memcmp(capture_topology,b+16,MOVIE_TOPO_CONFIG_SIZE)){
   if(!topology_valid(b+16)){fail("Unsupported device connected during input recording.");return;}
   put64(b,movie_clock());put64(b+8,capture_frames);write_chunk("TOPO",b,sizeof(b));
   memcpy(capture_topology,b+16,MOVIE_TOPO_SIZE);capture_topology_dirty=FALSE;
  }
 }
}

static boole supported_input(void){
 auint p,s;cu_state_esp_t const *es=cu_esp_get_state();
 if(cu_mouse_get_enabled()){
  capture_status="Use a virtual SUPERMOUSE32 slot instead of the legacy direct mouse.";return FALSE;
 }
 for(p=0;p<2;p++)for(s=0;s<4;s++){
  cu_vdev_t const *d=cu_vdev_get(cu_multitap_get_slot_vdev(p,s));
  if(d && d->type!=CU_VDEV_TYPE_NONE && d->type!=CU_VDEV_TYPE_PAD16 && d->type!=CU_VDEV_TYPE_SUPERMOUSE32 && d->type!=CU_VDEV_TYPE_KEYBOARD){
   capture_status="Unsupported input movie device type.";return FALSE;
  }
 }
 if(es && (es->serial_route==CU_ESP_SERIAL_HOST_SERIAL || es->serial_route==CU_ESP_SERIAL_HOST_MIDI || es->serial_route==CU_ESP_SERIAL_VIRTUAL_MIDI || es->serial_route==CU_ESP_SERIAL_TCP_SERIAL || es->uart_playback || es->server_enabled || es->tcp_serial_enabled || es->lan.joined || es->wifi_join_pending)){
  capture_status="Disconnect external serial/network sessions before input recording.";return FALSE;
 }
 if(es){for(p=0;p<ESP_MAX_LINKS;p++)if(es->link_state[p]!=CU_ESP_LS_IDLE){capture_status="Close ESP connections before input recording.";return FALSE;}}
 return TRUE;
}
static boole latch(auint port,auint slot,auint tick,boole before,uint64_t *packet,auint *bits,auint *probe){
 uint8 b[28];(void)tick;
 if(!capture_file) return FALSE;
 if(before)topology_sync();
 if(capture_playing){
  if(!before) return FALSE;
  *packet=~(uint64_t)0;*bits=16;*probe=0;
  if(expect("LACH",28)){
   if(capture_next[8]!=port || capture_next[9]!=slot || capture_next[10]<1 || capture_next[10]>64 || capture_next[11]>1){fail("Input replay desynchronized (controller port/slot).");return TRUE;}
   *bits=capture_next[10];*probe=capture_next[11];*packet=get64(capture_next+12);
   next_chunk();
  }
  return TRUE;
 }
 if(before || capture_failed) return FALSE;
 put64(b,movie_clock());b[8]=(uint8)port;b[9]=(uint8)slot;b[10]=(uint8)*bits;b[11]=(uint8)*probe;
 put64(b+12,*packet);put64(b+20,capture_frames);write_chunk("LACH",b,sizeof(b));return FALSE;
}
static boole keyboard_response(auint port,auint cmd,boole before,auint *response)
{
 uint8 b[16];auint slot;
 if(before)topology_sync();
 slot=cu_multitap_get_active_slot(port);
 if(capture_playing){
  if(!before)return FALSE;
  *response=0;
  if(expect("KEYB",16)){
   if(capture_next[8]!=port || capture_next[9]!=slot || capture_next[10]!=(cmd&255U) || get32(capture_next+12)!=(uint32)capture_frames){fail("Input replay desynchronized (keyboard command/slot).");return TRUE;}
   *response=capture_next[11];next_chunk();
  }
  return TRUE;
 }
 if(before || capture_failed)return FALSE;
 put64(b,movie_clock());b[8]=(uint8)port;b[9]=(uint8)slot;b[10]=(uint8)cmd;b[11]=(uint8)*response;
 put32(b+12,(uint32)capture_frames);write_chunk("KEYB",b,sizeof(b));return FALSE;
}

static movie_sector *sector(uint32 number){
 movie_sector *s;unsigned bucket=number%1024;uint8 b[524];unsigned i;
 for(s=capture_sectors[bucket];s;s=s->next)if(s->number==number)return s;
 if(capture_failed)return NULL;
 if(capture_sector_count>=MOVIE_MAX_SECTORS){fail("Input movie SD cache exceeded 8 MiB.");return NULL;}
 s=(movie_sector*)calloc(1,sizeof(*s));if(!s){fail("Input movie SD allocation failed.");return NULL;}
 s->number=number;
 if(capture_playing){
  if(!expect("SECT",sizeof(b)) || get32(capture_next+8)!=number){free(s);fail("Input replay desynchronized (SD sector).");return NULL;}
  memcpy(s->data,capture_next+12,512);next_chunk();
 }else{
  capture_sd_busy=TRUE;
  for(i=0;i<512;i++)s->data[i]=(uint8)cu_vfat_read((number<<9)+i);
  capture_sd_busy=FALSE;
  put64(b,movie_clock());put32(b+8,number);memcpy(b+12,s->data,512);write_chunk("SECT",b,sizeof(b));
 }
 s->next=capture_sectors[bucket];capture_sectors[bucket]=s;capture_sector_count++;return s;
}
static boole sd_read(auint pos,auint *data){
 movie_sector *s;if(capture_sd_busy)return FALSE;
 topology_sync();
 s=sector((uint32)pos>>9);*data=s?s->data[pos&511U]:0;return TRUE;
}
static boole sd_write(auint pos,auint data){
 movie_sector *s;uint8 b[5];
 topology_sync();s=sector((uint32)pos>>9);
 if(s && !capture_failed){s->data[pos&511U]=(uint8)data;put32(b,(uint32)pos);b[4]=(uint8)data;capture_sd_digest=crc_bytes(capture_sd_digest,b,5);}
 return TRUE; /* All movie writes stay private, including on failure. */
}
void capture_finalize(void){
 unsigned i;uint8 b[16];
 if(capture_file){
  if(!capture_playing && !capture_failed){put64(b,movie_clock());put64(b+8,capture_frames);write_chunk("END!",b,16);}
  if(fclose(capture_file)!=0)capture_status="Input movie close/flush failed.";
 }
 capture_file=NULL;capture_playing=FALSE;
 cu_ctr_set_latch_hook(NULL);cu_vfat_set_io_hooks(NULL,NULL);cu_kbd_set_movie_hook(NULL,FALSE);cu_multitap_set_reset_hook(NULL);
 if(capture_restore_settings){
  cu_mouse_set_enabled((uint8)capture_saved_mouse);
  cu_kbd_set_enabled((uint8)capture_saved_keyboard);
  cu_spisd_model_set(&capture_saved_model);
  cu_vdev_set_state(&capture_saved_vdev);cu_multitap_set_state(&capture_saved_taps);
  capture_restore_settings=FALSE;
 }
 free(capture_next);capture_next=NULL;capture_next_capacity=0;
 for(i=0;i<1024;i++){movie_sector *s=capture_sectors[i];while(s){movie_sector *next=s->next;free(s);s=next;}capture_sectors[i]=NULL;}
 capture_sector_count=0;capture_sd_busy=FALSE;
}
/* Validate every chunk before touching the live machine. This deliberately
 * rejects raw legacy captures and files without a clean completion marker. */
static boole validate(FILE *f,uint8 *meta,savestate_blob_t *snapshot){
 uint8 header[16],*b=NULL;uint32 n,capacity=0;char type[4];boole ended=FALSE;unsigned phase=0;
 uint64_t last=0,frames=0;uint32 sum;uint64_t size;
 if(fread(header,1,16,f)!=16 || memcmp(header,"CZINPUT\0",8) || get32(header+8)!=MOVIE_VERSION || get32(header+12)!=16){capture_status="Unsupported input file (expected CZINPUT v1; legacy .cap is not supported).";return FALSE;}
 while(read_chunk(f,type,&b,&n,&capacity)){
  if(phase==0){if(memcmp(type,"META",4)||n!=MOVIE_META_SIZE)goto bad;memcpy(meta,b,n);phase++;}
  else if(phase==1){if(memcmp(type,"STAT",4)||n<32)goto bad;snapshot->data=b;snapshot->size=n;b=NULL;capacity=0;phase++;}
  else{
   uint64_t time;
   if(n<8)goto bad;
   time=get64(b);if(time<last)goto bad;last=time;
   if(!memcmp(type,"LACH",4)){if(n!=28||b[8]>1||b[9]>3||b[10]<1||b[10]>64||b[11]>1||get64(b+20)!=frames)goto bad;}
   else if(!memcmp(type,"TOPO",4)){if(n!=16+MOVIE_TOPO_SIZE||get64(b+8)!=frames||!topology_valid(b+16))goto bad;}
   else if(!memcmp(type,"KEYB",4)){if(n!=16||b[8]>1||b[9]>3||get32(b+12)!=(uint32)frames)goto bad;}
   else if(!memcmp(type,"SECT",4)){if(n!=524||get32(b+8)>0x7FFFFFU)goto bad;}
   else if(!memcmp(type,"FRAM",4)){if(n!=24||get64(b+8)!=++frames)goto bad;}
   else if(!memcmp(type,"END!",4)){if(n!=16||get64(b+8)!=frames||fgetc(f)!=EOF||ferror(f))goto bad;ended=TRUE;break;}
   else goto bad; /* Unknown required chunks need a newer reader. */
  }
 }
 if(!ended)goto bad;
 free(b);
 if(get32(meta)!=abi_signature()){capture_status="Input movie snapshot requires a compatible emulator build/architecture.";return FALSE;}
 if(!identity(&sum,&size)||get32(meta+4)!=sum||get64(meta+8)!=size){capture_status="Input movie ROM checksum/size does not match the loaded game.";return FALSE;}
 if(!savestate_validate_blob(snapshot)){capture_status="Input movie contains an incompatible or invalid starting snapshot.";return FALSE;}
 return TRUE;
 bad:free(b);capture_status="Input movie is corrupt, truncated or missing its end marker.";return FALSE;
}
boole capture_start_ex(char const *name,boole replay,boole reset_first){
 FILE *file;uint8 meta[MOVIE_META_SIZE]={0},header[16];uint32 sum;uint64_t size;
 savestate_blob_t snapshot={NULL,0};unsigned p,s;cu_spisd_model_t model;
#ifndef ENABLE_IREP
 if(replay){capture_status="Input replay is disabled in this build.";return FALSE;}
#endif
 if(!name||!*name||strlen(name)>=sizeof(capture_name)){capture_status="Invalid input movie path.";return FALSE;}
 if(capture_file && !strcmp(name,capture_name) && !replay){capture_status="Stop the current movie before recording to the same file.";return FALSE;}
 if(!replay && !supported_input())return FALSE;
 if(!replay && !identity(&sum,&size)){capture_status="Could not checksum the loaded ROM file.";return FALSE;}
 if(!replay && !strcmp(name,mainui_get_loaded_rom_path())){capture_status="The movie output must not replace the loaded ROM.";return FALSE;}
 file=fopen(name,replay?"rb":"w+b");if(!file){capture_status="Could not open input movie.";return FALSE;}
 if(replay && !validate(file,meta,&snapshot)){fclose(file);savestate_free_blob(&snapshot);return FALSE;}
 capture_finalize();capture_failed=FALSE;
 if(!replay){
  if(reset_first)mainui_reset_rom();
  if(!savestate_capture_to_mem(&snapshot)){fclose(file);capture_status="Could not capture movie starting state.";return FALSE;}
  put32(meta,abi_signature());put32(meta+4,sum);put64(meta+8,size);
  put32(meta+16,crc(cu_avr_get_state()->crom,65536));put32(meta+20,reset_first?1:0);
  model=*cu_spisd_model_get();
  {auint fields[9]={model.preset,model.init_ms,model.cmd_wait_bytes,model.read_wait_bytes,model.write_busy_ms,model.cs_high_ms,model.init_min_byte_cycles,model.init_max_byte_cycles,0};for(p=0;p<9;p++)put32(meta+24+p*4,fields[p]);}
  for(p=0;p<2;p++){
   meta[60+p]=(uint8)cu_multitap_get_tap_present(p);
   for(s=0;s<4;s++){cu_vdev_t const *d=cu_vdev_get(cu_multitap_get_slot_vdev(p,s));meta[64+p*4+s]=(uint8)(d?d->type:CU_VDEV_TYPE_PAD16);}
  }
 }else{
  capture_saved_mouse=cu_mouse_get_enabled();capture_saved_keyboard=cu_kbd_get_enabled();
  capture_saved_model=*cu_spisd_model_get();
  cu_vdev_get_state(&capture_saved_vdev);cu_multitap_get_state(&capture_saved_taps);
  if(!savestate_restore_from_mem(&snapshot)){fclose(file);savestate_free_blob(&snapshot);capture_status="Could not restore movie starting state.";return FALSE;}
  capture_restore_settings=TRUE;
  cu_mouse_set_enabled(0);
  model.preset=get32(meta+24);model.init_ms=get32(meta+28);model.cmd_wait_bytes=get32(meta+32);model.read_wait_bytes=get32(meta+36);model.write_busy_ms=get32(meta+40);model.cs_high_ms=get32(meta+44);model.init_min_byte_cycles=get32(meta+48);model.init_max_byte_cycles=get32(meta+52);cu_spisd_model_set(&model);
 }
 topology_get(capture_topology);capture_topology_dirty=FALSE;
 capture_file=file;capture_playing=replay;capture_time=0;capture_frames=0;capture_cycle=(uint32)cu_avr_getcycle();capture_sd_digest=0xFFFFFFFFU;
 memmove(capture_name,name,strlen(name)+1);
 if(replay){
  if(fseek(file,16L+12L+MOVIE_META_SIZE+12L+snapshot.size,SEEK_SET)!=0)fail("Could not seek input movie events.");
  else next_chunk();
 }
 else{
  memcpy(header,"CZINPUT\0",8);put32(header+8,MOVIE_VERSION);put32(header+12,16);
  if(fwrite(header,1,16,file)!=16)fail("Input movie header write failed.");
  write_chunk("META",meta,sizeof(meta));write_chunk("STAT",snapshot.data,snapshot.size);
 }
 savestate_free_blob(&snapshot);
 if(capture_failed){capture_finalize();return FALSE;}
 cu_ctr_set_latch_hook(latch);cu_vfat_set_io_hooks(sd_read,sd_write);cu_kbd_set_movie_hook(keyboard_response,replay);cu_multitap_set_reset_hook(topology_reset);
 capture_status=replay?"Input movie replay running.":"Input movie recording running.";return TRUE;
}
boole capture_start(char const *name,boole replay){return capture_start_ex(name,replay,FALSE);}
void capture_inputs(void){if(capture_file){movie_clock();topology_sync();}}
boole capture_replay(void){
 if(!capture_file)return TRUE;
 topology_sync();
 if(capture_failed){capture_finalize();return TRUE;}
 if(capture_playing && !memcmp(capture_next_type,"END!",4)){
  if(!expect("END!",16)||get64(capture_next+8)!=capture_frames){fail("Input replay desynchronized at end.");}
  else capture_status="Input replay complete.";
  capture_finalize();return TRUE;
 }
 return FALSE;
}
boole capture_frame_end(void){
 uint8 b[24];uint32 state_crc;cu_state_cpu_t const *cpu=cu_avr_get_state();
 if(!capture_file)return TRUE;
 topology_sync();
 if(capture_failed){capture_finalize();return FALSE;}
 state_crc=crc(cpu->sram,sizeof(cpu->sram));state_crc=crc_bytes(state_crc,cpu->iors,sizeof(cpu->iors));
 state_crc=crc_bytes(state_crc,cpu->eepr,sizeof(cpu->eepr));
 {uint8 pc[4];put32(pc,cpu->pc);state_crc=crc_bytes(state_crc,pc,4);}
 put64(b,movie_clock());put64(b+8,++capture_frames);put32(b+16,state_crc);put32(b+20,capture_sd_digest);
 if(capture_playing){
  if(!expect("FRAM",24)||memcmp(b,capture_next,24)){fail("Input replay desynchronized (frame state or SD writes).");capture_finalize();return FALSE;}
  next_chunk();
 }else write_chunk("FRAM",b,24);
 if(capture_failed){capture_finalize();return FALSE;}return TRUE;
}
void capture_reset(void){capture_status="Input movie stopped.";capture_finalize();}
boole capture_is_replaying(void){return capture_playing;}
boole capture_is_active(void){return capture_file!=NULL;}
char const *capture_get_filename(void){return capture_name;}
char const *capture_get_status(void){return capture_status;}
