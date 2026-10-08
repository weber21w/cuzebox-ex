#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "configcfg.h"
#include "audio.h"
#include "cu_esp.h"
#include "api_server.h"
#include "configfile.h"

static const char* appcfg_gui_color_names[APPCFG_GUI_COLOR_COUNT] = {
	"Text",
	"Border",
	"WindowBg",
	"TitleBg",
	"TitleText",
	"PanelBg",
	"Button",
	"ButtonHover",
	"ButtonFocus",
	"Base",
	"BaseHover",
	"BaseFocus",
	"ScrollBase",
	"ScrollThumb"
};

static void appcfg_trim(char* s)
{
	char* end;
	while ((*s != 0) && isspace((unsigned char)*s)){
		memmove(s, s + 1, strlen(s));
	}
	end = s + strlen(s);
	while ((end > s) && isspace((unsigned char)end[-1])){
		end--;
	}
	*end = 0;
}

static boole appcfg_parse_bool(const char* s, boole defval)
{
	if ((strcmp(s, "1") == 0) || (strcmp(s, "true") == 0) || (strcmp(s, "TRUE") == 0) ||
	    (strcmp(s, "yes") == 0) || (strcmp(s, "on") == 0)){
		return TRUE;
	}
	if ((strcmp(s, "0") == 0) || (strcmp(s, "false") == 0) || (strcmp(s, "FALSE") == 0) ||
	    (strcmp(s, "no") == 0) || (strcmp(s, "off") == 0)){
		return FALSE;
	}
	return defval;
}

