/* Exercise production configuration and GUI setters without touching user files.
 * Built with the application's flags and objects; main.c is included to verify
 * the same capture/apply functions used by Save, Reload and normal exit. */
#define SDL_MAIN_HANDLED
#undef main
#define main preferences_emulator_main
#include "../../main.c"
#undef main

#define CHECK(cond, message) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } } while (0)

static int roundtrip(void)
{
 app_config_t expected, loaded;
 auint i, b, m;
 appcfg_defaults(&expected);
	CHECK(expected.input_movie_compact, "Compact recording default");
 expected.input_movie_compact = FALSE;
 expected.version = APPCFG_VERSION;
	expected.display_gameonly = !expected.display_gameonly;
	expected.display_fullscreen = !expected.display_fullscreen;
	expected.display_system_messages = !expected.display_system_messages;
	expected.log_verbosity = CU_LOG_TRACE;
	expected.gui_gamepad_enable = !expected.gui_gamepad_enable;
	expected.gui_gamepad_speed_pct = 125U;
	expected.gui_pause_while_open = !expected.gui_pause_while_open;
	expected.gui_virtual_keyboard = !expected.gui_virtual_keyboard;
	expected.frame_rate_limiter = !expected.frame_rate_limiter;
	expected.frame_merge = !expected.frame_merge;
 CHECK(!expected.tv_sync_pll, "TV sync PLL defaults off");
 expected.tv_sync_pll = TRUE;
 CHECK(expected.tv_sync_recovery == 100U, "original PLL recovery default");
 expected.tv_sync_recovery = 400U;
 CHECK(expected.tv_sync_strength == 100U, "full skew default");
 expected.tv_sync_strength = 35U;
	expected.render_path = RENDER_PATH_CLASSIC_2X;
	expected.input_kbuzem = !expected.input_kbuzem;
	expected.input_player2alloc = !expected.input_player2alloc;
	expected.audio_freqscale = !expected.audio_freqscale;
	expected.audio_s16 = !expected.audio_s16;
	expected.audio_output_rate = AUDIO_OUTRATE_96000;
	expected.audio_latency = AUDIO_LATENCY_SAFE;
	expected.audio_resampler = AUDIO_RESAMPLER_CUBIC;
	expected.audio_dcblock = !expected.audio_dcblock;
	expected.audio_lowpass = AUDIO_LOWPASS_STRONG;
	expected.audio_lowpass_quality = AUDIO_LOWPASS_QUALITY_SIMPLE;
	expected.audio_monitor_mode = AUDIO_MONITOR_STEREO;
	expected.audio_monitor_width = 175U;
	expected.audio_reverb = AUDIO_REVERB_STRONG;
	expected.audio_master_volume = 137U;
	expected.mouse_enable = !expected.mouse_enable;
	expected.mouse_scale = 3U;
	expected.filter_pre_mode = FILTER_PRE_BLACK_WHITE;
	expected.filter_scale_mode = FILTER_SCALE_XBR2X;
	expected.filter_crt_mode = FILTER_CRT_HEATWAVE;
	expected.spiram_pages_mode = 8U;
	expected.sd_allow_new_files = !expected.sd_allow_new_files;
	expected.sd_timing_preset = 0U;
	expected.sd_init_ms = expected.sd_init_ms + 1U;
	expected.sd_cmd_wait_bytes = expected.sd_cmd_wait_bytes + 1U;
	expected.sd_read_wait_bytes = expected.sd_read_wait_bytes + 1U;
	expected.sd_write_busy_ms = expected.sd_write_busy_ms + 1U;
	expected.sd_cs_high_ms = expected.sd_cs_high_ms + 1U;
	expected.sd_init_min_byte_cycles = expected.sd_init_min_byte_cycles + 1U;
	expected.sd_init_max_byte_cycles = expected.sd_init_max_byte_cycles + 1U;
	expected.esp_softap_mode = 2U;
	expected.netplay_rollback_window = 24U;
	expected.netplay_input_delay = 4U;
	expected.netplay_max_players = 4U;
	expected.netplay_local_player_mask = 5U;
	expected.netplay_rom_send = !expected.netplay_rom_send;
	expected.netplay_rom_receive = !expected.netplay_rom_receive;
	expected.netplay_rom_sync_mode = NETPLAY_ROM_SYNC_ALWAYS;
	expected.netplay_rom_tcp_port = 43812U;
	expected.netplay_rom_max_size = 8U * 1024U * 1024U;
	expected.netplay_relay_port = 43815U;
	expected.video_dump_autoinc = !expected.video_dump_autoinc;
	expected.video_dump_reset_first = !expected.video_dump_reset_first;
	expected.video_dump_omit_messages = FALSE;
	expected.resident_bootloader_enable = !expected.resident_bootloader_enable;
	expected.fast_flash = !expected.fast_flash;
	expected.recent_roms_frozen = !expected.recent_roms_frozen;
	expected.api_server = !expected.api_server;
	expected.api_server_port = 40123U;
	expected.cheats_enabled = !expected.cheats_enabled;
	expected.cheats_autoloadsave = !expected.cheats_autoloadsave;
	main_copy_str(expected.netplay_name, sizeof(expected.netplay_name), "test=value # spaces");
	snprintf(expected.netplay_relay_host, sizeof(expected.netplay_relay_host), "test=value # spaces");
	snprintf(expected.netplay_network_interface, sizeof(expected.netplay_network_interface), "test=value # spaces");
	snprintf(expected.remote_roms_host, sizeof(expected.remote_roms_host), "test=value # spaces");
	snprintf(expected.video_dump_file, sizeof(expected.video_dump_file), "test=value # spaces");
	snprintf(expected.gui_theme_name, sizeof(expected.gui_theme_name), "test=value # spaces");
	snprintf(expected.gui_theme_file, sizeof(expected.gui_theme_file), "test=value # spaces");
	snprintf(expected.rom_path, sizeof(expected.rom_path), "test=value # spaces");
	snprintf(expected.screenshot_path, sizeof(expected.screenshot_path), "test=value # spaces");
	expected.screenshot_include_gui = TRUE;
	expected.screenshot_bypass_filters = FALSE;
	expected.screenshot_x_width=256U;expected.screenshot_y_scale=3U;expected.screenshot_sampling=2U;
	snprintf(expected.save_path, sizeof(expected.save_path), "test=value # spaces");
	snprintf(expected.controllerdb_path, sizeof(expected.controllerdb_path), "test=value # spaces");
	snprintf(expected.resident_bootloader_file, sizeof(expected.resident_bootloader_file), "test=value # spaces");
 for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
  for (b = 0U; b < 4U; b++){ expected.gui_colors[i][b] = (uint8)(i * 7U + b); }
 }
 for (i = 0U; i < APPCFG_RECENT_ROMS; i++){
  snprintf(expected.recent_roms[i], sizeof(expected.recent_roms[i]), "roms/game %u=demo.uze", (unsigned)i);
 }
 for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
  snprintf(expected.netplay_pad_labels[i], sizeof(expected.netplay_pad_labels[i]), "Pad %u", (unsigned)i);
 }
 for (i = 0U; i < CU_MULTITAP_MAX_PORTS; i++){
  expected.tap_present[i] = TRUE;
  expected.tap_active_slot[i] = 2U;
  for (b = 0U; b < CU_MULTITAP_MAX_SLOTS; b++){ expected.tap_slot_vdev[i][b] = b; }
 }
 for (i = 0U; i < CU_VDEV_MAX; i++){
  appcfg_vdev_t* v = &expected.vdev[i];
  v->used = TRUE;
  snprintf(v->name, sizeof(v->name), "Device %u", (unsigned)i);
  v->type = CU_VDEV_TYPE_KEYBOARD;
  v->options = 3U;
  v->haptic_enabled = TRUE; v->haptic_accept_any = FALSE;
  v->haptic_binding = 2U; v->haptic_id = 5U;
  v->low16 = 0x1234U; v->high16 = 0x5678U;
  v->sm_scale_x_pct = 175U; v->sm_scale_y_pct = 80U;
  v->sm_deadzone = 7U; v->sm_invert_x = TRUE; v->sm_invert_y = TRUE;
  for (b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
   v->binding[b].type = CU_VDEV_HOST_KEYBOARD;
   v->binding[b].host_index = b; v->binding[b].flags = 1U;
   for (m = 0U; m < CU_VDEV_REMAP_SLOTS; m++){ v->binding[b].map_low16[m] = 1U << (m % 16U); }
  }
 }
 CHECK(appcfg_save(&expected, "roundtrip.cfg"), "save nondefault fixture");
 CHECK(appcfg_load_or_create(&loaded, "roundtrip.cfg"), "reload nondefault fixture");
	CHECK(memcmp(&expected.version, &loaded.version, sizeof(expected.version)) == 0, "version round trip");
	CHECK(memcmp(&expected.display_gameonly, &loaded.display_gameonly, sizeof(expected.display_gameonly)) == 0, "display_gameonly round trip");
	CHECK(memcmp(&expected.display_fullscreen, &loaded.display_fullscreen, sizeof(expected.display_fullscreen)) == 0, "display_fullscreen round trip");
	CHECK(memcmp(&expected.display_system_messages, &loaded.display_system_messages, sizeof(expected.display_system_messages)) == 0, "display_system_messages round trip");
	CHECK(expected.log_verbosity == loaded.log_verbosity, "log_verbosity round trip");
	CHECK(memcmp(&expected.gui_gamepad_enable, &loaded.gui_gamepad_enable, sizeof(expected.gui_gamepad_enable)) == 0, "gui_gamepad_enable round trip");
	CHECK(memcmp(&expected.gui_gamepad_speed_pct, &loaded.gui_gamepad_speed_pct, sizeof(expected.gui_gamepad_speed_pct)) == 0, "gui_gamepad_speed_pct round trip");
	CHECK(memcmp(&expected.gui_pause_while_open, &loaded.gui_pause_while_open, sizeof(expected.gui_pause_while_open)) == 0, "gui_pause_while_open round trip");
	CHECK(memcmp(&expected.gui_virtual_keyboard, &loaded.gui_virtual_keyboard, sizeof(expected.gui_virtual_keyboard)) == 0, "gui_virtual_keyboard round trip");
	CHECK(memcmp(&expected.frame_rate_limiter, &loaded.frame_rate_limiter, sizeof(expected.frame_rate_limiter)) == 0, "frame_rate_limiter round trip");
	CHECK(memcmp(&expected.frame_merge, &loaded.frame_merge, sizeof(expected.frame_merge)) == 0, "frame_merge round trip");
 CHECK(loaded.tv_sync_pll == expected.tv_sync_pll, "TV sync PLL round trip");
 CHECK(loaded.tv_sync_recovery == 400U, "PLL recovery round trip");
 CHECK(loaded.tv_sync_strength == 35U, "PLL strength round trip");
	CHECK(memcmp(&expected.render_path, &loaded.render_path, sizeof(expected.render_path)) == 0, "render_path round trip");
	CHECK(memcmp(&expected.input_kbuzem, &loaded.input_kbuzem, sizeof(expected.input_kbuzem)) == 0, "input_kbuzem round trip");
	CHECK(memcmp(&expected.input_player2alloc, &loaded.input_player2alloc, sizeof(expected.input_player2alloc)) == 0, "input_player2alloc round trip");
	CHECK(memcmp(&expected.audio_freqscale, &loaded.audio_freqscale, sizeof(expected.audio_freqscale)) == 0, "audio_freqscale round trip");
	CHECK(memcmp(&expected.audio_s16, &loaded.audio_s16, sizeof(expected.audio_s16)) == 0, "audio_s16 round trip");
	CHECK(memcmp(&expected.audio_output_rate, &loaded.audio_output_rate, sizeof(expected.audio_output_rate)) == 0, "audio_output_rate round trip");
	CHECK(memcmp(&expected.audio_latency, &loaded.audio_latency, sizeof(expected.audio_latency)) == 0, "audio_latency round trip");
	CHECK(memcmp(&expected.audio_resampler, &loaded.audio_resampler, sizeof(expected.audio_resampler)) == 0, "audio_resampler round trip");
	CHECK(memcmp(&expected.audio_dcblock, &loaded.audio_dcblock, sizeof(expected.audio_dcblock)) == 0, "audio_dcblock round trip");
	CHECK(memcmp(&expected.audio_lowpass, &loaded.audio_lowpass, sizeof(expected.audio_lowpass)) == 0, "audio_lowpass round trip");
	CHECK(memcmp(&expected.audio_lowpass_quality, &loaded.audio_lowpass_quality, sizeof(expected.audio_lowpass_quality)) == 0, "audio_lowpass_quality round trip");
	CHECK(memcmp(&expected.audio_monitor_mode, &loaded.audio_monitor_mode, sizeof(expected.audio_monitor_mode)) == 0, "audio_monitor_mode round trip");
	CHECK(memcmp(&expected.audio_monitor_width, &loaded.audio_monitor_width, sizeof(expected.audio_monitor_width)) == 0, "audio_monitor_width round trip");
	CHECK(memcmp(&expected.audio_reverb, &loaded.audio_reverb, sizeof(expected.audio_reverb)) == 0, "audio_reverb round trip");
	CHECK(memcmp(&expected.audio_master_volume, &loaded.audio_master_volume, sizeof(expected.audio_master_volume)) == 0, "audio_master_volume round trip");
	CHECK(memcmp(&expected.mouse_enable, &loaded.mouse_enable, sizeof(expected.mouse_enable)) == 0, "mouse_enable round trip");
	CHECK(memcmp(&expected.mouse_scale, &loaded.mouse_scale, sizeof(expected.mouse_scale)) == 0, "mouse_scale round trip");
	CHECK(memcmp(&expected.filter_pre_mode, &loaded.filter_pre_mode, sizeof(expected.filter_pre_mode)) == 0, "filter_pre_mode round trip");
	CHECK(memcmp(&expected.filter_scale_mode, &loaded.filter_scale_mode, sizeof(expected.filter_scale_mode)) == 0, "filter_scale_mode round trip");
	CHECK(memcmp(&expected.filter_crt_mode, &loaded.filter_crt_mode, sizeof(expected.filter_crt_mode)) == 0, "filter_crt_mode round trip");
	CHECK(memcmp(&expected.spiram_pages_mode, &loaded.spiram_pages_mode, sizeof(expected.spiram_pages_mode)) == 0, "spiram_pages_mode round trip");
	CHECK(memcmp(&expected.sd_allow_new_files, &loaded.sd_allow_new_files, sizeof(expected.sd_allow_new_files)) == 0, "sd_allow_new_files round trip");
	CHECK(memcmp(&expected.sd_timing_preset, &loaded.sd_timing_preset, sizeof(expected.sd_timing_preset)) == 0, "sd_timing_preset round trip");
	CHECK(memcmp(&expected.sd_init_ms, &loaded.sd_init_ms, sizeof(expected.sd_init_ms)) == 0, "sd_init_ms round trip");
	CHECK(memcmp(&expected.sd_cmd_wait_bytes, &loaded.sd_cmd_wait_bytes, sizeof(expected.sd_cmd_wait_bytes)) == 0, "sd_cmd_wait_bytes round trip");
	CHECK(memcmp(&expected.sd_read_wait_bytes, &loaded.sd_read_wait_bytes, sizeof(expected.sd_read_wait_bytes)) == 0, "sd_read_wait_bytes round trip");
	CHECK(memcmp(&expected.sd_write_busy_ms, &loaded.sd_write_busy_ms, sizeof(expected.sd_write_busy_ms)) == 0, "sd_write_busy_ms round trip");
	CHECK(memcmp(&expected.sd_cs_high_ms, &loaded.sd_cs_high_ms, sizeof(expected.sd_cs_high_ms)) == 0, "sd_cs_high_ms round trip");
	CHECK(memcmp(&expected.sd_init_min_byte_cycles, &loaded.sd_init_min_byte_cycles, sizeof(expected.sd_init_min_byte_cycles)) == 0, "sd_init_min_byte_cycles round trip");
	CHECK(memcmp(&expected.sd_init_max_byte_cycles, &loaded.sd_init_max_byte_cycles, sizeof(expected.sd_init_max_byte_cycles)) == 0, "sd_init_max_byte_cycles round trip");
	CHECK(memcmp(&expected.esp_softap_mode, &loaded.esp_softap_mode, sizeof(expected.esp_softap_mode)) == 0, "esp_softap_mode round trip");
	CHECK(memcmp(&expected.netplay_rollback_window, &loaded.netplay_rollback_window, sizeof(expected.netplay_rollback_window)) == 0, "netplay_rollback_window round trip");
	CHECK(memcmp(&expected.netplay_input_delay, &loaded.netplay_input_delay, sizeof(expected.netplay_input_delay)) == 0, "netplay_input_delay round trip");
	CHECK(memcmp(&expected.netplay_max_players, &loaded.netplay_max_players, sizeof(expected.netplay_max_players)) == 0, "netplay_max_players round trip");
	CHECK(memcmp(&expected.netplay_local_player_mask, &loaded.netplay_local_player_mask, sizeof(expected.netplay_local_player_mask)) == 0, "netplay_local_player_mask round trip");
	CHECK(memcmp(&expected.netplay_rom_send, &loaded.netplay_rom_send, sizeof(expected.netplay_rom_send)) == 0, "netplay_rom_send round trip");
	CHECK(memcmp(&expected.netplay_rom_receive, &loaded.netplay_rom_receive, sizeof(expected.netplay_rom_receive)) == 0, "netplay_rom_receive round trip");
	CHECK(memcmp(&expected.netplay_rom_sync_mode, &loaded.netplay_rom_sync_mode, sizeof(expected.netplay_rom_sync_mode)) == 0, "netplay_rom_sync_mode round trip");
	CHECK(memcmp(&expected.netplay_rom_tcp_port, &loaded.netplay_rom_tcp_port, sizeof(expected.netplay_rom_tcp_port)) == 0, "netplay_rom_tcp_port round trip");
	CHECK(memcmp(&expected.netplay_rom_max_size, &loaded.netplay_rom_max_size, sizeof(expected.netplay_rom_max_size)) == 0, "netplay_rom_max_size round trip");
	CHECK(strcmp(expected.netplay_name, loaded.netplay_name) == 0, "netplay_name round trip");
	CHECK(memcmp(&expected.netplay_pad_labels, &loaded.netplay_pad_labels, sizeof(expected.netplay_pad_labels)) == 0, "netplay_pad_labels round trip");
	CHECK(memcmp(&expected.netplay_relay_port, &loaded.netplay_relay_port, sizeof(expected.netplay_relay_port)) == 0, "netplay_relay_port round trip");
	CHECK(strcmp(expected.netplay_relay_host, loaded.netplay_relay_host) == 0, "netplay_relay_host round trip");
	CHECK(strcmp(expected.netplay_network_interface, loaded.netplay_network_interface) == 0, "netplay_network_interface round trip");
	CHECK(strcmp(expected.remote_roms_host, loaded.remote_roms_host) == 0, "remote_roms_host round trip");
	CHECK(strcmp(expected.video_dump_file, loaded.video_dump_file) == 0, "video_dump_file round trip");
	CHECK(memcmp(&expected.video_dump_autoinc, &loaded.video_dump_autoinc, sizeof(expected.video_dump_autoinc)) == 0, "video_dump_autoinc round trip");
	CHECK(memcmp(&expected.video_dump_reset_first, &loaded.video_dump_reset_first, sizeof(expected.video_dump_reset_first)) == 0, "video_dump_reset_first round trip");
	CHECK(!loaded.video_dump_omit_messages,"video_dump_omit_messages round trip");
	CHECK(memcmp(&expected.gui_colors, &loaded.gui_colors, sizeof(expected.gui_colors)) == 0, "gui_colors round trip");
	CHECK(strcmp(expected.gui_theme_name, loaded.gui_theme_name) == 0, "gui_theme_name round trip");
	CHECK(strcmp(expected.gui_theme_file, loaded.gui_theme_file) == 0, "gui_theme_file round trip");
	CHECK(strcmp(expected.rom_path, loaded.rom_path) == 0, "rom_path round trip");
	CHECK(strcmp(expected.screenshot_path, loaded.screenshot_path) == 0, "screenshot_path round trip");
	CHECK(!loaded.input_movie_compact, "Full Debug recording preference round trip");
	CHECK(loaded.screenshot_include_gui, "screenshot_include_gui round trip");
	CHECK(!loaded.screenshot_bypass_filters, "screenshot_bypass_filters round trip");
	CHECK(loaded.screenshot_x_width==256U&&loaded.screenshot_y_scale==3U&&loaded.screenshot_sampling==2U,"screenshot scales and sampling round trip");
	CHECK(strcmp(expected.save_path, loaded.save_path) == 0, "save_path round trip");
	CHECK(strcmp(expected.controllerdb_path, loaded.controllerdb_path) == 0, "controllerdb_path round trip");
	CHECK(memcmp(&expected.resident_bootloader_enable, &loaded.resident_bootloader_enable, sizeof(expected.resident_bootloader_enable)) == 0, "resident_bootloader_enable round trip");
	CHECK(expected.fast_flash == loaded.fast_flash, "fast_flash round trip");
	CHECK(strcmp(expected.resident_bootloader_file, loaded.resident_bootloader_file) == 0, "resident_bootloader_file round trip");
	CHECK(memcmp(&expected.recent_roms, &loaded.recent_roms, sizeof(expected.recent_roms)) == 0, "recent_roms round trip");
	CHECK(memcmp(&expected.recent_roms_frozen, &loaded.recent_roms_frozen, sizeof(expected.recent_roms_frozen)) == 0, "recent_roms_frozen round trip");
	CHECK(memcmp(&expected.api_server, &loaded.api_server, sizeof(expected.api_server)) == 0, "api_server round trip");
	CHECK(memcmp(&expected.api_server_port, &loaded.api_server_port, sizeof(expected.api_server_port)) == 0, "api_server_port round trip");
	CHECK(memcmp(&expected.cheats_enabled, &loaded.cheats_enabled, sizeof(expected.cheats_enabled)) == 0, "cheats_enabled round trip");
	CHECK(memcmp(&expected.cheats_autoloadsave, &loaded.cheats_autoloadsave, sizeof(expected.cheats_autoloadsave)) == 0, "cheats_autoloadsave round trip");
	CHECK(memcmp(&expected.tap_present, &loaded.tap_present, sizeof(expected.tap_present)) == 0, "tap_present round trip");
	CHECK(memcmp(&expected.tap_active_slot, &loaded.tap_active_slot, sizeof(expected.tap_active_slot)) == 0, "tap_active_slot round trip");
	CHECK(memcmp(&expected.tap_slot_vdev, &loaded.tap_slot_vdev, sizeof(expected.tap_slot_vdev)) == 0, "tap_slot_vdev round trip");
	CHECK(memcmp(&expected.vdev, &loaded.vdev, sizeof(expected.vdev)) == 0, "vdev round trip");
 CHECK(!appcfg_save(&expected, "missing-directory/config.cfg"), "unwritable path reports failure");
 /* Windows sharing rules: replacement must fail without deleting the old file. */
