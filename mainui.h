#ifndef MAINUI_H
#define MAINUI_H

#define MAINUI_ESP_SOFTAP_MODE_HINT    0U
#define MAINUI_ESP_SOFTAP_MODE_DISABLE 1U
#define MAINUI_ESP_SOFTAP_MODE_ENABLE  2U

#include "types.h"
#include "cheats.h"
#include "renderpath.h"
#include "cu_spisd.h"

#define MAINUI_RECENT_ROMS 10U


typedef struct{
	auint pc;
	auint cycle;
	auint sp;
	auint row_pulse;
	uint8 sreg;
	uint8 regs[32];
} mainui_debug_cpu_t;

typedef struct{
	auint file_index;
	auint line;
	auint column;
	auint word_addr;
	auint end_word_addr;
} mainui_debug_source_t;

typedef struct{
	boole	active;
	boole	predicted_any;
	boole	stalled;
	boole	resim_applied;
	auint	frame;
	auint	local_player;
	auint	remote_player;
	uint32	local_player_mask;
	uint32	remote_player_mask;
	auint	rollback_window;
	auint	input_delay;
	auint	resim_from_frame;
	auint	resim_to_frame;
} mainui_netplay_runtime_t;

#define MAINUI_DBG_MEM_SRAM    0U
#define MAINUI_DBG_MEM_SPIRAM  1U
#define MAINUI_DBG_MEM_EEPROM  2U
#define MAINUI_DBG_MEM_FLASH   3U

#define MAINUI_DBG_SYMBOL_PROG  0U
#define MAINUI_DBG_SYMBOL_DATA  1U

#define MAINUI_DBG_WATCH_REGION_SRAM  0U
#define MAINUI_DBG_WATCH_REGION_IO    1U
#define MAINUI_DBG_WATCH_READ         0x01U
#define MAINUI_DBG_WATCH_WRITE        0x02U
#define MAINUI_DBG_WATCH_SLOTS        8U
#define MAINUI_DBG_VALUE_WATCHES      8U

