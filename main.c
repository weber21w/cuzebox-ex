/*
 *  Main
 *
 *  Copyright (C) 2016 - 2017
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


#include "types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <direct.h>
#define MAIN_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MAIN_MKDIR(path) mkdir(path, 0777)
#endif
#include "cu_ufile.h"
#include "cu_hfile.h"
#include "cu_avr.h"
#include "bootloader_builtin.h"
#include "cu_avrc.h"
#include "cu_ctr.h"
#include "cu_spir.h"
#include "cu_spisd.h"
#include "cu_kbd.h"
#include "cu_mouse.h"
#include "cu_gun.h"
#include "cu_multitap.h"
#include "cu_esp.h"
#include "midi.h"
#include "filesys.h"
#include "guicore.h"
#include "audio.h"
#include "ginput.h"
#include "frame.h"
#include "netplay.h"
#include "netplay_host.h"
#include "eepdump.h"
#include "romdump.h"
#include "textgui.h"
#include "configcfg.h"
#include "mainui.h"
#include "renderpath.h"
#include "cheats.h"
#include "savestate.h"
#include "rollback.h"
#include "remote_roms.h"
#include "api_server.h"
#include "web_server.h"
#include "debug_source.h"
#include "debug_dwarf.h"
#ifdef ENABLE_VCAP
#include "avconv.h"
#endif
#ifdef ENABLE_ICAP
#include "capture.h"
#endif
#ifdef ENABLE_MICROUI
#include "microui/mui_integration.h"
#endif


/* Initial title */
static const char main_title[] = "CUzeBox";

static const char main_cfgfile[] = "config.cfg";
static app_config_t main_cfg;
static boole        main_cfg_dirty = FALSE;
static boole        main_runtime_apply_pending = FALSE;
static char         main_gui_theme_name[32] = "BOOTLOADER";
static char         main_gui_theme_file[APPCFG_PATH_MAX] = "themes/bootloader.cfg";

static boole main_load_rom_file_internal(char const* path);
static char const* main_path_basename(char const* path);
static void main_copy_str(char* dst, auint cap, char const* src);
static void main_join_path(char* dst, auint cap, char const* a, char const* b);
static boole main_mkdirs(char const* path);
static auint main_resolve_spiram_banks(boole is_uze, auint hint_banks);
static void main_apply_spiram_policy(boole is_uze, auint hint_banks);
static void main_apply_esp_softap_policy(boole is_uze, cu_ufile_header_t const* ufhead);
static void main_seed_builtin_bootloader(uint8* crom);
static boole main_try_external_bootloader_override(uint8* crom);
static boole main_resident_bootloader_boot_priority(uint8 const* crom);
static void main_cfg_capture_virtual_runtime(void);
static void main_cfg_apply_virtual_runtime(void);
static void main_gui_pause_tick(void);
static boole mainui_input_profile_save_current(void);
static boole mainui_input_profile_load_current(void);
static void main_system_message(char const* fmt, ...);
static void main_system_error(char const* fmt, ...);
static boole main_cheat_entry_equal(cheat_entry_t const* a, cheat_entry_t const* b);
static char const* main_cheat_entry_title(cheat_entry_t const* ent, char* buf, auint size);
static char const* main_watchpoint_region_name(auint region);
static char const* main_watchpoint_mode_name(auint flags);
static auint main_watchpoint_digits(auint region);

/* Exit request */
static boole main_exit = FALSE;

/* 60Hz main loop timing to coarsely maintain 60Hz output */
static auint main_tdrift = 0U;

/* Previous millisecond tick */
static auint main_ptick;

/* 1/3 counter to get the 16.6667ms average frame time */
static auint main_tfrac = 0U;

/* Frame counter for frame dropping and limiting */
static auint main_frc = 0U;

/* Frame dropping tendency; if nonzero, it drops frames */
static auint main_fdrop = 0U;

/* 500 millisecond counter to generate FPS info */
static auint main_t500 = 0U;

/* Current frame counter during the 500ms tick */
static auint main_t5_frc = 0U;

/* Previous frame count for the 500ms tick */
static auint main_t5_frp = 30U;

/* Previous cycle counter value for the 500ms tick */
static auint main_t5_cc;

/* Request for discarding frame limitation */
static boole main_nolimit = FALSE;
static boole main_fast_flash_active = FALSE;
static auint main_fast_flash_present_tick;

/* Audio frequency scaling state */
static boole main_audio_freqscale = TRUE;

/* Request frame merging (flicker reduction) */
#ifdef FLAG_DISPLAY_FRAMEMERGE
static boole main_fmerge = TRUE;
#else
static boole main_fmerge = FALSE;
#endif

/* Previous state of EEPROM changes to save only after a write burst was completed */
static boole main_peepch = FALSE;

/* Previous state of Code ROM changes to save only after a write burst was completed */
static boole main_promch = FALSE;

/* Pause / single-frame-advance state */
static boole main_ispause = FALSE;
static boole main_isadvfr = FALSE;
static char              main_input_profile_dir[APPCFG_PATH_MAX] = "";
static char              main_input_profile_status[160] = "Input profile idle.";
static boole             main_input_profile_dirty = FALSE;

#ifdef ENABLE_DEBUGGER
static boole main_dbg_break_hit = FALSE;
static auint main_dbg_break_addr = 0U;
#define MAIN_DBG_SYMBOL_CAP 4096U
#define MAIN_DBG_SYMBOL_NAME_CAP 64U
#define MAIN_DBG_SYMBOL_KIND_PROG 0U
#define MAIN_DBG_SYMBOL_KIND_DATA 1U
typedef struct{
	uint8 kind;
	uint16 addr;
	char name[MAIN_DBG_SYMBOL_NAME_CAP];
} main_dbg_symbol_t;
static main_dbg_symbol_t main_dbg_symbols[MAIN_DBG_SYMBOL_CAP];
static auint             main_dbg_symbol_count = 0U;
static char              main_dbg_symbol_file[APPCFG_PATH_MAX] = "";
static char              main_dbg_symbol_status[128] = "No symbols loaded.";
static char              main_dbg_label_temp[MAIN_DBG_SYMBOL_NAME_CAP];
static char              main_dbg_profile_dir[APPCFG_PATH_MAX] = "";
static char              main_dbg_profile_status[160] = "Debugger profile idle.";
static boole             main_dbg_profile_dirty = FALSE;
static boole             main_dbg_profile_loaded = FALSE;
static char              main_dbg_run_status[96] = "Idle.";
static void              mainui_debug_run_status_set(char const* text);
static void              mainui_debug_nav_clear(void);
static char const*       mainui_debug_io_name(auint addr);
static void              mainui_debug_report_break(auint addr);
#endif

static char        main_cheatfile[CHEAT_PATH_MAX] = "";
static char              main_controllerdb_loaded[APPCFG_PATH_MAX] = "";
static char        main_loaded_rom_path[APPCFG_PATH_MAX] = "";

#ifdef ENABLE_VCAP
static boole main_isvcap = FALSE;
#endif
#ifdef ENABLE_ICAP
static boole main_input_capture_active = TRUE;
#endif

/* Current UzeRom metadata for host-side netplay ROM sync */
static cu_ufile_header_t main_rom_head;
static boole             main_rom_is_uze = FALSE;
static char              main_rom_name[64] = "";
static uint32            main_frame_counter = 0U;

#define MAIN_INPUT_TRACE_CAP       512U
#define MAIN_INPUT_TRACE_LINE_CAP  320U

static boole             main_input_trace_enabled = FALSE;
static auint             main_input_trace_head = 0U;
static auint             main_input_trace_count = 0U;
static char              main_input_trace_lines[MAIN_INPUT_TRACE_CAP][MAIN_INPUT_TRACE_LINE_CAP];
static boole             main_input_trace_ctx_valid = FALSE;
static auint             main_input_trace_ctx_frame = 0U;
static char const*       main_input_trace_ctx_phase = "LIVE";
static boole             main_suppress_rom_load_system_message = FALSE;
static boole             main_suppress_watchpoint_system_messages = FALSE;
static boole             main_gui_pause_owned = FALSE;

/* Runtime netplay gameplay sync state */
static boole             main_np_session_active = FALSE;
static boole             main_np_last_predicted = FALSE;
static boole             main_np_last_stalled = FALSE;
static boole             main_np_last_resim = FALSE;
static auint             main_np_frame = 0U;

static void main_netplay_map_logical_player(auint logical_player, auint *port, auint *slot);

static void main_input_trace_pushf(char const* fmt, ...)
{
	va_list ap;
	char* dst;
	if (!main_input_trace_enabled){
		return;
	}
	dst = main_input_trace_lines[(main_input_trace_head + main_input_trace_count) % MAIN_INPUT_TRACE_CAP];
	va_start(ap, fmt);
	(void)vsnprintf(dst, MAIN_INPUT_TRACE_LINE_CAP, fmt, ap);
	va_end(ap);
	if (main_input_trace_count < MAIN_INPUT_TRACE_CAP){
		main_input_trace_count++;
	}else{
		main_input_trace_head = (main_input_trace_head + 1U) % MAIN_INPUT_TRACE_CAP;
	}
}

static void main_input_trace_log_frame_summary(auint frame, char const* phase)
{
	char line[MAIN_INPUT_TRACE_LINE_CAP];
	int pos;
	auint port;
	if (!main_input_trace_enabled){
		return;
	}
	pos = snprintf(line, sizeof(line), "F%u %s P1[S%u%s] P2[S%u%s]",
		(unsigned)frame,
		(phase != NULL) ? phase : "LIVE",
		(unsigned)(cu_multitap_get_active_slot(0U) & 0x03U),
		cu_multitap_get_probe_pending(0U) ? "/PROBE" : "",
		(unsigned)(cu_multitap_get_active_slot(1U) & 0x03U),
		cu_multitap_get_probe_pending(1U) ? "/PROBE" : "");
	for (port = 0U; port < ROLLBACK_MAX_PLAYERS; ++port){
		auint p;
		auint s;
		auint packet = 0U;
		auint bits = 16U;
		main_netplay_map_logical_player(port, &p, &s);
		(void)cu_ctr_getslot_packet_state(p, s, &packet, &bits);
		pos += snprintf(line + ((pos < (int)sizeof(line)) ? pos : (int)sizeof(line)),
			(pos < (int)sizeof(line)) ? (sizeof(line) - (size_t)pos) : 0U,
			" P%uS%u=%0*X/%u",
			(unsigned)(p + 1U),
			(unsigned)s,
			(bits > 16U) ? 8 : 4,
			(unsigned)packet,
			(unsigned)bits);
	}
	main_input_trace_pushf("%s", line);
}

static void main_input_trace_log_device_event(auint frame, rollback_device_event_t const* ev, char const* phase)
{
	char payload[64];
	char const* name = "UNKNOWN";
	auint i;
	int pos = 0;
	if ((!main_input_trace_enabled) || (ev == NULL)){
		return;
	}
	payload[0] = 0;
	for (i = 0U; (i < ev->len) && (i < ROLLBACK_DEVICE_EVENT_DATA_MAX); ++i){
		pos += snprintf(payload + ((pos < (int)sizeof(payload)) ? pos : (int)sizeof(payload)),
			(pos < (int)sizeof(payload)) ? (sizeof(payload) - (size_t)pos) : 0U,
			(i == 0U) ? "%02X" : " %02X",
			(unsigned)ev->data[i]);
	}
	switch (ev->type){
		case ROLLBACK_DEV_EVT_TAP_SELECT: name = "TAP_SELECT"; break;
		case ROLLBACK_DEV_EVT_TAP_PROBE:  name = "TAP_PROBE";  break;
		case ROLLBACK_DEV_EVT_KBD_BYTES:  name = "KBD_BYTES";  break;
		default: break;
	}
	main_input_trace_pushf("F%u %s EVT %s P%u S%u seq=%u%s%s",
		(unsigned)frame,
		(phase != NULL) ? phase : "LIVE",
		name,
		(unsigned)(ev->port + 1U),
		(unsigned)(ev->slot & 0x03U),
		(unsigned)ev->seq,
		(payload[0] != 0) ? " data=" : "",
		payload);
}

static void main_input_trace_kbd_visible_byte(auint port, uint8 value)
{
	auint frame = main_input_trace_ctx_valid ? main_input_trace_ctx_frame : (main_np_session_active ? main_np_frame : (auint)main_frame_counter);
	char const* phase = main_input_trace_ctx_valid ? main_input_trace_ctx_phase : "LIVE";
	main_input_trace_pushf("F%u %s KBD_VISIBLE P%u %02X",
		(unsigned)frame,
		phase,
		(unsigned)(port + 1U),
		(unsigned)value);
}

#ifdef ENABLE_MICROUI
static void main_cfg_capture_gui_runtime(void)
{
	mu_Style style;
	auint    i;
	main_copy_str(main_cfg.gui_theme_name, sizeof(main_cfg.gui_theme_name), main_gui_theme_name);
	main_copy_str(main_cfg.gui_theme_file, sizeof(main_cfg.gui_theme_file), main_gui_theme_file);
	if (!mui_get_style_snapshot(&style)){ return; }
	for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
		main_cfg.gui_colors[i][0] = style.colors[i].r;
		main_cfg.gui_colors[i][1] = style.colors[i].g;
		main_cfg.gui_colors[i][2] = style.colors[i].b;
		main_cfg.gui_colors[i][3] = style.colors[i].a;
	}
}

static void main_cfg_apply_gui_runtime(void)
{
	mu_Style style;
	auint    i;
	if (!mui_get_style_snapshot(&style)){ return; }
	style.padding = (int)APPCFG_GUI_PADDING;
	style.spacing = (int)APPCFG_GUI_SPACING;
	style.title_height = (int)APPCFG_GUI_TITLE_HEIGHT;
	style.scrollbar_size = (int)APPCFG_GUI_SCROLLBAR_SIZE;
	style.thumb_size = (int)APPCFG_GUI_THUMB_SIZE;
	for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
		style.colors[i].r = main_cfg.gui_colors[i][0];
		style.colors[i].g = main_cfg.gui_colors[i][1];
		style.colors[i].b = main_cfg.gui_colors[i][2];
		style.colors[i].a = main_cfg.gui_colors[i][3];
	}
	mui_apply_style_snapshot(&style);
}
static boole main_gui_theme_name_eq(char const* a, char const* b)
{
	if ((a == NULL) || (b == NULL)){ return FALSE; }
	while ((*a != 0) && (*b != 0)){
		char ca = *a;
		char cb = *b;
		if ((ca >= 'a') && (ca <= 'z')){ ca = (char)(ca - ('a' - 'A')); }
		if ((cb >= 'a') && (cb <= 'z')){ cb = (char)(cb - ('a' - 'A')); }
		if (ca != cb){ return FALSE; }
		a++;
		b++;
	}
	return ((*a == 0) && (*b == 0));
}