#ifdef _WIN32
 {
  HANDLE locked = CreateFileA("roundtrip.cfg", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  CHECK(locked != INVALID_HANDLE_VALUE, "lock existing config");
  expected.audio_master_volume = 17U;
  CHECK(!appcfg_save(&expected, "roundtrip.cfg"), "locked config reports failed save");
  CloseHandle(locked);
  CHECK(appcfg_load_or_create(&loaded, "roundtrip.cfg"), "old config survives replacement failure");
  CHECK(loaded.audio_master_volume == 137U, "old config contents preserved");
 }
#endif
 return 0;
}

static int logging_regression(void)
{
 uint8 sector[512] = {0U};
 auint i, summary_lines;
 textgui_log_set_enabled(TRUE);
 textgui_log_set_level(CU_LOG_INFO);
 cu_message("routine debug diagnostic\n");
 CHECK(textgui_log_get_count() == 0U, "INFO hides routine diagnostics");
 cu_log(CU_LOG_INFO, "file operation notice\n");
 CHECK(textgui_log_get_count() == 1U, "INFO retains useful notices");
 for (i = 0U; i < 100U; i++){ cu_log(CU_LOG_INFO, "file operation notice\n"); }
 CHECK(textgui_log_get_count() == 1U && textgui_log_get_suppressed() == 100U, "repeated logs coalesced");
 for (i = 0U; i < 100U; i++){ cu_log(CU_LOG_INFO, "file operation %u\n", i); }
 CHECK(textgui_log_get_count() <= 4U && textgui_log_get_suppressed() >= 197U, "distinct INFO bursts bounded");
 textgui_log_set_level(CU_LOG_ERROR);
 textgui_log_add("ordinary UI notice");
 cu_error("test error notice\n");
 CHECK(textgui_log_get_count() == 1U, "errors-only mode retains errors");
 textgui_log_set_level(CU_LOG_OFF);
 cu_error("hidden test error\n");
 CHECK(textgui_log_get_count() == 0U, "OFF silences messages");
 textgui_log_set_level(CU_LOG_DEBUG);
 cu_message("visible debug diagnostic\n");
 CHECK(textgui_log_get_count() == 1U, "DEBUG enables diagnostics");
 textgui_log_set_enabled(FALSE);
 cu_log(CU_LOG_INFO, "console-only notice\n");
 CHECK(textgui_log_get_count() == 0U && textgui_log_get_level() == CU_LOG_DEBUG, "overlay switch independent of verbosity");
 textgui_log_set_enabled(TRUE);
 textgui_log_set_level(CU_LOG_TRACE);
 for (i = 0U; i < 20U; i++){ cu_log(CU_LOG_TRACE, "trace sample\n"); }
 CHECK(textgui_log_get_suppressed() == 0U, "TRACE bypasses throttling");
 textgui_log_set_level(CU_LOG_INFO);
 filesys_setpath("./", NULL, 0U);
 (void)filesys_open(FILESYS_CH_EMU, "logging.bin");
 for (i = 0U; i < 128U; i++){
  CHECK(filesys_write(FILESYS_CH_EMU, sector, sizeof(sector)) == sizeof(sector), "fixture sector write");
 }
 CHECK(textgui_log_get_count() == 0U, "no per-sector message spam");
 filesys_flush(FILESYS_CH_EMU);
 summary_lines = textgui_log_get_count();
 CHECK(summary_lines != 0U && textgui_log_get_suppressed() == 0U, "one completed write summary");
 filesys_flush(FILESYS_CH_EMU);
 CHECK(textgui_log_get_count() == summary_lines && textgui_log_get_suppressed() == 0U, "empty flush does not duplicate summary");
 mainui_set_log_verbosity(CU_LOG_ERROR);
 mainui_set_system_messages(FALSE);
 mainui_config_save_now();
 mainui_set_log_verbosity(CU_LOG_TRACE);
 mainui_set_system_messages(TRUE);
 mainui_config_reload_now();
 CHECK(mainui_get_log_verbosity() == CU_LOG_ERROR && textgui_log_get_level() == CU_LOG_ERROR && !textgui_log_get_enabled(), "logging preferences restored");
 mainui_set_log_verbosity(CU_LOG_INFO);
 mainui_set_system_messages(TRUE);
 return 0;
}