boole mainui_get_display_gameonly(void);
boole mainui_get_display_fullscreen(void);
boole mainui_get_system_messages(void);
auint mainui_get_log_verbosity(void);
boole mainui_get_gui_gamepad_enable(void);
auint mainui_get_gui_gamepad_speed_pct(void);
boole mainui_get_gui_pause_while_open(void);
boole mainui_get_gui_virtual_keyboard(void);
boole mainui_get_frame_rate_limiter(void);
boole mainui_get_frame_merge(void);
boole mainui_get_input_kbuzem(void);
boole mainui_get_input_player2alloc(void);
boole mainui_get_audio_freqscale(void);
boole mainui_get_audio_s16(void);
auint mainui_get_audio_output_rate(void);
auint mainui_get_audio_latency(void);
auint mainui_get_audio_resampler(void);
boole mainui_get_audio_dcblock(void);
auint mainui_get_audio_lowpass(void);
auint mainui_get_audio_lowpass_quality(void);
auint mainui_get_audio_monitor_mode(void);
auint mainui_get_audio_monitor_width(void);
auint mainui_get_audio_reverb(void);
auint mainui_get_audio_master_volume(void);
auint mainui_get_audio_freq_hz(void);
auint mainui_get_audio_source_freq_hz(void);
auint mainui_get_audio_output_format(void);
auint mainui_get_audio_output_channels(void);
auint mainui_get_audio_output_samples(void);
boole mainui_get_mouse_enable(void);
auint mainui_get_mouse_scale(void);
auint mainui_get_render_path(void);
auint mainui_get_filter_pre_mode(void);
auint mainui_get_filter_scale_mode(void);
auint mainui_get_filter_crt_mode(void);
auint mainui_get_spiram_pages_mode(void);
boole mainui_get_sd_allow_new_files(void);
auint mainui_get_sd_timing_preset(void);
void mainui_get_sd_timing_model(cu_spisd_model_t* out);
auint mainui_get_esp_softap_mode(void);
auint mainui_get_serial_route(void);
auint mainui_get_serial_esp_model(void);
auint mainui_get_esp_at_firmware_profile(void);
auint mainui_get_uart_profile(void);
char const* mainui_get_host_serial_device_name(void);
char const* mainui_get_host_midi_port_name(void);
auint mainui_get_virtual_midi_mode(void);
char const* mainui_get_virtual_midi_port_name(void);
char const* mainui_get_tcp_serial_host(void);
auint mainui_get_tcp_serial_port(void);
boole mainui_get_tcp_serial_auto_reconnect(void);
auint mainui_get_tcp_serial_mode(void);
auint mainui_get_tcp_serial_state(void);
sint32 mainui_get_tcp_serial_last_error(void);
boole mainui_get_host_midi_supported(void);
boole mainui_get_virtual_midi_supported(void);
void mainui_refresh_host_midi_ports(void);
auint mainui_get_host_midi_port_count(void);
char const* mainui_get_host_midi_port_name_at(auint idx);
char const* mainui_get_host_midi_port_label_at(auint idx);
char const* mainui_get_rom_path(void);
char const* mainui_get_screenshot_path(void);
char const* mainui_get_save_path(void);
char const* mainui_get_controllerdb_path(void);
boole mainui_get_resident_bootloader_enable(void);
boole mainui_get_fast_flash(void);
char const* mainui_get_resident_bootloader_file(void);
char const* mainui_get_remote_roms_host(void);
char const* mainui_get_gui_theme_name(void);
char const* mainui_get_gui_theme_file(void);
char const* mainui_get_current_rom_name(void);
char const* mainui_get_current_rom_author(void);
auint mainui_get_current_rom_year(void);
boole mainui_get_current_rom_is_uze(void);
uint32 mainui_get_current_rom_crc32(void);
uint32 mainui_get_input_topology_hash(void);
boole mainui_get_input_trace_enabled(void);
void mainui_set_input_trace_enabled(boole enable);
void mainui_clear_input_trace(void);
auint mainui_get_input_trace_line_count(void);
auint mainui_get_input_trace_capacity(void);
void mainui_format_input_trace_line(auint idx, char* out, auint out_size);
uint8 const* mainui_get_current_rom_icon(void);
uint32 mainui_get_frame_counter(void);
char const* mainui_get_loaded_rom_path(void);
boole mainui_get_recent_rom_path(auint idx, char* out, auint out_size);
boole mainui_load_recent_rom(auint idx);
void mainui_clear_recent_roms(void);
boole mainui_get_recent_roms_frozen(void);
void mainui_set_recent_roms_frozen(boole frozen);
void mainui_reset_rom(void);
void mainui_request_quit(void);
boole mainui_get_video_dump_active(void);
char const* mainui_get_video_dump_file(void);
char const* mainui_get_video_dump_active_file(void);
char const* mainui_get_video_dump_status(void);
boole mainui_get_video_dump_autoinc(void);
boole mainui_get_video_dump_reset_first(void);
void mainui_set_video_dump_active(boole active);
void mainui_set_video_dump_file(char const* name);
void mainui_set_video_dump_autoinc(boole enable);
void mainui_set_video_dump_reset_first(boole enable);
boole mainui_get_input_capture_active(void);
void mainui_set_input_capture_active(boole active);
char const* mainui_get_input_capture_file(void);
boole mainui_save_screenshot_auto(char* out_path, auint out_size);
boole mainui_save_screenshot_file(char const* path);
/* Capture the current CUzeBox display as a complete 24-bit BMP in memory.
** The caller owns *out_data and must free() it. */
boole mainui_capture_screenshot_bmp(uint8** out_data, auint* out_size, auint* out_width, auint* out_height);
void mainui_get_netplay_runtime(mainui_netplay_runtime_t* out);
auint mainui_get_netplay_rollback_window(void);
void mainui_set_netplay_rollback_window(auint frames);
auint mainui_get_netplay_input_delay(void);
void mainui_set_netplay_input_delay(auint frames);

boole mainui_get_config_dirty(void);

