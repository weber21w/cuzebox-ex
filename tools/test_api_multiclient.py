#!/usr/bin/env python3
"""Host regression for CUzeBox API multi-client transport and wait ownership."""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time
from test_debug_dwarf import make_elf

ROOT = pathlib.Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#define TEST_SLEEP_MS(ms) Sleep((DWORD)(ms))
#else
#include <unistd.h>
#define TEST_SLEEP_MS(ms) usleep((useconds_t)((ms) * 1000U))
#endif
#include "api_server.h"
#include "mainui.h"
#include "cu_avr.h"
#include "cu_ctr.h"
#include "debug_dwarf.h"
#include "audio.h"
#include "cu_vfat.h"

static uint32 g_frame = 0U;
static boole g_paused = TRUE;
static boole g_quit = FALSE;
static uint8_t g_data[4096];
static cu_row_t g_row;
static char g_symbols_path[1024];
static cu_spisd_model_t g_sd_model = { CU_SPISD_PRESET_NORMAL,500U,0U,2U,100U,0U,16U,2296U };
static cu_state_spisd_t g_sd_state;
static cu_state_vfat_t g_vfat_state;
static cu_state_cpu_t g_cpu_state;
static boole g_audio_scope = FALSE;
static boole g_beam_capture = FALSE;
static boole g_mem_trace = FALSE;
static boole g_uart_logic_tx = FALSE;
static boole g_uart_logic_rx = FALSE;
void cu_uart_debug_logic_capture_set(boole tx_enable, boole rx_enable){g_uart_logic_tx=tx_enable?TRUE:FALSE;g_uart_logic_rx=rx_enable?TRUE:FALSE;}
void cu_uart_debug_logic_capture_get(boole* tx_enable, boole* rx_enable){if(tx_enable)*tx_enable=g_uart_logic_tx;if(rx_enable)*rx_enable=g_uart_logic_rx;}
void audio_scope_output_enable(boole e){g_audio_scope=e?TRUE:FALSE;}
boole audio_scope_output_enabled(void){return g_audio_scope;}
void audio_scope_clear(void){}
void audio_scope_get_status(audio_scope_status_t* o){if(!o)return;memset(o,0,sizeof(*o));o->output_capture_enabled=g_audio_scope;o->source_rate=15734U;o->output_rate=48000U;o->output_format=0x8010U;o->output_channels=2U;o->output_count=g_audio_scope?64U:0U;o->output_frames_total=g_audio_scope?1234U:0U;o->output_rail_left=g_audio_scope?2U:0U;o->output_rail_right=g_audio_scope?1U:0U;o->output_peak_left=32768U;o->output_peak_right=32000U;}
auint audio_scope_copy_source(uint8* o, auint n){auint i;if(n>4096U)n=4096U;for(i=0;i<n;i++)o[i]=(uint8)(i&0xFFU);return n;}
auint audio_scope_copy_output(sint16* l,sint16* r,auint n){auint i;if(!g_audio_scope)return 0U;if(n>64U)n=64U;for(i=0;i<n;i++){l[i]=(i==0U)?(sint16)-32768:(sint16)(i*10);r[i]=(i==1U)?(sint16)32767:(sint16)(-((sint32)i*10));}return n;}
auint audio_getfreq(void){return 15734U;}
void cu_spisd_model_preset(auint p, cu_spisd_model_t* o){ if(!o)return; if(p==CU_SPISD_PRESET_SLOW){*o=(cu_spisd_model_t){p,1000U,4U,8U,250U,2U,16U,2296U};}else if(p==CU_SPISD_PRESET_FAST){*o=(cu_spisd_model_t){p,50U,0U,0U,10U,0U,16U,2296U};}else{*o=(cu_spisd_model_t){CU_SPISD_PRESET_NORMAL,500U,0U,2U,100U,0U,16U,2296U};}}
void cu_spisd_model_set(cu_spisd_model_t const* m){if(m)g_sd_model=*m;}
cu_spisd_model_t const* cu_spisd_model_get(void){return &g_sd_model;}
cu_state_spisd_t* cu_spisd_get_state(void){return &g_sd_state;}
void cu_spisd_reset(auint c){(void)c;memset(&g_sd_state,0,sizeof(g_sd_state));}
void cu_spisd_trace_enable(boole e){(void)e;} boole cu_spisd_trace_built(void){return FALSE;} boole cu_spisd_trace_enabled(void){return FALSE;} boole cu_spisd_trace_active(void){return FALSE;} void cu_spisd_trace_clear(void){} auint cu_spisd_trace_count(void){return 0U;} uint32 cu_spisd_trace_first_seq(void){return 0U;} boole cu_spisd_trace_get(uint32 s,cu_spisd_trace_event_t*o){(void)s;(void)o;return FALSE;} void cu_spisd_trace_break_command_set(auint c,boole e){(void)c;(void)e;} void cu_spisd_trace_break_commands_clear(void){} uint32 cu_spisd_trace_break_command_mask_lo(void){return 0U;} uint32 cu_spisd_trace_break_command_mask_hi(void){return 0U;} void cu_spisd_trace_break_sector_set(boole e,auint s){(void)e;(void)s;} boole cu_spisd_trace_break_sector_get(auint*s){if(s)*s=0U;return FALSE;} void cu_spisd_trace_break_init_fail_set(boole e){(void)e;} boole cu_spisd_trace_break_init_fail_get(void){return FALSE;} void cu_spisd_trace_break_crc_set(boole e){(void)e;} boole cu_spisd_trace_break_crc_get(void){return FALSE;} void cu_spisd_trace_break_latency_set(boole e,auint c){(void)e;(void)c;} boole cu_spisd_trace_break_latency_get(auint*c){if(c)*c=0U;return FALSE;} boole cu_spisd_trace_hit_get(boole c,cu_spisd_trace_event_t*o){(void)c;(void)o;return FALSE;}
boole cu_spisd_trace_break_notice_get(boole c,cu_spisd_trace_event_t*o){(void)c;(void)o;return FALSE;}
void cu_spisd_trace_break_fs_role_set(auint r,boole e){(void)r;(void)e;} uint32 cu_spisd_trace_break_fs_role_mask(void){return 0U;} void cu_spisd_trace_break_fs_path_set(char const*p){(void)p;} char const* cu_spisd_trace_break_fs_path_get(void){return "";} void cu_spisd_trace_break_fs_access_set(auint a){(void)a;} auint cu_spisd_trace_break_fs_access_get(void){return CU_SPISD_FS_ACCESS_READ|CU_SPISD_FS_ACCESS_WRITE;}
boole cu_spisd_fault_built(void){return FALSE;} boole cu_spisd_fault_active(void){return FALSE;} void cu_spisd_fault_clear(auint s){(void)s;} boole cu_spisd_fault_set(auint s,cu_spisd_fault_rule_t const*r){(void)s;(void)r;return FALSE;} boole cu_spisd_fault_get(auint s,cu_spisd_fault_rule_t*o){(void)s;if(o)memset(o,0,sizeof(*o));return FALSE;}
boole cu_vfat_debug_sector_snapshot(auint sector, boole backing, cu_vfat_debug_sector_t* o){auint i;if(!o)return FALSE;memset(o,0,sizeof(*o));o->sector=sector;o->backing_valid=backing;o->file_offset=0x200U;o->role=CU_VFAT_DEBUG_ROLE_FILE;o->cluster=9U;o->sector_in_cluster=1U;o->owner_valid=TRUE;o->chain_index=0U;o->start_cluster=8U;o->next_cluster=0xFFFFU;o->entry_sector=513U;o->entry_index=2U;o->attr=0x20U;strcpy(o->source,"TEST.BIN");for(i=0;i<512U;i++)o->backing[i]=(uint8)(i&0xFFU);if(backing)o->backing[66]^=0xFFU;return TRUE;}
cu_state_vfat_t* cu_vfat_get_state(void){return &g_vfat_state;}