static int fast_flash_regression(void)
{
 cu_state_cpu_t* cpu = cu_avr_get_state();
 auint mode, i;
 CHECK(mainui_get_fast_flash(), "FAST FLASH defaults on");
 mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
 for (mode = 2U; mode <= 4U; mode += 2U){
  memset(cpu, 0, sizeof(*cpu));
  cpu->fuse[1] = 1U;
  cpu->crom[0] = 0xE8U; cpu->crom[1] = 0x95U; /* SPM */
  cpu->crom[2] = 0xFFU; cpu->crom[3] = 0xCFU; /* RJMP . */
  memset(cpu->sbuf, 0x5AU, sizeof(cpu->sbuf));
  memset(cpu->crom + 0x200U, mode == 2U ? 0U : 0xFFU, 256U);
  cu_avr_crom_update(0U, sizeof(cpu->crom));
  cu_avr_reset();
  CHECK(!cu_avr_flash_active(FALSE), "ROM load/reset is not flash activity");
  cpu->iors[31] = 2U;
  cpu->iors[CU_IO_SPMCSR] = mode | 1U;
  cpu->spm_mode = mode;
  cpu->spm_end = cpu->cycle + 4U;
  cu_avr_io_update();
  (void)cu_avr_run();
  CHECK(cpu->spm_prge && cu_avr_flash_active(FALSE), "real SPM operation detected");
  CHECK(cpu->crom[0x200U] == (mode == 2U ? 0xFFU : 0x5AU), "SPM changed target page");
  for (i = 0U; i < 100U && cpu->spm_prge; i++){ (void)cu_avr_run(); }
  CHECK(!cpu->spm_prge && cu_avr_flash_active(FALSE), "completed operation remains latched across a frame");
  main_acceleration_tick(100U);
  CHECK(main_acceleration_active, "flash enables acceleration");
  main_acceleration_tick(101U);
  CHECK(!main_acceleration_active, "idle frame stops acceleration");
 }
 cpu->spm_prge = TRUE;
 mainui_set_fast_flash(FALSE);
 main_acceleration_tick(102U);
 CHECK(!main_acceleration_active && main_cfg_dirty, "checkbox disables acceleration and marks preference modified");
 mainui_set_fast_flash(TRUE);
 main_ispause = TRUE;
 main_acceleration_tick(103U);
 CHECK(!main_acceleration_active, "pause prevents acceleration");
 main_ispause = FALSE;
 main_np_session_active = TRUE;
 main_acceleration_tick(104U);
 CHECK(!main_acceleration_active, "netplay session prevents acceleration");
 main_np_session_active = FALSE;
 mainui_set_serial_route(CU_ESP_SERIAL_HOST_SERIAL);
 main_acceleration_tick(104U);
 CHECK(!main_acceleration_active, "external serial prevents acceleration");
 mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
#ifdef ENABLE_VCAP
 main_isvcap = TRUE;
 main_acceleration_tick(105U);
 CHECK(!main_acceleration_active, "video recording prevents acceleration");
 main_isvcap = FALSE;
#endif
 cu_avr_reset();
 main_acceleration_tick(106U);
 CHECK(!main_acceleration_active && !cu_avr_flash_active(FALSE), "reset clears flash activity");
 mainui_set_fast_flash(FALSE);
 mainui_config_save_now();
 mainui_set_fast_flash(TRUE);
 mainui_config_reload_now();
 CHECK(!mainui_get_fast_flash(), "FAST FLASH GUI preference reloads");
 return 0;
}