auint mainui_get_cheat_count(void);
boole mainui_get_cheat_entry(auint idx, cheat_entry_t* out);
char const* mainui_get_cheat_path(void);
boole mainui_get_cheat_dirty(void);
boole mainui_get_cheats_enabled(void);
boole mainui_get_cheats_autoloadsave(void);

void mainui_set_display_gameonly(boole enable);
void mainui_set_display_fullscreen(boole enable);
void mainui_set_system_messages(boole enable);
void mainui_set_log_verbosity(auint level);
void mainui_set_gui_gamepad_enable(boole enable);
void mainui_set_gui_gamepad_speed_pct(auint percent);
void mainui_set_gui_pause_while_open(boole enable);
void mainui_set_gui_virtual_keyboard(boole enable);
void mainui_set_frame_rate_limiter(boole enable);
void mainui_set_frame_merge(boole enable);
void mainui_set_input_kbuzem(boole enable);
void mainui_set_input_player2alloc(boole enable);
void mainui_set_audio_freqscale(boole enable);
void mainui_set_audio_s16(boole enable);
void mainui_set_audio_output_rate(auint mode);
void mainui_set_audio_latency(auint mode);
void mainui_set_audio_resampler(auint mode);
void mainui_set_audio_dcblock(boole enable);
void mainui_set_audio_lowpass(auint mode);
void mainui_set_audio_lowpass_quality(auint mode);
void mainui_set_audio_monitor_mode(auint mode);
void mainui_set_audio_monitor_width(auint percent);
void mainui_set_audio_reverb(auint mode);
void mainui_set_audio_master_volume(auint percent);
void mainui_set_mouse_enable(boole enable);
void mainui_set_mouse_scale(auint scale);
void mainui_set_render_path(auint mode);
void mainui_set_filter_pre_mode(auint mode);
void mainui_set_filter_scale_mode(auint mode);
void mainui_set_filter_crt_mode(auint mode);
void mainui_set_spiram_pages_mode(auint mode);
void mainui_set_sd_allow_new_files(boole enable);
void mainui_set_sd_timing_preset(auint preset);
void mainui_set_sd_timing_custom(cu_spisd_model_t const* model);
void mainui_reset_sd_card(void);
void mainui_set_esp_softap_mode(auint mode);
void mainui_set_serial_route(auint route);
void mainui_set_serial_esp_model(auint model);
void mainui_set_esp_at_firmware_profile(auint profile);
void mainui_set_uart_profile(auint profile);
void mainui_set_host_serial_device_name(char const* name);
void mainui_set_host_midi_port_name(char const* name);
void mainui_set_virtual_midi_mode(auint mode);
void mainui_set_virtual_midi_port_name(char const* name);
void mainui_set_tcp_serial_host(char const* host);
void mainui_set_tcp_serial_port(auint port);
void mainui_set_tcp_serial_auto_reconnect(boole enable);
void mainui_set_tcp_serial_mode(auint mode);
void mainui_set_rom_path(char const* path);
void mainui_set_screenshot_path(char const* path);
void mainui_set_save_path(char const* path);
void mainui_set_controllerdb_path(char const* path);
boole mainui_reload_controllerdb_now(void);
void mainui_set_resident_bootloader_enable(boole enable);
void mainui_set_fast_flash(boole enable);
void mainui_set_resident_bootloader_file(char const* path);
void mainui_set_remote_roms_host(char const* host);
void mainui_set_gui_theme_file(char const* path);
void mainui_apply_gui_theme_preset(char const* name);
boole mainui_load_gui_theme_file(char const* path);
boole mainui_save_gui_theme_file(char const* path);
boole mainui_load_rom_file(char const* path);

void mainui_system_message(char const* fmt, ...);
boole mainui_open_web_tool(char const* section);

boole mainui_add_cheat_entry(auint* out_idx);
boole mainui_set_cheat_entry(auint idx, cheat_entry_t const* in);
void  mainui_remove_cheat_entry(auint idx);
void  mainui_clear_cheats(void);
void  mainui_cheats_save_now(void);
void  mainui_cheats_reload_now(void);
void  mainui_set_cheats_enabled(boole enable);
void  mainui_set_cheats_autoloadsave(boole enable);

