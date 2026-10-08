#ifndef CONFIGCFG_H
#define CONFIGCFG_H

#include "types.h"
#include "renderpath.h"
#include "filters.h"
#include "cu_vdev.h"
#include "cu_multitap.h"
#include "netplay.h"

#define APPCFG_PATH_MAX 256U
#define APPCFG_RECENT_ROMS 10U
#define APPCFG_GUI_COLOR_COUNT 14U

#define APPCFG_GUI_PADDING        4U
#define APPCFG_GUI_SPACING        4U
#define APPCFG_GUI_SCROLLBAR_SIZE 12U
#define APPCFG_GUI_THUMB_SIZE     10U
#define APPCFG_GUI_TITLE_HEIGHT   20U

#define APPCFG_VERSION 1U

typedef struct{
	auint type;
	auint host_index;
	auint flags;
	auint map_low16[CU_VDEV_REMAP_SLOTS];
} appcfg_vdev_binding_t;

typedef struct{
	boole used;
	char  name[CU_VDEV_NAME_MAX];
	auint type;
	auint options;
	boole haptic_enabled;
	boole haptic_accept_any;
	auint haptic_binding;
	auint haptic_id;
	auint low16;
	auint high16;
	auint sm_scale_x_pct;
	auint sm_scale_y_pct;
	auint sm_deadzone;
	boole sm_invert_x;
	boole sm_invert_y;
	appcfg_vdev_binding_t binding[CU_VDEV_MAX_BINDINGS];
} appcfg_vdev_t;

typedef struct{
	auint version;
	boole display_gameonly;
	boole display_fullscreen;
	boole display_system_messages;
	auint log_verbosity;
	boole gui_gamepad_enable;
	auint gui_gamepad_speed_pct;
	boole gui_pause_while_open;
	boole gui_virtual_keyboard;
	boole frame_rate_limiter;
	boole frame_merge;
	auint render_path;
	boole input_kbuzem;
	boole input_player2alloc;
	boole audio_freqscale;
	boole audio_s16;
	auint audio_output_rate;
	auint audio_latency;
	auint audio_resampler;
	boole audio_dcblock;
	auint audio_lowpass;
	auint audio_lowpass_quality;
	auint audio_monitor_mode;
	auint audio_monitor_width;
	auint audio_reverb;
	auint audio_master_volume;
	boole mouse_enable;
	auint mouse_scale;
	auint filter_pre_mode;
	auint filter_scale_mode;
	auint filter_crt_mode;
	auint spiram_pages_mode;
	boole sd_allow_new_files;
	auint sd_timing_preset;
	auint sd_init_ms;
	auint sd_cmd_wait_bytes;
	auint sd_read_wait_bytes;
	auint sd_write_busy_ms;
	auint sd_cs_high_ms;
	auint sd_init_min_byte_cycles;
	auint sd_init_max_byte_cycles;
	auint esp_softap_mode;
	auint netplay_rollback_window;
	auint netplay_input_delay;
	auint netplay_max_players;
	uint32 netplay_local_player_mask;
	boole netplay_rom_send;
	boole netplay_rom_receive;
	auint netplay_rom_sync_mode;
	auint netplay_rom_tcp_port;
	uint32 netplay_rom_max_size;
	char netplay_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char netplay_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
	auint netplay_relay_port;
	char  netplay_relay_host[APPCFG_PATH_MAX];
	char  netplay_network_interface[APPCFG_PATH_MAX];
	char  remote_roms_host[APPCFG_PATH_MAX];
	char  video_dump_file[APPCFG_PATH_MAX];
	boole video_dump_autoinc;
	boole video_dump_reset_first;
	uint8 gui_colors[APPCFG_GUI_COLOR_COUNT][4];
	char gui_theme_name[32];
	char gui_theme_file[APPCFG_PATH_MAX];
	char  rom_path[APPCFG_PATH_MAX];
	char  screenshot_path[APPCFG_PATH_MAX];
	char  save_path[APPCFG_PATH_MAX];
	char  controllerdb_path[APPCFG_PATH_MAX];
	boole resident_bootloader_enable;
	boole fast_flash;
	char  resident_bootloader_file[APPCFG_PATH_MAX];
	char  recent_roms[APPCFG_RECENT_ROMS][APPCFG_PATH_MAX];
	boole recent_roms_frozen;
	boole api_server;
	auint api_server_port;
	boole cheats_enabled;
	boole cheats_autoloadsave;
	boole tap_present[CU_MULTITAP_MAX_PORTS];
	auint tap_active_slot[CU_MULTITAP_MAX_PORTS];
	auint tap_slot_vdev[CU_MULTITAP_MAX_PORTS][CU_MULTITAP_MAX_SLOTS];
	appcfg_vdev_t vdev[CU_VDEV_MAX];
} app_config_t;

void  appcfg_defaults(app_config_t* cfg);
boole appcfg_load_or_create(app_config_t* cfg, const char* path);
boole appcfg_save(app_config_t const* cfg, const char* path);

#endif