static void appcfg_copy_str(char* dst, auint cap, const char* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

static void appcfg_set_color(uint8 dst[4], auint r, auint g, auint b, auint a)
{
	dst[0] = (uint8)(r & 0xFFU);
	dst[1] = (uint8)(g & 0xFFU);
	dst[2] = (uint8)(b & 0xFFU);
	dst[3] = (uint8)(a & 0xFFU);
}

static boole appcfg_parse_color(const char* s, uint8 dst[4])
{
	char local[128];
	char* tok;
	char* save = NULL;
	unsigned long vals[4];
	auint i;
	if ((s == NULL) || (dst == NULL)){ return FALSE; }
	strncpy(local, s, sizeof(local) - 1U);
	local[sizeof(local) - 1U] = 0;
	for (i = 0U; i < 4U; i++){
		tok = strtok_r((i == 0U) ? local : NULL, ",", &save);
		if (tok == NULL){ return FALSE; }
		appcfg_trim(tok);
		vals[i] = strtoul(tok, NULL, 0);
		if (vals[i] > 255UL){ vals[i] = 255UL; }
	}
	appcfg_set_color(dst, vals[0], vals[1], vals[2], vals[3]);
	return TRUE;
}

static void appcfg_save_color(FILE* f, const char* key, uint8 const src[4])
{
	if ((f == NULL) || (key == NULL) || (src == NULL)){ return; }
	fprintf(f, "%s=%u,%u,%u,%u\n", key,
		(unsigned)src[0],
		(unsigned)src[1],
		(unsigned)src[2],
		(unsigned)src[3]);
}

void appcfg_defaults(app_config_t* cfg)
{
	auint i;
	if (cfg == NULL){ return; }
	memset(cfg, 0, sizeof(*cfg));
	cfg->version = APPCFG_VERSION;
	cfg->display_gameonly = TRUE;
#ifdef FLAG_DISPLAY_FRAMEMERGE
	cfg->frame_merge = TRUE;
#else
	cfg->frame_merge = FALSE;
#endif
	cfg->render_path = RENDER_PATH_STAGED;
	cfg->display_fullscreen = FALSE;
	cfg->display_system_messages = TRUE;
	cfg->log_verbosity = CU_LOG_INFO;
	cfg->gui_gamepad_enable = TRUE;
	cfg->gui_gamepad_speed_pct = 50U;
	cfg->gui_pause_while_open = TRUE;
	cfg->frame_rate_limiter = TRUE;
#ifdef ENABLE_GUI_VKEYBOARD
 #if FLAG_GUI_VKEYBOARD_DEFAULT
	cfg->gui_virtual_keyboard = TRUE;
 #else
	cfg->gui_virtual_keyboard = FALSE;
 #endif
#else
	cfg->gui_virtual_keyboard = FALSE;
#endif
	cfg->input_kbuzem = FALSE;
	cfg->input_player2alloc = FALSE;
	cfg->audio_freqscale = TRUE;
	cfg->audio_s16 = TRUE;
	cfg->audio_output_rate = AUDIO_OUTRATE_48000;
	cfg->audio_latency = AUDIO_LATENCY_NORMAL;
	cfg->audio_resampler = AUDIO_RESAMPLER_LINEAR;
	cfg->audio_dcblock = TRUE;
	cfg->audio_lowpass = AUDIO_LOWPASS_LIGHT;
	cfg->audio_lowpass_quality = AUDIO_LOWPASS_QUALITY_HQ;
	cfg->audio_monitor_mode = AUDIO_MONITOR_MONO;
	cfg->audio_monitor_width = 100U;
	cfg->audio_reverb = AUDIO_REVERB_OFF;
	cfg->audio_master_volume = 100U;
	cfg->mouse_enable = FALSE;
	cfg->mouse_scale = 0U;
	cfg->filter_pre_mode = FILTER_PRE_NONE;
	cfg->filter_scale_mode = FILTER_SCALE_NONE;
	cfg->filter_crt_mode = FILTER_CRT_NONE;
	cfg->spiram_pages_mode = 0U;
	cfg->sd_allow_new_files = FALSE;
	/* NORMAL is exactly the original cu_spisd.c hardcoded timing model. */
	cfg->sd_timing_preset = 2U;
	cfg->sd_init_ms = 500U;
	cfg->sd_cmd_wait_bytes = 0U;
	cfg->sd_read_wait_bytes = 2U;
	cfg->sd_write_busy_ms = 100U;
	cfg->sd_cs_high_ms = 0U;
	cfg->sd_init_min_byte_cycles = 16U;
	cfg->sd_init_max_byte_cycles = 287U * 8U;
	cfg->esp_softap_mode = 0U;
	cfg->netplay_rollback_window = 12U;
	cfg->netplay_input_delay = 1U;
	cfg->netplay_max_players = 2U;
	cfg->netplay_local_player_mask = 1U;
	cfg->netplay_rom_send = TRUE;
	cfg->netplay_rom_receive = TRUE;
	cfg->netplay_rom_sync_mode = NETPLAY_ROM_SYNC_MISMATCH;
	cfg->netplay_rom_tcp_port = 0U;
	cfg->netplay_rom_max_size = 4U * 1024U * 1024U;
	cfg->netplay_relay_port = 43810U;
	appcfg_copy_str(cfg->netplay_relay_host, APPCFG_PATH_MAX, "uzenet.us");
	appcfg_copy_str(cfg->netplay_network_interface, APPCFG_PATH_MAX, "");
	appcfg_copy_str(cfg->remote_roms_host, APPCFG_PATH_MAX, "uzenet.us");
	appcfg_copy_str(cfg->video_dump_file, APPCFG_PATH_MAX, "video/CAPTURE001.MP4");
	cfg->video_dump_autoinc = TRUE;
	cfg->video_dump_reset_first = FALSE;
	appcfg_copy_str(cfg->gui_theme_name, sizeof(cfg->gui_theme_name), "BOOTLOADER");
	appcfg_copy_str(cfg->gui_theme_file, sizeof(cfg->gui_theme_file), "themes/bootloader.cfg");
	appcfg_set_color(cfg->gui_colors[0], 255U, 232U, 196U, 255U);
	appcfg_set_color(cfg->gui_colors[1], 52U, 24U, 0U, 255U);
	appcfg_set_color(cfg->gui_colors[2], 86U, 44U, 8U, 255U);
	appcfg_set_color(cfg->gui_colors[3], 68U, 28U, 0U, 255U);
	appcfg_set_color(cfg->gui_colors[4], 255U, 244U, 220U, 255U);
	appcfg_set_color(cfg->gui_colors[5], 0U, 0U, 0U, 0U);
	appcfg_set_color(cfg->gui_colors[6], 132U, 72U, 16U, 255U);
	appcfg_set_color(cfg->gui_colors[7], 160U, 96U, 28U, 255U);
	appcfg_set_color(cfg->gui_colors[8], 188U, 124U, 48U, 255U);
	appcfg_set_color(cfg->gui_colors[9], 58U, 32U, 6U, 255U);
	appcfg_set_color(cfg->gui_colors[10], 70U, 38U, 10U, 255U);
	appcfg_set_color(cfg->gui_colors[11], 84U, 46U, 16U, 255U);
	appcfg_set_color(cfg->gui_colors[12], 98U, 58U, 18U, 255U);
	appcfg_set_color(cfg->gui_colors[13], 70U, 38U, 10U, 255U);
	appcfg_copy_str(cfg->rom_path, APPCFG_PATH_MAX, "roms");
	appcfg_copy_str(cfg->screenshot_path, APPCFG_PATH_MAX, "screenshots");
	appcfg_copy_str(cfg->save_path, APPCFG_PATH_MAX, "savestates");
	appcfg_copy_str(cfg->controllerdb_path, APPCFG_PATH_MAX, "");
	cfg->resident_bootloader_enable = FALSE;
	cfg->fast_flash = TRUE;
	appcfg_copy_str(cfg->resident_bootloader_file, APPCFG_PATH_MAX, "Bootloader.hex");
	for (i = 0U; i < APPCFG_RECENT_ROMS; i++){
		cfg->recent_roms[i][0] = 0;
	}
	cfg->recent_roms_frozen = FALSE;
	cfg->api_server = TRUE;
	cfg->api_server_port = API_SERVER_DEFAULT_PORT;
	cfg->cheats_enabled = TRUE;
	cfg->cheats_autoloadsave = TRUE;
	for (i = 0U; i < CU_MULTITAP_MAX_PORTS; i++){
		cfg->tap_present[i] = FALSE;
		cfg->tap_active_slot[i] = 0U;
		memset(cfg->tap_slot_vdev[i], 0xFF, sizeof(cfg->tap_slot_vdev[i]));
	}
	if (cfg->gui_gamepad_speed_pct < 10U){ cfg->gui_gamepad_speed_pct = 10U; }
	if (cfg->gui_gamepad_speed_pct > 200U){ cfg->gui_gamepad_speed_pct = 200U; }
	for (i = 0U; i < CU_VDEV_MAX; i++){
		auint b;
		cfg->vdev[i].used = FALSE;
		cfg->vdev[i].name[0] = 0;
		cfg->vdev[i].type = CU_VDEV_TYPE_NONE;
		cfg->vdev[i].options = 0U;
		cfg->vdev[i].haptic_enabled = FALSE;
		cfg->vdev[i].haptic_accept_any = TRUE;
		cfg->vdev[i].haptic_binding = CU_VDEV_INVALID;
		cfg->vdev[i].haptic_id = 0U;
		cfg->vdev[i].low16 = 0U;
		cfg->vdev[i].high16 = 0U;
		cfg->vdev[i].sm_scale_x_pct = 100U;
		cfg->vdev[i].sm_scale_y_pct = 100U;
		cfg->vdev[i].sm_deadzone = 0U;
		cfg->vdev[i].sm_invert_x = FALSE;
		cfg->vdev[i].sm_invert_y = FALSE;
		for (b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			cfg->vdev[i].binding[b].type = CU_VDEV_HOST_NONE;
			cfg->vdev[i].binding[b].host_index = 0U;
			cfg->vdev[i].binding[b].flags = 0U;
			memset(cfg->vdev[i].binding[b].map_low16, 0, sizeof(cfg->vdev[i].binding[b].map_low16));
		}
	}
}

boole appcfg_save(app_config_t const* cfg, const char* path)
{
	FILE* f;
	char tmp_path[512];
	auint i;
	char key[64];
	boole esp_written = FALSE;

	if ((cfg == NULL) || (path == NULL)){ return FALSE; }
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	f = fopen(tmp_path, "w");
	if (f == NULL){ return FALSE; }

	fprintf(f, "# CUzeBox user config\n");
	fprintf(f, "# Missing entries fall back to built-in defaults.\n");
	fprintf(f, "# Booleans accept 0/1, false/true, off/on, or no/yes.\n\n");

	fprintf(f, "# -----------------------------------------------------------------------------\n");
	fprintf(f, "# Core emulator\n");
	fprintf(f, "# -----------------------------------------------------------------------------\n");
	fprintf(f, "Version=%u\n\n", (unsigned)cfg->version);

	fprintf(f, "# Display\n");
		fprintf(f, "DisplayGameOnly=%u\n", cfg->display_gameonly ? 1U : 0U);
	fprintf(f, "DisplayFullscreen=%u\n", cfg->display_fullscreen ? 1U : 0U);
	fprintf(f, "SystemMessages=%u\n", cfg->display_system_messages ? 1U : 0U);
	fprintf(f, "LogVerbosity=%u\n", cfg->log_verbosity);
	fprintf(f, "GuiGamepad=%u\n", cfg->gui_gamepad_enable ? 1U : 0U);
	fprintf(f, "# GuiGamepadSpeed: host GUI cursor speed percentage\n");
	fprintf(f, "GuiGamepadSpeed=%u\n", (unsigned)cfg->gui_gamepad_speed_pct);
	fprintf(f, "GuiPauseWhileOpen=%u\n", cfg->gui_pause_while_open ? 1U : 0U);
	fprintf(f, "GuiVirtualKeyboard=%u\n", cfg->gui_virtual_keyboard ? 1U : 0U);
	fprintf(f, "FrameRateLimiter=%u\n", cfg->frame_rate_limiter ? 1U : 0U);
	fprintf(f, "FrameMerge=%u\n", cfg->frame_merge ? 1U : 0U);
	fprintf(f, "# RenderPath: 0=potato, 1=classic1x, 2=classic2x, 3=staged\n");
	fprintf(f, "RenderPath=%u\n\n", (unsigned)cfg->render_path);

	fprintf(f, "# Input\n");
	fprintf(f, "InputKbUzEm=%u\n", cfg->input_kbuzem ? 1U : 0U);
	fprintf(f, "InputPlayer2Alloc=%u\n\n", cfg->input_player2alloc ? 1U : 0U);

	fprintf(f, "# Audio\n");
	fprintf(f, "# AudioOutputRate: 0=44100, 1=48000, 2=96000\n");
	fprintf(f, "# AudioLatency: 0=low, 1=normal, 2=safe\n");
	fprintf(f, "# AudioResampler: 0=hold, 1=linear, 2=cubic\n");
	fprintf(f, "# AudioLowPass: 0=off, 1=light, 2=medium, 3=strong\n");
	fprintf(f, "# AudioLowPassQuality: 0=simple, 1=hq\n");
	fprintf(f, "# AudioMonitorMode: 0=mono, 1=stereo, 2=wide\n");
	fprintf(f, "# AudioReverb: 0=off, 1=light, 2=medium, 3=strong\n");
	fprintf(f, "# AudioMonitorWidth and AudioMasterVolume are percentages.\n");
	fprintf(f, "AudioFreqScale=%u\n", cfg->audio_freqscale ? 1U : 0U);
	fprintf(f, "AudioS16=%u\n", cfg->audio_s16 ? 1U : 0U);
	fprintf(f, "AudioOutputRate=%u\n", (unsigned)cfg->audio_output_rate);
	fprintf(f, "AudioLatency=%u\n", (unsigned)cfg->audio_latency);
	fprintf(f, "AudioResampler=%u\n", (unsigned)cfg->audio_resampler);
	fprintf(f, "AudioDcBlock=%u\n", cfg->audio_dcblock ? 1U : 0U);
	fprintf(f, "AudioLowPass=%u\n", (unsigned)cfg->audio_lowpass);
	fprintf(f, "AudioLowPassQuality=%u\n", (unsigned)cfg->audio_lowpass_quality);
	fprintf(f, "AudioMonitorMode=%u\n", (unsigned)cfg->audio_monitor_mode);
	fprintf(f, "AudioMonitorWidth=%u\n", (unsigned)cfg->audio_monitor_width);
	fprintf(f, "AudioReverb=%u\n", (unsigned)cfg->audio_reverb);
	fprintf(f, "AudioMasterVolume=%u\n\n", (unsigned)cfg->audio_master_volume);

	fprintf(f, "# Mouse / filters\n");
	fprintf(f, "# MouseScale: 0..5\n");
	fprintf(f, "# Filters only apply in RenderPath=3 (staged).\n");
	fprintf(f, "# FilterPreMode: 0=none, 1=composite\n");
	fprintf(f, "# FilterScaleMode: 0=nearest, 1=scale2x, 2=hq2x, 3=xbr2x\n");
	fprintf(f, "# FilterCrtMode: 0=none, 1=scan25, 2=scan37, 3=scan50, 4=grille,\n");
	fprintf(f, "#                5=shadowmask, 6=curvature, 9=grille+scanlines, 10=mask+scanlines,\n");
	fprintf(f, "#                11=bumpmap, 12=lucid, 13=heatwave\n");
	fprintf(f, "MouseEnable=%u\n", cfg->mouse_enable ? 1U : 0U);
	fprintf(f, "MouseScale=%u\n", (unsigned)cfg->mouse_scale);
	fprintf(f, "FilterPreMode=%u\n", (unsigned)cfg->filter_pre_mode);
	fprintf(f, "FilterScaleMode=%u\n", (unsigned)cfg->filter_scale_mode);
	fprintf(f, "FilterCrtMode=%u\n", (unsigned)cfg->filter_crt_mode);
	fprintf(f, "# SpiRamPagesMode: 0=.UZE hint, 1=disable, 2=1 page, 3=2 pages, 4=4 pages, 5=8 pages, 6=16 pages, 7=32 pages, 8=64 pages, 9=128 pages\n");
	fprintf(f, "SpiRamPagesMode=%u\n", (unsigned)cfg->spiram_pages_mode);
	fprintf(f, "# SdAllowNewFiles: permit trusted emulated SD software to create/resize/rename/delete host files and folders\n");
	fprintf(f, "SdAllowNewFiles=%u\n", cfg->sd_allow_new_files ? 1U : 0U);
	fprintf(f, "# SD timing model: 0=custom, 1=slow, 2=normal (original CUzeBox constants), 3=fast\n");
	fprintf(f, "SdTimingPreset=%u\n", (unsigned)cfg->sd_timing_preset);
	fprintf(f, "SdInitMs=%u\n", (unsigned)cfg->sd_init_ms);
	fprintf(f, "SdCommandWaitBytes=%u\n", (unsigned)cfg->sd_cmd_wait_bytes);
	fprintf(f, "SdReadWaitBytes=%u\n", (unsigned)cfg->sd_read_wait_bytes);
	fprintf(f, "SdWriteBusyMs=%u\n", (unsigned)cfg->sd_write_busy_ms);
	fprintf(f, "SdCsHighMs=%u\n", (unsigned)cfg->sd_cs_high_ms);
	fprintf(f, "SdInitMinByteCycles=%u\n", (unsigned)cfg->sd_init_min_byte_cycles);
	fprintf(f, "SdInitMaxByteCycles=%u\n", (unsigned)cfg->sd_init_max_byte_cycles);
	fprintf(f, "# EspSoftApMode: 0=.UZE hint, 1=disable, 2=enable\n");
	fprintf(f, "EspSoftApMode=%u\n", (unsigned)cfg->esp_softap_mode);
	fprintf(f, "# NetplayRollbackWindow: predicted frames allowed before stall\n");
	fprintf(f, "# NetplayInputDelay: fixed local input delay in frames\n");
	fprintf(f, "NetplayRollbackWindow=%u\n", (unsigned)cfg->netplay_rollback_window);
	fprintf(f, "NetplayInputDelay=%u\n", (unsigned)cfg->netplay_input_delay);
	fprintf(f, "NetplayMaxPlayers=%u\n", (unsigned)cfg->netplay_max_players);
	fprintf(f, "NetplayLocalPlayerMask=%u\n", (unsigned)cfg->netplay_local_player_mask);
	fprintf(f, "NetplayRomSend=%u\n", (unsigned)(cfg->netplay_rom_send ? 1U : 0U));
	fprintf(f, "NetplayRomReceive=%u\n", (unsigned)(cfg->netplay_rom_receive ? 1U : 0U));
	fprintf(f, "NetplayRomSyncMode=%u\n", (unsigned)cfg->netplay_rom_sync_mode);
	fprintf(f, "NetplayRomTcpPort=%u\n", (unsigned)cfg->netplay_rom_tcp_port);
	fprintf(f, "NetplayRomMaxSize=%u\n", (unsigned)cfg->netplay_rom_max_size);
	fprintf(f, "NetplayName=%s\n", cfg->netplay_name);
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		fprintf(f, "NetplayPadLabel%u=%s\n", (unsigned)i, cfg->netplay_pad_labels[i]);
	}
	fprintf(f, "# NetplayNetworkInterface: blank=auto route, otherwise bind to a specific local IPv4/IPv6 address\n");
	fprintf(f, "NetplayRelayHost=%s\n", cfg->netplay_relay_host);
	fprintf(f, "NetplayNetworkInterface=%s\n", cfg->netplay_network_interface);
	fprintf(f, "NetplayRelayPort=%u\n", (unsigned)cfg->netplay_relay_port);
	fprintf(f, "RemoteRomsHost=%s\n", cfg->remote_roms_host);
	fprintf(f, "VideoDumpFile=%s\n", cfg->video_dump_file);
	fprintf(f, "VideoDumpAutoInc=%u\n", (unsigned)(cfg->video_dump_autoinc ? 1U : 0U));
	fprintf(f, "VideoDumpResetFirst=%u\n\n", (unsigned)(cfg->video_dump_reset_first ? 1U : 0U));

	fprintf(f, "# Bootloader / flash persistence\n");
	fprintf(f, "# ResidentBootloader: use the compiled-in Bootloader 5 image and AVR BOOTRST.\n");
	fprintf(f, "# ResidentBootloaderFile is an optional 4 KiB boot-section override.\n");
	fprintf(f, "# If the override is absent, the compiled-in image is used.\n");
	fprintf(f, "ResidentBootloader=%u\n", cfg->resident_bootloader_enable ? 1U : 0U);
	fprintf(f, "FastFlash=%u\n", cfg->fast_flash ? 1U : 0U);
	fprintf(f, "ResidentBootloaderFile=%s\n\n", cfg->resident_bootloader_file);

	fprintf(f, "# GUI appearance\n");
	fprintf(f, "GuiThemeName=%s\n", cfg->gui_theme_name);
	fprintf(f, "GuiThemeFile=%s\n", cfg->gui_theme_file);
	fprintf(f, "# GUI colors are RGBA bytes.\n");
	fprintf(f, "# Padding, spacing, scrollbar size, thumb size, and title height are compile-time constants.\n");
	for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
		snprintf(key, sizeof(key), "GuiColor%s", appcfg_gui_color_names[i]);
		appcfg_save_color(f, key, cfg->gui_colors[i]);
		}
	fprintf(f, "\n");

	fprintf(f, "# Paths\n");
	fprintf(f, "RomPath=%s\n", cfg->rom_path);
	fprintf(f, "ScreenshotPath=%s\n", cfg->screenshot_path);
	fprintf(f, "SavePath=%s\n", cfg->save_path);
	fprintf(f, "ControllerDbPath=%s\n\n", cfg->controllerdb_path);

	fprintf(f, "# Recent ROMs\n");
	fprintf(f, "RecentRomFreeze=%u\n", cfg->recent_roms_frozen ? 1U : 0U);
	for (i = 0U; i < APPCFG_RECENT_ROMS; i++){
		fprintf(f, "RecentRom%u=%s\n", (unsigned)i, cfg->recent_roms[i]);
	}
	fprintf(f, "\n");

	fprintf(f, "# API server\n");
	fprintf(f, "# ApiServer is temporary default-on for development. Change to 0 before release.\n");
	fprintf(f, "ApiServer=%u\n", cfg->api_server ? 1U : 0U);
	fprintf(f, "ApiServerPort=%u\n\n", (unsigned)cfg->api_server_port);

	fprintf(f, "# Cheats\n");
	fprintf(f, "CheatsEnabled=%u\n", cfg->cheats_enabled ? 1U : 0U);
	fprintf(f, "CheatsAutoLoadSave=%u\n", cfg->cheats_autoloadsave ? 1U : 0U);
	fprintf(f, "\n");

	fprintf(f, "# Virtual input topology\n");
	for (i = 0U; i < CU_MULTITAP_MAX_PORTS; i++){
		fprintf(f, "TapPresent%u=%u\n", (unsigned)i, cfg->tap_present[i] ? 1U : 0U);
		fprintf(f, "TapActiveSlot%u=%u\n", (unsigned)i, (unsigned)cfg->tap_active_slot[i]);
		for (auint s = 0U; s < CU_MULTITAP_MAX_SLOTS; s++){
			fprintf(f, "TapSlotVdev%u_%u=%u\n", (unsigned)i, (unsigned)s, (unsigned)cfg->tap_slot_vdev[i][s]);
		}
	}
	for (i = 0U; i < CU_VDEV_MAX; i++){
		appcfg_vdev_t const* v = &cfg->vdev[i];
		fprintf(f, "VDevUsed%u=%u\n", (unsigned)i, v->used ? 1U : 0U);
		fprintf(f, "VDevName%u=%s\n", (unsigned)i, v->name);
		fprintf(f, "VDevType%u=%u\n", (unsigned)i, (unsigned)v->type);
		fprintf(f, "VDevOptions%u=%u\n", (unsigned)i, (unsigned)v->options);
		fprintf(f, "VDevHapticEnabled%u=%u\n", (unsigned)i, v->haptic_enabled ? 1U : 0U);
		fprintf(f, "VDevHapticAcceptAny%u=%u\n", (unsigned)i, v->haptic_accept_any ? 1U : 0U);
		fprintf(f, "VDevHapticBinding%u=%u\n", (unsigned)i, (unsigned)v->haptic_binding);
		fprintf(f, "VDevHapticId%u=%u\n", (unsigned)i, (unsigned)v->haptic_id);
		fprintf(f, "VDevLow16%u=%u\n", (unsigned)i, (unsigned)v->low16);
		fprintf(f, "VDevHigh16%u=%u\n", (unsigned)i, (unsigned)v->high16);
		fprintf(f, "VDevSmScaleXPct%u=%u\n", (unsigned)i, (unsigned)v->sm_scale_x_pct);
		fprintf(f, "VDevSmScaleYPct%u=%u\n", (unsigned)i, (unsigned)v->sm_scale_y_pct);
		fprintf(f, "VDevSmDeadzone%u=%u\n", (unsigned)i, (unsigned)v->sm_deadzone);
		fprintf(f, "VDevSmInvertX%u=%u\n", (unsigned)i, v->sm_invert_x ? 1U : 0U);
		fprintf(f, "VDevSmInvertY%u=%u\n", (unsigned)i, v->sm_invert_y ? 1U : 0U);
		for (auint b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			fprintf(f, "VDevBindType%u_%u=%u\n", (unsigned)i, (unsigned)b, (unsigned)v->binding[b].type);
			fprintf(f, "VDevBindIndex%u_%u=%u\n", (unsigned)i, (unsigned)b, (unsigned)v->binding[b].host_index);
			fprintf(f, "VDevBindFlags%u_%u=%u\n", (unsigned)i, (unsigned)b, (unsigned)v->binding[b].flags);
			for (auint m = 0U; m < CU_VDEV_REMAP_SLOTS; m++){
				fprintf(f, "VDevBindMap%u_%u_%u=%u\n", (unsigned)i, (unsigned)b, (unsigned)m, (unsigned)(v->binding[b].map_low16[m] & 0xFFFFU));
			}
		}
	}
	fprintf(f, "\n");