void mainui_system_message(char const* fmt, ...){ (void)fmt; }
void cu_ctr_setsnes(auint player, auint buttons){ (void)player; (void)buttons; }

void cu_avr_profiler_enable(boole enable){ (void)enable; }
boole cu_avr_profiler_enabled(void){ return FALSE; }
void cu_avr_profiler_reset(void){}
boole cu_avr_profiler_get(auint a, uint32* h, uint64_t* c){ (void)a; if(h)*h=0; if(c)*c=0; return FALSE; }
void cu_avr_breakpoints_clear(void){}
void cu_avr_breakpoint_set(auint a, boole e){ (void)a; (void)e; }
boole cu_avr_breakpoint_next(auint a, auint* o){ (void)a; if(o)*o=0; return FALSE; }
void cu_avr_temp_break_set(auint a, boole e){ (void)a; (void)e; }
void cu_avr_temp_break_clear(void){}
boole cu_avr_temp_break_get(auint* a){ if(a)*a=0; return FALSE; }
void cu_avr_debug_step_instructions(auint c){ (void)c; }
void cu_avr_debug_step_clear(void){}
void cu_avr_memory_trace_enable(boole e){g_mem_trace=e?TRUE:FALSE;}
boole cu_avr_memory_trace_built(void){return TRUE;}
boole cu_avr_memory_trace_enabled(void){return g_mem_trace;}
void cu_avr_memory_trace_clear(void){}
auint cu_avr_memory_trace_count(void){return 0U;}
uint32 cu_avr_memory_trace_first_seq(void){return 0U;}
boole cu_avr_memory_trace_get(uint32 s, cu_avr_memtrace_event_t* o){(void)s;(void)o;return FALSE;}
void cu_avr_audio_trace_enable(boole e){(void)e;}
boole cu_avr_audio_trace_built(void){return FALSE;}
boole cu_avr_audio_trace_enabled(void){return FALSE;}
void cu_avr_audio_trace_clear(void){}
auint cu_avr_audio_trace_count(void){return 0U;}
uint32 cu_avr_audio_trace_first_seq(void){return 0U;}
boole cu_avr_audio_trace_get(uint32 s, cu_avr_audio_event_t* o){(void)s;(void)o;return FALSE;}
void cu_avr_audio_break_set(auint m, auint l, auint h){(void)m;(void)l;(void)h;}
auint cu_avr_audio_break_mode(void){return CU_AVR_AUDIO_BREAK_OFF;}
auint cu_avr_audio_break_low(void){return 0U;}
auint cu_avr_audio_break_high(void){return 255U;}
boole cu_avr_audio_break_hit(boole c, cu_avr_audio_event_t* o){(void)c;(void)o;return FALSE;}
uint64_t cu_avr_audio_event_total(void){return 0ULL;}
uint64_t cu_avr_audio_rail_low_total(void){return 0ULL;}
uint64_t cu_avr_audio_rail_high_total(void){return 0ULL;}
auint cu_avr_audio_peak_distance(void){return 0U;}
void cu_avr_watchpoints_clear(void){}
void cu_avr_watchpoint_set(auint s, boole e, auint r, auint f, auint a, auint z){(void)s;(void)e;(void)r;(void)f;(void)a;(void)z;}
boole cu_avr_watchpoint_get(auint s, boole* e, auint* r, auint* f, auint* a, auint* z){(void)s;if(e)*e=FALSE;if(r)*r=0;if(f)*f=0;if(a)*a=0;if(z)*z=0;return FALSE;}
boole cu_avr_watchpoint_get_last(boole c, auint* s, auint* r, auint* f, auint* a, auint* v, auint* p){(void)c;if(s)*s=0;if(r)*r=0;if(f)*f=0;if(a)*a=0;if(v)*v=0;if(p)*p=0;return FALSE;}
cu_row_t const* cu_avr_get_row(void){ auint i; for(i=0U;i<1820U;i++)g_row.pixels[i]=(uint8)(i&0xFFU); g_row.pno=40U; return &g_row; }
auint cu_avr_get_video_cycle(void){ return 321U; }
auint cu_avr_getcycle(void){ return 100000U; }
cu_state_cpu_t* cu_avr_get_state(void){ g_cpu_state.cycle=100000U; g_cpu_state.spi_tran=TRUE; g_cpu_state.spi_end=100016U; g_cpu_state.spi_tx=0xA5U; g_cpu_state.spi_rx=0x5AU; g_cpu_state.iors[CU_IO_SPDR]=0x3CU; g_cpu_state.iors[CU_IO_SPCR]=0x50U; g_cpu_state.iors[CU_IO_SPSR]=0x01U; return &g_cpu_state; }
auint cu_avr_get_video_pulse(void){ return 41U; }
auint cu_avr_get_video_beam_cycle(void){ return 321U; }
auint cu_avr_get_video_beam_pulse(void){ return 40U; }
uint8 const* cu_avr_get_video_beam_pixels(void){ (void)cu_avr_get_row(); return &g_row.pixels[0]; }
void cu_avr_video_beam_capture_enable(boole e){g_beam_capture=e?TRUE:FALSE;}
boole cu_avr_video_beam_capture_built(void){return TRUE;}
boole cu_avr_video_beam_capture_enabled(void){return g_beam_capture;}

