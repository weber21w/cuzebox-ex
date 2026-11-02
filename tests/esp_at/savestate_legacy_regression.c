#include "savestate.h"
#include "cu_avr.h"
#include "cu_spir.h"
#include "cu_spisd.h"
#include "cu_vfat.h"
#include "cu_esp.h"
#include "cu_multitap.h"
#include "cu_ctr.h"
#include "cu_kbd.h"
#include "cu_gun.h"
#include "cu_vdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define LEGACY_ESP_SIZE 132512u
#define MAGIC 0x435a5356UL
#define VERSION 1UL

typedef struct { uint32 magic,version,cpu_size,spir_meta_size,spir_ram_size,spisd_size,vfat_size,esp_size; } hdr_t;
typedef struct { boole ena; auint mode,state,addr,data,size; uint8 addr_bytes; auint addr_mask,page_size,page_mask; } spir_meta_t;

static cu_state_cpu_t cpu_state;
static cu_state_spir_t spir_state;
static cu_state_spisd_t spisd_state;
static cu_state_esp_t esp_state_test;
static int reset_network_calls, vfat_load_calls, crom_calls, io_calls, spir_update_calls, spisd_update_calls;
static int capture_reset_calls;

cu_state_cpu_t* cu_avr_get_state(void){return &cpu_state;}
void cu_avr_crom_update(auint b,auint l){(void)b;(void)l;crom_calls++;}
void cu_avr_io_update(void){io_calls++;}
void cu_avr_cancel_execution_hook(void){}
void cu_avr_timer1_restore_compare_block(boole blocked){(void)blocked;}
void frame_capture_reset(void){capture_reset_calls++;}
cu_state_spir_t* cu_spir_get_state(void){return &spir_state;}
void cu_spir_set_size(auint banks){(void)banks;}
void cu_spir_update(void){spir_update_calls++;}
cu_state_spisd_t* cu_spisd_get_state(void){return &spisd_state;}
void cu_spisd_update(void){spisd_update_calls++;}
boole cu_vfat_state_load(uint8 const*src,auint size){(void)src;(void)size;vfat_load_calls++;return TRUE;}
boole cu_vfat_state_validate(uint8 const*src,auint size){return src && size >= sizeof(cu_state_vfat_t);}
cu_state_esp_t* cu_esp_get_state(void){return &esp_state_test;}
void cu_esp_reset_network(void){reset_network_calls++;}
void cu_multitap_set_state(cu_state_multitap_t const*s){(void)s;}
void cu_ctr_set_state(cu_state_ctr_t const*s){(void)s;}
void cu_kbd_set_state(cu_state_kbd_t const*s){(void)s;}
void cu_gun_set_state(cu_state_gun_t const*s){(void)s;}
void cu_vdev_set_state(cu_state_vdev_t const*s){(void)s;}
void print_error(char const*fmt,...){(void)fmt;}

int main(void){
    hdr_t h;spir_meta_t sm;savestate_blob_t blob;size_t total;uint8 *p;
    memset(&h,0,sizeof(h));memset(&sm,0,sizeof(sm));memset(&cpu_state,0,sizeof(cpu_state));memset(&spir_state,0,sizeof(spir_state));memset(&spisd_state,0,sizeof(spisd_state));memset(&esp_state_test,0,sizeof(esp_state_test));
    h.magic=MAGIC;h.version=VERSION;h.cpu_size=(uint32)sizeof(cpu_state);h.spir_meta_size=(uint32)sizeof(sm);h.spir_ram_size=0;h.spisd_size=(uint32)sizeof(spisd_state);h.vfat_size=(uint32)sizeof(cu_state_vfat_t);h.esp_size=LEGACY_ESP_SIZE;
    total=sizeof(h)+h.cpu_size+h.spir_meta_size+h.spisd_size+h.vfat_size+h.esp_size;blob.data=(uint8*)calloc(1,total);blob.size=(auint)total;if(!blob.data)return 2;
    p=blob.data;memcpy(p,&h,sizeof(h));p+=sizeof(h);memset(p,0x3c,h.cpu_size);p+=h.cpu_size;memcpy(p,&sm,sizeof(sm));p+=sizeof(sm);memset(p,0,h.spisd_size);p+=h.spisd_size;memset(p,0,h.vfat_size);p+=h.vfat_size;memset(p,0xa5,h.esp_size);
    esp_state_test.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;esp_state_test.socks[0]=123;esp_state_test.socks[4]=456;esp_state_test.host_serial_enabled=1;
    if(!savestate_restore_from_mem(&blob)){fprintf(stderr,"legacy restore rejected\n");return 3;}
    if(((uint8*)&cpu_state)[0]!=0x3c){fprintf(stderr,"CPU not restored\n");return 4;}
    if(vfat_load_calls!=1||crom_calls!=1||io_calls!=1||spir_update_calls!=1||spisd_update_calls!=1){fprintf(stderr,"subsystem restore counts wrong\n");return 5;}
    if(reset_network_calls!=1){fprintf(stderr,"ESP network reset not invoked\n");return 6;}
    if(capture_reset_calls!=1){fprintf(stderr,"stale screenshot cache not cleared\n");return 9;}
    if(esp_state_test.at_firmware_profile!=CU_ESP_AT_FW_ESPAT_230){fprintf(stderr,"current ESP config was overwritten\n");return 7;}
    if(esp_state_test.socks[0]!=ESP_INVALID_SOCKET||esp_state_test.socks[4]!=ESP_INVALID_SOCKET||esp_state_test.host_serial_enabled){fprintf(stderr,"volatile ESP state not sanitized\n");return 8;}
    free(blob.data);printf("esp_legacy_savestate_regression: PASS (132512-byte ESP block)\n");return 0;
}