void mainui_config_save_now(void);
void mainui_config_reload_now(void);
void mainui_config_defaults_now(void);
void mainui_touch_gui_config(void);
void mainui_touch_config(void);
boole mainui_multitap_select_slot(auint port, auint slot);
boole mainui_multitap_probe_next_read(auint port);


boole mainui_debug_is_paused(void);
void mainui_debug_set_paused(boole paused);
void mainui_debug_step_frame(void);
void mainui_debug_step_instruction(void);
boole mainui_debug_step_over(void);
boole mainui_debug_step_out(void);
boole mainui_debug_run_to_word(auint word_addr);
boole mainui_debug_get_run_target(auint* out_word_addr);
char const* mainui_debug_get_run_control_status(void);
void mainui_debug_get_cpu(mainui_debug_cpu_t* out);
auint mainui_debug_get_sram_byte(auint addr);
auint mainui_debug_get_io_byte(auint addr);
auint mainui_debug_get_prog_word(auint word_addr);
boole mainui_debug_get_disasm(auint word_addr, char* out, auint out_size, auint* out_words);
char const* mainui_debug_get_symbols_file(void);
char const* mainui_debug_get_symbols_status(void);
auint mainui_debug_get_symbol_count(void);
void mainui_debug_set_symbols_file(char const* path);
boole mainui_debug_load_symbols_file(char const* path);
void mainui_debug_clear_symbols(void);
auint mainui_debug_get_mem_region_size(auint region);
auint mainui_debug_get_mem_region_byte(auint region, auint addr);
void mainui_debug_set_reg_byte(auint reg, auint value);
void mainui_debug_set_sram_byte(auint addr, auint value);
void mainui_debug_set_io_byte(auint addr, auint value);
void mainui_debug_set_mem_region_byte(auint region, auint addr, auint value);
boole mainui_debug_get_breakpoint(auint word_addr);
void mainui_debug_set_breakpoint(auint word_addr, boole enable);
void mainui_debug_clear_breakpoints(void);
boole mainui_debug_next_breakpoint(auint start_word_addr, auint* out_word_addr);
boole mainui_debug_get_last_break_hit(auint* out_word_addr);
void mainui_debug_clear_last_break_hit(void);
void mainui_debug_watchpoint_set(auint slot, boole enable, auint region, auint flags, auint start_addr, auint end_addr);
boole mainui_debug_watchpoint_get(auint slot, boole* out_enable, auint* out_region, auint* out_flags, auint* out_start_addr, auint* out_end_addr);
boole mainui_debug_watchpoint_next(auint start_slot, auint* out_slot);
void mainui_debug_clear_watchpoints(void);
boole mainui_debug_get_last_watch_hit(auint* out_slot, auint* out_region, auint* out_flags, auint* out_addr, auint* out_value, auint* out_pc);
char const* mainui_debug_get_data_label(auint addr);
boole mainui_debug_find_symbol(char const* name, auint* out_kind, auint* out_addr);
char const* mainui_debug_get_source_status(void);
auint mainui_debug_get_source_file_count(void);
auint mainui_debug_get_source_row_count(void);
char const* mainui_debug_get_source_file(auint index);
boole mainui_debug_source_lookup(auint word_addr, mainui_debug_source_t* out);
boole mainui_debug_source_resolve_line(auint file_index, auint line, mainui_debug_source_t* out);
char const* mainui_input_profile_get_dir(void);
char const* mainui_input_profile_get_status(void);
void mainui_input_profile_mark_dirty(void);
boole mainui_input_profile_save_now(void);
boole mainui_input_profile_reload(void);

char const* mainui_debug_get_profile_dir(void);
char const* mainui_debug_get_profile_status(void);
void mainui_debug_profile_mark_dirty(void);
boole mainui_debug_profile_save_now(void);
boole mainui_debug_profile_reload(void);
boole mainui_debug_profile_ensure_loaded(void);

#endif