char const* mainui_get_loaded_rom_path(void){ return "/tmp/test.uze"; }
char const* mainui_get_current_rom_name(void){ return "TEST"; }
char const* mainui_get_current_rom_author(void){ return "TEST"; }
auint mainui_get_current_rom_year(void){ return 2026U; }
boole mainui_get_current_rom_is_uze(void){ return TRUE; }
uint32 mainui_get_current_rom_crc32(void){ return 0x12345678U; }
uint32 mainui_get_frame_counter(void){ return g_frame; }
boole mainui_load_rom_file(char const* p){ (void)p; return TRUE; }
void mainui_reset_rom(void){}
void mainui_request_quit(void){ g_quit = TRUE; }
boole mainui_save_screenshot_auto(char* out, auint n){ if(n>0){ snprintf(out,n,"test.bmp"); } return TRUE; }
boole mainui_save_screenshot_file(char const* p){ (void)p; return TRUE; }
boole mainui_capture_screenshot_bmp(uint8** out, auint* n, auint* w, auint* h){ auint i; if(!out||!n)return FALSE; *n=66U; *out=(uint8*)malloc(*n); if(!*out)return FALSE; (*out)[0]='B';(*out)[1]='M';for(i=2U;i<*n;i++)(*out)[i]=(uint8)(i-2U); if(w)*w=4U;if(h)*h=4U;return TRUE; }