static void main_gui_theme_apply_neutral(void)
{
	main_cfg.gui_colors[0][0] = 230U; main_cfg.gui_colors[0][1] = 230U; main_cfg.gui_colors[0][2] = 230U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 25U;  main_cfg.gui_colors[1][1] = 25U;  main_cfg.gui_colors[1][2] = 25U;  main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 50U;  main_cfg.gui_colors[2][1] = 50U;  main_cfg.gui_colors[2][2] = 50U;  main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 25U;  main_cfg.gui_colors[3][1] = 25U;  main_cfg.gui_colors[3][2] = 25U;  main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 240U; main_cfg.gui_colors[4][1] = 240U; main_cfg.gui_colors[4][2] = 240U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 75U;  main_cfg.gui_colors[6][1] = 75U;  main_cfg.gui_colors[6][2] = 75U;  main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 95U;  main_cfg.gui_colors[7][1] = 95U;  main_cfg.gui_colors[7][2] = 95U;  main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 115U; main_cfg.gui_colors[8][1] = 115U; main_cfg.gui_colors[8][2] = 115U; main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 30U;  main_cfg.gui_colors[9][1] = 30U;  main_cfg.gui_colors[9][2] = 30U;  main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 35U; main_cfg.gui_colors[10][1] = 35U; main_cfg.gui_colors[10][2] = 35U; main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 40U; main_cfg.gui_colors[11][1] = 40U; main_cfg.gui_colors[11][2] = 40U; main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 43U; main_cfg.gui_colors[12][1] = 43U; main_cfg.gui_colors[12][2] = 43U; main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 30U; main_cfg.gui_colors[13][1] = 30U; main_cfg.gui_colors[13][2] = 30U; main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_bootloader(void)
{
	main_cfg.gui_colors[0][0] = 255U; main_cfg.gui_colors[0][1] = 232U; main_cfg.gui_colors[0][2] = 196U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 52U;  main_cfg.gui_colors[1][1] = 24U;  main_cfg.gui_colors[1][2] = 0U;   main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 86U;  main_cfg.gui_colors[2][1] = 44U;  main_cfg.gui_colors[2][2] = 8U;   main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 68U;  main_cfg.gui_colors[3][1] = 28U;  main_cfg.gui_colors[3][2] = 0U;   main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 255U; main_cfg.gui_colors[4][1] = 244U; main_cfg.gui_colors[4][2] = 220U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 132U; main_cfg.gui_colors[6][1] = 72U;  main_cfg.gui_colors[6][2] = 16U;  main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 160U; main_cfg.gui_colors[7][1] = 96U;  main_cfg.gui_colors[7][2] = 28U;  main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 188U; main_cfg.gui_colors[8][1] = 124U; main_cfg.gui_colors[8][2] = 48U;  main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 58U;  main_cfg.gui_colors[9][1] = 32U;  main_cfg.gui_colors[9][2] = 6U;   main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 70U; main_cfg.gui_colors[10][1] = 38U; main_cfg.gui_colors[10][2] = 10U;  main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 84U; main_cfg.gui_colors[11][1] = 46U; main_cfg.gui_colors[11][2] = 16U;  main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 98U; main_cfg.gui_colors[12][1] = 58U; main_cfg.gui_colors[12][2] = 18U;  main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 70U; main_cfg.gui_colors[13][1] = 38U; main_cfg.gui_colors[13][2] = 10U;  main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_chill(void)
{
	main_cfg.gui_colors[0][0] = 220U; main_cfg.gui_colors[0][1] = 236U; main_cfg.gui_colors[0][2] = 255U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 20U;  main_cfg.gui_colors[1][1] = 34U;  main_cfg.gui_colors[1][2] = 52U;  main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 26U;  main_cfg.gui_colors[2][1] = 54U;  main_cfg.gui_colors[2][2] = 80U;  main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 22U;  main_cfg.gui_colors[3][1] = 44U;  main_cfg.gui_colors[3][2] = 68U;  main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 238U; main_cfg.gui_colors[4][1] = 248U; main_cfg.gui_colors[4][2] = 255U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 40U;  main_cfg.gui_colors[6][1] = 94U;  main_cfg.gui_colors[6][2] = 132U; main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 50U;  main_cfg.gui_colors[7][1] = 116U; main_cfg.gui_colors[7][2] = 160U; main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 64U;  main_cfg.gui_colors[8][1] = 138U; main_cfg.gui_colors[8][2] = 188U; main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 18U;  main_cfg.gui_colors[9][1] = 42U;  main_cfg.gui_colors[9][2] = 62U;  main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 22U; main_cfg.gui_colors[10][1] = 50U; main_cfg.gui_colors[10][2] = 74U;  main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 26U; main_cfg.gui_colors[11][1] = 58U; main_cfg.gui_colors[11][2] = 84U;  main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 28U; main_cfg.gui_colors[12][1] = 68U; main_cfg.gui_colors[12][2] = 96U;  main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 22U; main_cfg.gui_colors[13][1] = 50U; main_cfg.gui_colors[13][2] = 74U;  main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_rave(void)
{
	main_cfg.gui_colors[0][0] = 255U; main_cfg.gui_colors[0][1] = 240U; main_cfg.gui_colors[0][2] = 255U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 32U;  main_cfg.gui_colors[1][1] = 0U;   main_cfg.gui_colors[1][2] = 40U;  main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 50U;  main_cfg.gui_colors[2][1] = 0U;   main_cfg.gui_colors[2][2] = 74U;  main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 26U;  main_cfg.gui_colors[3][1] = 0U;   main_cfg.gui_colors[3][2] = 54U;  main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 255U; main_cfg.gui_colors[4][1] = 242U; main_cfg.gui_colors[4][2] = 255U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 100U; main_cfg.gui_colors[6][1] = 0U;   main_cfg.gui_colors[6][2] = 170U; main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 140U; main_cfg.gui_colors[7][1] = 0U;   main_cfg.gui_colors[7][2] = 220U; main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 180U; main_cfg.gui_colors[8][1] = 32U;  main_cfg.gui_colors[8][2] = 255U; main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 24U;  main_cfg.gui_colors[9][1] = 0U;   main_cfg.gui_colors[9][2] = 48U;  main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 32U; main_cfg.gui_colors[10][1] = 0U;  main_cfg.gui_colors[10][2] = 64U;  main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 40U; main_cfg.gui_colors[11][1] = 0U;  main_cfg.gui_colors[11][2] = 78U;  main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 56U; main_cfg.gui_colors[12][1] = 0U;  main_cfg.gui_colors[12][2] = 88U;  main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 32U; main_cfg.gui_colors[13][1] = 0U;  main_cfg.gui_colors[13][2] = 64U;  main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_snow(void)
{
	main_cfg.gui_colors[0][0] = 28U;  main_cfg.gui_colors[0][1] = 40U;  main_cfg.gui_colors[0][2] = 56U;  main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 150U; main_cfg.gui_colors[1][1] = 166U; main_cfg.gui_colors[1][2] = 188U; main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 232U; main_cfg.gui_colors[2][1] = 238U; main_cfg.gui_colors[2][2] = 246U; main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 206U; main_cfg.gui_colors[3][1] = 220U; main_cfg.gui_colors[3][2] = 236U; main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 18U;  main_cfg.gui_colors[4][1] = 30U;  main_cfg.gui_colors[4][2] = 44U;  main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 196U; main_cfg.gui_colors[6][1] = 210U; main_cfg.gui_colors[6][2] = 228U; main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 214U; main_cfg.gui_colors[7][1] = 226U; main_cfg.gui_colors[7][2] = 240U; main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 228U; main_cfg.gui_colors[8][1] = 236U; main_cfg.gui_colors[8][2] = 248U; main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 242U; main_cfg.gui_colors[9][1] = 246U; main_cfg.gui_colors[9][2] = 252U; main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 248U; main_cfg.gui_colors[10][1] = 250U; main_cfg.gui_colors[10][2] = 254U; main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 255U; main_cfg.gui_colors[11][1] = 255U; main_cfg.gui_colors[11][2] = 255U; main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 222U; main_cfg.gui_colors[12][1] = 230U; main_cfg.gui_colors[12][2] = 240U; main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 188U; main_cfg.gui_colors[13][1] = 198U; main_cfg.gui_colors[13][2] = 214U; main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_d3thadd3r(void)
{
	main_cfg.gui_colors[0][0] = 255U; main_cfg.gui_colors[0][1] = 255U; main_cfg.gui_colors[0][2] = 255U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 64U;  main_cfg.gui_colors[1][1] = 0U;   main_cfg.gui_colors[1][2] = 0U;   main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 0U;   main_cfg.gui_colors[2][1] = 0U;   main_cfg.gui_colors[2][2] = 0U;   main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 96U;  main_cfg.gui_colors[3][1] = 0U;   main_cfg.gui_colors[3][2] = 0U;   main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 255U; main_cfg.gui_colors[4][1] = 255U; main_cfg.gui_colors[4][2] = 255U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 96U;  main_cfg.gui_colors[6][1] = 0U;   main_cfg.gui_colors[6][2] = 0U;   main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 144U; main_cfg.gui_colors[7][1] = 0U;   main_cfg.gui_colors[7][2] = 0U;   main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 192U; main_cfg.gui_colors[8][1] = 0U;   main_cfg.gui_colors[8][2] = 0U;   main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 0U;   main_cfg.gui_colors[9][1] = 0U;   main_cfg.gui_colors[9][2] = 0U;   main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 12U; main_cfg.gui_colors[10][1] = 0U;  main_cfg.gui_colors[10][2] = 0U;   main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 36U; main_cfg.gui_colors[11][1] = 0U;  main_cfg.gui_colors[11][2] = 0U;   main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 72U; main_cfg.gui_colors[12][1] = 0U;  main_cfg.gui_colors[12][2] = 0U;   main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 20U; main_cfg.gui_colors[13][1] = 20U; main_cfg.gui_colors[13][2] = 20U; main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_crt_amber(void)
{
	main_cfg.gui_colors[0][0] = 255U; main_cfg.gui_colors[0][1] = 212U; main_cfg.gui_colors[0][2] = 128U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 24U;  main_cfg.gui_colors[1][1] = 14U;  main_cfg.gui_colors[1][2] = 0U;   main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 38U;  main_cfg.gui_colors[2][1] = 22U;  main_cfg.gui_colors[2][2] = 0U;   main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 30U;  main_cfg.gui_colors[3][1] = 18U;  main_cfg.gui_colors[3][2] = 0U;   main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 255U; main_cfg.gui_colors[4][1] = 236U; main_cfg.gui_colors[4][2] = 192U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 108U; main_cfg.gui_colors[6][1] = 64U;  main_cfg.gui_colors[6][2] = 8U;   main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 132U; main_cfg.gui_colors[7][1] = 82U;  main_cfg.gui_colors[7][2] = 18U;  main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 160U; main_cfg.gui_colors[8][1] = 108U; main_cfg.gui_colors[8][2] = 32U;  main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 26U;  main_cfg.gui_colors[9][1] = 16U;  main_cfg.gui_colors[9][2] = 0U;   main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 32U; main_cfg.gui_colors[10][1] = 20U; main_cfg.gui_colors[10][2] = 0U;   main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 42U; main_cfg.gui_colors[11][1] = 28U; main_cfg.gui_colors[11][2] = 4U;   main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 52U; main_cfg.gui_colors[12][1] = 34U; main_cfg.gui_colors[12][2] = 6U;   main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 34U; main_cfg.gui_colors[13][1] = 20U; main_cfg.gui_colors[13][2] = 2U;   main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_terminal_green(void)
{
	main_cfg.gui_colors[0][0] = 180U; main_cfg.gui_colors[0][1] = 255U; main_cfg.gui_colors[0][2] = 180U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 0U;   main_cfg.gui_colors[1][1] = 20U;  main_cfg.gui_colors[1][2] = 0U;   main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 4U;   main_cfg.gui_colors[2][1] = 34U;  main_cfg.gui_colors[2][2] = 4U;   main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 0U;   main_cfg.gui_colors[3][1] = 24U;  main_cfg.gui_colors[3][2] = 0U;   main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 220U; main_cfg.gui_colors[4][1] = 255U; main_cfg.gui_colors[4][2] = 220U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 10U;  main_cfg.gui_colors[6][1] = 70U;  main_cfg.gui_colors[6][2] = 10U;  main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 14U;  main_cfg.gui_colors[7][1] = 92U;  main_cfg.gui_colors[7][2] = 14U;  main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 20U;  main_cfg.gui_colors[8][1] = 116U; main_cfg.gui_colors[8][2] = 20U;  main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 0U;   main_cfg.gui_colors[9][1] = 18U;  main_cfg.gui_colors[9][2] = 0U;   main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 0U;  main_cfg.gui_colors[10][1] = 24U; main_cfg.gui_colors[10][2] = 0U;   main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 0U;  main_cfg.gui_colors[11][1] = 30U; main_cfg.gui_colors[11][2] = 0U;   main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 8U;  main_cfg.gui_colors[12][1] = 40U; main_cfg.gui_colors[12][2] = 8U;   main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 0U;  main_cfg.gui_colors[13][1] = 26U; main_cfg.gui_colors[13][2] = 0U;   main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_uzebox_classic(void)
{
	main_cfg.gui_colors[0][0] = 255U; main_cfg.gui_colors[0][1] = 255U; main_cfg.gui_colors[0][2] = 255U; main_cfg.gui_colors[0][3] = 255U;
	main_cfg.gui_colors[1][0] = 46U;  main_cfg.gui_colors[1][1] = 0U;   main_cfg.gui_colors[1][2] = 0U;   main_cfg.gui_colors[1][3] = 255U;
	main_cfg.gui_colors[2][0] = 78U;  main_cfg.gui_colors[2][1] = 0U;   main_cfg.gui_colors[2][2] = 0U;   main_cfg.gui_colors[2][3] = 255U;
	main_cfg.gui_colors[3][0] = 62U;  main_cfg.gui_colors[3][1] = 0U;   main_cfg.gui_colors[3][2] = 0U;   main_cfg.gui_colors[3][3] = 255U;
	main_cfg.gui_colors[4][0] = 255U; main_cfg.gui_colors[4][1] = 246U; main_cfg.gui_colors[4][2] = 246U; main_cfg.gui_colors[4][3] = 255U;
	main_cfg.gui_colors[5][0] = 0U;   main_cfg.gui_colors[5][1] = 0U;   main_cfg.gui_colors[5][2] = 0U;   main_cfg.gui_colors[5][3] = 0U;
	main_cfg.gui_colors[6][0] = 168U; main_cfg.gui_colors[6][1] = 0U;   main_cfg.gui_colors[6][2] = 0U;   main_cfg.gui_colors[6][3] = 255U;
	main_cfg.gui_colors[7][0] = 204U; main_cfg.gui_colors[7][1] = 0U;   main_cfg.gui_colors[7][2] = 0U;   main_cfg.gui_colors[7][3] = 255U;
	main_cfg.gui_colors[8][0] = 232U; main_cfg.gui_colors[8][1] = 30U;  main_cfg.gui_colors[8][2] = 30U;  main_cfg.gui_colors[8][3] = 255U;
	main_cfg.gui_colors[9][0] = 34U;  main_cfg.gui_colors[9][1] = 0U;   main_cfg.gui_colors[9][2] = 0U;   main_cfg.gui_colors[9][3] = 255U;
	main_cfg.gui_colors[10][0] = 44U; main_cfg.gui_colors[10][1] = 0U;  main_cfg.gui_colors[10][2] = 0U;   main_cfg.gui_colors[10][3] = 255U;
	main_cfg.gui_colors[11][0] = 54U; main_cfg.gui_colors[11][1] = 0U;  main_cfg.gui_colors[11][2] = 0U;   main_cfg.gui_colors[11][3] = 255U;
	main_cfg.gui_colors[12][0] = 74U; main_cfg.gui_colors[12][1] = 0U;  main_cfg.gui_colors[12][2] = 0U;   main_cfg.gui_colors[12][3] = 255U;
	main_cfg.gui_colors[13][0] = 48U; main_cfg.gui_colors[13][1] = 0U;  main_cfg.gui_colors[13][2] = 0U;   main_cfg.gui_colors[13][3] = 255U;
}

static void main_gui_theme_apply_preset(char const* name)
{
	if (main_gui_theme_name_eq(name, "CUSTOM")){
		strncpy(main_gui_theme_name, "CUSTOM", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "BOOTLOADER")){
		main_gui_theme_apply_bootloader();
		strncpy(main_gui_theme_name, "BOOTLOADER", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "CHILL")){
		main_gui_theme_apply_chill();
		strncpy(main_gui_theme_name, "CHILL", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "RAVE")){
		main_gui_theme_apply_rave();
		strncpy(main_gui_theme_name, "RAVE", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "SNOW")){
		main_gui_theme_apply_snow();
		strncpy(main_gui_theme_name, "SNOW", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "D3THADD3R")){
		main_gui_theme_apply_d3thadd3r();
		strncpy(main_gui_theme_name, "D3THADD3R", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "CRT AMBER")){
		main_gui_theme_apply_crt_amber();
		strncpy(main_gui_theme_name, "CRT AMBER", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "TERMINAL") ||
	          main_gui_theme_name_eq(name, "TERM GREEN") ||
	          main_gui_theme_name_eq(name, "TERMINAL GREEN")){
		main_gui_theme_apply_terminal_green();
		strncpy(main_gui_theme_name, "TERMINAL", sizeof(main_gui_theme_name) - 1U);
	}else if (main_gui_theme_name_eq(name, "UZEBOX")){
		main_gui_theme_apply_uzebox_classic();
		strncpy(main_gui_theme_name, "UZEBOX", sizeof(main_gui_theme_name) - 1U);
	}else{
		main_gui_theme_apply_neutral();
		strncpy(main_gui_theme_name, "NEUTRAL", sizeof(main_gui_theme_name) - 1U);
	}
	main_gui_theme_name[sizeof(main_gui_theme_name) - 1U] = 0;
}

static void main_gui_theme_parse_keyval(char* key, char* val)
{
	auint i;
	if ((key == NULL) || (val == NULL)){ return; }
	if (strcmp(key, "GuiTheme") == 0){
		strncpy(main_gui_theme_name, val, sizeof(main_gui_theme_name) - 1U);
		main_gui_theme_name[sizeof(main_gui_theme_name) - 1U] = 0;
	}else if (strncmp(key, "GuiColor", 8) == 0){
		static const char* names[APPCFG_GUI_COLOR_COUNT] = { "Text", "Border", "WindowBg", "TitleBg", "TitleText", "PanelBg", "Button", "ButtonHover", "ButtonFocus", "Base", "BaseHover", "BaseFocus", "ScrollBase", "ScrollThumb" };
		for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
			char expect[64];
			unsigned int r, g, b, a;
			snprintf(expect, sizeof(expect), "GuiColor%s", names[i]);
			if (strcmp(key, expect) != 0){ continue; }
			if (sscanf(val, "%u,%u,%u,%u", &r, &g, &b, &a) == 4){
				main_cfg.gui_colors[i][0] = (uint8)(r & 0xFFU);
				main_cfg.gui_colors[i][1] = (uint8)(g & 0xFFU);
				main_cfg.gui_colors[i][2] = (uint8)(b & 0xFFU);
				main_cfg.gui_colors[i][3] = (uint8)(a & 0xFFU);
			}
			break;
		}
	}
}

#endif
static auint             main_np_local_player = 0U;
static auint             main_np_remote_player = 1U;
static uint32            main_np_local_mask = 0x00000001UL;
static uint32            main_np_remote_mask = 0x00000002UL;
static uint32            main_np_active_mask = 0x00000003UL;
static auint             main_np_local_pad_map[ROLLBACK_MAX_PLAYERS] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
static auint             main_np_physical_buttons[ROLLBACK_MAX_PLAYERS] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
static auint             main_np_input_delay = 1U;
static uint16            main_np_dev_event_seq = 1U;
static auint             main_np_resim_from = 0U;
static auint             main_np_resim_to = 0U;

static void main_apply_netplay_config_runtime(void)
{
	netplay_config_t npcfg;
	netplay_get_config(&npcfg);
	npcfg.max_players = main_cfg.netplay_max_players;
	npcfg.local_player_mask = main_cfg.netplay_local_player_mask;
	npcfg.rom_send_enabled = main_cfg.netplay_rom_send;
	npcfg.rom_receive_enabled = main_cfg.netplay_rom_receive;
	npcfg.rom_sync_mode = (netplay_rom_sync_mode_t)main_cfg.netplay_rom_sync_mode;
	npcfg.rom_tcp_port = main_cfg.netplay_rom_tcp_port;
	npcfg.rom_max_size = main_cfg.netplay_rom_max_size;
	main_copy_str(npcfg.local_name, sizeof(npcfg.local_name), main_cfg.netplay_name);
	memcpy(npcfg.local_pad_labels, main_cfg.netplay_pad_labels, sizeof(npcfg.local_pad_labels));
	npcfg.relay_server_port = main_cfg.netplay_relay_port ? main_cfg.netplay_relay_port : 43810U;
	strncpy(npcfg.relay_server_host, main_cfg.netplay_relay_host[0] ? main_cfg.netplay_relay_host : "uzenet.us", sizeof(npcfg.relay_server_host) - 1U);
	npcfg.relay_server_host[sizeof(npcfg.relay_server_host) - 1U] = 0;
	strncpy(npcfg.network_interface, main_cfg.netplay_network_interface, sizeof(npcfg.network_interface) - 1U);
	npcfg.network_interface[sizeof(npcfg.network_interface) - 1U] = 0;
	npcfg.relay_allow_direct = TRUE;
	npcfg.relay_allow_relay = TRUE;
	netplay_set_config(&npcfg);
}

static auint main_cfg_get_effective_render_path(void)
{
	auint mode = main_cfg.render_path;
	if (mode > RENDER_PATH_STAGED){
		mode = RENDER_PATH_STAGED;
	}
	return mode;
}

static boole main_cfg_is_classic_1x(void)
{
	return (main_cfg_get_effective_render_path() == RENDER_PATH_CLASSIC_1X);
}

static boole main_cfg_render_allows_frame_merge(void)
{
	return RENDER_PATH_IS_CLASSIC(main_cfg_get_effective_render_path());
}


static void main_cfg_capture_virtual_runtime(void)
{
	auint port;
	auint slot;
	auint id;
	for (port = 0U; port < CU_MULTITAP_MAX_PORTS; port++){
		main_cfg.tap_present[port] = (cu_multitap_get_tap_present(port) != 0U);
		main_cfg.tap_active_slot[port] = cu_multitap_get_active_slot(port);
		for (slot = 0U; slot < CU_MULTITAP_MAX_SLOTS; slot++){
			main_cfg.tap_slot_vdev[port][slot] = cu_multitap_get_slot_vdev(port, slot);
		}
	}
	for (id = 0U; id < CU_VDEV_MAX; id++){
		appcfg_vdev_t* dst = &main_cfg.vdev[id];
		cu_vdev_t const* src = cu_vdev_get(id);
		auint b;
		memset(dst, 0, sizeof(*dst));
		dst->haptic_accept_any = TRUE;
		dst->haptic_binding = CU_VDEV_INVALID;
		for (b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			dst->binding[b].type = CU_VDEV_HOST_NONE;
		}
		if (src == NULL){
			continue;
		}
		dst->used = src->used;
		main_copy_str(dst->name, sizeof(dst->name), src->name);
		dst->type = src->type;
		dst->options = src->options;
		dst->haptic_enabled = src->haptic_enabled;
		dst->haptic_accept_any = src->haptic_accept_any;
		dst->haptic_binding = src->haptic_binding;
		dst->haptic_id = src->haptic_id;
		dst->low16 = src->low16;
		dst->high16 = src->high16;
		dst->sm_scale_x_pct = src->sm_scale_x_pct;
		dst->sm_scale_y_pct = src->sm_scale_y_pct;
		dst->sm_deadzone = src->sm_deadzone;
		dst->sm_invert_x = src->sm_invert_x;
		dst->sm_invert_y = src->sm_invert_y;
		for (b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			auint m;
			dst->binding[b].type = src->binding[b].type;
			dst->binding[b].host_index = src->binding[b].host_index;
			dst->binding[b].flags = src->binding[b].flags;
			for (m = 0U; m < CU_VDEV_REMAP_SLOTS; m++){
				dst->binding[b].map_low16[m] = src->binding[b].map_low16[m] & 0xFFFFU;
			}
		}
	}
}

static void main_cfg_copy_input_profile_fields(app_config_t* dst, app_config_t const* src)
{
	if ((dst == NULL) || (src == NULL)){ return; }
	dst->input_kbuzem = src->input_kbuzem;
	dst->input_player2alloc = src->input_player2alloc;
	dst->mouse_enable = src->mouse_enable;
	dst->mouse_scale = src->mouse_scale;
	memcpy(dst->tap_present, src->tap_present, sizeof(dst->tap_present));
	memcpy(dst->tap_active_slot, src->tap_active_slot, sizeof(dst->tap_active_slot));
	memcpy(dst->tap_slot_vdev, src->tap_slot_vdev, sizeof(dst->tap_slot_vdev));
	memcpy(dst->vdev, src->vdev, sizeof(dst->vdev));
}

static void main_cfg_apply_virtual_runtime(void)
{
	auint port;
	auint slot;
	auint id;
	cu_multitap_reset();
	cu_vdev_reset();
	for (id = 0U; id < CU_VDEV_MAX; id++){
		cu_vdev_t vtmp;
		auint b;
		memset(&vtmp, 0, sizeof(vtmp));
		vtmp.used = main_cfg.vdev[id].used;
		main_copy_str(vtmp.name, sizeof(vtmp.name), main_cfg.vdev[id].name);
		vtmp.type = (cu_vdev_type_t)main_cfg.vdev[id].type;
		vtmp.options = main_cfg.vdev[id].options;
		vtmp.haptic_enabled = main_cfg.vdev[id].haptic_enabled;
		vtmp.haptic_accept_any = main_cfg.vdev[id].haptic_accept_any;
		vtmp.haptic_binding = main_cfg.vdev[id].haptic_binding;
		vtmp.haptic_id = main_cfg.vdev[id].haptic_id;
		vtmp.low16 = main_cfg.vdev[id].low16 & 0xFFFFU;
		vtmp.high16 = main_cfg.vdev[id].high16 & 0xFFFFU;
		vtmp.sm_scale_x_pct = main_cfg.vdev[id].sm_scale_x_pct;
		vtmp.sm_scale_y_pct = main_cfg.vdev[id].sm_scale_y_pct;
		vtmp.sm_deadzone = main_cfg.vdev[id].sm_deadzone;
		vtmp.sm_invert_x = main_cfg.vdev[id].sm_invert_x;
		vtmp.sm_invert_y = main_cfg.vdev[id].sm_invert_y;
		for (b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			auint m;
			vtmp.binding[b].type = (cu_vdev_host_type_t)main_cfg.vdev[id].binding[b].type;
			vtmp.binding[b].host_index = main_cfg.vdev[id].binding[b].host_index;
			vtmp.binding[b].flags = main_cfg.vdev[id].binding[b].flags;
			for (m = 0U; m < CU_VDEV_REMAP_SLOTS; m++){
				vtmp.binding[b].map_low16[m] = main_cfg.vdev[id].binding[b].map_low16[m] & 0xFFFFU;
			}
		}
		(void)cu_vdev_restore(id, &vtmp);
	}
	for (port = 0U; port < CU_MULTITAP_MAX_PORTS; port++){
		cu_multitap_set_tap_present(port, main_cfg.tap_present[port] ? 1U : 0U);
		cu_multitap_set_active_slot(port, main_cfg.tap_active_slot[port]);
		for (slot = 0U; slot < CU_MULTITAP_MAX_SLOTS; slot++){
			cu_multitap_set_slot_vdev(port, slot, main_cfg.tap_slot_vdev[port][slot]);
		}
	}
}

static void main_cfg_capture_runtime(void)
{
	auint flags = guicore_getflags();
	/* A GUI edit may still be waiting for the renderer to unlock. Keep the
	** requested display values when saving or capturing another edit. */
	if (!main_runtime_apply_pending){
		main_cfg.display_gameonly = ((flags & GUICORE_GAMEONLY) != 0U);
		main_cfg.display_fullscreen = ((flags & GUICORE_FULLSCREEN) != 0U);
	}
	main_cfg.frame_merge = main_cfg.frame_merge ? TRUE : FALSE;
	main_cfg.input_kbuzem = ginput_iskbuzem();
	main_cfg.input_player2alloc = ginput_is2palloc();
	/* main_audio_freqscale is the effective value with the limiter applied;
	** main_cfg.audio_freqscale remains the user's preference. */
	main_cfg.audio_s16 = audio_output_s16_get();
	main_cfg.audio_output_rate = audio_output_rate_get();
	main_cfg.audio_latency = audio_latency_get();
	main_cfg.audio_resampler = audio_resampler_get();
	main_cfg.audio_dcblock = audio_dcblock_get();
	main_cfg.audio_lowpass = audio_lowpass_get();
	main_cfg.audio_lowpass_quality = audio_lowpass_quality_get();
	main_cfg.audio_monitor_mode = audio_monitor_mode_get();
	main_cfg.audio_monitor_width = audio_monitor_width_get();
	main_cfg.audio_reverb = audio_reverb_get();
	main_cfg.audio_master_volume = audio_master_volume_get();
	main_cfg.mouse_enable = (cu_mouse_get_enabled() != 0U);
	main_cfg.mouse_scale = cu_mouse_get_scale();
	main_cfg.filter_pre_mode = guicore_get_filter_pre_mode();
	main_cfg.filter_scale_mode = guicore_get_filter_scale_mode();
	main_cfg.filter_crt_mode = guicore_get_filter_crt_mode();
	if (main_cfg.spiram_pages_mode > 9U){ main_cfg.spiram_pages_mode = 0U; }
	main_cfg.netplay_rollback_window = rollback_get_window();
	main_cfg.netplay_input_delay = main_np_input_delay;
	{
		netplay_config_t npcfg;
		netplay_get_config(&npcfg);
		main_cfg.netplay_max_players = npcfg.max_players;
		main_cfg.netplay_local_player_mask = npcfg.local_player_mask;
		main_cfg.netplay_rom_send = npcfg.rom_send_enabled;
		main_cfg.netplay_rom_receive = npcfg.rom_receive_enabled;
		main_cfg.netplay_rom_sync_mode = npcfg.rom_sync_mode;
		main_cfg.netplay_rom_tcp_port = npcfg.rom_tcp_port;
		main_cfg.netplay_rom_max_size = npcfg.rom_max_size;
		main_copy_str(main_cfg.netplay_name, sizeof(main_cfg.netplay_name), npcfg.local_name);
		memcpy(main_cfg.netplay_pad_labels, npcfg.local_pad_labels, sizeof(main_cfg.netplay_pad_labels));
		main_cfg.netplay_relay_port = npcfg.relay_server_port ? npcfg.relay_server_port : 43810U;
		strncpy(main_cfg.netplay_relay_host, npcfg.relay_server_host[0] ? npcfg.relay_server_host : "uzenet.us", APPCFG_PATH_MAX - 1U);
		main_cfg.netplay_relay_host[APPCFG_PATH_MAX - 1U] = 0;
		strncpy(main_cfg.netplay_network_interface, npcfg.network_interface, APPCFG_PATH_MAX - 1U);
		main_cfg.netplay_network_interface[APPCFG_PATH_MAX - 1U] = 0;
	}
	strncpy(main_cfg.remote_roms_host, remote_roms_get_host(), APPCFG_PATH_MAX - 1U);
	main_cfg.remote_roms_host[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg.api_server = api_server_is_enabled();
	main_cfg.api_server_port = api_server_get_port();
#ifdef ENABLE_VCAP
	strncpy(main_cfg.video_dump_file, avconv_get_filename(), APPCFG_PATH_MAX - 1U);
	main_cfg.video_dump_file[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg.video_dump_autoinc = avconv_get_autoinc();
#endif
#ifdef ENABLE_MICROUI
	main_cfg_capture_gui_runtime();
#endif
	main_cfg_capture_virtual_runtime();
	main_cfg.version = APPCFG_VERSION;
	main_cfg.cheats_enabled = cheats_get_master_enabled();
	main_cfg.cheats_autoloadsave = cheats_get_autoloadsave();
}

static void main_cfg_mark_dirty(void)
{
	main_cfg_capture_runtime();
	main_cfg_dirty = TRUE;
}

static void main_cfg_save_immediate(void)
{
	main_cfg_capture_runtime();
	main_cfg_dirty = TRUE;
	if (appcfg_save(&main_cfg, main_cfgfile)){
#ifdef ENABLE_ESP
		cu_esp_save_config();
#endif
		main_cfg_dirty = FALSE;
	}
}

static void main_cfg_apply_runtime_later(void)
{
	main_runtime_apply_pending = TRUE;
	main_cfg_dirty = TRUE;
}

static void main_sd_model_from_cfg(cu_spisd_model_t* m)
{
	if (m == NULL){ return; }
	m->preset = main_cfg.sd_timing_preset;
	m->init_ms = main_cfg.sd_init_ms;
	m->cmd_wait_bytes = main_cfg.sd_cmd_wait_bytes;
	m->read_wait_bytes = main_cfg.sd_read_wait_bytes;
	m->write_busy_ms = main_cfg.sd_write_busy_ms;
	m->cs_high_ms = main_cfg.sd_cs_high_ms;
	m->init_min_byte_cycles = main_cfg.sd_init_min_byte_cycles;
	m->init_max_byte_cycles = main_cfg.sd_init_max_byte_cycles;
}

static void main_sd_model_to_cfg(cu_spisd_model_t const* m)
{
	if (m == NULL){ return; }
	main_cfg.sd_timing_preset = m->preset;
	main_cfg.sd_init_ms = m->init_ms;
	main_cfg.sd_cmd_wait_bytes = m->cmd_wait_bytes;
	main_cfg.sd_read_wait_bytes = m->read_wait_bytes;
	main_cfg.sd_write_busy_ms = m->write_busy_ms;
	main_cfg.sd_cs_high_ms = m->cs_high_ms;
	main_cfg.sd_init_min_byte_cycles = m->init_min_byte_cycles;
	main_cfg.sd_init_max_byte_cycles = m->init_max_byte_cycles;
}

static void main_cfg_apply_runtime(void)
{
	auint flags;
	auint curflags;
	boole need_reinit;
	boole need_fullscreen;
	boole want_fullscreen;

	curflags = guicore_getflags();
	flags = curflags;
	flags &= ~(GUICORE_SMALL | GUICORE_GAMEONLY | GUICORE_FULLSCREEN | GUICORE_NOVSYNC);
	if (main_cfg_is_classic_1x()){
		flags |= GUICORE_SMALL;
	}
	if (main_cfg.display_gameonly){
		flags |= GUICORE_GAMEONLY;
	}
	if (main_cfg.display_fullscreen){
		flags |= GUICORE_FULLSCREEN;
	}
	if (!main_cfg.frame_rate_limiter){
		flags |= GUICORE_NOVSYNC;
	}
	main_fmerge = (main_cfg.frame_merge && main_cfg_render_allows_frame_merge());
	main_nolimit = (!main_cfg.frame_rate_limiter);
	main_audio_freqscale = (main_cfg.audio_freqscale && main_cfg.frame_rate_limiter);
	audio_output_rate_set(main_cfg.audio_output_rate);
	audio_latency_set(main_cfg.audio_latency);
	audio_output_s16_ena(main_cfg.audio_s16);
	audio_resampler_set(main_cfg.audio_resampler);
	audio_dcblock_ena(main_cfg.audio_dcblock);
	audio_lowpass_set(main_cfg.audio_lowpass);
	audio_lowpass_quality_set(main_cfg.audio_lowpass_quality);
	audio_monitor_mode_set(main_cfg.audio_monitor_mode);
	audio_monitor_width_set(main_cfg.audio_monitor_width);
	audio_reverb_set(main_cfg.audio_reverb);
	audio_master_volume_set(main_cfg.audio_master_volume);
	ginput_setkbuzem(main_cfg.input_kbuzem);
	ginput_set2palloc(main_cfg.input_player2alloc);
	cu_mouse_set_enabled(main_cfg.mouse_enable ? 1U : 0U);
	cu_mouse_set_scale((uint8)main_cfg.mouse_scale);
	filesys_set_sd_allow_new_files(main_cfg.sd_allow_new_files);
	{
		cu_spisd_model_t m;
		if ((main_cfg.sd_timing_preset >= CU_SPISD_PRESET_SLOW) && (main_cfg.sd_timing_preset <= CU_SPISD_PRESET_FAST)){
			cu_spisd_model_preset(main_cfg.sd_timing_preset, &m);
			main_sd_model_to_cfg(&m);
		}else{
			main_cfg.sd_timing_preset = CU_SPISD_PRESET_CUSTOM;
			main_sd_model_from_cfg(&m);
		}
		cu_spisd_model_set(&m);
	}
	main_cfg_apply_virtual_runtime();
	main_apply_esp_softap_policy(main_rom_is_uze, main_rom_is_uze ? &main_rom_head : NULL);
	audio_freqscale_ena(main_audio_freqscale);
	textgui_log_set_enabled(main_cfg.display_system_messages);
	textgui_log_set_level(main_cfg.log_verbosity);
	if (main_cfg.controllerdb_path[0] != 0){
		if (strcmp(main_controllerdb_loaded, main_cfg.controllerdb_path) != 0){
			if (ginput_reload_controllerdb_quiet(main_cfg.controllerdb_path) >= 0){
				main_copy_str(main_controllerdb_loaded, sizeof(main_controllerdb_loaded), main_cfg.controllerdb_path);
			}
		}
	}else{
		main_controllerdb_loaded[0] = 0;
	}

	need_reinit = ((curflags == 0U) ||
	               ((curflags & (GUICORE_SMALL | GUICORE_GAMEONLY | GUICORE_NOVSYNC)) !=
	                (flags    & (GUICORE_SMALL | GUICORE_GAMEONLY | GUICORE_NOVSYNC))));
	want_fullscreen = ((flags & GUICORE_FULLSCREEN) != 0U);
	need_fullscreen = ((curflags != 0U) &&
	                   (((curflags & GUICORE_FULLSCREEN) != 0U) != want_fullscreen));

	if (need_reinit){
		if (!guicore_init(flags, main_title)){
			main_exit = TRUE;
			return;
		}
	}else if (need_fullscreen){
		if (!guicore_setfullscreen(want_fullscreen)){
			if (!guicore_init(flags, main_title)){
				main_exit = TRUE;
				return;
			}
		}
	}
	guicore_set_render_path(main_cfg_get_effective_render_path());
	guicore_set_filter_pre_mode(main_cfg.filter_pre_mode);
	guicore_set_filter_scale_mode(main_cfg.filter_scale_mode);
	guicore_set_filter_crt_mode(main_cfg.filter_crt_mode);
	mainui_set_netplay_rollback_window(main_cfg.netplay_rollback_window);
	mainui_set_netplay_input_delay(main_cfg.netplay_input_delay);
	main_apply_netplay_config_runtime();
	remote_roms_set_host(main_cfg.remote_roms_host[0] ? main_cfg.remote_roms_host : "uzenet.us");
	api_server_configure(main_cfg.api_server, main_cfg.api_server_port);
	web_server_configure(main_cfg.api_server, WEB_SERVER_DEFAULT_PORT, main_cfg.api_server_port);
#ifdef ENABLE_VCAP
	avconv_set_filename(main_cfg.video_dump_file);
	avconv_set_autoinc(main_cfg.video_dump_autoinc);
#endif
#ifdef ENABLE_MICROUI
	main_copy_str(main_gui_theme_name, sizeof(main_gui_theme_name), main_cfg.gui_theme_name);
	main_copy_str(main_gui_theme_file, sizeof(main_gui_theme_file), main_cfg.gui_theme_file);
	main_cfg_apply_gui_runtime();
#endif
	cheats_set_master_enabled(main_cfg.cheats_enabled);
	cheats_set_autoloadsave(main_cfg.cheats_autoloadsave);
	main_cfg_capture_runtime();
}

static void main_cfg_apply_runtime_pending(void)
{
	if (!main_runtime_apply_pending){
		return;
	}
	main_runtime_apply_pending = FALSE;
	main_cfg_apply_runtime();
}

static void main_recent_rom_add(char const* path)
{
	auint i;
	auint j;
	if ((path == NULL) || (path[0] == 0)){
		return;
	}
	if (main_cfg.recent_roms_frozen){
		return;
	}
	for (i = 0U; i < MAINUI_RECENT_ROMS; i++){
		if (strcmp(main_cfg.recent_roms[i], path) == 0){
			for (j = i; j > 0U; j--){
				strncpy(main_cfg.recent_roms[j], main_cfg.recent_roms[j - 1U], APPCFG_PATH_MAX - 1U);
				main_cfg.recent_roms[j][APPCFG_PATH_MAX - 1U] = 0;
			}
			strncpy(main_cfg.recent_roms[0], path, APPCFG_PATH_MAX - 1U);
			main_cfg.recent_roms[0][APPCFG_PATH_MAX - 1U] = 0;
			main_cfg_save_immediate();
			return;
		}
	}
	for (i = MAINUI_RECENT_ROMS - 1U; i > 0U; i--){
		strncpy(main_cfg.recent_roms[i], main_cfg.recent_roms[i - 1U], APPCFG_PATH_MAX - 1U);
		main_cfg.recent_roms[i][APPCFG_PATH_MAX - 1U] = 0;
	}
	strncpy(main_cfg.recent_roms[0], path, APPCFG_PATH_MAX - 1U);
	main_cfg.recent_roms[0][APPCFG_PATH_MAX - 1U] = 0;
	main_cfg_save_immediate();
}

static void main_recent_rom_clear(void)
{
	memset(main_cfg.recent_roms, 0, sizeof(main_cfg.recent_roms));
	main_cfg_save_immediate();
}

static boole main_netplay_player_is_owned(uint32 mask, auint logical_player);

static auint CU_UNUSED_FN main_netplay_player_index(auint player_one_based)
{
	if (player_one_based <= 1U){
		return 0U;
	}
	if (player_one_based > ROLLBACK_MAX_PLAYERS){
		return (ROLLBACK_MAX_PLAYERS - 1U);
	}
	return player_one_based - 1U;
}

static void main_netplay_map_logical_player(auint logical_player, auint *port, auint *slot)
{
	auint lp = logical_player;
	if (port != NULL){
		*port = lp & 1U;
	}
	if (slot != NULL){
		*slot = (lp >> 1U);
	}
}

static void main_netplay_map_local_pad(auint pad_index, auint *port, auint *slot)
{
	auint p = pad_index;
	if (port != NULL){
		*port = p & 1U;
	}
	if (slot != NULL){
		*slot = (p >> 1U);
	}
}

static auint main_netplay_port_slot_to_logical(auint port, auint slot)
{
	return ((slot & 0x3U) << 1U) | (port & 0x1U);
}

static auint main_netplay_device_event_target_frame(void)
{
	auint delay = main_np_input_delay;
	if (delay == 0U){
		delay = 1U;
	}
	return main_np_frame + delay;
}

static boole main_netplay_submit_device_event(rollback_device_event_t const *ev, auint frame)
{
	rollback_device_event_t tmp;
	if (ev == NULL){
		return FALSE;
	}
	tmp = *ev;
	if (tmp.seq == 0U){
		tmp.seq = main_np_dev_event_seq++;
		if (main_np_dev_event_seq == 0U){
			main_np_dev_event_seq = 1U;
		}
	}
	(void)rollback_submit_local_device_event(frame, &tmp);
	(void)netplay_send_device_event(frame, &tmp);
	return TRUE;
}

static boole main_netplay_submit_tap_select(auint port, auint slot)
{
	rollback_device_event_t ev;
	auint logical;
	auint target_frame;

	if ((!main_np_session_active) || (!netplay_is_connected())){
		return FALSE;
	}
	if ((port >= 2U) || (slot >= 4U)){
		return TRUE;
	}
	logical = main_netplay_port_slot_to_logical(port, slot);
	if (!main_netplay_player_is_owned(main_np_local_mask, logical)){
		return TRUE;
	}
	memset(&ev, 0, sizeof(ev));
	ev.type = ROLLBACK_DEV_EVT_TAP_SELECT;
	ev.port = (uint8)port;
	ev.slot = (uint8)slot;
	ev.len = 1U;
	ev.data[0] = (uint8)(slot & 0x03U);
	target_frame = main_netplay_device_event_target_frame();
	(void)main_netplay_submit_device_event(&ev, target_frame);
	return TRUE;
}

static boole main_netplay_submit_tap_probe(auint port)
{
	rollback_device_event_t ev;
	auint slot;
	auint logical;
	auint target_frame;

	if ((!main_np_session_active) || (!netplay_is_connected())){
		return FALSE;
	}
	if (port >= 2U){
		return TRUE;
	}
	slot = cu_multitap_get_active_slot(port) & 0x03U;
	logical = main_netplay_port_slot_to_logical(port, slot);
	if (!main_netplay_player_is_owned(main_np_local_mask, logical)){
		return TRUE;
	}
	memset(&ev, 0, sizeof(ev));
	ev.type = ROLLBACK_DEV_EVT_TAP_PROBE;
	ev.port = (uint8)port;
	ev.slot = (uint8)slot;
	target_frame = main_netplay_device_event_target_frame();
	(void)main_netplay_submit_device_event(&ev, target_frame);
	return TRUE;
}

static boole main_netplay_kbd_enqueue_hook(auint port, uint8 value)
{
	rollback_device_event_t ev;
	auint slot;
	auint logical;
	auint target_frame;

	if ((!main_np_session_active) || (!netplay_is_connected())){
		return FALSE;
	}
	if (port >= 2U){
		return TRUE;
	}
	slot = cu_multitap_get_active_slot(port);
	logical = main_netplay_port_slot_to_logical(port, slot);
	if (!main_netplay_player_is_owned(main_np_local_mask, logical)){
		return TRUE;
	}
	memset(&ev, 0, sizeof(ev));
	ev.type = ROLLBACK_DEV_EVT_KBD_BYTES;
	ev.port = (uint8)port;
	ev.slot = (uint8)slot;
	ev.len = 1U;
	ev.data[0] = value;
	target_frame = main_netplay_device_event_target_frame();
	(void)main_netplay_submit_device_event(&ev, target_frame);
	return TRUE;
}

static void main_netplay_capture_physical_inputs(void)
{
	auint i;
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		auint port;
		auint slot;
		main_netplay_map_local_pad(i, &port, &slot);
		main_np_physical_buttons[i] = 0U;
		(void)cu_ctr_getslot_packet_state(port, slot, &(main_np_physical_buttons[i]), NULL);
	}
}

static void main_netplay_restore_physical_inputs(void)
{
	auint i;
	cu_ctr_clear_packet_overrides();
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		auint port;
		auint slot;
		main_netplay_map_local_pad(i, &port, &slot);
		cu_ctr_setsnes_slot(port, slot, main_np_physical_buttons[i]);
	}
}


static uint32 main_netplay_active_mask(auint max_players)
{
	if (max_players == 0U){
		return 0U;
	}
	if (max_players >= 32U){
		return 0xFFFFFFFFUL;
	}
	return (((uint32)1UL << max_players) - 1UL);
}

static uint32 main_netplay_mask_from_player(auint player_one_based)
{
	if ((player_one_based == 0U) || (player_one_based > 32U)){
		return 0U;
	}
	return ((uint32)1UL << (player_one_based - 1U));
}

static auint main_netplay_first_player_from_mask(uint32 mask, auint max_players)
{
	auint i;
	uint32 active = main_netplay_active_mask(max_players);
	mask &= active;
	for (i = 0U; i < max_players; i++){
		if ((mask & ((uint32)1UL << i)) != 0U){
			return i;
		}
	}
	return 0U;
}

static boole main_netplay_player_is_owned(uint32 mask, auint logical_player)
{
	return ((mask & ((uint32)1UL << logical_player)) != 0U);
}

static boole main_netplay_cheats_blocked(void)
{
	netplay_status_t st;
	netplay_get_status(&st);
	return st.enabled ? TRUE : FALSE;
}

static void main_netplay_runtime_reset(void)
{
	auint i;
	rollback_reset();
	savestate_checkpoint_clear();
	main_np_session_active = FALSE;
	main_np_last_predicted = FALSE;
	main_np_last_stalled = FALSE;
	main_np_last_resim = FALSE;
	main_np_frame = 0U;
	main_np_local_player = 0U;
	main_np_remote_player = 1U;
	main_np_local_mask = 0x00000001UL;
	main_np_remote_mask = 0x00000002UL;
	main_np_active_mask = 0x00000003UL;
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		main_np_local_pad_map[i] = i;
		main_np_physical_buttons[i] = 0U;
	}
	main_np_resim_from = 0U;
	main_np_resim_to = 0U;
	main_np_dev_event_seq = 1U;
}

static void main_netplay_sync_reset_start_state(void)
{
	if (main_loaded_rom_path[0] != 0){
		(void)main_load_rom_file_internal(main_loaded_rom_path);
	}else{
		cu_avr_reset();
		main_netplay_runtime_reset();
		main_t5_cc = cu_avr_getcycle();
		audio_reset();
		textgui_reset();
		main_ispause = FALSE;
		main_isadvfr = FALSE;
#ifdef ENABLE_DEBUGGER
		mainui_debug_nav_clear();
#endif
	}
	print_message("Netplay: synchronized reset to ROM start.\n");
}

static void main_netplay_session_begin(netplay_status_t const* st)
{
	auint peer_player;
	auint i;

	uint32 active_mask;
	main_netplay_runtime_reset();
	main_np_session_active = TRUE;
	active_mask = main_netplay_active_mask((st->max_players == 0U) ? 1U : st->max_players);
	main_np_active_mask = active_mask;
	main_np_local_mask = st->local_player_mask;
	if (main_np_local_mask == 0U){
		main_np_local_mask = main_netplay_mask_from_player((st->local_player != 0U) ? st->local_player : 1U);
	}
	main_np_local_mask &= active_mask;
	if (main_np_local_mask == 0U){
		main_np_local_mask = 1UL;
	}
	main_np_remote_mask = st->peer_player_mask;
	if (main_np_remote_mask == 0U){
		peer_player = (st->peer_player != 0U) ? st->peer_player : ((st->local_player == 1U) ? 2U : 1U);
		main_np_remote_mask = main_netplay_mask_from_player(peer_player);
	}
	main_np_remote_mask &= active_mask;
	if (main_np_remote_mask == 0U){
		main_np_remote_mask = (active_mask & (~main_np_local_mask));
	}
	main_np_local_player = main_netplay_first_player_from_mask(main_np_local_mask, st->max_players ? st->max_players : 1U);
	main_np_remote_player = main_netplay_first_player_from_mask(main_np_remote_mask, st->max_players ? st->max_players : 1U);
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		main_np_local_pad_map[i] = i;
		if (st->lobby_seat_map_valid && (i < st->max_players) && (((st->local_player_mask >> i) & 1U) != 0U)){
			main_np_local_pad_map[i] = st->lobby_seat_pad_index[i];
		}
		if ((active_mask & ((uint32)1UL << i)) != 0U){
			auint delay_frame;
			rollback_seed_remote_input(i, 0U);
			if (main_netplay_player_is_owned(main_np_local_mask, i)){
				for (delay_frame = 0U; delay_frame < main_np_input_delay; delay_frame++){
					rollback_submit_local_input(delay_frame, i, 0U);
				}
			}
		}
	}
	if (st->max_players > ROLLBACK_MAX_PLAYERS){
		print_message("Netplay: gameplay sync currently clamps to %u players.\n", (unsigned)ROLLBACK_MAX_PLAYERS);
	}
	print_message("Netplay: gameplay input sync active (local mask %08X, peer mask %08X).\n",
			(unsigned)main_np_local_mask,
			(unsigned)main_np_remote_mask);
	main_system_message("NETPLAY SESSION STARTED");
}

static void main_netplay_sync_runtime_state(void)
{
	netplay_status_t st;
	boole active;

	netplay_get_status(&st);
	active = st.enabled && st.connected && st.compat_ok && st.session_started;
	if (active){
		if (!main_np_session_active){
			main_netplay_sync_reset_start_state();
			netplay_get_status(&st);
			main_netplay_session_begin(&st);
		}
	}else if (main_np_session_active){
		print_message("Netplay: gameplay input sync stopped.\n");
		main_system_message("NETPLAY SESSION STOPPED");
		main_netplay_runtime_reset();
	}
}

static void main_netplay_apply_frame_inputs(auint frame)
{
	auint player;
	auint buttons;
	auint port;
	auint slot;
	auint ev_count = 0U;
	auint ei;
	boole predicted;
	rollback_device_event_t events[ROLLBACK_DEVICE_EVENTS_PER_FRAME];
	main_input_trace_ctx_valid = TRUE;
	main_input_trace_ctx_frame = frame;
	main_input_trace_ctx_phase = "APPLY";

	for (player = 0U; player < ROLLBACK_MAX_PLAYERS; player++){
		auint bits = 16U;
		main_netplay_map_logical_player(player, &port, &slot);
		(void)cu_ctr_getslot_packet_format(port, slot, &bits);
		if (rollback_get_buttons(frame, player, &buttons, &predicted)){
			cu_ctr_setslot_packet_override(port, slot, TRUE, buttons, bits);
			cu_ctr_setsnes_slot(port, slot, buttons);
		}else{
			cu_ctr_setslot_packet_override(port, slot, TRUE, 0U, bits);
			cu_ctr_setsnes_slot(port, slot, 0U);
		}
	}
	if (rollback_get_device_events(frame, events, ROLLBACK_DEVICE_EVENTS_PER_FRAME, &ev_count)){
		for (ei = 0U; ei < ev_count; ++ei){
			rollback_device_event_t const *ev = &(events[ei]);
			main_input_trace_log_device_event(frame, ev, "APPLY");
			switch (ev->type){
				case ROLLBACK_DEV_EVT_TAP_SELECT:
					if ((ev->port < 2U) && (ev->len >= 1U)){
						cu_multitap_set_active_slot(ev->port, (auint)(ev->data[0] & 0x03U));
						cu_multitap_touch_activity(ev->port, cu_avr_getcycle());
					}
					break;
				case ROLLBACK_DEV_EVT_TAP_PROBE:
					if (ev->port < 2U){
						cu_multitap_set_probe_pending(ev->port, 1U);
						cu_multitap_touch_activity(ev->port, cu_avr_getcycle());
					}
					break;
				case ROLLBACK_DEV_EVT_KBD_BYTES:
					if (ev->port < 2U){
						auint bi;
						for (bi = 0U; bi < ev->len && bi < ROLLBACK_DEVICE_EVENT_DATA_MAX; ++bi){
							cu_kbd_debug_enqueue_port_byte(ev->port, ev->data[bi]);
						}
					}
					break;
				default:
					break;
			}
		}
	}
	main_input_trace_log_frame_summary(frame, "APPLY");
	main_input_trace_ctx_valid = FALSE;
}

static void main_netplay_audio_resync(void)
{
	/*
	** Netplay rollback can restore an older savestate and resimulate from there,
	** but audio already queued toward SDL is not rollback-safe: we don't know how
	** much of that queue the host has already consumed, so trying to "undo" or
	** splice old samples is more likely to cause drift or duplication. Instead we
	** discard pending emulator-side audio at rollback / stall boundaries and let
	** fresh audio start from the corrected emulated state.
	*/
	audio_flush();
}

static void main_netplay_resim_to_current(void)
{
	auint correction_frame;
	auint loaded_frame;
	auint frame;
#ifdef ENABLE_DEBUGGER
	auint break_addr;
#endif

	if (!main_np_session_active){
		return;
	}
	if (!rollback_has_pending_correction()){
		main_np_last_resim = FALSE;
		return;
	}

	correction_frame = rollback_get_pending_correction_frame();
	if (!savestate_checkpoint_restore_at_or_before(correction_frame, &loaded_frame)){
		rollback_clear_pending_correction();
		print_message("Netplay: correction at frame %u ignored (no checkpoint).\n", (unsigned)correction_frame);
		main_np_last_resim = FALSE;
		return;
	}

	rollback_clear_pending_correction();
	main_np_last_resim = TRUE;
	main_np_resim_from = loaded_frame;
	main_np_resim_to = main_np_frame;
	main_netplay_audio_resync();
	print_message("Netplay: rollback correction, resim %u..%u.\n",
			(unsigned)loaded_frame,
			(unsigned)((main_np_frame == 0U) ? 0U : (main_np_frame - 1U)));

	for (frame = loaded_frame; frame < main_np_frame; frame++){
		rollback_prepare_t prep;
		rollback_process_remote_queue();
		rollback_prepare_frame(frame, &prep);
		if (prep.stall){
			main_np_last_stalled = TRUE;
			break;
		}
		savestate_checkpoint_capture(frame);
		main_netplay_apply_frame_inputs(frame);
		if (!main_netplay_cheats_blocked()){ cheats_apply(); }
		(void)frame_run(TRUE, FALSE);
#ifdef ENABLE_DEBUGGER
		if (frame_breakpoint_hit(TRUE, &break_addr)){
			main_dbg_break_hit = TRUE;
			main_dbg_break_addr = break_addr;
			main_ispause = TRUE;
			main_isadvfr = FALSE;
			if ((!cu_avr_temp_break_get(NULL)) && (!cu_avr_debug_step_active())){
				mainui_debug_run_status_set("Idle.");
			}
			break;
		}
#endif
	}
}

static boole main_netplay_prepare_live_frame(void)
{
	rollback_prepare_t prep;
	auint raw_buttons[ROLLBACK_MAX_PLAYERS];
	auint local_buttons = 0U;
	auint i;

	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		raw_buttons[i] = main_np_physical_buttons[i];
	}

	main_netplay_resim_to_current();
	rollback_process_remote_queue();

	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		uint32 bit = ((uint32)1UL << i);
		if ((main_np_active_mask & bit) == 0U){
			rollback_submit_local_input(main_np_frame, i, 0U);
			continue;
		}
		if (main_netplay_player_is_owned(main_np_local_mask, i)){
			auint pad = main_np_local_pad_map[i];
			auint target_frame = main_np_frame + main_np_input_delay;
			if (pad >= ROLLBACK_MAX_PLAYERS){
				pad = i;
			}
			local_buttons = raw_buttons[pad];
			rollback_submit_local_input(target_frame, i, local_buttons);
			(void)netplay_send_input(target_frame, i, local_buttons);
		}else if ((main_np_remote_mask & bit) == 0U){
			rollback_submit_local_input(main_np_frame, i, 0U);
		}
	}

	rollback_process_remote_queue();
	rollback_prepare_frame(main_np_frame, &prep);
	main_np_last_predicted = prep.predicted_any;
	if (prep.stall && (!main_np_last_stalled)){
		/*
		** Flush only on stall entry. Once one peer is forced to wait, any audio it
		** already queued locally is liable to trail behind the corrected gameplay
		** timeline after the wait clears. Re-flushing every stalled frame would only
		** add needless churn, so the transition into stall is the right barrier.
		*/
		main_netplay_audio_resync();
		print_message("Netplay: waiting for input mask %08X at frame %u.\n",
				(unsigned)prep.missing_mask,
				(unsigned)main_np_frame);
	}
	main_np_last_stalled = prep.stall;
	if (prep.stall){
		return FALSE;
	}

	savestate_checkpoint_capture(main_np_frame);
	main_netplay_apply_frame_inputs(main_np_frame);
	return TRUE;
}

boole mainui_get_display_gameonly(void)
{
	return main_cfg.display_gameonly;
}

boole mainui_get_display_fullscreen(void)
{
	return main_cfg.display_fullscreen;
}

boole mainui_get_system_messages(void)
{
	return main_cfg.display_system_messages;
}

auint mainui_get_log_verbosity(void){ return main_cfg.log_verbosity; }

boole mainui_get_gui_gamepad_enable(void)
{
	return main_cfg.gui_gamepad_enable;
}

auint mainui_get_gui_gamepad_speed_pct(void)
{
	return main_cfg.gui_gamepad_speed_pct;
}

boole mainui_get_gui_pause_while_open(void)
{
	return main_cfg.gui_pause_while_open;
}

boole mainui_get_gui_virtual_keyboard(void)
{
	return main_cfg.gui_virtual_keyboard;
}

boole mainui_get_frame_rate_limiter(void)
{
	return main_cfg.frame_rate_limiter;
}

boole mainui_get_frame_merge(void)
{
	return (main_cfg.frame_merge && main_cfg_render_allows_frame_merge());
}

boole mainui_get_input_kbuzem(void)
{
	return main_cfg.input_kbuzem;
}

boole mainui_get_input_player2alloc(void)
{
	return main_cfg.input_player2alloc;
}

boole mainui_get_audio_freqscale(void)
{
	return main_cfg.audio_freqscale;
}

boole mainui_get_audio_s16(void)
{
	return main_cfg.audio_s16;
}

auint mainui_get_audio_output_rate(void)
{
	return main_cfg.audio_output_rate;
}

auint mainui_get_audio_latency(void)
{
	return main_cfg.audio_latency;
}

auint mainui_get_audio_resampler(void)
{
	return main_cfg.audio_resampler;
}

boole mainui_get_audio_dcblock(void)
{
	return main_cfg.audio_dcblock;
}

auint mainui_get_audio_lowpass(void)
{
	return main_cfg.audio_lowpass;
}

auint mainui_get_audio_lowpass_quality(void)
{
	return main_cfg.audio_lowpass_quality;
}

auint mainui_get_audio_monitor_mode(void)
{
	return main_cfg.audio_monitor_mode;
}

auint mainui_get_audio_monitor_width(void)
{
	return main_cfg.audio_monitor_width;
}

auint mainui_get_audio_reverb(void)
{
	return main_cfg.audio_reverb;
}

auint mainui_get_audio_master_volume(void)
{
	return main_cfg.audio_master_volume;
}

auint mainui_get_audio_freq_hz(void)
{
	return audio_get_output_freq();
}

auint mainui_get_audio_source_freq_hz(void)
{
	return audio_getfreq();
}

auint mainui_get_audio_output_format(void)
{
	return audio_get_output_format();
}

auint mainui_get_audio_output_channels(void)
{
	return audio_get_output_channels();
}

auint mainui_get_audio_output_samples(void)
{
	return audio_get_output_samples();
}

boole mainui_get_mouse_enable(void)
{
	return main_cfg.mouse_enable;
}

auint mainui_get_mouse_scale(void)
{
	return main_cfg.mouse_scale;
}

auint mainui_get_render_path(void)
{
	return main_cfg_get_effective_render_path();
}

auint mainui_get_filter_pre_mode(void)
{
	return main_cfg.filter_pre_mode;
}

auint mainui_get_filter_scale_mode(void)
{
	return main_cfg.filter_scale_mode;
}

auint mainui_get_filter_crt_mode(void)
{
	return main_cfg.filter_crt_mode;
}

auint mainui_get_spiram_pages_mode(void)
{
	if (main_cfg.spiram_pages_mode > 9U){ return 0U; }
	return main_cfg.spiram_pages_mode;
}

boole mainui_get_sd_allow_new_files(void)
{
	return main_cfg.sd_allow_new_files;
}

auint mainui_get_sd_timing_preset(void)
{
	return main_cfg.sd_timing_preset;
}

void mainui_get_sd_timing_model(cu_spisd_model_t* out)
{
	main_sd_model_from_cfg(out);
}

auint mainui_get_esp_softap_mode(void)
{
	if (main_cfg.esp_softap_mode > MAINUI_ESP_SOFTAP_MODE_ENABLE){ return MAINUI_ESP_SOFTAP_MODE_HINT; }
	return main_cfg.esp_softap_mode;
}

auint mainui_get_serial_route(void)
{
	return cu_esp_get_serial_route();
}

auint mainui_get_serial_esp_model(void)
{
	return cu_esp_get_serial_esp_model();
}

auint mainui_get_esp_at_firmware_profile(void)
{
	return cu_esp_get_at_firmware_profile();
}

auint mainui_get_uart_profile(void)
{
	return cu_esp_get_uart_profile();
}

char const* mainui_get_host_serial_device_name(void)
{
	return cu_esp_get_host_serial_device_name();
}

char const* mainui_get_host_midi_port_name(void)
{
	return cu_esp_get_host_midi_port_name();
}

auint mainui_get_virtual_midi_mode(void)
{
	return cu_esp_get_virtual_midi_mode();
}

char const* mainui_get_virtual_midi_port_name(void)
{
	return cu_esp_get_virtual_midi_port_name();
}

char const* mainui_get_tcp_serial_host(void)
{
	return cu_esp_get_tcp_serial_host();
}

auint mainui_get_tcp_serial_port(void)
{
	return cu_esp_get_tcp_serial_port();
}

boole mainui_get_tcp_serial_auto_reconnect(void)
{
	return cu_esp_get_tcp_serial_auto_reconnect();
}

auint mainui_get_tcp_serial_mode(void)
{
	return cu_esp_get_tcp_serial_mode();
}

auint mainui_get_tcp_serial_state(void)
{
	return cu_esp_get_tcp_serial_state();
}

sint32 mainui_get_tcp_serial_last_error(void)
{
	return cu_esp_get_tcp_serial_last_error();
}

boole mainui_get_host_midi_supported(void)
{
	return cu_esp_host_midi_supported();
}

boole mainui_get_virtual_midi_supported(void)
{
	return cu_esp_virtual_midi_supported();
}

void mainui_refresh_host_midi_ports(void)
{
	cu_esp_host_midi_refresh_ports();
}

auint mainui_get_host_midi_port_count(void)
{
	return cu_esp_host_midi_get_port_count();
}

char const* mainui_get_host_midi_port_name_at(auint idx)
{
	return cu_esp_host_midi_get_port_name(idx);
}

char const* mainui_get_host_midi_port_label_at(auint idx)
{
	return cu_esp_host_midi_get_port_label(idx);
}

char const* mainui_get_rom_path(void)
{
	return main_cfg.rom_path;
}

char const* mainui_get_screenshot_path(void)
{
	return main_cfg.screenshot_path;
}

char const* mainui_get_save_path(void)
{
	return main_cfg.save_path;
}

char const* mainui_get_controllerdb_path(void)
{
	return main_cfg.controllerdb_path;
}

boole mainui_get_resident_bootloader_enable(void)
{
	return main_cfg.resident_bootloader_enable;
}

boole mainui_get_fast_flash(void)
{
	return main_cfg.fast_flash;
}

char const* mainui_get_resident_bootloader_file(void)
{
	return main_cfg.resident_bootloader_file;
}

char const* mainui_get_remote_roms_host(void)
{
	return main_cfg.remote_roms_host[0] ? main_cfg.remote_roms_host : "uzenet.us";
}

char const* mainui_get_gui_theme_name(void)
{
	return main_gui_theme_name;
}

char const* mainui_get_gui_theme_file(void)
{
	return main_gui_theme_file;
}

void mainui_get_netplay_runtime(mainui_netplay_runtime_t* out)
{
	if (out == NULL){ return; }
	memset(out, 0, sizeof(*out));
	out->active = main_np_session_active;
	out->predicted_any = main_np_last_predicted;
	out->stalled = main_np_last_stalled;
	out->resim_applied = main_np_last_resim;
	out->frame = main_np_frame;
	out->local_player = main_np_local_player + 1U;
	out->remote_player = main_np_remote_player + 1U;
	out->local_player_mask = main_np_local_mask;
	out->remote_player_mask = main_np_remote_mask;
	out->rollback_window = rollback_get_window();
	out->input_delay = main_np_input_delay;
	out->resim_from_frame = main_np_resim_from;
	out->resim_to_frame = main_np_resim_to;
}

auint mainui_get_netplay_rollback_window(void)
{
	return rollback_get_window();
}

void mainui_set_netplay_rollback_window(auint frames)
{
	auint max_frames = SAVESTATE_DEFAULT_CHECKPOINTS - 1U;
	if (max_frames == 0U){
		max_frames = 1U;
	}
	if (frames == 0U){
		frames = ROLLBACK_DEFAULT_WINDOW;
	}
	if (frames > max_frames){
		frames = max_frames;
	}
	rollback_set_window(frames);
	if (main_cfg.netplay_rollback_window != frames){
		main_cfg.netplay_rollback_window = frames;
		main_cfg_dirty = TRUE;
	}
}

auint mainui_get_netplay_input_delay(void)
{
	return main_np_input_delay;
}

void mainui_set_netplay_input_delay(auint frames)
{
	if (frames > 8U){
		frames = 8U;
	}
	main_np_input_delay = frames;
	if (main_cfg.netplay_input_delay != frames){
		main_cfg.netplay_input_delay = frames;
		main_cfg_dirty = TRUE;
	}
}

char const* mainui_get_current_rom_name(void)
{
	if (main_rom_name[0] != 0){ return main_rom_name; }
	if (main_loaded_rom_path[0] != 0){
		return main_path_basename(main_loaded_rom_path);
	}
	return "(no ROM loaded)";
}

char const* mainui_get_current_rom_author(void)
{
	if (!main_rom_is_uze){ return ""; }
	return (char const*)(&(main_rom_head.author[0]));
}

auint mainui_get_current_rom_year(void)
{
	if (!main_rom_is_uze){ return 0U; }
	return main_rom_head.year;
}


static uint32 main_hash_step(uint32 h, uint32 v)
{
	h ^= v;
	h *= 16777619U;
	return h;
}

uint32 mainui_get_input_topology_hash(void)
{
	uint32 h = 2166136261U;
	auint port;
	auint slot;
	auint id;
	for (port = 0U; port < CU_MULTITAP_MAX_PORTS; port++){
		h = main_hash_step(h, cu_multitap_get_tap_present(port) ? 1U : 0U);
		h = main_hash_step(h, cu_multitap_get_active_slot(port) & 0xFFU);
		h = main_hash_step(h, cu_multitap_get_probe_pending(port) ? 1U : 0U);
		for (slot = 0U; slot < CU_MULTITAP_MAX_SLOTS; slot++){
			h = main_hash_step(h, cu_multitap_get_slot_vdev(port, slot));
		}
	}
	for (id = 0U; id < CU_VDEV_MAX; id++){
		cu_vdev_t const* v = cu_vdev_get(id);
		auint b;
		if (v == NULL){
			h = main_hash_step(h, 0xFFFFFFFFU);
			continue;
		}
		h = main_hash_step(h, v->used ? 1U : 0U);
		h = main_hash_step(h, (uint32)v->type);
		h = main_hash_step(h, (uint32)v->options);
		h = main_hash_step(h, (uint32)(v->low16 & 0xFFFFU));
		h = main_hash_step(h, (uint32)(v->high16 & 0xFFFFU));
		for (b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			h = main_hash_step(h, (uint32)v->binding[b].type);
			h = main_hash_step(h, (uint32)(v->binding[b].flags & 0xFFFFU));
		}
	}
	return h;
}

uint32 mainui_get_current_rom_crc32(void)
{
	if (!main_rom_is_uze){ return 0U; }
	return (uint32)main_rom_head.crc32;
}

boole mainui_get_current_rom_is_uze(void)
{
	return main_rom_is_uze;
}


static boole main_uze_icon_has_visible_pixels(uint8 const* icon)
{
	auint i;
	if (icon == NULL){ return FALSE; }
	for (i = 0U; i < 256U; i++){
		if (icon[i] != 0U){
			return TRUE;
		}
	}
	return FALSE;
}

uint8 const* mainui_get_current_rom_icon(void)
{
	if (!main_rom_is_uze){ return NULL; }
	if (!main_uze_icon_has_visible_pixels(&(main_rom_head.icon[0]))){ return NULL; }
	return &(main_rom_head.icon[0]);
}

uint32 mainui_get_frame_counter(void)
{
	return main_frame_counter;
}

boole mainui_get_input_trace_enabled(void)
{
	return main_input_trace_enabled;
}

void mainui_set_input_trace_enabled(boole enable)
{
	main_input_trace_enabled = enable ? TRUE : FALSE;
}

void mainui_clear_input_trace(void)
{
	main_input_trace_head = 0U;
	main_input_trace_count = 0U;
}

auint mainui_get_input_trace_line_count(void)
{
	return main_input_trace_count;
}

auint mainui_get_input_trace_capacity(void)
{
	return MAIN_INPUT_TRACE_CAP;
}

void mainui_format_input_trace_line(auint idx, char* out, auint out_size)
{
	if ((out == NULL) || (out_size == 0U)){
		return;
	}
	if (idx >= main_input_trace_count){
		out[0] = 0;
		return;
	}
	main_copy_str(out, out_size,
		main_input_trace_lines[(main_input_trace_head + idx) % MAIN_INPUT_TRACE_CAP]);
}

char const* mainui_get_loaded_rom_path(void)
{
	return main_loaded_rom_path;
}

boole mainui_get_recent_rom_path(auint idx, char* out, auint out_size)
{
	if ((out == NULL) || (out_size == 0U)){ return FALSE; }
	out[0] = 0;
	if (idx >= MAINUI_RECENT_ROMS){ return FALSE; }
	if (main_cfg.recent_roms[idx][0] == 0){ return FALSE; }
	strncpy(out, main_cfg.recent_roms[idx], out_size - 1U);
	out[out_size - 1U] = 0;
	return TRUE;
}

boole mainui_load_recent_rom(auint idx)
{
	char path[APPCFG_PATH_MAX];
	if (idx >= MAINUI_RECENT_ROMS){ return FALSE; }
	if (main_cfg.recent_roms[idx][0] == 0){ return FALSE; }
	strncpy(path, main_cfg.recent_roms[idx], sizeof(path) - 1U);
	path[sizeof(path) - 1U] = 0;
	return main_load_rom_file_internal(path);
}

void mainui_clear_recent_roms(void)
{
	main_recent_rom_clear();
}

boole mainui_get_recent_roms_frozen(void)
{
	return main_cfg.recent_roms_frozen;
}

void mainui_set_recent_roms_frozen(boole frozen)
{
	main_cfg.recent_roms_frozen = frozen ? TRUE : FALSE;
	main_cfg_save_immediate();
}

void mainui_reset_rom(void)
{
	if (main_loaded_rom_path[0] != 0){
		boole ok;
		main_suppress_rom_load_system_message = TRUE;
		ok = main_load_rom_file_internal(main_loaded_rom_path);
		main_suppress_rom_load_system_message = FALSE;
		if (ok){
			main_system_message("ROM RESET");
		}else{
			main_system_error("ROM RESET FAILED");
		}
		return;
	}
	cu_avr_reset();
	main_netplay_runtime_reset();
	main_t5_cc = cu_avr_getcycle();
	audio_reset();
	textgui_reset();
	main_ispause = FALSE;
	main_isadvfr = FALSE;
#ifdef ENABLE_DEBUGGER
	mainui_debug_nav_clear();
#endif
	main_system_message("ROM RESET");
}

void mainui_request_quit(void)
{
	main_exit = TRUE;
}

boole mainui_get_video_dump_active(void)
{
#ifdef ENABLE_VCAP
	return main_isvcap;
#else
	return FALSE;
#endif
}

char const* mainui_get_video_dump_file(void)
{
#ifdef ENABLE_VCAP
	return avconv_get_filename();
#else
	return "";
#endif
}

char const* mainui_get_video_dump_active_file(void)
{
#ifdef ENABLE_VCAP
	return avconv_get_active_filename();
#else
	return "";
#endif
}

char const* mainui_get_video_dump_status(void)
{
#ifdef ENABLE_VCAP
	return avconv_get_status();
#else
	return "VIDEO DUMP IS DISABLED IN THIS BUILD.";
#endif
}

boole mainui_get_video_dump_autoinc(void)
{
#ifdef ENABLE_VCAP
	return avconv_get_autoinc();
#else
	return FALSE;
#endif
}

boole mainui_get_video_dump_reset_first(void)
{
#ifdef ENABLE_VCAP
	return main_cfg.video_dump_reset_first;
#else
	return FALSE;
#endif
}

void mainui_set_video_dump_active(boole active)
{
#ifdef ENABLE_VCAP
	if (active){
		if (!main_isvcap){
			/* Reset while capture is still disabled so the recording starts at
			 * the first post-reset emulation frame rather than including reload
			 * work or the previous game's final frame. */
			if (main_cfg.video_dump_reset_first){
				mainui_reset_rom();
			}
			main_system_message("VIDEO DUMP STARTED");
		}
		main_isvcap = TRUE;
	}else{
		if (main_isvcap){
			avconv_finalize();
			if (avconv_get_status()[0] != 0){
				main_system_message("%s", avconv_get_status());
			}else{
				main_system_message("VIDEO DUMP STOPPED");
			}
		}
		main_isvcap = FALSE;
	}
#else
	(void)active;
#endif
}

void mainui_set_video_dump_file(char const* name)
{
#ifdef ENABLE_VCAP
	avconv_set_filename(name);
	main_cfg_mark_dirty();
#else
	(void)name;
#endif
}

void mainui_set_video_dump_autoinc(boole enable)
{
#ifdef ENABLE_VCAP
	avconv_set_autoinc(enable);
	main_cfg_mark_dirty();
#else
	(void)enable;
#endif
}

void mainui_set_video_dump_reset_first(boole enable)
{
#ifdef ENABLE_VCAP
	main_cfg.video_dump_reset_first = enable ? TRUE : FALSE;
	main_cfg_mark_dirty();
#else
	(void)enable;
#endif
}

boole mainui_get_input_capture_active(void)
{
#ifdef ENABLE_ICAP
	return main_input_capture_active;
#else
	return FALSE;
#endif
}

void mainui_set_input_capture_active(boole active)
{
#ifdef ENABLE_ICAP
	if (active){
		capture_reset();
		main_input_capture_active = TRUE;
#ifdef ENABLE_IREP
		main_system_message("INPUT REPLAY STARTED");
#else
		main_system_message("INPUT CAPTURE STARTED");
#endif
	}else{
		capture_finalize();
		main_input_capture_active = FALSE;
#ifdef ENABLE_IREP
		main_system_message("INPUT REPLAY STOPPED");
#else
		main_system_message("INPUT CAPTURE STOPPED");
#endif
	}
#else
	(void)active;
#endif
}

char const* mainui_get_input_capture_file(void)
{
#ifdef ENABLE_ICAP
	return capture_get_filename();
#else
	return "";
#endif
}

static boole main_screenshot_build_path(char* out, auint out_size)
{
	auint i;
	FILE* fp;
	char base[APPCFG_PATH_MAX + 32U];
	char candidate[APPCFG_PATH_MAX + 96U];
	char stem[64];
	auint j = 0U;
	if ((out == NULL) || (out_size == 0U)){ return FALSE; }
	if (main_cfg.screenshot_path[0] != 0){
		main_copy_str(base, sizeof(base), main_cfg.screenshot_path);
	}else{
		main_copy_str(base, sizeof(base), "screenshots");
	}
	if (!main_mkdirs(base)){ return FALSE; }
	while ((j < (sizeof(stem) - 1U)) && (main_rom_name[j] != 0)){
		char c = main_rom_name[j];
		if (!(isalnum((unsigned char)c) || (c == '_') || (c == '-'))){ c = '_'; }
		stem[j] = c;
		j++;
	}
	if (j == 0U){ strcpy(stem, "cuzebox"); }
	else{ stem[j] = 0; }
	for (i = 0U; i < 10000U; i++){
		snprintf(candidate, sizeof(candidate), "%s/%s_shot_%04u.bmp", base, stem, (unsigned)i);
		fp = fopen(candidate, "rb");
		if (fp == NULL){
			strncpy(out, candidate, out_size - 1U);
			out[out_size - 1U] = 0;
			return TRUE;
		}
		fclose(fp);
	}
	return FALSE;
}

static boole main_save_screenshot_to_path(char const* path)
{
	guicore_pixfmt_t pixfmt;
	SDL_Surface* surf;
	auint texw;
	auint texh;
	uint32 rmask;
	uint32 gmask;
	uint32 bmask;
	char dirbuf[APPCFG_PATH_MAX + 64U];
	char* slash;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	main_copy_str(dirbuf, sizeof(dirbuf), path);
	slash = strrchr(dirbuf, '/');
#ifdef WIN32
	{
		char* bslash = strrchr(dirbuf, '\\');
		if ((bslash != NULL) && ((slash == NULL) || (bslash > slash))){ slash = bslash; }
	}
#endif
	if (slash != NULL){
		*slash = 0;
		if (dirbuf[0] != 0){
			if (!main_mkdirs(dirbuf)){ return FALSE; }
		}
	}
	guicore_gettexsize(&texw, &texh);
	guicore_getpixfmt(&pixfmt);
	rmask = 0xFFUL << pixfmt.rsh;
	gmask = 0xFFUL << pixfmt.gsh;
	bmask = 0xFFUL << pixfmt.bsh;
	surf = SDL_CreateRGBSurfaceFrom((void*)guicore_getpixbuf(), (int)texw, (int)texh, 32, (int)(guicore_getpitch() * sizeof(uint32)), rmask, gmask, bmask, 0U);
	if (surf == NULL){ return FALSE; }
	if (SDL_SaveBMP(surf, path) != 0){
		SDL_FreeSurface(surf);
		return FALSE;
	}
	SDL_FreeSurface(surf);
	return TRUE;
}

boole mainui_save_screenshot_auto(char* out_path, auint out_size)
{
	char pathbuf[APPCFG_PATH_MAX + 64U];
	if (!main_screenshot_build_path(pathbuf, (auint)sizeof(pathbuf))){
		main_system_error("SCREENSHOT FAILED");
		return FALSE;
	}
	if (!main_save_screenshot_to_path(pathbuf)){
		main_system_error("SCREENSHOT FAILED");
		return FALSE;
	}
	if ((out_path != NULL) && (out_size != 0U)){
		strncpy(out_path, pathbuf, out_size - 1U);
		out_path[out_size - 1U] = 0;
	}
	main_system_message("SCREENSHOT SAVED: %s", main_path_basename(pathbuf));
	return TRUE;
}

boole mainui_save_screenshot_file(char const* path)
{
	if (!main_save_screenshot_to_path(path)){
		main_system_error("SCREENSHOT FAILED");
		return FALSE;
	}
	main_system_message("SCREENSHOT SAVED: %s", main_path_basename(path));
	return TRUE;
}

static void main_bmp_put16(uint8* p, auint v)
{
	p[0] = (uint8)(v & 0xFFU);
	p[1] = (uint8)((v >> 8) & 0xFFU);
}

static void main_bmp_put32(uint8* p, uint32 v)
{
	p[0] = (uint8)(v & 0xFFU);
	p[1] = (uint8)((v >> 8) & 0xFFU);
	p[2] = (uint8)((v >> 16) & 0xFFU);
	p[3] = (uint8)((v >> 24) & 0xFFU);
}

boole mainui_capture_screenshot_bmp(uint8** out_data, auint* out_size, auint* out_width, auint* out_height)
{
	guicore_pixfmt_t pixfmt;
	uint32 const* pixels;
	uint8* bmp;
	auint w, h, pitch, row_bytes, image_bytes, total;
	auint x, y;
	if ((out_data == NULL) || (out_size == NULL)){ return FALSE; }
	*out_data = NULL;
	*out_size = 0U;
	guicore_getpixbufsize(&w, &h);
	pixels = guicore_getpixbuf();
	pitch = guicore_getpitch();
	if ((pixels == NULL) || (w == 0U) || (h == 0U)){ return FALSE; }
	row_bytes = ((w * 3U) + 3U) & ~3U;
	image_bytes = row_bytes * h;
	total = 54U + image_bytes;
	if ((image_bytes / h) != row_bytes || total < image_bytes){ return FALSE; }
	bmp = (uint8*)malloc(total);
	if (bmp == NULL){ return FALSE; }
	memset(bmp, 0, total);
	/* BITMAPFILEHEADER */
	bmp[0] = 'B'; bmp[1] = 'M';
	main_bmp_put32(bmp + 2U, (uint32)total);
	main_bmp_put32(bmp + 10U, 54U);
	/* BITMAPINFOHEADER: uncompressed 24-bit BGR, bottom-up. */
	main_bmp_put32(bmp + 14U, 40U);
	main_bmp_put32(bmp + 18U, (uint32)w);
	main_bmp_put32(bmp + 22U, (uint32)h);
	main_bmp_put16(bmp + 26U, 1U);
	main_bmp_put16(bmp + 28U, 24U);
	main_bmp_put32(bmp + 34U, (uint32)image_bytes);
	guicore_getpixfmt(&pixfmt);
	for (y = 0U; y < h; ++y){
		uint32 const* src = pixels + ((h - 1U - y) * pitch);
		uint8* dst = bmp + 54U + (y * row_bytes);
		for (x = 0U; x < w; ++x){
			uint32 c = src[x];
			dst[(x * 3U) + 0U] = (uint8)((c >> pixfmt.bsh) & 0xFFU);
			dst[(x * 3U) + 1U] = (uint8)((c >> pixfmt.gsh) & 0xFFU);
			dst[(x * 3U) + 2U] = (uint8)((c >> pixfmt.rsh) & 0xFFU);
		}
	}
	*out_data = bmp;
	*out_size = total;
	if (out_width != NULL){ *out_width = w; }
	if (out_height != NULL){ *out_height = h; }
	return TRUE;
}

boole mainui_get_config_dirty(void)
{
	return main_cfg_dirty;
}

void mainui_set_display_gameonly(boole enable)
{
	if (main_cfg.display_gameonly == enable){ return; }
	main_cfg.display_gameonly = enable;
	main_system_message(enable ? "GAME-ONLY LAYOUT ENABLED" : "GAME-ONLY LAYOUT DISABLED");
	main_cfg_apply_runtime_later();
}

void mainui_set_display_fullscreen(boole enable)
{
	if (main_cfg.display_fullscreen == enable){ return; }
	main_cfg.display_fullscreen = enable;
	main_system_message(enable ? "FULLSCREEN ENABLED" : "FULLSCREEN DISABLED");
	main_cfg_apply_runtime_later();
}

void mainui_set_system_messages(boole enable)
{
	if (main_cfg.display_system_messages == enable){ return; }
	main_cfg.display_system_messages = enable;
	textgui_log_set_enabled(enable);
	main_cfg_mark_dirty();
}

void mainui_set_log_verbosity(auint level)
{
 if (level > CU_LOG_TRACE){ level = CU_LOG_INFO; }
 if (main_cfg.log_verbosity == level){ return; }
 main_cfg.log_verbosity = level;
 textgui_log_set_level(level);
 main_cfg_mark_dirty();
}

void mainui_set_gui_gamepad_enable(boole enable)
{
	if (main_cfg.gui_gamepad_enable == enable){ return; }
	main_cfg.gui_gamepad_enable = enable;
	main_system_message(enable ? "GUI GAMEPAD ENABLED" : "GUI GAMEPAD DISABLED");
	main_cfg_mark_dirty();
}

void mainui_set_gui_gamepad_speed_pct(auint percent)
{
	if (percent < 10U){ percent = 10U; }
	if (percent > 200U){ percent = 200U; }
	if (main_cfg.gui_gamepad_speed_pct == percent){ return; }
	main_cfg.gui_gamepad_speed_pct = percent;
	main_cfg_mark_dirty();
}

void mainui_set_gui_pause_while_open(boole enable)
{
	if (main_cfg.gui_pause_while_open == enable){ return; }
	main_cfg.gui_pause_while_open = enable;
	main_system_message(enable ? "PAUSE WHILE GUI OPEN ENABLED" : "PAUSE WHILE GUI OPEN DISABLED");
	main_cfg_mark_dirty();
}

void mainui_set_gui_virtual_keyboard(boole enable)
{
	if (main_cfg.gui_virtual_keyboard == enable){ return; }
	main_cfg.gui_virtual_keyboard = enable;
	main_system_message(enable ? "VIRTUAL KEYBOARD ENABLED" : "VIRTUAL KEYBOARD DISABLED");
	main_cfg_mark_dirty();
}

void mainui_set_frame_rate_limiter(boole enable)
{
	if (main_cfg.frame_rate_limiter == enable){ return; }
	main_cfg.frame_rate_limiter = enable;
	main_system_message(enable ? "FRAME RATE LIMITER ENABLED" : "FRAME RATE LIMITER DISABLED");
	/* Changing the limiter changes GUICORE_NOVSYNC, which recreates the SDL
	** renderer.  This setter can be called while MicroUI is being rendered into
	** the currently locked texture, so applying it immediately would destroy
	** that texture/renderer underneath guicore_update().  Defer display
	** reinitialization until guicore_update() has returned. */
	main_cfg_apply_runtime_later();
}

void mainui_set_frame_merge(boole enable)
{
	if (main_cfg.frame_merge == enable){ return; }
	main_cfg.frame_merge = enable;
	main_system_message(enable ? "FRAME MERGE ENABLED" : "FRAME MERGE DISABLED");
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_render_path(auint mode)
{
	if (mode > RENDER_PATH_STAGED){ mode = RENDER_PATH_STAGED; }
	if (main_cfg.render_path == mode){ return; }
	main_cfg.render_path = mode;
	main_cfg_apply_runtime_later();
}

void mainui_set_input_kbuzem(boole enable)
{
	if (main_cfg.input_kbuzem == enable){ return; }
	main_cfg.input_kbuzem = enable;
	main_cfg_apply_runtime();
	main_system_message(enable ? "KEYBOARD PASSTHROUGH ENABLED" : "KEYBOARD PASSTHROUGH DISABLED");
	main_cfg_mark_dirty();
}

void mainui_set_input_player2alloc(boole enable)
{
	if (main_cfg.input_player2alloc == enable){ return; }
	main_cfg.input_player2alloc = enable;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_freqscale(boole enable)
{
	if (main_cfg.audio_freqscale == enable){ return; }
	main_cfg.audio_freqscale = enable;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_s16(boole enable)
{
	if (main_cfg.audio_s16 == enable){ return; }
	main_cfg.audio_s16 = enable;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_output_rate(auint mode)
{
	if (mode > AUDIO_OUTRATE_96000){ mode = AUDIO_OUTRATE_48000; }
	if (main_cfg.audio_output_rate == mode){ return; }
	main_cfg.audio_output_rate = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_latency(auint mode)
{
	if (mode > AUDIO_LATENCY_SAFE){ mode = AUDIO_LATENCY_NORMAL; }
	if (main_cfg.audio_latency == mode){ return; }
	main_cfg.audio_latency = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_resampler(auint mode)
{
	if (mode > AUDIO_RESAMPLER_CUBIC){ mode = AUDIO_RESAMPLER_LINEAR; }
	if (main_cfg.audio_resampler == mode){ return; }
	main_cfg.audio_resampler = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_dcblock(boole enable)
{
	if (main_cfg.audio_dcblock == enable){ return; }
	main_cfg.audio_dcblock = enable;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_lowpass(auint mode)
{
	if (mode > AUDIO_LOWPASS_STRONG){ mode = AUDIO_LOWPASS_LIGHT; }
	if (main_cfg.audio_lowpass == mode){ return; }
	main_cfg.audio_lowpass = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_lowpass_quality(auint mode)
{
	if (mode > AUDIO_LOWPASS_QUALITY_HQ){ mode = AUDIO_LOWPASS_QUALITY_HQ; }
	if (main_cfg.audio_lowpass_quality == mode){ return; }
	main_cfg.audio_lowpass_quality = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_monitor_mode(auint mode)
{
	if (mode > AUDIO_MONITOR_STEREO){ mode = AUDIO_MONITOR_STEREO; }
	if (main_cfg.audio_monitor_mode == mode){ return; }
	main_cfg.audio_monitor_mode = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_monitor_width(auint percent)
{
	if (percent > 200U){ percent = 200U; }
	if (main_cfg.audio_monitor_width == percent){ return; }
	main_cfg.audio_monitor_width = percent;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_reverb(auint mode)
{
	if (mode > AUDIO_REVERB_STRONG){ mode = AUDIO_REVERB_OFF; }
	if (main_cfg.audio_reverb == mode){ return; }
	main_cfg.audio_reverb = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_audio_master_volume(auint percent)
{
	if (percent > 200U){ percent = 200U; }
	if (main_cfg.audio_master_volume == percent){ return; }
	main_cfg.audio_master_volume = percent;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_mouse_enable(boole enable)
{
	if ((main_cfg.mouse_enable == enable) && ((!enable) || (main_cfg.mouse_scale != 0U))){ return; }
	main_cfg.mouse_enable = enable;
	if (enable && (main_cfg.mouse_scale == 0U)){
		main_cfg.mouse_scale = 1U;
	}
	if (!enable){ main_cfg.mouse_scale = 0U; }
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_mouse_scale(auint scale)
{
	boole need_enable;
	if (scale > 5U){ scale = 5U; }
	need_enable = ((!main_cfg.mouse_enable) && (scale != 0U));
	if ((!need_enable) && (main_cfg.mouse_scale == scale)){ return; }
	if (need_enable){
		main_cfg.mouse_enable = TRUE;
	}
	main_cfg.mouse_scale = scale;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_filter_pre_mode(auint mode)
{
	if (mode > (auint)FILTER_PRE_BLACK_WHITE){ mode = (auint)FILTER_PRE_NONE; }
	if (main_cfg.filter_pre_mode == mode){ return; }
	main_cfg.filter_pre_mode = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_filter_scale_mode(auint mode)
{
	if (mode > FILTER_SCALE_XBR2X){ mode = FILTER_SCALE_NONE; }
	if (main_cfg.filter_scale_mode == mode){ return; }
	main_cfg.filter_scale_mode = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_filter_crt_mode(auint mode)
{
	if ((mode == 7U) || (mode == 8U) || (mode > FILTER_CRT_HEATWAVE)){ mode = FILTER_CRT_NONE; }
	if (main_cfg.filter_crt_mode == mode){ return; }
	main_cfg.filter_crt_mode = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_spiram_pages_mode(auint mode)
{
	if (mode > 9U){ mode = 0U; }
	if (main_cfg.spiram_pages_mode == mode){ return; }
	main_cfg.spiram_pages_mode = mode;
	main_cfg_mark_dirty();
	if (main_loaded_rom_path[0] != 0){
		(void)main_load_rom_file_internal(main_loaded_rom_path);
	}else{
		main_apply_spiram_policy(main_rom_is_uze, main_rom_head.spiram_banks);
	}
}

void mainui_set_sd_allow_new_files(boole enable)
{
	enable = enable ? TRUE : FALSE;
	if (main_cfg.sd_allow_new_files == enable){ return; }
	main_cfg.sd_allow_new_files = enable;
	filesys_set_sd_allow_new_files(enable);
	main_cfg_mark_dirty();
}

void mainui_set_sd_timing_preset(auint preset)
{
	cu_spisd_model_t m;
	if (preset == CU_SPISD_PRESET_CUSTOM){
		main_cfg.sd_timing_preset = CU_SPISD_PRESET_CUSTOM;
		main_sd_model_from_cfg(&m);
		m.preset = CU_SPISD_PRESET_CUSTOM;
	}else{
		if ((preset < CU_SPISD_PRESET_SLOW) || (preset > CU_SPISD_PRESET_FAST)){ preset = CU_SPISD_PRESET_NORMAL; }
		cu_spisd_model_preset(preset, &m);
	}
	main_sd_model_to_cfg(&m);
	cu_spisd_model_set(&m);
	cu_spisd_reset(cu_avr_getcycle());
	main_cfg_mark_dirty();
}

void mainui_set_sd_timing_custom(cu_spisd_model_t const* model)
{
	cu_spisd_model_t m;
	if (model == NULL){ return; }
	m = *model;
	m.preset = CU_SPISD_PRESET_CUSTOM;
	cu_spisd_model_set(&m);
	m = *cu_spisd_model_get();
	m.preset = CU_SPISD_PRESET_CUSTOM;
	main_sd_model_to_cfg(&m);
	cu_spisd_reset(cu_avr_getcycle());
	main_cfg_mark_dirty();
}

void mainui_reset_sd_card(void)
{
	cu_spisd_reset(cu_avr_getcycle());
}

void mainui_set_esp_softap_mode(auint mode)
{
	if (mode > MAINUI_ESP_SOFTAP_MODE_ENABLE){ mode = MAINUI_ESP_SOFTAP_MODE_HINT; }
	if (main_cfg.esp_softap_mode == mode){ return; }
	main_cfg.esp_softap_mode = mode;
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
}

void mainui_set_serial_route(auint route)
{
	if (cu_esp_get_serial_route() == route){ return; }
	cu_esp_set_serial_route(route);
	main_cfg_mark_dirty();
}

void mainui_set_serial_esp_model(auint model)
{
	if (cu_esp_get_serial_esp_model() == model){ return; }
	cu_esp_set_serial_esp_model(model);
	main_cfg_mark_dirty();
}

void mainui_set_esp_at_firmware_profile(auint profile)
{
	if (cu_esp_get_at_firmware_profile() == profile){ return; }
	cu_esp_set_at_firmware_profile(profile);
	main_cfg_mark_dirty();
}

void mainui_set_uart_profile(auint profile)
{
	if (cu_esp_get_uart_profile() == profile){ return; }
	cu_esp_set_uart_profile(profile);
	main_cfg_mark_dirty();
}

void mainui_set_host_serial_device_name(char const* name)
{
	cu_esp_set_host_serial_device_name(name);
	main_cfg_mark_dirty();
}

void mainui_set_host_midi_port_name(char const* name)
{
	cu_esp_set_host_midi_port_name(name);
	main_cfg_mark_dirty();
}

void mainui_set_virtual_midi_mode(auint mode)
{
	if (cu_esp_get_virtual_midi_mode() == mode){ return; }
	cu_esp_set_virtual_midi_mode(mode);
	main_cfg_mark_dirty();
}

void mainui_set_virtual_midi_port_name(char const* name)
{
	cu_esp_set_virtual_midi_port_name(name);
	main_cfg_mark_dirty();
}

void mainui_set_tcp_serial_host(char const* host)
{
	cu_esp_set_tcp_serial_host(host);
	main_cfg_mark_dirty();
}

void mainui_set_tcp_serial_port(auint port)
{
	cu_esp_set_tcp_serial_port(port);
	main_cfg_mark_dirty();
}

void mainui_set_tcp_serial_auto_reconnect(boole enable)
{
	cu_esp_set_tcp_serial_auto_reconnect(enable);
	main_cfg_mark_dirty();
}

void mainui_set_tcp_serial_mode(auint mode)
{
	if (cu_esp_get_tcp_serial_mode() == mode){ return; }
	cu_esp_set_tcp_serial_mode(mode);
	main_cfg_mark_dirty();
}

void mainui_set_rom_path(char const* path)
{
	strncpy(main_cfg.rom_path, (path == NULL) ? "" : path, APPCFG_PATH_MAX - 1U);
	main_cfg.rom_path[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg_mark_dirty();
}

void mainui_set_screenshot_path(char const* path)
{
	strncpy(main_cfg.screenshot_path, (path == NULL) ? "" : path, APPCFG_PATH_MAX - 1U);
	main_cfg.screenshot_path[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg_mark_dirty();
}

void mainui_set_save_path(char const* path)
{
	strncpy(main_cfg.save_path, (path == NULL) ? "" : path, APPCFG_PATH_MAX - 1U);
	main_cfg.save_path[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg_mark_dirty();
}

void mainui_set_controllerdb_path(char const* path)
{
	strncpy(main_cfg.controllerdb_path, (path == NULL) ? "" : path, APPCFG_PATH_MAX - 1U);
	main_cfg.controllerdb_path[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg_mark_dirty();
}

boole mainui_reload_controllerdb_now(void)
{
	asint n;
	if (main_cfg.controllerdb_path[0] == 0){
		main_controllerdb_loaded[0] = 0;
		main_system_message("CONTROLLER DB PATH EMPTY");
		return FALSE;
	}
	n = ginput_reload_controllerdb(main_cfg.controllerdb_path);
	if (n >= 0){
		main_copy_str(main_controllerdb_loaded, sizeof(main_controllerdb_loaded), main_cfg.controllerdb_path);
		main_system_message("CONTROLLER DB RELOADED: %d ENTRIES", (int)n);
		return TRUE;
	}
	main_system_error("CONTROLLER DB LOAD FAILED");
	return FALSE;
}

void mainui_set_resident_bootloader_enable(boole enable)
{
	if (main_cfg.resident_bootloader_enable == enable){ return; }
	main_cfg.resident_bootloader_enable = enable;
	main_cfg_mark_dirty();
}

void mainui_set_fast_flash(boole enable)
{
	if (main_cfg.fast_flash == enable){ return; }
	main_cfg.fast_flash = enable;
	main_cfg_mark_dirty();
}

void mainui_set_resident_bootloader_file(char const* path)
{
	strncpy(main_cfg.resident_bootloader_file, (path == NULL) ? "" : path, APPCFG_PATH_MAX - 1U);
	main_cfg.resident_bootloader_file[APPCFG_PATH_MAX - 1U] = 0;
	main_cfg_mark_dirty();
}

void mainui_set_remote_roms_host(char const* host)
{
	char prev[APPCFG_PATH_MAX];
	main_copy_str(prev, sizeof(prev), main_cfg.remote_roms_host);
	strncpy(main_cfg.remote_roms_host, ((host != NULL) && (host[0] != 0)) ? host : "uzenet.us", APPCFG_PATH_MAX - 1U);
	main_cfg.remote_roms_host[APPCFG_PATH_MAX - 1U] = 0;
	remote_roms_set_host(main_cfg.remote_roms_host);
	main_cfg_mark_dirty();
	if (strcmp(prev, main_cfg.remote_roms_host) != 0){
		main_system_message("REMOTE ROM HOST: %s", main_cfg.remote_roms_host);
	}
}

void mainui_set_gui_theme_file(char const* path)
{
	strncpy(main_gui_theme_file, (path != NULL) ? path : "", sizeof(main_gui_theme_file) - 1U);
	main_gui_theme_file[sizeof(main_gui_theme_file) - 1U] = 0;
	main_cfg_mark_dirty();
}

void mainui_apply_gui_theme_preset(char const* name)
{
#ifdef ENABLE_MICROUI
	main_gui_theme_apply_preset(name);
	main_cfg_apply_gui_runtime();
	main_system_message("GUI THEME APPLIED: %s", main_gui_theme_name);
#else
	(void)name;
#endif
	main_cfg_mark_dirty();
}

boole mainui_load_gui_theme_file(char const* path)
{
#ifdef ENABLE_MICROUI
	FILE* f;
	char buf[256];
	char* eq;
	char* key;
	char* val;
	if ((path == NULL) || (path[0] == 0)){
		main_system_error("GUI THEME LOAD FAILED");
		return FALSE;
	}
	f = fopen(path, "r");
	if (f == NULL){
		main_system_error("GUI THEME LOAD FAILED: %s", main_path_basename(path));
		return FALSE;
	}
	while (fgets(buf, sizeof(buf), f) != NULL){
		key = buf;
		while ((*key == ' ') || (*key == '\t')){ key++; }
		if ((*key == 0) || (*key == '#') || (*key == ';') || (*key == '\n')){ continue; }
		eq = strchr(key, '=');
		if (eq == NULL){ continue; }
		*eq = 0;
		val = eq + 1;
		while ((*val == ' ') || (*val == '\t')){ val++; }
		for (eq = key + strlen(key); (eq > key) && ((eq[-1] == ' ') || (eq[-1] == '\t')); eq--){ eq[-1] = 0; }
		for (eq = val + strlen(val); (eq > val) && ((eq[-1] == '\r') || (eq[-1] == '\n') || (eq[-1] == ' ') || (eq[-1] == '\t')); eq--){ eq[-1] = 0; }
		main_gui_theme_parse_keyval(key, val);
	}
	fclose(f);
	strncpy(main_gui_theme_file, path, sizeof(main_gui_theme_file) - 1U);
	main_gui_theme_file[sizeof(main_gui_theme_file) - 1U] = 0;
	main_cfg_apply_gui_runtime();
	main_cfg_mark_dirty();
	main_system_message("GUI THEME FILE LOADED: %s", main_path_basename(path));
	return TRUE;
#else
	(void)path;
	return FALSE;
#endif
}

boole mainui_save_gui_theme_file(char const* path)
{
#ifdef ENABLE_MICROUI
	FILE* f;
	auint i;
	static const char* names[APPCFG_GUI_COLOR_COUNT] = { "Text", "Border", "WindowBg", "TitleBg", "TitleText", "PanelBg", "Button", "ButtonHover", "ButtonFocus", "Base", "BaseHover", "BaseFocus", "ScrollBase", "ScrollThumb" };
	if ((path == NULL) || (path[0] == 0)){
		main_system_error("GUI THEME SAVE FAILED");
		return FALSE;
	}
	main_cfg_capture_gui_runtime();
	f = fopen(path, "w");
	if (f == NULL){
		main_system_error("GUI THEME SAVE FAILED: %s", main_path_basename(path));
		return FALSE;
	}
	fprintf(f, "GuiTheme=%s\n", main_gui_theme_name);
	for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
		fprintf(f, "GuiColor%s=%u,%u,%u,%u\n", names[i], (unsigned)main_cfg.gui_colors[i][0], (unsigned)main_cfg.gui_colors[i][1], (unsigned)main_cfg.gui_colors[i][2], (unsigned)main_cfg.gui_colors[i][3]);
	}
	fclose(f);
	strncpy(main_gui_theme_file, path, sizeof(main_gui_theme_file) - 1U);
	main_gui_theme_file[sizeof(main_gui_theme_file) - 1U] = 0;
	main_cfg_mark_dirty();
	main_system_message("GUI THEME FILE SAVED: %s", main_path_basename(path));
	return TRUE;
#else
	(void)path;
	return FALSE;
#endif
}

void mainui_config_save_now(void)
{
	main_cfg_capture_runtime();
	main_cfg_dirty = TRUE;
	if (appcfg_save(&main_cfg, main_cfgfile)){
#ifdef ENABLE_ESP
		cu_esp_save_config();
#endif
		main_cfg_dirty = FALSE;
		main_system_message("CONFIG SAVED");
	}else{
		main_system_error("CONFIG SAVE FAILED");
	}
}

void mainui_config_reload_now(void)
{
	if (appcfg_load_or_create(&main_cfg, main_cfgfile)){
		main_cfg_apply_runtime();
#ifdef ENABLE_ESP
		cu_esp_reload_config_runtime();
#endif
		main_cfg_dirty = FALSE;
		main_system_message("CONFIG RELOADED");
	}else{
		main_system_error("CONFIG RELOAD FAILED");
	}
}

void mainui_config_defaults_now(void)
{
	appcfg_defaults(&main_cfg);
	strncpy(main_gui_theme_name, "BOOTLOADER", sizeof(main_gui_theme_name) - 1U);
	main_gui_theme_name[sizeof(main_gui_theme_name) - 1U] = 0;
	strncpy(main_gui_theme_file, "themes/bootloader.cfg", sizeof(main_gui_theme_file) - 1U);
	main_gui_theme_file[sizeof(main_gui_theme_file) - 1U] = 0;
#ifdef ENABLE_ESP
	mainui_set_serial_route(CU_ESP_SERIAL_ESP_MODULE);
	mainui_set_serial_esp_model(1U);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	mainui_set_host_serial_device_name("COM3");
	mainui_set_host_midi_port_name("");
	mainui_set_virtual_midi_port_name("CUzeBox MIDI");
#else
	mainui_set_host_serial_device_name("/dev/ttyUSB0");
	mainui_set_host_midi_port_name("CUzeBox MIDI");
	mainui_set_virtual_midi_port_name("CUzeBox MIDI");
#endif
	mainui_set_tcp_serial_host("127.0.0.1");
	mainui_set_tcp_serial_port(12001U);
	mainui_set_tcp_serial_auto_reconnect(TRUE);
	mainui_set_tcp_serial_mode(CU_ESP_TCP_SERIAL_MODE_AUTO);
	mainui_set_virtual_midi_mode(CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL);
#endif
	main_cfg_apply_runtime();
	main_cfg_mark_dirty();
	main_system_message("CONFIG DEFAULTS RESTORED");
}

void mainui_touch_gui_config(void)
{
	strncpy(main_gui_theme_name, "CUSTOM", sizeof(main_gui_theme_name) - 1U);
	main_gui_theme_name[sizeof(main_gui_theme_name) - 1U] = 0;
	main_cfg_mark_dirty();
}

void mainui_touch_config(void)
{
	main_cfg_mark_dirty();
	main_input_profile_dirty = TRUE;
}

boole mainui_multitap_select_slot(auint port, auint slot)
{
	if ((port >= 2U) || (slot >= 4U)){
		return FALSE;
	}
	if (main_netplay_submit_tap_select(port, slot)){
		return TRUE;
	}
	cu_multitap_set_active_slot(port, slot);
	cu_multitap_touch_activity(port, cu_avr_getcycle());
	main_input_trace_pushf("F%u LIVE EVT TAP_SELECT P%u S%u",
		(unsigned)main_frame_counter,
		(unsigned)(port + 1U),
		(unsigned)slot);
	mainui_touch_config();
	return TRUE;
}

boole mainui_multitap_probe_next_read(auint port)
{
	if (port >= 2U){
		return FALSE;
	}
	if (main_netplay_submit_tap_probe(port)){
		return TRUE;
	}
	cu_multitap_set_probe_pending(port, 1U);
	cu_multitap_touch_activity(port, cu_avr_getcycle());
	main_input_trace_pushf("F%u LIVE EVT TAP_PROBE P%u",
		(unsigned)main_frame_counter,
		(unsigned)(port + 1U));
	return TRUE;
}

static boole main_profile_file_exists(char const* path)
{
	FILE* f;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	f = fopen(path, "rb");
	if (f == NULL){ return FALSE; }
	fclose(f);
	return TRUE;
}

static void main_profile_sanitize_name(char* out, auint out_size, char const* text)
{
	auint i = 0U;
	if ((out == NULL) || (out_size == 0U)){ return; }
	if (text == NULL){ text = "rom"; }
	while ((i < (out_size - 1U)) && (text[i] != 0)){
		char c = text[i];
		if (!(isalnum((unsigned char)c) || (c == '_') || (c == '-'))){ c = '_'; }
		out[i] = c;
		i++;
	}
	if (i == 0U){
		main_copy_str(out, out_size, "rom");
	}else{
		out[i] = 0;
	}
}

static uint32 main_profile_current_crc32(void)
{
	cu_state_cpu_t const* cpu;
	if (main_rom_is_uze && (main_rom_head.crc32 != 0U)){
		return (uint32)main_rom_head.crc32;
	}
	cpu = cu_avr_get_state();
	if (cpu == NULL){ return 0U; }
	return cu_ufile_crc32(&(cpu->crom[0]), 65536U);
}

static void mainui_input_profile_status_set(char const* text)
{
	main_copy_str(main_input_profile_status, sizeof(main_input_profile_status), (text != NULL) ? text : "");
}

static boole mainui_input_profile_build_dir(char* out, auint out_size)
{
	char stem[96];
	char leaf[128];
	char const* romname;
	uint32 crc;
	if ((out == NULL) || (out_size == 0U)){ return FALSE; }
	out[0] = 0;
	romname = mainui_get_current_rom_name();
	if ((romname == NULL) || (romname[0] == 0) || (strcmp(romname, "(no ROM loaded)") == 0)){
		return FALSE;
	}
	main_profile_sanitize_name(stem, sizeof(stem), romname);
	crc = main_profile_current_crc32();
	snprintf(leaf, sizeof(leaf), "%s_%08X", stem, (unsigned)crc);
	main_join_path(out, out_size, "profiles", leaf);
	return TRUE;
}

static void mainui_input_profile_default_path(char const* dir, char* cfg_path, auint cfg_cap)
{
	if ((cfg_path != NULL) && (cfg_cap != 0U)){ main_join_path(cfg_path, cfg_cap, dir, "input.cfg"); }
}

static boole mainui_input_profile_save_current(void)
{
	char dir[APPCFG_PATH_MAX];
	char cfg_path[APPCFG_PATH_MAX];
	app_config_t tmp;
	if (!mainui_input_profile_build_dir(dir, sizeof(dir))){
		main_input_profile_dir[0] = 0;
		mainui_input_profile_status_set("No ROM-specific input profile available.");
		main_system_message("INPUT PROFILE UNAVAILABLE");
		return FALSE;
	}
	if (!main_mkdirs(dir)){
		mainui_input_profile_status_set("Failed to create input profile directory.");
		main_system_error("INPUT PROFILE SAVE FAILED");
		return FALSE;
	}
	mainui_input_profile_default_path(dir, cfg_path, sizeof(cfg_path));
	main_cfg_capture_runtime();
	appcfg_defaults(&tmp);
	main_cfg_copy_input_profile_fields(&tmp, &main_cfg);
	if (!appcfg_save(&tmp, cfg_path)){
		mainui_input_profile_status_set("Failed to write input profile.");
		main_system_error("INPUT PROFILE SAVE FAILED");
		return FALSE;
	}
	main_copy_str(main_input_profile_dir, sizeof(main_input_profile_dir), dir);
	mainui_input_profile_status_set("Input profile saved.");
	main_input_profile_dirty = FALSE;
	main_system_message("INPUT PROFILE SAVED");
	return TRUE;
}

static boole mainui_input_profile_load_current(void)
{
	char dir[APPCFG_PATH_MAX];
	char cfg_path[APPCFG_PATH_MAX];
	app_config_t tmp;
	if (!mainui_input_profile_build_dir(dir, sizeof(dir))){
		main_input_profile_dir[0] = 0;
		mainui_input_profile_status_set("No ROM-specific input profile available.");
		main_system_message("INPUT PROFILE UNAVAILABLE");
		return FALSE;
	}
	main_copy_str(main_input_profile_dir, sizeof(main_input_profile_dir), dir);
	mainui_input_profile_default_path(dir, cfg_path, sizeof(cfg_path));
	if (!main_profile_file_exists(cfg_path)){
		mainui_input_profile_status_set("No input profile yet for this ROM.");
		main_input_profile_dirty = FALSE;
		main_system_message("NO INPUT PROFILE FOR THIS ROM");
		return FALSE;
	}
	if (!appcfg_load_or_create(&tmp, cfg_path)){
		mainui_input_profile_status_set("Failed to load input profile.");
		main_system_error("INPUT PROFILE LOAD FAILED");
		return FALSE;
	}
	main_cfg_copy_input_profile_fields(&main_cfg, &tmp);
	main_cfg_apply_runtime();
	mainui_input_profile_status_set("Input profile loaded.");
	main_input_profile_dirty = FALSE;
	main_system_message("INPUT PROFILE LOADED");
	return TRUE;
}

char const* mainui_input_profile_get_dir(void)
{
	return main_input_profile_dir;
}

char const* mainui_input_profile_get_status(void)
{
	return main_input_profile_status;
}

void mainui_input_profile_mark_dirty(void)
{
	main_input_profile_dirty = TRUE;
}

boole mainui_input_profile_save_now(void)
{
	return mainui_input_profile_save_current();
}

boole mainui_input_profile_reload(void)
{
	return mainui_input_profile_load_current();
}

#ifdef ENABLE_DEBUGGER
static void mainui_debug_symbol_status_set(char const* text)
{
	main_copy_str(main_dbg_symbol_status, sizeof(main_dbg_symbol_status), (text != NULL) ? text : "");
}

static void mainui_debug_symbol_trim(char* text)
{
	char* end;
	if (text == NULL){ return; }
	while ((*text != 0) && isspace((unsigned char)(*text))){ text++; }
	end = text + strlen(text);
	while ((end > text) && isspace((unsigned char)(end[-1]))){ end--; }
	*end = 0;
}

static boole mainui_debug_symbol_kind_parse(char const* text, auint* out_kind)
{
	char tmp[16];
	auint i;
	if ((text == NULL) || (out_kind == NULL)){ return FALSE; }
	for (i = 0U; (i < (sizeof(tmp) - 1U)) && (text[i] != 0); i++){
		char c = text[i];
		if ((c >= 'a') && (c <= 'z')){ c = (char)(c - ('a' - 'A')); }
		tmp[i] = c;
	}
	tmp[i] = 0;
	if ((strcmp(tmp, "PROG") == 0) || (strcmp(tmp, "CODE") == 0) || (strcmp(tmp, "FLASH") == 0) || (strcmp(tmp, "ROM") == 0) || (strcmp(tmp, "TEXT") == 0)){
		*out_kind = MAIN_DBG_SYMBOL_KIND_PROG;
		return TRUE;
	}
	if ((strcmp(tmp, "DATA") == 0) || (strcmp(tmp, "SRAM") == 0) || (strcmp(tmp, "RAM") == 0) || (strcmp(tmp, "IO") == 0) || (strcmp(tmp, "ABS") == 0)){
		*out_kind = MAIN_DBG_SYMBOL_KIND_DATA;
		return TRUE;
	}
	return FALSE;
}

static boole mainui_debug_symbol_add(auint kind, auint addr, char const* name)
{
	auint i;
	if ((name == NULL) || (name[0] == 0)){ return FALSE; }
	for (i = 0U; i < main_dbg_symbol_count; i++){
		if ((main_dbg_symbols[i].kind == (uint8)kind) && (main_dbg_symbols[i].addr == (uint16)(addr & 0xFFFFU))){
			main_copy_str(main_dbg_symbols[i].name, sizeof(main_dbg_symbols[i].name), name);
			return TRUE;
		}
	}
	if (main_dbg_symbol_count >= MAIN_DBG_SYMBOL_CAP){ return FALSE; }
	main_dbg_symbols[main_dbg_symbol_count].kind = (uint8)kind;
	main_dbg_symbols[main_dbg_symbol_count].addr = (uint16)(addr & 0xFFFFU);
	main_copy_str(main_dbg_symbols[main_dbg_symbol_count].name, sizeof(main_dbg_symbols[main_dbg_symbol_count].name), name);
	main_dbg_symbol_count++;
	return TRUE;
}

static char const* mainui_debug_symbol_find(auint kind, auint addr)
{
	auint i;
	for (i = 0U; i < main_dbg_symbol_count; i++){
		if ((main_dbg_symbols[i].kind == (uint8)kind) && (main_dbg_symbols[i].addr == (uint16)(addr & 0xFFFFU))){
			return main_dbg_symbols[i].name;
		}
	}
	return NULL;
}

static boole mainui_debug_symbol_find_name(char const* name, auint* out_kind, auint* out_addr)
{
	auint i;
	if ((name == NULL) || (name[0] == 0)){ return FALSE; }
	for (i = 0U; i < main_dbg_symbol_count; i++){
		if (strcmp(main_dbg_symbols[i].name, name) == 0){
			if (out_kind != NULL){ *out_kind = (auint)main_dbg_symbols[i].kind; }
			if (out_addr != NULL){ *out_addr = (auint)main_dbg_symbols[i].addr; }
			return TRUE;
		}
	}
	for (i = 0U; i < 0x100U; i++){
		char const* iname = mainui_debug_io_name(i);
		if ((iname != NULL) && (strcmp(iname, name) == 0)){
			if (out_kind != NULL){ *out_kind = MAINUI_DBG_SYMBOL_DATA; }
			if (out_addr != NULL){ *out_addr = i; }
			return TRUE;
		}
	}
	return FALSE;
}

static char const* mainui_debug_prog_label(auint word_addr)
{
	return mainui_debug_symbol_find(MAIN_DBG_SYMBOL_KIND_PROG, word_addr & 0x7FFFU);
}

static char const* mainui_debug_data_label(auint addr)
{
	return mainui_debug_symbol_find(MAIN_DBG_SYMBOL_KIND_DATA, addr & 0xFFFFU);
}

static auint mainui_debug_symbol_addr_parse(char const* text, boole* ok)
{
	char* endp;
	unsigned long v;
	if (ok != NULL){ *ok = FALSE; }
	if ((text == NULL) || (text[0] == 0)){ return 0U; }
	errno = 0;
	v = strtoul(text, &endp, 0);
	while ((endp != NULL) && (*endp != 0) && isspace((unsigned char)(*endp))){ endp++; }
	if ((errno != 0) || (endp == text) || ((endp != NULL) && (*endp != 0))){ return 0U; }
	if (ok != NULL){ *ok = TRUE; }
	return (auint)v;
}

static char const* mainui_debug_symbols_path_ext(char const* path)
{
	char const* dot = NULL;
	char const* p;
	if (path == NULL){ return NULL; }
	for (p = path; *p != 0; p++){
		if ((*p == '/') || (*p == '\\')){ dot = NULL; }
		else if (*p == '.'){ dot = p; }
	}
	return dot;
}

static boole mainui_debug_symbols_ext_eq(char const* path, char const* ext)
{
	char const* a = mainui_debug_symbols_path_ext(path);
	if ((a == NULL) || (ext == NULL)){ return FALSE; }
	while ((*a != 0) && (*ext != 0)){
		char ca = *a;
		char ce = *ext;
		if ((ca >= 'a') && (ca <= 'z')){ ca = (char)(ca - ('a' - 'A')); }
		if ((ce >= 'a') && (ce <= 'z')){ ce = (char)(ce - ('a' - 'A')); }
		if (ca != ce){ return FALSE; }
		a++;
		ext++;
	}
	return ((*a == 0) && (*ext == 0));
}

static uint16 mainui_debug_rd16le(uint8 const* p)
{
	return (uint16)(((uint16)p[0]) | (((uint16)p[1]) << 8));
}

static uint32 mainui_debug_rd32le(uint8 const* p)
{
	return ((uint32)p[0]) |
	       (((uint32)p[1]) << 8) |
	       (((uint32)p[2]) << 16) |
	       (((uint32)p[3]) << 24);
}

static boole mainui_debug_symbols_is_elf_file(char const* path)
{
	FILE* f;
	uint8 hdr[4];
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	f = fopen(path, "rb");
	if (f == NULL){ return FALSE; }
	if (fread(hdr, 1U, sizeof(hdr), f) != sizeof(hdr)){
		fclose(f);
		return FALSE;
	}
	fclose(f);
	return ((hdr[0] == 0x7FU) && (hdr[1] == 'E') && (hdr[2] == 'L') && (hdr[3] == 'F'));
}

static boole mainui_debug_symbols_read_all(char const* path, uint8** out_buf, size_t* out_size)
{
	FILE* f;
	long  flen;
	uint8* buf;
	if (out_buf != NULL){ *out_buf = NULL; }
	if (out_size != NULL){ *out_size = 0U; }
	if ((path == NULL) || (path[0] == 0) || (out_buf == NULL) || (out_size == NULL)){ return FALSE; }
	f = fopen(path, "rb");
	if (f == NULL){ return FALSE; }
	if (fseek(f, 0L, SEEK_END) != 0){ fclose(f); return FALSE; }
	flen = ftell(f);
	if (flen < 0L){ fclose(f); return FALSE; }
	if (fseek(f, 0L, SEEK_SET) != 0){ fclose(f); return FALSE; }
	buf = (uint8*)malloc((size_t)flen + 1U);
	if (buf == NULL){ fclose(f); return FALSE; }
	if ((size_t)flen != fread(buf, 1U, (size_t)flen, f)){
		free(buf);
		fclose(f);
		return FALSE;
	}
	fclose(f);
	buf[(size_t)flen] = 0U;
	*out_buf = buf;
	*out_size = (size_t)flen;
	return TRUE;
}

static boole mainui_debug_symbols_prog_section_name(char const* name)
{
	if (name == NULL){ return FALSE; }
	return ((strncmp(name, ".text", 5) == 0) ||
	        (strncmp(name, ".init", 5) == 0) ||
	        (strncmp(name, ".fini", 5) == 0) ||
	        (strncmp(name, ".vectors", 8) == 0) ||
	        (strncmp(name, ".progmem", 8) == 0) ||
	        (strncmp(name, ".rodata", 7) == 0));
}

static boole mainui_debug_symbols_data_section_name(char const* name)
{
	if (name == NULL){ return FALSE; }
	return ((strncmp(name, ".data", 5) == 0) ||
	        (strncmp(name, ".bss", 4) == 0) ||
	        (strncmp(name, ".noinit", 7) == 0) ||
	        (strncmp(name, ".common", 7) == 0));
}

static boole mainui_debug_symbols_load_text_stream(FILE* f, char const* path, char* status, auint status_size)
{
	char  line[512];
	auint loaded = 0U;
	auint ignored = 0U;
	if (f == NULL){ return FALSE; }
	while (fgets(line, sizeof(line), f) != NULL){
		char* p = line;
		char* kindtok;
		char* addrtok;
		char* nametok;
		auint kind;
		auint addr;
		boole ok;
		while ((*p != 0) && isspace((unsigned char)(*p))){ p++; }
		if ((*p == 0) || (*p == '#') || (*p == ';') || (*p == '\n') || (*p == '\r')){ continue; }
		kindtok = p;
		while ((*p != 0) && (!isspace((unsigned char)(*p)))){ p++; }
		if (*p != 0){ *p++ = 0; }
		while ((*p != 0) && isspace((unsigned char)(*p))){ p++; }
		addrtok = p;
		while ((*p != 0) && (!isspace((unsigned char)(*p)))){ p++; }
		if (*p != 0){ *p++ = 0; }
		while ((*p != 0) && isspace((unsigned char)(*p))){ p++; }
		nametok = p;
		mainui_debug_symbol_trim(nametok);
		if ((!mainui_debug_symbol_kind_parse(kindtok, &kind)) || (nametok[0] == 0)){
			ignored++;
			continue;
		}
		addr = mainui_debug_symbol_addr_parse(addrtok, &ok);
		if (!ok){
			ignored++;
			continue;
		}
		if (kind == MAIN_DBG_SYMBOL_KIND_PROG){ addr &= 0x7FFFU; }
		else{ addr &= 0xFFFFU; }
		if (mainui_debug_symbol_add(kind, addr, nametok)){ loaded++; }
		else{ ignored++; }
	}
	if (status != NULL){
		snprintf(status, status_size, "Loaded %u symbol%s from %s%s [text]", (unsigned)main_dbg_symbol_count, (main_dbg_symbol_count == 1U) ? "" : "s", path, (ignored != 0U) ? " (some lines ignored)" : "");
	}
	return (loaded != 0U);
}

static boole mainui_debug_symbols_load_map_stream(FILE* f, char const* path, char* status, auint status_size)
{
	char  line[512];
	auint loaded = 0U;
	auint ignored = 0U;
	if (f == NULL){ return FALSE; }
	while (fgets(line, sizeof(line), f) != NULL){
		char* p = line;
		char* tok[6];
		int   ntok = 0;
		char  name[MAIN_DBG_SYMBOL_NAME_CAP];
		char const* sect = NULL;
		char const* nsrc = NULL;
		auint addr = 0U;
		auint kind = 0U;
		boole ok = FALSE;
		while ((*p != 0) && isspace((unsigned char)(*p))){ p++; }
		if ((*p == 0) || (*p == '#') || (*p == '\n') || (*p == '\r')){ continue; }
		while ((*p != 0) && (ntok < 6)){
			while ((*p != 0) && isspace((unsigned char)(*p))){ *p++ = 0; }
			if (*p == 0){ break; }
			tok[ntok++] = p;
			while ((*p != 0) && (!isspace((unsigned char)(*p)))){ p++; }
		}
		if (ntok < 2){ continue; }
		if (tok[0][0] == '.'){
			sect = tok[0];
			addr = mainui_debug_symbol_addr_parse(tok[1], &ok);
			if (!ok){ ignored++; continue; }
			if (mainui_debug_symbols_prog_section_name(sect)){
				kind = MAIN_DBG_SYMBOL_KIND_PROG;
			}else if (mainui_debug_symbols_data_section_name(sect)){
				kind = MAIN_DBG_SYMBOL_KIND_DATA;
			}else{
				continue;
			}
			if ((strncmp(sect, ".text.", 6) == 0) || (strncmp(sect, ".data.", 6) == 0) || (strncmp(sect, ".bss.", 5) == 0) || (strncmp(sect, ".rodata.", 8) == 0) || (strncmp(sect, ".progmem.", 9) == 0) || (strncmp(sect, ".noinit.", 8) == 0)){
				nsrc = strrchr(sect, '.');
				if ((nsrc != NULL) && (nsrc[1] != 0)){ nsrc++; }
			}
		}else{
			addr = mainui_debug_symbol_addr_parse(tok[0], &ok);
			if ((!ok) || (tok[1][0] == '.')){ continue; }
			nsrc = tok[1];
			if ((addr & 0xFF0000UL) != 0U){
				kind = MAIN_DBG_SYMBOL_KIND_DATA;
			}else if ((addr & 0xFFFFU) < 0x0200U){
				kind = MAIN_DBG_SYMBOL_KIND_DATA;
			}else{
				kind = MAIN_DBG_SYMBOL_KIND_PROG;
			}
		}
		if ((nsrc == NULL) || (nsrc[0] == 0) || (nsrc[0] == '.') || (strcmp(nsrc, "=") == 0) || (strcmp(nsrc, "*fill*") == 0)){ continue; }
		main_copy_str(name, sizeof(name), nsrc);
		if (kind == MAIN_DBG_SYMBOL_KIND_PROG){ addr = (addr >> 1) & 0x7FFFU; }
		else{ addr &= 0xFFFFU; }
		if (mainui_debug_symbol_add(kind, addr, name)){ loaded++; }
		else{ ignored++; }
	}
	if (status != NULL){
		snprintf(status, status_size, "Loaded %u symbol%s from %s%s [map]", (unsigned)main_dbg_symbol_count, (main_dbg_symbol_count == 1U) ? "" : "s", path, (ignored != 0U) ? " (some lines ignored)" : "");
	}
	return (loaded != 0U);
}

static boole mainui_debug_symbols_load_elf_file(char const* path, char* status, auint status_size)
{
	uint8* data = NULL;
	size_t size = 0U;
	uint32 shoff;
	uint16 shentsize;
	uint16 shnum;
	uint16 shstrndx;
	uint16 i;
	auint  loaded = 0U;
	auint  ignored = 0U;
	if (!mainui_debug_symbols_read_all(path, &data, &size)){
		if (status != NULL){ snprintf(status, status_size, "Symbol open failed: %s", path); }
		return FALSE;
	}
	if ((size < 52U) || (data[0] != 0x7FU) || (data[1] != 'E') || (data[2] != 'L') || (data[3] != 'F') || (data[4] != 1U) || (data[5] != 1U)){
		free(data);
		if (status != NULL){ snprintf(status, status_size, "Unsupported ELF format: %s", path); }
		return FALSE;
	}
	shoff = mainui_debug_rd32le(data + 32U);
	shentsize = mainui_debug_rd16le(data + 46U);
	shnum = mainui_debug_rd16le(data + 48U);
	shstrndx = mainui_debug_rd16le(data + 50U);
	if ((shoff >= size) || (shentsize < 40U) || (shnum == 0U) || (shstrndx >= shnum) || ((size_t)shoff + ((size_t)shentsize * (size_t)shnum) > size)){
		free(data);
		if (status != NULL){ snprintf(status, status_size, "Corrupt ELF sections: %s", path); }
		return FALSE;
	}
	for (i = 0U; i < shnum; i++){
		uint8 const* sh = data + shoff + ((size_t)i * (size_t)shentsize);
		uint32 shtype = mainui_debug_rd32le(sh + 4U);
		uint32 syoff;
		uint32 sysize;
		uint32 syentsize;
		uint32 strsec;
		uint32 j;
		if ((shtype != 2U) && (shtype != 11U)){ continue; }
		syoff = mainui_debug_rd32le(sh + 16U);
		sysize = mainui_debug_rd32le(sh + 20U);
		strsec = mainui_debug_rd32le(sh + 24U);
		syentsize = mainui_debug_rd32le(sh + 36U);
		if ((strsec >= shnum) || (syentsize < 16U) || (syoff >= size) || ((size_t)syoff + (size_t)sysize > size)){ ignored++; continue; }
		for (j = 0U; (j + syentsize) <= sysize; j += syentsize){
			uint8 const* sym = data + syoff + (size_t)j;
			uint32 st_name = mainui_debug_rd32le(sym + 0U);
			uint32 st_value = mainui_debug_rd32le(sym + 4U);
			uint8  st_info = sym[12U];
			uint16 st_shndx = mainui_debug_rd16le(sym + 14U);
			char const* sname = NULL;
			char const* secname = NULL;
			auint kind;
			auint addr;
			if ((st_name == 0U) || (st_shndx == 0U) || (st_shndx >= shnum)){ continue; }
			if (((st_info & 0x0FU) == 3U) || ((st_info & 0x0FU) == 4U)){ continue; }
			{
				uint8 const* strsh = data + shoff + ((size_t)strsec * (size_t)shentsize);
				uint32 stroff = mainui_debug_rd32le(strsh + 16U);
				uint32 strsize = mainui_debug_rd32le(strsh + 20U);
				uint8 const* secsh = data + shoff + ((size_t)st_shndx * (size_t)shentsize);
				uint32 secnameoff = mainui_debug_rd32le(secsh + 0U);
				uint8 const* shstr = data + shoff + ((size_t)shstrndx * (size_t)shentsize);
				uint32 shstroff = mainui_debug_rd32le(shstr + 16U);
				uint32 shstrsize = mainui_debug_rd32le(shstr + 20U);
				if (((size_t)stroff + (size_t)strsize > size) || ((size_t)shstroff + (size_t)shstrsize > size) || (st_name >= strsize) || (secnameoff >= shstrsize)){ ignored++; continue; }
				sname = (char const*)(data + stroff + st_name);
				secname = (char const*)(data + shstroff + secnameoff);
			}
			if ((sname == NULL) || (sname[0] == 0) || (sname[0] == '.')){ continue; }
			if (mainui_debug_symbols_prog_section_name(secname)){
				kind = MAIN_DBG_SYMBOL_KIND_PROG;
				addr = (auint)((st_value >> 1) & 0x7FFFU);
			}else if (mainui_debug_symbols_data_section_name(secname) || ((st_value & 0xFF0000UL) != 0U)){
				kind = MAIN_DBG_SYMBOL_KIND_DATA;
				addr = (auint)(st_value & 0xFFFFU);
			}else{
				continue;
			}
			if (mainui_debug_symbol_add(kind, addr, sname)){ loaded++; }
			else{ ignored++; }
		}
	}
	/* Source/line information is deliberately loaded into the emulator core as
	** part of the same ELF import.  The web debugger and other clients only
	** consume it through the public debugger/API interface. */
	(void)cu_debug_source_load_elf(path);
	free(data);
	if (status != NULL){
		snprintf(status, status_size, "Loaded %u symbol%s, %u DWARF variable%s, %u type%s from %s%s [elf]", (unsigned)main_dbg_symbol_count, (main_dbg_symbol_count == 1U) ? "" : "s", (unsigned)cu_debug_dwarf_variable_count(), (cu_debug_dwarf_variable_count() == 1U) ? "" : "s", (unsigned)cu_debug_dwarf_type_count(), (cu_debug_dwarf_type_count() == 1U) ? "" : "s", path, (ignored != 0U) ? " (some lines ignored)" : "");
	}
	return ((loaded != 0U) || (cu_debug_source_row_count() != 0U) || (cu_debug_dwarf_variable_count() != 0U));
}

static void mainui_debug_symbols_reset_runtime(void)
{
	main_dbg_symbol_count = 0U;
	cu_debug_source_reset();
}

static void mainui_debug_profile_status_set(char const* text)
{
	main_copy_str(main_dbg_profile_status, sizeof(main_dbg_profile_status), (text != NULL) ? text : "");
}

static boole mainui_debug_profile_build_dir(char* out, auint out_size)
{
	char stem[96];
	char leaf[128];
	char const* romname;
	uint32 crc;
	if ((out == NULL) || (out_size == 0U)){ return FALSE; }
	out[0] = 0;
	romname = mainui_get_current_rom_name();
	if ((romname == NULL) || (romname[0] == 0) || (strcmp(romname, "(no ROM loaded)") == 0)){
		return FALSE;
	}
	main_profile_sanitize_name(stem, sizeof(stem), romname);
	crc = main_profile_current_crc32();
	snprintf(leaf, sizeof(leaf), "%s_%08X", stem, (unsigned)crc);
	main_join_path(out, out_size, "debugger", leaf);
	return TRUE;
}

static void mainui_debug_profile_default_paths(char const* dir, char* cfg_path, auint cfg_cap, char* cache_path, auint cache_cap, char* labels_path, auint labels_cap)
{
	if ((cfg_path != NULL) && (cfg_cap != 0U)){ main_join_path(cfg_path, cfg_cap, dir, "debugger.cfg"); }
	if ((cache_path != NULL) && (cache_cap != 0U)){ main_join_path(cache_path, cache_cap, dir, "symbols.cache"); }
	if ((labels_path != NULL) && (labels_cap != 0U)){ main_join_path(labels_path, labels_cap, dir, "labels.txt"); }
}

static void mainui_debug_profile_ensure_labels_file(char const* path)
{
	FILE* f;
	if ((path == NULL) || (path[0] == 0) || main_profile_file_exists(path)){ return; }
	f = fopen(path, "w");
	if (f == NULL){ return; }
	fprintf(f, "# Manual debugger labels loaded after imported ELF/MAP/cache symbols.\n");
	fprintf(f, "# Format: prog 1234 LabelName\n");
	fprintf(f, "#         data 0152 player_x\n");
	fclose(f);
}

static boole mainui_debug_symbols_save_text_file(char const* path)
{
	FILE* f;
	auint i;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	f = fopen(path, "w");
	if (f == NULL){ return FALSE; }
	fprintf(f, "# CUzeBox debugger symbol cache\n");
	for (i = 0U; i < main_dbg_symbol_count; i++){
		fprintf(f, "%s %04X %s\n",
		        (main_dbg_symbols[i].kind == MAIN_DBG_SYMBOL_KIND_PROG) ? "prog" : "data",
		        (unsigned)main_dbg_symbols[i].addr,
		        main_dbg_symbols[i].name);
	}
	fclose(f);
	return TRUE;
}

static boole mainui_debug_symbols_load_cache_file(char const* path)
{
	FILE* f;
	char status[160];
	boole ok;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	f = fopen(path, "r");
	if (f == NULL){ return FALSE; }
	ok = mainui_debug_symbols_load_text_stream(f, path, status, sizeof(status));
	fclose(f);
	if (ok){ mainui_debug_symbol_status_set(status); }
	return ok;
}

static boole mainui_debug_symbols_merge_labels_file(char const* path)
{
	FILE* f;
	char status[160];
	boole ok;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	f = fopen(path, "r");
	if (f == NULL){ return FALSE; }
	ok = mainui_debug_symbols_load_text_stream(f, path, status, sizeof(status));
	fclose(f);
	if (ok){ mainui_debug_symbol_status_set(status); }
	return ok;
}

static boole mainui_debug_profile_save_current(void)
{
	char dir[APPCFG_PATH_MAX];
	char cfg_path[APPCFG_PATH_MAX];
	char cache_path[APPCFG_PATH_MAX];
	char labels_path[APPCFG_PATH_MAX];
	FILE* f;
	auint slot;
	auint addr;
	boole ok = FALSE;
	if (!mainui_debug_profile_build_dir(dir, sizeof(dir))){
		mainui_debug_profile_status_set("No ROM-specific debugger profile available.");
		main_system_message("DEBUGGER PROFILE UNAVAILABLE");
		return FALSE;
	}
	if (!main_mkdirs(dir)){
		mainui_debug_profile_status_set("Failed to create debugger profile directory.");
		main_system_error("DEBUGGER PROFILE SAVE FAILED");
		return FALSE;
	}
	mainui_debug_profile_default_paths(dir, cfg_path, sizeof(cfg_path), cache_path, sizeof(cache_path), labels_path, sizeof(labels_path));
	f = fopen(cfg_path, "w");
	if (f == NULL){
		mainui_debug_profile_status_set("Failed to write debugger profile config.");
		main_system_error("DEBUGGER PROFILE SAVE FAILED");
		return FALSE;
	}
	fprintf(f, "Version=1\n");
	fprintf(f, "RomName=%s\n", mainui_get_current_rom_name());
	fprintf(f, "RomCrc32=%08X\n", (unsigned)main_profile_current_crc32());
	fprintf(f, "SymbolsFile=%s\n", main_dbg_symbol_file);
	fprintf(f, "LabelsFile=labels.txt\n");
#ifdef ENABLE_MICROUI
	for (slot = 0U; slot < MAINUI_DBG_VALUE_WATCHES; slot++){
		boole wen = FALSE;
		auint wregion = 0U;
		char  waddr[32];
		waddr[0] = 0;
		if (mui_debug_value_watch_get(slot, &wen, &wregion, waddr, sizeof(waddr))){
			fprintf(f, "Watch%u=%u,%u,%s\n", (unsigned)slot, wen ? 1U : 0U, (unsigned)wregion, waddr);
		}
	}
#endif
	for (slot = 0U; slot < MAINUI_DBG_WATCH_SLOTS; slot++){
		boole wen = FALSE;
		auint wregion = 0U;
		auint wflags = 0U;
		auint wstart = 0U;
		auint wend = 0U;
		if (mainui_debug_watchpoint_get(slot, &wen, &wregion, &wflags, &wstart, &wend) && wen){
			fprintf(f, "Watchpoint%u=1,%u,%u,%X,%X\n", (unsigned)slot, (unsigned)wregion, (unsigned)wflags, (unsigned)wstart, (unsigned)wend);
		}
	}
	addr = 0U;
	while (mainui_debug_next_breakpoint(addr, &slot)){
		fprintf(f, "Breakpoint=%04X\n", (unsigned)(slot & 0x7FFFU));
		addr = (slot + 1U) & 0x7FFFU;
		if (addr == 0U){ break; }
	}
	fclose(f);
	if (main_dbg_symbol_count != 0U){
		ok = mainui_debug_symbols_save_text_file(cache_path);
	}else{
		remove(cache_path);
		ok = TRUE;
	}
	mainui_debug_profile_ensure_labels_file(labels_path);
	main_copy_str(main_dbg_profile_dir, sizeof(main_dbg_profile_dir), dir);
	mainui_debug_profile_status_set(ok ? "Debugger profile saved." : "Debugger profile saved, but symbol cache update failed.");
	main_dbg_profile_dirty = FALSE;
	main_dbg_profile_loaded = TRUE;
	main_system_message(ok ? "DEBUGGER PROFILE SAVED" : "DEBUGGER PROFILE SAVED (CACHE PARTIAL)");
	return TRUE;
}

static boole mainui_debug_profile_load_current(void)
{
	char dir[APPCFG_PATH_MAX];
	char cfg_path[APPCFG_PATH_MAX];
	char cache_path[APPCFG_PATH_MAX];
	char labels_path[APPCFG_PATH_MAX];
	FILE* f;
	char line[512];
	char symbols_file[APPCFG_PATH_MAX];
	boole had_cfg = FALSE;
	boole symbols_loaded = FALSE;
	boole labels_loaded = FALSE;
	if (!mainui_debug_profile_build_dir(dir, sizeof(dir))){
		main_dbg_profile_dir[0] = 0;
		mainui_debug_profile_status_set("No ROM-specific debugger profile available.");
		main_dbg_profile_loaded = TRUE;
		main_system_message("DEBUGGER PROFILE UNAVAILABLE");
		return FALSE;
	}
	main_copy_str(main_dbg_profile_dir, sizeof(main_dbg_profile_dir), dir);
	mainui_debug_profile_default_paths(dir, cfg_path, sizeof(cfg_path), cache_path, sizeof(cache_path), labels_path, sizeof(labels_path));
	mainui_debug_symbols_reset_runtime();
	mainui_debug_symbol_status_set("No symbols loaded.");
	mainui_debug_set_symbols_file("");
	cu_avr_breakpoints_clear();
	mainui_debug_nav_clear();
	main_suppress_watchpoint_system_messages = TRUE;
	cu_avr_watchpoints_clear();
#ifdef ENABLE_MICROUI
	mui_debug_value_watches_reset_defaults();
#endif
	symbols_file[0] = 0;
	f = fopen(cfg_path, "r");
	if (f != NULL){
		had_cfg = TRUE;
		while (fgets(line, sizeof(line), f) != NULL){
			char* p = line;
			char* eq;
			while ((*p != 0) && isspace((unsigned char)(*p))){ p++; }
			if ((*p == 0) || (*p == '#') || (*p == ';') || (*p == '\n') || (*p == '\r')){ continue; }
			eq = strchr(p, '=');
			if (eq == NULL){ continue; }
			*eq++ = 0;
			mainui_debug_symbol_trim(p);
			mainui_debug_symbol_trim(eq);
			if (strcmp(p, "SymbolsFile") == 0){
				main_copy_str(symbols_file, sizeof(symbols_file), eq);
			}else if (strncmp(p, "Watchpoint", 10) == 0){
				unsigned slotu, enu, regu, flagu, startu, endu;
				if ((sscanf(p, "Watchpoint%u", &slotu) == 1) && (slotu < MAINUI_DBG_WATCH_SLOTS) && (sscanf(eq, "%u,%u,%u,%x,%x", &enu, &regu, &flagu, &startu, &endu) == 5)){
					mainui_debug_watchpoint_set((auint)slotu, (enu != 0U), (auint)regu, (auint)flagu, (auint)startu, (auint)endu);
				}
			}else if (strncmp(p, "Watch", 5) == 0){
				unsigned slotu, enu, regu;
				char addrbuf[32];
				if ((sscanf(p, "Watch%u", &slotu) == 1) && (slotu < MAINUI_DBG_VALUE_WATCHES) && (sscanf(eq, "%u,%u,%31s", &enu, &regu, addrbuf) == 3)){
#ifdef ENABLE_MICROUI
					mui_debug_value_watch_set((auint)slotu, (enu != 0U), (auint)regu, addrbuf);
#endif
				}
			}else if (strcmp(p, "Breakpoint") == 0){
				boole ok;
				auint bp = mainui_debug_symbol_addr_parse(eq, &ok);
				if (ok){ cu_avr_breakpoint_set(bp & 0x7FFFU, TRUE); }
			}
		}
		fclose(f);
	}
	if (symbols_file[0] != 0){
		mainui_debug_set_symbols_file(symbols_file);
		symbols_loaded = mainui_debug_load_symbols_file(symbols_file);
	}
	if ((!symbols_loaded) && main_profile_file_exists(cache_path)){
		mainui_debug_symbols_reset_runtime();
		symbols_loaded = mainui_debug_symbols_load_cache_file(cache_path);
	}
	if (main_profile_file_exists(labels_path)){
		labels_loaded = mainui_debug_symbols_merge_labels_file(labels_path);
	}
	main_suppress_watchpoint_system_messages = FALSE;
	if (!had_cfg){
		mainui_debug_profile_status_set((symbols_loaded || labels_loaded) ? "Debugger profile labels/cache loaded." : "No debugger profile yet for this ROM.");
		main_system_message((symbols_loaded || labels_loaded) ? "DEBUGGER PROFILE LABELS/CACHE LOADED" : "NO DEBUGGER PROFILE FOR THIS ROM");
	}else{
		mainui_debug_profile_status_set((symbols_loaded || labels_loaded) ? "Debugger profile loaded." : "Debugger profile loaded (no symbols). ");
		main_system_message((symbols_loaded || labels_loaded) ? "DEBUGGER PROFILE LOADED" : "DEBUGGER PROFILE LOADED (NO SYMBOLS)");
	}
	main_dbg_profile_dirty = FALSE;
	return TRUE;
}

char const* mainui_debug_get_profile_dir(void)
{
	return main_dbg_profile_dir;
}

char const* mainui_debug_get_profile_status(void)
{
	return main_dbg_profile_status;
}

void mainui_debug_profile_mark_dirty(void)
{
	main_dbg_profile_dirty = TRUE;
}

boole mainui_debug_profile_save_now(void)
{
	return mainui_debug_profile_save_current();
}

boole mainui_debug_profile_reload(void)
{
	main_dbg_profile_loaded = FALSE;
	return mainui_debug_profile_load_current();
}

boole mainui_debug_profile_ensure_loaded(void)
{
	if (main_dbg_profile_loaded){ return TRUE; }
	return mainui_debug_profile_load_current();
}

char const* mainui_debug_get_symbols_file(void)
{
	return main_dbg_symbol_file;
}

char const* mainui_debug_get_symbols_status(void)
{
	return main_dbg_symbol_status;
}

auint mainui_debug_get_symbol_count(void)
{
	return main_dbg_symbol_count;
}

void mainui_debug_set_symbols_file(char const* path)
{
	main_copy_str(main_dbg_symbol_file, sizeof(main_dbg_symbol_file), (path != NULL) ? path : "");
}

void mainui_debug_clear_symbols(void)
{
	mainui_debug_symbols_reset_runtime();
	mainui_debug_symbol_status_set("No symbols loaded.");
	main_dbg_profile_dirty = TRUE;
	main_system_message("SYMBOLS CLEARED");
}

boole mainui_debug_load_symbols_file(char const* path)
{
	FILE* f;
	char  status[160];
	boole ok = FALSE;
	if ((path == NULL) || (path[0] == 0)){
		mainui_debug_symbol_status_set("No symbol file selected.");
		main_system_error("SYMBOL LOAD FAILED");
		return FALSE;
	}
	mainui_debug_set_symbols_file(path);
	mainui_debug_symbols_reset_runtime();
	status[0] = 0;
	if (mainui_debug_symbols_is_elf_file(path) || mainui_debug_symbols_ext_eq(path, ".elf")){
		ok = mainui_debug_symbols_load_elf_file(path, status, sizeof(status));
		mainui_debug_symbol_status_set(status);
		main_dbg_profile_dirty = TRUE;
		if (ok){ main_system_message("SYMBOLS LOADED: %s", main_path_basename(path)); }
		else{ main_system_error("SYMBOL LOAD FAILED: %s", main_path_basename(path)); }
		return ok;
	}
	f = fopen(path, "r");
	if (f == NULL){
		snprintf(status, sizeof(status), "Symbol open failed: %s", path);
		mainui_debug_symbol_status_set(status);
		main_system_error("SYMBOL LOAD FAILED: %s", main_path_basename(path));
		return FALSE;
	}
	if (mainui_debug_symbols_ext_eq(path, ".map")){
		ok = mainui_debug_symbols_load_map_stream(f, path, status, sizeof(status));
	}else{
		ok = mainui_debug_symbols_load_text_stream(f, path, status, sizeof(status));
		if (!ok){
			mainui_debug_symbols_reset_runtime();
			if (fseek(f, 0L, SEEK_SET) == 0){
				ok = mainui_debug_symbols_load_map_stream(f, path, status, sizeof(status));
			}
		}
	}
	fclose(f);
	mainui_debug_symbol_status_set(status);
	main_dbg_profile_dirty = TRUE;
	if (ok){ main_system_message("SYMBOLS LOADED: %s", main_path_basename(path)); }
	else{ main_system_error("SYMBOL LOAD FAILED: %s", main_path_basename(path)); }
	return ok;
}

static auint mainui_debug_get_prog_word_raw(auint word_addr);
static auint mainui_debug_disasm_word_count(auint opcode);

static void mainui_debug_run_status_set(char const* text)
{
	main_copy_str(main_dbg_run_status, sizeof(main_dbg_run_status), (text != NULL) ? text : "");
}

static void mainui_debug_report_break(auint addr)
{
	auint ws, wr, wf, wa, wv, wp;
	if (mainui_debug_get_last_watch_hit(&ws, &wr, &wf, &wa, &wv, &wp)){
		print_message("Watchpoint hit: slot %u %s %04X=%02X @ %04X\n",
		              (unsigned)(ws + 1U),
		              ((wf & MAINUI_DBG_WATCH_WRITE) != 0U) ? "W" : "R",
		              (unsigned)wa, (unsigned)(wv & 0xFFU), (unsigned)(wp & 0x7FFFU));
		return;
	}
#if (FLAG_SELFCONT == 0)
	{
	 cu_spisd_trace_event_t sd;
	 if (cu_spisd_trace_break_notice_get(TRUE, &sd)){
		mainui_debug_run_status_set("SD trace breakpoint.");
		if (sd.kind == CU_SPISD_TRACE_KIND_INIT_FAIL){
			print_message("SD trace break: initialization failure @ %04X (cycle %u)\n", (unsigned)(sd.pc & 0x7FFFU), (unsigned)sd.cycle);
		}else if (sd.kind == CU_SPISD_TRACE_KIND_DATA_CRC){
			print_message("SD trace break: write data CRC reject, sector %u @ %04X\n", (unsigned)sd.sector, (unsigned)(sd.pc & 0x7FFFU));
		}else if (sd.kind == CU_SPISD_TRACE_KIND_LATENCY){
			print_message("SD trace break: %sCMD%u response latency %u cycles @ %04X\n", sd.app ? "A" : "", (unsigned)sd.cmd, (unsigned)sd.latency_cycles, (unsigned)(sd.pc & 0x7FFFU));
		}else{
			print_message("SD trace break: %sCMD%u sector %u R1=%02X @ %04X\n", sd.app ? "A" : "", (unsigned)sd.cmd, (unsigned)sd.sector, (unsigned)sd.r1, (unsigned)(sd.pc & 0x7FFFU));
		}
		return;
	 }
	}
#endif
	print_message("Breakpoint hit at %04X\n", (unsigned)(addr & 0x7FFFU));
}

static void mainui_debug_nav_clear(void)
{
	cu_avr_temp_break_clear();
	cu_avr_debug_step_clear();
	mainui_debug_run_status_set("Idle.");
}

static void mainui_debug_prepare_run_control(void)
{
	main_dbg_break_hit = FALSE;
	main_dbg_break_addr = 0U;
	(void)cu_avr_watchpoint_get_last(TRUE, NULL, NULL, NULL, NULL, NULL, NULL);
#if (FLAG_SELFCONT == 0)
	(void)cu_spisd_trace_break_notice_get(TRUE, NULL);
#endif
}

static boole mainui_debug_decode_prog(auint word_addr, auint* out_word0, auint* out_word1, auint* out_opcode, auint* out_opid, auint* out_arg1, auint* out_arg2, auint* out_disp, auint* out_words)
{
	auint word0 = mainui_debug_get_prog_word_raw(word_addr);
	auint word1 = mainui_debug_get_prog_word_raw((word_addr + 1U) & 0x7FFFU);
	auint opcode = cu_avrc_compile(word0, word1);
	if (out_word0 != NULL){ *out_word0 = word0; }
	if (out_word1 != NULL){ *out_word1 = word1; }
	if (out_opcode != NULL){ *out_opcode = opcode; }
	if (out_opid != NULL){ *out_opid = opcode & 0x7FU; }
	if (out_arg1 != NULL){ *out_arg1 = (opcode >> 8) & 0xFFU; }
	if (out_arg2 != NULL){ *out_arg2 = (opcode >> 16) & 0xFFFFU; }
	if (out_disp != NULL){ *out_disp = (opcode >> 24) & 0x3FU; }
	if (out_words != NULL){ *out_words = mainui_debug_disasm_word_count(opcode); }
	return TRUE;
}

static boole mainui_debug_is_call_like_opid(auint opid)
{
	switch (opid & 0x7FU){
		case 0x2DU: /* CALL */
		case 0x32U: /* ICALL */
		case 0x41U: /* RCALL */
			return TRUE;
		default:
			return FALSE;
	}
}

static boole mainui_debug_step_out_target(auint* out_word_addr)
{
	cu_state_cpu_t const* cpu = cu_avr_get_state();
	auint sp;
	auint hi;
	auint lo;
	if (cpu == NULL){ return FALSE; }
	sp = ((auint)(cpu->iors[CU_IO_SPL])) | (((auint)(cpu->iors[CU_IO_SPH])) << 8);
	/*
	** CALL/RCALL/ICALL push a 16-bit return address and leave SP two bytes
	** below the stored pair. RET/RETI then increment SP, read the high byte,
	** increment again, and read the low byte. So a paused frame's current
	** return target is best approximated from SP+1 / SP+2.
	*/
	if ((sp + 2U) >= 0x1100U){ return FALSE; }
	hi = cpu->sram[(sp + 1U) & 0x0FFFU];
	lo = cpu->sram[(sp + 2U) & 0x0FFFU];
	if (out_word_addr != NULL){ *out_word_addr = (((hi << 8) | lo) & 0x7FFFU); }
	return TRUE;
}

static auint mainui_debug_get_prog_word_raw(auint word_addr)
{
	cu_state_cpu_t const* cpu = cu_avr_get_state();
	auint byte_addr = (word_addr & 0x7FFFU) << 1;
	return ((auint)(cpu->crom[byte_addr + 1U]) << 8) | (auint)(cpu->crom[byte_addr + 0U]);
}

static auint mainui_debug_disasm_word_count(auint opcode)
{
	return ((opcode & 0x80U) != 0U) ? 2U : 1U;
}

static auint mainui_debug_mask_bit(auint mask)
{
	auint bit;
	for (bit = 0U; bit < 16U; bit++){
		if ((mask & ((auint)1U << bit)) != 0U){ return bit; }
	}
	return 0U;
}

static char const* mainui_debug_ptr_name(auint reg)
{
	switch (reg & 0xFFU){
		case 26U: return "X";
		case 28U: return "Y";
		case 30U: return "Z";
		default:  return "?";
	}
}

static void mainui_debug_reg_name(char* buf, auint size, auint reg)
{
	if ((buf == NULL) || (size == 0U)){ return; }
	snprintf(buf, size, "r%u", (unsigned)(reg & 0x1FU));
}

static void mainui_debug_regpair_name(char* buf, auint size, auint reg)
{
	if ((buf == NULL) || (size == 0U)){ return; }
	snprintf(buf, size, "r%u:r%u", (unsigned)(((reg & 0x1FU) + 1U) & 0x1FU), (unsigned)(reg & 0x1FU));
}

static void mainui_debug_rel_target(char* buf, auint size, auint word_addr, auint rel)
{
	sint32 offs;
	auint  dst;
	char const* label;
	if ((buf == NULL) || (size == 0U)){ return; }
	offs = (sint32)(sint16)(rel & 0xFFFFU);
	dst = (auint)(((sint32)(word_addr & 0x7FFFU) + 1 + offs) & 0x7FFF);
	label = mainui_debug_prog_label(dst);
	if ((label != NULL) && (label[0] != 0)){
		snprintf(buf, size, "%s", label);
	}else{
		snprintf(buf, size, "%04X", (unsigned)dst);
	}
}

static char const* mainui_debug_io_name(auint addr)
{
	switch (addr & 0xFFU){
		case CU_IO_PINA: return "PINA";
		case CU_IO_DDRA: return "DDRA";
		case CU_IO_PORTA: return "PORTA";
		case CU_IO_PINB: return "PINB";
		case CU_IO_DDRB: return "DDRB";
		case CU_IO_PORTB: return "PORTB";
		case CU_IO_PINC: return "PINC";
		case CU_IO_DDRC: return "DDRC";
		case CU_IO_PORTC: return "PORTC";
		case CU_IO_PIND: return "PIND";
		case CU_IO_DDRD: return "DDRD";
		case CU_IO_PORTD: return "PORTD";
		case CU_IO_TIFR0: return "TIFR0";
		case CU_IO_TIFR1: return "TIFR1";
		case CU_IO_TIFR2: return "TIFR2";
		case CU_IO_PCIFR: return "PCIFR";
		case CU_IO_EIFR: return "EIFR";
		case CU_IO_EIMSK: return "EIMSK";
		case CU_IO_GPIOR0: return "GPIOR0";
		case CU_IO_EECR: return "EECR";
		case CU_IO_EEDR: return "EEDR";
		case CU_IO_EEARL: return "EEARL";
		case CU_IO_EEARH: return "EEARH";
		case CU_IO_GTCCR: return "GTCCR";
		case CU_IO_TCCR0A: return "TCCR0A";
		case CU_IO_TCCR0B: return "TCCR0B";
		case CU_IO_TCNT0: return "TCNT0";
		case CU_IO_OCR0A: return "OCR0A";
		case CU_IO_OCR0B: return "OCR0B";
		case CU_IO_GPIOR1: return "GPIOR1";
		case CU_IO_GPIOR2: return "GPIOR2";
		case CU_IO_SPCR: return "SPCR";
		case CU_IO_SPSR: return "SPSR";
		case CU_IO_SPDR: return "SPDR";
		case CU_IO_ACSR: return "ACSR";
		case CU_IO_OCDR: return "OCDR";
		case CU_IO_SMCR: return "SMCR";
		case CU_IO_MCUSR: return "MCUSR";
		case CU_IO_MCUCR: return "MCUCR";
		case CU_IO_SPMCSR: return "SPMCSR";
		case CU_IO_SPL: return "SPL";
		case CU_IO_SPH: return "SPH";
		case CU_IO_SREG: return "SREG";
		case CU_IO_WDTCSR: return "WDTCSR";
		case CU_IO_CLKPR: return "CLKPR";
		case CU_IO_PRR: return "PRR";
		case CU_IO_OSCCAL: return "OSCCAL";
		case CU_IO_PCICR: return "PCICR";
		case CU_IO_EICRA: return "EICRA";
		case CU_IO_PCMSK0: return "PCMSK0";
		case CU_IO_PCMSK1: return "PCMSK1";
		case CU_IO_PCMSK2: return "PCMSK2";
		case CU_IO_TIMSK0: return "TIMSK0";
		case CU_IO_TIMSK1: return "TIMSK1";
		case CU_IO_TIMSK2: return "TIMSK2";
		case CU_IO_PCMSK3: return "PCMSK3";
		case CU_IO_ADCL: return "ADCL";
		case CU_IO_ADCH: return "ADCH";
		case CU_IO_ADCSRA: return "ADCSRA";
		case CU_IO_ADCSRB: return "ADCSRB";
		case CU_IO_ADMUX: return "ADMUX";
		case CU_IO_DIDR0: return "DIDR0";
		case CU_IO_DIDR1: return "DIDR1";
		case CU_IO_TCCR1A: return "TCCR1A";
		case CU_IO_TCCR1B: return "TCCR1B";
		case CU_IO_TCCR1C: return "TCCR1C";
		case CU_IO_TCNT1L: return "TCNT1L";
		case CU_IO_TCNT1H: return "TCNT1H";
		case CU_IO_ICR1L: return "ICR1L";
		case CU_IO_ICR1H: return "ICR1H";
		case CU_IO_OCR1AL: return "OCR1AL";
		case CU_IO_OCR1AH: return "OCR1AH";
		case CU_IO_OCR1BL: return "OCR1BL";
		case CU_IO_OCR1BH: return "OCR1BH";
		case CU_IO_TCCR2A: return "TCCR2A";
		case CU_IO_TCCR2B: return "TCCR2B";
		case CU_IO_TCNT2: return "TCNT2";
		case CU_IO_OCR2A: return "OCR2A";
		case CU_IO_OCR2B: return "OCR2B";
		case CU_IO_ASSR: return "ASSR";
		case CU_IO_TWBR: return "TWBR";
		case CU_IO_TWSR: return "TWSR";
		case CU_IO_TWAR: return "TWAR";
		case CU_IO_TWDR: return "TWDR";
		case CU_IO_TWCR: return "TWCR";
		case CU_IO_TWAMR: return "TWAMR";
		case CU_IO_UCSR0A: return "UCSR0A";
		case CU_IO_UCSR0B: return "UCSR0B";
		case CU_IO_UCSR0C: return "UCSR0C";
		case CU_IO_UBRR0L: return "UBRR0L";
		case CU_IO_UBRR0H: return "UBRR0H";
		case CU_IO_UDR0: return "UDR0";
		default: return NULL;
	}
}

static boole mainui_debug_data_addr_name(char* buf, auint size, auint addr)
{
	char const* iname;
	if ((buf == NULL) || (size == 0U)){ return FALSE; }
	if ((addr & 0xFFFFU) < 0x20U){
		snprintf(buf, size, "r%u", (unsigned)(addr & 0x1FU));
		return TRUE;
	}
	iname = mainui_debug_data_label(addr);
	if ((iname != NULL) && (iname[0] != 0)){
		snprintf(buf, size, "%s", iname);
		return TRUE;
	}
	if ((addr & 0xFFFFU) < 0x100U){
		iname = mainui_debug_io_name(addr);
		if (iname != NULL){
			snprintf(buf, size, "%s", iname);
			return TRUE;
		}
	}
	return FALSE;
}

static auint mainui_debug_reg_value(cu_state_cpu_t const* cpu, auint reg)
{
	if (cpu == NULL){ return 0U; }
	return cpu->iors[reg & 0x1FU];
}

static auint mainui_debug_regpair_value(cu_state_cpu_t const* cpu, auint reg)
{
	if (cpu == NULL){ return 0U; }
	return (((auint)cpu->iors[(reg + 1U) & 0x1FU]) << 8) | (auint)cpu->iors[reg & 0x1FU];
}

static auint mainui_debug_data_read_abs(cu_state_cpu_t const* cpu, auint addr)
{
	if (cpu == NULL){ return 0U; }
	if ((addr & 0xFFFFU) < 0x100U){
		return cpu->iors[addr & 0xFFU];
	}
	return cpu->sram[addr & 0x0FFFU];
}

static auint CU_UNUSED_FN mainui_debug_sreg_bit_index(auint mask)
{
	auint i;
	for (i = 0U; i < 8U; i++){
		if (((mask >> i) & 1U) != 0U){ return i; }
	}
	return 0U;
}

static void mainui_debug_append_comment(char* out, auint out_size, char const* text, char const* comment)
{
	if ((out == NULL) || (out_size == 0U)){ return; }
	if ((comment != NULL) && (comment[0] != 0)){
		snprintf(out, out_size, "%s  ; %s", text, comment);
	}else{
		snprintf(out, out_size, "%s", text);
	}
}

static void mainui_debug_build_annotation(auint word_addr, auint opid, auint arg1, auint arg2, auint disp, char* comment, auint comment_size)
{
	cu_state_cpu_t const* cpu;
	boole                 is_current;
	char                  label[32];
	auint                 a;
	auint                 v;
	auint                 ptr;
	auint                 dst;
	auint                 src;
	auint                 bit;
	if ((comment == NULL) || (comment_size == 0U)){ return; }
	comment[0] = 0;
	cpu = cu_avr_get_state();
	is_current = ((cpu != NULL) && (((cpu->pc) & 0x7FFFU) == (word_addr & 0x7FFFU)));
	switch (opid){
		case 0x48U:
			snprintf(comment, comment_size, "r%u=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)(arg2 & 0xFFU));
			break;
		case 0x38U:
			if (mainui_debug_data_addr_name(label, sizeof(label), arg2 & 0xFFU)){
				if (is_current){
					v = mainui_debug_data_read_abs(cpu, arg2 & 0xFFU);
					snprintf(comment, comment_size, "r%u=%s(%02X)", (unsigned)(arg1 & 0x1FU), label, (unsigned)v);
				}else{
					snprintf(comment, comment_size, "%s", label);
				}
			}
			break;
		case 0x39U:
			if (mainui_debug_data_addr_name(label, sizeof(label), arg1 & 0xFFU)){
				if (is_current){
					v = mainui_debug_reg_value(cpu, arg2);
					snprintf(comment, comment_size, "%s=%02X", label, (unsigned)v);
				}else{
					snprintf(comment, comment_size, "%s", label);
				}
			}
			break;
		case 0x1CU:
			if (mainui_debug_data_addr_name(label, sizeof(label), arg2 & 0xFFFFU)){
				if (is_current){
					v = mainui_debug_reg_value(cpu, arg1);
					snprintf(comment, comment_size, "%s=%02X", label, (unsigned)v);
				}else{
					snprintf(comment, comment_size, "%s", label);
				}
			}else if (is_current){
				v = mainui_debug_reg_value(cpu, arg1);
				snprintf(comment, comment_size, "[%04X]=%02X", (unsigned)(arg2 & 0xFFFFU), (unsigned)v);
			}
			break;
		case 0x20U:
			if (is_current){
				v = mainui_debug_data_read_abs(cpu, arg2 & 0xFFFFU);
				if (mainui_debug_data_addr_name(label, sizeof(label), arg2 & 0xFFFFU)){
					snprintf(comment, comment_size, "r%u=%s(%02X)", (unsigned)(arg1 & 0x1FU), label, (unsigned)v);
				}else{
					snprintf(comment, comment_size, "r%u=[%04X]=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)(arg2 & 0xFFFFU), (unsigned)v);
				}
			}else if (mainui_debug_data_addr_name(label, sizeof(label), arg2 & 0xFFFFU)){
				snprintf(comment, comment_size, "%s", label);
			}
			break;
		case 0x1DU:
		case 0x1EU:
		case 0x1FU:
			if (is_current){
				ptr = mainui_debug_regpair_value(cpu, arg2);
				a = ptr;
				if (opid == 0x1DU){ a = (ptr + disp) & 0xFFFFU; }
				if (opid == 0x1EU){ a = (ptr - 1U) & 0xFFFFU; }
				v = mainui_debug_reg_value(cpu, arg1);
				if (opid == 0x1FU){
					snprintf(comment, comment_size, "[%04X]=%02X, %s->%04X", (unsigned)a, (unsigned)v, mainui_debug_ptr_name(arg2), (unsigned)((ptr + 1U) & 0xFFFFU));
				}else if (opid == 0x1EU){
					snprintf(comment, comment_size, "%s->%04X, [%04X]=%02X", mainui_debug_ptr_name(arg2), (unsigned)a, (unsigned)a, (unsigned)v);
				}else{
					snprintf(comment, comment_size, "[%04X]=%02X", (unsigned)a, (unsigned)v);
				}
			}
			break;
		case 0x21U:
		case 0x22U:
		case 0x23U:
			if (is_current){
				ptr = mainui_debug_regpair_value(cpu, arg2);
				a = ptr;
				if (opid == 0x21U){ a = (ptr + disp) & 0xFFFFU; }
				if (opid == 0x22U){ a = (ptr - 1U) & 0xFFFFU; }
				v = mainui_debug_data_read_abs(cpu, a);
				if (opid == 0x23U){
					snprintf(comment, comment_size, "r%u=[%04X]=%02X, %s->%04X", (unsigned)(arg1 & 0x1FU), (unsigned)a, (unsigned)v, mainui_debug_ptr_name(arg2), (unsigned)((ptr + 1U) & 0xFFFFU));
				}else if (opid == 0x22U){
					snprintf(comment, comment_size, "%s->%04X, r%u=[%04X]=%02X", mainui_debug_ptr_name(arg2), (unsigned)a, (unsigned)(arg1 & 0x1FU), (unsigned)a, (unsigned)v);
				}else{
					snprintf(comment, comment_size, "r%u=[%04X]=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)a, (unsigned)v);
				}
			}
			break;
		case 0x11U:
			if (is_current){
				v = mainui_debug_reg_value(cpu, arg2);
				snprintf(comment, comment_size, "r%u=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		case 0x01U:
			if (is_current){
				v = mainui_debug_regpair_value(cpu, arg2);
				snprintf(comment, comment_size, "r%u:r%u=%04X", (unsigned)(((arg1 + 1U) & 0x1FU)), (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		case 0x15U:
		case 0x16U:
		case 0x14U:
		case 0x13U:
			if (is_current){
				dst = mainui_debug_reg_value(cpu, arg1);
				src = arg2 & 0xFFU;
				v = dst;
				if (opid == 0x15U){ v = dst | src; }
				if (opid == 0x16U){ v = dst & src; }
				if (opid == 0x14U){ v = (dst - src) & 0xFFU; }
				if (opid == 0x13U){ v = (dst - (src + (cpu->iors[CU_IO_SREG] & 1U))) & 0xFFU; }
				snprintf(comment, comment_size, "r%u=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		case 0x24U:
		case 0x25U:
		case 0x26U:
		case 0x27U:
		case 0x28U:
		case 0x29U:
		case 0x2AU:
		case 0x2BU:
			if (is_current){
				dst = mainui_debug_reg_value(cpu, arg1);
				v = dst;
				if (opid == 0x24U){ v = dst ^ 0xFFU; }
				if (opid == 0x25U){ v = (-((sint32)(dst & 0xFFU))) & 0xFFU; }
				if (opid == 0x26U){ v = ((dst >> 4) | (dst << 4)) & 0xFFU; }
				if (opid == 0x27U){ v = (dst + 1U) & 0xFFU; }
				if (opid == 0x28U){ v = ((dst & 0x80U) | (dst >> 1)) & 0xFFU; }
				if (opid == 0x29U){ v = (dst >> 1) & 0xFFU; }
				if (opid == 0x2AU){ v = (((cpu->iors[CU_IO_SREG] & 1U) << 7) | (dst >> 1)) & 0xFFU; }
				if (opid == 0x2BU){ v = (dst - 1U) & 0xFFU; }
				snprintf(comment, comment_size, "r%u=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		case 0x09U:
		case 0x0DU:
		case 0x0CU:
		case 0x08U:
		case 0x0EU:
		case 0x0FU:
		case 0x10U:
			if (is_current){
				dst = mainui_debug_reg_value(cpu, arg1);
				src = mainui_debug_reg_value(cpu, arg2);
				v = dst;
				if (opid == 0x09U){ v = (dst + src) & 0xFFU; }
				if (opid == 0x0DU){ v = (dst + src + (cpu->iors[CU_IO_SREG] & 1U)) & 0xFFU; }
				if (opid == 0x0CU){ v = (dst - src) & 0xFFU; }
				if (opid == 0x08U){ v = (dst - (src + (cpu->iors[CU_IO_SREG] & 1U))) & 0xFFU; }
				if (opid == 0x0EU){ v = dst & src; }
				if (opid == 0x0FU){ v = dst ^ src; }
				if (opid == 0x10U){ v = dst | src; }
				snprintf(comment, comment_size, "r%u=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		case 0x12U:
			if (is_current){
				dst = mainui_debug_reg_value(cpu, arg1);
				snprintf(comment, comment_size, "compare %02X vs %02X", (unsigned)dst, (unsigned)(arg2 & 0xFFU));
			}
			break;
		case 0x07U:
		case 0x0BU:
		case 0x0AU:
			if (is_current){
				dst = mainui_debug_reg_value(cpu, arg1);
				src = mainui_debug_reg_value(cpu, arg2);
				if (opid == 0x0AU){
					snprintf(comment, comment_size, "skip %s", (dst == src) ? "yes" : "no");
				}else if (opid == 0x07U){
					snprintf(comment, comment_size, "compare %02X vs %02X+C", (unsigned)dst, (unsigned)src);
				}else{
					snprintf(comment, comment_size, "compare %02X vs %02X", (unsigned)dst, (unsigned)src);
				}
			}
			break;
		case 0x3AU:
		case 0x3BU:
			if (is_current){
				dst = mainui_debug_regpair_value(cpu, arg1);
				v = (opid == 0x3AU) ? ((dst + (arg2 & 0xFFFFU)) & 0xFFFFU) : ((dst - (arg2 & 0xFFFFU)) & 0xFFFFU);
				snprintf(comment, comment_size, "r%u:r%u=%04X", (unsigned)(((arg1 + 1U) & 0x1FU)), (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		case 0x3CU:
		case 0x3EU:
		case 0x3DU:
		case 0x3FU:
			bit = mainui_debug_mask_bit(arg2);
			a = arg1 & 0xFFU;
			if (mainui_debug_data_addr_name(label, sizeof(label), a)){
				if (is_current){
					v = mainui_debug_data_read_abs(cpu, a);
					if (opid == 0x3CU){
						snprintf(comment, comment_size, "%s=%02X", label, (unsigned)(v & (~(1U << bit))));
					}else if (opid == 0x3EU){
						snprintf(comment, comment_size, "%s=%02X", label, (unsigned)(v | (1U << bit)));
					}else if (opid == 0x3DU){
						snprintf(comment, comment_size, "%s bit%u %s", label, (unsigned)bit, ((v & (1U << bit)) == 0U) ? "clear -> skip" : "set");
					}else{
						snprintf(comment, comment_size, "%s bit%u %s", label, (unsigned)bit, ((v & (1U << bit)) != 0U) ? "set -> skip" : "clear");
					}
				}else{
					snprintf(comment, comment_size, "%s", label);
				}
			}
			break;
		case 0x46U:
		case 0x47U:
			if (is_current){
				v = mainui_debug_reg_value(cpu, arg1);
				bit = mainui_debug_mask_bit(arg2);
				snprintf(comment, comment_size, "skip %s", ((opid == 0x46U) ? ((v & (1U << bit)) == 0U) : ((v & (1U << bit)) != 0U)) ? "yes" : "no");
			}
			break;
		case 0x40U:
			mainui_debug_rel_target(label, sizeof(label), word_addr, arg2);
			snprintf(comment, comment_size, "->%s", label);
			break;
		case 0x41U:
			mainui_debug_rel_target(label, sizeof(label), word_addr, arg2);
			snprintf(comment, comment_size, "call %s", label);
			break;
		case 0x42U:
		case 0x43U:
			mainui_debug_rel_target(label, sizeof(label), word_addr, arg2);
			if (is_current){
				v = cpu->iors[CU_IO_SREG] & arg1;
				snprintf(comment, comment_size, "%s -> %s", ((opid == 0x42U) ? (v != 0U) : (v == 0U)) ? "taken" : "not taken", label);
			}else{
				snprintf(comment, comment_size, "->%s", label);
			}
			break;
		case 0x1AU:
			if (is_current){
				v = mainui_debug_reg_value(cpu, arg1);
				snprintf(comment, comment_size, "push %02X", (unsigned)v);
			}
			break;
		case 0x1BU:
			if (is_current){
				v = cpu->sram[(cpu->iors[CU_IO_SPL] | ((auint)cpu->iors[CU_IO_SPH] << 8)) & 0x0FFFU];
				snprintf(comment, comment_size, "r%u<=%02X", (unsigned)(arg1 & 0x1FU), (unsigned)v);
			}
			break;
		default:
			break;
	}
}

boole mainui_debug_get_disasm(auint word_addr, char* out, auint out_size, auint* out_words)
{
	auint word0;
	auint word1;
	auint opcode;
	auint opid;
	auint arg1;
	auint arg2;
	auint disp;
	char  ra[16];
	char  rb[16];
	char  rc[16];
	char  text[160];
	char  comment[192];

	if ((out == NULL) || (out_size == 0U)){ return FALSE; }
	mainui_debug_decode_prog(word_addr, &word0, &word1, &opcode, &opid, &arg1, &arg2, &disp, out_words);
	mainui_debug_reg_name(ra, sizeof(ra), arg1);
	mainui_debug_reg_name(rb, sizeof(rb), arg2);
	mainui_debug_regpair_name(rc, sizeof(rc), arg1);
	text[0] = 0;
	comment[0] = 0;

	switch (opid){
		case 0x00U: snprintf(text, sizeof(text), "NOP"); break;
		case 0x01U: snprintf(text, sizeof(text), "MOVW %s,%s", rc, rb); break;
		case 0x02U: snprintf(text, sizeof(text), "MULS %s,%s", ra, rb); break;
		case 0x03U: snprintf(text, sizeof(text), "MULSU %s,%s", ra, rb); break;
		case 0x04U: snprintf(text, sizeof(text), "FMUL %s,%s", ra, rb); break;
		case 0x05U: snprintf(text, sizeof(text), "FMULS %s,%s", ra, rb); break;
		case 0x06U: snprintf(text, sizeof(text), "FMULSU %s,%s", ra, rb); break;
		case 0x07U: snprintf(text, sizeof(text), "CPC %s,%s", ra, rb); break;
		case 0x08U: snprintf(text, sizeof(text), "SBC %s,%s", ra, rb); break;
		case 0x09U: snprintf(text, sizeof(text), "ADD %s,%s", ra, rb); break;
		case 0x0AU: snprintf(text, sizeof(text), "CPSE %s,%s", ra, rb); break;
		case 0x0BU: snprintf(text, sizeof(text), "CP %s,%s", ra, rb); break;
		case 0x0CU: snprintf(text, sizeof(text), "SUB %s,%s", ra, rb); break;
		case 0x0DU: snprintf(text, sizeof(text), "ADC %s,%s", ra, rb); break;
		case 0x0EU: snprintf(text, sizeof(text), "AND %s,%s", ra, rb); break;
		case 0x0FU: snprintf(text, sizeof(text), "EOR %s,%s", ra, rb); break;
		case 0x10U: snprintf(text, sizeof(text), "OR %s,%s", ra, rb); break;
		case 0x11U: snprintf(text, sizeof(text), "MOV %s,%s", ra, rb); break;
		case 0x12U: snprintf(text, sizeof(text), "CPI %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x13U: snprintf(text, sizeof(text), "SBCI %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x14U: snprintf(text, sizeof(text), "SUBI %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x15U: snprintf(text, sizeof(text), "ORI %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x16U: snprintf(text, sizeof(text), "ANDI %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x17U: snprintf(text, sizeof(text), "SPM"); break;
		case 0x18U: snprintf(text, sizeof(text), "LPM %s,Z", ra); break;
		case 0x19U: snprintf(text, sizeof(text), "LPM %s,Z+", ra); break;
		case 0x1AU: snprintf(text, sizeof(text), "PUSH %s", ra); break;
		case 0x1BU: snprintf(text, sizeof(text), "POP %s", ra); break;
		case 0x1CU: snprintf(text, sizeof(text), "STS %04X,%s", (unsigned)(arg2 & 0xFFFFU), ra); break;
		case 0x1DU: snprintf(text, sizeof(text), "ST %s+%u,%s", mainui_debug_ptr_name(arg2), (unsigned)disp, ra); break;
		case 0x1EU: snprintf(text, sizeof(text), "ST -%s,%s", mainui_debug_ptr_name(arg2), ra); break;
		case 0x1FU: snprintf(text, sizeof(text), "ST %s+,%s", mainui_debug_ptr_name(arg2), ra); break;
		case 0x20U: snprintf(text, sizeof(text), "LDS %s,%04X", ra, (unsigned)(arg2 & 0xFFFFU)); break;
		case 0x21U: snprintf(text, sizeof(text), "LD %s,%s+%u", ra, mainui_debug_ptr_name(arg2), (unsigned)disp); break;
		case 0x22U: snprintf(text, sizeof(text), "LD %s,-%s", ra, mainui_debug_ptr_name(arg2)); break;
		case 0x23U: snprintf(text, sizeof(text), "LD %s,%s+", ra, mainui_debug_ptr_name(arg2)); break;
		case 0x24U: snprintf(text, sizeof(text), "COM %s", ra); break;
		case 0x25U: snprintf(text, sizeof(text), "NEG %s", ra); break;
		case 0x26U: snprintf(text, sizeof(text), "SWAP %s", ra); break;
		case 0x27U: snprintf(text, sizeof(text), "INC %s", ra); break;
		case 0x28U: snprintf(text, sizeof(text), "ASR %s", ra); break;
		case 0x29U: snprintf(text, sizeof(text), "LSR %s", ra); break;
		case 0x2AU: snprintf(text, sizeof(text), "ROR %s", ra); break;
		case 0x2BU: snprintf(text, sizeof(text), "DEC %s", ra); break;
		case 0x2CU: snprintf(text, sizeof(text), "JMP %04X", (unsigned)(arg2 & 0xFFFFU)); break;
		case 0x2DU: snprintf(text, sizeof(text), "CALL %04X", (unsigned)(arg2 & 0xFFFFU)); break;
		case 0x2EU: snprintf(text, sizeof(text), "BSET %u", (unsigned)mainui_debug_mask_bit(arg1)); break;
		case 0x2FU: snprintf(text, sizeof(text), "BCLR %u", (unsigned)mainui_debug_mask_bit(arg1)); break;
		case 0x30U: snprintf(text, sizeof(text), "IJMP"); break;
		case 0x31U: snprintf(text, sizeof(text), "RET"); break;
		case 0x32U: snprintf(text, sizeof(text), "ICALL"); break;
		case 0x33U: snprintf(text, sizeof(text), "RETI"); break;
		case 0x34U: snprintf(text, sizeof(text), "SLEEP"); break;
		case 0x35U: snprintf(text, sizeof(text), "BREAK"); break;
		case 0x36U: snprintf(text, sizeof(text), "WDR"); break;
		case 0x37U: snprintf(text, sizeof(text), "MUL %s,%s", ra, rb); break;
		case 0x38U: snprintf(text, sizeof(text), "IN %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x39U: snprintf(text, sizeof(text), "OUT %02X,%s", (unsigned)(arg1 & 0xFFU), rb); break;
		case 0x3AU: snprintf(text, sizeof(text), "ADIW %s,%u", rc, (unsigned)(arg2 & 0xFFFFU)); break;
		case 0x3BU: snprintf(text, sizeof(text), "SBIW %s,%u", rc, (unsigned)(arg2 & 0xFFFFU)); break;
		case 0x3CU: snprintf(text, sizeof(text), "CBI %02X,%u", (unsigned)(arg1 & 0xFFU), (unsigned)mainui_debug_mask_bit(arg2)); break;
		case 0x3DU: snprintf(text, sizeof(text), "SBIC %02X,%u", (unsigned)(arg1 & 0xFFU), (unsigned)mainui_debug_mask_bit(arg2)); break;
		case 0x3EU: snprintf(text, sizeof(text), "SBI %02X,%u", (unsigned)(arg1 & 0xFFU), (unsigned)mainui_debug_mask_bit(arg2)); break;
		case 0x3FU: snprintf(text, sizeof(text), "SBIS %02X,%u", (unsigned)(arg1 & 0xFFU), (unsigned)mainui_debug_mask_bit(arg2)); break;
		case 0x40U:
			mainui_debug_rel_target(rb, sizeof(rb), word_addr, arg2);
			snprintf(text, sizeof(text), "RJMP %s", rb);
			break;
		case 0x41U:
			mainui_debug_rel_target(rb, sizeof(rb), word_addr, arg2);
			snprintf(text, sizeof(text), "RCALL %s", rb);
			break;
		case 0x42U:
			mainui_debug_rel_target(rb, sizeof(rb), word_addr, arg2);
			snprintf(text, sizeof(text), "BRBS %u,%s", (unsigned)mainui_debug_mask_bit(arg1), rb);
			break;
		case 0x43U:
			mainui_debug_rel_target(rb, sizeof(rb), word_addr, arg2);
			snprintf(text, sizeof(text), "BRBC %u,%s", (unsigned)mainui_debug_mask_bit(arg1), rb);
			break;
		case 0x44U: snprintf(text, sizeof(text), "BLD %s,%u", ra, (unsigned)(arg2 & 0x7U)); break;
		case 0x45U: snprintf(text, sizeof(text), "BST %s,%u", ra, (unsigned)(arg2 & 0x7U)); break;
		case 0x46U: snprintf(text, sizeof(text), "SBRC %s,%u", ra, (unsigned)mainui_debug_mask_bit(arg2)); break;
		case 0x47U: snprintf(text, sizeof(text), "SBRS %s,%u", ra, (unsigned)mainui_debug_mask_bit(arg2)); break;
		case 0x48U: snprintf(text, sizeof(text), "LDI %s,%02X", ra, (unsigned)(arg2 & 0xFFU)); break;
		case 0x49U: snprintf(text, sizeof(text), "PIXEL %s", rb); break;
		default:
			snprintf(text, sizeof(text), "UNDEF");
			break;
	}
	mainui_debug_build_annotation(word_addr, opid, arg1, arg2, disp, comment, sizeof(comment));
	mainui_debug_append_comment(out, out_size, text, comment);
	{
		char const* plabel = mainui_debug_prog_label(word_addr);
		if ((plabel != NULL) && (plabel[0] != 0)){
			char tmp[384];
			snprintf(tmp, sizeof(tmp), "%s: %s", plabel, out);
			main_copy_str(out, out_size, tmp);
		}
	}
	return TRUE;
}

boole mainui_debug_is_paused(void)
{
	return main_ispause;
}

void mainui_debug_set_paused(boole paused)
{
	boole was_paused = main_ispause;
	main_ispause = paused;
	if (!paused){
		main_isadvfr = FALSE;
		main_dbg_break_hit = FALSE;
		main_dbg_break_addr = 0U;
	}
	if (was_paused != main_ispause){
		main_system_message(main_ispause ? "EMULATION PAUSED" : "EMULATION RESUMED");
	}
}

void mainui_debug_step_frame(void)
{
	mainui_debug_nav_clear();
	mainui_debug_prepare_run_control();
	main_isadvfr = TRUE;
	main_system_message("DEBUGGER STEP FRAME");
}

void mainui_debug_step_instruction(void)
{
	mainui_debug_nav_clear();
	mainui_debug_prepare_run_control();
	cu_avr_debug_step_instructions(1U);
	mainui_debug_run_status_set("Step instruction armed.");
	main_ispause = FALSE;
	main_isadvfr = FALSE;
	main_system_message("DEBUGGER STEP INSTRUCTION");
}

boole mainui_debug_step_over(void)
{
	cu_state_cpu_t const* cpu = cu_avr_get_state();
	auint opid;
	auint words;
	auint target;
	if (cpu == NULL){ return FALSE; }
	mainui_debug_decode_prog(cpu->pc & 0x7FFFU, NULL, NULL, NULL, &opid, NULL, NULL, NULL, &words);
	if (!mainui_debug_is_call_like_opid(opid)){
		mainui_debug_step_instruction();
		mainui_debug_run_status_set("Step-over fell back to single-instruction step.");
		return TRUE;
	}
	target = (cpu->pc + ((words == 0U) ? 1U : words)) & 0x7FFFU;
	mainui_debug_nav_clear();
	mainui_debug_prepare_run_control();
	cu_avr_temp_break_set(target, TRUE);
	snprintf(main_dbg_run_status, sizeof(main_dbg_run_status), "Step-over armed for %04X.", (unsigned)target);
	main_ispause = FALSE;
	main_isadvfr = FALSE;
	main_system_message("DEBUGGER STEP OVER: %04X", (unsigned)target);
	return TRUE;
}

boole mainui_debug_step_out(void)
{
	auint target;
	if (!mainui_debug_step_out_target(&target)){
		mainui_debug_run_status_set("Step-out unavailable (no return target found).");
		return FALSE;
	}
	mainui_debug_nav_clear();
	mainui_debug_prepare_run_control();
	cu_avr_temp_break_set(target, TRUE);
	snprintf(main_dbg_run_status, sizeof(main_dbg_run_status), "Step-out armed for %04X.", (unsigned)target);
	main_ispause = FALSE;
	main_isadvfr = FALSE;
	main_system_message("DEBUGGER STEP OUT: %04X", (unsigned)target);
	return TRUE;
}

boole mainui_debug_run_to_word(auint word_addr)
{
	auint target = word_addr & 0x7FFFU;
	mainui_debug_nav_clear();
	mainui_debug_prepare_run_control();
	cu_avr_temp_break_set(target, TRUE);
	snprintf(main_dbg_run_status, sizeof(main_dbg_run_status), "Run-to armed for %04X.", (unsigned)target);
	main_ispause = FALSE;
	main_isadvfr = FALSE;
	main_system_message("DEBUGGER RUN TO: %04X", (unsigned)target);
	return TRUE;
}

boole mainui_debug_get_run_target(auint* out_word_addr)
{
	return cu_avr_temp_break_get(out_word_addr);
}

char const* mainui_debug_get_run_control_status(void)
{
	return main_dbg_run_status;
}

void mainui_debug_get_cpu(mainui_debug_cpu_t* out)
{
	cu_state_cpu_t const* cpu;
	cu_row_t const* row;
	auint i;
	if (out == NULL){ return; }
	memset(out, 0, sizeof(*out));
	cpu = cu_avr_get_state();
	row = cu_avr_get_row();
	out->pc = cpu->pc;
	out->cycle = cpu->cycle;
	out->sp = ((auint)(cpu->iors[CU_IO_SPH]) << 8) | (auint)(cpu->iors[CU_IO_SPL]);
	out->row_pulse = row->pno;
	out->sreg = cpu->iors[CU_IO_SREG];
	for (i = 0U; i < 32U; i++){
		out->regs[i] = cpu->iors[i];
	}
}

auint mainui_debug_get_sram_byte(auint addr)
{
	cu_state_cpu_t const* cpu = cu_avr_get_state();
	if (cpu == NULL){ return 0U; }
	return cpu->sram[addr & 0x0FFFU];
}

auint mainui_debug_get_io_byte(auint addr)
{
	cu_state_cpu_t const* cpu = cu_avr_get_state();
	if (cpu == NULL){ return 0U; }
	return cpu->iors[addr & 0x00FFU];
}

auint mainui_debug_get_mem_region_size(auint region)
{
	cu_state_cpu_t const*  cpu;
	cu_state_spir_t const* spir;
	auint                 i;
	boole                 has_flash = FALSE;
	if ((main_loaded_rom_path[0] == 0) && (main_rom_name[0] == 0)){
		return 0U;
	}
	switch (region){
		case MAINUI_DBG_MEM_SRAM:
			return (cu_avr_get_state() == NULL) ? 0U : 0x1000U;
		case MAINUI_DBG_MEM_SPIRAM:
			spir = cu_spir_get_state();
			return (spir == NULL) ? 0U : spir->size;
		case MAINUI_DBG_MEM_EEPROM:
			return (cu_avr_get_state() == NULL) ? 0U : 0x0800U;
		case MAINUI_DBG_MEM_FLASH:
			cpu = cu_avr_get_state();
			if (cpu == NULL){ return 0U; }
			for (i = 0U; i < 16U; ++i){
				if ((cpu->crom[i] != 0x00U) && (cpu->crom[i] != 0xFFU)){
					has_flash = TRUE;
					break;
				}
			}
			return has_flash ? 0x10000U : 0U;
		default:
			return 0U;
	}
}

auint mainui_debug_get_mem_region_byte(auint region, auint addr)
{
	cu_state_cpu_t const*  cpu;
	cu_state_spir_t const* spir;
	if ((main_loaded_rom_path[0] == 0) && (main_rom_name[0] == 0)){
		return 0U;
	}
	switch (region){
		case MAINUI_DBG_MEM_SRAM:
			cpu = cu_avr_get_state();
			return (cpu == NULL) ? 0U : cpu->sram[addr & 0x0FFFU];
		case MAINUI_DBG_MEM_SPIRAM:
			spir = cu_spir_get_state();
			if ((spir == NULL) || (spir->ram == NULL) || (spir->size == 0U)){ return 0U; }
			if (addr >= spir->size){ return 0U; }
			return spir->ram[addr];
		case MAINUI_DBG_MEM_EEPROM:
			cpu = cu_avr_get_state();
			return (cpu == NULL) ? 0U : cpu->eepr[addr & 0x07FFU];
		case MAINUI_DBG_MEM_FLASH:
			cpu = cu_avr_get_state();
			return (cpu == NULL) ? 0U : cpu->crom[addr & 0xFFFFU];
		default:
			return 0U;
	}
}

auint mainui_debug_get_prog_word(auint word_addr)
{
	return mainui_debug_get_prog_word_raw(word_addr);
}

void mainui_debug_set_reg_byte(auint reg, auint value)
{
	cu_state_cpu_t* cpu = cu_avr_get_state();
	if (cpu == NULL){ return; }
	cpu->iors[reg & 0x1FU] = (uint8)(value & 0xFFU);
}

void mainui_debug_set_sram_byte(auint addr, auint value)
{
	cu_state_cpu_t* cpu = cu_avr_get_state();
	if (cpu == NULL){ return; }
	cpu->sram[addr & 0x0FFFU] = (uint8)(value & 0xFFU);
}

void mainui_debug_set_io_byte(auint addr, auint value)
{
	cu_state_cpu_t* cpu = cu_avr_get_state();
	if (cpu == NULL){ return; }
	cpu->iors[addr & 0x00FFU] = (uint8)(value & 0xFFU);
	cu_avr_io_update();
}

void mainui_debug_set_mem_region_byte(auint region, auint addr, auint value)
{
	cu_state_spir_t* spir;
	if ((main_loaded_rom_path[0] == 0) && (main_rom_name[0] == 0)){
		return;
	}
	switch (region){
		case MAINUI_DBG_MEM_SRAM:
			mainui_debug_set_sram_byte(addr, value);
			break;
		case MAINUI_DBG_MEM_SPIRAM:
			spir = cu_spir_get_state();
			if ((spir != NULL) && (spir->ram != NULL) && (addr < spir->size)){
				spir->ram[addr] = (uint8)(value & 0xFFU);
			}
			break;
		case MAINUI_DBG_MEM_EEPROM:
			{
				cu_state_cpu_t* cpu = cu_avr_get_state();
				if (cpu == NULL){ break; }
				cpu->eepr[addr & 0x07FFU] = (uint8)(value & 0xFFU);
				main_peepch = TRUE;
			}
			break;
		default:
			break;
	}
}

boole mainui_debug_get_breakpoint(auint word_addr)
{
	return cu_avr_breakpoint_get(word_addr & 0x7FFFU);
}

void mainui_debug_set_breakpoint(auint word_addr, boole enable)
{
	cu_avr_breakpoint_set(word_addr & 0x7FFFU, enable);
	main_dbg_profile_dirty = TRUE;
	main_system_message(enable ? "BREAKPOINT %04X SET" : "BREAKPOINT %04X CLEARED", (unsigned)(word_addr & 0x7FFFU));
}

void mainui_debug_clear_breakpoints(void)
{
	cu_avr_breakpoints_clear();
	main_dbg_profile_dirty = TRUE;
	main_system_message("ALL BREAKPOINTS CLEARED");
}

boole mainui_debug_next_breakpoint(auint start_word_addr, auint* out_word_addr)
{
	return cu_avr_breakpoint_next(start_word_addr & 0x7FFFU, out_word_addr);
}

boole mainui_debug_get_last_break_hit(auint* out_word_addr)
{
	if (out_word_addr != NULL){ *out_word_addr = main_dbg_break_addr; }
	return main_dbg_break_hit;
}

void mainui_debug_clear_last_break_hit(void)
{
	main_dbg_break_hit = FALSE;
	main_dbg_break_addr = 0U;
}

void mainui_debug_watchpoint_set(auint slot, boole enable, auint region, auint flags, auint start_addr, auint end_addr)
{
	boole prev_enable = FALSE;
	auint prev_region = 0U;
	auint prev_flags = 0U;
	auint prev_start = 0U;
	auint prev_end = 0U;
	boole prev_active;
	boole new_active;

	cu_avr_watchpoint_get(slot, &prev_enable, &prev_region, &prev_flags, &prev_start, &prev_end);
	prev_active = prev_enable && ((prev_flags & (MAINUI_DBG_WATCH_READ | MAINUI_DBG_WATCH_WRITE)) != 0U);
	new_active = enable && ((flags & (MAINUI_DBG_WATCH_READ | MAINUI_DBG_WATCH_WRITE)) != 0U);
	cu_avr_watchpoint_set(slot, enable, region, flags, start_addr, end_addr);
	main_dbg_profile_dirty = TRUE;
	if (main_suppress_watchpoint_system_messages){ return; }
	if (prev_active != new_active){
		if (new_active){
			main_system_message("WATCHPOINT %u SET: %s %s %0*X..%0*X",
					(unsigned)(slot + 1U),
					main_watchpoint_region_name(region),
					main_watchpoint_mode_name(flags),
					(int)main_watchpoint_digits(region), (unsigned)start_addr,
					(int)main_watchpoint_digits(region), (unsigned)end_addr);
		}else{
			main_system_message("WATCHPOINT %u CLEARED", (unsigned)(slot + 1U));
		}
	}else if (new_active && ((prev_region != region) || (prev_flags != flags) || (prev_start != start_addr) || (prev_end != end_addr))){
		main_system_message("WATCHPOINT %u UPDATED: %s %s %0*X..%0*X",
				(unsigned)(slot + 1U),
				main_watchpoint_region_name(region),
				main_watchpoint_mode_name(flags),
				(int)main_watchpoint_digits(region), (unsigned)start_addr,
				(int)main_watchpoint_digits(region), (unsigned)end_addr);
	}
}

boole mainui_debug_watchpoint_get(auint slot, boole* out_enable, auint* out_region, auint* out_flags, auint* out_start_addr, auint* out_end_addr)
{
	return cu_avr_watchpoint_get(slot, out_enable, out_region, out_flags, out_start_addr, out_end_addr);
}

boole mainui_debug_watchpoint_next(auint start_slot, auint* out_slot)
{
	return cu_avr_watchpoint_next(start_slot, out_slot);
}

void mainui_debug_clear_watchpoints(void)
{
	auint slot;
	boole had_any = cu_avr_watchpoint_next(0U, &slot);
	cu_avr_watchpoints_clear();
	main_dbg_profile_dirty = TRUE;
	if ((!main_suppress_watchpoint_system_messages) && had_any){
		main_system_message("ALL WATCHPOINTS CLEARED");
	}
}

boole mainui_debug_get_last_watch_hit(auint* out_slot, auint* out_region, auint* out_flags, auint* out_addr, auint* out_value, auint* out_pc)
{
	return cu_avr_watchpoint_get_last(FALSE, out_slot, out_region, out_flags, out_addr, out_value, out_pc);
}

char const* mainui_debug_get_data_label(auint addr)
{
	char const* label = mainui_debug_data_label(addr);
	if ((label != NULL) && (label[0] != 0)){
		return label;
	}
	if (mainui_debug_data_addr_name(main_dbg_label_temp, sizeof(main_dbg_label_temp), addr)){
		return main_dbg_label_temp;
	}
	return "";
}

boole mainui_debug_find_symbol(char const* name, auint* out_kind, auint* out_addr)
{
	return mainui_debug_symbol_find_name(name, out_kind, out_addr);
}

char const* mainui_debug_get_source_status(void)
{
	return cu_debug_source_status();
}

auint mainui_debug_get_source_file_count(void)
{
	return (auint)cu_debug_source_file_count();
}

auint mainui_debug_get_source_row_count(void)
{
	return (auint)cu_debug_source_row_count();
}

char const* mainui_debug_get_source_file(auint index)
{
	return cu_debug_source_file_path((uint32_t)index);
}

boole mainui_debug_source_lookup(auint word_addr, mainui_debug_source_t* out)
{
	cu_debug_source_location_t loc;
	if (!cu_debug_source_lookup((uint32_t)(word_addr & 0x7FFFU), &loc)){ return FALSE; }
	if (out != NULL){
		out->file_index = (auint)loc.file_index;
		out->line = (auint)loc.line;
		out->column = (auint)loc.column;
		out->word_addr = (auint)loc.word_addr;
		out->end_word_addr = (auint)loc.end_word_addr;
	}
	return TRUE;
}

boole mainui_debug_source_resolve_line(auint file_index, auint line, mainui_debug_source_t* out)
{
	cu_debug_source_location_t loc;
	if (!cu_debug_source_resolve_line((uint32_t)file_index, (uint32_t)line, &loc)){ return FALSE; }
	if (out != NULL){
		out->file_index = (auint)loc.file_index;
		out->line = (auint)loc.line;
		out->column = (auint)loc.column;
		out->word_addr = (auint)loc.word_addr;
		out->end_word_addr = (auint)loc.end_word_addr;
	}
	return TRUE;
}

#else
boole mainui_debug_get_disasm(auint word_addr, char* out, auint out_size, auint* out_words)
{
 (void)word_addr;
 if ((out != NULL) && (out_size > 0U)){ out[0] = 0; }
 if (out_words != NULL){ *out_words = 0U; }
 return FALSE;
}

char const* mainui_debug_get_symbols_file(void)
{
 return "";
}

char const* mainui_debug_get_symbols_status(void)
{
 return "Debugger disabled.";
}

auint mainui_debug_get_symbol_count(void)
{
 return 0U;
}

void mainui_debug_set_symbols_file(char const* path)
{
 (void)path;
}

boole mainui_debug_load_symbols_file(char const* path)
{
 (void)path;
 return FALSE;
}

void mainui_debug_clear_symbols(void)
{
}

boole mainui_debug_is_paused(void)
{
 return main_ispause;
}

void mainui_debug_set_paused(boole paused)
{
 boole was_paused = main_ispause;
 main_ispause = paused;
 if (!paused){
  main_isadvfr = FALSE;
 }
 if (was_paused != main_ispause){
  main_system_message(main_ispause ? "EMULATION PAUSED" : "EMULATION RESUMED");
 }
}

void mainui_debug_step_frame(void)
{
 main_isadvfr = TRUE;
 main_system_message("DEBUGGER STEP FRAME");
}

void mainui_debug_step_instruction(void)
{
}

boole mainui_debug_step_over(void)
{
 return FALSE;
}

boole mainui_debug_step_out(void)
{
 return FALSE;
}

boole mainui_debug_run_to_word(auint word_addr)
{
 (void)word_addr;
 return FALSE;
}

boole mainui_debug_get_run_target(auint* out_word_addr)
{
 if (out_word_addr != NULL){ *out_word_addr = 0U; }
 return FALSE;
}

char const* mainui_debug_get_run_control_status(void)
{
 return "Debugger disabled.";
}

void mainui_debug_get_cpu(mainui_debug_cpu_t* out)
{
 if (out != NULL){ memset(out, 0, sizeof(*out)); }
}

auint mainui_debug_get_sram_byte(auint addr)
{
 (void)addr;
 return 0U;
}

auint mainui_debug_get_io_byte(auint addr)
{
 (void)addr;
 return 0U;
}

auint mainui_debug_get_mem_region_size(auint region)
{
 cu_state_cpu_t const*  cpu;
 cu_state_spir_t const* spir;
 auint                 i;
 boole                 has_flash = FALSE;
 if ((main_loaded_rom_path[0] == 0) && (main_rom_name[0] == 0)){
  return 0U;
 }
 switch (region){
  case MAINUI_DBG_MEM_SRAM:
   return (cu_avr_get_state() == NULL) ? 0U : 0x1000U;
  case MAINUI_DBG_MEM_SPIRAM:
   spir = cu_spir_get_state();
   return (spir == NULL) ? 0U : spir->size;
  case MAINUI_DBG_MEM_EEPROM:
   return (cu_avr_get_state() == NULL) ? 0U : 0x0800U;
  case MAINUI_DBG_MEM_FLASH:
   cpu = cu_avr_get_state();
   if (cpu == NULL){ return 0U; }
   for (i = 0U; i < 16U; ++i){
    if ((cpu->crom[i] != 0x00U) && (cpu->crom[i] != 0xFFU)){
     has_flash = TRUE;
     break;
    }
   }
   return has_flash ? 0x10000U : 0U;
  default:
   return 0U;
 }
}

auint mainui_debug_get_mem_region_byte(auint region, auint addr)
{
 cu_state_cpu_t const* cpu;
 cu_state_spir_t const* spir;
 if ((main_loaded_rom_path[0] == 0) && (main_rom_name[0] == 0)){
  return 0U;
 }
 switch (region){
  case MAINUI_DBG_MEM_SRAM:
   cpu = cu_avr_get_state();
   return (cpu == NULL) ? 0U : cpu->sram[addr & 0x0FFFU];
  case MAINUI_DBG_MEM_SPIRAM:
   spir = cu_spir_get_state();
   if ((spir == NULL) || (spir->ram == NULL) || (spir->size == 0U) || (addr >= spir->size)){ return 0U; }
   return spir->ram[addr];
  case MAINUI_DBG_MEM_EEPROM:
   cpu = cu_avr_get_state();
   return (cpu == NULL) ? 0U : cpu->eepr[addr & 0x07FFU];
  case MAINUI_DBG_MEM_FLASH:
   cpu = cu_avr_get_state();
   return (cpu == NULL) ? 0U : cpu->crom[addr & 0xFFFFU];
  default:
   return 0U;
 }
}

auint mainui_debug_get_prog_word(auint word_addr)
{
 (void)word_addr;
 return 0U;
}

void mainui_debug_set_reg_byte(auint reg, auint value)
{
 (void)reg;
 (void)value;
}

void mainui_debug_set_sram_byte(auint addr, auint value)
{
 cu_state_cpu_t* cpu = cu_avr_get_state();
 if (cpu == NULL){ return; }
 cpu->sram[addr & 0x0FFFU] = (uint8)(value & 0xFFU);
}

void mainui_debug_set_io_byte(auint addr, auint value)
{
 (void)addr;
 (void)value;
}

void mainui_debug_set_mem_region_byte(auint region, auint addr, auint value)
{
 cu_state_spir_t* spir;
 if ((main_loaded_rom_path[0] == 0) && (main_rom_name[0] == 0)){
  return;
 }
 switch (region){
  case MAINUI_DBG_MEM_SRAM:
   mainui_debug_set_sram_byte(addr, value);
   break;
  case MAINUI_DBG_MEM_SPIRAM:
   spir = cu_spir_get_state();
   if ((spir != NULL) && (spir->ram != NULL) && (addr < spir->size)){
    spir->ram[addr] = (uint8)(value & 0xFFU);
   }
   break;
  case MAINUI_DBG_MEM_EEPROM:
   {
    cu_state_cpu_t* cpu = cu_avr_get_state();
    if (cpu == NULL){ break; }
    cpu->eepr[addr & 0x07FFU] = (uint8)(value & 0xFFU);
    main_peepch = TRUE;
   }
   break;
  default:
   break;
 }
}

boole mainui_debug_get_breakpoint(auint word_addr)
{
 (void)word_addr;
 return FALSE;
}

void mainui_debug_set_breakpoint(auint word_addr, boole enable)
{
 (void)word_addr;
 (void)enable;
}

void mainui_debug_clear_breakpoints(void)
{
}

boole mainui_debug_next_breakpoint(auint start_word_addr, auint* out_word_addr)
{
 (void)start_word_addr;
 if (out_word_addr != NULL){ *out_word_addr = 0U; }
 return FALSE;
}

boole mainui_debug_get_last_break_hit(auint* out_word_addr)
{
 if (out_word_addr != NULL){ *out_word_addr = 0U; }
 return FALSE;
}

void mainui_debug_clear_last_break_hit(void)
{
}

void mainui_debug_watchpoint_set(auint slot, boole enable, auint region, auint flags, auint start_addr, auint end_addr)
{
 (void)slot; (void)enable; (void)region; (void)flags; (void)start_addr; (void)end_addr;
}

boole mainui_debug_watchpoint_get(auint slot, boole* out_enable, auint* out_region, auint* out_flags, auint* out_start_addr, auint* out_end_addr)
{
 (void)slot;
 if (out_enable != NULL){ *out_enable = FALSE; }
 if (out_region != NULL){ *out_region = 0U; }
 if (out_flags != NULL){ *out_flags = 0U; }
 if (out_start_addr != NULL){ *out_start_addr = 0U; }
 if (out_end_addr != NULL){ *out_end_addr = 0U; }
 return FALSE;
}

boole mainui_debug_watchpoint_next(auint start_slot, auint* out_slot)
{
 (void)start_slot;
 if (out_slot != NULL){ *out_slot = 0U; }
 return FALSE;
}

void mainui_debug_clear_watchpoints(void)
{
}

boole mainui_debug_get_last_watch_hit(auint* out_slot, auint* out_region, auint* out_flags, auint* out_addr, auint* out_value, auint* out_pc)
{
 if (out_slot != NULL){ *out_slot = 0U; }
 if (out_region != NULL){ *out_region = 0U; }
 if (out_flags != NULL){ *out_flags = 0U; }
 if (out_addr != NULL){ *out_addr = 0U; }
 if (out_value != NULL){ *out_value = 0U; }
 if (out_pc != NULL){ *out_pc = 0U; }
 return FALSE;
}

char const* mainui_debug_get_data_label(auint addr)
{
 (void)addr;
 return "";
}

boole mainui_debug_find_symbol(char const* name, auint* out_kind, auint* out_addr)
{
 (void)name;
 if (out_kind != NULL){ *out_kind = 0U; }
 if (out_addr != NULL){ *out_addr = 0U; }
 return FALSE;
}

char const* mainui_debug_get_source_status(void)
{
 return "Debugger disabled.";
}

auint mainui_debug_get_source_file_count(void)
{
 return 0U;
}

auint mainui_debug_get_source_row_count(void)
{
 return 0U;
}

char const* mainui_debug_get_source_file(auint index)
{
 (void)index;
 return NULL;
}

boole mainui_debug_source_lookup(auint word_addr, mainui_debug_source_t* out)
{
 (void)word_addr; (void)out; return FALSE;
}

boole mainui_debug_source_resolve_line(auint file_index, auint line, mainui_debug_source_t* out)
{
 (void)file_index; (void)line; (void)out; return FALSE;
}

char const* mainui_debug_get_profile_dir(void)
{
 return "";
}

char const* mainui_debug_get_profile_status(void)
{
 return "Debugger disabled.";
}

void mainui_debug_profile_mark_dirty(void)
{
}

boole mainui_debug_profile_save_now(void)
{
 return FALSE;
}

boole mainui_debug_profile_reload(void)
{
 return FALSE;
}
#endif

auint mainui_get_cheat_count(void)
{
	return cheats_get_count();
}

boole mainui_get_cheat_entry(auint idx, cheat_entry_t* out)
{
	return cheats_get_entry(idx, out);
}

char const* mainui_get_cheat_path(void)
{
	return cheats_get_current_path();
}

boole mainui_get_cheat_dirty(void)
{
	return cheats_is_dirty();
}

boole mainui_get_cheats_enabled(void)
{
	if (main_netplay_cheats_blocked()){
		return FALSE;
	}
	return cheats_get_master_enabled();
}

boole mainui_get_cheats_autoloadsave(void)
{
	return cheats_get_autoloadsave();
}

boole mainui_add_cheat_entry(auint* out_idx)
{
	return cheats_add_entry(out_idx);
}

boole mainui_set_cheat_entry(auint idx, cheat_entry_t const* in)
{
	cheat_entry_t prev;
	cheat_entry_t cur;
	boole         had_prev = FALSE;
	boole         ok;
	char          title[CHEAT_DESC_MAX + 16U];

	if ((in != NULL) && cheats_get_entry(idx, &prev) && prev.used){
		had_prev = TRUE;
	}
	ok = cheats_set_entry(idx, in);
	if ((!ok) || (in == NULL)){ return ok; }
	if (!cheats_get_entry(idx, &cur)){ return ok; }
	if (!cur.used){ return ok; }
	if ((!had_prev) || (!main_cheat_entry_equal(&prev, &cur))){
		main_system_message("CHEAT %u %s: %s",
				(unsigned)(idx + 1U),
				had_prev ? "UPDATED" : "ADDED",
				main_cheat_entry_title(&cur, title, sizeof(title)));
	}
	return ok;
}

void mainui_remove_cheat_entry(auint idx)
{
	cheat_entry_t prev;
	char          title[CHEAT_DESC_MAX + 16U];
	boole         had_prev = cheats_get_entry(idx, &prev) && prev.used;
	cheats_remove_entry(idx);
	if (had_prev){
		main_system_message("CHEAT %u REMOVED: %s",
				(unsigned)(idx + 1U),
				main_cheat_entry_title(&prev, title, sizeof(title)));
	}
}

void mainui_clear_cheats(void)
{
	cheats_clear_all();
	main_system_message("ALL CHEATS CLEARED");
}

void mainui_cheats_save_now(void)
{
	if (cheats_save_file(NULL)){ main_system_message("CHEATS SAVED"); }
	else{ main_system_error("CHEATS SAVE FAILED"); }
}

void mainui_cheats_reload_now(void)
{
	if (cheats_load_file(cheats_get_current_path())){ main_system_message("CHEATS RELOADED"); }
	else{ main_system_error("CHEATS RELOAD FAILED"); }
}

void mainui_set_cheats_enabled(boole enable)
{
	boole prev = cheats_get_master_enabled();
	cheats_set_master_enabled(enable);
	main_cfg.cheats_enabled = enable ? TRUE : FALSE;
	main_cfg_mark_dirty();
	if (prev != (enable ? TRUE : FALSE)){
		main_system_message(enable ? "CHEATS ENABLED" : "CHEATS DISABLED");
	}
}

void mainui_set_cheats_autoloadsave(boole enable)
{
	boole prev = cheats_get_autoloadsave();
	cheats_set_autoloadsave(enable);
	main_cfg.cheats_autoloadsave = enable ? TRUE : FALSE;
	main_cfg_mark_dirty();
	if (prev != (enable ? TRUE : FALSE)){
		main_system_message(enable ? "CHEAT AUTOLOAD/SAVE ENABLED" : "CHEAT AUTOLOAD/SAVE DISABLED");
	}
}

static void main_build_cheatfile_path(char const* game)
{
	char raw[128];
	char name[128];
	char const* base;
	char* dot;
	auint i;
	if ((game == NULL) || (game[0] == 0)){
		game = main_rom_name[0] ? main_rom_name : "default";
	}
	base = strrchr(game, '/');
	if (base != NULL){ base++; }
	else{
		base = strrchr(game, '\\');
		if (base != NULL){ base++; }
		else{ base = game; }
	}
	strncpy(raw, base, sizeof(raw) - 1U);
	raw[sizeof(raw) - 1U] = 0;
	dot = strrchr(raw, '.');
	if (dot != NULL){ *dot = 0; }
	if (raw[0] == 0){ strncpy(raw, "default", sizeof(raw) - 1U); raw[sizeof(raw) - 1U] = 0; }
	for (i = 0U; raw[i] != 0; i++){
		unsigned char c = (unsigned char)raw[i];
		if (isalnum(c) || (c == '_') || (c == '-')){ name[i] = (char)c; }
		else{ name[i] = '_'; }
	}
	name[i] = 0;
	(void)main_mkdirs("cheats");
	snprintf(main_cheatfile, sizeof(main_cheatfile), "cheats/cheats_%s.cfg", name[0] ? name : "default");
	cheats_set_current_path(main_cheatfile);
}

static boole main_try_load_cheats(void)
{
	char legacy[CHEAT_PATH_MAX];
	char const* leaf;
	if (cheats_load_file(main_cheatfile)){
		cheats_set_current_path(main_cheatfile);
		return TRUE;
	}
	leaf = strrchr(main_cheatfile, '/');
	if (leaf == NULL){ leaf = main_cheatfile; }
	else{ leaf++; }
	main_copy_str(legacy, sizeof(legacy), leaf);
	if (cheats_load_file(legacy)){
		cheats_set_current_path(main_cheatfile);
		return TRUE;
	}
	cheats_set_current_path(main_cheatfile);
	return FALSE;
}

static auint main_resolve_spiram_banks(boole is_uze, auint hint_banks)
{
	switch (main_cfg.spiram_pages_mode){
		case 1U: return 0U;
		case 2U: return 1U;
		case 3U: return 2U;
		case 4U: return 4U;
		case 5U: return 8U;
		case 6U: return 16U;
		case 7U: return 32U;
		case 8U: return 64U;
		case 9U: return 128U;
		default: break;
	}
	if (is_uze){
		if (hint_banks == 0U){ return 2U; }
		return hint_banks;
	}
	return 2U;
}

static void main_apply_spiram_policy(boole is_uze, auint hint_banks)
{
	cu_spir_set_size(main_resolve_spiram_banks(is_uze, hint_banks));
}

static void main_apply_esp_softap_policy(boole is_uze, cu_ufile_header_t const* ufhead)
{
	boole enable = FALSE;
	switch (mainui_get_esp_softap_mode()){
		case MAINUI_ESP_SOFTAP_MODE_ENABLE:
			enable = TRUE;
			break;
		case MAINUI_ESP_SOFTAP_MODE_DISABLE:
			enable = FALSE;
			break;
		default:
			enable = (boole)((is_uze && (ufhead != NULL) && ((ufhead->pdefault & PERIPHERAL_ESP8266_AP) != 0U)) ? TRUE : FALSE);
			break;
	}
	cu_esp_set_softap_enabled(enable);
}

static void main_apply_loaded_uzerom(cu_ufile_header_t const* ufhead)
{
	textgui_struct_t* tgui;
	cu_state_cpu_t*   ecpu;
	auint             np_player;
	auint             np_max;
	uint32            np_rom;
	uint32            np_build;
	uint32            np_features;

	if (ufhead == NULL){
		return;
	}

	main_rom_head = *ufhead;
	main_rom_is_uze = TRUE;
	strncpy(main_rom_name, (char const*)(&(ufhead->name[0])), sizeof(main_rom_name) - 1U);
	main_rom_name[sizeof(main_rom_name) - 1U] = 0;
	if (main_rom_head.spiram_banks == 0U){
		main_rom_head.spiram_banks = 2U;
	}
	main_apply_spiram_policy(TRUE, main_rom_head.spiram_banks);
	cu_spir_reset(0U);
	guicore_seticon_uze(&(main_rom_head.icon[0]));
	guicore_set_jamma(main_rom_head.jamma);

	ecpu = cu_avr_get_state();
	cu_avr_autofuse(main_resident_bootloader_boot_priority(&(ecpu->crom[0])));
	cu_avr_reset();
	main_netplay_runtime_reset();
	main_t5_cc = cu_avr_getcycle();
	audio_reset();
	textgui_reset();
	tgui = textgui_getelementptr();
	strncpy((char*)(&(tgui->game[0])), (char const*)(&(main_rom_head.name[0])), TEXTGUI_STR_MAX);
	strncpy((char*)(&(tgui->auth[0])), (char const*)(&(main_rom_head.author[0])), TEXTGUI_STR_MAX);
	main_peepch = FALSE;
	main_promch = FALSE;
	ecpu = cu_avr_get_state();
	ecpu->wd_seed = rand();

	netplay_get_local_identity(&np_rom, &np_build, &np_features, &np_player, &np_max);
	if (np_features == 0U){ np_features = (uint32)(NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP); }
	netplay_set_local_identity(main_rom_head.crc32, (uint32)VER_DATE, np_features, np_player, np_max);
	main_apply_esp_softap_policy(TRUE, ufhead);
}

boole main_get_loaded_rom_info(uint32 *crc32, uint32 *size, char *name, auint name_cap, boole *sendable)
{
	if (crc32 != NULL){
		*crc32 = main_rom_is_uze ? main_rom_head.crc32 : 0U;
	}
	if (size != NULL){
		*size = main_rom_is_uze ? (uint32)(512U + main_rom_head.pmemsize) : 0U;
	}
	if (sendable != NULL){
		*sendable = main_rom_is_uze;
	}
	if ((name != NULL) && (name_cap != 0U)){
		if (main_rom_is_uze){
			strncpy(name, main_rom_name, name_cap - 1U);
			name[name_cap - 1U] = 0;
		}else{
			name[0] = 0;
		}
	}
	return main_rom_is_uze;
}

boole main_get_loaded_rom_image(uint8 **data_out, uint32 *size_out, uint32 *crc32_out, char *name, auint name_cap)
{
	cu_state_cpu_t* ecpu;
	uint8*          buf;
	auint           size;

	if ((data_out == NULL) || (!main_rom_is_uze)){
		return FALSE;
	}
	ecpu = cu_avr_get_state();
	size = 512U + main_rom_head.pmemsize;
	buf = (uint8*)malloc(size);
	if (buf == NULL){
		return FALSE;
	}
	if (cu_ufile_build_image(buf, size, &(ecpu->crom[0]), &main_rom_head) == 0U){
		free(buf);
		return FALSE;
	}
	*data_out = buf;
	if (size_out != NULL){
		*size_out = (uint32)size;
	}
	if (crc32_out != NULL){
		*crc32_out = main_rom_head.crc32;
	}
	if ((name != NULL) && (name_cap != 0U)){
		strncpy(name, main_rom_name, name_cap - 1U);
		name[name_cap - 1U] = 0;
	}
	return TRUE;
}

boole main_load_rom_from_memory(uint8 const *data, uint32 size, char const *name)
{
	cu_state_cpu_t*   ecpu;
#ifdef ENABLE_DEBUGGER
	if (main_dbg_profile_dirty && main_dbg_profile_loaded){ (void)mainui_debug_profile_save_current(); }
#endif
	cu_ufile_header_t ufhead;

	if (data == NULL){
		return FALSE;
	}
	ecpu = cu_avr_get_state();
	memset(&(ecpu->crom[0]), 0xFF, sizeof(ecpu->crom));
	if (main_cfg.resident_bootloader_enable){
		main_seed_builtin_bootloader(&(ecpu->crom[0]));
		(void)main_try_external_bootloader_override(&(ecpu->crom[0]));
	}
	memset(&ufhead, 0, sizeof(ufhead));
	if (!cu_ufile_load_buf(data, (auint)size, &(ecpu->crom[0]), &ufhead)){
		return FALSE;
	}
	main_apply_loaded_uzerom(&ufhead);
	main_loaded_rom_path[0] = 0;
	main_frame_counter = 0U;
	api_server_notify_rom_loaded();
	(void)mainui_input_profile_load_current();
#ifdef ENABLE_DEBUGGER
	main_dbg_profile_loaded = FALSE;
	main_dbg_profile_dir[0] = 0;
	mainui_debug_profile_status_set("Debugger profile pending.");
#endif
	print_message("Netplay ROM sync loaded %s from memory.\n", (name != NULL) ? name : main_rom_name);
	main_system_message("ROM SYNC LOADED: %s", (name != NULL) ? name : main_rom_name);
	return TRUE;
}

static char const* main_path_basename(char const* path)
{
	char const* base;
	char const* p;
	if ((path == NULL) || (path[0] == 0)){ return ""; }
	base = path;
	for (p = path; *p != 0; ++p){
		if ((*p == '/') || (*p == '\\')){ base = p + 1; }
	}
	return base;
}

static void main_system_message_v(char const* fmt, va_list ap)
{
	char buf[128];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	textgui_log_add(&(buf[0]));
}

static void main_system_error(char const* fmt, ...)
{
 char buf[256];
 va_list ap;
 va_start(ap, fmt);
 vsnprintf(buf, sizeof(buf), fmt, ap);
 va_end(ap);
 cu_error("%s\n", buf);
}

static void main_system_message(char const* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	main_system_message_v(fmt, ap);
	va_end(ap);
}

void mainui_system_message(char const* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	main_system_message_v(fmt, ap);
	va_end(ap);
}

boole mainui_open_web_tool(char const* section)
{
	char url[256];
	char cmd[384];
	int rc;
	if ((section == NULL) || (section[0] == 0)){ section = ""; }
	snprintf(url, sizeof(url), "http://127.0.0.1:%u/%s", (unsigned)web_server_get_port(), section);
#if SDL_VERSION_ATLEAST(2,0,14)
	if (SDL_OpenURL(url) == 0){
		main_system_message("WEB: %s", url);
		return TRUE;
	}
#endif
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", url);
#elif defined(__APPLE__)
	snprintf(cmd, sizeof(cmd), "open \"%s\" >/dev/null 2>&1 &", url);
#else
	snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", url);
#endif
	rc = system(cmd);
	if (rc == 0){
		main_system_message("WEB: %s", url);
		return TRUE;
	}
	main_system_error("WEB OPEN FAILED: %s", url);
	return FALSE;
}

static boole main_cheat_entry_equal(cheat_entry_t const* a, cheat_entry_t const* b)
{
	if ((a == NULL) || (b == NULL)){ return FALSE; }
	if (a->used != b->used){ return FALSE; }
	if (a->enabled != b->enabled){ return FALSE; }
	if (a->compare_used != b->compare_used){ return FALSE; }
	if (a->signed_value != b->signed_value){ return FALSE; }
	if (a->width_bytes != b->width_bytes){ return FALSE; }
	if (a->addr != b->addr){ return FALSE; }
	if (a->value != b->value){ return FALSE; }
	if (a->compare != b->compare){ return FALSE; }
	return (strcmp(a->desc, b->desc) == 0);
}

static char const* main_cheat_entry_title(cheat_entry_t const* ent, char* buf, auint size)
{
	if (buf == NULL){ return ""; }
	buf[0] = 0;
	if (ent == NULL){ return buf; }
	if (ent->desc[0] != 0){
		main_copy_str(buf, size, ent->desc);
	}else{
		snprintf(buf, size, "SRAM $%03X", (unsigned)(ent->addr & 0x0FFFU));
	}
	return buf;
}

static char const* main_watchpoint_region_name(auint region)
{
	return (region == MAINUI_DBG_WATCH_REGION_IO) ? "I/O" : "SRAM";
}

static char const* main_watchpoint_mode_name(auint flags)
{
	if ((flags & MAINUI_DBG_WATCH_READ) && (flags & MAINUI_DBG_WATCH_WRITE)){ return "RW"; }
	if ((flags & MAINUI_DBG_WATCH_READ) != 0U){ return "R"; }
	if ((flags & MAINUI_DBG_WATCH_WRITE) != 0U){ return "W"; }
	return "-";
}

static auint main_watchpoint_digits(auint region)
{
	return (region == MAINUI_DBG_WATCH_REGION_IO) ? 2U : 3U;
}

static void main_copy_str(char* dst, auint cap, char const* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

static void main_join_path(char* dst, auint cap, char const* a, char const* b)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if ((a == NULL) || (a[0] == 0)){
		main_copy_str(dst, cap, (b == NULL) ? "" : b);
		return;
	}
	if ((b == NULL) || (b[0] == 0)){
		main_copy_str(dst, cap, a);
		return;
	}
	if ((a[strlen(a) - 1U] == '/') || (a[strlen(a) - 1U] == '\\')){
		snprintf(dst, cap, "%s%s", a, b);
	}else{
		snprintf(dst, cap, "%s/%s", a, b);
	}
	dst[cap - 1U] = 0;
}

static boole main_mkdirs(char const* path)
{
	char tmp[1024];
	auint i;
	auint len;
	int rc;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	main_copy_str(tmp, sizeof(tmp), path);
	len = (auint)strlen(tmp);
	if ((len > 0U) && ((tmp[len - 1U] == '/') || (tmp[len - 1U] == '\\'))){
		tmp[len - 1U] = 0;
	}
	for (i = 1U; tmp[i] != 0; i++){
		if ((tmp[i] == '/') || (tmp[i] == '\\')){
			char hold = tmp[i];
			tmp[i] = 0;
			rc = MAIN_MKDIR(tmp);
			if ((rc != 0) && (errno != EEXIST)){
				tmp[i] = hold;
				return FALSE;
			}
			tmp[i] = hold;
		}
	}
	rc = MAIN_MKDIR(tmp);
	if ((rc != 0) && (errno != EEXIST)){ return FALSE; }
	return TRUE;
}

static void main_cfg_set_rom_dir_from_file(char const* path)
{
	auint i = 0U;
	if (path == NULL){ return; }
	while ((i < (APPCFG_PATH_MAX - 1U)) && (path[i] != 0)){
		main_cfg.rom_path[i] = path[i];
		i++;
	}
	main_cfg.rom_path[i] = 0;
	while (i != 0U){
		i--;
		if ((main_cfg.rom_path[i] == '/') || (main_cfg.rom_path[i] == '\\')){
			main_cfg.rom_path[i] = 0;
			break;
		}
	}
	if (main_cfg.rom_path[0] == 0){
		strcpy(main_cfg.rom_path, ".");
	}
}

static boole main_boot_image_has_game_or_boot(uint8 const* crom, boole* hasgame, boole* hasboot)
{
	boole hg = FALSE;
	boole hb = FALSE;
	if (crom == NULL){
		if (hasgame != NULL){ *hasgame = FALSE; }
		if (hasboot != NULL){ *hasboot = FALSE; }
		return FALSE;
	}
	hg = !(((crom[0U] == 0x00U) && (crom[1U] == 0x00U)) ||
	       ((crom[0U] == 0xFFU) && (crom[1U] == 0xFFU)));
	hb = !(((crom[(0x7800U * 2U) + 0U] == 0x00U) && (crom[(0x7800U * 2U) + 1U] == 0x00U)) ||
	       ((crom[(0x7800U * 2U) + 0U] == 0xFFU) && (crom[(0x7800U * 2U) + 1U] == 0xFFU)));
	if (hasgame != NULL){ *hasgame = hg; }
	if (hasboot != NULL){ *hasboot = hb; }
	return (hg || hb);
}

static boole main_boot_section_has_data(uint8 const* crom)
{
	auint i;
	if (crom == NULL){ return FALSE; }
	for (i = 0U; i < CU_BUILTIN_BOOTLOADER_SIZE; i++){
		if (crom[CU_BUILTIN_BOOTLOADER_ORIGIN + i] != 0xFFU){ return TRUE; }
	}
	return FALSE;
}

static void main_seed_builtin_bootloader(uint8* crom)
{
	if (crom == NULL){ return; }
	memcpy(&(crom[CU_BUILTIN_BOOTLOADER_ORIGIN]), &(cu_builtin_bootloader[0]), CU_BUILTIN_BOOTLOADER_SIZE);
}

/*
** Attempts to load the configured resident-bootloader file as an override.
** Missing files are intentionally silent: the compiled-in Bootloader 5 image
** is the normal source. A present but invalid image is reported and ignored.
** Only the AVR boot section is copied, so an override can never seed or
** replace application flash as a side effect.
*/
static boole main_try_external_bootloader_override(uint8* crom)
{
	char              tstr[128];
	uint8             probe[65536];
	cu_ufile_header_t ufhead;
	boole             ok;

	if ((crom == NULL) || (main_cfg.resident_bootloader_file[0] == 0)){ return FALSE; }
	filesys_setpath(main_cfg.resident_bootloader_file, &(tstr[0]), 100U);

	/* Probe existence without making a missing optional override noisy. */
	if (!filesys_open(FILESYS_CH_EMU, &(tstr[0]))){ return FALSE; }
	filesys_flush(FILESYS_CH_EMU);

	memset(&(probe[0]), 0xFF, sizeof(probe));
	memset(&ufhead, 0, sizeof(ufhead));
	ok = cu_hfile_load(&(tstr[0]), &(probe[0]));
	if (!ok){
		memset(&(probe[0]), 0xFF, sizeof(probe));
		ok = cu_ufile_load(&(tstr[0]), &(probe[0]), &ufhead);
	}
	if ((!ok) || (!main_boot_section_has_data(&(probe[0])))){
		print_message("Resident bootloader override invalid; using built-in image: %s\n", main_cfg.resident_bootloader_file);
		return FALSE;
	}

	memcpy(&(crom[CU_BUILTIN_BOOTLOADER_ORIGIN]),
	       &(probe[CU_BUILTIN_BOOTLOADER_ORIGIN]),
	       CU_BUILTIN_BOOTLOADER_SIZE);
	print_message("Resident bootloader override loaded: %s\n", main_cfg.resident_bootloader_file);
	return TRUE;
}

static boole main_load_resident_bootloader_image(uint8* crom)
{
	if ((crom == NULL) || (!main_cfg.resident_bootloader_enable)){ return FALSE; }
	main_seed_builtin_bootloader(crom);
	(void)main_try_external_bootloader_override(crom);
	return TRUE;
}

static boole main_resident_bootloader_boot_priority(uint8 const* crom)
{
	boole hasgame;
	boole hasboot;
	if (!main_cfg.resident_bootloader_enable){ return FALSE; }
	(void)main_boot_image_has_game_or_boot(crom, &hasgame, &hasboot);
	(void)hasgame;
	return hasboot;
}

static boole main_file_prefers_bootloader(char const* path)
{
	char              tstr[128];
	uint8             probe[65536];
	boole             hasgame;
	boole             hasboot;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	memset(probe, 0, sizeof(probe));
	filesys_setpath(path, &(tstr[0]), 100U);
	if (!cu_hfile_load(&(tstr[0]), &(probe[0]))){ return FALSE; }
	(void)main_boot_image_has_game_or_boot(&(probe[0]), &hasgame, &hasboot);
	return ((hasboot) && (!hasgame)) ? TRUE : FALSE;
}

static boole main_seed_code_rom_image(uint8* crom, boole allow_romdump_fallback)
{
	if (crom == NULL){ return FALSE; }
	memset(crom, 0, 65536U);
	if (main_load_resident_bootloader_image(crom)){
		return TRUE;
	}
	if (allow_romdump_fallback){
		return romdump_load(crom);
	}
	return FALSE;
}


static boole main_load_rom_file_internal(char const* path)
{
	cu_state_cpu_t*   ecpu;
	cu_ufile_header_t ufhead;
	uint8             new_crom[65536];
	uint8             new_eepr[2048];
	char              tstr[128];
	char              game_name[64];
	boole             uzefile;
	boole             hasrom;
	boole             bootpri;
	uint32            np_rom;
	uint32            np_build;
	uint32            np_features;
	auint             np_player;
	auint             np_max;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
if (main_input_profile_dirty){ (void)mainui_input_profile_save_current(); }
#ifdef ENABLE_DEBUGGER
	if (main_dbg_profile_dirty && main_dbg_profile_loaded){ (void)mainui_debug_profile_save_current(); }
#endif
	ecpu = cu_avr_get_state();
	if ((main_promch) || (cu_avr_crom_ischanged(TRUE))){ romdump_save(&(ecpu->crom[0])); }
	if ((main_peepch) || (cu_avr_eeprom_ischanged(TRUE))){ eepdump_save(&(ecpu->eepr[0])); }
	if (cheats_is_dirty() && cheats_get_autoloadsave()){ cheats_save_file(NULL); }
	main_promch = FALSE;
	main_peepch = FALSE;
	memset(&ufhead, 0, sizeof(ufhead));
	hasrom = main_seed_code_rom_image(&(new_crom[0]), TRUE);
	(void)hasrom;
	eepdump_load(&(new_eepr[0]));
	filesys_setpath(path, &(tstr[0]), 100U);
	uzefile = cu_ufile_load(&(tstr[0]), &(new_crom[0]), &ufhead);
	bootpri = FALSE;
	if (!uzefile){
		bootpri = main_file_prefers_bootloader(path);
		if (!cu_hfile_load(&(tstr[0]), &(new_crom[0]))){
			print_error("Failed to load ROM: %s\n", path);
			main_system_error("ROM LOAD FAILED: %s", main_path_basename(path));
			if (main_loaded_rom_path[0] != 0){ filesys_setpath(main_loaded_rom_path, NULL, 0U); }
			return FALSE;
		}
	}
	memcpy(&(ecpu->crom[0]), &(new_crom[0]), sizeof(new_crom));
	memcpy(&(ecpu->eepr[0]), &(new_eepr[0]), sizeof(new_eepr));
	cu_avr_autofuse((boole)(bootpri || main_resident_bootloader_boot_priority(&(ecpu->crom[0]))));
	main_apply_spiram_policy(uzefile ? TRUE : FALSE, uzefile ? ufhead.spiram_banks : 0U);
	cu_spir_reset(0U);
	cu_avr_reset();
	main_netplay_runtime_reset();
	cu_avr_breakpoints_clear();
	main_t5_cc = cu_avr_getcycle();
	audio_reset();
	textgui_reset();
	ecpu->wd_seed = rand();
	main_ispause = FALSE;
	main_isadvfr = FALSE;
#ifdef ENABLE_DEBUGGER
	main_dbg_break_hit = FALSE;
	main_dbg_break_addr = 0U;
#endif
	if (uzefile){
		main_apply_loaded_uzerom(&ufhead);
	}else{
		memset(&main_rom_head, 0, sizeof(main_rom_head));
		main_rom_is_uze = FALSE;
		guicore_seticon_uze(NULL);
		guicore_set_jamma(0U);
		strncpy(main_rom_name, main_path_basename(path), sizeof(main_rom_name) - 1U);
		main_rom_name[sizeof(main_rom_name) - 1U] = 0;
		strncpy((char*)(&(textgui_getelementptr()->game[0])), main_rom_name, TEXTGUI_STR_MAX);
		((char*)(&(textgui_getelementptr()->auth[0])))[0] = 0;
		main_apply_esp_softap_policy(FALSE, NULL);
		netplay_get_local_identity(&np_rom, &np_build, &np_features, &np_player, &np_max);
		if (np_features == 0U){ np_features = (uint32)(NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP); }
		netplay_set_local_identity(0U, (uint32)VER_DATE, np_features, np_player, np_max);
	}
	main_build_cheatfile_path(path);
	cheats_reset();
	cheats_set_master_enabled(main_cfg.cheats_enabled);
	cheats_set_autoloadsave(main_cfg.cheats_autoloadsave);
	if (cheats_get_autoloadsave()){
		main_try_load_cheats();
	}else{
		cheats_set_current_path(main_cheatfile);
	}
	main_cfg_set_rom_dir_from_file(path);
	strncpy(main_loaded_rom_path, path, sizeof(main_loaded_rom_path) - 1U);
	main_loaded_rom_path[sizeof(main_loaded_rom_path) - 1U] = 0;
	main_frame_counter = 0U;
	api_server_notify_rom_loaded();
	main_recent_rom_add(path);
	(void)mainui_input_profile_load_current();
#ifdef ENABLE_DEBUGGER
	main_dbg_profile_loaded = FALSE;
	main_dbg_profile_dir[0] = 0;
	mainui_debug_profile_status_set("Debugger profile pending.");
#endif
	main_cfg_dirty = FALSE;
	strncpy(game_name, main_path_basename(path), sizeof(game_name) - 1U);
	game_name[sizeof(game_name) - 1U] = 0;
	print_message("Loaded ROM: %s\n", game_name);
	if (!main_suppress_rom_load_system_message){
		main_system_message("ROM LOADED: %s", game_name);
	}
	return TRUE;
}

boole mainui_load_rom_file(char const* path)
{
	return main_load_rom_file_internal(path);
}


/* Flash events are latched by the AVR core, so a complete page operation
** between frame boundaries is visible here. Never infer activity from ROM
** modifications (loading a ROM or debugger patches also modify it). */
static void main_fast_flash_tick(auint tick)
{
 boole active = cu_avr_flash_active(TRUE);
 auint route = cu_esp_get_serial_route();
 active = active && main_cfg.fast_flash && (!main_ispause) && (!main_isadvfr) &&
          (!netplay_is_connected()) && (!main_np_session_active) &&
          ((route == CU_ESP_SERIAL_DISCONNECTED) ||
           (route == CU_ESP_SERIAL_ESP_MODULE) || (route == CU_ESP_SERIAL_LOOPBACK));
#ifdef ENABLE_VCAP
 if (main_isvcap){ active = FALSE; }
#endif
 if (active != main_fast_flash_active){
  audio_flush(); /* Do not queue accelerated audio or replay stale samples. */
  main_tdrift = 0U;
  main_fast_flash_present_tick = tick;
 }
 main_fast_flash_active = active;
}

/*
** Main loop body
*/
static void main_loop(void)
{
 auint ctick = SDL_GetTicks();
 auint tdif  = ctick - main_ptick;
 auint drift = main_tdrift;
 auint favg;
 auint ccur;
 auint cdif;
 auint rows;
 auint fdtmp = main_fdrop;
 auint afreq;
 boole fdrop = FALSE;
 boole eepch;
 boole romch;
 SDL_Event sdlevent;
 cu_state_cpu_t* ecpu;
 textgui_struct_t* tgui;

#ifdef ENABLE_MICROUI
 if (netplay_is_connected() || mui_netplay_ui_active() || ((main_frc & 3U) == 0U)){
  netplay_poll();
 }
#else
 if (netplay_is_connected() || ((main_frc & 3U) == 0U)){
  netplay_poll();
 }
#endif
 main_netplay_sync_runtime_state();
 main_fast_flash_tick(ctick);
 drift = main_tdrift;

 /* First "sandbox" the drift to silently skip main loops if the emulator is
 ** running too fast */

 /* Calculate drift from perfect 60Hz */

 if (main_tfrac >= 2U){ drift += 16U; }
 else{                  drift += 17U; }
 drift -= tdif;

 /* Check for possible total CPU starvation. When such a situation arises (a
 ** huge difference in prev. and current tick), throw drift away. */

 if (tdif > 100U){
  drift = 0U;
  tdif  = 16U;
 }

 /* Handle divergences */

 if (drift >= 0x80000000U){   /* Possibly too slow */
  if ((drift + 8U) >= 0x80000000U){   /* Transient problem (late by at least 1 frame) */
   if ((drift + 58U) >= 0x80000000U){ /* Permanent problem (late by several frames) */
    if ((drift + 225U) >= 0x80000000U){
     drift = (auint)(0U) - 225U;      /* Limit (throw away memory) */
    }
    if (fdtmp < 160U){ fdtmp = 160U; }   /* Semi-permanently limit frame rate */
   }
   if (fdtmp < 240U){ fdtmp ++; }     /* Push frame drop request */
  }
 }else{                       /* Possibly too fast */
  if (fdtmp != 0U){ fdtmp --; }  /* Eat away frame drop request */
  if (drift > 75U){           /* Limit drift (to not let it accumulating for a nolimit run) */
   drift = 75U;
  }
  if ( ((drift > 25U)) &&     /* Waste away time by dropping main loops */
       (!main_nolimit) && (!main_fast_flash_active) ){
#ifndef __EMSCRIPTEN__
   SDL_Delay(5);              /* Emscripten doesn't need this since the loop is a callback */
#endif
   return;
  }
 }
 if ( (fdtmp != 0U) &&
      ((main_frc & 1U) != 0U) ){ fdrop = TRUE; } /* Skip every second frame if necessary */

 if (main_fast_flash_active){
  /* Keep the GUI responsive at host display speed without allowing vsync
  ** to cap every emulated frame. AVR cycles and SPM busy timing stay intact. */
  fdrop = ((ctick - main_fast_flash_present_tick) < 16U);
  if (!fdrop){ main_fast_flash_present_tick = ctick; }
  drift = 0U;
  fdtmp = 0U;
 }

 /* Now the drift can be saved */

 main_tdrift = drift;
 main_ptick  = ctick;
 main_fdrop  = fdtmp;
 main_frc   ++;
 if (main_tfrac >= 2U){ main_tfrac = 0U; }
 else{                  main_tfrac ++;   }

 /* Tolerate some divergence from perfect 60Hz (1.5%) */

 if ((main_frc & 0x3U) == 0U){
  if       (main_tdrift > 0x80000000U){
   main_tdrift ++;
  }else if (main_tdrift != 0U){
   main_tdrift --;
  }else{}
 }

 /* Check for EEPROM & Code ROM changes and save as needed */

 ecpu = cu_avr_get_state(); /* Also needed by emulator whisper ports */

 eepch = cu_avr_eeprom_ischanged(TRUE);
 if ( (main_peepch) &&
      (!eepch) ){ /* End of EEPROM write burst */
  eepdump_save(&(ecpu->eepr[0]));
 }
 main_peepch = eepch;

 romch = cu_avr_crom_ischanged(TRUE);
 if ( (main_promch) &&
      (!romch) ){ /* End of Code ROM write burst */
  romdump_save(&(ecpu->crom[0]));
 }
 main_promch = romch;

 /* Generate various emulator info for the GUI */

 main_t500 += tdif;
 if (!fdrop){ main_t5_frc ++; }

 tgui = textgui_getelementptr();

 tgui->merge   = main_fmerge;
 tgui->kbuzem  = ginput_iskbuzem();
 tgui->player2 = ginput_is2palloc();
#ifdef ENABLE_VCAP
 tgui->capture = main_isvcap;
#else
 tgui->capture = FALSE;
#endif

 tgui->ports[0] = ecpu->iors[0x39U];
 tgui->ports[1] = ecpu->iors[0x3AU];
 /* The physical Uzebox status LED is active-high on ATmega644 PD4.
 ** Requiring DDRD bit 4 as well makes the HUD match whether the pin is
 ** actually being driven, rather than merely showing the PORT latch. */
 tgui->led = (boole)(((ecpu->iors[CU_IO_DDRD] & ecpu->iors[CU_IO_PORTD] & 0x10U) != 0U) ? TRUE : FALSE);
 /* The LED is a host-side HUD indicator, so composite it after the normal
 ** 1bpp-style text/game image has been rendered into the final texture. */
 guicore_set_status_led(tgui->led);
 tgui->wdrint = cu_avr_get_lastwdrinterval(&(tgui->wdrbeg), &(tgui->wdrend));

 if (main_t500 >= 500U){
  main_t500 -= 500U;

  favg = main_t5_frc + main_t5_frp;
  main_t5_frp = main_t5_frc;
  main_t5_frc = 0U;

  ccur       = cu_avr_getcycle();
  cdif       = WRAP32(ccur - main_t5_cc);
  main_t5_cc = ccur;

  afreq = audio_getfreq();

  tgui->cpufreq  = cdif * 2U;
  tgui->aufreq   = afreq;
  tgui->dispfreq = favg * 1000U;
 }

 api_server_tick();
 web_server_set_api_port(api_server_get_port());
#ifdef ENABLE_ESP
 cu_esp_link_set_rom_id(mainui_get_current_rom_crc32());
 /* frame_run() normally services the serial link. While emulation is paused
 ** (debugger, or the GUI being open) keep servicing it here so pairing,
 ** keepalives and the relay heartbeat continue and the peer does not drop. */
 if (mainui_debug_is_paused()){ cu_esp_endpoint_host_tick(); }
#endif

 /* Process events */

 while (SDL_PollEvent(&sdlevent) != 0){
#ifdef ENABLE_MICROUI
  if (mui_handle_event(&sdlevent) == 0){
   ginput_sendevent(&sdlevent);
  }
#else
  ginput_sendevent(&sdlevent);
#endif

  if (sdlevent.type == SDL_QUIT){ main_exit = TRUE; }

  if(cu_kbd_capture_active()){ continue; }
  if(cu_kbd_bypassed == 255){ cu_kbd_bypassed = 1; continue; } /* incase it was just disabled in ginput_sendevent(), eat F1 keypress */

  if ((sdlevent.type) == SDL_KEYDOWN){

   switch (sdlevent.key.keysym.sym){

    case SDLK_ESCAPE:
     main_exit = TRUE;
     break;
    case SDLK_F1:
     cu_kbd_bypassed = 0U; /* allow the keyboard dongle to be detected again */
     print_unf("KEYBOARD PASSTHROUGH DISABLED\n");
     break;
    case SDLK_F2:
     if (!guicore_init(guicore_getflags() ^ GUICORE_SMALL, main_title)){ main_exit = TRUE; }
     main_cfg_mark_dirty();
     break;

    case SDLK_F3:
     mainui_set_display_gameonly(mainui_get_display_gameonly() ? FALSE : TRUE);
     break;

    case SDLK_F4:
     mainui_set_frame_rate_limiter(mainui_get_frame_rate_limiter() ? FALSE : TRUE);
     break;

#ifdef ENABLE_VCAP
    case SDLK_F5:
     mainui_set_video_dump_active(mainui_get_video_dump_active() ? FALSE : TRUE);
     break;
#endif
    case SDLK_F6:
     if ((sdlevent.key.keysym.mod & KMOD_SHIFT) != 0U){
      auint ena = (cu_gun_get_enabled(0U) == 0U) ? 1U : 0U;
      cu_gun_set_enabled(ena, 0U);
      if (ena != 0U){
       print_unf("Lightgun Enabled on P1 (legacy single-mouse fallback)\n");
      }else{
       print_unf("Lightgun Disabled on P1\n");
      }
      break;
     }
     /* cu_mouse_adjust_scale(); MOVE THIS TO cu_mouse.c */
     cu_mouse_scale++;
     if(cu_mouse_scale == 1U){ /* mouse just disabled */
      cu_mouse_enabled = 1U;
      print_unf("SNES Mouse Enabled(overriding P1)\n");
     }else if(cu_mouse_scale == 6U){ /* wrapped around sensitivity, disable(user can re-enable and go back through scale) */
      cu_mouse_enabled = 0U;
      cu_mouse_scale = 0U;
      print_unf("SNES Mouse Disabled(P1 restored)\n");
     }else
      print_message("SNES Mouse Sensitivity: %d\n", cu_mouse_scale);
     main_cfg_mark_dirty();
     break;

    case SDLK_F7:
     if ((sdlevent.key.keysym.mod & KMOD_SHIFT) != 0U){
      auint ena = (cu_multitap_get_tap_present(0U) == 0U) ? 1U : 0U;
      cu_multitap_set_tap_present(0U, ena);
      if (ena != 0U){
       print_unf("UzeTap enabled on P1\n");
      }else{
       print_unf("UzeTap disabled on P1\n");
      }
      break;
     }
     mainui_set_frame_merge(mainui_get_frame_merge() ? FALSE : TRUE);
     break;

    case SDLK_F8:
     if ((sdlevent.key.keysym.mod & KMOD_SHIFT) != 0U){
      auint ena = (cu_multitap_get_tap_present(1U) == 0U) ? 1U : 0U;
      cu_multitap_set_tap_present(1U, ena);
      if (ena != 0U){
       print_unf("UzeTap enabled on P2\n");
      }else{
       print_unf("UzeTap disabled on P2\n");
      }
      break;
     }
     mainui_set_input_kbuzem(mainui_get_input_kbuzem() ? FALSE : TRUE);
     break;

    case SDLK_F9:
     if ((sdlevent.key.keysym.mod & KMOD_SHIFT) != 0U){
      auint slot = (cu_multitap_get_active_slot(0U) + 1U) & 3U;
      cu_multitap_set_active_slot(0U, slot);
      print_message("UzeTap P1 slot %u selected\n", slot);
      break;
     }
     mainui_debug_set_paused(mainui_debug_is_paused() ? FALSE : TRUE);
     break;

    case SDLK_F10:
     if ((sdlevent.key.keysym.mod & KMOD_SHIFT) != 0U){
      auint slot = (cu_multitap_get_active_slot(1U) + 1U) & 3U;
      cu_multitap_set_active_slot(1U, slot);
      print_message("UzeTap P2 slot %u selected\n", slot);
      break;
     }
     main_isadvfr = TRUE;
     break;

    case SDLK_F11:
     mainui_set_display_fullscreen(mainui_get_display_fullscreen() ? FALSE : TRUE);
     break;

    case SDLK_F12:
     ginput_set2palloc(!ginput_is2palloc());
     main_cfg_mark_dirty();
     break;

    default:
     break;
   }

  }
 }

 main_gui_pause_tick();

 if (main_np_session_active){
  main_netplay_capture_physical_inputs();
 }

 /* Go on with the frame's logic. Does it here so the SDL_GetTicks() on the
 ** top of the loop is as close to the render's end as reasonably possible. */

 if (main_isadvfr){
  main_ispause = FALSE;
 }

 if (!main_ispause){

#ifdef ENABLE_ICAP
  if (main_input_capture_active){
#ifndef ENABLE_IREP
   capture_inputs();
#else
   if (capture_replay()){
    main_ispause = TRUE; /* Pause emulator after replay is complete */
    main_input_capture_active = FALSE;
    main_system_message("INPUT REPLAY COMPLETE");
   }
#endif
  }
#endif

  if (main_np_session_active){
   if (!main_netplay_prepare_live_frame()){
    main_netplay_restore_physical_inputs();
    audio_sendframe(NULL, 262U);
    guicore_update(FALSE);
	 main_cfg_apply_runtime_pending();
    return;
   }
  }

	  main_input_trace_ctx_valid = TRUE;
	  main_input_trace_ctx_frame = main_np_session_active ? main_np_frame : (auint)main_frame_counter;
	  main_input_trace_ctx_phase = "LIVE";
	  cu_kbd_frame_boundary();
	  main_input_trace_ctx_valid = FALSE;
	  if (!main_np_session_active){
		  main_input_trace_log_frame_summary((auint)main_frame_counter, "LIVE");
	  }
  api_server_frame_begin();
  if (!main_netplay_cheats_blocked()){ cheats_apply(); }
#ifdef ENABLE_VCAP
  rows = frame_run(fdrop && (!main_isvcap), main_fmerge);
#ifdef ENABLE_DEBUGGER
  if (frame_breakpoint_hit(TRUE, &main_dbg_break_addr)){
   main_dbg_break_hit = TRUE;
   main_ispause = TRUE;
   main_isadvfr = FALSE;
   if ((!cu_avr_temp_break_get(NULL)) && (!cu_avr_debug_step_active())){
    mainui_debug_run_status_set("Idle.");
   }
   mainui_debug_report_break(main_dbg_break_addr);
  }
#endif
  if (!main_fast_flash_active){ audio_sendframe(frame_getaudio(), rows); }
  guicore_update(fdrop);
  main_cfg_apply_runtime_pending();
  if (main_isvcap){
   avconv_push(frame_getaudio(), rows);
  }
  if (main_np_session_active){
   main_netplay_restore_physical_inputs();
   main_np_frame++;
  }
  main_frame_counter++;
  api_server_frame_end();
#else
  rows = frame_run(fdrop, main_fmerge);
#ifdef ENABLE_DEBUGGER
  if (frame_breakpoint_hit(TRUE, &main_dbg_break_addr)){
   main_dbg_break_hit = TRUE;
   main_ispause = TRUE;
   main_isadvfr = FALSE;
   if ((!cu_avr_temp_break_get(NULL)) && (!cu_avr_debug_step_active())){
    mainui_debug_run_status_set("Idle.");
   }
   mainui_debug_report_break(main_dbg_break_addr);
  }
#endif
  if (!main_fast_flash_active){ audio_sendframe(frame_getaudio(), rows); }
  guicore_update(fdrop);
  main_cfg_apply_runtime_pending();
  if (main_np_session_active){
   main_netplay_restore_physical_inputs();
   main_np_frame++;
  }
  main_frame_counter++;
  api_server_frame_end();
#endif

 }else{

  audio_sendframe(NULL, 262U);
  guicore_update(FALSE);
	 main_cfg_apply_runtime_pending();

 }

 if (main_isadvfr){
  main_isadvfr = FALSE;
  main_ispause = TRUE;
 }
}



/*
** Main entry point
*/
static void main_gui_pause_tick(void)
{
#ifdef ENABLE_MICROUI
	boole gui_open = mui_is_open();
	if (main_gui_pause_owned && (!mainui_debug_is_paused())){
		main_gui_pause_owned = FALSE;
	}
	if ((!main_cfg.gui_pause_while_open) || (!gui_open)){
		if (main_gui_pause_owned){
			main_gui_pause_owned = FALSE;
			mainui_debug_set_paused(FALSE);
		}
		return;
	}
	if ((!mainui_debug_is_paused()) && (!main_gui_pause_owned)){
		mainui_debug_set_paused(TRUE);
		main_gui_pause_owned = TRUE;
	}
#else
	main_gui_pause_owned = FALSE;
#endif
}

int main (int argc, char** argv)
{
 cu_ufile_header_t ufhead;
 cu_state_cpu_t*   ecpu;
 char              tstr[128];
 char              startup_game[APPCFG_PATH_MAX] = "";
 auint             flg;
 boole             uzefile = FALSE;
 boole             hasrom = FALSE;
 boole             bootpri = FALSE;
 boole             startup_builtin_bootloader = FALSE;
 boole             startup_has_path = FALSE;

 memset(&ufhead, 0, sizeof(ufhead));
 memset(&main_rom_head, 0, sizeof(main_rom_head));
 main_rom_is_uze = FALSE;
 main_rom_name[0] = 0;
 cheats_reset();
	main_loaded_rom_path[0] = 0;

 print_unf(main_title);
 print_message(" %08X\n", VER_DATE);

 appcfg_load_or_create(&main_cfg, main_cfgfile);
 textgui_log_set_enabled(main_cfg.display_system_messages);
 textgui_log_set_level(main_cfg.log_verbosity);
 main_cfg_dirty = FALSE;
 api_server_configure(main_cfg.api_server, main_cfg.api_server_port);
 web_server_configure(main_cfg.api_server, WEB_SERVER_DEFAULT_PORT, main_cfg.api_server_port);
 main_fmerge = (main_cfg.frame_merge && main_cfg_render_allows_frame_merge());
 main_nolimit = (!main_cfg.frame_rate_limiter);
 main_audio_freqscale = (main_cfg.audio_freqscale && main_cfg.frame_rate_limiter);
 audio_output_rate_set(main_cfg.audio_output_rate);
 audio_latency_set(main_cfg.audio_latency);
 audio_output_s16_ena(main_cfg.audio_s16);
 audio_resampler_set(main_cfg.audio_resampler);
 audio_dcblock_ena(main_cfg.audio_dcblock);
 audio_lowpass_set(main_cfg.audio_lowpass);
 audio_lowpass_quality_set(main_cfg.audio_lowpass_quality);
 audio_monitor_mode_set(main_cfg.audio_monitor_mode);
 audio_monitor_width_set(main_cfg.audio_monitor_width);
 audio_reverb_set(main_cfg.audio_reverb);
 audio_master_volume_set(main_cfg.audio_master_volume);
 audio_freqscale_ena(main_audio_freqscale);
 mainui_set_netplay_rollback_window(main_cfg.netplay_rollback_window);
 mainui_set_netplay_input_delay(main_cfg.netplay_input_delay);

 netplay_init();
 main_apply_netplay_config_runtime();
 savestate_init();
 remote_roms_init();
 remote_roms_set_host(main_cfg.remote_roms_host[0] ? main_cfg.remote_roms_host : "uzenet.us");

 if (argc > 1){
  strncpy(startup_game, argv[1], sizeof(startup_game) - 1U);
  startup_game[sizeof(startup_game) - 1U] = 0;
  startup_has_path = TRUE;
 }else{
  strncpy(startup_game, "Bootloader.hex", sizeof(startup_game) - 1U);
  startup_game[sizeof(startup_game) - 1U] = 0;
  startup_builtin_bootloader = TRUE;
 }

 filesys_setpath(&(startup_game[0]), &(tstr[0]), 100U); /* Locate everything beside the game */

 ecpu = cu_avr_get_state();
 if (startup_has_path){
  hasrom = main_seed_code_rom_image(&(ecpu->crom[0]), TRUE);
 }else{
  /* No explicit ROM: boot the project bootloader. The compiled-in image is
  ** authoritative by default; a present configured Bootloader.hex overrides
  ** only its 4 KiB boot section. */
  memset(&(ecpu->crom[0]), 0, sizeof(ecpu->crom));
  main_seed_builtin_bootloader(&(ecpu->crom[0]));
  (void)main_try_external_bootloader_override(&(ecpu->crom[0]));
  hasrom = TRUE;
  bootpri = TRUE;
 }
 eepdump_load(&(ecpu->eepr[0]));

 if (startup_has_path){
  uzefile = cu_ufile_load(&(tstr[0]), &(ecpu->crom[0]), &ufhead);
  if (!uzefile){
   bootpri = main_file_prefers_bootloader(&(startup_game[0]));
   if (!cu_hfile_load(&(tstr[0]), &(ecpu->crom[0]))){
    if (!hasrom){ return 1; }
   }
  }
 }else{
  uzefile = FALSE;
 }
 if ((!startup_builtin_bootloader) && (startup_game[0] != 0)){
  main_build_cheatfile_path(&(startup_game[0]));
 }else{
  main_build_cheatfile_path(NULL);
 }

 ecpu->wd_seed = rand(); /* Seed the WD timeout used for PRNG seed in Uzebox games */

 print_unf("Starting emulator\n");


 flg = 0U;
 if (main_cfg_is_classic_1x()){
  flg |= GUICORE_SMALL;
 }
 if (main_cfg.display_gameonly){
  flg |= GUICORE_GAMEONLY;
 }
 if (main_cfg.display_fullscreen){
  flg |= GUICORE_FULLSCREEN;
 }
 if (!main_cfg.frame_rate_limiter){
  flg |= GUICORE_NOVSYNC;
 }

 if (!guicore_init(flg, main_title)){
  netplay_shutdown();
  remote_roms_shutdown();
  savestate_shutdown();
  return 1;
 }
#ifdef ENABLE_MICROUI
 if (mui_init() == 0){
  netplay_shutdown();
  remote_roms_shutdown();
  savestate_shutdown();
  guicore_quit();
  return 1;
 }
#endif
 (void)(audio_init());
 (void)(ginput_init());
 cu_kbd_set_enqueue_hook(main_netplay_kbd_enqueue_hook);
	cu_kbd_set_visible_hook(main_input_trace_kbd_visible_byte);
 main_cfg_apply_runtime();
 main_cfg_dirty = FALSE;
 guicore_seticon_uze(NULL);

 cu_avr_autofuse((boole)(bootpri || main_resident_bootloader_boot_priority(&(ecpu->crom[0]))));
 cu_avr_reset();
 main_netplay_runtime_reset();
 main_t5_cc = cu_avr_getcycle();
 audio_reset();
 textgui_reset();
 main_frame_counter = 0U;

 if (startup_builtin_bootloader){
  uint32 np_rom;
  uint32 np_build;
  uint32 np_features;
  auint  np_player;
  auint  np_max;
  netplay_get_local_identity(&np_rom, &np_build, &np_features, &np_player, &np_max);
  if (np_features == 0U){ np_features = (uint32)(NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP); }
  netplay_set_local_identity(0U, (uint32)VER_DATE, np_features, np_player, np_max);
  strncpy(main_rom_name, "Built-in Bootloader", sizeof(main_rom_name) - 1U);
  main_rom_name[sizeof(main_rom_name) - 1U] = 0;
  main_loaded_rom_path[0] = 0;
  guicore_seticon_uze(NULL);
  guicore_set_jamma(0U);
  main_apply_esp_softap_policy(FALSE, NULL);
 }else if (uzefile){
  main_apply_loaded_uzerom(&ufhead);
  strncpy(main_loaded_rom_path, &(startup_game[0]), sizeof(main_loaded_rom_path) - 1U);
  main_loaded_rom_path[sizeof(main_loaded_rom_path) - 1U] = 0;
 }else{
  uint32 np_rom;
  uint32 np_build;
  uint32 np_features;
  auint  np_player;
  auint  np_max;
  netplay_get_local_identity(&np_rom, &np_build, &np_features, &np_player, &np_max);
  if (np_features == 0U){ np_features = (uint32)(NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP); }
  netplay_set_local_identity(0U, (uint32)VER_DATE, np_features, np_player, np_max);
  strncpy(main_rom_name, main_path_basename(&(startup_game[0])), sizeof(main_rom_name) - 1U);
  main_rom_name[sizeof(main_rom_name) - 1U] = 0;
  strncpy(main_loaded_rom_path, &(startup_game[0]), sizeof(main_loaded_rom_path) - 1U);
  main_loaded_rom_path[sizeof(main_loaded_rom_path) - 1U] = 0;
  main_apply_esp_softap_policy(FALSE, NULL);
 }
 api_server_notify_rom_loaded();
#ifdef ENABLE_DEBUGGER
 main_dbg_profile_loaded = FALSE;
 main_dbg_profile_dir[0] = 0;
 mainui_debug_profile_status_set("Debugger profile pending.");
#endif
 cheats_set_master_enabled(main_cfg.cheats_enabled);
 cheats_set_autoloadsave(main_cfg.cheats_autoloadsave);
 if (cheats_get_autoloadsave()){
  main_try_load_cheats();
 }else{
  cheats_set_current_path(main_cheatfile);
 }
 /* cu_mouse_set_enabled((ufhead.pdefault & PERIPHERAL_MOUSE)?1:0); */
 /* cu_kbd_set_enabled((ufhead.pdefault & PERIPHERAL_KEYBOARD)?1:0); */
 /* cu_multitap_set_enabled((ufhead.pdefault & PERIPHERAL_MULTITAP)?1:0); */
 /* cu_esp_set_enabled((ufhead.pdefault & PERIPHERAL_ESP8266)?1:0); */

 main_ptick = SDL_GetTicks() - 16U; /* First frame interval should be normal */
#ifdef __EMSCRIPTEN__
 emscripten_set_main_loop(&main_loop, 0, 1);
#else
 while (!main_exit){ main_loop(); }


 ecpu = cu_avr_get_state();
 if ( (main_promch) || (cu_avr_crom_ischanged  (TRUE)) ){ romdump_save(&(ecpu->crom[0])); }
 if ( (main_peepch) || (cu_avr_eeprom_ischanged(TRUE)) ){ eepdump_save(&(ecpu->eepr[0])); }
 if (cheats_is_dirty() && cheats_get_autoloadsave()){ cheats_save_file(NULL); }
if (main_input_profile_dirty){ (void)mainui_input_profile_save_current(); }
#ifdef ENABLE_DEBUGGER
 if (main_dbg_profile_dirty && main_dbg_profile_loaded){ (void)mainui_debug_profile_save_current(); }
#endif

 main_cfg_capture_runtime();
 if (main_cfg_dirty){
  appcfg_save(&main_cfg, main_cfgfile);
#ifdef ENABLE_ESP
  cu_esp_save_config();
#endif
 }

 ginput_quit();
 audio_quit();
 web_server_shutdown();
 api_server_shutdown();
#ifdef ENABLE_MICROUI
 mui_shutdown();
#endif
#ifdef ENABLE_ESP
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 /*
  * On Windows process exit, skipping full ESP runtime shutdown avoids a
  * noticeable exit stall when the ESP backend has background state alive.
  * The process exits immediately after this path, so the OS will reclaim the
  * remaining ESP resources anyway.
  */
#else
 cu_esp_runtime_shutdown();
#endif
#endif
 guicore_quit();
 remote_roms_shutdown();
 savestate_shutdown();
 netplay_shutdown();
 filesys_flushall();
#ifdef ENABLE_ICAP
 capture_finalize();
#endif
#ifdef ENABLE_VCAP
 avconv_finalize();
#endif
 exit(0);
#endif

 netplay_shutdown();
 return 0;
}