#ifdef ENABLE_ESP
	if (cu_esp_config_live_ready()){
		esp_written = cu_esp_append_live_config(f, TRUE);
	}else if (path[0] != 0){
		esp_written = cu_esp_append_config_from_path(f, path, TRUE);
		if (!esp_written){
			esp_written = cu_esp_append_default_config(f, TRUE);
		}
	}else{
		esp_written = cu_esp_append_default_config(f, TRUE);
	}
#else
	/* A gameplay build without the ESP backend still saves core preferences. */
	esp_written = TRUE;
#endif

	if (ferror(f)){ esp_written = FALSE; }
	if (fclose(f) != 0){ esp_written = FALSE; }
	if (!esp_written){
		remove(tmp_path);
		return FALSE;
	}
	if (configfile_replace(tmp_path, path) != 0){
		remove(tmp_path);
		return FALSE;
	}
	return TRUE;
}

boole appcfg_load_or_create(app_config_t* cfg, const char* path)
{
	FILE* f;
	char  buf[512];
	char* eq;
	char* key;
	char* val;
	auint i;

	if ((cfg == NULL) || (path == NULL)){ return FALSE; }
	appcfg_defaults(cfg);
	f = fopen(path, "r");
	if (f == NULL){
		return appcfg_save(cfg, path);
	}
	while (fgets(buf, sizeof(buf), f) != NULL){
		key = buf;
		while ((*key != 0) && ((*key == ' ') || (*key == '\t'))){ key++; }
		if ((*key == '#') || (*key == ';') || (*key == '\n') || (*key == 0)){ continue; }
		eq = strchr(key, '=');
		if (eq == NULL){ continue; }
		*eq = 0;
		val = eq + 1;
		appcfg_trim(key);
		appcfg_trim(val);
		if (strcmp(key, "Version") == 0){
			cfg->version = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "DisplayGameOnly") == 0){
			cfg->display_gameonly = appcfg_parse_bool(val, cfg->display_gameonly);
		}else if (strcmp(key, "DisplayFullscreen") == 0){
			cfg->display_fullscreen = appcfg_parse_bool(val, cfg->display_fullscreen);
		}else if (strcmp(key, "SystemMessages") == 0){
			cfg->display_system_messages = appcfg_parse_bool(val, cfg->display_system_messages);
		}else if (strcmp(key, "GuiGamepad") == 0){
			cfg->gui_gamepad_enable = appcfg_parse_bool(val, cfg->gui_gamepad_enable);
		}else if (strcmp(key, "GuiGamepadSpeed") == 0){
			cfg->gui_gamepad_speed_pct = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "GuiPauseWhileOpen") == 0){
			cfg->gui_pause_while_open = appcfg_parse_bool(val, cfg->gui_pause_while_open);
		}else if (strcmp(key, "GuiVirtualKeyboard") == 0){
			cfg->gui_virtual_keyboard = appcfg_parse_bool(val, cfg->gui_virtual_keyboard);
		}else if (strcmp(key, "FrameRateLimiter") == 0){
			cfg->frame_rate_limiter = appcfg_parse_bool(val, cfg->frame_rate_limiter);
		}else if (strcmp(key, "FrameMerge") == 0){
			cfg->frame_merge = appcfg_parse_bool(val, cfg->frame_merge);
		}else if (strcmp(key, "RenderPath") == 0){
			cfg->render_path = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "InputKbUzEm") == 0){
			cfg->input_kbuzem = appcfg_parse_bool(val, cfg->input_kbuzem);
		}else if (strcmp(key, "InputPlayer2Alloc") == 0){
			cfg->input_player2alloc = appcfg_parse_bool(val, cfg->input_player2alloc);
		}else if (strcmp(key, "AudioFreqScale") == 0){
			cfg->audio_freqscale = appcfg_parse_bool(val, cfg->audio_freqscale);
		}else if (strcmp(key, "AudioS16") == 0){
			cfg->audio_s16 = appcfg_parse_bool(val, cfg->audio_s16);
		}else if (strcmp(key, "AudioOutputRate") == 0){
			cfg->audio_output_rate = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioLatency") == 0){
			cfg->audio_latency = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioResampler") == 0){
			cfg->audio_resampler = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioDcBlock") == 0){
			cfg->audio_dcblock = appcfg_parse_bool(val, cfg->audio_dcblock);
		}else if (strcmp(key, "AudioLowPass") == 0){
			cfg->audio_lowpass = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioLowPassQuality") == 0){
			cfg->audio_lowpass_quality = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioMonitorMode") == 0){
			cfg->audio_monitor_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioMonitorWidth") == 0){
			cfg->audio_monitor_width = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioReverb") == 0){
			cfg->audio_reverb = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "AudioMasterVolume") == 0){
			cfg->audio_master_volume = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "MouseEnable") == 0){
			cfg->mouse_enable = appcfg_parse_bool(val, cfg->mouse_enable);
		}else if (strcmp(key, "MouseScale") == 0){
			cfg->mouse_scale = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "FilterPreMode") == 0){
			cfg->filter_pre_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "FilterScaleMode") == 0){
			cfg->filter_scale_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "FilterCrtMode") == 0){
			cfg->filter_crt_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SpiRamPagesMode") == 0){
			cfg->spiram_pages_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdAllowNewFiles") == 0){
			cfg->sd_allow_new_files = appcfg_parse_bool(val, cfg->sd_allow_new_files);
		}else if (strcmp(key, "SdTimingPreset") == 0){
			cfg->sd_timing_preset = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdInitMs") == 0){
			cfg->sd_init_ms = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdCommandWaitBytes") == 0){
			cfg->sd_cmd_wait_bytes = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdReadWaitBytes") == 0){
			cfg->sd_read_wait_bytes = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdWriteBusyMs") == 0){
			cfg->sd_write_busy_ms = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdCsHighMs") == 0){
			cfg->sd_cs_high_ms = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdInitMinByteCycles") == 0){
			cfg->sd_init_min_byte_cycles = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "SdInitMaxByteCycles") == 0){
			cfg->sd_init_max_byte_cycles = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "EspSoftApMode") == 0){
			cfg->esp_softap_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayRollbackWindow") == 0){
			cfg->netplay_rollback_window = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayInputDelay") == 0){
			cfg->netplay_input_delay = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayMaxPlayers") == 0){
			cfg->netplay_max_players = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayLocalPlayerMask") == 0){
			cfg->netplay_local_player_mask = (uint32)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayRomSend") == 0){
			cfg->netplay_rom_send = appcfg_parse_bool(val, cfg->netplay_rom_send);
		}else if (strcmp(key, "NetplayRomReceive") == 0){
			cfg->netplay_rom_receive = appcfg_parse_bool(val, cfg->netplay_rom_receive);
		}else if (strcmp(key, "NetplayRomSyncMode") == 0){
			cfg->netplay_rom_sync_mode = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayRomTcpPort") == 0){
			cfg->netplay_rom_tcp_port = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayRomMaxSize") == 0){
			cfg->netplay_rom_max_size = (uint32)strtoul(val, NULL, 0);
		}else if (strcmp(key, "NetplayName") == 0){
			appcfg_copy_str(cfg->netplay_name, sizeof(cfg->netplay_name), val);
		}else if (strncmp(key, "NetplayPadLabel", 15) == 0){
			auint idx = (auint)strtoul(key + 15, NULL, 0);
			if (idx < ROLLBACK_MAX_PLAYERS){
				appcfg_copy_str(cfg->netplay_pad_labels[idx], sizeof(cfg->netplay_pad_labels[idx]), val);
			}
		}else if (strcmp(key, "GuiThemeName") == 0){
			appcfg_copy_str(cfg->gui_theme_name, sizeof(cfg->gui_theme_name), val);
		}else if (strcmp(key, "GuiThemeFile") == 0){
			appcfg_copy_str(cfg->gui_theme_file, sizeof(cfg->gui_theme_file), val);
		}else if (strcmp(key, "NetplayRelayHost") == 0){
			appcfg_copy_str(cfg->netplay_relay_host, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "NetplayNetworkInterface") == 0){
			appcfg_copy_str(cfg->netplay_network_interface, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "NetplayRelayPort") == 0){
			cfg->netplay_relay_port = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "RemoteRomsHost") == 0){
			appcfg_copy_str(cfg->remote_roms_host, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "VideoDumpFile") == 0){
			appcfg_copy_str(cfg->video_dump_file, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "VideoDumpAutoInc") == 0){
			cfg->video_dump_autoinc = appcfg_parse_bool(val, cfg->video_dump_autoinc);
		}else if (strcmp(key, "VideoDumpResetFirst") == 0){
			cfg->video_dump_reset_first = appcfg_parse_bool(val, cfg->video_dump_reset_first);
		}else if (strcmp(key, "GuiTitleHeight") == 0){
			/* Ignored: title height is compile-time fixed. */
		}else if (strcmp(key, "RomPath") == 0){
			appcfg_copy_str(cfg->rom_path, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "ScreenshotPath") == 0){
			appcfg_copy_str(cfg->screenshot_path, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "SavePath") == 0){
			appcfg_copy_str(cfg->save_path, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "ControllerDbPath") == 0){
			appcfg_copy_str(cfg->controllerdb_path, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "ResidentBootloader") == 0){
			cfg->resident_bootloader_enable = appcfg_parse_bool(val, cfg->resident_bootloader_enable);
		}else if (strcmp(key, "FastFlash") == 0){
			cfg->fast_flash = appcfg_parse_bool(val, cfg->fast_flash);
		}else if (strcmp(key, "LogVerbosity") == 0){
			cfg->log_verbosity = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "ResidentBootloaderFile") == 0){
			appcfg_copy_str(cfg->resident_bootloader_file, APPCFG_PATH_MAX, val);
		}else if (strcmp(key, "RecentRomFreeze") == 0){
			cfg->recent_roms_frozen = appcfg_parse_bool(val, cfg->recent_roms_frozen);
		}else if (strcmp(key, "ApiServer") == 0){
			cfg->api_server = appcfg_parse_bool(val, cfg->api_server);
		}else if (strcmp(key, "ApiServerPort") == 0){
			cfg->api_server_port = (auint)strtoul(val, NULL, 0);
		}else if (strcmp(key, "CheatsEnabled") == 0){
			cfg->cheats_enabled = appcfg_parse_bool(val, cfg->cheats_enabled);
		}else if (strcmp(key, "CheatsAutoLoadSave") == 0){
			cfg->cheats_autoloadsave = appcfg_parse_bool(val, cfg->cheats_autoloadsave);
		}else if (strncmp(key, "TapPresent", 10) == 0){
			auint port = (auint)strtoul(key + 10, NULL, 0);
			if (port < CU_MULTITAP_MAX_PORTS){
				cfg->tap_present[port] = appcfg_parse_bool(val, cfg->tap_present[port]);
			}
		}else if (strncmp(key, "TapActiveSlot", 13) == 0){
			auint port = (auint)strtoul(key + 13, NULL, 0);
			if (port < CU_MULTITAP_MAX_PORTS){
				cfg->tap_active_slot[port] = (auint)strtoul(val, NULL, 0);
			}
		}else if (strncmp(key, "TapSlotVdev", 11) == 0){
			unsigned port = 0U, slot = 0U;
			if (sscanf(key + 11, "%u_%u", &port, &slot) == 2){
				if ((port < CU_MULTITAP_MAX_PORTS) && (slot < CU_MULTITAP_MAX_SLOTS)){
					cfg->tap_slot_vdev[port][slot] = (auint)strtoul(val, NULL, 0);
				}
			}
		}else if (strncmp(key, "VDevUsed", 8) == 0){
			auint id = (auint)strtoul(key + 8, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].used = appcfg_parse_bool(val, cfg->vdev[id].used); }
		}else if (strncmp(key, "VDevName", 8) == 0){
			auint id = (auint)strtoul(key + 8, NULL, 0);
			if (id < CU_VDEV_MAX){ appcfg_copy_str(cfg->vdev[id].name, CU_VDEV_NAME_MAX, val); }
		}else if (strncmp(key, "VDevType", 8) == 0){
			auint id = (auint)strtoul(key + 8, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].type = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevOptions", 11) == 0){
			auint id = (auint)strtoul(key + 11, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].options = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevHapticEnabled", 17) == 0){
			auint id = (auint)strtoul(key + 17, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].haptic_enabled = appcfg_parse_bool(val, cfg->vdev[id].haptic_enabled); }
		}else if (strncmp(key, "VDevHapticAcceptAny", 19) == 0){
			auint id = (auint)strtoul(key + 19, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].haptic_accept_any = appcfg_parse_bool(val, cfg->vdev[id].haptic_accept_any); }
		}else if (strncmp(key, "VDevHapticBinding", 17) == 0){
			auint id = (auint)strtoul(key + 17, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].haptic_binding = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevHapticId", 12) == 0){
			auint id = (auint)strtoul(key + 12, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].haptic_id = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevLow16", 9) == 0){
			auint id = (auint)strtoul(key + 9, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].low16 = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevHigh16", 10) == 0){
			auint id = (auint)strtoul(key + 10, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].high16 = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevSmScaleXPct", 15) == 0){
			auint id = (auint)strtoul(key + 15, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].sm_scale_x_pct = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevSmScaleYPct", 15) == 0){
			auint id = (auint)strtoul(key + 15, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].sm_scale_y_pct = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevSmDeadzone", 14) == 0){
			auint id = (auint)strtoul(key + 14, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].sm_deadzone = (auint)strtoul(val, NULL, 0); }
		}else if (strncmp(key, "VDevSmInvertX", 13) == 0){
			auint id = (auint)strtoul(key + 13, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].sm_invert_x = appcfg_parse_bool(val, cfg->vdev[id].sm_invert_x); }
		}else if (strncmp(key, "VDevSmInvertY", 13) == 0){
			auint id = (auint)strtoul(key + 13, NULL, 0);
			if (id < CU_VDEV_MAX){ cfg->vdev[id].sm_invert_y = appcfg_parse_bool(val, cfg->vdev[id].sm_invert_y); }
		}else if (strncmp(key, "VDevBindType", 12) == 0){
			unsigned id = 0U, bind = 0U;
			if (sscanf(key + 12, "%u_%u", &id, &bind) == 2){
				if ((id < CU_VDEV_MAX) && (bind < CU_VDEV_MAX_BINDINGS)){ cfg->vdev[id].binding[bind].type = (auint)strtoul(val, NULL, 0); }
			}
		}else if (strncmp(key, "VDevBindIndex", 13) == 0){
			unsigned id = 0U, bind = 0U;
			if (sscanf(key + 13, "%u_%u", &id, &bind) == 2){
				if ((id < CU_VDEV_MAX) && (bind < CU_VDEV_MAX_BINDINGS)){ cfg->vdev[id].binding[bind].host_index = (auint)strtoul(val, NULL, 0); }
			}
		}else if (strncmp(key, "VDevBindFlags", 13) == 0){
			unsigned id = 0U, bind = 0U;
			if (sscanf(key + 13, "%u_%u", &id, &bind) == 2){
				if ((id < CU_VDEV_MAX) && (bind < CU_VDEV_MAX_BINDINGS)){ cfg->vdev[id].binding[bind].flags = (auint)strtoul(val, NULL, 0); }
			}
		}else if (strncmp(key, "VDevBindMap", 11) == 0){
			unsigned id = 0U, bind = 0U, map = 0U;
			if (sscanf(key + 11, "%u_%u_%u", &id, &bind, &map) == 3){
				if ((id < CU_VDEV_MAX) && (bind < CU_VDEV_MAX_BINDINGS) && (map < CU_VDEV_REMAP_SLOTS)){ cfg->vdev[id].binding[bind].map_low16[map] = (auint)strtoul(val, NULL, 0) & 0xFFFFU; }
			}
		}else if (strncmp(key, "RecentRom", 9) == 0){
			auint idx = (auint)strtoul(key + 9, NULL, 0);
			if (idx < APPCFG_RECENT_ROMS){
				appcfg_copy_str(cfg->recent_roms[idx], APPCFG_PATH_MAX, val);
			}
		}else if (strncmp(key, "GuiColor", 8) == 0){
			for (i = 0U; i < APPCFG_GUI_COLOR_COUNT; i++){
				char expect[64];
				snprintf(expect, sizeof(expect), "GuiColor%s", appcfg_gui_color_names[i]);
				if (strcmp(key, expect) == 0){
					(void)appcfg_parse_color(val, cfg->gui_colors[i]);
					break;
				}
			}
		}
	}
	fclose(f);
	cfg->version = APPCFG_VERSION;
	if (cfg->mouse_scale > 5U){ cfg->mouse_scale = 5U; }
	if (cfg->render_path > RENDER_PATH_STAGED){ cfg->render_path = RENDER_PATH_STAGED; }
	if (cfg->filter_pre_mode > FILTER_PRE_BLACK_WHITE){ cfg->filter_pre_mode = FILTER_PRE_NONE; }
	if (cfg->filter_scale_mode > FILTER_SCALE_XBR2X){ cfg->filter_scale_mode = FILTER_SCALE_NONE; }
	if ((cfg->filter_crt_mode == 7U) || (cfg->filter_crt_mode == 8U)){ cfg->filter_crt_mode = FILTER_CRT_NONE; }
	if (cfg->filter_crt_mode > FILTER_CRT_HEATWAVE){ cfg->filter_crt_mode = FILTER_CRT_NONE; }
	if (cfg->audio_output_rate > AUDIO_OUTRATE_96000){ cfg->audio_output_rate = AUDIO_OUTRATE_48000; }
	if (cfg->audio_latency > AUDIO_LATENCY_SAFE){ cfg->audio_latency = AUDIO_LATENCY_NORMAL; }
	if (cfg->audio_resampler > AUDIO_RESAMPLER_CUBIC){ cfg->audio_resampler = AUDIO_RESAMPLER_LINEAR; }
	if (cfg->audio_lowpass > AUDIO_LOWPASS_STRONG){ cfg->audio_lowpass = AUDIO_LOWPASS_LIGHT; }
	if (cfg->audio_lowpass_quality > AUDIO_LOWPASS_QUALITY_HQ){ cfg->audio_lowpass_quality = AUDIO_LOWPASS_QUALITY_HQ; }
	if (cfg->audio_monitor_mode > AUDIO_MONITOR_STEREO){ cfg->audio_monitor_mode = AUDIO_MONITOR_STEREO; }
	if (cfg->audio_monitor_width > 200U){ cfg->audio_monitor_width = 200U; }
	if (cfg->audio_reverb > AUDIO_REVERB_STRONG){ cfg->audio_reverb = AUDIO_REVERB_OFF; }
	if (cfg->audio_master_volume > 200U){ cfg->audio_master_volume = 200U; }
	if (cfg->spiram_pages_mode > 9U){ cfg->spiram_pages_mode = 0U; }
	if (cfg->esp_softap_mode > 2U){ cfg->esp_softap_mode = 0U; }
	if (cfg->netplay_rollback_window == 0U){ cfg->netplay_rollback_window = 12U; }
	if (cfg->netplay_rollback_window > 255U){ cfg->netplay_rollback_window = 255U; }
	if (cfg->netplay_input_delay > 8U){ cfg->netplay_input_delay = 8U; }
	if (cfg->netplay_max_players == 0U){ cfg->netplay_max_players = 2U; }
	if (cfg->netplay_max_players > ROLLBACK_MAX_PLAYERS){ cfg->netplay_max_players = ROLLBACK_MAX_PLAYERS; }
	cfg->netplay_local_player_mask &= (1UL << cfg->netplay_max_players) - 1UL;
	if (cfg->netplay_local_player_mask == 0U){ cfg->netplay_local_player_mask = 1U; }
	if (cfg->netplay_rom_sync_mode > NETPLAY_ROM_SYNC_ALWAYS){ cfg->netplay_rom_sync_mode = NETPLAY_ROM_SYNC_MISMATCH; }
	if (cfg->netplay_rom_tcp_port > 65535U){ cfg->netplay_rom_tcp_port = 0U; }
	if (cfg->netplay_rom_max_size == 0U){ cfg->netplay_rom_max_size = 4U * 1024U * 1024U; }
	if (cfg->gui_gamepad_speed_pct < 10U){ cfg->gui_gamepad_speed_pct = 10U; }
	if (cfg->gui_gamepad_speed_pct > 200U){ cfg->gui_gamepad_speed_pct = 200U; }
	if (cfg->netplay_relay_port == 0U){ cfg->netplay_relay_port = 43810U; }
	if (cfg->netplay_relay_host[0] == 0){ appcfg_copy_str(cfg->netplay_relay_host, APPCFG_PATH_MAX, "uzenet.us"); }
	if (cfg->remote_roms_host[0] == 0){ appcfg_copy_str(cfg->remote_roms_host, APPCFG_PATH_MAX, "uzenet.us"); }
	if (cfg->screenshot_path[0] == 0){ appcfg_copy_str(cfg->screenshot_path, APPCFG_PATH_MAX, "screenshots"); }
	if (cfg->save_path[0] == 0){ appcfg_copy_str(cfg->save_path, APPCFG_PATH_MAX, "savestates"); }
	if (cfg->log_verbosity > CU_LOG_TRACE){ cfg->log_verbosity = CU_LOG_INFO; }
	if (cfg->resident_bootloader_file[0] == 0){ appcfg_copy_str(cfg->resident_bootloader_file, APPCFG_PATH_MAX, "Bootloader.hex"); }
	if (cfg->video_dump_file[0] == 0){
		appcfg_copy_str(cfg->video_dump_file, APPCFG_PATH_MAX, "video/CAPTURE001.MP4");
	}
	if (cfg->api_server_port == 0U){ cfg->api_server_port = API_SERVER_DEFAULT_PORT; }
	for (i = 0U; i < CU_MULTITAP_MAX_PORTS; i++){
		if (cfg->tap_active_slot[i] >= CU_MULTITAP_MAX_SLOTS){ cfg->tap_active_slot[i] = 0U; }
		for (auint s = 0U; s < CU_MULTITAP_MAX_SLOTS; s++){
			auint vv = cfg->tap_slot_vdev[i][s];
			if ((vv != CU_VDEV_INVALID) && (vv >= CU_VDEV_MAX)){ cfg->tap_slot_vdev[i][s] = CU_VDEV_INVALID; }
		}
	}
	for (i = 0U; i < CU_VDEV_MAX; i++){
		if (cfg->vdev[i].name[0] == 0 && cfg->vdev[i].used){
			appcfg_copy_str(cfg->vdev[i].name, CU_VDEV_NAME_MAX, "VDEV");
		}
		if (cfg->vdev[i].type > CU_VDEV_TYPE_KEYBOARD){ cfg->vdev[i].type = CU_VDEV_TYPE_NONE; }
		if (cfg->vdev[i].haptic_binding > 3U){ cfg->vdev[i].haptic_binding = CU_VDEV_INVALID; }
		cfg->vdev[i].haptic_id &= 0x07U;
		cfg->vdev[i].low16 &= 0xFFFFU;
		cfg->vdev[i].high16 &= 0xFFFFU;
		if (cfg->vdev[i].sm_scale_x_pct == 0U){ cfg->vdev[i].sm_scale_x_pct = 100U; }
		if (cfg->vdev[i].sm_scale_y_pct == 0U){ cfg->vdev[i].sm_scale_y_pct = 100U; }
		if (cfg->vdev[i].sm_scale_x_pct > 400U){ cfg->vdev[i].sm_scale_x_pct = 400U; }
		if (cfg->vdev[i].sm_scale_y_pct > 400U){ cfg->vdev[i].sm_scale_y_pct = 400U; }
		if (cfg->vdev[i].sm_deadzone > 32U){ cfg->vdev[i].sm_deadzone = 32U; }
		for (auint b = 0U; b < CU_VDEV_MAX_BINDINGS; b++){
			boole all_zero = TRUE;
			if (cfg->vdev[i].binding[b].type > CU_VDEV_HOST_JOYSTICK){ cfg->vdev[i].binding[b].type = CU_VDEV_HOST_NONE; }
			for (auint m = 0U; m < CU_VDEV_REMAP_SLOTS; m++){
				cfg->vdev[i].binding[b].map_low16[m] &= 0xFFFFU;
				if (cfg->vdev[i].binding[b].map_low16[m] != 0U){ all_zero = FALSE; }
			}
			if (all_zero && (cfg->vdev[i].binding[b].type != CU_VDEV_HOST_NONE)){
				for (auint m = 0U; m < CU_VDEV_REMAP_SLOTS; m++){
					cfg->vdev[i].binding[b].map_low16[m] = cu_vdev_default_map_for_host_type((cu_vdev_host_type_t)cfg->vdev[i].binding[b].type, m) & 0xFFFFU;
				}
			}
		}
	}
	return TRUE;
}