boole mainui_debug_is_paused(void){ return g_paused; }
void mainui_debug_set_paused(boole p){ g_paused=p; }
void mainui_debug_step_frame(void){}
void mainui_debug_step_instruction(void){}
boole mainui_debug_step_over(void){ return TRUE; }
boole mainui_debug_step_out(void){ return TRUE; }
boole mainui_debug_run_to_word(auint a){ (void)a; return TRUE; }
boole mainui_debug_get_run_target(auint* a){ if(a)*a=0; return FALSE; }
char const* mainui_debug_get_run_control_status(void){ return "Idle"; }
void mainui_debug_get_cpu(mainui_debug_cpu_t* o){ if(o){ memset(o,0,sizeof(*o)); o->pc=9U; o->sp=0x10FFU; o->regs[24]=0x44U; o->regs[28]=0x00U; o->regs[29]=0x02U; } }
auint mainui_debug_get_sram_byte(auint a){ return g_data[a & 0x0FFFU]; }
auint mainui_debug_get_io_byte(auint a){ return g_data[a & 0x0FFFU]; }
auint mainui_debug_get_prog_word(auint a){ (void)a; return 0; }
boole mainui_debug_get_disasm(auint a,char* o,auint n,auint* w){(void)a;if(o&&n)snprintf(o,n,"NOP");if(w)*w=1;return TRUE;}
char const* mainui_debug_get_symbols_file(void){ return g_symbols_path; }
char const* mainui_debug_get_symbols_status(void){ return cu_debug_dwarf_status(); }
auint mainui_debug_get_symbol_count(void){ return 0; }
boole mainui_debug_load_symbols_file(char const* p){ snprintf(g_symbols_path,sizeof(g_symbols_path),"%s",p?p:""); return cu_debug_dwarf_load_elf(p)?TRUE:FALSE; }
void mainui_debug_clear_symbols(void){ g_symbols_path[0]=0; cu_debug_dwarf_reset(); }
auint mainui_debug_get_mem_region_size(auint r){ (void)r; return 0x1000U; }
auint mainui_debug_get_mem_region_byte(auint r, auint a){ (void)r;(void)a;return 0; }
void mainui_debug_set_io_byte(auint a, auint v){g_data[a&0x0FFFU]=(uint8)v;}
void mainui_debug_set_sram_byte(auint a, auint v){g_data[a&0x0FFFU]=(uint8)v;}
void mainui_debug_set_reg_byte(auint r, auint v){(void)r;(void)v;}
void mainui_debug_set_mem_region_byte(auint r, auint a, auint v){(void)r;(void)a;(void)v;}
char const* mainui_debug_get_profile_dir(void){ return "/tmp"; }
char const* mainui_debug_get_profile_status(void){ return "test"; }
boole mainui_debug_profile_ensure_loaded(void){ return TRUE; }
boole mainui_debug_profile_save_now(void){ return TRUE; }
boole mainui_debug_profile_reload(void){ return TRUE; }
void mainui_debug_set_breakpoint(auint a, boole e){(void)a;(void)e;}
boole mainui_debug_get_last_break_hit(auint* a){ if(a)*a=0; return FALSE; }
boole mainui_debug_find_symbol(char const* n, auint* k, auint* a){(void)n;if(k)*k=0;if(a)*a=0;return FALSE;}
char const* mainui_debug_get_source_status(void){ return "none"; }
auint mainui_debug_get_source_file_count(void){ return 0; }
auint mainui_debug_get_source_row_count(void){ return 0; }
char const* mainui_debug_get_source_file(auint i){ (void)i; return NULL; }
boole mainui_debug_source_lookup(auint a, mainui_debug_source_t* o){(void)a;(void)o;return FALSE;}
boole mainui_debug_source_resolve_line(auint f, auint l, mainui_debug_source_t* o){(void)f;(void)l;(void)o;return FALSE;}
#define BGET(n) boole n(void){return FALSE;}
#define UGET(n) auint n(void){return 0U;}
#define BSET(n) void n(boole v){(void)v;}
#define USET(n) void n(auint v){(void)v;}
#define SGET(n) char const* n(void){return "";}
#define SSET(n) void n(char const* v){(void)v;}
BGET(mainui_get_display_gameonly) BGET(mainui_get_display_fullscreen) BGET(mainui_get_system_messages) BGET(mainui_get_gui_gamepad_enable) UGET(mainui_get_gui_gamepad_speed_pct) BGET(mainui_get_gui_pause_while_open) BGET(mainui_get_gui_virtual_keyboard)
BGET(mainui_get_frame_rate_limiter) BGET(mainui_get_frame_merge) BGET(mainui_get_input_kbuzem) BGET(mainui_get_input_player2alloc) BGET(mainui_get_audio_freqscale) BGET(mainui_get_audio_s16) UGET(mainui_get_audio_output_rate) UGET(mainui_get_audio_latency) UGET(mainui_get_audio_resampler) BGET(mainui_get_audio_dcblock) UGET(mainui_get_audio_lowpass) UGET(mainui_get_audio_lowpass_quality) UGET(mainui_get_audio_monitor_mode) UGET(mainui_get_audio_monitor_width) UGET(mainui_get_audio_reverb) UGET(mainui_get_audio_master_volume) BGET(mainui_get_mouse_enable) UGET(mainui_get_mouse_scale) UGET(mainui_get_render_path) UGET(mainui_get_filter_pre_mode) UGET(mainui_get_filter_scale_mode) UGET(mainui_get_filter_crt_mode) UGET(mainui_get_spiram_pages_mode) BGET(mainui_get_sd_allow_new_files)
auint mainui_get_sd_timing_preset(void){return g_sd_model.preset;} void mainui_get_sd_timing_model(cu_spisd_model_t* o){if(o)*o=g_sd_model;}
SGET(mainui_get_rom_path) SGET(mainui_get_screenshot_path) SGET(mainui_get_save_path) SGET(mainui_get_controllerdb_path) BGET(mainui_get_resident_bootloader_enable) SGET(mainui_get_resident_bootloader_file) SGET(mainui_get_remote_roms_host) SGET(mainui_get_gui_theme_name) SGET(mainui_get_gui_theme_file) BGET(mainui_get_recent_roms_frozen) BGET(mainui_get_config_dirty) BGET(mainui_get_video_dump_active) SGET(mainui_get_video_dump_file) SGET(mainui_get_video_dump_status) BGET(mainui_get_video_dump_autoinc) BGET(mainui_get_video_dump_reset_first) BGET(mainui_get_input_capture_active) SGET(mainui_get_input_capture_file)
BSET(mainui_set_display_gameonly) BSET(mainui_set_display_fullscreen) BSET(mainui_set_system_messages) BSET(mainui_set_gui_gamepad_enable) USET(mainui_set_gui_gamepad_speed_pct) BSET(mainui_set_gui_pause_while_open) BSET(mainui_set_gui_virtual_keyboard) BSET(mainui_set_frame_rate_limiter) BSET(mainui_set_frame_merge) USET(mainui_set_render_path) USET(mainui_set_filter_pre_mode) USET(mainui_set_filter_scale_mode) USET(mainui_set_filter_crt_mode) BSET(mainui_set_input_kbuzem) BSET(mainui_set_input_player2alloc) BSET(mainui_set_mouse_enable) USET(mainui_set_mouse_scale) BSET(mainui_set_audio_freqscale) BSET(mainui_set_audio_s16) USET(mainui_set_audio_output_rate) USET(mainui_set_audio_latency) USET(mainui_set_audio_resampler) BSET(mainui_set_audio_dcblock) USET(mainui_set_audio_lowpass) USET(mainui_set_audio_lowpass_quality) USET(mainui_set_audio_monitor_mode) USET(mainui_set_audio_monitor_width) USET(mainui_set_audio_reverb) USET(mainui_set_audio_master_volume) USET(mainui_set_spiram_pages_mode) BSET(mainui_set_sd_allow_new_files) void mainui_set_sd_timing_preset(auint p){cu_spisd_model_preset(p,&g_sd_model);} void mainui_set_sd_timing_custom(cu_spisd_model_t const* m){if(m){g_sd_model=*m;g_sd_model.preset=CU_SPISD_PRESET_CUSTOM;}} void mainui_reset_sd_card(void){cu_spisd_reset(0U);} BSET(mainui_set_resident_bootloader_enable) SSET(mainui_set_rom_path) SSET(mainui_set_save_path) SSET(mainui_set_screenshot_path) SSET(mainui_set_controllerdb_path) SSET(mainui_set_resident_bootloader_file) SSET(mainui_set_remote_roms_host) SSET(mainui_set_gui_theme_file) SSET(mainui_set_video_dump_file) BSET(mainui_set_video_dump_active) BSET(mainui_set_video_dump_autoinc) BSET(mainui_set_video_dump_reset_first) BSET(mainui_set_input_capture_active) BSET(mainui_set_recent_roms_frozen)
boole mainui_get_recent_rom_path(auint i,char*o,auint n){(void)i;if(o&&n)o[0]=0;return FALSE;} boole mainui_load_recent_rom(auint i){(void)i;return FALSE;} void mainui_clear_recent_roms(void){} void mainui_config_save_now(void){} void mainui_config_reload_now(void){} void mainui_config_defaults_now(void){}
static auint g_save_slot=0U; void savestate_set_slot(auint s){g_save_slot=s%10U;} auint savestate_get_slot(void){return g_save_slot;} boole savestate_save_slot(auint s){(void)s;return TRUE;} boole savestate_load_slot(auint s){(void)s;return TRUE;} boole savestate_slot_exists(auint s){(void)s;return FALSE;}
#undef BGET
#undef UGET
#undef BSET
#undef USET
#undef SGET
#undef SSET