int main(void)
{
 app_config_t saved;
 netplay_config_t np;
 FILE* legacy;
 CHECK(roundtrip() == 0, "all serialized preference fields");
 legacy = fopen("legacy.cfg", "w");
 CHECK(legacy != NULL, "legacy fixture");
 fputs("AudioMasterVolume=72\nGuiGamepadSpeed=999\n", legacy);
 fclose(legacy);
 CHECK(appcfg_load_or_create(&saved, "legacy.cfg"), "old config loads with defaults");
 CHECK(saved.audio_master_volume == 72U && saved.netplay_max_players == 2U, "legacy settings and new defaults");
 CHECK(saved.fast_flash, "older configs default FAST FLASH on");
 CHECK(saved.video_dump_omit_messages,"older configs default video OMIT MESSAGES on");
 CHECK(!saved.tv_sync_pll, "older configs default TV sync PLL off");
 CHECK(saved.tv_sync_recovery == 100U, "legacy PLL recovery default");
 CHECK(saved.tv_sync_strength == 100U, "legacy PLL strength default");
 CHECK(saved.resident_bootloader_enable, "older configs default resident bootloader on");
 CHECK(saved.log_verbosity == CU_LOG_INFO, "older configs default to quiet INFO logging");
 CHECK(saved.gui_gamepad_speed_pct == 200U, "loaded GUI speed is clamped");
 SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
 SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
 SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
 appcfg_defaults(&main_cfg);
 main_cfg.api_server = FALSE;
 main_cfg.frame_rate_limiter = FALSE;
 netplay_init();
 remote_roms_init();
 savestate_init();
 CHECK(guicore_init(GUICORE_GAMEONLY | GUICORE_NOVSYNC, "Preference tests"), "dummy display");
 CHECK(mui_init() != 0, "GUI init");
 (void)audio_init();
 (void)ginput_init();
 main_cfg_apply_runtime();
#ifdef ENABLE_API_SERVER
 mainui_set_api_server_enable(TRUE);
 CHECK(mainui_get_api_server_enable() && web_server_is_enabled() && main_cfg_dirty, "GUI API checkbox enables both servers and marks preference modified");
 mainui_config_save_now();
 mainui_set_api_server_enable(FALSE);
 CHECK(!mainui_get_api_server_enable() && !web_server_is_enabled(), "GUI API checkbox shuts down API and web servers");
 mainui_config_reload_now();
 CHECK(mainui_get_api_server_enable() && web_server_is_enabled(), "GUI API checkbox preference reloads");
 mainui_set_api_server_enable(FALSE);
#endif
 CHECK(fast_flash_regression() == 0, "FAST FLASH instructions, pacing policy and preferences");
 CHECK(logging_regression() == 0, "logging verbosity, throttling, file summaries and persistence");
 CHECK(main_cfg.audio_freqscale && !main_audio_freqscale, "limiter does not overwrite requested audio scaling");
 mainui_set_audio_output_rate(AUDIO_OUTRATE_96000);
 mainui_set_audio_latency(AUDIO_LATENCY_SAFE);
 mainui_set_audio_s16(FALSE);
 mainui_set_audio_dcblock(FALSE);
 mainui_set_audio_resampler(AUDIO_RESAMPLER_CUBIC);
 mainui_set_audio_lowpass(AUDIO_LOWPASS_STRONG);
 mainui_set_audio_lowpass_quality(AUDIO_LOWPASS_QUALITY_SIMPLE);
 mainui_set_audio_monitor_mode(AUDIO_MONITOR_STEREO);
 mainui_set_audio_monitor_width(175U);
 mainui_set_audio_reverb(AUDIO_REVERB_STRONG);
 mainui_set_audio_master_volume(137U);
 mainui_apply_gui_theme_preset("CHILL");
 mainui_set_gui_theme_file("themes/my theme.cfg");
 mainui_set_display_gameonly(FALSE);
 CHECK(main_runtime_apply_pending, "display edit is deferred");
 mainui_config_save_now();
 CHECK(appcfg_load_or_create(&saved, "config.cfg"), "read saved GUI preferences");
 CHECK(!saved.display_gameonly && saved.audio_freqscale, "save retains pending display and requested audio scaling");
 CHECK(saved.audio_output_rate == AUDIO_OUTRATE_96000 && saved.audio_latency == AUDIO_LATENCY_SAFE, "audio device preferences");
 CHECK(!saved.audio_s16 && !saved.audio_dcblock && saved.audio_resampler == AUDIO_RESAMPLER_CUBIC, "audio format and resampler");
 CHECK(saved.audio_lowpass == AUDIO_LOWPASS_STRONG && saved.audio_lowpass_quality == AUDIO_LOWPASS_QUALITY_SIMPLE, "low-pass preferences");
 CHECK(saved.audio_monitor_mode == AUDIO_MONITOR_STEREO && saved.audio_monitor_width == 175U && saved.audio_reverb == AUDIO_REVERB_STRONG && saved.audio_master_volume == 137U, "monitor preferences");
 CHECK(strcmp(saved.gui_theme_name, "CHILL") == 0 && strcmp(saved.gui_theme_file, "themes/my theme.cfg") == 0, "theme identity");
 main_cfg_apply_runtime_pending();
 mainui_config_reload_now();
 CHECK(mainui_get_audio_monitor_width() == 175U && audio_monitor_width_get() == 175U && audio_reverb_get() == AUDIO_REVERB_STRONG, "audio restored to runtime");
 CHECK(strcmp(mainui_get_gui_theme_name(), "CHILL") == 0, "theme restored");
 main_cfg_dirty = FALSE;
 mainui_set_netplay_input_delay(5U);
 CHECK(main_cfg_dirty, "input delay marks preferences modified");
 main_cfg_dirty = FALSE;
 mainui_set_netplay_rollback_window(13U);
 CHECK(main_cfg_dirty, "rollback window marks preferences modified");
 netplay_get_config(&np);
 np.max_players = 4U; np.local_player_mask = 5U;
 np.rom_send_enabled = FALSE; np.rom_receive_enabled = FALSE;
 np.rom_sync_mode = NETPLAY_ROM_SYNC_ALWAYS; np.rom_tcp_port = 43812U; np.rom_max_size = 8U * 1024U * 1024U;
 strcpy(np.local_name, "Test Player"); strcpy(np.local_pad_labels[7], "Last Pad");
 netplay_set_config(&np);
 mainui_touch_config();
 mainui_config_save_now();
 mainui_config_defaults_now();
 mainui_config_reload_now();
 netplay_get_config(&np);
 CHECK(np.max_players == 4U && np.local_player_mask == 5U && np.rom_tcp_port == 43812U, "netplay player and port preferences restored");
 CHECK(!np.rom_send_enabled && !np.rom_receive_enabled && np.rom_sync_mode == NETPLAY_ROM_SYNC_ALWAYS && np.rom_max_size == 8U * 1024U * 1024U, "netplay transfer preferences restored");
 CHECK(strcmp(np.local_name, "Test Player") == 0 && strcmp(np.local_pad_labels[7], "Last Pad") == 0, "netplay identity restored");
 mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
 mainui_set_serial_esp_model(2U);
 mainui_set_esp_at_firmware_profile(CU_ESP_AT_FW_ESPAT_230);
 mainui_set_host_serial_device_name("COM19");
 mainui_set_host_midi_port_name("Preference MIDI");
 mainui_set_virtual_midi_port_name("Preference virtual MIDI");
 mainui_set_virtual_midi_mode(CU_ESP_VIRTUAL_MIDI_CONTROLLER);
 mainui_set_tcp_serial_host("192.0.2.10");
 mainui_set_tcp_serial_port(32123U);
 mainui_set_tcp_serial_auto_reconnect(TRUE);
 mainui_set_tcp_serial_mode(CU_ESP_TCP_SERIAL_MODE_LAN);
 mainui_config_save_now();
 mainui_set_host_serial_device_name("COM1");
 mainui_set_tcp_serial_port(12001U);
 mainui_config_reload_now();
 CHECK(mainui_get_serial_route() == CU_ESP_SERIAL_DISCONNECTED && mainui_get_serial_esp_model() == 2U && mainui_get_esp_at_firmware_profile() == CU_ESP_AT_FW_ESPAT_230, "serial route, model and firmware restored");
 CHECK(strcmp(mainui_get_host_serial_device_name(), "COM19") == 0 && strcmp(mainui_get_host_midi_port_name(), "Preference MIDI") == 0, "host serial and MIDI restored");
 CHECK(strcmp(mainui_get_virtual_midi_port_name(), "Preference virtual MIDI") == 0 && mainui_get_virtual_midi_mode() == CU_ESP_VIRTUAL_MIDI_CONTROLLER, "virtual MIDI restored");
 CHECK(strcmp(mainui_get_tcp_serial_host(), "192.0.2.10") == 0 && mainui_get_tcp_serial_port() == 32123U && mainui_get_tcp_serial_auto_reconnect() && mainui_get_tcp_serial_mode() == CU_ESP_TCP_SERIAL_MODE_LAN, "TCP serial restored");
#ifdef _WIN32
 {
  HANDLE locked = CreateFileA("config.cfg", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  CHECK(locked != INVALID_HANDLE_VALUE, "lock GUI config");
  main_cfg_dirty = FALSE;
  mainui_config_save_now();
  CHECK(main_cfg_dirty, "failed GUI save remains modified");
  main_cfg_dirty = FALSE;
  main_cfg_save_immediate();
  CHECK(main_cfg_dirty, "failed immediate save remains modified");
  cu_esp_save_config();
  CloseHandle(locked);
  CHECK(appcfg_load_or_create(&saved, "config.cfg") && saved.audio_master_volume == 137U, "ESP replacement failure preserves application settings");
 }
#endif
 ginput_quit(); audio_quit(); mui_shutdown(); guicore_quit();
 netplay_shutdown(); remote_roms_shutdown(); savestate_shutdown();
 puts("PASS: all config fields including screenshot GUI preference, GUI save/reload, audio, themes, netplay, serial/MIDI, FAST FLASH, logging, legacy defaults, failed replacement");
 return 0;
}