int main(int argc, char** argv){
    auint port = (argc > 1) ? (auint)strtoul(argv[1], NULL, 0) : 24680U;
    unsigned tick = 0U;
    g_data[0x120]=0x5AU; g_data[0x130]=7U; g_data[0x131]=9U; g_data[0x1FE]=0x33U;
    { auint i; for(i=0U;i<512U;i++) g_vfat_state.rwbuf[i]=(uint8)(i&0xFFU); }
    g_sd_state.ena=TRUE; g_sd_state.state=6U; g_sd_state.pstat=2U; g_sd_state.paddr=7U; g_sd_state.ppos=123U;
    g_sd_state.cmd=0x100U|17U; g_sd_state.crarg=0x12340000U; g_sd_state.evcnt=2U; g_sd_state.cc7v=0x2AU; g_sd_state.cc16v=0xBEEFU; g_sd_state.data=0x44U;
    api_server_configure(TRUE, port);
    while(!g_quit && tick < 10000U){
        api_server_tick();
        if((tick % 2U)==0U){ g_frame++; api_server_frame_end(); }
        TEST_SLEEP_MS(1U);
        tick++;
    }
    api_server_shutdown();
    return g_quit ? 0 : 3;
}
'''

SDL_STUB = r'''
#ifndef SDL_H_STUB
#define SDL_H_STUB
#include <stdint.h>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;
typedef int32_t SDL_JoystickID;
typedef struct SDL_Surface SDL_Surface; typedef struct SDL_Window SDL_Window; typedef struct SDL_Renderer SDL_Renderer; typedef struct SDL_Texture SDL_Texture;
typedef struct { int sym; uint16_t mod; } SDL_Keysym; typedef struct { SDL_Keysym keysym; } SDL_KeyboardEvent; typedef struct { uint32_t type; SDL_KeyboardEvent key; } SDL_Event;
#include <time.h>
static inline Uint32 SDL_GetTicks(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (Uint32)(t.tv_sec*1000ULL+t.tv_nsec/1000000ULL); }
#endif
'''


def recv_json(sock: socket.socket, timeout: float = 2.0) -> dict:
    sock.settimeout(timeout)
    data = bytearray()
    while True:
        b = sock.recv(1)
        if not b:
            raise RuntimeError("API connection closed")
        if b == b"\n":
            return json.loads(data.decode())
        data += b


def send(sock: socket.socket, command: str) -> None:
    sock.sendall(command.encode() + b"\n")


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def connect_retry(port: int) -> socket.socket:
    deadline = time.time() + 3.0
    while time.time() < deadline:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=0.2)
        except OSError:
            time.sleep(0.02)
    raise RuntimeError("mock CUzeBox API did not start")


def main() -> int:
    cc = os.environ.get("CCNAT") or os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        raise SystemExit("No host C compiler found")
    with tempfile.TemporaryDirectory(prefix="cuzebox-api-multi-") as td:
        d = pathlib.Path(td)
        (d / "SDL2").mkdir()
        (d / "SDL2" / "SDL.h").write_text(SDL_STUB, encoding="utf-8")
        elf = d / "fixture.elf"
        make_elf(elf)
        harness = d / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        exe = d / ("api_multi.exe" if os.name == "nt" else "api_multi")
        subprocess.run(
            [str(cc), "-std=gnu99", "-DENABLE_API_SERVER", "-DENABLE_DEBUGGER", "-DENABLE_SD_TRACE=1", "-DENABLE_BEAM_CAPTURE=1", "-DENABLE_BEAM_HISTORY=1", "-DHEADLESS=1", "-DFLAG_NOCONSOLE=1", "-I", str(d), "-I", str(ROOT), str(ROOT / "api_server.c"), str(ROOT / "debug_dwarf.c"), str(ROOT / "debug_timing.c"), str(ROOT / "debug_sd_fs_history.c"), str(ROOT / "debug_sd_timing_analysis.c"), str(harness), "-lm", "-o", str(exe)],
            check=True,
        )
        port = free_port()
        proc = subprocess.Popen([str(exe), str(port)])
        sockets: list[socket.socket] = []
        try:
            c1 = connect_retry(port); sockets.append(c1); h1 = recv_json(c1)
            c2 = connect_retry(port); sockets.append(c2); h2 = recv_json(c2)
            assert h1["max_clients"] == 16 and h2["max_clients"] == 16
            assert h1["client"] != h2["client"]

            # A blocked wait on one client must not head-of-line block another.
            send(c1, "WAIT_FRAME 40 200")
            send(c2, "PING")
            assert recv_json(c2)["reply"] == "PONG"

            c3 = connect_retry(port); sockets.append(c3); recv_json(c3)
            send(c2, "WAIT_FRAME 45 200")
            send(c3, "PING")
            assert recv_json(c3)["reply"] == "PONG"
            assert recv_json(c1)["wait"] == "frame"
            assert recv_json(c2)["wait"] == "frame"

            # A fourth client can subscribe to asynchronous events without
            # changing the command streams of the other clients.
            c4 = connect_retry(port); sockets.append(c4); recv_json(c4)
            send(c4, "SUBSCRIBE FRAME CPU")
            sub = recv_json(c4)
            assert sub["ok"] == 1 and sub["subscriptions"] != 0
            ev = recv_json(c4)
            assert ev.get("event") in ("FRAME", "CPU")

            # Reset from c3 must explicitly cancel c1's pending wait.
            send(c1, "WAIT_FRAME 100000 0")
            send(c3, "RESET")
            assert recv_json(c3)["ok"] == 1
            cancelled = recv_json(c1)
            assert cancelled.get("wait_cancelled") == 1

            send(c2, "GET_STATE")
            state = recv_json(c2)
            assert state["api_clients"] == 4 and state["api_max_clients"] == 16

            send(c3, "MEM_SIZE SRAM")
            ms = recv_json(c3)
            assert ms["ok"] == 1 and ms["region"] == "SRAM" and ms["size"] == 4096
            send(c3, "MEM_REGIONS")
            mr = recv_json(c3)
            assert mr["regions"]["SRAM"] == 4096 and mr["regions"]["IO"] == 256

            send(c3, "VIDEO_BEAM STATUS")
            assert recv_json(c3)["enabled"] == 0
            send(c3, "VIDEO_BEAM ENABLE")
            assert recv_json(c3)["enabled"] == 1
            send(c3, "VIDEO_BEAM")
            vb = recv_json(c3)
            assert vb["ok"] == 1 and vb["cycle"] == 321 and vb["line_cycles"] == 1820 and vb["active_cycles"] == 1440
            assert vb["row_pulse"] == 40 and vb["pulse_counter"] == 41
            assert vb["abs_cycle"] == 100000 and vb["line_start_abs"] == 99679
            assert vb["enabled"] == 1 and vb["valid_cycles"] == 321 and vb["pixels"][300] == (300 & 0xFF)
            send(c3, "VIDEO_BEAM DISABLE")
            assert recv_json(c3)["enabled"] == 0
            send(c3, "VIDEO_BEAM")
            vb_off = recv_json(c3)
            assert vb_off["enabled"] == 0 and vb_off["cycle"] == 321 and vb_off["row_pulse"] == 40
            assert vb_off["valid_cycles"] == 0 and vb_off["pixels"] == []
            send(c3, "BEAM_HISTORY STATUS")
            bh = recv_json(c3)
            assert bh["ok"] == 1 and bh["built"] == 1 and bh["enabled"] == 0 and bh["capacity"] == 4096
            send(c3, "BEAM_HISTORY ENABLE")
            assert recv_json(c3)["enabled"] == 1
            send(c3, "BEAM_HISTORY DISABLE")
            assert recv_json(c3)["enabled"] == 0
            send(c3, "MEM_TRACE STATUS")
            mt = recv_json(c3)
            assert mt["ok"] == 1 and mt["built"] == 1 and mt["enabled"] == 0 and mt["capacity"] == 4096
            send(c3, "MEM_TRACE ENABLE")
            assert recv_json(c3)["enabled"] == 1
            send(c3, "MEM_TRACE DISABLE")
            assert recv_json(c3)["enabled"] == 0
            send(c3, "RASTER_BREAK SET 40 299")
            rb = recv_json(c3)
            assert rb["ok"] == 1 and rb["armed"] == 1 and rb["row"] == 40 and rb["cycle"] == 299
            send(c3, "RASTER_BREAK STATUS")
            rb = recv_json(c3)
            assert rb["enabled"] == 1 and rb["hit"] == 0 and rb["mode"] == "CYCLE"
            send(c3, "RASTER_BREAK EVENT RISE ANY")
            rb = recv_json(c3)
            assert rb["armed"] == 1 and rb["mode"] == "SYNC_RISE" and rb["any_row"] == 1
            send(c3, "RASTER_BREAK STATUS")
            rb = recv_json(c3)
            assert rb["enabled"] == 1 and rb["mode"] == "SYNC_RISE"
            send(c3, "LOGIC_TRACE ENABLE")
            assert recv_json(c3)["enabled"] == 1
            send(c3, "LOGIC_TRACE STATUS")
            lt = recv_json(c3)
            assert lt["enabled"] == 1 and lt["cpu_hz"] == 28636360
            assert lt["uart_tx_capture"] == 1 and lt["uart_rx_capture"] == 1
            assert (lt["mask"] & (1 << 1)) == 0 and (lt["mask"] & (1 << 16)) != 0 and lt["mask"] == lt["default_mask"]
            send(c3, "LOGIC_TRACE MASK ALL")
            lt = recv_json(c3)
            assert lt["ok"] == 1 and (lt["mask"] & (1 << 1)) != 0
            send(c3, "LOGIC_TRACE MASK NONE")
            lt = recv_json(c3)
            assert lt["ok"] == 1 and lt["mask"] == 0
            send(c3, "LOGIC_TRACE STATUS")
            lt = recv_json(c3)
            assert lt["uart_tx_capture"] == 0 and lt["uart_rx_capture"] == 0
            send(c3, "LOGIC_TRACE MASK DEFAULT")
            lt = recv_json(c3)
            assert lt["ok"] == 1 and (lt["mask"] & (1 << 1)) == 0 and (lt["mask"] & (1 << 16)) != 0
            send(c3, "SCANLINE_PROFILE ENABLE")
            assert recv_json(c3)["enabled"] == 1
            send(c3, "SCANLINE_PROFILE STATUS")
            assert recv_json(c3)["enabled"] == 1
            send(c3, "RASTER_BREAK CLEAR")
            assert recv_json(c3)["ok"] == 1
            send(c3, "PAUSE")
            assert recv_json(c3)["ok"] == 1
            send(c3, "SCREENSHOT_CAPTURE")
            sc = recv_json(c3)
            assert sc["ok"] == 1 and sc["size"] == 66 and sc["format"] == "bmp24"
            send(c3, "SCREENSHOT_READ 0 66")
            sr = recv_json(c3)
            assert sr["values"][:4] == [66, 77, 0, 1]
            send(c3, "SCREENSHOT_CLEAR")
            assert recv_json(c3)["ok"] == 1

            send(c3, "EMU_STATUS")
            es = recv_json(c3)
            assert es["ok"] == 1 and len(es["save_slots"]) == 10
            send(c3, "SD_STATUS")
            sd = recv_json(c3)
            assert sd["ok"] == 1 and sd["model"]["preset_name"] == "NORMAL"
            assert sd["model"]["init_ms"] == 500 and sd["model"]["read_wait_bytes"] == 2
            assert sd["cpu_cycle"] == 100000 and sd["card"]["state_name"] == "AVAILABLE"
            assert sd["card"]["packet_state_name"] == "READ DATA" and sd["card"]["packet_pos"] == 123 and sd["card"]["r1_flags"] == "OK"
            assert sd["spi"]["active"] == 1 and sd["spi"]["remaining_cycles"] == 16
            assert sd["spi"]["duration_cycles"] == 16 and sd["spi"]["elapsed_cycles"] == 0
            assert sd["spi"]["tx"] == 0xA5 and sd["spi"]["rx"] == 0x5A and sd["spi"]["spdr"] == 0x3C
            pcmd = sd["protocol"]["command"]
            assert pcmd["active"] == 1 and pcmd["cmd"] == 17 and pcmd["known_mask"] == 7 and pcmd["next_index"] == 3
            assert pcmd["bytes"][:3] == [0x51, 0x12, 0x34] and pcmd["crc_expected_valid"] == 0
            pdata = sd["protocol"]["data"]
            assert sd["protocol"]["response"]["phase"] == "IDLE" and pdata["active"] == 1 and pdata["direction"] == "READ"
            assert pdata["phase"] == "DATA" and pdata["data_done"] == 123 and pdata["data_total"] == 512 and pdata["crc_calculated"] == 0xBEEF
            send(c3, "SD_PAYLOAD CURRENT 64 32 COMPARE")
            pay = recv_json(c3)
            assert pay["ok"] == 1 and pay["built"] == 1 and pay["sector"] == sd["card"]["sector"]
            assert pay["start"] == 64 and pay["count"] == 32 and pay["stream_valid"] == 1 and pay["backing_valid"] == 1
            assert pay["cursor_offset"] == 123 and pay["last_offset"] == 122 and pay["diff_count"] == 1 and pay["source"] == "TEST.BIN"
            assert pay["fs"]["role"] == "FILE" and pay["fs"]["cluster"] == 9 and pay["fs"]["sector_in_cluster"] == 1
            assert pay["fs"]["start_cluster"] == 8 and pay["fs"]["next_cluster"] == 0xFFFF and pay["fs"]["entry_sector"] == 513
            assert pay["stream"][:4] == [64,65,66,67] and pay["backing"][2] == (66 ^ 0xFF)
            send(c3, "SD_TRACE STATUS")
            sdtr = recv_json(c3)
            assert sdtr["ok"] == 1 and sdtr["built"] == 0 and sdtr["enabled"] == 0 and sdtr["active"] == 0
            send(c3, "SD_FS_HISTORY 64")
            fsh = recv_json(c3)
            assert fsh["ok"] == 1 and fsh["built"] == 0 and fsh["count"] == 0 and fsh["operations"] == []
            send(c3, "SD_TIMING_ANALYSIS 16")
            sta = recv_json(c3)
            assert sta["ok"] == 1 and sta["built"] == 0 and sta["count"] == 0 and sta["samples"] == []
            send(c3, "SD_FAULT STATUS")
            sdf = recv_json(c3)
            assert sdf["ok"] == 1 and sdf["built"] == 0 and sdf["active"] == 0 and sdf["capacity"] == 8 and len(sdf["rules"]) == 8
            send(c3, "SD_FAULT SET 0 REJECT 2 ONCE CMD 17 SECTOR 7")
            sdf = recv_json(c3)
            assert sdf["ok"] == 0 and "not built" in sdf["error"].lower()
            send(c3, "SD_PRESET SLOW")
            sd = recv_json(c3)
            assert sd["model"]["preset_name"] == "SLOW" and sd["model"]["init_ms"] == 1000
            send(c3, "SD_SET intersector_wait_bytes 13")
            sd = recv_json(c3)
            assert sd["model"]["preset_name"] == "CUSTOM" and sd["model"]["read_wait_bytes"] == 13
            send(c3, "SD_PRESET NORMAL")
            sd = recv_json(c3)
            assert sd["model"]["preset_name"] == "NORMAL" and sd["model"]["read_wait_bytes"] == 2
            send(c3, "AUDIO_SCOPE STATUS")
            ast = recv_json(c3)
            assert ast["ok"] == 1 and ast["output_capture"] == 0 and ast["source_rate"] == 15734
            send(c3, "AUDIO_SCOPE SOURCE 32")
            asc = recv_json(c3)
            assert asc["ok"] == 1 and asc["count"] == 32 and asc["samples"][:4] == [0, 1, 2, 3]
            send(c3, "AUDIO_SCOPE ENABLE")
            ast = recv_json(c3)
            assert ast["output_capture"] == 1
            send(c3, "AUDIO_SCOPE OUTPUT 32")
            aout = recv_json(c3)
            assert aout["ok"] == 1 and aout["count"] == 32 and aout["left"][0] == -32768 and aout["right"][1] == 32767
            send(c3, "AUDIO_SCOPE DISABLE")
            assert recv_json(c3)["output_capture"] == 0
            send(c3, "EMU_SET audio_volume 73")
            assert recv_json(c3)["ok"] == 1
            send(c3, "SAVESTATE SLOT 3")
            assert recv_json(c3)["ok"] == 1
            send(c3, "SAVESTATE SAVE 3")
            assert recv_json(c3)["saved"] == 1
            send(c3, "CONFIG SAVE")
            assert recv_json(c3)["ok"] == 1

            # Load the synthetic AVR DWARF fixture through the public API and
            # verify the variable/type surface, including a paused scalar write.
            send(c3, f"LOAD_SYMBOLS {elf}")
            loaded = recv_json(c3)
            assert loaded["ok"] == 1 and loaded["dwarf_variables"] >= 7
            send(c3, "DWARF_STATUS")
            ds = recv_json(c3)
            assert ds["globals"] == 5 and ds["locals"] == 2 and ds["types"] >= 5
            send(c3, "DWARF_GLOBALS 0 8")
            dg = recv_json(c3)
            assert dg["total"] == 5 and dg["variables"][0]["name"] == "counter" and dg["variables"][0]["value_u"] == 0x5A
            send(c3, "DWARF_LOCALS PC 0 8")
            dl = recv_json(c3)
            assert dl["total"] == 2 and {v["name"] for v in dl["variables"]} == {"p", "local"}
            counter_id = dg["variables"][0]["id"]
            send(c3, f"DWARF_WRITE {counter_id} 0x66 PC")
            assert recv_json(c3)["ok"] == 1
            send(c3, f"DWARF_VALUE {counter_id} PC")
            assert recv_json(c3)["variable"]["value_u"] == 0x66

            send(c3, "QUIT")
            assert recv_json(c3)["ok"] == 1
            assert proc.wait(timeout=3.0) == 0
        finally:
            for s in sockets:
                s.close()
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=3.0)
    print("API multi-client regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
