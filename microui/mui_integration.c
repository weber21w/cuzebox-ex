#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stddef.h>
#include "mui_data.h"
#include "mui_integration.h"
#include "mui_filedialog.h"
#include "../guicore.h"
#include "../cu_avr.h"
#include "../chars.h"
#include "../audio.h"
#include "../configcfg.h"
#include "../mainui.h"
#include "../cu_multitap.h"
#include "../cu_vdev.h"
#include "../cu_ctr.h"
#include "../cu_kbd.h"
#include "../cu_haptic.h"
#include "../ginput.h"
#include "../cu_esp.h"
#include "../savestate.h"
#include "../textgui.h"
#include "../remote_roms.h"
#include "../cu_multitap.h"
#include "../cu_vdev.h"
#ifdef ENABLE_NETPLAY
#include "../netplay.h"
#include "../rollback.h"
#endif

#define MUI_BAR_HEIGHT      26
#define MUI_BAR_BACKDROP_H  (MUI_BAR_HEIGHT + 2)
#define MUI_BAR_REVEAL_Y    (MUI_BAR_BACKDROP_H + 6)
#define MUI_BAR_ROW_H      18
#define MUI_FONT_SCALE     2
#define MUI_FONT_CHAR_W    6
#define MUI_FONT_CHAR_H    6
#define MUI_FONT_CHAR_ADV  7
#define MUI_CFG_PATH_CAP   256
#define MUI_RECENT_ROMS    10
#define MUI_ICON_GAME_UZE  0x100
#define MUI_HASH_INITIAL    2166136261u

typedef struct{
	mu_Context* ctx;
	uint32*     dest;
	auint       pitch;
	auint       texw;
	auint       texh;
	mu_Rect     clip;
	int         mouse_x;
	int         mouse_y;
	boole       mouse_inside;
	boole       gamepad_cursor_active;
	auint       gamepad_slot;
	auint       gamepad_prev_buttons;
	auint       gamepad_event_buttons[2];
	boole       gamepad_open_prev;
	boole       gamepad_menu_chord_latch;
	boole       bar_visible;
	boole       bar_pinned;
	boole       show_game;
	boole       show_config;
	boole       show_config_video;
	boole       show_config_input;
	boole       show_config_devices;
	boole       show_sd_write_warning;
	boole       show_config_audio;
	boole       show_config_paths;
	boole       show_config_gui;
	boole       show_config_serial;
	boole       show_config_midi_ports;
	boole       show_cheats;
	boole       show_play_online;
	boole       show_online_games;
	boole       show_netplay;
	boole       show_netplay_start;
	boole       show_netplay_settings;
	boole       show_netplay_status;
	boole       show_netplay_disconnect;
	boole       show_lobby;
	boole       show_tools;
	boole       show_tool_screenshot;
	boole       show_tool_video;
	boole       show_tool_romview;
	boole       show_tool_sram;
	boole       show_tool_spiram;
	boole       show_tool_eeprom;
	boole       show_tool_inputcap;
		boole       show_tool_input_trace;
	boole       show_tool_serial_trace;
	boole       show_tool_esp_monitor;
	boole       show_tool_cheatsearch;
	boole       show_remote;
#ifdef ENABLE_DEBUGGER
	boole       show_debugger;
#endif
	boole       show_quick;
	boole       show_statepick;
} mui_state_t;

static mui_state_t mui_state;

static int mui_apply_gui_cursor_speed(int delta);

typedef enum{
	MUI_WIN_GAME = 0,
	MUI_WIN_CONFIG,
	MUI_WIN_CFG_VIDEO,
	MUI_WIN_CFG_INPUT,
	MUI_WIN_CFG_DEVICES,
	MUI_WIN_SD_WRITE_WARNING,
	MUI_WIN_CFG_AUDIO,
	MUI_WIN_CFG_PATHS,
	MUI_WIN_CFG_GUI,
	MUI_WIN_SERIAL,
	MUI_WIN_MIDI_PORTS,
	MUI_WIN_VIEW_CHEATS,
	MUI_WIN_SEARCH_CHEATS,
	MUI_WIN_PLAY_ONLINE,
	MUI_WIN_ONLINE_GAMES,
	MUI_WIN_NETPLAY,
	MUI_WIN_NETPLAY_START,
	MUI_WIN_NETPLAY_SETTINGS,
	MUI_WIN_NETPLAY_STATUS,
	MUI_WIN_NETPLAY_DISCONNECT,
	MUI_WIN_LOBBY,
	MUI_WIN_TOOLS,
	MUI_WIN_REMOTE_ROMS,
	MUI_WIN_RECENT_ROMS,
	MUI_WIN_PICK_STATE,
	MUI_WIN_SCREENSHOT,
	MUI_WIN_DUMP_VIDEO,
	MUI_WIN_ROM_VIEW,
	MUI_WIN_SRAM_EDIT,
	MUI_WIN_SPIRAM_EDIT,
	MUI_WIN_EEPROM_EDIT,
	MUI_WIN_INPUT_CAPTURE,
		MUI_WIN_INPUT_TRACE,
	MUI_WIN_SERIAL_TRACE,
	MUI_WIN_ESP_MONITOR,
#ifdef ENABLE_DEBUGGER
	MUI_WIN_DEBUGGER,
#endif
	MUI_WIN_COUNT
} mui_window_id_t;

#define MUI_WCLASS_MENU  0x01U
#define MUI_WCLASS_TOOL  0x02U
#define MUI_WCLASS_CHILD 0x04U

typedef struct{
	char const* title;
	size_t      show_offset;
	mu_Rect     rect;
	int         opts;
	auint       wclass;
} mui_window_desc_t;

#define MUI_SHOW_OFF(member) offsetof(mui_state_t, member)
#define MUI_WINDOW_DESC(title, member, x, y, w, h, opts, wclass) \
	{ title, MUI_SHOW_OFF(member), { x, y, w, h }, opts, wclass }

static mui_window_desc_t const mui_window_descs[MUI_WIN_COUNT] = {
	MUI_WINDOW_DESC("GAME",           show_game,              8,   MUI_BAR_HEIGHT + 6, 324, 376, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_MENU),
	MUI_WINDOW_DESC("CONFIG",         show_config,           36,   MUI_BAR_HEIGHT + 10, 292, 272, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_MENU),
	MUI_WINDOW_DESC("VIDEO",          show_config_video,    8,     MUI_BAR_HEIGHT + 10, 416, 340, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("INPUT",          show_config_input,    8,     MUI_BAR_HEIGHT + 10, 428, 388, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("DEVICES",        show_config_devices,  8,     MUI_BAR_HEIGHT + 10, 428, 344, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("ALLOW NEW SD FILES?", show_sd_write_warning, 92, MUI_BAR_HEIGHT + 54, 560, 250, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("AUDIO",          show_config_audio,    8,     MUI_BAR_HEIGHT + 10, 432, 430, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("PATHS",          show_config_paths,    8,     MUI_BAR_HEIGHT + 10, 448, 338, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("GUI",            show_config_gui,      8,     MUI_BAR_HEIGHT + 4,  500, 392, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("SERIAL",         show_config_serial,   10,    MUI_BAR_HEIGHT + 18, 760, 560, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("MIDI PORTS",     show_config_midi_ports,204,  MUI_BAR_HEIGHT + 48, 360, 260, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("VIEW CHEATS",    show_cheats,           8,    MUI_BAR_HEIGHT + 6,  520, 364, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("SEARCH CHEATS",  show_tool_cheatsearch, 72,   MUI_BAR_HEIGHT + 16, 500, 472, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("PLAY ONLINE",    show_play_online,       8,   MUI_BAR_HEIGHT + 18, 520, 336, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_MENU),
	MUI_WINDOW_DESC("BROWSE GAMES",   show_online_games,      8,   MUI_BAR_HEIGHT + 18, 620, 360, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("NETPLAY",        show_netplay,        262,   MUI_BAR_HEIGHT + 12, 300, 190, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_MENU),
	MUI_WINDOW_DESC("NETPLAY START",  show_netplay_start,   8,     MUI_BAR_HEIGHT + 18, 584, 318, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("NETPLAY SETTINGS",show_netplay_settings,160,  MUI_BAR_HEIGHT + 18, 520, 412, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("NETPLAY STATUS", show_netplay_status,   8,    MUI_BAR_HEIGHT + 12, 620, 420, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("QUIT NETPLAY",show_netplay_disconnect,208, MUI_BAR_HEIGHT + 52, 220, 116, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("LOBBY",          show_lobby,           8,     MUI_BAR_HEIGHT + 12, 620, 470, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("TOOLS",          show_tools,           262,   MUI_BAR_HEIGHT + 18, 266, 430, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_MENU),
	MUI_WINDOW_DESC("REMOTE ROMS",    show_remote,          16,    MUI_BAR_HEIGHT + 6,  640, 452, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("RECENT ROMS",    show_quick,           0,     MUI_BAR_HEIGHT + 2,  520, 342, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_MENU),
	MUI_WINDOW_DESC("PICK STATE",     show_statepick,       256,   MUI_BAR_HEIGHT + 6,  244, 132, MU_OPT_NORESIZE,                 MUI_WCLASS_CHILD),
	MUI_WINDOW_DESC("SCREENSHOT",     show_tool_screenshot, 210,   MUI_BAR_HEIGHT + 18, 330, 148, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("DUMP VIDEO",     show_tool_video,      108,   MUI_BAR_HEIGHT + 18, 396, 284, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("ROM VIEW",       show_tool_romview,    52,    MUI_BAR_HEIGHT + 8,  536, 456, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("SRAM EDIT",      show_tool_sram,       52,    MUI_BAR_HEIGHT + 8,  536, 456, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("SPI RAM EDIT",   show_tool_spiram,     52,    MUI_BAR_HEIGHT + 8,  536, 456, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("EEPROM EDIT",    show_tool_eeprom,     52,    MUI_BAR_HEIGHT + 8,  536, 456, MU_OPT_NORESIZE | MU_OPT_NOSCROLL, MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("INP. CAPTURE",   show_tool_inputcap,   210,   MUI_BAR_HEIGHT + 18, 360, 176, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
		MUI_WINDOW_DESC("INPUT TRACE",    show_tool_input_trace, 36,   MUI_BAR_HEIGHT + 8,  744, 484, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("SERIAL TRACE",   show_tool_serial_trace, 40,   MUI_BAR_HEIGHT + 8,  720, 456, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
	MUI_WINDOW_DESC("ESP MONITOR",    show_tool_esp_monitor,  36,   MUI_BAR_HEIGHT + 8,  620, 418, MU_OPT_NORESIZE,                 MUI_WCLASS_TOOL),
#ifdef ENABLE_DEBUGGER
	MUI_WINDOW_DESC("Debugger",       show_debugger,        0,     MUI_BAR_HEIGHT + 8,  604, 680, 0,                               MUI_WCLASS_TOOL),
#endif
};
static boole mui_window_seen_frame[MUI_WIN_COUNT];

#define MUI_FILE_TARGET_BOOTLOADER   1
#define MUI_FILE_TARGET_VIDEO_DUMP   2
#define MUI_FILE_TARGET_DEBUG_SYMBOL 3
#define MUI_FILE_TARGET_THEME_SELECT 4
#define MUI_FILE_TARGET_THEME_EXPORT 5


static char  mui_cfg_rom_path[MUI_CFG_PATH_CAP];
static char  mui_cfg_screenshot_path[MUI_CFG_PATH_CAP];
static char  mui_cfg_save_path[MUI_CFG_PATH_CAP];
static char  mui_cfg_controllerdb_path[MUI_CFG_PATH_CAP];
static char  mui_cfg_resident_bootloader_file[MUI_CFG_PATH_CAP];
static char  mui_cfg_gui_theme_file[MUI_CFG_PATH_CAP];
static char  mui_cfg_gui_theme_status[128] = "";
static int   mui_cfg_gui_picker_state = -1;
static boole mui_cfg_gui_picker_open = FALSE;
static boole mui_cfg_gui_picker_just_opened = FALSE;
static boole mui_cfg_paths_synced = FALSE;
static char const* const mui_gui_color_labels[APPCFG_GUI_COLOR_COUNT] = {
	"TEXT", "BORDER", "WIN BG", "TITLE BG", "TITLE FG", "PANEL BG", "BTN", "BTN HOT",
	"BTN FOCUS", "CTRL BG", "CTRL HOT", "CTRL FOCUS", "SCR BG", "SCR THUMB"
};
static char  mui_cfg_host_serial_device[MUI_CFG_PATH_CAP];
static char  mui_cfg_host_midi_port[MUI_CFG_PATH_CAP];
static char  mui_cfg_virtual_midi_port[MUI_CFG_PATH_CAP];
static char  mui_cfg_tcp_serial_host[MUI_CFG_PATH_CAP];
static char  mui_cfg_tcp_serial_port[32];
static int   mui_cfg_tcp_serial_auto_reconnect = 1;
static int   mui_cfg_tcp_serial_mode = 0;   /* 0 client, 1 server, 2 auto, 3 internet, 4 lan */
static char  mui_cfg_link_room[16];
static char  mui_cfg_link_relay_host[64];
static char  mui_cfg_link_relay_port[16];
static boole mui_cfg_serial_synced = FALSE;
static char  mui_serial_status[256] = "";
static char  mui_input_trace_status[128] = "";
static int   mui_input_trace_show_frames = 1;
static int   mui_input_trace_show_tap = 1;
static int   mui_input_trace_show_kbd = 1;
static int   mui_input_trace_show_p1 = 1;
static int   mui_input_trace_show_p2 = 1;
static char  mui_trace_status[256] = "";
static auint mui_cheat_selected = 0U;
static boole mui_cheat_editor_synced = FALSE;
static char  mui_cheat_desc[CHEAT_DESC_MAX];
static char  mui_cheat_addr[16];
static char  mui_cheat_value[16];
static char  mui_cheat_compare[16] = "0";
static int   mui_cheat_enabled = 1;
static int   mui_cheat_compare_used = 0;
static int   mui_cheat_value_type = 0;
static char  mui_cheat_search_value[16] = "00";
static char  mui_cheat_search_by_value[16] = "01";
static char  mui_cheat_search_status[128] = "";
static auint mui_cheat_search_selected_addr = 0U;
static uint8 mui_cheat_search_hits[0x1000U];
static uint32 mui_cheat_search_prev[0x1000U];
static auint mui_cheat_search_count = 0U;
static int   mui_cheat_search_value_type = 0;
static int   mui_cheat_search_pause_while_active = 1;
static int   mui_cheat_search_use_by = 0;
static boole mui_cheat_search_forced_pause = FALSE;
static boole mui_cheat_search_initialized = FALSE;
static mui_filedialog_t mui_rom_dialog;
static mui_filedialog_t mui_dir_dialog;
static mui_filedialog_t mui_cfg_file_dialog;
static char  mui_game_status[256] = "";
static char  mui_pending_rom_load[MUI_FILEDIALOG_PATH_CAP] = "";
static char  mui_pending_dir_apply[MUI_FILEDIALOG_PATH_CAP] = "";
static int   mui_pending_dir_target = 0;
static char  mui_pending_file_apply[MUI_FILEDIALOG_PATH_CAP] = "";
static int   mui_pending_file_target = 0;
static char  mui_remote_filter[96] = "";
static char  mui_remote_host[MUI_CFG_PATH_CAP] = "uzenet.us";
static auint mui_remote_selected = 0U;
static int   mui_cfg_page = 1;
static auint mui_input_cfg_tab = 0U;
static auint mui_vdev_selected = CU_VDEV_INVALID;
static boole mui_vdev_editor_synced = FALSE;
static char  mui_vdev_name[CU_VDEV_NAME_MAX] = "";
static char  mui_vdev_status[160] = "";
static auint mui_vdev_learn_target[CU_VDEV_MAX_BINDINGS] = { CU_CTR_SNES_M_B, CU_CTR_SNES_M_B };
static boole mui_vdev_learn_armed = FALSE;
static auint mui_vdev_learn_vdev = CU_VDEV_INVALID;
static auint mui_vdev_learn_bind = 0U;
static auint mui_vdev_learn_last_mask = 0U;
static char  mui_tap_debug_status[2][160] = {{0}};
static char  mui_kbd_debug_status[2][160] = {{0}};
static char  mui_hap_debug_status[2][160] = {{0}};
static char __attribute__((unused)) mui_preset_status[160] = "";

#define MUI_VDEV_CHOICE_MAX      (1U + CU_VDEV_MAX)
#define MUI_HOST_CHOICE_MAX      64U
#define MUI_HOST_LABEL_MAX       128U

static char  mui_vdev_choice_labels[MUI_VDEV_CHOICE_MAX][96];
static char const* mui_vdev_choice_ptrs[MUI_VDEV_CHOICE_MAX];
static auint mui_vdev_choice_ids[MUI_VDEV_CHOICE_MAX];
static auint mui_vdev_choice_count = 0U;

static char  mui_host_choice_labels[MUI_HOST_CHOICE_MAX][MUI_HOST_LABEL_MAX];
static char const* mui_host_choice_ptrs[MUI_HOST_CHOICE_MAX];
static cu_vdev_host_type_t mui_host_choice_type[MUI_HOST_CHOICE_MAX];
static auint mui_host_choice_index[MUI_HOST_CHOICE_MAX];
static auint mui_host_choice_count = 0U;
static char  mui_toolshot_status[256] = "";
static char  mui_toolvideo_status[256] = "";
static char  mui_toolvideo_file[APPCFG_PATH_MAX] = "";
static char  mui_toolcap_status[256] = "";
static char  mui_tools_status[256] = "";
static auint mui_esp_monitor_tab = 0U;
static char  mui_tool_mem_base_rom[16] = "0000";
static char  mui_tool_mem_base_sram[16] = "000";
static char  mui_tool_mem_base_spiram[16] = "000";
static char  mui_tool_mem_base_eeprom[16] = "000";
static char  mui_tool_mem_edit_addr[16] = "000";
static char  mui_tool_mem_edit_value[16] = "00";
#ifdef ENABLE_NETPLAY
static boole mui_np_synced = FALSE;
static char  mui_np_host[256] = "127.0.0.1";
static char  mui_np_port[16] = "43800";
static char  mui_np_local_port[16] = "";
static char  mui_np_relay_host[NETPLAY_RELAY_HOST_LEN + 1U] = "uzenet.us";
static char  mui_np_relay_port[16] = "43810";
static char  mui_np_room_code[NETPLAY_RELAY_ROOM_CODE_LEN + 1U] = "";
static char  mui_np_name[NETPLAY_LOBBY_NAME_LEN + 1U] = "Player";
static char  mui_np_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U] = { "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "Pad 7", "Pad 8" };
static int   mui_np_local_mask_bits[ROLLBACK_MAX_PLAYERS] = { 1, 0, 0, 0 };
static char  mui_np_max_players[8] = "2";
static char  mui_np_tcp_port[16] = "0";
static char  mui_np_max_rom_mb[16] = "4";
static char  mui_np_rb_window[8] = "12";
static char  mui_np_input_delay[8] = "1";
static int   mui_np_send_rom = 1;
static int   mui_np_recv_rom = 1;
static int   mui_np_sync_mode = NETPLAY_ROM_SYNC_MISMATCH;
static char const* const mui_np_sync_mode_names[4] = { "OFF", "MISSING", "MISMATCH", "ALWAYS" };
static char  mui_np_status[256] = "";
static int   mui_np_public_room = 1;
static auint mui_np_iface_choice = 0U;
static char  mui_np_iface_labels[1U + NETPLAY_IFACE_MAX][NETPLAY_IFACE_LABEL_LEN];
static char const* mui_np_iface_name_ptrs[1U + NETPLAY_IFACE_MAX];
static auint mui_np_iface_count = 1U;
static netplay_relay_room_entry_t mui_online_rooms[NETPLAY_RELAY_BROWSE_MAX];
static auint mui_online_room_count = 0U;
static char  mui_online_room_status[256] = "";
static char  mui_lobby_chat_input[80] = "";
static char  mui_lobby_status[256] = "";
static auint mui_lobby_req_pad[ROLLBACK_MAX_PLAYERS] = { 0U, 0U, 0U, 0U };
static boole mui_np_enabled_prev = FALSE;
static boole mui_np_connected_prev = FALSE;
static boole mui_np_session_started_prev = FALSE;
#define MUI_NP_PING_HISTORY 24U
static uint16 mui_np_ping_history[MUI_NP_PING_HISTORY];
static auint  mui_np_ping_history_head = 0U;
static auint  mui_np_ping_history_count = 0U;
static uint32 mui_np_ping_history_last = 0U;
#endif
#ifdef ENABLE_DEBUGGER
static char  mui_dbg_symbols_file[MUI_CFG_PATH_CAP] = "";
static char  mui_dbg_sram_base[8] = "000";
static char  mui_dbg_io_base[8] = "00";
static char  mui_dbg_prog_base[8] = "0000";
static int   mui_dbg_follow_pc = 1;
static char  mui_dbg_reg_index[8] = "00";
static char  mui_dbg_reg_value[8] = "00";
static char  mui_dbg_sram_edit_addr[8] = "000";
static char  mui_dbg_sram_edit_value[8] = "00";
static char  mui_dbg_io_edit_addr[8] = "00";
static char  mui_dbg_io_edit_value[8] = "00";
static char  mui_dbg_break_addr[8] = "0000";
static int   mui_dbg_wp_slot = 0;
static int   mui_dbg_wp_region = MAINUI_DBG_WATCH_REGION_SRAM;
static int   mui_dbg_wp_enable = 1;
static int   mui_dbg_wp_read = 0;
static int   mui_dbg_wp_write = 1;
static char  mui_dbg_wp_start[16] = "000";
static char  mui_dbg_wp_end[16] = "000";
static int   mui_dbg_watch_enable[MAINUI_DBG_VALUE_WATCHES] = { 1, 1, 0, 0, 0, 0, 0, 0 };
static int   mui_dbg_watch_region[MAINUI_DBG_VALUE_WATCHES] = { MAINUI_DBG_WATCH_REGION_SRAM, MAINUI_DBG_WATCH_REGION_IO, MAINUI_DBG_WATCH_REGION_SRAM, MAINUI_DBG_WATCH_REGION_SRAM, MAINUI_DBG_WATCH_REGION_SRAM, MAINUI_DBG_WATCH_REGION_SRAM, MAINUI_DBG_WATCH_REGION_SRAM, MAINUI_DBG_WATCH_REGION_SRAM };
static char  mui_dbg_watch_addr[MAINUI_DBG_VALUE_WATCHES][16] = { "000", "C0", "000", "000", "000", "000", "000", "000" };

static auint mui_dbg_watch_last[MAINUI_DBG_VALUE_WATCHES];
static int   mui_dbg_watch_seen[MAINUI_DBG_VALUE_WATCHES];
static void mui_dbg_reset_value_watches_defaults(void)
{
	auint i;
	for (i = 0U; i < MAINUI_DBG_VALUE_WATCHES; i++){
		mui_dbg_watch_enable[i] = 0;
		mui_dbg_watch_region[i] = MAINUI_DBG_WATCH_REGION_SRAM;
		strncpy(mui_dbg_watch_addr[i], "000", sizeof(mui_dbg_watch_addr[i]) - 1U);
		mui_dbg_watch_addr[i][sizeof(mui_dbg_watch_addr[i]) - 1U] = 0;
		mui_dbg_watch_last[i] = 0U;
		mui_dbg_watch_seen[i] = 0;
	}
	mui_dbg_watch_enable[0] = 1;
	mui_dbg_watch_enable[1] = 1;
	mui_dbg_watch_region[1] = MAINUI_DBG_WATCH_REGION_IO;
	strncpy(mui_dbg_watch_addr[1], "C0", sizeof(mui_dbg_watch_addr[1]) - 1U);
	mui_dbg_watch_addr[1][sizeof(mui_dbg_watch_addr[1]) - 1U] = 0;
}
static auint mui_dbg_mem_region = MAINUI_DBG_MEM_SRAM;
static char  mui_dbg_mem_base[16] = "000";
static char  mui_dbg_gfx_base[16] = "000";
static char  mui_dbg_gfx_width[16] = "64";
static char  mui_dbg_gfx_zoom[8] = "2";
static int   mui_dbg_gfx_bpp = 1;
#endif

static int mui_button_width(const char* text);
static void mui_set_game_status(char const* text);
static void mui_sync_window_open(mu_Context* ctx, char const* title, boole* show_flag) CU_UNUSED_FN;
static void mui_open_window_flag(mu_Context* ctx, char const* title, boole* show_flag);
static boole mui_window_flag_is_open(mu_Context* ctx, char const* title, boole const* show_flag);
static void mui_toggle_window_flag(mu_Context* ctx, char const* title, boole* show_flag);
static mui_window_desc_t const* mui_window_desc(mui_window_id_t id);
static boole* mui_window_show_ptr(mui_window_id_t id);
static void mui_window_sync(mu_Context* ctx, mui_window_id_t id);
static void mui_window_open(mu_Context* ctx, mui_window_id_t id);
static void mui_window_close(mu_Context* ctx, mui_window_id_t id);
static void mui_window_toggle(mu_Context* ctx, mui_window_id_t id);
static boole mui_window_is_open(mu_Context* ctx, mui_window_id_t id);
static boole mui_window_begin(mu_Context* ctx, mui_window_id_t id);
static void mui_window_end(mu_Context* ctx, mui_window_id_t id);
static mui_window_id_t mui_cfg_page_to_window_id(int page);
static boole mui_any_config_page_open(mu_Context* ctx);
static void mui_close_config_pages(mu_Context* ctx);
static void mui_open_config_page(mu_Context* ctx, int page);
#ifdef ENABLE_NETPLAY
static void mui_np_ping_track(uint32 ping_ms);
static void mui_np_ping_labels(mu_Context* ctx);
static void mui_np_refresh_interfaces(void);
#endif
static void mui_open_tool_mem_window(mu_Context* ctx, auint region);
static boole mui_try_open_tool_mem_window(mu_Context* ctx, auint region);
static auint mui_tool_parse_hex(char const* text, auint defv);
static void mui_tool_sync_edit_fields(auint region, auint addr);
static void mui_tool_select_byte(auint region, auint addr);
static void mui_build_tool_screenshot_window(mu_Context* ctx);
static void mui_build_tool_video_window(mu_Context* ctx);
static void mui_build_tool_mem_window(mu_Context* ctx, mui_window_id_t win_id, auint region, char* base_text, boole editable);
static void mui_build_tool_inputcap_window(mu_Context* ctx);
static void mui_build_tool_serial_trace_window(mu_Context* ctx);
static void mui_build_tool_input_trace_window(mu_Context* ctx);
static void mui_build_sd_write_warning_window(mu_Context* ctx);
static boole mui_input_trace_line_visible(char const* line);
static boole mui_input_trace_export_filtered(char const* path, boole filtered_only);
static void mui_build_tool_esp_monitor_window(mu_Context* ctx);
#ifdef ENABLE_NETPLAY
static void mui_build_online_games_window(mu_Context* ctx);
#endif
static void mui_build_cheat_search_window(mu_Context* ctx);
static void mui_apply_pending_rom_load(void);
static void mui_apply_pending_dir_change(void);
static void mui_apply_pending_file_change(void);
static void mui_sync_toolvideo_file(void);
static void mui_sync_serial_buffers(void);
static char const* mui_serial_route_name(auint route);
static char const* mui_serial_esp_name(auint model);
static char const* mui_virtual_midi_mode_name(auint mode);
static void mui_vdev_cancel_learn(char const* status);
static boole mui_labeled_choice_popup(mu_Context* ctx, const char* label, const char* popup_id, const char* value, const char* const* names, auint count, auint* io_value, int labelw, int choicew);
#ifdef ENABLE_NETPLAY
static void mui_netplay_auto_open_lobby(mu_Context* ctx);
#endif
static void mui_system_message(char const* fmt, ...);
static void mui_close_all_windows(void);
static void mui_gamepad_open_default(mu_Context* ctx);
static void mui_gamepad_toggle(mu_Context* ctx, auint slot);
static void mui_gamepad_release(void);
static auint mui_gamepad_pick_slot(void);
static void mui_gamepad_draw_cursor(void);
static void mui_gamepad_tick(void);

static void mui_build_midi_ports_window(mu_Context* ctx) CU_UNUSED_FN;
static void mui_cheat_search_reset_all(void);
static void mui_cheat_search_sync_pause(void);
static auint mui_parse_hex_field(char const* text, auint mask);
static auint mui_cheat_type_width_bytes(int type);
static boole mui_cheat_type_signed(int type);
static int mui_cheat_type_from_entry(cheat_entry_t const* ent);
static uint32 mui_cheat_width_mask(auint width);
static uint32 mui_read_sram_typed_value(auint addr, auint width);
static sint32 mui_value_to_signed(uint32 value, auint width);
static char mui_ascii_byte(auint v);
static void mui_format_ascii_bytes(char* out, auint out_size, auint addr, auint width);
static void mui_cheat_search_set_type(int type);
static mu_Container* mui_find_container(mu_Context* ctx, char const* title);
#ifdef ENABLE_GUI_VKEYBOARD
static char*  mui_vkbd_buf = NULL;
static int    mui_vkbd_bufsz = 0;
static mu_Id  mui_vkbd_buf_id = 0;
static boole  mui_vkbd_shift = FALSE;
static int    mui_vkbd_hold_frames = 0;
static int mui_textbox_tracked(mu_Context* ctx, char* buf, int bufsz, int opt);
static boole mui_vkbd_window_hovered(mu_Context* ctx);
static boole mui_vkbd_available(mu_Context* ctx);
static void mui_vkbd_begin_frame(void);
static void mui_vkbd_focus_restore(mu_Context* ctx);
static void mui_vkbd_insert_text(mu_Context* ctx, char const* txt);
static void mui_vkbd_backspace(mu_Context* ctx);
static void mui_vkbd_move_left(mu_Context* ctx);
static void mui_vkbd_move_right(mu_Context* ctx);
static void mui_vkbd_submit(mu_Context* ctx);
static void mui_build_virtual_keyboard(mu_Context* ctx);
#undef mu_textbox
#undef mu_textbox_ex
#define mu_textbox(ctx, buf, bufsz) mui_textbox_tracked((ctx), (buf), (bufsz), 0)
#define mu_textbox_ex(ctx, buf, bufsz, opt) mui_textbox_tracked((ctx), (buf), (bufsz), (opt))
#endif

#ifdef ENABLE_GUI_VKEYBOARD
int mui_textbox_with_vkbd(mu_Context* ctx, char* buf, int bufsz, int opt)
{
	return mui_textbox_tracked(ctx, buf, bufsz, opt);
}

static int mui_textbox_tracked(mu_Context* ctx, char* buf, int bufsz, int opt)
{
	mu_Id id;
	mu_Rect r;
	int res;
	if (ctx == NULL){ return 0; }
	id = mu_get_id(ctx, &buf, (int)sizeof(buf));
	r = mu_layout_next(ctx);
	res = mu_textbox_raw(ctx, buf, bufsz, id, r, opt);
	if ((ctx->focus == id) || (ctx->textbox_view_id == id)){
		mui_vkbd_buf = buf;
		mui_vkbd_bufsz = bufsz;
		mui_vkbd_buf_id = id;
		mui_vkbd_hold_frames = 2;
	}
	return res;
}

static boole mui_vkbd_window_hovered(mu_Context* ctx)
{
	mu_Container* cnt;
	if (ctx == NULL){ return FALSE; }
	cnt = mui_find_container(ctx, "##vkeyboard");
	if (cnt == NULL){ return FALSE; }
	if ((ctx->hover_root == cnt) || (ctx->next_hover_root == cnt)){ return TRUE; }
	return FALSE;
}

static boole mui_vkbd_available(mu_Context* ctx)
{
	if (!mainui_get_gui_virtual_keyboard()){ return FALSE; }
	if (ctx == NULL){ return FALSE; }
	if ((mui_vkbd_buf == NULL) || (mui_vkbd_bufsz <= 1) || (mui_vkbd_buf_id == 0U)){ return FALSE; }
	if ((ctx->focus == mui_vkbd_buf_id) || (ctx->textbox_view_id == mui_vkbd_buf_id)){ return TRUE; }
	if (mui_vkbd_window_hovered(ctx)){ return TRUE; }
	if (mui_vkbd_hold_frames > 0){ return TRUE; }
	return FALSE;
}

static void mui_vkbd_begin_frame(void)
{
	if (mui_vkbd_hold_frames > 0){
		mui_vkbd_hold_frames--;
	}else{
		mui_vkbd_buf = NULL;
		mui_vkbd_bufsz = 0;
		mui_vkbd_buf_id = 0U;
	}
}

static void mui_vkbd_focus_restore(mu_Context* ctx)
{
	if ((ctx == NULL) || (mui_vkbd_buf_id == 0U)){ return; }
	ctx->textbox_view_id = mui_vkbd_buf_id;
	if (ctx->textbox_view_ofs < 0){ ctx->textbox_view_ofs = 0; }
	mu_set_focus(ctx, mui_vkbd_buf_id);
	mui_vkbd_hold_frames = 2;
}

static void mui_vkbd_insert_text(mu_Context* ctx, char const* txt)
{
	int len;
	int ins;
	int add;
	if ((ctx == NULL) || (mui_vkbd_buf == NULL) || (txt == NULL)){ return; }
	len = (int)strlen(mui_vkbd_buf);
	ins = (ctx->textbox_view_id == mui_vkbd_buf_id) ? ctx->textbox_view_ofs : len;
	if (ins < 0){ ins = 0; }
	if (ins > len){ ins = len; }
	add = (int)strlen(txt);
	if (add <= 0){ mui_vkbd_focus_restore(ctx); return; }
	if ((len + add) >= mui_vkbd_bufsz){ add = mui_vkbd_bufsz - len - 1; }
	if (add <= 0){ mui_vkbd_focus_restore(ctx); return; }
	memmove(mui_vkbd_buf + ins + add, mui_vkbd_buf + ins, (size_t)(len - ins + 1));
	memcpy(mui_vkbd_buf + ins, txt, (size_t)add);
	ctx->textbox_view_id = mui_vkbd_buf_id;
	ctx->textbox_view_ofs = ins + add;
	mui_vkbd_focus_restore(ctx);
}

static void mui_vkbd_backspace(mu_Context* ctx)
{
	int len;
	int pos;
	if ((ctx == NULL) || (mui_vkbd_buf == NULL)){ return; }
	len = (int)strlen(mui_vkbd_buf);
	pos = (ctx->textbox_view_id == mui_vkbd_buf_id) ? ctx->textbox_view_ofs : len;
	if (pos <= 0){ mui_vkbd_focus_restore(ctx); return; }
	if (pos > len){ pos = len; }
	memmove(mui_vkbd_buf + pos - 1, mui_vkbd_buf + pos, (size_t)(len - pos + 1));
	ctx->textbox_view_id = mui_vkbd_buf_id;
	ctx->textbox_view_ofs = pos - 1;
	mui_vkbd_focus_restore(ctx);
}

static void mui_vkbd_move_left(mu_Context* ctx)
{
	if (ctx == NULL){ return; }
	ctx->textbox_view_id = mui_vkbd_buf_id;
	if (ctx->textbox_view_ofs > 0){ ctx->textbox_view_ofs--; }
	mui_vkbd_focus_restore(ctx);
}

static void mui_vkbd_move_right(mu_Context* ctx)
{
	int len;
	if ((ctx == NULL) || (mui_vkbd_buf == NULL)){ return; }
	len = (int)strlen(mui_vkbd_buf);
	ctx->textbox_view_id = mui_vkbd_buf_id;
	if (ctx->textbox_view_ofs < len){ ctx->textbox_view_ofs++; }
	mui_vkbd_focus_restore(ctx);
}

static void mui_vkbd_submit(mu_Context* ctx)
{
	if (ctx == NULL){ return; }
	mu_set_focus(ctx, 0);
	mui_vkbd_hold_frames = 0;
}

static boole mui_vkbd_key_button(mu_Context* ctx, char const* label, char const* txt)
{
	if (mu_button(ctx, label)){
		mui_vkbd_insert_text(ctx, txt);
		return TRUE;
	}
	return FALSE;
}

static void mui_build_virtual_keyboard(mu_Context* ctx)
{
	mu_Container* cnt;
	static char const* const row0[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "_" };
	static char const* const row1[] = { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", ".", "/" };
	static char const* const row2[] = { "a", "s", "d", "f", "g", "h", "j", "k", "l", ":", "\\", "@" };
	static char const* const row3[] = { "z", "x", "c", "v", "b", "n", "m" };
	char keybuf[4];
	char title[96];
	auint i;
	if (!mui_vkbd_available(ctx)){ return; }
	cnt = mui_find_container(ctx, "##vkeyboard");
	if (cnt != NULL){
		cnt->open = 1;
		mu_bring_to_front(ctx, cnt);
	}
	snprintf(title, sizeof(title), "KEYBOARD [%s]", mui_vkbd_shift ? "SHIFT" : "abc");
	if (mu_begin_window_ex(ctx, "##vkeyboard", mu_rect(12, (int)mui_state.texh - 188, (int)mui_state.texw - 24, 176), MU_OPT_NORESIZE | MU_OPT_NOSCROLL)){
		mu_layout_row(ctx, 1, (int[]){ -1 }, 18);
		mu_label(ctx, title);
		mu_layout_row(ctx, 12, (int[]){ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 }, 22);
		for (i = 0U; i < 12U; ++i){
			keybuf[0] = mui_vkbd_shift ? (char)toupper((unsigned char)row0[i][0]) : row0[i][0];
			keybuf[1] = 0;
			mui_vkbd_key_button(ctx, keybuf, keybuf);
		}
		mu_layout_row(ctx, 12, (int[]){ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 }, 22);
		for (i = 0U; i < 12U; ++i){
			keybuf[0] = mui_vkbd_shift ? (char)toupper((unsigned char)row1[i][0]) : row1[i][0];
			keybuf[1] = 0;
			mui_vkbd_key_button(ctx, keybuf, keybuf);
		}
		mu_layout_row(ctx, 12, (int[]){ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 }, 22);
		for (i = 0U; i < 12U; ++i){
			keybuf[0] = row2[i][0];
			if (((keybuf[0] >= 'a') && (keybuf[0] <= 'z')) && mui_vkbd_shift){
				keybuf[0] = (char)toupper((unsigned char)keybuf[0]);
			}
			keybuf[1] = 0;
			mui_vkbd_key_button(ctx, keybuf, keybuf);
		}
		mu_layout_row(ctx, 10, (int[]){ mui_button_width("SHIFT"), -1,-1,-1,-1,-1,-1,-1, mui_button_width("SPACE"), mui_button_width("BKSP") }, 24);
		if (mu_button(ctx, "SHIFT")){
			mui_vkbd_shift = !mui_vkbd_shift;
			mui_vkbd_focus_restore(ctx);
		}
		for (i = 0U; i < 7U; ++i){
			keybuf[0] = mui_vkbd_shift ? (char)toupper((unsigned char)row3[i][0]) : row3[i][0];
			keybuf[1] = 0;
			mui_vkbd_key_button(ctx, keybuf, keybuf);
		}
		if (mu_button(ctx, "SPACE")){
			mui_vkbd_insert_text(ctx, " ");
		}
		if (mu_button(ctx, "BKSP")){
			mui_vkbd_backspace(ctx);
		}
		mu_layout_row(ctx, 5, (int[]){ mui_button_width("LEFT"), mui_button_width("RIGHT"), mui_button_width("ENTER"), mui_button_width("CLR"), -1 }, 24);
		if (mu_button(ctx, "LEFT")){
			mui_vkbd_move_left(ctx);
		}
		if (mu_button(ctx, "RIGHT")){
			mui_vkbd_move_right(ctx);
		}
		if (mu_button(ctx, "ENTER")){
			mui_vkbd_submit(ctx);
		}
		if (mu_button(ctx, "CLR")){
			if (mui_vkbd_buf != NULL){ mui_vkbd_buf[0] = 0; }
			ctx->textbox_view_id = mui_vkbd_buf_id;
			ctx->textbox_view_ofs = 0;
			mui_vkbd_focus_restore(ctx);
		}
		mu_label(ctx, "");
		mu_end_window(ctx);
	}
}

#else
int mui_textbox_with_vkbd(mu_Context* ctx, char* buf, int bufsz, int opt)
{
	return mu_textbox_ex(ctx, buf, bufsz, opt);
}
#endif

static auint CU_UNUSED_FN mui_preset_new_vdev(cu_vdev_type_t type, char const* name)
{
	auint id = cu_vdev_create(type, name);
	if (id == CU_VDEV_INVALID){ return id; }
	cu_vdev_set_name(id, name);
	return id;
}

static void CU_UNUSED_FN mui_preset_configure_pad(auint id, auint host_index)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){ return; }
	v->options = 0U;
	v->haptic_enabled = FALSE;
	v->haptic_accept_any = TRUE;
	v->haptic_binding = CU_VDEV_INVALID;
	v->haptic_id = 0U;
	(void)cu_vdev_set_binding(id, 0U, CU_VDEV_HOST_GAMECONTROLLER, host_index, CU_VDEV_BIND_BUTTONS);
	(void)cu_vdev_set_binding(id, 1U, CU_VDEV_HOST_NONE, CU_VDEV_INVALID, 0U);
}

static void CU_UNUSED_FN mui_preset_configure_keyboard(auint id);

static void CU_UNUSED_FN mui_preset_configure_supermouse(auint id, boole light_sense)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){ return; }
	v->options = CU_VDEV_OPT_REL_AXES | CU_VDEV_OPT_EXTRA_BTNS;
	if (light_sense){ v->options |= CU_VDEV_OPT_LIGHT_SENSE; }
	v->haptic_enabled = FALSE;
	v->haptic_accept_any = TRUE;
	v->haptic_binding = CU_VDEV_INVALID;
	v->haptic_id = 0U;
	v->sm_scale_x_pct = 100U;
	v->sm_scale_y_pct = 100U;
	v->sm_deadzone = 0U;
	v->sm_invert_x = FALSE;
	v->sm_invert_y = FALSE;
	(void)cu_vdev_set_binding(id, 0U, CU_VDEV_HOST_MOUSE, 0U, CU_VDEV_BIND_BUTTONS | CU_VDEV_BIND_AXES | (light_sense ? CU_VDEV_BIND_TRIGGER : 0U));
	(void)cu_vdev_set_binding(id, 1U, CU_VDEV_HOST_GAMECONTROLLER, 0U, CU_VDEV_BIND_BUTTONS);
}

static void mui_preset_configure_mouse(auint id, boole extra_buttons)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){ return; }
	v->options = CU_VDEV_OPT_REL_AXES;
	if (extra_buttons){ v->options |= CU_VDEV_OPT_EXTRA_BTNS; }
	v->haptic_enabled = FALSE;
	v->haptic_accept_any = TRUE;
	v->haptic_binding = CU_VDEV_INVALID;
	v->haptic_id = 0U;
	v->sm_scale_x_pct = 100U;
	v->sm_scale_y_pct = 100U;
	v->sm_deadzone = 0U;
	v->sm_invert_x = FALSE;
	v->sm_invert_y = FALSE;
	(void)cu_vdev_set_binding(id, 0U, CU_VDEV_HOST_MOUSE, 0U, CU_VDEV_BIND_BUTTONS | CU_VDEV_BIND_AXES);
	(void)cu_vdev_set_binding(id, 1U, CU_VDEV_HOST_NONE, CU_VDEV_INVALID, 0U);
}

static auint mui_input_layout_guess_mode(auint port)
{
	auint vdev_id;
	cu_vdev_t const* v;
	if (cu_multitap_get_tap_present(port)){ return 4U; }
	vdev_id = cu_multitap_get_slot_vdev(port, 0U);
	if (vdev_id == CU_VDEV_INVALID){ return 0U; }
	v = cu_vdev_get(vdev_id);
	if (v == NULL){ return 0U; }
	if (v->type == CU_VDEV_TYPE_KEYBOARD){ return 3U; }
	if (v->type == CU_VDEV_TYPE_SUPERMOUSE32){
		return (v->options & CU_VDEV_OPT_EXTRA_BTNS) ? 2U : 1U;
	}
	return 0U;
}

static void mui_input_layout_apply_mode(auint port, auint mode)
{
	auint i;
	auint id;
	char name[32];
	cu_multitap_set_tap_present(port, FALSE);
	cu_multitap_set_active_slot(port, 0U);
	for (i = 0U; i < CU_MULTITAP_MAX_SLOTS; ++i){
		cu_multitap_set_slot_vdev(port, i, CU_VDEV_INVALID);
	}
	switch (mode){
		case 0U:
			snprintf(name, sizeof(name), "P%u PAD", (unsigned)(port + 1U));
			id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, name);
			if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, port); cu_multitap_set_slot_vdev(port, 0U, id); }
			break;
		case 1U:
			snprintf(name, sizeof(name), "P%u MOUSE", (unsigned)(port + 1U));
			id = mui_preset_new_vdev(CU_VDEV_TYPE_SUPERMOUSE32, name);
			if (id != CU_VDEV_INVALID){ mui_preset_configure_mouse(id, FALSE); cu_multitap_set_slot_vdev(port, 0U, id); }
			break;
		case 2U:
			snprintf(name, sizeof(name), "P%u SUPERMOUSE", (unsigned)(port + 1U));
			id = mui_preset_new_vdev(CU_VDEV_TYPE_SUPERMOUSE32, name);
			if (id != CU_VDEV_INVALID){ mui_preset_configure_mouse(id, TRUE); cu_multitap_set_slot_vdev(port, 0U, id); }
			break;
		case 3U:
			snprintf(name, sizeof(name), "P%u KEYBOARD", (unsigned)(port + 1U));
			id = mui_preset_new_vdev(CU_VDEV_TYPE_KEYBOARD, name);
			if (id != CU_VDEV_INVALID){ mui_preset_configure_keyboard(id); cu_multitap_set_slot_vdev(port, 0U, id); }
			break;
		case 4U:
			cu_multitap_set_tap_present(port, TRUE);
			for (i = 0U; i < CU_MULTITAP_MAX_SLOTS; ++i){
				snprintf(name, sizeof(name), "P%u TAP PAD %u", (unsigned)(port + 1U), (unsigned)(i + 1U));
				id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, name);
				if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, (port * 4U) + i); cu_multitap_set_slot_vdev(port, i, id); }
			}
			break;
		default:
			break;
	}
	mainui_touch_config();
}

static void CU_UNUSED_FN mui_preset_configure_keyboard(auint id)
{
	cu_vdev_t* v = cu_vdev_get_mut(id);
	if (v == NULL){ return; }
	v->options = 0U;
	v->haptic_enabled = FALSE;
	v->haptic_accept_any = TRUE;
	v->haptic_binding = CU_VDEV_INVALID;
	v->haptic_id = 0U;
	(void)cu_vdev_set_binding(id, 0U, CU_VDEV_HOST_KEYBOARD, 0U, CU_VDEV_BIND_BUTTONS);
	(void)cu_vdev_set_binding(id, 1U, CU_VDEV_HOST_NONE, CU_VDEV_INVALID, 0U);
}

static void CU_UNUSED_FN mui_apply_topology_preset(auint preset_id)
{
	auint id;
	char const* status = "Preset applied";
	if (mui_vdev_learn_armed){ mui_vdev_cancel_learn("Learn cancelled"); }
	cu_multitap_reset();
	cu_vdev_reset();
	memset(mui_tap_debug_status, 0, sizeof(mui_tap_debug_status));
	memset(mui_kbd_debug_status, 0, sizeof(mui_kbd_debug_status));
	memset(mui_hap_debug_status, 0, sizeof(mui_hap_debug_status));
	mui_vdev_selected = CU_VDEV_INVALID;
	mui_vdev_editor_synced = FALSE;
	switch (preset_id){
		case 0U: /* empty */
			status = "Preset: empty topology";
			break;
		case 1U: /* 2 pads */
			id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, "P1 PAD");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, 0U); cu_multitap_set_slot_vdev(0U, 0U, id); mui_vdev_selected = id; }
			id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, "P2 PAD");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, 1U); cu_multitap_set_slot_vdev(1U, 0U, id); if (mui_vdev_selected == CU_VDEV_INVALID){ mui_vdev_selected = id; } }
			status = "Preset: 2 pads";
			break;
		case 2U: /* tap on P1, 4 pads */
			cu_multitap_set_tap_present(0U, TRUE);
			for (auint s = 0U; s < 4U; ++s){
				char name[32];
				snprintf(name, sizeof(name), "P1 TAP PAD %u", (unsigned)(s + 1U));
				id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, name);
				if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, s); cu_multitap_set_slot_vdev(0U, s, id); if (mui_vdev_selected == CU_VDEV_INVALID){ mui_vdev_selected = id; } }
			}
			status = "Preset: P1 tap with 4 pads";
			break;
		case 3U: /* dual tap 8 pads */
			cu_multitap_set_tap_present(0U, TRUE);
			cu_multitap_set_tap_present(1U, TRUE);
			for (auint port = 0U; port < 2U; ++port){
				for (auint s = 0U; s < 4U; ++s){
					char name[32];
					snprintf(name, sizeof(name), "P%u TAP PAD %u", (unsigned)(port + 1U), (unsigned)(s + 1U));
					id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, name);
					if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, (port * 4U) + s); cu_multitap_set_slot_vdev(port, s, id); if (mui_vdev_selected == CU_VDEV_INVALID){ mui_vdev_selected = id; } }
				}
			}
			status = "Preset: dual tap 8 pads";
			break;
		case 4U: /* keyboard on P1/S1 */
			cu_multitap_set_tap_present(0U, TRUE);
			id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, "P1 PAD");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, 0U); cu_multitap_set_slot_vdev(0U, 0U, id); mui_vdev_selected = id; }
			id = mui_preset_new_vdev(CU_VDEV_TYPE_KEYBOARD, "P1 KEYBOARD");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_keyboard(id); cu_multitap_set_slot_vdev(0U, 1U, id); if (mui_vdev_selected == CU_VDEV_INVALID){ mui_vdev_selected = id; } }
			id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, "P2 PAD");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, 1U); cu_multitap_set_slot_vdev(1U, 0U, id); }
			status = "Preset: keyboard on P1 slot 1";
			break;
		case 5U: /* gun + pad */
			id = mui_preset_new_vdev(CU_VDEV_TYPE_SUPERMOUSE32, "P1 GUN");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_supermouse(id, TRUE); cu_multitap_set_slot_vdev(0U, 0U, id); mui_vdev_selected = id; }
			id = mui_preset_new_vdev(CU_VDEV_TYPE_PAD16, "P2 PAD");
			if (id != CU_VDEV_INVALID){ mui_preset_configure_pad(id, 1U); cu_multitap_set_slot_vdev(1U, 0U, id); if (mui_vdev_selected == CU_VDEV_INVALID){ mui_vdev_selected = id; } }
			status = "Preset: lightgun on P1, pad on P2";
			break;
		default:
			status = "Unknown preset";
			break;
	}
	strncpy(mui_vdev_status, status, sizeof(mui_vdev_status) - 1U);
	mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
	strncpy(mui_preset_status, status, sizeof(mui_preset_status) - 1U);
	mui_preset_status[sizeof(mui_preset_status) - 1U] = 0;
	mainui_touch_config();
}

static void mui_sync_config_buffers(void)
{
	strncpy(mui_cfg_rom_path, mainui_get_rom_path(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_rom_path[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_screenshot_path, mainui_get_screenshot_path(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_screenshot_path[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_save_path, mainui_get_save_path(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_save_path[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_controllerdb_path, mainui_get_controllerdb_path(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_controllerdb_path[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_resident_bootloader_file, mainui_get_resident_bootloader_file(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_resident_bootloader_file[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_remote_host, mainui_get_remote_roms_host(), sizeof(mui_remote_host) - 1);
	mui_remote_host[sizeof(mui_remote_host) - 1] = 0;
	strncpy(mui_cfg_gui_theme_file, mainui_get_gui_theme_file(), sizeof(mui_cfg_gui_theme_file) - 1);
	mui_cfg_gui_theme_file[sizeof(mui_cfg_gui_theme_file) - 1] = 0;
#ifdef ENABLE_DEBUGGER
	strncpy(mui_dbg_symbols_file, mainui_debug_get_symbols_file(), sizeof(mui_dbg_symbols_file) - 1);
	mui_dbg_symbols_file[sizeof(mui_dbg_symbols_file) - 1] = 0;
#endif
	mui_cfg_paths_synced = TRUE;
}

static void mui_sync_serial_buffers(void)
{
	strncpy(mui_cfg_host_serial_device, mainui_get_host_serial_device_name(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_host_serial_device[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_host_midi_port, mainui_get_host_midi_port_name(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_host_midi_port[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_virtual_midi_port, mainui_get_virtual_midi_port_name(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_virtual_midi_port[MUI_CFG_PATH_CAP - 1] = 0;
	strncpy(mui_cfg_tcp_serial_host, mainui_get_tcp_serial_host(), MUI_CFG_PATH_CAP - 1);
	mui_cfg_tcp_serial_host[MUI_CFG_PATH_CAP - 1] = 0;
	snprintf(mui_cfg_tcp_serial_port, sizeof(mui_cfg_tcp_serial_port), "%u", (unsigned)mainui_get_tcp_serial_port());
	mui_cfg_tcp_serial_auto_reconnect = mainui_get_tcp_serial_auto_reconnect() ? 1 : 0;
	mui_cfg_tcp_serial_mode = (int)mainui_get_tcp_serial_mode();
	if (mui_cfg_tcp_serial_mode < 0 || mui_cfg_tcp_serial_mode > 4){ mui_cfg_tcp_serial_mode = 0; }
	snprintf(mui_cfg_link_room, sizeof(mui_cfg_link_room), "%s", cu_esp_link_get_room());
	snprintf(mui_cfg_link_relay_host, sizeof(mui_cfg_link_relay_host), "%s", cu_esp_link_get_relay_host());
	snprintf(mui_cfg_link_relay_port, sizeof(mui_cfg_link_relay_port), "%u", (unsigned)cu_esp_link_get_relay_port());
	mui_cfg_serial_synced = TRUE;
}

static char const* mui_uart_profile_name(auint profile)
{
	switch (profile){
		case CU_UART_PROFILE_FAST: return "FAST";
		case CU_UART_PROFILE_BALANCED: return "BALANCED";
		case CU_UART_PROFILE_DEBUG: return "DEBUG";
		default: return "FAST";
	}
}

static char const* mui_serial_route_name(auint route)
{
	switch (route){
		case CU_ESP_SERIAL_ESP_MODULE: return "ESP MODULE";
		case CU_ESP_SERIAL_HOST_SERIAL: return "HOST SERIAL";
		case CU_ESP_SERIAL_HOST_MIDI: return "HOST MIDI";
		case CU_ESP_SERIAL_VIRTUAL_MIDI: return "MIDI ENDPOINT";
	case CU_ESP_SERIAL_TCP_SERIAL: return "TCP SERIAL";
	case CU_ESP_SERIAL_LOOPBACK: return "LOOPBACK";
	default: return "DISCONNECTED";
	}
}

static const char* mui_serial_route_names[] = {
	"DISCONNECTED",
	"ESP MODULE",
	"HOST SERIAL",
	"HOST MIDI",
	"MIDI ENDPOINT",
	"TCP SERIAL",
	"LOOPBACK"
};

static const char* mui_spiram_pages_names[] = {
	".UZE HINT",
	"0-DISABLE",
	"1 PAGE (64K)",
	"2 PAGES (128K)",
	"4 PAGES (256K)",
	"8 PAGES (512K)",
	"16 PAGES (1M)",
	"32 PAGES (2M)",
	"64 PAGES (4M)",
	"128 PAGES (8M)"
};

static const char* mui_sd_timing_names[] = {
	"CUSTOM",
	"SLOW",
	"NORMAL",
	"FAST"
};

static const char* mui_esp_softap_mode_names[] = {
	".UZE HINT",
	"DISABLE",
	"ENABLE"
};

static const char* mui_esp_model_names[] = {
	"ESP8266",
	"ESP32",
	"ESP32-ETH01"
};

static const char* mui_esp_at_fw_names[] = {
	"NONOS AT 1.7 LEGACY",
	"ESP-AT 2.3.0.0"
};

static const char* mui_audio_monitor_width_names[] = {
	"0%",
	"25%",
	"50%",
	"75%",
	"100%",
	"125%",
	"150%",
	"175%",
	"200%"
};
static const auint mui_audio_monitor_width_values[] = { 0U, 25U, 50U, 75U, 100U, 125U, 150U, 175U, 200U };

static char const* mui_serial_esp_name(auint model)
{
	switch (model){
		case 1U: return "ESP8266";
		case 2U: return "ESP32";
		default: return "ESP32-ETH01";
	}
}


static char const* mui_virtual_midi_mode_name(auint mode)
{
	switch (mode){
		case CU_ESP_VIRTUAL_MIDI_INSTRUMENT: return "INSTRUMENT";
		case CU_ESP_VIRTUAL_MIDI_CONTROLLER: return "CONTROLLER";
		default: return "BIDIRECTIONAL";
	}
}

static char const* mui_tcp_serial_state_name(auint state)
{
	switch (state){
		case CU_ESP_TCP_SERIAL_STATE_CONNECTING: return "CONNECTING";
		case CU_ESP_TCP_SERIAL_STATE_CONNECTED: return "CONNECTED";
		case CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT: return "RETRY WAIT";
		case CU_ESP_TCP_SERIAL_STATE_LISTENING: return "LISTENING";
		case CU_ESP_TCP_SERIAL_STATE_SEARCHING: return "SEARCHING LAN";
		case CU_ESP_TCP_SERIAL_STATE_WAITING_PEER: return "WAITING FOR PLAYER";
		default: return "DISCONNECTED";
	}
}

static char const* mui_tcp_serial_mode_name(auint mode)
{
	if (mode == CU_ESP_TCP_SERIAL_MODE_INTERNET){ return "INTERNET"; }
	if (mode == CU_ESP_TCP_SERIAL_MODE_LAN){
		if (mainui_get_tcp_serial_state() == CU_ESP_TCP_SERIAL_STATE_SEARCHING){ return "LAN"; }
		return (cu_esp_get_tcp_serial_role() == CU_ESP_TCP_SERIAL_MODE_SERVER) ? "LAN (SERVER)" : "LAN (CLIENT)";
	}
	if (mode == CU_ESP_TCP_SERIAL_MODE_AUTO){
		return (cu_esp_get_tcp_serial_role() == CU_ESP_TCP_SERIAL_MODE_SERVER) ? "AUTO (SERVER)" : "AUTO (CLIENT)";
	}
	return (mode == CU_ESP_TCP_SERIAL_MODE_SERVER) ? "SERVER" : "CLIENT";
}

static auint mui_tcp_serial_cfg_mode(void)
{
	if (mui_cfg_tcp_serial_mode == 1){ return CU_ESP_TCP_SERIAL_MODE_SERVER; }
	if (mui_cfg_tcp_serial_mode == 2){ return CU_ESP_TCP_SERIAL_MODE_AUTO; }
	if (mui_cfg_tcp_serial_mode == 3){ return CU_ESP_TCP_SERIAL_MODE_INTERNET; }
	if (mui_cfg_tcp_serial_mode == 4){ return CU_ESP_TCP_SERIAL_MODE_LAN; }
	return CU_ESP_TCP_SERIAL_MODE_CLIENT;
}

static void mui_apply_tcp_serial_target(void)
{
	auint mode = mui_tcp_serial_cfg_mode();

	mainui_set_tcp_serial_host(mui_cfg_tcp_serial_host);
	mainui_set_tcp_serial_port((auint)strtoul(mui_cfg_tcp_serial_port, NULL, 10));
	mainui_set_tcp_serial_auto_reconnect(mui_cfg_tcp_serial_auto_reconnect ? TRUE : FALSE);
	mainui_set_tcp_serial_mode(mode);
	cu_esp_link_set_room(mui_cfg_link_room);
	cu_esp_link_set_relay_host(mui_cfg_link_relay_host);
	cu_esp_link_set_relay_port((auint)strtoul(mui_cfg_link_relay_port, NULL, 10));
	snprintf(mui_cfg_link_room, sizeof(mui_cfg_link_room), "%s", cu_esp_link_get_room());
	mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
	mainui_set_serial_route(CU_ESP_SERIAL_TCP_SERIAL);
	snprintf(mui_serial_status, sizeof(mui_serial_status), "%s",
		(mode == CU_ESP_TCP_SERIAL_MODE_SERVER) ? "TCP serial server starting" :
		(mode == CU_ESP_TCP_SERIAL_MODE_AUTO) ? "TCP serial auto pairing" :
		(mode == CU_ESP_TCP_SERIAL_MODE_INTERNET) ? "Internet link joining room" :
		(mode == CU_ESP_TCP_SERIAL_MODE_LAN) ? "Searching the LAN for a partner" : "TCP serial client connecting");
	if (mode == CU_ESP_TCP_SERIAL_MODE_INTERNET){
		mui_system_message("LINK: JOINING ROOM %s", cu_esp_link_get_room());
	}else{
		mui_system_message("TCP SERIAL %s",
			(mode == CU_ESP_TCP_SERIAL_MODE_SERVER) ? "SERVER STARTED" :
			(mode == CU_ESP_TCP_SERIAL_MODE_AUTO) ? "AUTO PAIRING" :
			(mode == CU_ESP_TCP_SERIAL_MODE_LAN) ? "LAN SEARCH" : "CLIENT CONNECTING");
	}
}

static void mui_disconnect_tcp_serial(void)
{
	mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
	snprintf(mui_serial_status, sizeof(mui_serial_status), "TCP serial disconnected");
	mui_system_message("TCP SERIAL DISCONNECTED");
}

static void mui_apply_tcp_serial_local_server(void)
{
	mui_cfg_tcp_serial_mode = 1;
	mui_cfg_tcp_serial_host[0] = 0;
	snprintf(mui_cfg_tcp_serial_port, sizeof(mui_cfg_tcp_serial_port), "12001");
	/* Keep listening until the second local instance is started. */
	mui_cfg_tcp_serial_auto_reconnect = 1;
	mui_apply_tcp_serial_target();
	mui_system_message("TCP SERIAL LOCAL SERVER :12001");
}

static void mui_apply_tcp_serial_local_auto(void)
{
	mui_cfg_tcp_serial_mode = 2;
	snprintf(mui_cfg_tcp_serial_host, sizeof(mui_cfg_tcp_serial_host), "127.0.0.1");
	snprintf(mui_cfg_tcp_serial_port, sizeof(mui_cfg_tcp_serial_port), "12001");
	mui_cfg_tcp_serial_auto_reconnect = 1;
	mui_apply_tcp_serial_target();
	mui_system_message("TCP SERIAL LOCAL AUTO 127.0.0.1:12001");
}

static void mui_link_random_room(void)
{
	static char const alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; /* no 0/O, 1/I */
	unsigned x = (unsigned)SDL_GetTicks() * 2654435761u ^ (unsigned)rand();
	int i;
	for (i = 0; i < 6; i++){
		mui_cfg_link_room[i] = alphabet[x % 32u];
		x = x * 1103515245u + 12345u;
		x ^= x >> 13;
	}
	mui_cfg_link_room[6] = 0;
}

static void mui_apply_tcp_serial_local_client(void)
{
	mui_cfg_tcp_serial_mode = 0;
	snprintf(mui_cfg_tcp_serial_host, sizeof(mui_cfg_tcp_serial_host), "127.0.0.1");
	snprintf(mui_cfg_tcp_serial_port, sizeof(mui_cfg_tcp_serial_port), "12001");
	/* Retry until the local server instance is ready. */
	mui_cfg_tcp_serial_auto_reconnect = 1;
	mui_apply_tcp_serial_target();
	mui_system_message("TCP SERIAL LOCAL CLIENT 127.0.0.1:12001");
}


static char const* mui_esp_wifi_mode_name(auint mode)
{
	switch (mode){
		case ESP_WIFI_MODE_STATION: return "STATION";
		case ESP_WIFI_MODE_SOFTAP: return "SOFTAP";
		case ESP_WIFI_MODE_SOFTAP_STATION: return "SOFTAP+STA";
		default: return "UNKNOWN";
	}
}

static char const* mui_esp_user_mode_name(auint mode)
{
	switch (mode){
		case ESP_USER_MODE_AT: return "AT";
		case ESP_USER_MODE_SEND: return "SEND";
		case ESP_USER_MODE_UNVARNISHED: return "UNVARNISHED";
		case ESP_USER_MODE_PASSTHROUGH: return "PASSTHROUGH";
		default: return "UNKNOWN";
	}
}

static char const* mui_esp_link_state_name(auint state)
{
	switch (state){
		case CU_ESP_LS_IDLE: return "IDLE";
		case CU_ESP_LS_DNS: return "DNS";
		case CU_ESP_LS_CONNECT: return "CONNECT";
		case CU_ESP_LS_DELAY: return "DELAY";
		case CU_ESP_LS_TLS: return "TLS";
		case CU_ESP_LS_OPEN: return "OPEN";
		case CU_ESP_LS_ERROR: return "ERROR";
		default: return "?";
	}
}

static char const* mui_esp_proto_name(auint proto)
{
	if ((proto & ESP_PROTO_SSL) != 0U){ return "SSL"; }
	if ((proto & ESP_PROTO_UDP) != 0U){ return "UDP"; }
	if ((proto & ESP_PROTO_TCP) != 0U){ return "TCP"; }
	return "-";
}

static auint mui_esp_count_discovered_aps(cu_state_esp_t const* es)
{
	auint i;
	auint count = 0U;
	if (es == NULL){ return 0U; }
	for (i = 0U; i < (auint)CU_ESP_LAN_MAX_APS; ++i){
		if (es->lan.aps[i].ssid[0] != 0){ ++count; }
	}
	return count;
}

static auint mui_esp_count_open_links(cu_state_esp_t const* es)
{
	auint i;
	auint count = 0U;
	if (es == NULL){ return 0U; }
	for (i = 0U; i < (auint)ESP_MAX_LINKS; ++i){
		if (es->link_state[i] == CU_ESP_LS_OPEN){ ++count; }
	}
	return count;
}

static void mui_esp_state_flags_string(cu_state_esp_t const* es, char* out, auint out_size)
{
	int used = 0;
	if (out_size == 0U){ return; }
	out[0] = 0;
	if (es == NULL){ snprintf(out, (size_t)out_size, "(no state)"); return; }
	if ((es->state & ESP_READY) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sREADY", (used != 0) ? " " : ""); }
	if ((es->state & ESP_START_UP) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sSTARTUP", (used != 0) ? " " : ""); }
	if ((es->state & ESP_ECHO) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sECHO", (used != 0) ? " " : ""); }
	if ((es->state & ESP_MUX) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sMUX", (used != 0) ? " " : ""); }
	if ((es->state & ESP_AP_CONNECTED) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sAP-CONN", (used != 0) ? " " : ""); }
	if ((es->state & ESP_INTERNET_ACCESS) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sINET", (used != 0) ? " " : ""); }
	if ((es->state & ESP_CIPDINFO) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sCIPDINFO", (used != 0) ? " " : ""); }
	if ((es->state & ESP_SMARTCONFIG_ACTIVE) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sSMARTCFG", (used != 0) ? " " : ""); }
	if ((es->state & ESP_AUTOCONNECT) != 0U){ used += snprintf(out + used, (size_t)((used < (int)out_size) ? (out_size - (auint)used) : 0U), "%sAUTOCONN", (used != 0) ? " " : ""); }
	if (out[0] == 0){ snprintf(out, (size_t)out_size, "(none)"); }
}


static char const* mui_render_path_name(auint mode)
{
	switch (mode){
		case RENDER_PATH_POTATO: return "POTATO";
		case RENDER_PATH_CLASSIC_1X: return "CLASSIC 1X";
		case RENDER_PATH_CLASSIC_2X: return "CLASSIC 2X";
		default: return "STAGED";
	}
}

static char const* mui_display_prefilter_name(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	switch (mode){
		case FILTER_PRE_SOFT_RGB: return "SOFT RGB";
		case FILTER_PRE_SVIDEO: return "S-VIDEO";
		case FILTER_PRE_COMPOSITE: return "COMPOSITE";
		case FILTER_PRE_RF_MOD: return "RF MOD";
		case FILTER_PRE_BLACK_WHITE: return "BLACK/WHITE";
		default: return "NONE";
	}
#else
	(void)mode;
	return "NONE";
#endif
}

static char const* const mui_gui_theme_names[] = { "CUSTOM", "BOOTLOADER", "NEUTRAL", "CHILL", "RAVE", "SNOW", "D3THADD3R", "CRT AMBER", "TERMINAL", "UZEBOX" };
static char const* const mui_gui_theme_files[] = { "themes/custom.cfg", "themes/bootloader.cfg", "themes/neutral.cfg", "themes/chill.cfg", "themes/rave.cfg", "themes/snow.cfg", "themes/D3thAdd3r.cfg", "themes/crt_amber.cfg", "themes/terminal_green.cfg", "themes/uzebox_classic.cfg" };

static auint mui_gui_theme_to_choice(char const* name)
{
	auint i;
	if (name == NULL){ return 2U; }
	for (i = 0U; i < (sizeof(mui_gui_theme_names) / sizeof(mui_gui_theme_names[0])); ++i){
		if (strcmp(name, mui_gui_theme_names[i]) == 0){ return i; }
	}
	return 2U;
}

static char const* mui_gui_theme_from_choice(auint choice)
{
	if (choice >= (sizeof(mui_gui_theme_names) / sizeof(mui_gui_theme_names[0]))){ return "NEUTRAL"; }
	return mui_gui_theme_names[choice];
}

static char const* mui_gui_theme_file_from_choice(auint choice)
{
	if (choice >= (sizeof(mui_gui_theme_files) / sizeof(mui_gui_theme_files[0]))){ return "themes/neutral.cfg"; }
	return mui_gui_theme_files[choice];
}

static char const* mui_display_scaler_name(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	switch (mode){
		case FILTER_SCALE_SCALE2X: return "SCALE2X";
		case FILTER_SCALE_HQ2X: return "HQ2X";
		case FILTER_SCALE_XBR2X: return "XBR2X";
		default: return "NEAREST";
	}
#else
	(void)mode;
	return "NEAREST";
#endif
}

static const auint mui_display_postfilter_values[] = {
	0U, 1U, 2U, 3U, 4U, 5U, 6U, 9U, 10U, 11U, 12U, 13U
};

static auint mui_display_postfilter_to_choice(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	auint i;
	for (i = 0U; i < (sizeof(mui_display_postfilter_values) / sizeof(mui_display_postfilter_values[0])); ++i){
		if (mui_display_postfilter_values[i] == mode){
			return i;
		}
	}
#endif
	return 0U;
}

static auint mui_display_postfilter_from_choice(auint choice)
{
#ifdef ENABLE_DISPLAY_FILTERS
	if (choice >= (sizeof(mui_display_postfilter_values) / sizeof(mui_display_postfilter_values[0]))){
		return 0U;
	}
	return mui_display_postfilter_values[choice];
#else
	(void)choice;
	return 0U;
#endif
}

static char const* mui_display_postfilter_name(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	static const char* filter_names[] = {
		"NONE",
		"SCANLINES 25%",
		"SCANLINES 37%",
		"SCANLINES 50%",
		"APERTURE GRILLE",
		"SHADOW MASK",
		"CURVATURE",
		"GRILLE + SCANLINES",
		"MASK + SCANLINES",
		"BUMP MAP",
		"LUCID",
		"HEATWAVE"
	};
	return filter_names[mui_display_postfilter_to_choice(mode)];
#else
	(void)mode;
	return "NONE";
#endif
}

static char const* mui_path_basename(char const* path)
{
	char const* a;
	char const* b;
	if (path == NULL){ return ""; }
	a = strrchr(path, '/');
	b = strrchr(path, '\\');
	if ((a == NULL) && (b == NULL)){ return path; }
	if (a == NULL){ return b + 1; }
	if (b == NULL){ return a + 1; }
	return ((a > b) ? a : b) + 1;
}

static void CU_UNUSED_FN mui_recent_sync_current_rom(void)
{
}

static boole mui_recent_launch_index(auint idx)
{
	char pathbuf[MUI_FILEDIALOG_PATH_CAP];
	if (idx >= MUI_RECENT_ROMS){ return FALSE; }
	if (!mainui_get_recent_rom_path(idx, pathbuf, sizeof(pathbuf))){ return FALSE; }
	if (!mainui_load_recent_rom(idx)){ return FALSE; }
	mui_state.show_quick = FALSE;
	mui_set_game_status(mui_path_basename(pathbuf));
	return TRUE;
}

static mui_window_desc_t const* mui_window_desc(mui_window_id_t id)
{
	if (((int)id < 0) || (id >= MUI_WIN_COUNT)){ return NULL; }
	return &mui_window_descs[id];
}

static boole* mui_window_show_ptr(mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	desc = mui_window_desc(id);
	if (desc == NULL){ return NULL; }
	return (boole*)(((char*)(&mui_state)) + desc->show_offset);
}

static mu_Container* mui_find_container(mu_Context* ctx, char const* title)
{
	mu_Id id;
	mu_Id last_id;
	int idx;
	int saved_idx;
	if ((ctx == NULL) || (title == NULL)){ return NULL; }
	saved_idx = ctx->id_stack.idx;
	last_id = ctx->last_id;
	ctx->id_stack.idx = 0;
	id = mu_get_id(ctx, title, (int)strlen(title));
	ctx->id_stack.idx = saved_idx;
	ctx->last_id = last_id;
	if (id == 0U){ id = (mu_Id)MUI_HASH_INITIAL; }
	idx = mu_pool_get(ctx, ctx->container_pool, MU_CONTAINERPOOL_SIZE, id);
	if (idx < 0){ return NULL; }
	return &ctx->containers[idx];
}

static void mui_window_sync(mu_Context* ctx, mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	boole* show_flag;
	mu_Container* cnt;
	desc = mui_window_desc(id);
	show_flag = mui_window_show_ptr(id);
	if ((desc == NULL) || (show_flag == NULL)){ return; }
	cnt = mui_find_container(ctx, desc->title);
	if (cnt != NULL){
		if (!cnt->open){ *show_flag = FALSE; }
	}else if (*show_flag && mui_window_seen_frame[id]){
		*show_flag = FALSE;
	}
}

static void mui_window_open(mu_Context* ctx, mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	boole* show_flag;
	desc = mui_window_desc(id);
	show_flag = mui_window_show_ptr(id);
	if ((desc == NULL) || (show_flag == NULL)){ return; }
	mui_open_window_flag(ctx, desc->title, show_flag);
}

static void mui_window_close(mu_Context* ctx, mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	boole* show_flag;
	mu_Container* cnt;
	desc = mui_window_desc(id);
	show_flag = mui_window_show_ptr(id);
	if ((desc == NULL) || (show_flag == NULL)){ return; }
	*show_flag = FALSE;
	if ((ctx == NULL) || (desc->title == NULL)){ return; }
	cnt = mui_find_container(ctx, desc->title);
	if (cnt != NULL){ cnt->open = 0; }
}

static boole mui_window_is_open(mu_Context* ctx, mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	boole* show_flag;
	desc = mui_window_desc(id);
	show_flag = mui_window_show_ptr(id);
	if ((desc == NULL) || (show_flag == NULL)){ return FALSE; }
	return mui_window_flag_is_open(ctx, desc->title, show_flag);
}

static void mui_window_toggle(mu_Context* ctx, mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	boole* show_flag;
	desc = mui_window_desc(id);
	show_flag = mui_window_show_ptr(id);
	if ((desc == NULL) || (show_flag == NULL)){ return; }
	mui_toggle_window_flag(ctx, desc->title, show_flag);
}

static boole mui_window_begin(mu_Context* ctx, mui_window_id_t id)
{
	mui_window_desc_t const* desc;
	boole* show_flag;
	desc = mui_window_desc(id);
	show_flag = mui_window_show_ptr(id);
	if ((desc == NULL) || (show_flag == NULL) || !(*show_flag)){ return FALSE; }
	mui_window_seen_frame[id] = TRUE;
	if (!mu_begin_window_ex(ctx, desc->title, desc->rect, desc->opts)){
		mui_window_sync(ctx, id);
		return FALSE;
	}
	return TRUE;
}

static void mui_window_end(mu_Context* ctx, mui_window_id_t id)
{
	mu_end_window(ctx);
	mui_window_sync(ctx, id);
}

static void mui_sync_window_open(mu_Context* ctx, char const* title, boole* show_flag)
{
	mu_Container* cnt;
	if ((ctx == NULL) || (title == NULL) || (show_flag == NULL)){ return; }
	cnt = mui_find_container(ctx, title);
	if ((cnt != NULL) && !cnt->open){ *show_flag = FALSE; }
}

static void mui_window_bring_front(mu_Context* ctx, char const* title)
{
	mu_Container* cnt;
	if ((ctx == NULL) || (title == NULL)){ return; }
	cnt = mui_find_container(ctx, title);
	if (cnt != NULL){
		mu_bring_to_front(ctx, cnt);
	}
}

static void mui_open_window_flag(mu_Context* ctx, char const* title, boole* show_flag)
{
	mu_Container* cnt;
	if (show_flag == NULL){ return; }
	*show_flag = TRUE;
	if ((ctx == NULL) || (title == NULL)){ return; }
	cnt = mui_find_container(ctx, title);
	if (cnt != NULL){
		cnt->open = 1;
		mu_bring_to_front(ctx, cnt);
	}
}

static boole mui_window_flag_is_open(mu_Context* ctx, char const* title, boole const* show_flag)
{
	mu_Container* cnt;
	if ((show_flag == NULL) || (!(*show_flag))){ return FALSE; }
	if ((ctx == NULL) || (title == NULL)){ return TRUE; }
	cnt = mui_find_container(ctx, title);
	if (cnt != NULL){ return cnt->open ? TRUE : FALSE; }
	return TRUE;
}

static void mui_toggle_window_flag(mu_Context* ctx, char const* title, boole* show_flag)
{
	if (show_flag == NULL){ return; }
	if (mui_window_flag_is_open(ctx, title, show_flag)){
		*show_flag = FALSE;
	}else{
		mui_open_window_flag(ctx, title, show_flag);
	}
}

static void mui_separator(mu_Context* ctx)
{
	mu_Rect line;
	int y;
	if ((ctx == NULL) || (ctx->style == NULL)){ return; }
	mu_layout_row(ctx, 1, (int[]){ -1 }, 8);
	line = mu_layout_next(ctx);
	y = line.y + (line.h / 2);
	mu_draw_rect(ctx,
	             mu_rect(line.x + 2, y, mu_max(0, line.w - 4), 1),
	             ctx->style->colors[MU_COLOR_BORDER]);
}

static void mui_build_vdev_choices(void)
{
	auint i;
	mui_vdev_choice_count = 0U;
	strncpy(mui_vdev_choice_labels[0], "<NONE>", sizeof(mui_vdev_choice_labels[0]) - 1U);
	mui_vdev_choice_labels[0][sizeof(mui_vdev_choice_labels[0]) - 1U] = 0;
	mui_vdev_choice_ptrs[0] = mui_vdev_choice_labels[0];
	mui_vdev_choice_ids[0] = CU_VDEV_INVALID;
	mui_vdev_choice_count = 1U;
	for (i = 0U; i < CU_VDEV_MAX; ++i){
		cu_vdev_t const* v = cu_vdev_get(i);
		if (v == NULL){ continue; }
		if (mui_vdev_choice_count >= MUI_VDEV_CHOICE_MAX){ break; }
		snprintf(mui_vdev_choice_labels[mui_vdev_choice_count], sizeof(mui_vdev_choice_labels[0]),
			"%u: %s (%s)", (unsigned)i, (v->name[0] != 0) ? v->name : "UNNAMED", cu_vdev_type_name(v->type));
		mui_vdev_choice_ptrs[mui_vdev_choice_count] = mui_vdev_choice_labels[mui_vdev_choice_count];
		mui_vdev_choice_ids[mui_vdev_choice_count] = i;
		mui_vdev_choice_count++;
	}
}

static auint mui_vdev_choice_index_from_id(auint id)
{
	auint i;
	for (i = 0U; i < mui_vdev_choice_count; ++i){
		if (mui_vdev_choice_ids[i] == id){ return i; }
	}
	return 0U;
}

static void mui_sync_vdev_editor(void)
{
	cu_vdev_t const* v;
	if (mui_vdev_selected == CU_VDEV_INVALID){
		mui_vdev_name[0] = 0;
		mui_vdev_editor_synced = TRUE;
		return;
	}
	v = cu_vdev_get(mui_vdev_selected);
	if (v == NULL){
		mui_vdev_selected = CU_VDEV_INVALID;
		mui_vdev_name[0] = 0;
		mui_vdev_editor_synced = TRUE;
		return;
	}
	strncpy(mui_vdev_name, v->name, sizeof(mui_vdev_name) - 1U);
	mui_vdev_name[sizeof(mui_vdev_name) - 1U] = 0;
	mui_vdev_editor_synced = TRUE;
}

static void mui_build_host_choices(void)
{
	int jcount;
	int j;
	mui_host_choice_count = 0U;
	strncpy(mui_host_choice_labels[0], "<NONE>", MUI_HOST_LABEL_MAX - 1U);
	mui_host_choice_labels[0][MUI_HOST_LABEL_MAX - 1U] = 0;
	mui_host_choice_ptrs[0] = mui_host_choice_labels[0];
	mui_host_choice_type[0] = CU_VDEV_HOST_NONE;
	mui_host_choice_index[0] = CU_VDEV_INVALID;
	mui_host_choice_count = 1U;
	strncpy(mui_host_choice_labels[mui_host_choice_count], "HOST KEYBOARD", MUI_HOST_LABEL_MAX - 1U);
	mui_host_choice_labels[mui_host_choice_count][MUI_HOST_LABEL_MAX - 1U] = 0;
	mui_host_choice_ptrs[mui_host_choice_count] = mui_host_choice_labels[mui_host_choice_count];
	mui_host_choice_type[mui_host_choice_count] = CU_VDEV_HOST_KEYBOARD;
	mui_host_choice_index[mui_host_choice_count] = 0U;
	mui_host_choice_count++;
	strncpy(mui_host_choice_labels[mui_host_choice_count], "HOST MOUSE", MUI_HOST_LABEL_MAX - 1U);
	mui_host_choice_labels[mui_host_choice_count][MUI_HOST_LABEL_MAX - 1U] = 0;
	mui_host_choice_ptrs[mui_host_choice_count] = mui_host_choice_labels[mui_host_choice_count];
	mui_host_choice_type[mui_host_choice_count] = CU_VDEV_HOST_MOUSE;
	mui_host_choice_index[mui_host_choice_count] = 0U;
	mui_host_choice_count++;
	jcount = SDL_NumJoysticks();
	for (j = 0; j < jcount; ++j){
		if (mui_host_choice_count >= MUI_HOST_CHOICE_MAX){ break; }
		if (SDL_IsGameController(j)){
			char const* name = SDL_GameControllerNameForIndex(j);
			snprintf(mui_host_choice_labels[mui_host_choice_count], MUI_HOST_LABEL_MAX, "PAD %d: %s", j, (name != NULL) ? name : "UNKNOWN CONTROLLER");
			mui_host_choice_ptrs[mui_host_choice_count] = mui_host_choice_labels[mui_host_choice_count];
			mui_host_choice_type[mui_host_choice_count] = CU_VDEV_HOST_GAMECONTROLLER;
			mui_host_choice_index[mui_host_choice_count] = (auint)j;
			mui_host_choice_count++;
		}else{
			char const* name = SDL_JoystickNameForIndex(j);
			snprintf(mui_host_choice_labels[mui_host_choice_count], MUI_HOST_LABEL_MAX, "JOY %d: %s", j, (name != NULL) ? name : "UNKNOWN JOYSTICK");
			mui_host_choice_ptrs[mui_host_choice_count] = mui_host_choice_labels[mui_host_choice_count];
			mui_host_choice_type[mui_host_choice_count] = CU_VDEV_HOST_JOYSTICK;
			mui_host_choice_index[mui_host_choice_count] = (auint)j;
			mui_host_choice_count++;
		}
	}
}

static auint mui_host_choice_from_binding(cu_vdev_binding_t const* bind)
{
	auint i;
	if (bind == NULL){ return 0U; }
	for (i = 0U; i < mui_host_choice_count; ++i){
		if ((mui_host_choice_type[i] == bind->type) && (mui_host_choice_index[i] == bind->host_index)){
			return i;
		}
	}
	return 0U;
}


static const char* const mui_low16_target_names[] = {
	"NONE", "B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT",
	"A", "X", "L", "R", "BIT12", "BIT13", "BIT14", "BIT15"
};

static const auint mui_low16_target_masks[] = {
	0U,
	CU_CTR_SNES_M_B, CU_CTR_SNES_M_Y, CU_CTR_SNES_M_SELECT, CU_CTR_SNES_M_START,
	CU_CTR_SNES_M_UP, CU_CTR_SNES_M_DOWN, CU_CTR_SNES_M_LEFT, CU_CTR_SNES_M_RIGHT,
	CU_CTR_SNES_M_A, CU_CTR_SNES_M_X, CU_CTR_SNES_M_LSH, CU_CTR_SNES_M_RSH,
	(1U << 12), (1U << 13), (1U << 14), (1U << 15)
};

static const char* const mui_src_labels_padish[CU_VDEV_REMAP_SLOTS] = {
	"SRC B", "SRC Y", "SRC SELECT", "SRC START", "SRC UP", "SRC DOWN",
	"SRC LEFT", "SRC RIGHT", "SRC A", "SRC X", "SRC L", "SRC R",
	"SRC 12", "SRC 13", "SRC 14", "SRC 15"
};

static const char* const mui_src_labels_mouse[5] = {
	"MOUSE LEFT", "MOUSE RIGHT", "MOUSE MIDDLE", "MOUSE X1", "MOUSE X2"
};

static const char* const mui_sm_scale_names[] = {
	"25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%"
};

static const auint mui_sm_scale_values[] = { 25U, 50U, 75U, 100U, 125U, 150U, 200U, 300U, 400U };

static const char* const mui_sm_deadzone_names[] = {
	"0", "1", "2", "4", "8", "12", "16", "24", "32"
};

static const auint mui_sm_deadzone_values[] = { 0U, 1U, 2U, 4U, 8U, 12U, 16U, 24U, 32U };

static auint mui_choice_from_value(auint value, auint const* values, auint count, auint fallback)
{
	for (auint i = 0U; i < count; ++i){
		if (values[i] == value){ return i; }
	}
	return fallback;
}

static auint mui_low16_choice_from_mask(auint mask)
{
	auint i;
	for (i = 0U; i < (sizeof(mui_low16_target_masks) / sizeof(mui_low16_target_masks[0])); ++i){
		if ((mui_low16_target_masks[i] & 0xFFFFU) == (mask & 0xFFFFU)){
			return i;
		}
	}
	return 0U;
}

static auint mui_binding_source_count(cu_vdev_host_type_t type)
{
	switch (type){
		case CU_VDEV_HOST_MOUSE: return 5U;
		case CU_VDEV_HOST_KEYBOARD:
		case CU_VDEV_HOST_GAMECONTROLLER:
		case CU_VDEV_HOST_JOYSTICK:
			return 12U;
		default:
			return 0U;
	}
}

static const char* mui_binding_source_label(cu_vdev_host_type_t type, auint index)
{
	if (type == CU_VDEV_HOST_MOUSE){
		if (index < 5U){ return mui_src_labels_mouse[index]; }
		return "MOUSE ?";
	}
	if (index < CU_VDEV_REMAP_SLOTS){ return mui_src_labels_padish[index]; }
	return "SRC ?";
}
static boole mui_binding_get_raw_mask(cu_vdev_binding_t const* bind, auint* mask)
{
	if ((bind == NULL) || (mask == NULL)){
		return FALSE;
	}
	switch (bind->type){
		case CU_VDEV_HOST_KEYBOARD:
			*mask = ginput_host_get_keyboard_buttons();
			return TRUE;
		case CU_VDEV_HOST_MOUSE:
			ginput_host_get_mouse_buttons(mask);
			return TRUE;
		case CU_VDEV_HOST_GAMECONTROLLER:
			return ginput_host_get_controller_buttons(bind->host_index, mask);
		default:
			*mask = 0U;
			return FALSE;
	}
}

static void mui_vdev_cancel_learn(char const* status)
{
	mui_vdev_learn_armed = FALSE;
	mui_vdev_learn_vdev = CU_VDEV_INVALID;
	mui_vdev_learn_bind = 0U;
	mui_vdev_learn_last_mask = 0U;
	if (status != NULL){
		strncpy(mui_vdev_status, status, sizeof(mui_vdev_status) - 1U);
		mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
	}
}

static void mui_vdev_poll_learn(void)
{
	cu_vdev_t* v;
	cu_vdev_binding_t* bind;
	auint raw_mask;
	auint src_count;
	auint new_bits;
	auint src;
	auint target_mask;
	if (!mui_vdev_learn_armed){ return; }
	if (mui_vdev_learn_vdev == CU_VDEV_INVALID){
		mui_vdev_cancel_learn("Learn cancelled");
		return;
	}
	v = cu_vdev_get_mut(mui_vdev_learn_vdev);
	if ((v == NULL) || (mui_vdev_learn_bind >= CU_VDEV_MAX_BINDINGS)){
		mui_vdev_cancel_learn("Learn cancelled");
		return;
	}
	bind = &v->binding[mui_vdev_learn_bind];
	if ((bind->flags & CU_VDEV_BIND_BUTTONS) == 0U){
		mui_vdev_cancel_learn("Learn cancelled: binding has no buttons");
		return;
	}
	if (!mui_binding_get_raw_mask(bind, &raw_mask)){
		mui_vdev_cancel_learn("Learn unavailable for this binding");
		return;
	}
	src_count = mui_binding_source_count(bind->type);
	if (src_count < CU_VDEV_REMAP_SLOTS){
		raw_mask &= ((1U << src_count) - 1U);
	}
	new_bits = raw_mask & (~mui_vdev_learn_last_mask);
	if (new_bits == 0U){
		mui_vdev_learn_last_mask = raw_mask;
		return;
	}
	for (src = 0U; src < src_count; ++src){
		if ((new_bits & (1U << src)) == 0U){ continue; }
		target_mask = mui_vdev_learn_target[mui_vdev_learn_bind] & 0xFFFFU;
		for (auint j = 0U; j < src_count; ++j){
			if (bind->map_low16[j] == target_mask){ bind->map_low16[j] = 0U; }
		}
		bind->map_low16[src] = target_mask;
		mainui_touch_config();
		snprintf(mui_vdev_status, sizeof(mui_vdev_status), "Learned %s -> %s", mui_binding_source_label(bind->type, src), mui_low16_target_names[mui_low16_choice_from_mask(target_mask)]);
		mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
		mui_vdev_learn_armed = FALSE;
		mui_vdev_learn_vdev = CU_VDEV_INVALID;
		return;
	}
	mui_vdev_learn_last_mask = raw_mask;
}


static void mui_draw_vdev_slot_popup(mu_Context* ctx, char const* label, char const* popup_id, auint* io_vdev)
{
	auint choice = mui_vdev_choice_index_from_id(*io_vdev);
	if (mui_labeled_choice_popup(ctx, label, popup_id, mui_vdev_choice_ptrs[choice], mui_vdev_choice_ptrs, mui_vdev_choice_count, &choice, 88, 220)){
		*io_vdev = mui_vdev_choice_ids[choice];
	}
}

static void mui_draw_multitap_vdev_ui(mu_Context* ctx)
{
	static const char* const slot_names[] = { "SLOT 0", "SLOT 1", "SLOT 2", "SLOT 3" };
	static const char* const vdev_type_names[] = { "PAD16", "SUPERMOUSE32", "KEYBOARD" };
	static const char* const haptic_names[] = { "NONE", "BIND A", "BIND B", "BOTH" };
	static const char* const haptic_id_names[] = { "0", "1", "2", "3", "4", "5", "6", "7" };
	char buf[128];
	auint i;
	int port;
	int state;
	boole cfg_changed = FALSE;
	cu_vdev_t* v;

	mui_build_vdev_choices();
	mui_build_host_choices();
	mui_vdev_poll_learn();
	if ((mui_vdev_selected != CU_VDEV_INVALID) && (cu_vdev_get(mui_vdev_selected) == NULL)){
		mui_vdev_selected = CU_VDEV_INVALID;
		mui_vdev_editor_synced = FALSE;
	}
	if ((mui_vdev_selected == CU_VDEV_INVALID) && (mui_vdev_choice_count > 1U)){
		mui_vdev_selected = mui_vdev_choice_ids[1U];
		mui_vdev_editor_synced = FALSE;
	}
	if (!mui_vdev_editor_synced){
		mui_sync_vdev_editor();
	}

	mui_separator(ctx);
	mu_label(ctx, "VIRTUAL INPUT TOPOLOGY");
	mu_label(ctx, "TAPS, SLOT ASSIGNMENT, HOST BINDINGS, AND LOW16 BUTTON REMAP ARE LIVE.");
	mu_layout_row(ctx, 2, (int[]){ 112, 112 }, 22);
	if (mu_button(ctx, "SAVE PROFILE")){ (void)mainui_input_profile_save_now(); }
	if (mu_button(ctx, "LOAD PROFILE")){ (void)mainui_input_profile_reload(); }
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, mainui_input_profile_get_status());
	mu_layout_row(ctx, 1, (int[]){ -1 }, 188);
	mu_begin_panel(ctx, "uzetap_ports_panel");
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, "PORT TOPOLOGY");
	for (port = 0; port < 2; ++port){
		auint active_slot;
		state = cu_multitap_get_tap_present((auint)port) ? 1 : 0;
		snprintf(buf, sizeof(buf), "PORT %d TAP PRESENT", port + 1);
		if (mu_checkbox(ctx, buf, &state)){
			cu_multitap_set_tap_present((auint)port, state ? 1U : 0U);
			cfg_changed = TRUE;
		}
		active_slot = cu_multitap_get_active_slot((auint)port);
		if (active_slot > 3U){ active_slot = 0U; }
		if (state){
			if (mui_labeled_choice_popup(ctx, "ACTIVE SLOT", (port == 0) ? "tap_active_p0" : "tap_active_p1", slot_names[active_slot], slot_names, 4U, &active_slot, 88, 120)){
				cu_multitap_set_active_slot((auint)port, active_slot);
				cfg_changed = TRUE;
			}
		}
		for (i = 0U; i < CU_MULTITAP_MAX_SLOTS; ++i){
			auint slot_vdev = cu_multitap_get_slot_vdev((auint)port, i);
			snprintf(buf, sizeof(buf), "SLOT %u", (unsigned)i);
			mu_push_id(ctx, &port, (int)sizeof(port));
			mu_push_id(ctx, &i, (int)sizeof(i));
			mui_draw_vdev_slot_popup(ctx, buf, "slot_vdev_popup", &slot_vdev);
			if (slot_vdev != cu_multitap_get_slot_vdev((auint)port, i)){
				cu_multitap_set_slot_vdev((auint)port, i, slot_vdev);
				cfg_changed = TRUE;
			}
			if (slot_vdev != CU_VDEV_INVALID){
				mu_layout_row(ctx, 1, (int[]){ 108 }, 20);
				if (mu_button(ctx, "EDIT SLOT VDEV")){
					mui_vdev_selected = slot_vdev;
					mui_vdev_editor_synced = FALSE;
				}
			}
			mu_pop_id(ctx);
			mu_pop_id(ctx);
		}
		{
			auint dbg_slot = cu_multitap_get_active_slot((auint)port);
			auint dbg_vdev = CU_VDEV_INVALID;
			cu_vdev_t const* dbg_dev = NULL;
			if (dbg_slot >= CU_MULTITAP_MAX_SLOTS){ dbg_slot = 0U; }
			dbg_vdev = cu_multitap_get_slot_vdev((auint)port, dbg_slot);
			if (dbg_vdev != CU_VDEV_INVALID){
				dbg_dev = cu_vdev_get(dbg_vdev);
			}
			snprintf(buf, sizeof(buf), "ACTIVE DEV: %s [%s]",
				(dbg_dev != NULL) ? dbg_dev->name : "(none)",
				(dbg_dev != NULL) ? cu_vdev_type_name(dbg_dev->type) : "None");
			mu_label(ctx, buf);
			mu_layout_row(ctx, 5, (int[]){ 60, 46, 46, 46, 46 }, 20);
			if (mu_button(ctx, "PROBE")){
				(void)mainui_multitap_probe_next_read((auint)port);
				snprintf(mui_tap_debug_status[port], sizeof(mui_tap_debug_status[port]),
					"Probe armed on P%u, next first byte = 0x%02X",
					(unsigned)(port + 1), (unsigned)cu_multitap_get_probe_byte((auint)port));
			}
			if (mu_button(ctx, "S0")){ (void)mainui_multitap_select_slot((auint)port, 0U); cfg_changed = TRUE; }
			if (mu_button(ctx, "S1")){ (void)mainui_multitap_select_slot((auint)port, 1U); cfg_changed = TRUE; }
			if (mu_button(ctx, "S2")){ (void)mainui_multitap_select_slot((auint)port, 2U); cfg_changed = TRUE; }
			if (mu_button(ctx, "S3")){ (void)mainui_multitap_select_slot((auint)port, 3U); cfg_changed = TRUE; }
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, mui_tap_debug_status[port]);
			if ((dbg_dev != NULL) && (dbg_dev->type == CU_VDEV_TYPE_KEYBOARD)){
				auint resp = 0U;
				mu_layout_row(ctx, 4, (int[]){ 62, 62, 62, 62 }, 20);
				if (mu_button(ctx, "Q A")){
					cu_kbd_debug_enqueue_port_byte((auint)port, 0x1CU);
					snprintf(mui_kbd_debug_status[port], sizeof(mui_kbd_debug_status[port]), "Queued make 0x1C on P%u", (unsigned)(port + 1));
				}
				if (mu_button(ctx, "Q BRK")){
					cu_kbd_debug_enqueue_port_byte((auint)port, 0xF0U);
					cu_kbd_debug_enqueue_port_byte((auint)port, 0x1CU);
					snprintf(mui_kbd_debug_status[port], sizeof(mui_kbd_debug_status[port]), "Queued break F0 1C on P%u", (unsigned)(port + 1));
				}
				if (mu_button(ctx, "GET")){
					resp = cu_kbd_response_for_port_command((auint)port, KBD_SEND_KEY);
					snprintf(mui_kbd_debug_status[port], sizeof(mui_kbd_debug_status[port]), "P%u GET KEY -> 0x%02X", (unsigned)(port + 1), (unsigned)(resp & 0xFFU));
				}
				if (mu_button(ctx, "ID")){
					resp = cu_kbd_response_for_port_command((auint)port, KBD_SEND_DEVICE_ID);
					snprintf(mui_kbd_debug_status[port], sizeof(mui_kbd_debug_status[port]), "P%u DEVICE_ID -> 0x%02X", (unsigned)(port + 1), (unsigned)(resp & 0xFFU));
				}
				mu_layout_row(ctx, 3, (int[]){ 62, 62, 128 }, 20);
				if (mu_button(ctx, "REV")){
					resp = cu_kbd_response_for_port_command((auint)port, KBD_SEND_FIRMWARE_REV);
					snprintf(mui_kbd_debug_status[port], sizeof(mui_kbd_debug_status[port]), "P%u FW_REV -> 0x%02X", (unsigned)(port + 1), (unsigned)(resp & 0xFFU));
				}
				if (mu_button(ctx, "RST")){
					resp = cu_kbd_response_for_port_command((auint)port, KBD_RESET);
					snprintf(mui_kbd_debug_status[port], sizeof(mui_kbd_debug_status[port]), "P%u RESET -> 0x%02X", (unsigned)(port + 1), (unsigned)(resp & 0xFFU));
				}
				mu_label(ctx, mui_kbd_debug_status[port]);
			}
			if ((dbg_dev != NULL) && dbg_dev->haptic_enabled){
				auint hap_id = dbg_dev->haptic_accept_any ? 0U : (dbg_dev->haptic_id & 0x07U);
				cu_state_hap_t* hapst = cu_hap_get_state((port == 0) ? CU_HAP_P1 : CU_HAP_P2);
				mu_layout_row(ctx, 4, (int[]){ 52, 52, 52, 52 }, 20);
				if (mu_button(ctx, "OFF")){ cu_hap_route_payload_port((auint)port, (uint8)(0x40U | (hap_id << 3U) | (CU_HAP_STATE_OFF << 1U))); }
				if (mu_button(ctx, "LARGE")){ cu_hap_route_payload_port((auint)port, (uint8)(0x40U | (hap_id << 3U) | (CU_HAP_STATE_LARGE << 1U))); }
				if (mu_button(ctx, "SMALL")){ cu_hap_route_payload_port((auint)port, (uint8)(0x40U | (hap_id << 3U) | (CU_HAP_STATE_SMALL << 1U))); }
				if (mu_button(ctx, "BOTH")){ cu_hap_route_payload_port((auint)port, (uint8)(0x40U | (hap_id << 3U) | (CU_HAP_STATE_BOTH << 1U))); }
				if (hapst != NULL){
					snprintf(mui_hap_debug_status[port], sizeof(mui_hap_debug_status[port]),
						"HAPTIC P%u id=%u state=%u mask=%u",
						(unsigned)(port + 1), (unsigned)hapst->last_id, (unsigned)hapst->last_state, (unsigned)hapst->last_mask);
				}
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, mui_hap_debug_status[port]);
			}
		}
		mui_separator(ctx);
	}
	mu_end_panel(ctx);
	mu_layout_row(ctx, 1, (int[]){ -1 }, 236);
	mu_begin_panel(ctx, "uzetap_vdev_editor_panel");
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, "VIRTUAL DEVICES");
	mu_layout_row(ctx, 5, (int[]){ 72, 96, 88, 80, 72 }, 24);
	if (mu_button(ctx, "NEW PAD")){
		auint id = cu_vdev_create(CU_VDEV_TYPE_PAD16, "PAD16");
		if (id != CU_VDEV_INVALID){
			cu_vdev_t* nv = cu_vdev_get_mut(id);
			if (nv != NULL){
				nv->options = 0U;
				nv->haptic_enabled = FALSE;
				nv->haptic_accept_any = TRUE;
				nv->haptic_binding = CU_VDEV_INVALID;
				nv->haptic_id = 0U;
			}
			mui_vdev_selected = id;
			mui_vdev_editor_synced = FALSE;
			snprintf(mui_vdev_name, sizeof(mui_vdev_name), "PAD16 %u", (unsigned)(id + 1U));
			cu_vdev_set_name(id, mui_vdev_name);
			cfg_changed = TRUE;
			strncpy(mui_vdev_status, "Created Pad16 virtual device", sizeof(mui_vdev_status) - 1U);
			mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
		}
	}
	if (mu_button(ctx, "NEW MOUSE")){
		auint id = cu_vdev_create(CU_VDEV_TYPE_SUPERMOUSE32, "SUPERMOUSE32");
		if (id != CU_VDEV_INVALID){
			cu_vdev_t* nv = cu_vdev_get_mut(id);
			if (nv != NULL){
				nv->options = CU_VDEV_OPT_REL_AXES | CU_VDEV_OPT_EXTRA_BTNS;
				nv->haptic_enabled = FALSE;
				nv->haptic_accept_any = TRUE;
				nv->haptic_binding = CU_VDEV_INVALID;
				nv->haptic_id = 0U;
			}
			mui_vdev_selected = id;
			mui_vdev_editor_synced = FALSE;
			snprintf(mui_vdev_name, sizeof(mui_vdev_name), "SUPERMOUSE32 %u", (unsigned)(id + 1U));
			cu_vdev_set_name(id, mui_vdev_name);
			cfg_changed = TRUE;
			strncpy(mui_vdev_status, "Created SuperMouse32 virtual device", sizeof(mui_vdev_status) - 1U);
			mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
		}
	}
	if (mu_button(ctx, "NEW KBD")){
		auint id = cu_vdev_create(CU_VDEV_TYPE_KEYBOARD, "KEYBOARD");
		if (id != CU_VDEV_INVALID){
			cu_vdev_t* nv = cu_vdev_get_mut(id);
			if (nv != NULL){
				nv->options = 0U;
				nv->haptic_enabled = FALSE;
				nv->haptic_accept_any = TRUE;
				nv->haptic_binding = CU_VDEV_INVALID;
				nv->haptic_id = 0U;
			}
			mui_vdev_selected = id;
			mui_vdev_editor_synced = FALSE;
			snprintf(mui_vdev_name, sizeof(mui_vdev_name), "KEYBOARD %u", (unsigned)(id + 1U));
			cu_vdev_set_name(id, mui_vdev_name);
			cfg_changed = TRUE;
			strncpy(mui_vdev_status, "Created keyboard virtual device", sizeof(mui_vdev_status) - 1U);
			mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
		}
	}
	if (mu_button(ctx, "DUPLICATE")){
		if (mui_vdev_selected != CU_VDEV_INVALID){
			auint id;
			char dup_name[CU_VDEV_NAME_MAX];
			cu_vdev_t const* srcv = cu_vdev_get(mui_vdev_selected);
			if (srcv != NULL){
				snprintf(dup_name, sizeof(dup_name), "%s COPY", srcv->name[0] ? srcv->name : "VDEV");
				id = cu_vdev_duplicate(mui_vdev_selected, dup_name);
				if (id != CU_VDEV_INVALID){
					mui_vdev_selected = id;
					mui_vdev_editor_synced = FALSE;
					cfg_changed = TRUE;
					strncpy(mui_vdev_status, "Duplicated virtual device", sizeof(mui_vdev_status) - 1U);
					mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
				}else{
					strncpy(mui_vdev_status, "No free virtual-device slots", sizeof(mui_vdev_status) - 1U);
					mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
				}
			}
		}
	}
	if (mu_button(ctx, "DELETE")){
		if (mui_vdev_selected != CU_VDEV_INVALID){
			if (mui_vdev_learn_armed && (mui_vdev_learn_vdev == mui_vdev_selected)){
				mui_vdev_cancel_learn("Learn cancelled");
			}
			for (port = 0; port < 2; ++port){
				for (i = 0U; i < CU_MULTITAP_MAX_SLOTS; ++i){
					if (cu_multitap_get_slot_vdev((auint)port, i) == mui_vdev_selected){
						cu_multitap_set_slot_vdev((auint)port, i, CU_VDEV_INVALID);
					}
				}
			}
			cu_vdev_delete(mui_vdev_selected);
			cfg_changed = TRUE;
			mui_vdev_selected = CU_VDEV_INVALID;
			mui_vdev_editor_synced = FALSE;
			strncpy(mui_vdev_status, "Deleted virtual device", sizeof(mui_vdev_status) - 1U);
			mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
		}
	}
	{
		auint selected_choice = mui_vdev_choice_index_from_id(mui_vdev_selected);
		if (mui_labeled_choice_popup(ctx, "SELECT", "vdev_select_popup", mui_vdev_choice_ptrs[selected_choice], mui_vdev_choice_ptrs, mui_vdev_choice_count, &selected_choice, 88, 220)){
			if (mui_vdev_learn_armed){ mui_vdev_cancel_learn("Learn cancelled"); }
			mui_vdev_selected = mui_vdev_choice_ids[selected_choice];
			mui_vdev_editor_synced = FALSE;
		}
	}
	v = (mui_vdev_selected == CU_VDEV_INVALID) ? NULL : cu_vdev_get_mut(mui_vdev_selected);
	if (v != NULL){
		auint type_choice = 0U;
		mu_layout_row(ctx, 2, (int[]){ 88, 220 }, 24);
		mu_label(ctx, "NAME");
		if (mu_textbox(ctx, mui_vdev_name, (int)sizeof(mui_vdev_name)) & MU_RES_CHANGE){
			cu_vdev_set_name(mui_vdev_selected, mui_vdev_name);
			cfg_changed = TRUE;
		}
		if (v->type == CU_VDEV_TYPE_SUPERMOUSE32){ type_choice = 1U; }
		else if (v->type == CU_VDEV_TYPE_KEYBOARD){ type_choice = 2U; }
		if (mui_labeled_choice_popup(ctx, "TYPE", "vdev_type_popup", vdev_type_names[type_choice], vdev_type_names, 3U, &type_choice, 88, 160)){
			v->type = (type_choice == 0U) ? CU_VDEV_TYPE_PAD16 : ((type_choice == 1U) ? CU_VDEV_TYPE_SUPERMOUSE32 : CU_VDEV_TYPE_KEYBOARD);
			cfg_changed = TRUE;
		}
		state = v->haptic_enabled ? 1 : 0;
		if (mu_checkbox(ctx, "HAPTIC CAPABLE", &state)){
			v->haptic_enabled = state ? TRUE : FALSE;
			cfg_changed = TRUE;
		}
		if (v->type == CU_VDEV_TYPE_SUPERMOUSE32){
			auint scale_choice;
			auint deadzone_choice;
			state = (v->options & CU_VDEV_OPT_REL_AXES) ? 1 : 0;
			if (mu_checkbox(ctx, "HAS RELATIVE AXES", &state)){ if (state) v->options |= CU_VDEV_OPT_REL_AXES; else v->options &= ~CU_VDEV_OPT_REL_AXES; cfg_changed = TRUE; }
			state = (v->options & CU_VDEV_OPT_EXTRA_BTNS) ? 1 : 0;
			if (mu_checkbox(ctx, "HAS EXTRA DIGITAL BUTTONS", &state)){ if (state) v->options |= CU_VDEV_OPT_EXTRA_BTNS; else v->options &= ~CU_VDEV_OPT_EXTRA_BTNS; cfg_changed = TRUE; }
			state = (v->options & CU_VDEV_OPT_LIGHT_SENSE) ? 1 : 0;
			if (mu_checkbox(ctx, "LIGHT SENSE (LIGHTGUN PROFILE)", &state)){ if (state) v->options |= CU_VDEV_OPT_LIGHT_SENSE; else v->options &= ~CU_VDEV_OPT_LIGHT_SENSE; cfg_changed = TRUE; }
			mui_separator(ctx);
			mu_label(ctx, "SUPERMOUSE TUNING");
			scale_choice = mui_choice_from_value(v->sm_scale_x_pct, mui_sm_scale_values, (auint)(sizeof(mui_sm_scale_values) / sizeof(mui_sm_scale_values[0])), 3U);
			if (mui_labeled_choice_popup(ctx, "X SCALE", "vdev_sm_scale_x", mui_sm_scale_names[scale_choice], mui_sm_scale_names, (auint)(sizeof(mui_sm_scale_names) / sizeof(mui_sm_scale_names[0])), &scale_choice, 88, 112)){
				v->sm_scale_x_pct = mui_sm_scale_values[scale_choice];
				cfg_changed = TRUE;
			}
			scale_choice = mui_choice_from_value(v->sm_scale_y_pct, mui_sm_scale_values, (auint)(sizeof(mui_sm_scale_values) / sizeof(mui_sm_scale_values[0])), 3U);
			if (mui_labeled_choice_popup(ctx, "Y SCALE", "vdev_sm_scale_y", mui_sm_scale_names[scale_choice], mui_sm_scale_names, (auint)(sizeof(mui_sm_scale_names) / sizeof(mui_sm_scale_names[0])), &scale_choice, 88, 112)){
				v->sm_scale_y_pct = mui_sm_scale_values[scale_choice];
				cfg_changed = TRUE;
			}
			deadzone_choice = mui_choice_from_value(v->sm_deadzone, mui_sm_deadzone_values, (auint)(sizeof(mui_sm_deadzone_values) / sizeof(mui_sm_deadzone_values[0])), 0U);
			if (mui_labeled_choice_popup(ctx, "DEADZONE", "vdev_sm_deadzone", mui_sm_deadzone_names[deadzone_choice], mui_sm_deadzone_names, (auint)(sizeof(mui_sm_deadzone_names) / sizeof(mui_sm_deadzone_names[0])), &deadzone_choice, 88, 112)){
				v->sm_deadzone = mui_sm_deadzone_values[deadzone_choice];
				cfg_changed = TRUE;
			}
			state = v->sm_invert_x ? 1 : 0;
			if (mu_checkbox(ctx, "INVERT X", &state)){ v->sm_invert_x = state ? TRUE : FALSE; cfg_changed = TRUE; }
			state = v->sm_invert_y ? 1 : 0;
			if (mu_checkbox(ctx, "INVERT Y", &state)){ v->sm_invert_y = state ? TRUE : FALSE; cfg_changed = TRUE; }
			mu_layout_row(ctx, 1, (int[]){ 128 }, 20);
			if (mu_button(ctx, "RESET TUNING")){
				(void)cu_vdev_reset_supermouse_tuning(mui_vdev_selected);
				cfg_changed = TRUE;
			}
		}
		mui_separator(ctx);
		mu_label(ctx, "HOST BINDINGS");
		mu_layout_row(ctx, 2, (int[]){ 104, 104 }, 20);
		if (mu_button(ctx, "COPY A->B")){
			v->binding[1] = v->binding[0];
			cfg_changed = TRUE;
		}
		if (mu_button(ctx, "COPY B->A")){
			v->binding[0] = v->binding[1];
			cfg_changed = TRUE;
		}
		for (i = 0U; i < CU_VDEV_MAX_BINDINGS; ++i){
			auint host_choice = mui_host_choice_from_binding(&v->binding[i]);
			char popup_id[32];
			snprintf(buf, sizeof(buf), "BIND %c", (int)('A' + (int)i));
			snprintf(popup_id, sizeof(popup_id), "vdev_bind_%u", (unsigned)i);
			if (mui_labeled_choice_popup(ctx, buf, popup_id, mui_host_choice_ptrs[host_choice], mui_host_choice_ptrs, mui_host_choice_count, &host_choice, 88, 220)){
				cu_vdev_set_binding(mui_vdev_selected, i, mui_host_choice_type[host_choice], mui_host_choice_index[host_choice], v->binding[i].flags);
				cfg_changed = TRUE;
			}
			mu_layout_row(ctx, 4, (int[]){ 72, 72, 72, 88 }, 20);
			state = (v->binding[i].flags & CU_VDEV_BIND_BUTTONS) ? 1 : 0;
			if (mu_checkbox(ctx, "BUTTONS", &state)){ if (state) v->binding[i].flags |= CU_VDEV_BIND_BUTTONS; else v->binding[i].flags &= ~CU_VDEV_BIND_BUTTONS; cfg_changed = TRUE; }
			state = (v->binding[i].flags & CU_VDEV_BIND_AXES) ? 1 : 0;
			if (mu_checkbox(ctx, "AXES", &state)){ if (state) v->binding[i].flags |= CU_VDEV_BIND_AXES; else v->binding[i].flags &= ~CU_VDEV_BIND_AXES; cfg_changed = TRUE; }
			state = (v->binding[i].flags & CU_VDEV_BIND_TRIGGER) ? 1 : 0;
			if (mu_checkbox(ctx, "TRIGGER", &state)){ if (state) v->binding[i].flags |= CU_VDEV_BIND_TRIGGER; else v->binding[i].flags &= ~CU_VDEV_BIND_TRIGGER; cfg_changed = TRUE; }
			state = (v->binding[i].flags & CU_VDEV_BIND_HAPTIC) ? 1 : 0;
			if (mu_checkbox(ctx, "HAPTIC", &state)){ if (state) v->binding[i].flags |= CU_VDEV_BIND_HAPTIC; else v->binding[i].flags &= ~CU_VDEV_BIND_HAPTIC; cfg_changed = TRUE; }
			if ((v->binding[i].type != CU_VDEV_HOST_NONE) && ((v->binding[i].flags & CU_VDEV_BIND_BUTTONS) != 0U)){
				auint src_count = mui_binding_source_count(v->binding[i].type);
				auint src;
				auint learn_choice = mui_low16_choice_from_mask(mui_vdev_learn_target[i]);
				char popup_id3[40];
				mui_separator(ctx);
				snprintf(buf, sizeof(buf), "LOW16 REMAP %c", (int)('A' + (int)i));
				mu_label(ctx, buf);
				snprintf(popup_id3, sizeof(popup_id3), "vdev_learn_target_%u", (unsigned)i);
				if (mui_labeled_choice_popup(ctx, "LEARN TARGET", popup_id3, mui_low16_target_names[learn_choice], mui_low16_target_names, (auint)(sizeof(mui_low16_target_names) / sizeof(mui_low16_target_names[0])), &learn_choice, 112, 128)){
					mui_vdev_learn_target[i] = mui_low16_target_masks[learn_choice] & 0xFFFFU;
				}
				mu_layout_row(ctx, 2, (int[]){ 88, 88 }, 20);
				if ((mui_vdev_learn_armed) && (mui_vdev_learn_vdev == mui_vdev_selected) && (mui_vdev_learn_bind == i)){
					if (mu_button(ctx, "CANCEL")){
						mui_vdev_cancel_learn("Learn cancelled");
					}
					mu_label(ctx, "PRESS A BUTTON");
				}else{
					if (mu_button(ctx, "LEARN")){
						auint raw_mask = 0U;
						if (mui_binding_get_raw_mask(&v->binding[i], &raw_mask)){
							if (src_count < CU_VDEV_REMAP_SLOTS){ raw_mask &= ((1U << src_count) - 1U); }
							mui_vdev_learn_armed = TRUE;
							mui_vdev_learn_vdev = mui_vdev_selected;
							mui_vdev_learn_bind = i;
							mui_vdev_learn_last_mask = raw_mask;
							snprintf(mui_vdev_status, sizeof(mui_vdev_status), "Learning %s for binding %c", mui_low16_target_names[mui_low16_choice_from_mask(mui_vdev_learn_target[i])], (int)('A' + (int)i));
							mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
						}else{
							strncpy(mui_vdev_status, "Learn unavailable for this binding", sizeof(mui_vdev_status) - 1U);
							mui_vdev_status[sizeof(mui_vdev_status) - 1U] = 0;
						}
					}
					mu_label(ctx, "");
				}
				for (src = 0U; src < src_count; ++src){
					auint map_choice = mui_low16_choice_from_mask(v->binding[i].map_low16[src]);
					char popup_id2[40];
					snprintf(popup_id2, sizeof(popup_id2), "vdev_map_%u_%u", (unsigned)i, (unsigned)src);
					if (mui_labeled_choice_popup(ctx, mui_binding_source_label(v->binding[i].type, src), popup_id2, mui_low16_target_names[map_choice], mui_low16_target_names, (auint)(sizeof(mui_low16_target_names) / sizeof(mui_low16_target_names[0])), &map_choice, 112, 128)){
						v->binding[i].map_low16[src] = mui_low16_target_masks[map_choice] & 0xFFFFU;
						cfg_changed = TRUE;
					}
				}
				mu_layout_row(ctx, 1, (int[]){ 112 }, 20);
				if (mu_button(ctx, "RESET MAP")){
					(void)cu_vdev_reset_binding_map(mui_vdev_selected, i);
					cfg_changed = TRUE;
				}
			}
		}
		if (v->haptic_enabled){
			auint haptic_choice = (v->haptic_binding <= 3U) ? v->haptic_binding : 0U;
			if (mui_labeled_choice_popup(ctx, "HAPTIC OUT", "vdev_haptic_out", haptic_names[haptic_choice], haptic_names, 4U, &haptic_choice, 88, 120)){
				v->haptic_binding = haptic_choice;
				cfg_changed = TRUE;
			}
			state = v->haptic_accept_any ? 1 : 0;
			if (mu_checkbox(ctx, "HAPTIC ACCEPT ANY ID", &state)){
				v->haptic_accept_any = state ? TRUE : FALSE;
				cfg_changed = TRUE;
			}
			if (!v->haptic_accept_any){
				auint haptic_id_choice = v->haptic_id & 0x07U;
				if (mui_labeled_choice_popup(ctx, "HAPTIC ID", "vdev_haptic_id", haptic_id_names[haptic_id_choice], haptic_id_names, 8U, &haptic_id_choice, 88, 96)){
					v->haptic_id = haptic_id_choice & 0x07U;
					cfg_changed = TRUE;
				}
			}
		}
	}
	if (cfg_changed){
		mainui_touch_config();
	}
	mu_label(ctx, mui_vdev_status);
	mu_end_panel(ctx);
}

static mui_window_id_t mui_cfg_page_to_window_id(int page)
{
	switch (page){
		case 0: return MUI_WIN_CFG_VIDEO;
		case 1: return MUI_WIN_CFG_INPUT;
		case 2: return MUI_WIN_CFG_DEVICES;
		case 3: return MUI_WIN_CFG_AUDIO;
		case 4: return MUI_WIN_CFG_PATHS;
		default: return MUI_WIN_CFG_GUI;
	}
}

static boole mui_any_config_page_open(mu_Context* ctx)
{
	return (mui_window_is_open(ctx, MUI_WIN_CFG_VIDEO) ||
	        mui_window_is_open(ctx, MUI_WIN_CFG_INPUT) ||
	        mui_window_is_open(ctx, MUI_WIN_CFG_DEVICES) ||
	        mui_window_is_open(ctx, MUI_WIN_CFG_AUDIO) ||
	        mui_window_is_open(ctx, MUI_WIN_CFG_PATHS) ||
	        mui_window_is_open(ctx, MUI_WIN_CFG_GUI));
}

static void mui_close_config_pages(mu_Context* ctx)
{
	mui_window_close(ctx, MUI_WIN_CFG_VIDEO);
	mui_window_close(ctx, MUI_WIN_CFG_INPUT);
	mui_window_close(ctx, MUI_WIN_CFG_DEVICES);
	mui_window_close(ctx, MUI_WIN_SD_WRITE_WARNING);
	mui_window_close(ctx, MUI_WIN_CFG_AUDIO);
	mui_window_close(ctx, MUI_WIN_CFG_PATHS);
	mui_window_close(ctx, MUI_WIN_CFG_GUI);
}

static void mui_open_config_page(mu_Context* ctx, int page)
{
	mui_cfg_page = page;
	mui_close_config_pages(ctx);
	mui_window_close(ctx, MUI_WIN_CONFIG);
	mui_window_open(ctx, mui_cfg_page_to_window_id(page));
}

#ifdef ENABLE_NETPLAY
static auint mui_parse_u32_field(char const *text, auint defv, auint maxv)
{
	char *endp;
	unsigned long v;
	if ((text == NULL) || (text[0] == 0)){
		return defv;
	}
	v = strtoul(text, &endp, 0);
	if ((endp == text) || (*endp != 0)){
		return defv;
	}
	if ((maxv != 0U) && (v > (unsigned long)maxv)){
		v = (unsigned long)maxv;
	}
	return (auint)v;
}

static char const* mui_np_sync_name(int mode)
{
	switch (mode){
		case NETPLAY_ROM_SYNC_OFF: return "Off";
		case NETPLAY_ROM_SYNC_MISSING: return "Missing";
		case NETPLAY_ROM_SYNC_MISMATCH: return "Mismatch";
		case NETPLAY_ROM_SYNC_ALWAYS: return "Always";
		default: return "?";
	}
}

static char const* mui_np_af_name(boole ipv6)
{
	return ipv6 ? "IPV6" : "IPV4";
}

static void mui_np_format_transport(char *dst, size_t dsz, netplay_status_t const *st)
{
	char const *af;
	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	dst[0] = 0;
	if (st == NULL){
		snprintf(dst, dsz, "TRANSPORT OFF");
		return;
	}
	if (!st->enabled){
		snprintf(dst, dsz, "TRANSPORT OFF");
		return;
	}
	af = mui_np_af_name(st->peer_addr_ipv6 ? TRUE : st->socket_ipv6);
	if (st->relay_mode){
		if (st->relay_direct_established){
			snprintf(dst, dsz, "TRANSPORT DIRECT %s VIA RELAY", af);
		}else{
			snprintf(dst, dsz, "TRANSPORT RELAY %s", mui_np_af_name(st->relay_addr_ipv6));
		}
	}else{
		snprintf(dst, dsz, "TRANSPORT DIRECT UDP %s", af);
	}
}

static char const* mui_lobby_owner_name(auint owner) CU_UNUSED_FN;

static char const* mui_lobby_owner_name(auint owner)
{
	switch (owner){
		case NETPLAY_LOBBY_OWNER_HOST: return "HOST";
		case NETPLAY_LOBBY_OWNER_GUEST: return "GUEST";
		default: return "NONE";
	}
}

static char const* mui_lobby_party_name(netplay_lobby_status_t const *lb, auint owner)
{
	if (lb == NULL){
		return "";
	}
	if (owner == NETPLAY_LOBBY_OWNER_NONE){
		return "UNASSIGNED";
	}
	if (lb->is_host){
		if (owner == NETPLAY_LOBBY_OWNER_HOST){
			return lb->local_name[0] ? lb->local_name : "HOST";
		}
		return lb->peer_name[0] ? lb->peer_name : "GUEST";
	}
	if (owner == NETPLAY_LOBBY_OWNER_GUEST){
		return lb->local_name[0] ? lb->local_name : "GUEST";
	}
	return lb->peer_name[0] ? lb->peer_name : "HOST";
}

static char const* mui_lobby_pad_label(netplay_lobby_status_t const *lb, auint owner, auint pad)
{
	if ((lb == NULL) || (pad >= ROLLBACK_MAX_PLAYERS)){
		return "Pad";
	}
	if (lb->is_host){
		if (owner == NETPLAY_LOBBY_OWNER_HOST){
			return lb->local_pad_labels[pad][0] ? lb->local_pad_labels[pad] : "Pad";
		}
		return lb->peer_pad_labels[pad][0] ? lb->peer_pad_labels[pad] : "Pad";
	}
	if (owner == NETPLAY_LOBBY_OWNER_GUEST){
		return lb->local_pad_labels[pad][0] ? lb->local_pad_labels[pad] : "Pad";
	}
	return lb->peer_pad_labels[pad][0] ? lb->peer_pad_labels[pad] : "Pad";
}

static void mui_lobby_format_seat(char *dst, size_t dsz, netplay_lobby_status_t const *lb, auint seat, auint owner, auint pad)
{
	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	if (owner == NETPLAY_LOBBY_OWNER_NONE){
		snprintf(dst, dsz, "P%u -> UNASSIGNED", (unsigned)(seat + 1U));
	}else{
		snprintf(dst, dsz, "P%u -> %s / %s", (unsigned)(seat + 1U), mui_lobby_party_name(lb, owner), mui_lobby_pad_label(lb, owner, pad));
	}
}

static int mui_disabled_button(mu_Context* ctx, const char* label)
{
	mu_Rect r = mu_layout_next(ctx);
	mu_draw_control_frame(ctx, 0U, r, MU_COLOR_BASE, 0);
	mu_draw_control_text(ctx, label, r, MU_COLOR_BORDER, MU_OPT_ALIGNCENTER);
	return 0;
}

#ifdef ENABLE_NETPLAY
static void mui_np_refresh_interfaces(void)
{
	netplay_config_t cfg;
	auint i;
	(void)netplay_refresh_interfaces();
	memset(mui_np_iface_labels, 0, sizeof(mui_np_iface_labels));
	memset(mui_np_iface_name_ptrs, 0, sizeof(mui_np_iface_name_ptrs));
	strncpy(mui_np_iface_labels[0], "AUTO", sizeof(mui_np_iface_labels[0]) - 1U);
	mui_np_iface_name_ptrs[0] = mui_np_iface_labels[0];
	mui_np_iface_count = 1U;
	for (i = 0U; (i < netplay_get_interface_count()) && (mui_np_iface_count < (1U + NETPLAY_IFACE_MAX)); ++i){
		strncpy(mui_np_iface_labels[mui_np_iface_count], netplay_get_interface_label(i), sizeof(mui_np_iface_labels[mui_np_iface_count]) - 1U);
		mui_np_iface_labels[mui_np_iface_count][sizeof(mui_np_iface_labels[mui_np_iface_count]) - 1U] = 0;
		mui_np_iface_name_ptrs[mui_np_iface_count] = mui_np_iface_labels[mui_np_iface_count];
		mui_np_iface_count++;
	}
	netplay_get_config(&cfg);
	mui_np_iface_choice = netplay_find_interface_value(cfg.network_interface);
	if (mui_np_iface_choice >= mui_np_iface_count){
		mui_np_iface_choice = 0U;
	}
}
#endif

static void mui_sync_netplay_buffers(void)
{
	netplay_config_t cfg;
	netplay_get_config(&cfg);
	{
		auint i;
		for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
			mui_np_local_mask_bits[i] = ((cfg.local_player_mask & ((uint32)1UL << i)) != 0U) ? 1 : 0;
		}
	}
	snprintf(mui_np_max_players, sizeof(mui_np_max_players), "%u", (unsigned)cfg.max_players);
	snprintf(mui_np_tcp_port, sizeof(mui_np_tcp_port), "%u", (unsigned)cfg.rom_tcp_port);
	snprintf(mui_np_max_rom_mb, sizeof(mui_np_max_rom_mb), "%u", (unsigned)((cfg.rom_max_size + (1024U * 1024U - 1U)) / (1024U * 1024U)));
	snprintf(mui_np_rb_window, sizeof(mui_np_rb_window), "%u", (unsigned)mainui_get_netplay_rollback_window());
	snprintf(mui_np_input_delay, sizeof(mui_np_input_delay), "%u", (unsigned)mainui_get_netplay_input_delay());
	snprintf(mui_np_relay_port, sizeof(mui_np_relay_port), "%u", (unsigned)(cfg.relay_server_port ? cfg.relay_server_port : 43810U));
	if (cfg.relay_server_host[0] != 0){
		memcpy(mui_np_relay_host, cfg.relay_server_host, sizeof(mui_np_relay_host));
	}else{
		strncpy(mui_np_relay_host, "uzenet.us", sizeof(mui_np_relay_host) - 1U);
		mui_np_relay_host[sizeof(mui_np_relay_host) - 1U] = 0;
	}
	memcpy(mui_np_name, cfg.local_name, sizeof(mui_np_name));
	memcpy(mui_np_pad_labels, cfg.local_pad_labels, sizeof(mui_np_pad_labels));
	mui_np_refresh_interfaces();
	mui_np_send_rom = cfg.rom_send_enabled ? 1 : 0;
	mui_np_recv_rom = cfg.rom_receive_enabled ? 1 : 0;
	mui_np_sync_mode = (int)cfg.rom_sync_mode;
	mui_np_synced = TRUE;
}

static void mui_apply_netplay_config(void)
{
	netplay_config_t cfg;
	netplay_get_config(&cfg);
	uint32 mask = 0U;
	auint i;
	cfg.max_players = mui_parse_u32_field(mui_np_max_players, cfg.max_players ? cfg.max_players : 2U, 255U);
	if (cfg.max_players == 0U){ cfg.max_players = 2U; }
	if (cfg.max_players > ROLLBACK_MAX_PLAYERS){ cfg.max_players = ROLLBACK_MAX_PLAYERS; }
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		if ((i < cfg.max_players) && (mui_np_local_mask_bits[i] != 0)){
			mask |= ((uint32)1UL << i);
		}
	}
	if (mask == 0U){
		mask = 1UL;
		mui_np_local_mask_bits[0] = 1;
	}
	cfg.local_player_mask = mask;
	cfg.local_player = 1U;
	for (i = 0U; i < cfg.max_players; i++){
		if ((mask & ((uint32)1UL << i)) != 0U){
			cfg.local_player = i + 1U;
			break;
		}
	}
	cfg.rom_tcp_port = mui_parse_u32_field(mui_np_tcp_port, cfg.rom_tcp_port, 65535U);
	cfg.rom_max_size = mui_parse_u32_field(mui_np_max_rom_mb, (cfg.rom_max_size + (1024U * 1024U - 1U)) / (1024U * 1024U), 1024U) * 1024U * 1024U;
	cfg.rom_send_enabled = (mui_np_send_rom != 0) ? TRUE : FALSE;
	cfg.rom_receive_enabled = (mui_np_recv_rom != 0) ? TRUE : FALSE;
	cfg.rom_sync_mode = (netplay_rom_sync_mode_t)mui_np_sync_mode;
	cfg.relay_server_port = mui_parse_u32_field(mui_np_relay_port, cfg.relay_server_port ? cfg.relay_server_port : 43810U, 65535U);
	cfg.relay_allow_direct = TRUE;
	cfg.relay_allow_relay = TRUE;
	if (mui_np_relay_host[0] != 0){
		memcpy(cfg.relay_server_host, mui_np_relay_host, sizeof(cfg.relay_server_host));
	}else{
		strncpy(cfg.relay_server_host, "uzenet.us", sizeof(cfg.relay_server_host) - 1U);
		cfg.relay_server_host[sizeof(cfg.relay_server_host) - 1U] = 0;
	}
	if ((mui_np_iface_choice > 0U) && (mui_np_iface_choice <= netplay_get_interface_count())){
		strncpy(cfg.network_interface, netplay_get_interface_value(mui_np_iface_choice - 1U), sizeof(cfg.network_interface) - 1U);
		cfg.network_interface[sizeof(cfg.network_interface) - 1U] = 0;
	}else{
		cfg.network_interface[0] = 0;
	}
	memcpy(cfg.local_name, mui_np_name, sizeof(cfg.local_name));
	memcpy(cfg.local_pad_labels, mui_np_pad_labels, sizeof(cfg.local_pad_labels));
	netplay_set_config(&cfg);
	mainui_set_netplay_rollback_window(mui_parse_u32_field(mui_np_rb_window, mainui_get_netplay_rollback_window(), 255U));
	mainui_set_netplay_input_delay(mui_parse_u32_field(mui_np_input_delay, mainui_get_netplay_input_delay(), 8U));
	mainui_touch_config();
	snprintf(mui_np_rb_window, sizeof(mui_np_rb_window), "%u", (unsigned)mainui_get_netplay_rollback_window());
	snprintf(mui_np_input_delay, sizeof(mui_np_input_delay), "%u", (unsigned)mainui_get_netplay_input_delay());
}
#endif

const char button_map[256] = {
	[ SDL_BUTTON_LEFT   & 0xFF ] = MU_MOUSE_LEFT,
	[ SDL_BUTTON_RIGHT  & 0xFF ] = MU_MOUSE_RIGHT,
	[ SDL_BUTTON_MIDDLE & 0xFF ] = MU_MOUSE_MIDDLE,
};

const int key_map[256] = {
	[ SDLK_LSHIFT    & 0xFF ] = MU_KEY_SHIFT,
	[ SDLK_RSHIFT    & 0xFF ] = MU_KEY_SHIFT,
	[ SDLK_LCTRL     & 0xFF ] = MU_KEY_CTRL,
	[ SDLK_RCTRL     & 0xFF ] = MU_KEY_CTRL,
	[ SDLK_LALT      & 0xFF ] = MU_KEY_ALT,
	[ SDLK_RALT      & 0xFF ] = MU_KEY_ALT,
	[ SDLK_RETURN    & 0xFF ] = MU_KEY_RETURN,
	[ SDLK_BACKSPACE & 0xFF ] = MU_KEY_BACKSPACE,
	[ SDLK_LEFT      & 0xFF ] = MU_KEY_LEFT,
	[ SDLK_RIGHT     & 0xFF ] = MU_KEY_RIGHT,
	[ SDLK_HOME      & 0xFF ] = MU_KEY_HOME,
	[ SDLK_END       & 0xFF ] = MU_KEY_END,
};

#ifdef ENABLE_NETPLAY
static void mui_netplay_auto_open_lobby(mu_Context* ctx)
{
	netplay_status_t st;
	if (ctx == NULL){ return; }
	netplay_get_status(&st);
	if (st.relay_room_code[0] != 0){
		strncpy(mui_np_room_code, st.relay_room_code, sizeof(mui_np_room_code) - 1U);
		mui_np_room_code[sizeof(mui_np_room_code) - 1U] = 0;
	}
	if ((!st.enabled) && mui_np_enabled_prev){
		if (st.compat_reason[0] != 0){
			snprintf(mui_np_status, sizeof(mui_np_status), "%s", st.compat_reason);
		}else{
			snprintf(mui_np_status, sizeof(mui_np_status), "DISCONNECTED");
		}
	}
	if (st.connected){
		if (!mui_np_connected_prev){
			mui_window_open(ctx, MUI_WIN_LOBBY);
			mui_window_close(ctx, MUI_WIN_NETPLAY_START);
			if (st.mode == NETPLAY_MODE_SERVER){
				snprintf(mui_np_status, sizeof(mui_np_status), "PEER CONNECTED. LOBBY OPENED.");
			}else if (st.mode == NETPLAY_MODE_CLIENT){
				snprintf(mui_np_status, sizeof(mui_np_status), "CONNECTED TO HOST. LOBBY OPENED.");
			}else{
				snprintf(mui_np_status, sizeof(mui_np_status), "NETPLAY CONNECTED. LOBBY OPENED.");
			}
		}
		if (st.session_started){
			if (!mui_np_session_started_prev){
				mui_close_all_windows();
				if (st.mode == NETPLAY_MODE_SERVER){
					snprintf(mui_np_status, sizeof(mui_np_status), "MATCH STARTED. GUI CLOSED.");
				}else if (st.mode == NETPLAY_MODE_CLIENT){
					snprintf(mui_np_status, sizeof(mui_np_status), "MATCH STARTED BY HOST. GUI CLOSED.");
				}else{
					snprintf(mui_np_status, sizeof(mui_np_status), "MATCH STARTED. GUI CLOSED.");
				}
			}
			mui_np_session_started_prev = TRUE;
		}else{
			mui_np_session_started_prev = FALSE;
		}
		mui_np_connected_prev = TRUE;
	}else{
		mui_np_connected_prev = FALSE;
		mui_np_session_started_prev = FALSE;
	}
	mui_np_enabled_prev = st.enabled ? TRUE : FALSE;
}
#endif

static boole mui_any_window_open(void)
{
	auint i;
	for (i = 0U; i < (auint)MUI_WIN_COUNT; ++i){
		if (mui_window_is_open(mui_state.ctx, (mui_window_id_t)i)){ return TRUE; }
	}
	return (mui_filedialog_is_open(&mui_rom_dialog) || mui_filedialog_is_open(&mui_dir_dialog) || mui_filedialog_is_open(&mui_cfg_file_dialog));
}

static void mui_close_all_windows(void)
{
	auint i;
	for (i = 0U; i < (auint)MUI_WIN_COUNT; ++i){
		boole* show_flag = mui_window_show_ptr((mui_window_id_t)i);
		if (show_flag != NULL){ *show_flag = FALSE; }
	}
	mui_cheat_search_sync_pause();
	mui_filedialog_close(&mui_rom_dialog);
	mui_filedialog_close(&mui_dir_dialog);
	mui_filedialog_close(&mui_cfg_file_dialog);
}

static boole mui_close_topmost_window(void)
{
	if (mui_filedialog_is_open(&mui_rom_dialog)){ mui_filedialog_close(&mui_rom_dialog); return TRUE; }
	if (mui_filedialog_is_open(&mui_dir_dialog)){ mui_filedialog_close(&mui_dir_dialog); return TRUE; }
	if (mui_filedialog_is_open(&mui_cfg_file_dialog)){ mui_filedialog_close(&mui_cfg_file_dialog); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_PICK_STATE)){ mui_window_close(mui_state.ctx, MUI_WIN_PICK_STATE); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_RECENT_ROMS)){ mui_window_close(mui_state.ctx, MUI_WIN_RECENT_ROMS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_ESP_MONITOR)){ mui_window_close(mui_state.ctx, MUI_WIN_ESP_MONITOR); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SERIAL_TRACE)){ mui_window_close(mui_state.ctx, MUI_WIN_SERIAL_TRACE); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_INPUT_CAPTURE)){ mui_window_close(mui_state.ctx, MUI_WIN_INPUT_CAPTURE); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SEARCH_CHEATS)){ mui_window_close(mui_state.ctx, MUI_WIN_SEARCH_CHEATS); mui_cheat_search_sync_pause(); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SPIRAM_EDIT)){ mui_window_close(mui_state.ctx, MUI_WIN_SPIRAM_EDIT); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SRAM_EDIT)){ mui_window_close(mui_state.ctx, MUI_WIN_SRAM_EDIT); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_ROM_VIEW)){ mui_window_close(mui_state.ctx, MUI_WIN_ROM_VIEW); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_DUMP_VIDEO)){ mui_window_close(mui_state.ctx, MUI_WIN_DUMP_VIDEO); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SCREENSHOT)){ mui_window_close(mui_state.ctx, MUI_WIN_SCREENSHOT); return TRUE; }
#ifdef ENABLE_DEBUGGER
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_DEBUGGER)){ mui_window_close(mui_state.ctx, MUI_WIN_DEBUGGER); return TRUE; }
#endif
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_TOOLS)){ mui_window_close(mui_state.ctx, MUI_WIN_TOOLS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_LOBBY)){ mui_window_close(mui_state.ctx, MUI_WIN_LOBBY); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_PLAY_ONLINE)){ mui_window_close(mui_state.ctx, MUI_WIN_PLAY_ONLINE); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_NETPLAY_DISCONNECT)){ mui_window_close(mui_state.ctx, MUI_WIN_NETPLAY_DISCONNECT); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_NETPLAY_STATUS)){ mui_window_close(mui_state.ctx, MUI_WIN_NETPLAY_STATUS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_NETPLAY_SETTINGS)){ mui_window_close(mui_state.ctx, MUI_WIN_NETPLAY_SETTINGS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_NETPLAY_START)){ mui_window_close(mui_state.ctx, MUI_WIN_NETPLAY_START); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_NETPLAY)){ mui_window_close(mui_state.ctx, MUI_WIN_NETPLAY); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_VIEW_CHEATS)){ mui_window_close(mui_state.ctx, MUI_WIN_VIEW_CHEATS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_MIDI_PORTS)){ mui_window_close(mui_state.ctx, MUI_WIN_MIDI_PORTS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SERIAL)){ mui_window_close(mui_state.ctx, MUI_WIN_SERIAL); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CFG_GUI)){ mui_window_close(mui_state.ctx, MUI_WIN_CFG_GUI); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CFG_PATHS)){ mui_window_close(mui_state.ctx, MUI_WIN_CFG_PATHS); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CFG_AUDIO)){ mui_window_close(mui_state.ctx, MUI_WIN_CFG_AUDIO); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_SD_WRITE_WARNING)){ mui_window_close(mui_state.ctx, MUI_WIN_SD_WRITE_WARNING); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CFG_DEVICES)){ mui_window_close(mui_state.ctx, MUI_WIN_CFG_DEVICES); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CFG_INPUT)){ mui_window_close(mui_state.ctx, MUI_WIN_CFG_INPUT); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CFG_VIDEO)){ mui_window_close(mui_state.ctx, MUI_WIN_CFG_VIDEO); return TRUE; }
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_CONFIG)){
		mui_window_close(mui_state.ctx, MUI_WIN_CONFIG);
		mui_close_config_pages(mui_state.ctx);
		mui_window_close(mui_state.ctx, MUI_WIN_SERIAL);
		mui_window_close(mui_state.ctx, MUI_WIN_MIDI_PORTS);
		return TRUE;
	}
	if (mui_window_is_open(mui_state.ctx, MUI_WIN_GAME)){ mui_window_close(mui_state.ctx, MUI_WIN_GAME); return TRUE; }
	return FALSE;
}

static void mui_refresh_visibility(void)
{
	mui_state.bar_pinned = mui_any_window_open();
	if (mui_state.bar_pinned){
		mui_state.bar_visible = TRUE;
	}else if (mui_state.mouse_inside && (mui_state.mouse_y >= 0) && (mui_state.mouse_y <= MUI_BAR_REVEAL_Y)){
		mui_state.bar_visible = TRUE;
	}else{
		mui_state.bar_visible = FALSE;
	}
}

static void mui_gamepad_open_default(mu_Context* ctx)
{
	if (ctx == NULL){ return; }
	mui_window_open(ctx, MUI_WIN_GAME);
	mui_state.bar_visible = TRUE;
	mui_state.bar_pinned = TRUE;
	if ((!mui_state.mouse_inside) || (mui_state.mouse_x < 0) || (mui_state.mouse_y < 0)){
		mui_state.mouse_x = 64;
		mui_state.mouse_y = MUI_BAR_HEIGHT + 56;
	}
}

static void mui_gamepad_release(void)
{
	ginput_host_set_gui_capture(FALSE, 0U);
	mui_state.gamepad_cursor_active = FALSE;
	mui_state.gamepad_prev_buttons = 0U;
	mui_state.gamepad_menu_chord_latch = FALSE;
}

static auint mui_gamepad_pick_slot(void)
{
	auint buttons;
	if (ginput_get_controller_present(mui_state.gamepad_slot)){
		return mui_state.gamepad_slot;
	}
	if (ginput_host_get_controller_buttons(0U, &buttons)){ return 0U; }
	if (ginput_host_get_controller_buttons(1U, &buttons)){ return 1U; }
	return 2U;
}

static void mui_gamepad_toggle(mu_Context* ctx, auint slot)
{
	if (ctx == NULL){ return; }
	if (mui_is_open()){
		mui_close_all_windows();
		mui_refresh_visibility();
		mui_gamepad_release();
		return;
	}
	mui_state.gamepad_slot = slot;
	ginput_host_set_gui_capture(TRUE, slot);
	mui_state.gamepad_cursor_active = TRUE;
	mui_state.gamepad_prev_buttons = 0U;
	mui_state.gamepad_menu_chord_latch = TRUE;
	mui_gamepad_open_default(ctx);
	mui_refresh_visibility();
}

static void mui_sync_all_windows(mu_Context* ctx)
{
	auint i;
	if (ctx == NULL){ return; }
	for (i = 0U; i < (auint)MUI_WIN_COUNT; ++i){
		mui_window_sync(ctx, (mui_window_id_t)i);
	}
}


static boole mui_point_in_rect(int x, int y, mu_Rect rect)
{
	if (x < rect.x){ return FALSE; }
	if (y < rect.y){ return FALSE; }
	if (x >= (rect.x + rect.w)){ return FALSE; }
	if (y >= (rect.y + rect.h)){ return FALSE; }
	return TRUE;
}

static boole mui_point_in_container(const char* name, int x, int y)
{
	mu_Container* cnt;

	if (mui_state.ctx == NULL){ return FALSE; }
	cnt = mui_find_container(mui_state.ctx, name);
	if (cnt == NULL){ return FALSE; }
	if (!cnt->open){ return FALSE; }
	return mui_point_in_rect(x, y, cnt->rect);
}

static boole mui_point_in_any_ui(int x, int y)
{
	auint i;
	if (!mui_state.bar_visible && !mui_state.bar_pinned){ return FALSE; }
	if (mui_point_in_container("##topbar", x, y)){ return TRUE; }
	for (i = 0U; i < (auint)MUI_WIN_COUNT; ++i){
		mui_window_desc_t const* desc = mui_window_desc((mui_window_id_t)i);
		if ((desc != NULL) && mui_point_in_container(desc->title, x, y)){ return TRUE; }
	}
	if (mui_point_in_container("File Selector", x, y)){ return TRUE; }
	return FALSE;
}

static boole mui_update_mouse_position(int wx, int wy)
{
	int   tx;
	int   ty;
	boole inside;

	inside = guicore_window_to_tex(wx, wy, &tx, &ty);
	mui_state.mouse_x = tx;
	mui_state.mouse_y = ty;
	mui_state.mouse_inside = inside;
	mui_refresh_visibility();
	return inside;
}

static uint32 mui_pack(mu_Color color)
{
	return guicore_packrgb((auint)color.r, (auint)color.g, (auint)color.b);
}

static boole mui_clip_contains(int x, int y)
{
	if (x < mui_state.clip.x){ return FALSE; }
	if (y < mui_state.clip.y){ return FALSE; }
	if (x >= (mui_state.clip.x + mui_state.clip.w)){ return FALSE; }
	if (y >= (mui_state.clip.y + mui_state.clip.h)){ return FALSE; }
	if (x < 0){ return FALSE; }
	if (y < 0){ return FALSE; }
	if (x >= (int)mui_state.texw){ return FALSE; }
	if (y >= (int)mui_state.texh){ return FALSE; }
	return TRUE;
}

static void mui_plot(int x, int y, uint32 color)
{
	if (!mui_clip_contains(x, y)){ return; }
	mui_state.dest[(y * mui_state.pitch) + x] = color;
}

static void mui_draw_rect(mu_Rect rect, mu_Color color)
{
	int    x;
	int    y;
	uint32 packed;

	packed = mui_pack(color);
	for (y = 0; y < rect.h; y++){
		for (x = 0; x < rect.w; x++){
			mui_plot(rect.x + x, rect.y + y, packed);
		}
	}
}

static void mui_draw_text(const char* text, mu_Vec2 pos, mu_Color color)
{
	int    chr;
	int    x;
	int    y;
	int    sx;
	int    sy;
	int    dx;
	uint8  row;
	uint32 packed;

	packed = mui_pack(color);
	dx = pos.x;

	for (; *text != 0; text++){
		if (((unsigned char)(*text) & 0xC0U) == 0x80U){ continue; }
		chr = mu_min((unsigned char)(*text), 127);
		for (y = 0; y < MUI_FONT_CHAR_H; y++){
			row = chars[(chr * MUI_FONT_CHAR_H) + y];
			for (x = 0; x < MUI_FONT_CHAR_W; x++){
				if ((row & (0x80U >> x)) != 0U){
					for (sy = 0; sy < MUI_FONT_SCALE; sy++){
						for (sx = 0; sx < MUI_FONT_SCALE; sx++){
							mui_plot(dx + (x * MUI_FONT_SCALE) + sx,
							         pos.y + (y * MUI_FONT_SCALE) + sy,
							         packed);
						}
					}
				}
			}
		}
		dx += MUI_FONT_CHAR_ADV * MUI_FONT_SCALE;
	}
}

static void mui_draw_uze_icon(mu_Rect rect, uint8 const* icon, auint scale)
{
	int x;
	int y;
	int sx;
	int sy;
	uint32 const* pal;
	if ((icon == NULL) || (scale == 0U)){ return; }
	pal = guicore_getpalette();
	for (y = 0; y < 16; y++){
		for (x = 0; x < 16; x++){
			uint32 packed = pal[icon[(y * 16) + x] & 0xFFU];
			for (sy = 0; sy < (int)scale; sy++){
				for (sx = 0; sx < (int)scale; sx++){
					mui_plot(rect.x + (x * (int)scale) + sx,
					         rect.y + (y * (int)scale) + sy,
					         packed);
				}
			}
		}
	}
}

static void mui_draw_rgba_icon(mu_Rect rect, uint32 const* icon)
{
	int    x;
	int    y;
	uint32 rgba;
	uint8  r;
	uint8  g;
	uint8  b;
	uint8  a;
	if (icon == NULL){ return; }
	for (y = 0; y < 32; y++){
		for (x = 0; x < 32; x++){
			rgba = icon[(y * 32) + x];
			r = (uint8)((rgba >> 24) & 0xFFU);
			g = (uint8)((rgba >> 16) & 0xFFU);
			b = (uint8)((rgba >> 8) & 0xFFU);
			a = (uint8)(rgba & 0xFFU);
			if (a == 0U){ continue; }
			mui_plot(rect.x + x,
			         rect.y + y,
			         guicore_packrgb((auint)r, (auint)g, (auint)b));
		}
	}
}

static void mui_draw_icon(int id, mu_Rect rect, mu_Color color)
{
	mu_Rect src;
	mu_Rect dst;
	int     x;
	int     y;
	uint32  packed;

	if (id == MUI_ICON_GAME_UZE){
		uint8 const* icon = mainui_get_current_rom_icon();
		if (icon != NULL){
			mui_draw_uze_icon(rect, icon, 2U);
		}else{
			mui_draw_rgba_icon(rect, guicore_get_default_icon_rgba());
		}
		return;
	}

	src = atlas[id];
	dst.x = rect.x + ((rect.w - src.w) / 2);
	dst.y = rect.y + ((rect.h - src.h) / 2);
	dst.w = src.w;
	dst.h = src.h;
	packed = mui_pack(color);

	for (y = 0; y < src.h; y++){
		for (x = 0; x < src.w; x++){
			if (atlas_texture[((src.y + y) * ATLAS_WIDTH) + src.x + x] != 0U){
				mui_plot(dst.x + x, dst.y + y, packed);
			}
		}
	}
}

static int mui_get_text_width(const char* text, int len)
{
	int res;

	if (len < 0){ len = (int)strlen(text); }
	res = 0;
	for (; (*text != 0) && (len > 0); text++, len--){
		if (((unsigned char)(*text) & 0xC0U) == 0x80U){ continue; }
		res += MUI_FONT_CHAR_ADV * MUI_FONT_SCALE;
	}
	return res;
}

static int mui_get_text_height(void)
{
	return MUI_FONT_CHAR_H * MUI_FONT_SCALE;
}

static int mui_button_width(const char* text)
{
	return mui_get_text_width(text, -1) + 18;
}

static int mui_checkbox_width(const char* text)
{
	return mui_get_text_width(text, -1) + 28;
}

static void mui_set_clip_rect(mu_Rect rect)
{
	mui_state.clip = rect;
}

static int mui_text_width(mu_Font font, const char* text, int len)
{
	(void)font;
	return mui_get_text_width(text, len);
}

static int mui_text_height(mu_Font font)
{
	(void)font;
	return mui_get_text_height();
}

static int mui_uint8_slider(mu_Context* ctx, unsigned char* value, int low, int high)
{
	static float tmp;
	mu_push_id(ctx, &value, sizeof(value));
	tmp = *value;
	if (tmp < (float)low){ tmp = (float)low; }
	if (tmp > (float)high){ tmp = (float)high; }
	if (mu_slider_ex(ctx, &tmp, (float)low, (float)high, 1.0f, "%.0f", MU_OPT_ALIGNCENTER) & MU_RES_CHANGE){
		*value = (unsigned char)tmp;
		mu_pop_id(ctx);
		return MU_RES_CHANGE;
	}
	*value = (unsigned char)tmp;
	mu_pop_id(ctx);
	return 0;
}

static void CU_UNUSED_FN mui_draw_topbar_backdrop(void)
{
	mui_draw_rect(mu_rect(0, 0, (int)mui_state.texw, MUI_BAR_BACKDROP_H), mu_color(0, 0, 0, 255));
}


static boole mui_remote_filter_match(remote_roms_game_t const* game)
{
	char hay[320];
	char needle[96];
	auint i;
	if ((mui_remote_filter[0] == 0) || (game == NULL)){ return TRUE; }
	snprintf(hay, sizeof(hay), "%u %s %s %s", (unsigned)game->id, game->title, game->authors, game->status);
	for (i = 0U; hay[i] != 0; i++){
		hay[i] = (char)tolower((unsigned char)hay[i]);
	}
	strncpy(needle, mui_remote_filter, sizeof(needle) - 1U);
	needle[sizeof(needle) - 1U] = 0;
	for (i = 0U; needle[i] != 0; i++){
		needle[i] = (char)tolower((unsigned char)needle[i]);
	}
	return (strstr(hay, needle) != NULL);
}

static void mui_set_game_status(char const* text)
{
	if (text == NULL){ text = ""; }
	strncpy(mui_game_status, text, sizeof(mui_game_status) - 1U);
	mui_game_status[sizeof(mui_game_status) - 1U] = 0;
}

static void mui_system_message(char const* fmt, ...)
{
	va_list ap;
	char    buf[128];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	textgui_log_add(&(buf[0]));
}

static void mui_do_save_slot(auint slot)
{
	char msg[96];
	savestate_set_slot(slot);
	if (savestate_save_slot(slot)){
		snprintf(msg, sizeof(msg), "Saved slot %u", (unsigned)slot);
		mui_set_game_status(msg);
	}else{
		mui_set_game_status("Save state failed");
	}
}

static void mui_do_load_slot(auint slot)
{
	char msg[96];
	savestate_set_slot(slot);
	if (savestate_load_slot(slot)){
		snprintf(msg, sizeof(msg), "Loaded slot %u", (unsigned)slot);
		mui_set_game_status(msg);
	}else{
		mui_set_game_status("Load state failed");
	}
}


static int mui_apply_gui_cursor_speed(int delta)
{
	auint speed;
	int absd;
	if (delta == 0){ return 0; }
	speed = mainui_get_gui_gamepad_speed_pct();
	if (speed < 10U){ speed = 10U; }
	if (speed > 200U){ speed = 200U; }
	absd = (delta < 0) ? -delta : delta;
	absd = (int)(((unsigned)absd * speed + 50U) / 100U);
	if (absd < 1){ absd = 1; }
	return (delta < 0) ? (-absd) : absd;
}

static void mui_gamepad_draw_cursor(void)
{
	int x = mui_state.mouse_x;
	int y = mui_state.mouse_y;
	uint32 white = guicore_packrgb(255U, 244U, 220U);
	uint32 dark = guicore_packrgb(0U, 0U, 0U);
	int i;
	if ((!mui_state.gamepad_cursor_active) || (!mui_state.mouse_inside)){ return; }
	for (i = -5; i <= 5; i++){
		if (i != 0){
			mui_plot(x + i, y, dark);
			mui_plot(x, y + i, dark);
		}
	}
	for (i = -4; i <= 4; i++){
		if (i != 0){
			mui_plot(x + i, y, white);
			mui_plot(x, y + i, white);
		}
	}
	mui_plot(x, y, white);
	mui_plot(x - 1, y - 1, dark);
	mui_plot(x + 1, y - 1, dark);
	mui_plot(x - 1, y + 1, dark);
	mui_plot(x + 1, y + 1, dark);
}

static void mui_gamepad_tick(void)
{
	auint slot;
	auint buttons = 0U;
	auint pressed;
	sint32 ax = 0;
	sint32 ay = 0;
	int move_x = 0;
	int move_y = 0;
	boole gui_enabled;
	if (mui_state.ctx == NULL){ return; }
	gui_enabled = mainui_get_gui_gamepad_enable();
	if (!gui_enabled){
		if (mui_state.gamepad_cursor_active && mui_is_open()){
			mui_close_all_windows();
			mui_refresh_visibility();
		}
		mui_gamepad_release();
		return;
	}
	slot = mui_gamepad_pick_slot();
	if (slot >= 2U){
		mui_gamepad_release();
		return;
	}
	mui_state.gamepad_slot = slot;
	(void)ginput_host_get_controller_buttons(slot, &buttons);
	(void)ginput_host_get_controller_axis(slot, 0U, &ax);
	(void)ginput_host_get_controller_axis(slot, 1U, &ay);
	pressed = buttons & ~mui_state.gamepad_prev_buttons;
	{
		boole menu_chord = (((buttons & CU_CTR_SNES_M_START) != 0U) &&
		                    ((buttons & CU_CTR_SNES_M_SELECT) != 0U));
		if (menu_chord){
			if (!mui_state.gamepad_menu_chord_latch){
				mui_gamepad_toggle(mui_state.ctx, slot);
				mui_state.gamepad_menu_chord_latch = TRUE;
				buttons &= ~(CU_CTR_SNES_M_START | CU_CTR_SNES_M_SELECT);
				pressed &= ~(CU_CTR_SNES_M_START | CU_CTR_SNES_M_SELECT);
			}
		}else{
			mui_state.gamepad_menu_chord_latch = FALSE;
		}
	}
	if (!mui_is_open()){
		mui_state.gamepad_prev_buttons = buttons;
		if (mui_state.gamepad_cursor_active){ mui_gamepad_release(); }
		return;
	}
	{
		auint captured_slot = 2U;
		if ((!mui_state.gamepad_cursor_active) || (!ginput_host_get_gui_capture(&captured_slot)) || (captured_slot != slot)){
			ginput_host_set_gui_capture(TRUE, slot);
			mui_state.gamepad_cursor_active = TRUE;
			if (captured_slot != slot){
				mui_state.gamepad_prev_buttons = buttons;
			}
		}
	}
	move_x += ((buttons & CU_CTR_SNES_M_LEFT)  != 0U) ? -6 : 0;
	move_x += ((buttons & CU_CTR_SNES_M_RIGHT) != 0U) ?  6 : 0;
	move_y += ((buttons & CU_CTR_SNES_M_UP)    != 0U) ? -6 : 0;
	move_y += ((buttons & CU_CTR_SNES_M_DOWN)  != 0U) ?  6 : 0;
	if (ax <= -8192){ move_x -= (int)((-ax + 4095) / 4096); }
	else if (ax >= 8192){ move_x += (int)((ax + 4095) / 4096); }
	if (ay <= -8192){ move_y -= (int)((-ay + 4095) / 4096); }
	else if (ay >= 8192){ move_y += (int)((ay + 4095) / 4096); }
	move_x = mui_apply_gui_cursor_speed(move_x);
	move_y = mui_apply_gui_cursor_speed(move_y);
	if ((!mui_state.mouse_inside) || (mui_state.mouse_x < 0) || (mui_state.mouse_y < 0)){
		mui_state.mouse_x = (int)(mui_state.texw / 2U);
		mui_state.mouse_y = (int)(mui_state.texh / 2U);
		mui_state.mouse_inside = TRUE;
	}
	if (move_x != 0 || move_y != 0){
		mui_state.mouse_x += move_x;
		mui_state.mouse_y += move_y;
		if (mui_state.mouse_x < 0){ mui_state.mouse_x = 0; }
		if (mui_state.mouse_y < 0){ mui_state.mouse_y = 0; }
		if (mui_state.mouse_x >= (int)mui_state.texw){ mui_state.mouse_x = (int)mui_state.texw - 1; }
		if (mui_state.mouse_y >= (int)mui_state.texh){ mui_state.mouse_y = (int)mui_state.texh - 1; }
		mu_input_mousemove(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y);
	}
	if ((pressed & CU_CTR_SNES_M_B) != 0U){
		mu_input_mousedown(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y, MU_MOUSE_LEFT);
	}
	if (((mui_state.gamepad_prev_buttons & CU_CTR_SNES_M_B) != 0U) && ((buttons & CU_CTR_SNES_M_B) == 0U)){
		mu_input_mouseup(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y, MU_MOUSE_LEFT);
	}
	if ((pressed & CU_CTR_SNES_M_Y) != 0U){
		mu_input_mousedown(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y, MU_MOUSE_RIGHT);
	}
	if (((mui_state.gamepad_prev_buttons & CU_CTR_SNES_M_Y) != 0U) && ((buttons & CU_CTR_SNES_M_Y) == 0U)){
		mu_input_mouseup(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y, MU_MOUSE_RIGHT);
	}
	if ((pressed & CU_CTR_SNES_M_LSH) != 0U){
		mu_input_scroll(mui_state.ctx, 0, 30);
	}
	if ((pressed & CU_CTR_SNES_M_RSH) != 0U){
		mu_input_scroll(mui_state.ctx, 0, -30);
	}
	if ((pressed & CU_CTR_SNES_M_A) != 0U){
		if (mui_close_topmost_window()){
			mui_refresh_visibility();
		}else{
			mui_gamepad_toggle(mui_state.ctx, slot);
			buttons &= ~CU_CTR_SNES_M_A;
		}
	}
	mui_state.gamepad_prev_buttons = buttons;
	mui_refresh_visibility();
}

static void mui_build_topbar(mu_Context* ctx)
{
	int widths[7];

	widths[0] = mui_get_text_width("v", -1) + 20;
	widths[1] = mui_get_text_width("GAME", -1) + 20;
	widths[2] = mui_get_text_width("CONFIG", -1) + 20;
	widths[3] = mui_get_text_width("NETPLAY", -1) + 20;
	widths[4] = mui_get_text_width("TOOLS", -1) + 20;
	widths[5] = -1;
	widths[6] = mui_get_text_width("HIDE", -1) + 20;

	if (!mui_state.bar_visible){ return; }
	if (mu_begin_window_ex(ctx,
	                       "##topbar",
	                       mu_rect(0, -1, (int)mui_state.texw, MUI_BAR_HEIGHT + 1),
	                       MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NOSCROLL | MU_OPT_NORESIZE | MU_OPT_NOCLOSE)){
		mu_layout_row(ctx, 7, widths, MUI_BAR_ROW_H);
		if (mu_button(ctx, "v")){ mui_window_toggle(ctx, MUI_WIN_RECENT_ROMS); }
		if (mu_button(ctx, "GAME")){ mui_window_toggle(ctx, MUI_WIN_GAME); }
		if (mu_button(ctx, "CONFIG")){
			if (mui_any_config_page_open(ctx)){
				mui_close_config_pages(ctx);
				mui_window_open(ctx, MUI_WIN_CONFIG);
			}else{
				mui_window_toggle(ctx, MUI_WIN_CONFIG);
			}
		}
		if (mu_button(ctx, "NETPLAY")){ mui_window_toggle(ctx, MUI_WIN_NETPLAY); }
		if (mu_button(ctx, "TOOLS")){ mui_window_toggle(ctx, MUI_WIN_TOOLS); }
		mu_label(ctx, "");
		if (mui_state.bar_pinned){
			if (mu_button(ctx, "HIDE")){
				mui_close_all_windows();
			}
		}else{
			mu_label(ctx, "");
		}
		mu_end_window(ctx);
	}
}

boole mui_netplay_ui_active(void)
{
	return mui_state.show_play_online || mui_state.show_online_games || mui_state.show_netplay || mui_state.show_netplay_start || mui_state.show_netplay_settings || mui_state.show_netplay_status || mui_state.show_netplay_disconnect || mui_state.show_lobby;
}

boole mui_tools_ui_active(void)
{
	return mui_state.show_tools || mui_state.show_tool_screenshot || mui_state.show_tool_video || mui_state.show_tool_romview || mui_state.show_tool_sram || mui_state.show_tool_spiram || mui_state.show_tool_eeprom || mui_state.show_tool_inputcap || mui_state.show_tool_input_trace || mui_state.show_tool_serial_trace || mui_state.show_tool_esp_monitor || mui_state.show_cheats || mui_state.show_tool_cheatsearch || mui_state.show_remote;
}

boole mui_debugger_ui_active(void)
{
#ifdef ENABLE_DEBUGGER
	return mui_state.show_debugger;
#else
	return FALSE;
#endif
}

boole mui_gamepad_capture_active(void)
{
	return mui_state.gamepad_cursor_active;
}

static void mui_build_game_window(mu_Context* ctx)
{
	char buf[MUI_FILEDIALOG_PATH_CAP + 64];
	char titlebuf[128];
	mu_Rect header;
	mu_Rect icon_box;
	mu_Rect text_box;
	auint year;
	uint32 crc32;
	if (!mui_window_begin(ctx, MUI_WIN_GAME)){ return; }
	{
		year = mainui_get_current_rom_year();
		crc32 = mainui_get_current_rom_crc32();
		snprintf(titlebuf, sizeof(titlebuf), "%s", mainui_get_current_rom_name());
		mu_layout_row(ctx, 1, (int[]){ -1 }, 42);
		header = mu_layout_next(ctx);
		if (titlebuf[0] != 0){
			icon_box = mu_rect(header.x + 2, header.y + 1, 32, 32);
			mu_draw_rect(ctx, mu_rect(icon_box.x - 1, icon_box.y - 1, icon_box.w + 2, icon_box.h + 2), mu_color(20, 20, 20, 255));
			mu_draw_icon(ctx, MUI_ICON_GAME_UZE, icon_box, mu_color(255, 255, 255, 255));
			text_box = mu_rect(icon_box.x + 40, header.y + 1, mu_max(0, header.w - 42), 14);
		}else{
			text_box = mu_rect(header.x + 2, header.y + 1, mu_max(0, header.w - 4), 14);
		}
		mu_draw_control_text(ctx, titlebuf, text_box, MU_COLOR_TEXT, 0);
		text_box.y += 16;
		if (year != 0U){
			snprintf(buf, sizeof(buf), "%u", (unsigned)year);
		}else{
			snprintf(buf, sizeof(buf), "UNKNOWN YEAR");
		}
		mu_draw_control_text(ctx, buf, text_box, MU_COLOR_TEXT, 0);
		if (mainui_get_current_rom_is_uze()){
			text_box.y += 16;
			snprintf(buf, sizeof(buf), "CRC32 %08X", (unsigned)crc32);
			mu_draw_control_text(ctx, buf, text_box, MU_COLOR_TEXT, 0);
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, mainui_get_current_rom_author());
		mui_separator(ctx);
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_button(ctx, "LOAD")){
			mui_filedialog_open(&mui_rom_dialog, "ROM LOAD", mainui_get_rom_path(), ".uze;.hex");
		}
		if (mu_button(ctx, "RUN")){
#ifdef ENABLE_DEBUGGER
			mainui_debug_set_paused(FALSE);
#endif
			mui_close_all_windows();
			mui_set_game_status("RUN");
		}
		if (mu_button(ctx, "RESET")){
			mainui_reset_rom();
			mui_set_game_status("RESET");
			mui_close_all_windows();
		}
		if (mu_button(ctx, "REMOTE ROMS")){
			mui_window_open(ctx, MUI_WIN_REMOTE_ROMS);
		}
		mui_separator(ctx);
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_button(ctx, "SAVE STATE")){
			mui_do_save_slot(savestate_get_slot());
		}
		if (mu_button(ctx, "OPEN STATE")){
			mui_do_load_slot(savestate_get_slot());
		}
		if (mu_button(ctx, "PICK STATE")){
			mui_window_open(ctx, MUI_WIN_PICK_STATE);
			mui_window_bring_front(ctx, "PICK STATE");
		}
		snprintf(buf, sizeof(buf), "CURRENT SLOT: %u%s", (unsigned)savestate_get_slot(), savestate_slot_exists(savestate_get_slot()) ? "*" : "");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, buf);
		mui_separator(ctx);
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_button(ctx, "QUIT")){
			mainui_request_quit();
		}
		mui_window_end(ctx, MUI_WIN_GAME);
	}
}

static void mui_build_statepick_window(mu_Context* ctx)
{
	char slotbuf[32];
	auint slot;
	int state;
	if (!mui_window_begin(ctx, MUI_WIN_PICK_STATE)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "SELECT CURRENT STATE SLOT");
		mu_layout_row(ctx, 5, (int[]){ mui_checkbox_width("0*"), mui_checkbox_width("1*"), mui_checkbox_width("2*"), mui_checkbox_width("3*"), mui_checkbox_width("4*") }, 22);
		for (slot = 0U; slot < 5U; slot++){
			state = (savestate_get_slot() == slot) ? 1 : 0;
			snprintf(slotbuf, sizeof(slotbuf), "%u%s", (unsigned)slot, savestate_slot_exists(slot) ? "*" : "");
			if (mu_checkbox(ctx, slotbuf, &state)){
				savestate_set_slot(slot);
			}
		}
		mu_layout_row(ctx, 5, (int[]){ mui_checkbox_width("5*"), mui_checkbox_width("6*"), mui_checkbox_width("7*"), mui_checkbox_width("8*"), mui_checkbox_width("9*") }, 22);
		for (slot = 5U; slot < 10U; slot++){
			state = (savestate_get_slot() == slot) ? 1 : 0;
			snprintf(slotbuf, sizeof(slotbuf), "%u%s", (unsigned)slot, savestate_slot_exists(slot) ? "*" : "");
			if (mu_checkbox(ctx, slotbuf, &state)){
				savestate_set_slot(slot);
			}
		}
		mui_window_end(ctx, MUI_WIN_PICK_STATE);
	}
}

static void mui_build_serial_window(mu_Context* ctx)
{
	auint route;
	auint esp_model;
	auint uart_profile;
	auint virtual_mode;
	int i;
	char buf[128];
	static const char* route_names[] = {
		"DISCONNECTED",
		"ESP MODULE",
		"HOST SERIAL",
		"HOST MIDI",
		"MIDI ENDPOINT",
		"TCP SERIAL",
		"LOOPBACK"
	};

	if (!mui_window_is_open(ctx, MUI_WIN_SERIAL)){ return; }
	if (!mui_cfg_serial_synced){
		mui_sync_serial_buffers();
	}
	route = mainui_get_serial_route();
	esp_model = mainui_get_serial_esp_model();
	uart_profile = mainui_get_uart_profile();
	virtual_mode = mainui_get_virtual_midi_mode();
	if (mui_window_begin(ctx, MUI_WIN_SERIAL)){
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "Select where Uzebox UART traffic goes.");
		mu_label(ctx, "Route");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_button(ctx, mui_serial_route_name(route))){
			mu_open_popup(ctx, "serial_route_popup");
		}
		{
			mu_Container* popup = mu_find_container(ctx, "serial_route_popup");
			if (mu_begin_popup(ctx, "serial_route_popup")){
				mu_layout_row(ctx, 1, (int[]){ 220 }, 24);
				for (i = 0; i < 7; i++){
					if (mu_button(ctx, route_names[i])){
						mainui_set_serial_route((auint)i);
						route = (auint)i;
						snprintf(mui_serial_status, sizeof(mui_serial_status), "Serial route: %s", route_names[i]);
						mui_system_message("SERIAL ROUTE: %s", route_names[i]);
						if (popup != NULL){ popup->open = 0; }
					}
				}
				mu_end_popup(ctx);
			}
		}

		mu_label(ctx, (route == CU_ESP_SERIAL_ESP_MODULE) ? "UART Profile" : "UART Timing Profile");
		mu_layout_row(ctx, 3, (int[]){ mui_button_width("PREV"), -1, mui_button_width("NEXT") }, 24);
		if (mu_button(ctx, "PREV")){
			if (uart_profile <= CU_UART_PROFILE_FAST){ uart_profile = CU_UART_PROFILE_DEBUG; }
			else{ uart_profile--; }
			mainui_set_uart_profile(uart_profile);
			snprintf(mui_serial_status, sizeof(mui_serial_status), "UART profile: %s", mui_uart_profile_name(uart_profile));
			mui_system_message("UART PROFILE: %s", mui_uart_profile_name(uart_profile));
		}
		mu_label(ctx, mui_uart_profile_name(uart_profile));
		if (mu_button(ctx, "NEXT")){
			uart_profile++;
			if (uart_profile > CU_UART_PROFILE_DEBUG){ uart_profile = CU_UART_PROFILE_FAST; }
			mainui_set_uart_profile(uart_profile);
			snprintf(mui_serial_status, sizeof(mui_serial_status), "UART profile: %s", mui_uart_profile_name(uart_profile));
			mui_system_message("UART PROFILE: %s", mui_uart_profile_name(uart_profile));
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		if (route == CU_ESP_SERIAL_ESP_MODULE){
			mu_label(ctx, "FAST keeps host overhead lowest. BALANCED improves pacing. DEBUG adds staged RX timing.");
		}else{
			mu_label(ctx, "FAST minimizes host overhead. BALANCED models UART pacing.");
			mu_label(ctx, "DEBUG adds staged RX timing and richer error diagnostics.");
			mu_label(ctx, "ESP model settings do not apply to raw UART cable routes.");
		}

		if (route == CU_ESP_SERIAL_ESP_MODULE){
			mu_label(ctx, "ESP Module Profile");
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("PREV"), -1, mui_button_width("NEXT") }, 24);
			if (mu_button(ctx, "PREV")){
				if (esp_model <= 1U){ esp_model = 3U; }
				else{ esp_model--; }
				mainui_set_serial_esp_model(esp_model);
				mui_system_message("ESP MODULE PROFILE: %s", mui_serial_esp_name(esp_model));
			}
			mu_label(ctx, mui_serial_esp_name(esp_model));
			if (mu_button(ctx, "NEXT")){
				esp_model++;
				if (esp_model > 3U){ esp_model = 1U; }
				mainui_set_serial_esp_model(esp_model);
				mui_system_message("ESP MODULE PROFILE: %s", mui_serial_esp_name(esp_model));
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "ESP routing applies live.");
		}else if (route == CU_ESP_SERIAL_HOST_SERIAL){
			mu_label(ctx, "Host Serial Device");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			mu_textbox(ctx, mui_cfg_host_serial_device, (int)sizeof(mui_cfg_host_serial_device));
			mu_layout_row(ctx, 2, (int[]){ mui_button_width("SAVE DEVICE"), -1 }, 24);
			if (mu_button(ctx, "SAVE DEVICE")){
				mainui_set_host_serial_device_name(mui_cfg_host_serial_device);
				mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
				mainui_set_serial_route(CU_ESP_SERIAL_HOST_SERIAL);
				snprintf(mui_serial_status, sizeof(mui_serial_status), "Host serial device saved");
				mui_system_message("HOST SERIAL DEVICE SAVED");
			}
			mu_label(ctx, "Examples: COM3 or /dev/ttyUSB0");
		}else if (route == CU_ESP_SERIAL_HOST_MIDI){
			mu_label(ctx, "Host MIDI Port Name");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			mu_textbox(ctx, mui_cfg_host_midi_port, (int)sizeof(mui_cfg_host_midi_port));
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("SAVE PORT"), mui_button_width("DETECTED PORTS"), -1 }, 24);
#else
			mu_layout_row(ctx, 2, (int[]){ mui_button_width("SAVE PORT"), -1 }, 24);
#endif
			if (mu_button(ctx, "SAVE PORT")){
				mainui_set_host_midi_port_name(mui_cfg_host_midi_port);
				mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
				mainui_set_serial_route(CU_ESP_SERIAL_HOST_MIDI);
				snprintf(mui_serial_status, sizeof(mui_serial_status), "Host MIDI port saved");
				mui_system_message("HOST MIDI PORT SAVED");
			}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			if (mu_button(ctx, "DETECTED PORTS")){
				mainui_refresh_host_midi_ports();
				mui_window_open(ctx, MUI_WIN_MIDI_PORTS);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "Windows: pick an existing MIDI endpoint name or type one.");
#else
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "This backend remains the existing named MIDI backend on this platform.");
#endif
			mu_label(ctx, mainui_get_host_midi_supported() ? "Host MIDI backend ready." : "Host MIDI backend not available in this build.");
		}else if (route == CU_ESP_SERIAL_VIRTUAL_MIDI){
			mu_label(ctx, "MIDI ENDPOINT NAME");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			mu_textbox(ctx, mui_cfg_virtual_midi_port, (int)sizeof(mui_cfg_virtual_midi_port));
			mu_label(ctx, "Mode");
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("PREV"), -1, mui_button_width("NEXT") }, 24);
			if (mu_button(ctx, "PREV")){
				if (virtual_mode <= CU_ESP_VIRTUAL_MIDI_INSTRUMENT){ virtual_mode = CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL; }
				else{ virtual_mode--; }
				mainui_set_virtual_midi_mode(virtual_mode);
				mui_system_message("MIDI ENDPOINT MODE: %s", mui_virtual_midi_mode_name(virtual_mode));
			}
			mu_label(ctx, mui_virtual_midi_mode_name(virtual_mode));
			if (mu_button(ctx, "NEXT")){
				virtual_mode++;
				if (virtual_mode > CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL){ virtual_mode = CU_ESP_VIRTUAL_MIDI_INSTRUMENT; }
				mainui_set_virtual_midi_mode(virtual_mode);
				mui_system_message("MIDI ENDPOINT MODE: %s", mui_virtual_midi_mode_name(virtual_mode));
			}
			mu_layout_row(ctx, 2, (int[]){ mui_button_width("SAVE DEVICE"), -1 }, 24);
			if (mu_button(ctx, "SAVE DEVICE")){
				mainui_set_virtual_midi_port_name(mui_cfg_virtual_midi_port);
				mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
				mainui_set_serial_route(CU_ESP_SERIAL_VIRTUAL_MIDI);
				snprintf(mui_serial_status, sizeof(mui_serial_status), "MIDI endpoint saved");
				mui_system_message("MIDI ENDPOINT SAVED");
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			mu_label(ctx, "WINDOWS: MIDI ENDPOINT MODE NEEDS WINMM-VISIBLE LOOPBACK PORTS.");
			mu_label(ctx, "USE WINDOWS MIDI SERVICES BASIC MIDI 1.0 LOOPBACK OR loopMIDI FOR ANVIL STUDIO.");
#else
			mu_label(ctx, "CUZEBOX EXPORTS ITS OWN MIDI ENDPOINT(S) IN THIS MODE.");
#endif
			mu_label(ctx, mainui_get_virtual_midi_supported() ? "MIDI ENDPOINT BACKEND READY." : "MIDI ENDPOINT BACKEND NOT AVAILABLE IN THIS BUILD.");
		}else if (route == CU_ESP_SERIAL_TCP_SERIAL){
			char status_buf[320];
			char const* action_label;
			auint tcp_state;
			cu_esp_tcp_serial_diag_t tcp_diag;
			uint32 sock_rx_age;
			uint32 sock_tx_age;
			mu_layout_row(ctx, 6, (int[]){ mui_button_width("MODE"), mui_button_width("CLIENT"), mui_button_width("SERVER"), mui_button_width("AUTO"), mui_button_width("INTERNET"), mui_button_width("LAN") }, 24);
			mu_label(ctx, "MODE");
			if (mu_button(ctx, "CLIENT")){ mui_cfg_tcp_serial_mode = 0; }
			if (mu_button(ctx, "SERVER")){ mui_cfg_tcp_serial_mode = 1; }
			if (mu_button(ctx, "AUTO")){ mui_cfg_tcp_serial_mode = 2; }
			if (mu_button(ctx, "INTERNET")){ mui_cfg_tcp_serial_mode = 3; if (mui_cfg_link_room[0] == 0){ mui_link_random_room(); } }
			if (mu_button(ctx, "LAN")){ mui_cfg_tcp_serial_mode = 4; }
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, (mui_cfg_tcp_serial_mode == 1) ? "Server mode listens for one raw TCP client at a time." :
				(mui_cfg_tcp_serial_mode == 2) ? "Auto mode connects if a peer is listening, otherwise listens itself." :
				(mui_cfg_tcp_serial_mode == 3) ? "Both players: INTERNET + the SAME room code." :
				(mui_cfg_tcp_serial_mode == 4) ? "Pairs with the other CUzeBox on your LAN." :
				"Client mode connects out to a raw TCP endpoint.");
			if (mui_cfg_tcp_serial_mode == 3){
				mu_layout_row(ctx, 3, (int[]){ mui_button_width("ROOM CODE"), 160, mui_button_width("NEW CODE") }, 24);
				mu_label(ctx, "ROOM CODE");
				mu_textbox(ctx, mui_cfg_link_room, 9);
				if (mu_button(ctx, "NEW CODE")){ mui_link_random_room(); }
				mu_layout_row(ctx, 4, (int[]){ mui_button_width("ROOM CODE"), 230, mui_button_width("PORT"), 90 }, 24);
				mu_label(ctx, "RELAY");
				mu_textbox(ctx, mui_cfg_link_relay_host, (int)sizeof(mui_cfg_link_relay_host));
				mu_label(ctx, "PORT");
				mu_textbox(ctx, mui_cfg_link_relay_port, (int)sizeof(mui_cfg_link_relay_port));
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				if (cu_esp_link_get_notice()[0] != 0){
					snprintf(status_buf, sizeof(status_buf), "LINK: %s", cu_esp_link_get_notice());
					mu_label(ctx, status_buf);
				}
			}else if (mui_cfg_tcp_serial_mode == 4){
				cu_esp_link_lan_peer_t peers[8];
				auint np = cu_esp_link_lan_peers(peers, 8u);
				auint pi;
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				if (mainui_get_serial_route() != CU_ESP_SERIAL_TCP_SERIAL || mainui_get_tcp_serial_mode() != CU_ESP_TCP_SERIAL_MODE_LAN){
					mu_label(ctx, "Press START SEARCH to look for partners.");
				}else if (np == 0u){
					mu_label(ctx, "Nobody found yet (UDP 12002, check firewall).");
				}else{
					mu_label(ctx, "Found (auto-pairs if only one; else PAIR):");
				}
				for (pi = 0u; pi < np; pi++){
					mu_push_id(ctx, &peers[pi].id, (int)sizeof(peers[pi].id));
					mu_layout_row(ctx, 2, (int[]){ 480, mui_button_width("PAIR") }, 24);
					snprintf(status_buf, sizeof(status_buf), "%s %s%s%s", peers[pi].ip,
						peers[pi].same_rom ? "" : " OTHER ROM",
						peers[pi].searching ? "" : " BUSY",
						peers[pi].wants_us ? " WANTS YOU" : "");
					mu_label(ctx, status_buf);
					if (peers[pi].searching){
						if (mu_button(ctx, "PAIR")){ cu_esp_link_lan_choose(peers[pi].id); }
					}else{
						mu_label(ctx, "");
					}
					mu_pop_id(ctx);
				}
			}else{
				mu_layout_row(ctx, 4, (int[]){ mui_button_width("LOCAL PAIR"), mui_button_width("LOCAL AUTO"), mui_button_width("LOCAL SERVER"), mui_button_width("LOCAL CLIENT") }, 24);
				mu_label(ctx, "LOCAL PAIR");
				if (mu_button(ctx, "LOCAL AUTO")){ mui_apply_tcp_serial_local_auto(); }
				if (mu_button(ctx, "LOCAL SERVER")){ mui_apply_tcp_serial_local_server(); }
				if (mu_button(ctx, "LOCAL CLIENT")){ mui_apply_tcp_serial_local_client(); }
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				if (mui_cfg_tcp_serial_mode != 1){
					mu_label(ctx, "TCP Serial Host");
					mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
					mu_textbox(ctx, mui_cfg_tcp_serial_host, (int)sizeof(mui_cfg_tcp_serial_host));
				}
			}
			if (mui_cfg_tcp_serial_mode != 3){
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, "TCP Serial Port");
				mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
				mu_textbox(ctx, mui_cfg_tcp_serial_port, (int)sizeof(mui_cfg_tcp_serial_port));
			}
			if (mui_cfg_tcp_serial_mode <= 1){
				mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
				mu_checkbox(ctx, "AUTO RECONNECT", &mui_cfg_tcp_serial_auto_reconnect);
			}
			tcp_state = mainui_get_tcp_serial_state();
			if (mui_cfg_tcp_serial_mode == 3){
				action_label = (tcp_state == CU_ESP_TCP_SERIAL_STATE_DISCONNECTED) ? "JOIN ROOM" : "REJOIN ROOM";
			}else if (mui_cfg_tcp_serial_mode == 4){
				action_label = (tcp_state == CU_ESP_TCP_SERIAL_STATE_DISCONNECTED) ? "START SEARCH" : "RESTART SEARCH";
			}else if (mui_cfg_tcp_serial_mode == 2){
				action_label = (tcp_state == CU_ESP_TCP_SERIAL_STATE_DISCONNECTED) ? "START AUTO" : "RESTART AUTO";
			}else if (mui_cfg_tcp_serial_mode == 1){
				action_label = ((tcp_state == CU_ESP_TCP_SERIAL_STATE_LISTENING) ||
				                (tcp_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED)) ?
					"RESTART SERVER" : "START LISTENING";
			}else{
				action_label = ((tcp_state == CU_ESP_TCP_SERIAL_STATE_CONNECTING) ||
				                (tcp_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED) ||
				                (tcp_state == CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT)) ?
					"RECONNECT" : "CONNECT";
			}
			mu_layout_row(ctx, 3, (int[]){ mui_button_width(action_label), mui_button_width("DISCONNECT"), -1 }, 24);
			if (mu_button(ctx, action_label)){
				mui_apply_tcp_serial_target();
			}
			if (mu_button(ctx, "DISCONNECT")){
				mui_disconnect_tcp_serial();
				route = CU_ESP_SERIAL_DISCONNECTED;
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			snprintf(status_buf, sizeof(status_buf), "MODE: %s   ERROR: %d",
				mui_tcp_serial_mode_name(mainui_get_tcp_serial_mode()),
				(int)mainui_get_tcp_serial_last_error());
			mu_label(ctx, status_buf);
			snprintf(status_buf, sizeof(status_buf), "STATUS: %s",
				mui_tcp_serial_state_name(mainui_get_tcp_serial_state()));
			mu_label(ctx, status_buf);
			cu_esp_tcp_serial_diag_get(&tcp_diag);
			sock_rx_age = (tcp_diag.last_socket_rx_ms != 0u) ? (tcp_diag.current_ms - tcp_diag.last_socket_rx_ms) : 0xFFFFFFFFu;
			sock_tx_age = (tcp_diag.last_socket_tx_ms != 0u) ? (tcp_diag.current_ms - tcp_diag.last_socket_tx_ms) : 0xFFFFFFFFu;
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			snprintf(status_buf, sizeof(status_buf),
				"AVR TO LINK: %u  LINK TO AVR: %u   SOCKET TX: %u  RX: %u",
				(unsigned)tcp_diag.avr_to_backend_bytes,
				(unsigned)tcp_diag.backend_to_avr_bytes,
				(unsigned)tcp_diag.socket_tx_bytes,
				(unsigned)tcp_diag.socket_rx_bytes);
			mu_label(ctx, status_buf);
			snprintf(status_buf, sizeof(status_buf),
				"QUEUES TX %u/%u  RX %u/%u  SOCKET PENDING RX %u",
				(unsigned)tcp_diag.tx_queue_bytes, (unsigned)tcp_diag.tx_queue_high_water,
				(unsigned)tcp_diag.rx_queue_bytes, (unsigned)tcp_diag.rx_queue_high_water,
				(unsigned)tcp_diag.socket_rx_pending_bytes);
			mu_label(ctx, status_buf);
			snprintf(status_buf, sizeof(status_buf),
				"WOULD-BLOCK SEND %u RX %u   ERRORS SEND %u RX %u",
				(unsigned)tcp_diag.send_would_block, (unsigned)tcp_diag.recv_would_block,
				(unsigned)tcp_diag.send_errors, (unsigned)tcp_diag.recv_errors);
			mu_label(ctx, status_buf);
			snprintf(status_buf, sizeof(status_buf),
				"LAST SOCKET ERROR: SEND %d  RECV %d",
				(int)tcp_diag.last_send_error, (int)tcp_diag.last_recv_error);
			mu_label(ctx, status_buf);
			snprintf(status_buf, sizeof(status_buf),
				"SERVICE MAX GAP %u MS  >50MS %u  >250MS %u   SOCKET AGE TX %s RX %s",
				(unsigned)tcp_diag.max_service_gap_ms,
				(unsigned)tcp_diag.service_gap_over_50ms,
				(unsigned)tcp_diag.service_gap_over_250ms,
				(sock_tx_age == 0xFFFFFFFFu) ? "N/A" : "ACTIVE",
				(sock_rx_age == 0xFFFFFFFFu) ? "N/A" : "ACTIVE");
			mu_label(ctx, status_buf);
			if(sock_tx_age != 0xFFFFFFFFu || sock_rx_age != 0xFFFFFFFFu){
				snprintf(status_buf, sizeof(status_buf), "SOCKET LAST ACTIVITY AGE: TX %u MS  RX %u MS",
					(unsigned)((sock_tx_age == 0xFFFFFFFFu) ? 0u : sock_tx_age),
					(unsigned)((sock_rx_age == 0xFFFFFFFFu) ? 0u : sock_rx_age));
				mu_label(ctx, status_buf);
			}
			snprintf(status_buf, sizeof(status_buf), "LOG: %s",
				(cu_esp_tcp_serial_diag_get_log_path()[0] != 0) ? cu_esp_tcp_serial_diag_get_log_path() : "NOT OPEN");
			mu_label(ctx, status_buf);
			mu_layout_row(ctx, 2, (int[]){ mui_button_width("RESET TCP STATS"), mui_button_width("FLUSH TCP LOG") }, 24);
			if(mu_button(ctx, "RESET TCP STATS")){
				cu_esp_tcp_serial_diag_reset();
				mui_system_message("TCP SERIAL STATS RESET");
			}
			if(mu_button(ctx, "FLUSH TCP LOG")){
				cu_esp_tcp_serial_diag_flush_log();
				mui_system_message("TCP SERIAL LOG FLUSHED");
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, (mui_cfg_tcp_serial_mode == 1) ? "Server mode binds 0.0.0.0 on the selected port." : "Use host 127.0.0.1 for same-machine pairing.");
			mu_label(ctx, "Same PC: LOCAL AUTO in both windows.");
			mu_label(ctx, "Same network: LAN in both.");
			mu_label(ctx, "Internet: INTERNET + same room code in both.");
		}else if (route == CU_ESP_SERIAL_LOOPBACK){
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "Loopback feeds AVR TX back into AVR RX through the UART model.");
			mu_label(ctx, "Useful for self-tests, timing tests, and separating UART bugs from endpoint bugs.");
		}else{
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "Serial is disconnected from all backends.");
		}

		mu_layout_row(ctx, 3, (int[]){ mui_button_width("OPEN INP TRACE"), mui_button_width("OPEN TRACE"), mui_button_width("OPEN ESP MONITOR") }, 24);
		if (mu_button(ctx, "OPEN INP TRACE")){
			mui_window_open(ctx, MUI_WIN_INPUT_TRACE);
		}
		if (mu_button(ctx, "OPEN TRACE")){
			mainui_open_web_tool("serial");
		}
		if (mu_button(ctx, "OPEN ESP MONITOR")){
			mainui_open_web_tool("network");
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		snprintf(buf, sizeof(buf), "Current route: %s", mui_serial_route_name(mainui_get_serial_route()));
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "UART profile: %s", mui_uart_profile_name(mainui_get_uart_profile()));
		mu_label(ctx, buf);
		if (route == CU_ESP_SERIAL_TCP_SERIAL || route == CU_ESP_SERIAL_HOST_SERIAL || route == CU_ESP_SERIAL_LOOPBACK)
			mu_label(ctx, "Raw endpoints always use the AVR UDR + shift-register model.");
		mu_label(ctx, mui_serial_status);
		mui_window_end(ctx, MUI_WIN_SERIAL);
	}
}


static void mui_build_midi_ports_window(mu_Context* ctx)
{
	auint count;
	auint i;
	char const* raw;
	char const* label;

	if (!mui_window_begin(ctx, MUI_WIN_MIDI_PORTS)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		mu_label(ctx, "Detected Windows MIDI endpoint names:");
		mu_layout_row(ctx, 2, (int[]){ mui_button_width("REFRESH"), mui_button_width("USE DEFAULT") }, 24);
		if (mu_button(ctx, "REFRESH")){
			mainui_refresh_host_midi_ports();
		}
		if (mu_button(ctx, "USE DEFAULT")){
			mui_cfg_host_midi_port[0] = 0;
			mainui_set_host_midi_port_name(mui_cfg_host_midi_port);
			mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
			mainui_set_serial_route(CU_ESP_SERIAL_HOST_MIDI);
			snprintf(mui_serial_status, sizeof(mui_serial_status), "Host MIDI port set to default endpoint");
			mui_system_message("HOST MIDI PORT: DEFAULT ENDPOINT");
			mui_window_close(ctx, MUI_WIN_MIDI_PORTS);
			mui_window_end(ctx, MUI_WIN_MIDI_PORTS);
			return;
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		count = mainui_get_host_midi_port_count();
		if (count == 0U){
			mu_label(ctx, "No Windows MIDI endpoints detected.");
		}else{
			for (i = 0U; i < count; ++i){
				raw = mainui_get_host_midi_port_name_at(i);
				label = mainui_get_host_midi_port_label_at(i);
				mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
				if (mu_button(ctx, label)){
					strncpy(mui_cfg_host_midi_port, raw, sizeof(mui_cfg_host_midi_port) - 1U);
					mui_cfg_host_midi_port[sizeof(mui_cfg_host_midi_port) - 1U] = 0;
					mainui_set_host_midi_port_name(mui_cfg_host_midi_port);
					mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
					mainui_set_serial_route(CU_ESP_SERIAL_HOST_MIDI);
					if (mui_cfg_host_midi_port[0] != 0){
						snprintf(mui_serial_status, sizeof(mui_serial_status), "Host MIDI port selected");
						mui_system_message("HOST MIDI PORT SELECTED");
					}else{
						snprintf(mui_serial_status, sizeof(mui_serial_status), "Host MIDI port set to default endpoint");
						mui_system_message("HOST MIDI PORT: DEFAULT ENDPOINT");
					}
					mui_window_close(ctx, MUI_WIN_MIDI_PORTS);
					mui_window_end(ctx, MUI_WIN_MIDI_PORTS);
					return;
				}
			}
		}
#else
		(void)count;
		(void)i;
		(void)raw;
		(void)label;
		mu_label(ctx, "This platform uses the text field in SERIAL to name the virtual MIDI port.");
		mu_label(ctx, "There is no existing-endpoint picker for this backend.");
#endif
		mui_window_end(ctx, MUI_WIN_MIDI_PORTS);
	}
}

static void mui_build_rom_dialog(mu_Context* ctx)
{
	int rc;
	char pathbuf[MUI_FILEDIALOG_PATH_CAP];
	if (!mui_filedialog_is_open(&mui_rom_dialog)){ return; }
	rc = mui_filedialog_draw(ctx, &mui_rom_dialog, mu_rect(20, MUI_BAR_HEIGHT + 8, 540, 472));
	if (rc <= 0){ return; }
	if (!mui_filedialog_consume_accept(&mui_rom_dialog, pathbuf, sizeof(pathbuf))){ return; }
	strncpy(mui_pending_rom_load, pathbuf, sizeof(mui_pending_rom_load) - 1U);
	mui_pending_rom_load[sizeof(mui_pending_rom_load) - 1U] = 0;
}

static void mui_apply_pending_rom_load(void)
{
	char dirbuf[MUI_FILEDIALOG_PATH_CAP];
	char const* slash;
	if (mui_pending_rom_load[0] == 0){ return; }
	strncpy(dirbuf, mui_pending_rom_load, sizeof(dirbuf) - 1U);
	dirbuf[sizeof(dirbuf) - 1U] = 0;
	slash = strrchr(dirbuf, '/');
	if (slash == NULL){ slash = strrchr(dirbuf, '\\'); }
	if (slash != NULL){
		size_t dlen = (size_t)(slash - dirbuf);
		dirbuf[dlen] = 0;
		if (dlen == 0U){ strcpy(dirbuf, "/"); }
		mainui_set_rom_path(dirbuf);
	}
	if (mainui_load_rom_file(mui_pending_rom_load)){
		mui_state.show_game = FALSE;
		mui_set_game_status("ROM LOADED");
	}else{
		mui_set_game_status("ROM LOAD FAILED");
	}
	mui_pending_rom_load[0] = 0;
}

static void mui_apply_pending_dir_change(void)
{
	if (mui_pending_dir_apply[0] == 0){ return; }
	switch (mui_pending_dir_target){
		case 1:
			strncpy(mui_cfg_rom_path, mui_pending_dir_apply, sizeof(mui_cfg_rom_path) - 1U);
			mui_cfg_rom_path[sizeof(mui_cfg_rom_path) - 1U] = 0;
			mainui_set_rom_path(mui_cfg_rom_path);
			break;
		case 2:
			strncpy(mui_cfg_screenshot_path, mui_pending_dir_apply, sizeof(mui_cfg_screenshot_path) - 1U);
			mui_cfg_screenshot_path[sizeof(mui_cfg_screenshot_path) - 1U] = 0;
			mainui_set_screenshot_path(mui_cfg_screenshot_path);
			break;
		case 3:
			strncpy(mui_cfg_save_path, mui_pending_dir_apply, sizeof(mui_cfg_save_path) - 1U);
			mui_cfg_save_path[sizeof(mui_cfg_save_path) - 1U] = 0;
			mainui_set_save_path(mui_cfg_save_path);
			break;
		case 4:
			strncpy(mui_cfg_controllerdb_path, mui_pending_dir_apply, sizeof(mui_cfg_controllerdb_path) - 1U);
			mui_cfg_controllerdb_path[sizeof(mui_cfg_controllerdb_path) - 1U] = 0;
			mainui_set_controllerdb_path(mui_cfg_controllerdb_path);
			break;
		default:
			break;
	}
	mui_pending_dir_target = 0;
	mui_pending_dir_apply[0] = 0;
}

static void mui_build_dir_dialog(mu_Context* ctx)
{
	int rc;
	char pathbuf[MUI_FILEDIALOG_PATH_CAP];
	if (!mui_filedialog_is_open(&mui_dir_dialog)){ return; }
	rc = mui_filedialog_draw(ctx, &mui_dir_dialog, mu_rect(16, MUI_BAR_HEIGHT + 8, 520, 452));
	if (rc <= 0){ return; }
	if (!mui_filedialog_consume_accept(&mui_dir_dialog, pathbuf, sizeof(pathbuf))){ return; }
	strncpy(mui_pending_dir_apply, pathbuf, sizeof(mui_pending_dir_apply) - 1U);
	mui_pending_dir_apply[sizeof(mui_pending_dir_apply) - 1U] = 0;
}

static void mui_apply_pending_file_change(void)
{
	if (mui_pending_file_apply[0] == 0){ return; }
	switch (mui_pending_file_target){
		case MUI_FILE_TARGET_BOOTLOADER:
			strncpy(mui_cfg_resident_bootloader_file, mui_pending_file_apply, sizeof(mui_cfg_resident_bootloader_file) - 1U);
			mui_cfg_resident_bootloader_file[sizeof(mui_cfg_resident_bootloader_file) - 1U] = 0;
			mainui_set_resident_bootloader_file(mui_cfg_resident_bootloader_file);
			break;
		case MUI_FILE_TARGET_VIDEO_DUMP:
			strncpy(mui_toolvideo_file, mui_pending_file_apply, sizeof(mui_toolvideo_file) - 1U);
			mui_toolvideo_file[sizeof(mui_toolvideo_file) - 1U] = 0;
			mainui_set_video_dump_file(mui_toolvideo_file);
			break;
		case MUI_FILE_TARGET_THEME_SELECT:
			strncpy(mui_cfg_gui_theme_file, mui_pending_file_apply, sizeof(mui_cfg_gui_theme_file) - 1U);
			mui_cfg_gui_theme_file[sizeof(mui_cfg_gui_theme_file) - 1U] = 0;
			mainui_set_gui_theme_file(mui_cfg_gui_theme_file);
			break;
		case MUI_FILE_TARGET_THEME_EXPORT:
			strncpy(mui_cfg_gui_theme_file, mui_pending_file_apply, sizeof(mui_cfg_gui_theme_file) - 1U);
			mui_cfg_gui_theme_file[sizeof(mui_cfg_gui_theme_file) - 1U] = 0;
			mainui_set_gui_theme_file(mui_cfg_gui_theme_file);
			if (mainui_save_gui_theme_file(mui_cfg_gui_theme_file)){
				strncpy(mui_cfg_gui_theme_status, "THEME EXPORTED", sizeof(mui_cfg_gui_theme_status) - 1U);
				mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
			}else{
				strncpy(mui_cfg_gui_theme_status, "THEME EXPORT FAILED", sizeof(mui_cfg_gui_theme_status) - 1U);
				mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
			}
			break;
#ifdef ENABLE_DEBUGGER
		case MUI_FILE_TARGET_DEBUG_SYMBOL:
			strncpy(mui_dbg_symbols_file, mui_pending_file_apply, sizeof(mui_dbg_symbols_file) - 1U);
			mui_dbg_symbols_file[sizeof(mui_dbg_symbols_file) - 1U] = 0;
			mainui_debug_set_symbols_file(mui_dbg_symbols_file);
			mainui_debug_load_symbols_file(mui_dbg_symbols_file);
			break;
#endif
		default:
			break;
	}
	mui_pending_file_target = 0;
	mui_pending_file_apply[0] = 0;
}

static void mui_build_cfg_file_dialog(mu_Context* ctx)
{
	int rc;
	char pathbuf[MUI_FILEDIALOG_PATH_CAP];
	if (!mui_filedialog_is_open(&mui_cfg_file_dialog)){ return; }
	rc = mui_filedialog_draw(ctx, &mui_cfg_file_dialog, mu_rect(18, MUI_BAR_HEIGHT + 8, 540, 472));
	if (rc <= 0){ return; }
	if (!mui_filedialog_consume_accept(&mui_cfg_file_dialog, pathbuf, sizeof(pathbuf))){ return; }
	strncpy(mui_pending_file_apply, pathbuf, sizeof(mui_pending_file_apply) - 1U);
	mui_pending_file_apply[sizeof(mui_pending_file_apply) - 1U] = 0;
}

#ifdef ENABLE_NETPLAY
static boole mui_online_has_loaded_rom(void)
{
	char const* name = mainui_get_current_rom_name();
	return ((name != NULL) && (strcmp(name, "(no ROM loaded)") != 0));
}

static boole mui_online_host_current_game(void)
{
	auint relay_port = mui_parse_u32_field(mui_np_relay_port, 43810U, 65535U);
	auint local_port = mui_parse_u32_field(mui_np_local_port, 0U, 65535U);
	if (!mui_online_has_loaded_rom()){
		snprintf(mui_np_status, sizeof(mui_np_status), "LOAD OR RUN A GAME FIRST");
		return FALSE;
	}
	mui_apply_netplay_config();
	if (netplay_open_relay_host(mui_np_relay_host, relay_port, local_port, mui_np_room_code[0] ? mui_np_room_code : NULL, mui_np_public_room ? TRUE : FALSE, mainui_get_current_rom_name())){
		snprintf(mui_np_status, sizeof(mui_np_status), mui_np_public_room ? "CREATING PUBLIC ROOM FOR %s" : "CREATING PRIVATE ROOM FOR %s", mainui_get_current_rom_name());
		return TRUE;
	}
	snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO CREATE RELAY ROOM");
	return FALSE;
}

static boole mui_online_join_room(void)
{
	auint relay_port = mui_parse_u32_field(mui_np_relay_port, 43810U, 65535U);
	auint local_port = mui_parse_u32_field(mui_np_local_port, 0U, 65535U);
	mui_apply_netplay_config();
	if (mui_np_room_code[0] == 0){
		snprintf(mui_np_status, sizeof(mui_np_status), "ENTER A ROOM CODE FIRST");
		return FALSE;
	}
	if (netplay_open_relay_join(mui_np_relay_host, relay_port, mui_np_room_code, local_port)){
		snprintf(mui_np_status, sizeof(mui_np_status), "JOINING ROOM %s", mui_np_room_code);
		return TRUE;
	}
	snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO JOIN ROOM %s", mui_np_room_code);
	return FALSE;
}

static void mui_online_refresh_public_rooms(void)
{
	auint relay_port = mui_parse_u32_field(mui_np_relay_port, 43810U, 65535U);
	mui_online_room_count = 0U;
	mui_online_room_status[0] = 0;
	if (netplay_relay_fetch_public_rooms(mui_np_relay_host, relay_port, mui_online_rooms, NETPLAY_RELAY_BROWSE_MAX, &mui_online_room_count, mui_online_room_status, sizeof(mui_online_room_status))){
		if (mui_online_room_count != 0U){
			snprintf(mui_np_status, sizeof(mui_np_status), "FOUND %u PUBLIC GAME%s", (unsigned)mui_online_room_count, (mui_online_room_count == 1U) ? "" : "S");
		}else{
			snprintf(mui_np_status, sizeof(mui_np_status), "NO PUBLIC GAMES AVAILABLE");
		}
	}else if (mui_online_room_status[0] != 0){
		snprintf(mui_np_status, sizeof(mui_np_status), "%s", mui_online_room_status);
	}
}

static void mui_build_online_games_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_ONLINE_GAMES)){ return; }
	{
#ifdef ENABLE_NETPLAY
		char line[160];
		mu_layout_row(ctx, 2, (int[]){ mui_button_width("REFRESH"), -1 }, 24);
		if (mu_button(ctx, "REFRESH")){
			mui_online_refresh_public_rooms();
		}
		mu_label(ctx, "");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "PUBLIC ROOMS. PRIVATE ROOMS REQUIRE A CODE.");
		if (mui_online_room_status[0] != 0){
			mu_label(ctx, mui_online_room_status);
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
		mu_begin_panel(ctx, "online_games_list");
		for (auint i = 0U; i < mui_online_room_count; ++i){
				mu_layout_row(ctx, 4, (int[]){ 230, 90, 90, mui_button_width("JOIN") }, 24);
				snprintf(line, sizeof(line), "%s", mui_online_rooms[i].game_title[0] ? mui_online_rooms[i].game_title : "UNKNOWN GAME");
				mu_label(ctx, line);
				snprintf(line, sizeof(line), "HOST %s", mui_online_rooms[i].host_name[0] ? mui_online_rooms[i].host_name : "PLAYER");
				mu_label(ctx, line);
				snprintf(line, sizeof(line), "ROOM %s", mui_online_rooms[i].room_code);
				mu_label(ctx, line);
				if (mu_button(ctx, "JOIN")){
					strncpy(mui_np_room_code, mui_online_rooms[i].room_code, sizeof(mui_np_room_code) - 1U);
					mui_np_room_code[sizeof(mui_np_room_code) - 1U] = 0;
					(void)mui_online_join_room();
				}
			}
		if (mui_online_room_count == 0U){
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "NO PUBLIC GAMES LISTED.");
		}
		mu_end_panel(ctx);
#else
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "NETPLAY DISABLED AT BUILD TIME.");
#endif
	}
	mui_window_end(ctx, MUI_WIN_ONLINE_GAMES);
}
#endif

static void mui_build_play_online_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_PLAY_ONLINE)){ return; }
	{
#ifdef ENABLE_NETPLAY
		netplay_status_t st;
		char buf[256];
		boole has_rom = mui_online_has_loaded_rom();
		auint year = mainui_get_current_rom_year();
		netplay_get_status(&st);
		int state;
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "OPEN A GAME, THEN HOST IT ONLINE.");
		mu_label(ctx, "JOINING WORKS EVEN IF NO GAME IS LOADED YET.");
		mu_layout_row(ctx, 3, (int[]){ mui_get_text_width("NAME", -1) + 8, 120, -1 }, 24);
		mu_label(ctx, "NAME");
		mu_textbox(ctx, mui_np_name, (int)sizeof(mui_np_name));
		mu_label(ctx, "");

		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		if (has_rom){
			if (year != 0U){
				snprintf(buf, sizeof(buf), "LOADED GAME %s (%u)", mainui_get_current_rom_name(), (unsigned)year);
			}else{
				snprintf(buf, sizeof(buf), "LOADED GAME %s", mainui_get_current_rom_name());
			}
		}else{
			snprintf(buf, sizeof(buf), "NO GAME LOADED");
		}
		mu_label(ctx, buf);

		mu_layout_row(ctx, 4, (int[]){ mui_button_width("HOST GAME"), mui_button_width("JOIN GAME"), mui_button_width("BROWSE GAMES"), mui_button_width("LOBBY") }, 24);
		if (mu_button_ex(ctx, "HOST GAME", 0, has_rom ? 0 : MU_OPT_NOINTERACT)){
			(void)mui_online_host_current_game();
		}
		if (mu_button(ctx, "JOIN GAME")){
			(void)mui_online_join_room();
		}
		if (mu_button(ctx, "BROWSE GAMES")){
			mui_online_refresh_public_rooms();
			mui_window_open(ctx, MUI_WIN_ONLINE_GAMES);
		}
		if (mu_button(ctx, "LOBBY")){
			mui_window_open(ctx, MUI_WIN_LOBBY);
		}

		mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("JOIN CODE", -1) + 8, 96, mui_button_width("GAME MENU"), mui_button_width("DETAILS") }, 24);
		mu_label(ctx, "JOIN CODE");
		mu_textbox(ctx, mui_np_room_code, (int)sizeof(mui_np_room_code));
		if (mu_button(ctx, "GAME MENU")){
			mui_window_open(ctx, MUI_WIN_GAME);
		}
		if (mu_button(ctx, "DETAILS")){
			mui_window_open(ctx, MUI_WIN_NETPLAY);
			mui_window_open(ctx, MUI_WIN_NETPLAY_START);
		}

		mu_layout_row(ctx, 2, (int[]){ 160, -1 }, 24);
		state = mui_np_public_room;
		if (mu_checkbox(ctx, "PUBLIC ROOM", &state)){
			mui_np_public_room = state ? 1 : 0;
		}
		mu_label(ctx, mui_np_public_room ? "LISTED IN BROWSE GAMES" : "HIDDEN - JOIN BY CODE ONLY");

		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		if (!has_rom){
			mu_label(ctx, "HOST GAME REQUIRES A LOADED ROM.");
		}
		if (st.relay_mode && st.relay_room_code[0] != 0){
			snprintf(buf, sizeof(buf), "ROOM CODE %s", st.relay_room_code);
			mu_label(ctx, buf);
		}
		if (st.connected){
			mui_np_format_transport(buf, sizeof(buf), &st);
			mu_label(ctx, buf);
		}else if (st.enabled){
			mu_label(ctx, "CONNECTING...");
		}
		mu_label(ctx, mui_np_status);
#else
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "NETPLAY IS DISABLED IN THIS BUILD.");
#endif
		mui_window_end(ctx, MUI_WIN_PLAY_ONLINE);
	}
}

static void mui_build_remote_window(mu_Context* ctx)
{
	auint i;
	remote_roms_game_t game;
	char label[320];
	if (!mui_window_begin(ctx, MUI_WIN_REMOTE_ROMS)){ return; }
	{
		mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("HOST:", -1) + 8, 250, mui_button_width("APPLY"), mui_button_width("DEFAULT") }, 24);
		mu_label(ctx, "HOST:");
		mu_textbox(ctx, mui_remote_host, (int)sizeof(mui_remote_host));
		if (mu_button(ctx, "APPLY")){
			mainui_set_remote_roms_host(mui_remote_host);
			mui_set_game_status("REMOTE HOST APPLIED");
		}
		if (mu_button(ctx, "DEFAULT")){
			strncpy(mui_remote_host, "uzenet.us", sizeof(mui_remote_host) - 1U);
			mui_remote_host[sizeof(mui_remote_host) - 1U] = 0;
			mainui_set_remote_roms_host(mui_remote_host);
			mui_set_game_status("REMOTE HOST DEFAULTED");
		}
		mu_layout_row(ctx, 3, (int[]){ mui_get_text_width("FIND:", -1) + 8, 220, mui_button_width("REFRESH") }, 24);
		mu_label(ctx, "FIND:");
		mu_textbox(ctx, mui_remote_filter, (int)sizeof(mui_remote_filter));
		if (mu_button(ctx, "REFRESH")){
			mainui_set_remote_roms_host(mui_remote_host);
			remote_roms_refresh_list();
			mui_set_game_status(remote_roms_get_status());
			mui_system_message("REMOTE ROMS: %s", remote_roms_get_status());
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, remote_roms_get_status());
		mu_layout_row(ctx, 4, (int[]){ mui_button_width("TOP"), mui_button_width("UP"), mui_button_width("DOWN"), -1 }, 24);
		if (mu_button(ctx, "TOP")){
			mu_Container* cnt = mu_get_container(ctx, "remote_list_panel");
			if (cnt != NULL){ cnt->scroll.y = 0; }
		}
		if (mu_button(ctx, "UP")){
			mu_Container* cnt = mu_get_container(ctx, "remote_list_panel");
			if (cnt != NULL){ cnt->scroll.y = (cnt->scroll.y >= 60) ? (cnt->scroll.y - 60) : 0; }
		}
		if (mu_button(ctx, "DOWN")){
			mu_Container* cnt = mu_get_container(ctx, "remote_list_panel");
			if (cnt != NULL){ cnt->scroll.y += 60; }
		}
		mu_begin_panel(ctx, "remote_list_panel");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
			for (i = 0U; i < remote_roms_get_count(); i++){
				if (!remote_roms_get_entry(i, &game)){ continue; }
				if (!mui_remote_filter_match(&game)){ continue; }
				snprintf(label, sizeof(label), "%u. %s [%s]",
				         (unsigned)game.id,
				         game.title,
				         game.status);
				if (mu_button(ctx, label)){
					mui_remote_selected = i;
					mui_set_game_status(label);
				}
			}
			mu_end_panel(ctx);
		mu_layout_row(ctx, 4, (int[]){ mui_button_width("DOWNLOAD"), mui_button_width("RUN"), mui_button_width("RUN + HOST"), mui_button_width("DOWNLOAD ALL") }, 24);
		if (mu_button(ctx, "DOWNLOAD")){
			mainui_set_remote_roms_host(mui_remote_host);
			remote_roms_download_game(mui_remote_selected, FALSE);
			mui_set_game_status(remote_roms_get_status());
			mui_system_message("REMOTE ROMS: %s", remote_roms_get_status());
		}
		if (mu_button(ctx, "RUN")){
			mainui_set_remote_roms_host(mui_remote_host);
			remote_roms_download_game(mui_remote_selected, TRUE);
			mui_set_game_status(remote_roms_get_status());
			mui_system_message("REMOTE ROMS: %s", remote_roms_get_status());
		}
		if (mu_button(ctx, "RUN + HOST")){
			mainui_set_remote_roms_host(mui_remote_host);
			if (remote_roms_download_game(mui_remote_selected, TRUE)){
				mui_system_message("REMOTE ROMS: %s", remote_roms_get_status());
#ifdef ENABLE_NETPLAY
				(void)mui_online_host_current_game();
				mui_window_open(ctx, MUI_WIN_PLAY_ONLINE);
#else
				mui_set_game_status(remote_roms_get_status());
#endif
			}else{
				mui_set_game_status(remote_roms_get_status());
				mui_system_message("REMOTE ROMS: %s", remote_roms_get_status());
			}
		}
		if (mu_button(ctx, "DOWNLOAD ALL")){
			mainui_set_remote_roms_host(mui_remote_host);
			remote_roms_download_all();
			mui_set_game_status(remote_roms_get_status());
			mui_system_message("REMOTE ROMS: %s", remote_roms_get_status());
		}
		mui_window_end(ctx, MUI_WIN_REMOTE_ROMS);
	}
}

static void mui_file_dialog_start_dir(char* out, auint cap, char const* path)
{
	char const* slash;
	if ((out == NULL) || (cap == 0U)){ return; }
	if ((path == NULL) || (path[0] == 0)){
		strncpy(out, mainui_get_rom_path(), cap - 1U);
		out[cap - 1U] = 0;
		return;
	}
	strncpy(out, path, cap - 1U);
	out[cap - 1U] = 0;
	slash = strrchr(out, '/');
	if (slash == NULL){ slash = strrchr(out, '\\'); }
	if (slash != NULL){
		size_t dlen = (size_t)(slash - out);
		out[dlen] = 0;
		if (dlen == 0U){ strcpy(out, "/"); }
	}else{
		strncpy(out, mainui_get_rom_path(), cap - 1U);
		out[cap - 1U] = 0;
	}
}


static void mui_sync_toolvideo_file(void)
{
	strncpy(mui_toolvideo_file, mainui_get_video_dump_file(), sizeof(mui_toolvideo_file) - 1U);
	mui_toolvideo_file[sizeof(mui_toolvideo_file) - 1U] = 0;
}

static void mui_config_file_row(mu_Context* ctx, const char* label, char* buf, int bufsz, void (*setter)(const char*), int target_id, const char* exts, const char* dialog_title)
{
	char startdir[MUI_FILEDIALOG_PATH_CAP];
	mu_push_id(ctx, &target_id, (int)sizeof(target_id));
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, label);
	mu_layout_row(ctx, 2, (int[]){ 292, mui_button_width("BROWSE") }, 22);
	if (mu_textbox(ctx, buf, bufsz)){
		if (setter != NULL){ setter(buf); }
	}
	if (mu_button(ctx, "BROWSE")){
		mui_pending_file_target = target_id;
		mui_file_dialog_start_dir(startdir, sizeof(startdir), buf);
		mui_filedialog_open(&mui_cfg_file_dialog, (dialog_title != NULL) ? dialog_title : "SELECT FILE", startdir, (exts != NULL) ? exts : "*");
	}
	mu_pop_id(ctx);
}

static void mui_config_path_row(mu_Context* ctx, const char* label, char* buf, int bufsz, void (*setter)(const char*), int target_id)
{
	mu_push_id(ctx, &target_id, (int)sizeof(target_id));
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, label);
	mu_layout_row(ctx, 2, (int[]){ 292, mui_button_width("BROWSE") }, 22);
	if (mu_textbox(ctx, buf, bufsz)){
		if (setter != NULL){ setter(buf); }
	}
	if (mu_button(ctx, "BROWSE")){
		mui_pending_dir_target = target_id;
		mui_filedialog_open_dir(&mui_dir_dialog, "SELECT DIRECTORY", buf);
	}
	mu_pop_id(ctx);
}

static int mui_color_swatch_button(mu_Context* ctx, mu_Color color, const char* label)
{
	int res = 0;
	int tw = 0;
	int th = 0;
	mu_Id id = mu_get_id(ctx, label, (int)strlen(label));
	mu_Rect r = mu_layout_next(ctx);
	mu_Color border = ctx->style->colors[MU_COLOR_BORDER];
	mu_Color text = ((int)color.r + (int)color.g + (int)color.b >= 384) ? mu_color(0, 0, 0, 255) : mu_color(255, 255, 255, 255);
	mu_update_control(ctx, id, r, 0);
	if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id){
		res = MU_RES_SUBMIT;
	}
	mu_draw_rect(ctx, r, color);
	mu_draw_box(ctx, r, border);
	if (label != NULL && label[0] != 0){
		tw = ctx->text_width(ctx->style->font, label, -1);
		th = ctx->text_height(ctx->style->font);
		mu_draw_text(ctx, ctx->style->font, label, -1, mu_vec2(r.x + ((r.w - tw) / 2), r.y + ((r.h - th) / 2)), text);
	}
	return res;
}

static boole CU_UNUSED_FN mui_choice_popup(mu_Context* ctx, const char* popup_id, const char* value, const char* const* names, auint count, auint* io_value, int width)
{
	auint i;
	boole changed = FALSE;
	mu_Container* popup = NULL;
	mu_push_id(ctx, popup_id, (int)strlen(popup_id));
	mu_layout_row(ctx, 1, (int[]){ width }, 24);
	if (mu_button(ctx, value)){
		mu_open_popup(ctx, popup_id);
	}
	popup = mu_find_container(ctx, popup_id);
	if (mu_begin_popup(ctx, popup_id)){
		mu_layout_row(ctx, 1, (int[]){ width }, 24);
		for (i = 0U; i < count; ++i){
			if (mu_button(ctx, names[i])){
				*io_value = i;
				changed = TRUE;
				if (popup != NULL){ popup->open = 0; }
			}
		}
		mu_end_popup(ctx);
	}
	mu_pop_id(ctx);
	return changed;
}


static boole mui_choice_popup_inline(mu_Context* ctx, const char* popup_id, const char* value, const char* const* names, auint count, auint* io_value, int width)
{
	auint i;
	auint visible;
	boole changed = FALSE;
	char panel_id[96];
	mu_Container* popup = NULL;
	mu_push_id(ctx, popup_id, (int)strlen(popup_id));
	if (mu_button(ctx, value)){
		mu_open_popup(ctx, popup_id);
	}
	popup = mu_find_container(ctx, popup_id);
	if (mu_begin_popup(ctx, popup_id)){
		visible = count;
		if (visible > 8U){ visible = 8U; }
		if (count > visible){
			snprintf(panel_id, sizeof(panel_id), "%s_list", popup_id);
			mu_layout_row(ctx, 1, (int[]){ width }, (int)(visible * 24U));
			mu_begin_panel(ctx, panel_id);
			mu_layout_row(ctx, 1, (int[]){ width - 18 }, 24);
			for (i = 0U; i < count; ++i){
				if (mu_button(ctx, names[i])){
					*io_value = i;
					changed = TRUE;
					if (popup != NULL){ popup->open = 0; }
				}
			}
			mu_end_panel(ctx);
		}else{
			mu_layout_row(ctx, 1, (int[]){ width }, 24);
			for (i = 0U; i < count; ++i){
				if (mu_button(ctx, names[i])){
					*io_value = i;
					changed = TRUE;
					if (popup != NULL){ popup->open = 0; }
				}
			}
		}
		mu_end_popup(ctx);
	}
	mu_pop_id(ctx);
	return changed;
}

static boole mui_labeled_choice_popup(mu_Context* ctx, const char* label, const char* popup_id, const char* value, const char* const* names, auint count, auint* io_value, int labelw, int choicew)
{
		mu_layout_row(ctx, 2, (int[]){ labelw, choicew }, 22);
	mu_label(ctx, label);
	return mui_choice_popup_inline(ctx, popup_id, value, names, count, io_value, choicew);
}

static mu_Rect mui_gui_color_picker_default_rect(void)
{
	int x = 176;
	int y = MUI_BAR_HEIGHT + 22;
	int w = 340;
	int h = 312;
	if ((int)mui_state.texw > 0){
		if (w > (int)mui_state.texw){ w = (int)mui_state.texw; }
		if ((x + w) > (int)mui_state.texw){ x = (int)mui_state.texw - w; }
	}
	if ((int)mui_state.texh > 0){
		if (h > (int)mui_state.texh){ h = (int)mui_state.texh; }
		if ((y + h) > (int)mui_state.texh){ y = (int)mui_state.texh - h; }
	}
	if (x < 0){ x = 0; }
	if (y < 0){ y = 0; }
	if (w < 96){ w = 96; }
	if (h < 64){ h = 64; }
	return mu_rect(x, y, w, h);
}

static void mui_gui_color_picker_reset_container(mu_Context* ctx, boole open_state)
{
	mu_Container* cnt;
	if (ctx == NULL){ return; }
	cnt = mu_get_container(ctx, "COLOR PICKER");
	if (cnt == NULL){ return; }
	cnt->open = open_state ? 1 : 0;
	cnt->rect = mui_gui_color_picker_default_rect();
	cnt->body = mu_rect(0, 0, 0, 0);
	cnt->content_size = mu_vec2(0, 0);
	cnt->scroll = mu_vec2(0, 0);
	if (open_state){
		mu_bring_to_front(ctx, cnt);
	}else{
		cnt->zindex = 0;
	}
}

static void mui_gui_color_picker_open(mu_Context* ctx, int color_idx)
{
	mui_cfg_gui_picker_state = color_idx;
	mui_cfg_gui_picker_open = TRUE;
	mui_cfg_gui_picker_just_opened = TRUE;
	mui_gui_color_picker_reset_container(ctx, TRUE);
}

static void mui_draw_gui_color_picker_window(mu_Context* ctx)
{
	mu_Style* style;
	mu_Color* color;
	char buf[64];
	int gui_changed = 0;
	int state;
	if (!mui_cfg_gui_picker_open){ return; }
	if ((mui_cfg_gui_picker_state < 0) || (mui_cfg_gui_picker_state >= APPCFG_GUI_COLOR_COUNT)){
		mui_cfg_gui_picker_open = FALSE;
		return;
	}
	if (!mui_window_is_open(ctx, MUI_WIN_CFG_GUI)){
		mui_cfg_gui_picker_open = FALSE;
		return;
	}
	{
		mu_Container* cnt = mu_get_container(ctx, "COLOR PICKER");
		if (cnt != NULL){
			cnt->open = 1;
			mu_bring_to_front(ctx, cnt);
			if (cnt->rect.w == 0){
				cnt->rect = mui_gui_color_picker_default_rect();
			}else{
				mu_Rect dflt = mui_gui_color_picker_default_rect();
				if (cnt->rect.w > dflt.w){ cnt->rect.w = dflt.w; }
				if (cnt->rect.h > dflt.h){ cnt->rect.h = dflt.h; }
				if (cnt->rect.x < 0){ cnt->rect.x = 0; }
				if (cnt->rect.y < 0){ cnt->rect.y = 0; }
				if ((cnt->rect.x + cnt->rect.w) > (int)mui_state.texw){ cnt->rect.x = (int)mui_state.texw - cnt->rect.w; }
				if ((cnt->rect.y + cnt->rect.h) > (int)mui_state.texh){ cnt->rect.y = (int)mui_state.texh - cnt->rect.h; }
				if (cnt->rect.x < 0){ cnt->rect.x = 0; }
				if (cnt->rect.y < 0){ cnt->rect.y = 0; }
			}
			if (!mui_cfg_gui_picker_just_opened && (ctx->mouse_pressed == MU_MOUSE_LEFT)){
				if ((ctx->mouse_pos.x < cnt->rect.x) || (ctx->mouse_pos.y < cnt->rect.y) || (ctx->mouse_pos.x >= (cnt->rect.x + cnt->rect.w)) || (ctx->mouse_pos.y >= (cnt->rect.y + cnt->rect.h))){
					mui_cfg_gui_picker_open = FALSE;
					mui_cfg_gui_picker_just_opened = FALSE;
					mui_gui_color_picker_reset_container(ctx, FALSE);
					return;
				}
			}
		}
	}
	if (!mu_begin_window_ex(ctx, "COLOR PICKER", mui_gui_color_picker_default_rect(), MU_OPT_NORESIZE | MU_OPT_NOSCROLL)){
		mu_Container* cnt = mu_get_container(ctx, "COLOR PICKER");
		if ((cnt != NULL) && !cnt->open){
			mui_cfg_gui_picker_open = FALSE;
			mui_cfg_gui_picker_just_opened = FALSE;
			mui_gui_color_picker_reset_container(ctx, FALSE);
		}
		return;
	}
	style = ctx->style;
	color = &style->colors[mui_cfg_gui_picker_state];
	mu_layout_row(ctx, 2, (int[]){ 110, -1 }, 24);
	mu_label(ctx, "COLOR");
	mu_label(ctx, mui_gui_color_labels[mui_cfg_gui_picker_state]);
	snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", color->r, color->g, color->b, color->a);
	mu_layout_row(ctx, 2, (int[]){ 110, 120 }, 24);
	mu_label(ctx, "VALUE");
	mu_label(ctx, buf);
	mu_layout_row(ctx, 1, (int[]){ -1 }, 132);
	if (mu_rgbpicker(ctx, color) & MU_RES_CHANGE){
		gui_changed = 1;
	}
	for (state = 0; state < 4; ++state){
		mu_layout_row(ctx, 2, (int[]){ 24, -1 }, 24);
		mu_label(ctx, (state == 0) ? "R" : (state == 1) ? "G" : (state == 2) ? "B" : "A");
		if (mui_uint8_slider(ctx, (state == 0) ? &color->r : (state == 1) ? &color->g : (state == 2) ? &color->b : &color->a, 0, 255)){
			gui_changed = 1;
		}
	}
	mu_layout_row(ctx, 2, (int[]){ mui_button_width("CLOSE"), mui_button_width("DONE") }, 24);
	if (mu_button(ctx, "CLOSE")){
		mui_cfg_gui_picker_open = FALSE;
		mui_cfg_gui_picker_just_opened = FALSE;
		mui_gui_color_picker_reset_container(ctx, FALSE);
	}
	if (mu_button(ctx, "DONE")){
		mui_cfg_gui_picker_open = FALSE;
		mui_cfg_gui_picker_just_opened = FALSE;
		mui_gui_color_picker_reset_container(ctx, FALSE);
	}
	if (gui_changed){
		mainui_touch_gui_config();
	}
	mu_end_window(ctx);
	{
		mu_Container* cnt = mu_get_container(ctx, "COLOR PICKER");
		if ((cnt != NULL) && !cnt->open){
			mui_cfg_gui_picker_open = FALSE;
			mui_cfg_gui_picker_just_opened = FALSE;
			mui_gui_color_picker_reset_container(ctx, FALSE);
		}
	}
	mui_cfg_gui_picker_just_opened = FALSE;
}

static void mui_build_config_window(mu_Context* ctx)
{
	char buf[128];
	if (!mui_window_is_open(ctx, MUI_WIN_CONFIG)){ return; }
	if (!mui_cfg_paths_synced){
		mui_sync_config_buffers();
	}
	if (mui_window_begin(ctx, MUI_WIN_CONFIG)){
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		snprintf(buf, sizeof(buf), "CONFIG.CFG: %s", mainui_get_config_dirty() ? "MODIFIED" : "SAVED");
		mu_label(ctx, buf);
		mu_layout_row(ctx, 1, (int[]){ -1 }, 22);
		if (mu_button_ex(ctx, "1. INPUT", 0, 0)){ mui_open_config_page(ctx, 1); }
		if (mu_button_ex(ctx, "2. VIDEO", 0, 0)){ mui_open_config_page(ctx, 0); }
		if (mu_button_ex(ctx, "3. AUDIO", 0, 0)){ mui_open_config_page(ctx, 3); }
		if (mu_button_ex(ctx, "4. GUI", 0, 0)){ mui_open_config_page(ctx, 5); }
		if (mu_button_ex(ctx, "5. DEVICES", 0, 0)){ mui_open_config_page(ctx, 2); }
		if (mu_button_ex(ctx, "6. PATHS", 0, 0)){ mui_open_config_page(ctx, 4); }
		mui_separator(ctx);
		mu_layout_row(ctx, 3, (int[]){ mui_button_width("SAVE"), mui_button_width("RELOAD"), mui_button_width("DEFAULTS") }, 20);
		if (mu_button(ctx, "SAVE")){
			mainui_config_save_now();
			mui_sync_config_buffers();
		}
		if (mu_button(ctx, "RELOAD")){
			mainui_config_reload_now();
			mui_sync_config_buffers();
		}
		if (mu_button(ctx, "DEFAULTS")){
			mainui_config_defaults_now();
			mui_sync_config_buffers();
		}
		mui_window_end(ctx, MUI_WIN_CONFIG);
	}
}

static void mui_build_config_page_window(mu_Context* ctx)
{
	char buf[160];
	int state;
	mu_Real rval;
	auint audio_output_rate;
	auint audio_latency;
	auint audio_resampler;
	auint audio_lowpass;
	auint audio_lowpass_quality;
	auint audio_monitor_mode;
	auint audio_master_volume;
	auint i;
	mui_window_id_t page_win;
	static const char* audio_output_rate_names[] = { "44.1 KHZ", "48 KHZ", "96 KHZ" };
	static const char* audio_latency_names[] = { "LOW", "NORMAL", "SAFE" };
	static const char* audio_resampler_names[] = { "HOLD", "LINEAR", "CUBIC" };
	static const char* audio_lowpass_names[] = { "OFF", "LIGHT", "MEDIUM", "STRONG" };
	static const char* audio_lowpass_quality_names[] = { "FAST", "HIGH" };
	static const char* audio_monitor_mode_names[] = { "MONO", "STEREO-SIM" };
	static const char* audio_reverb_names[] = { "OFF", "LIGHT", "MEDIUM", "STRONG" };
#ifdef ENABLE_DISPLAY_FILTERS
	auint pre_filter_mode;
	auint post_filter_mode;
#endif
	page_win = mui_cfg_page_to_window_id(mui_cfg_page);
	if ((mui_cfg_page != 5) || (!mui_window_is_open(ctx, MUI_WIN_CFG_GUI))){
		mui_cfg_gui_picker_open = FALSE;
		mui_cfg_gui_picker_just_opened = FALSE;
	}
	if (!mui_window_is_open(ctx, page_win)){ return; }
	if (!mui_cfg_paths_synced){
		mui_sync_config_buffers();
	}
	if (mui_window_begin(ctx, page_win)){
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		snprintf(buf, sizeof(buf), "CONFIG.CFG: %s", mainui_get_config_dirty() ? "MODIFIED" : "SAVED");
		mu_label(ctx, buf);

		if (mui_cfg_page == 0){
			auint render_path = mainui_get_render_path();
			if (render_path > RENDER_PATH_STAGED){ render_path = RENDER_PATH_STAGED; }
			if (mui_labeled_choice_popup(ctx, "PATH", "display_renderpath_popup", mui_render_path_name(render_path), (const char* const[]){ "POTATO", "CLASSIC 1X", "CLASSIC 2X", "STAGED" }, 4U, &render_path, 104, 184)){
				mainui_set_render_path(render_path);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_display_gameonly() ? 1 : 0;
			if (mu_checkbox(ctx, "GAME-ONLY LAYOUT", &state)){
				mainui_set_display_gameonly(state ? TRUE : FALSE);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_display_fullscreen() ? 1 : 0;
			if (mu_checkbox(ctx, "FULLSCREEN", &state)){
				mainui_set_display_fullscreen(state ? TRUE : FALSE);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_system_messages() ? 1 : 0;
			if (mu_checkbox(ctx, "ON-SCREEN MESSAGES", &state)){
				mainui_set_system_messages(state ? TRUE : FALSE);
			}
			{
				static const char* const levels[] = { "OFF", "ERRORS", "INFO", "DEBUG", "TRACE" };
				auint level = mainui_get_log_verbosity();
				if (level > CU_LOG_TRACE){ level = CU_LOG_INFO; }
				if (mui_labeled_choice_popup(ctx, "LOG VERBOSITY", "log_verbosity", levels[level], levels, 5U, &level, 152, 140)){
					mainui_set_log_verbosity(level);
				}
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_gui_gamepad_enable() ? 1 : 0;
			if (mu_checkbox(ctx, "GUI GAMEPAD", &state)){
				mainui_set_gui_gamepad_enable(state ? TRUE : FALSE);
			}
			{
				auint gui_speed_choice;
				static const char* const mui_gui_speed_names[] = { "25%", "50%", "75%", "100%", "125%", "150%", "200%" };
				static const auint mui_gui_speed_values[] = { 25U, 50U, 75U, 100U, 125U, 150U, 200U };
				gui_speed_choice = mui_choice_from_value(mainui_get_gui_gamepad_speed_pct(), mui_gui_speed_values, (auint)(sizeof(mui_gui_speed_values) / sizeof(mui_gui_speed_values[0])), 1U);
				if (mui_labeled_choice_popup(ctx, "GUI CURSOR SPEED", "display_gui_cursor_speed", mui_gui_speed_names[gui_speed_choice], mui_gui_speed_names, (auint)(sizeof(mui_gui_speed_names) / sizeof(mui_gui_speed_names[0])), &gui_speed_choice, 152, 112)){
					mainui_set_gui_gamepad_speed_pct(mui_gui_speed_values[gui_speed_choice]);
				}
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_gui_pause_while_open() ? 1 : 0;
			if (mu_checkbox(ctx, "PAUSE WHILE GUI OPEN", &state)){
				mainui_set_gui_pause_while_open(state ? TRUE : FALSE);
			}
#ifdef ENABLE_GUI_VKEYBOARD
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_gui_virtual_keyboard() ? 1 : 0;
			if (mu_checkbox(ctx, "VIRTUAL KEYBOARD", &state)){
				mainui_set_gui_virtual_keyboard(state ? TRUE : FALSE);
			}
#endif
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_frame_rate_limiter() ? 1 : 0;
			if (mu_checkbox(ctx, "FRAME RATE LIMITER", &state)){
				mainui_set_frame_rate_limiter(state ? TRUE : FALSE);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			state = mainui_get_frame_merge() ? 1 : 0;
			if (mu_checkbox(ctx, "FRAME MERGE", &state)){
				mainui_set_frame_merge(state ? TRUE : FALSE);
			}
			if (RENDER_PATH_IS_CLASSIC(render_path)){
				mu_label(ctx, "CLASSIC MODE USES THE LEGACY FILTERED PRESENTATION PATH.");
			}else if (render_path == RENDER_PATH_POTATO){
				mu_label(ctx, "POTATO DISABLES FILTERS AND FRAME MERGE FOR SPEED.");
			}else{
#ifdef ENABLE_DISPLAY_FILTERS
				pre_filter_mode = mainui_get_filter_pre_mode();
				if (mui_labeled_choice_popup(ctx, "PRE", "display_prefilter_popup", mui_display_prefilter_name(mainui_get_filter_pre_mode()), (const char* const[]){ "NONE", "SOFT RGB", "S-VIDEO", "COMPOSITE", "RF MOD", "BLACK/WHITE" }, 6U, &pre_filter_mode, 104, 184)){
					mainui_set_filter_pre_mode(pre_filter_mode);
				}
				{
					auint scaler_mode = mainui_get_filter_scale_mode();
					if (scaler_mode > FILTER_SCALE_XBR2X){ scaler_mode = FILTER_SCALE_NONE; }
					if (mui_labeled_choice_popup(ctx, "SCALER", "display_scaler_popup", mui_display_scaler_name(mainui_get_filter_scale_mode()), (const char* const[]){ "NEAREST", "SCALE2X", "HQ2X", "XBR2X" }, 4U, &scaler_mode, 104, 184)){
						mainui_set_filter_scale_mode(scaler_mode);
					}
				}

				post_filter_mode = mui_display_postfilter_to_choice(mainui_get_filter_crt_mode());
				if (mui_labeled_choice_popup(ctx, "POST", "display_postfilter_popup", mui_display_postfilter_name(mainui_get_filter_crt_mode()), (const char* const[]){ "NONE", "SCANLINES 25%", "SCANLINES 37%", "SCANLINES 50%", "APERTURE GRILLE", "SHADOW MASK", "CURVATURE", "GRILLE + SCANLINES", "MASK + SCANLINES", "BUMP MAP", "LUCID", "HEATWAVE" }, 12U, &post_filter_mode, 104, 184)){
					mainui_set_filter_crt_mode(mui_display_postfilter_from_choice(post_filter_mode));
				}
#endif
			}
		}else if (mui_cfg_page == 1){
			static const char* const input_tab_names[] = { "LAYOUT", "SDL DEVS", "VIRT DEV" };
			static const char* const layout_mode_names[] = { "JOYPAD", "MOUSE", "SUPER MOUSE", "KEYBOARD", "TAP" };
			auint layout_mode;
			auint port;
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("LAYOUT"), mui_button_width("SDL DEVS"), mui_button_width("VIRT DEV") }, 22);
			if (mu_button(ctx, "LAYOUT")){ mui_input_cfg_tab = 0U; }
			if (mu_button(ctx, "SDL DEVS")){ mui_input_cfg_tab = 1U; }
			if (mu_button(ctx, "VIRT DEV")){ mui_input_cfg_tab = 2U; }
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			snprintf(buf, sizeof(buf), "TAB: %s", input_tab_names[(mui_input_cfg_tab < 3U) ? mui_input_cfg_tab : 0U]);
			mu_label(ctx, buf);
			if (mui_input_cfg_tab == 0U){
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, "PORT LAYOUT");
				for (port = 0U; port < 2U; ++port){
					layout_mode = mui_input_layout_guess_mode(port);
					snprintf(buf, sizeof(buf), "P%u PORT", (unsigned)(port + 1U));
					if (mui_labeled_choice_popup(ctx, buf, (port == 0U) ? "input_layout_p1" : "input_layout_p2", layout_mode_names[layout_mode], layout_mode_names, 5U, &layout_mode, 96, 168)){
						mui_input_layout_apply_mode(port, layout_mode);
					}
				}
				mu_layout_row(ctx, 2, (int[]){ mui_button_width("2P PADS"), mui_button_width("P1 TAP") }, 24);
				if (mu_button(ctx, "2P PADS")){ mui_apply_topology_preset(1U); }
				if (mu_button(ctx, "P1 TAP")){ mui_apply_topology_preset(2U); }
				mu_layout_row(ctx, 2, (int[]){ mui_button_width("DUAL TAP"), mui_button_width("P1 KBD") }, 24);
				if (mu_button(ctx, "DUAL TAP")){ mui_apply_topology_preset(3U); }
				if (mu_button(ctx, "P1 KBD")){ mui_apply_topology_preset(4U); }
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, mainui_input_profile_get_status());
			}else if (mui_input_cfg_tab == 1U){
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, "SDL INPUT DEVICES");
				state = mainui_get_input_kbuzem() ? 1 : 0;
				if (mu_checkbox(ctx, "KEYBOARD AS UZEBOX CONTROLLER", &state)){
					mainui_set_input_kbuzem(state ? TRUE : FALSE);
				}
				state = mainui_get_input_player2alloc() ? 1 : 0;
				if (mu_checkbox(ctx, "KEYBOARD TO PLAYER 2", &state)){
					mainui_set_input_player2alloc(state ? TRUE : FALSE);
				}
				state = mainui_get_mouse_enable() ? 1 : 0;
				if (mu_checkbox(ctx, "SNES MOUSE OVERRIDE P1", &state)){
					mainui_set_mouse_enable(state ? TRUE : FALSE);
				}
				rval = (mu_Real)mainui_get_mouse_scale();
				mu_layout_row(ctx, 2, (int[]){ 96, -1 }, 0);
				mu_label(ctx, "MOUSE SEN.");
				if (mu_slider(ctx, &rval, 0.0f, 5.0f)){
					mainui_set_mouse_scale((auint)(rval + 0.5f));
				}
				mui_build_host_choices();
				mu_layout_row(ctx, 1, (int[]){ -1 }, 160);
				mu_begin_panel(ctx, "input_sdl_devices_panel");
				mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
				if (mui_host_choice_count <= 1U){
					mu_label(ctx, "NO SDL GAMEPADS OR JOYSTICKS DETECTED.");
				}else{
					for (i = 1U; i < mui_host_choice_count; ++i){
						mu_label(ctx, mui_host_choice_ptrs[i]);
					}
				}
				mu_end_panel(ctx);
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, "USE VIRT DEV TO EDIT HOST BINDINGS.");
			}else{
				mui_draw_multitap_vdev_ui(ctx);
			}
		}else if (mui_cfg_page == 2){
			{
				auint spiram_pages_mode = mainui_get_spiram_pages_mode();
				if (spiram_pages_mode > 9U){ spiram_pages_mode = 0U; }
				if (mui_labeled_choice_popup(ctx, "SPI RAM", "spiram_pages_popup", mui_spiram_pages_names[spiram_pages_mode], mui_spiram_pages_names, 10U, &spiram_pages_mode, 132, 220)){
					mainui_set_spiram_pages_mode(spiram_pages_mode);
				}
			}
			{
				auint sd_timing = mainui_get_sd_timing_preset();
				if (sd_timing > CU_SPISD_PRESET_FAST){ sd_timing = CU_SPISD_PRESET_NORMAL; }
				if (mui_labeled_choice_popup(ctx, "SD TIMING", "sd_timing_popup", mui_sd_timing_names[sd_timing], mui_sd_timing_names, 4U, &sd_timing, 132, 220)){
					mainui_set_sd_timing_preset(sd_timing);
				}
				mu_layout_row(ctx, 2, (int[]){ 132, mui_button_width("SD DETAILS...") }, 24);
				mu_label(ctx, (sd_timing == CU_SPISD_PRESET_NORMAL) ? "NORMAL = ORIGINAL" : "");
				if (mu_button(ctx, "SD DETAILS...")){
					mainui_open_web_tool("sd");
				}
			}
			{
				auint serial_route = mainui_get_serial_route();
				auint esp_model = mainui_get_serial_esp_model();
				auint esp_index;
				if (serial_route > CU_ESP_SERIAL_LOOPBACK){ serial_route = CU_ESP_SERIAL_DISCONNECTED; }
				if ((esp_model < 1U) || (esp_model > 3U)){ esp_model = 3U; }
				esp_index = esp_model - 1U;
				if (mui_labeled_choice_popup(ctx, "SERIAL", "serial_route_popup", mui_serial_route_names[serial_route], mui_serial_route_names, 7U, &serial_route, 132, 220)){
					mainui_set_serial_route(serial_route);
					if (!mui_cfg_serial_synced){
						mui_sync_serial_buffers();
					}
				}
				if (serial_route == CU_ESP_SERIAL_ESP_MODULE){
					if (mui_labeled_choice_popup(ctx, "ESP MODEL", "serial_esp_model_popup", mui_esp_model_names[esp_index], mui_esp_model_names, 3U, &esp_index, 132, 220)){
						mainui_set_serial_esp_model(esp_index + 1U);
					}
					if (esp_model == 1U){
						auint fw = mainui_get_esp_at_firmware_profile();
						if (fw > 1U){ fw = 1U; }
						if (mui_labeled_choice_popup(ctx, "ESP FIRMWARE", "esp_at_fw_popup", mui_esp_at_fw_names[fw], mui_esp_at_fw_names, 2U, &fw, 132, 220)){
							mainui_set_esp_at_firmware_profile(fw);
						}
					}
				}else if ((serial_route == CU_ESP_SERIAL_HOST_SERIAL) || (serial_route == CU_ESP_SERIAL_TCP_SERIAL) || (serial_route == CU_ESP_SERIAL_LOOPBACK)){
					mu_layout_row(ctx, 2, (int[]){ 132, -1 }, 24);
					mu_label(ctx, "ESP MODEL");
					mu_label(ctx, "RAW LINK (IGNORED)");
				}
				{
					auint esp_softap_mode = mainui_get_esp_softap_mode();
					if (esp_softap_mode > MAINUI_ESP_SOFTAP_MODE_ENABLE){ esp_softap_mode = MAINUI_ESP_SOFTAP_MODE_HINT; }
					if (mui_labeled_choice_popup(ctx, "ESP AP", "esp_softap_mode_popup", mui_esp_softap_mode_names[esp_softap_mode], mui_esp_softap_mode_names, 3U, &esp_softap_mode, 132, 220)){
						mainui_set_esp_softap_mode(esp_softap_mode);
					}
				}
				if ((serial_route == CU_ESP_SERIAL_HOST_SERIAL) || (serial_route == CU_ESP_SERIAL_HOST_MIDI) || (serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI) || (serial_route == CU_ESP_SERIAL_TCP_SERIAL)){
					mu_layout_row(ctx, 2, (int[]){ 132, mui_button_width("WEB SERIAL...") }, 24);
					mu_label(ctx, "BACKEND DETAILS");
					if (mu_button(ctx, "WEB SERIAL...")){
						mainui_open_web_tool("serial");
					}
				}
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			state = mainui_get_resident_bootloader_enable() ? 1 : 0;
			if (mu_checkbox(ctx, "RESIDENT BOOTLOADER", &state)){
				mainui_set_resident_bootloader_enable(state ? TRUE : FALSE);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			state = mainui_get_fast_flash() ? 1 : 0;
			if (mu_checkbox(ctx, "FAST FLASH", &state)){
				mainui_set_fast_flash(state ? TRUE : FALSE);
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 18);
			mu_label(ctx, "");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			state = mainui_get_sd_allow_new_files() ? 1 : 0;
			if (mu_checkbox(ctx, "ALLOW NEW SD FILES", &state)){
				if (state){
					mui_window_open(ctx, MUI_WIN_SD_WRITE_WARNING);
				}else{
					mainui_set_sd_allow_new_files(FALSE);
				}
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "OFF: EXISTING SD FILES MAY STILL BE MODIFIED.");
			mu_label(ctx, "CREATE/RESIZE/RENAME/DELETE FILES");
			mu_label(ctx, "OR FOLDERS REQUIRE THIS OPTION.");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 42);
			mu_label(ctx, "");
		}else if (mui_cfg_page == 3){
			state = mainui_get_audio_s16() ? 1 : 0;
			if (mu_checkbox(ctx, "S16 OUTPUT EXTENSION", &state)){
				mainui_set_audio_s16(state ? TRUE : FALSE);
			}
			state = mainui_get_audio_dcblock() ? 1 : 0;
			if (mu_checkbox(ctx, "DC BLOCK", &state)){
				mainui_set_audio_dcblock(state ? TRUE : FALSE);
			}
			state = mainui_get_audio_freqscale() ? 1 : 0;
			if (mu_checkbox(ctx, "ENABLE FREQUENCY SCALING", &state)){
				mainui_set_audio_freqscale(state ? TRUE : FALSE);
			}
			audio_output_rate = mainui_get_audio_output_rate();
			if (audio_output_rate > AUDIO_OUTRATE_96000){ audio_output_rate = AUDIO_OUTRATE_48000; }
			if (mui_labeled_choice_popup(ctx, "OUTPUT RATE", "audio_output_rate_popup", audio_output_rate_names[audio_output_rate], audio_output_rate_names, 3U, &audio_output_rate, 184, 196)){
				mainui_set_audio_output_rate(audio_output_rate);
			}
			audio_latency = mainui_get_audio_latency();
			if (audio_latency > AUDIO_LATENCY_SAFE){ audio_latency = AUDIO_LATENCY_NORMAL; }
			if (mui_labeled_choice_popup(ctx, "LATENCY", "audio_latency_popup", audio_latency_names[audio_latency], audio_latency_names, 3U, &audio_latency, 184, 196)){
				mainui_set_audio_latency(audio_latency);
			}
			audio_resampler = mainui_get_audio_resampler();
			if (audio_resampler > AUDIO_RESAMPLER_CUBIC){ audio_resampler = AUDIO_RESAMPLER_LINEAR; }
			if (mui_labeled_choice_popup(ctx, "RESAMPLER", "audio_resampler_popup", audio_resampler_names[audio_resampler], audio_resampler_names, 3U, &audio_resampler, 184, 196)){
				mainui_set_audio_resampler(audio_resampler);
			}
			audio_lowpass = mainui_get_audio_lowpass();
			if (audio_lowpass > AUDIO_LOWPASS_STRONG){ audio_lowpass = AUDIO_LOWPASS_LIGHT; }
			if (mui_labeled_choice_popup(ctx, "LOW-PASS", "audio_lowpass_popup", audio_lowpass_names[audio_lowpass], audio_lowpass_names, 4U, &audio_lowpass, 184, 196)){
				mainui_set_audio_lowpass(audio_lowpass);
			}
			audio_lowpass_quality = mainui_get_audio_lowpass_quality();
			if (audio_lowpass_quality > AUDIO_LOWPASS_QUALITY_HQ){ audio_lowpass_quality = AUDIO_LOWPASS_QUALITY_HQ; }
			if (mui_labeled_choice_popup(ctx, "LP QUALITY", "audio_lowpass_quality_popup", audio_lowpass_quality_names[audio_lowpass_quality], audio_lowpass_quality_names, 2U, &audio_lowpass_quality, 184, 196)){
				mainui_set_audio_lowpass_quality(audio_lowpass_quality);
			}
			audio_monitor_mode = mainui_get_audio_monitor_mode();
			if (audio_monitor_mode > AUDIO_MONITOR_STEREO){ audio_monitor_mode = AUDIO_MONITOR_STEREO; }
			if (mui_labeled_choice_popup(ctx, "MONITOR MODE", "audio_monitor_mode_popup", audio_monitor_mode_names[audio_monitor_mode], audio_monitor_mode_names, 2U, &audio_monitor_mode, 184, 196)){
				mainui_set_audio_monitor_mode(audio_monitor_mode);
			}
			{
				auint audio_monitor_width = mainui_get_audio_monitor_width();
				auint audio_monitor_width_idx = 4U;
				for (i = 0U; i < 9U; i++){
					if (audio_monitor_width <= mui_audio_monitor_width_values[i]){
						audio_monitor_width_idx = i;
						break;
					}
				}
				if (mui_labeled_choice_popup(ctx, "STEREO WIDTH", "audio_monitor_width_popup", mui_audio_monitor_width_names[audio_monitor_width_idx], mui_audio_monitor_width_names, 9U, &audio_monitor_width_idx, 184, 196)){
					mainui_set_audio_monitor_width(mui_audio_monitor_width_values[audio_monitor_width_idx]);
				}
			}
			{
				auint audio_reverb = mainui_get_audio_reverb();
				if (audio_reverb > AUDIO_REVERB_STRONG){ audio_reverb = AUDIO_REVERB_OFF; }
				if (mui_labeled_choice_popup(ctx, "MON REVERB", "audio_reverb_popup", audio_reverb_names[audio_reverb], audio_reverb_names, 4U, &audio_reverb, 184, 196)){
					mainui_set_audio_reverb(audio_reverb);
				}
			}
			audio_master_volume = mainui_get_audio_master_volume();
			if (audio_master_volume > 200U){ audio_master_volume = 200U; }
			snprintf(buf, sizeof(buf), "%u%%", (unsigned)audio_master_volume);
			mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("VOLUME", -1) + 8, mui_button_width("-10"), mui_button_width("-1"), mui_get_text_width("100%", -1) + 12, mui_button_width("+1"), mui_button_width("+10") }, 24);
			mu_label(ctx, "VOLUME");
			if (mu_button(ctx, "-10")){
				mainui_set_audio_master_volume((audio_master_volume > 10U) ? (audio_master_volume - 10U) : 0U);
			}
			if (mu_button(ctx, "-1")){
				mainui_set_audio_master_volume((audio_master_volume > 0U) ? (audio_master_volume - 1U) : 0U);
			}
			mu_label(ctx, buf);
			if (mu_button(ctx, "+1")){
				mainui_set_audio_master_volume((audio_master_volume < 200U) ? (audio_master_volume + 1U) : 200U);
			}
			if (mu_button(ctx, "+10")){
				mainui_set_audio_master_volume((audio_master_volume < 190U) ? (audio_master_volume + 10U) : 200U);
			}
		}else if (mui_cfg_page == 4){
			mui_config_path_row(ctx, "ROM PATH", mui_cfg_rom_path, (int)sizeof(mui_cfg_rom_path), mainui_set_rom_path, 1);
			mui_config_path_row(ctx, "SCREENSHOT PATH", mui_cfg_screenshot_path, (int)sizeof(mui_cfg_screenshot_path), mainui_set_screenshot_path, 2);
			mui_config_path_row(ctx, "STATE PATH", mui_cfg_save_path, (int)sizeof(mui_cfg_save_path), mainui_set_save_path, 3);
			mui_config_path_row(ctx, "CONTROLLER DB PATH", mui_cfg_controllerdb_path, (int)sizeof(mui_cfg_controllerdb_path), mainui_set_controllerdb_path, 4);
			mu_layout_row(ctx, 2, (int[]){ mui_button_width("RELOAD DB"), -1 }, 22);
			if (mu_button(ctx, "RELOAD DB")){
				(void)mainui_reload_controllerdb_now();
			}
			mu_label(ctx, "Loads controller mappings from the selected file or directory.");
			mui_config_file_row(ctx, "BOOTLOADER OVERRIDE", mui_cfg_resident_bootloader_file, (int)sizeof(mui_cfg_resident_bootloader_file), mainui_set_resident_bootloader_file, MUI_FILE_TARGET_BOOTLOADER, ".hex", "SELECT BOOTLOADER OVERRIDE");
		}else if (mui_cfg_page == 5){
			mu_Style* style = ctx->style;
			char const* gui_theme = mainui_get_gui_theme_name();
			auint color_idx;
			auint gui_theme_choice = mui_gui_theme_to_choice(gui_theme);
			if (mui_labeled_choice_popup(ctx, "THEME", "gui_theme_popup", mui_gui_theme_from_choice(gui_theme_choice), mui_gui_theme_names, (auint)(sizeof(mui_gui_theme_names) / sizeof(mui_gui_theme_names[0])), &gui_theme_choice, 112, 216)){
				mainui_apply_gui_theme_preset(mui_gui_theme_from_choice(gui_theme_choice));
				strncpy(mui_cfg_gui_theme_file, mui_gui_theme_file_from_choice(gui_theme_choice), sizeof(mui_cfg_gui_theme_file) - 1U);
				mui_cfg_gui_theme_file[sizeof(mui_cfg_gui_theme_file) - 1U] = 0;
				mainui_set_gui_theme_file(mui_cfg_gui_theme_file);
				strncpy(mui_cfg_gui_theme_status, "BUILT-IN THEME APPLIED", sizeof(mui_cfg_gui_theme_status) - 1U);
				mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
			}
			mu_label(ctx, "THEME FILE");
			mu_layout_row(ctx, 2, (int[]){ -1, mui_button_width("BROWSE") }, 20);
			if (mu_textbox(ctx, mui_cfg_gui_theme_file, (int)sizeof(mui_cfg_gui_theme_file)) & MU_RES_CHANGE){
				mainui_set_gui_theme_file(mui_cfg_gui_theme_file);
			}
			if (mu_button(ctx, "BROWSE")){
				char startdir[MUI_FILEDIALOG_PATH_CAP];
				mui_pending_file_target = MUI_FILE_TARGET_THEME_SELECT;
				mui_file_dialog_start_dir(startdir, sizeof(startdir), mui_cfg_gui_theme_file);
				mui_filedialog_open(&mui_cfg_file_dialog, "SELECT THEME FILE", startdir, ".cfg;*");
			}
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("LOAD FILE"), mui_button_width("SAVE NOW"), mui_button_width("EXPORT...") }, 20);
			if (mu_button(ctx, "LOAD FILE")){
				if (mainui_load_gui_theme_file(mui_cfg_gui_theme_file)){
					strncpy(mui_cfg_gui_theme_status, "THEME FILE LOADED", sizeof(mui_cfg_gui_theme_status) - 1U);
					mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
				}else{
					strncpy(mui_cfg_gui_theme_status, "THEME LOAD FAILED", sizeof(mui_cfg_gui_theme_status) - 1U);
					mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
				}
			}
			if (mu_button(ctx, "SAVE NOW")){
				if (mainui_save_gui_theme_file(mui_cfg_gui_theme_file)){
					strncpy(mui_cfg_gui_theme_status, "THEME FILE SAVED", sizeof(mui_cfg_gui_theme_status) - 1U);
					mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
				}else{
					strncpy(mui_cfg_gui_theme_status, "THEME SAVE FAILED", sizeof(mui_cfg_gui_theme_status) - 1U);
					mui_cfg_gui_theme_status[sizeof(mui_cfg_gui_theme_status) - 1U] = 0;
				}
			}
			if (mu_button(ctx, "EXPORT...")){
				char startdir[MUI_FILEDIALOG_PATH_CAP];
				mui_pending_file_target = MUI_FILE_TARGET_THEME_EXPORT;
				mui_file_dialog_start_dir(startdir, sizeof(startdir), mui_cfg_gui_theme_file);
				mui_filedialog_open(&mui_cfg_file_dialog, "EXPORT THEME FILE", startdir, ".cfg;*");
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, mui_cfg_gui_theme_status);
			for (color_idx = 0U; color_idx < APPCFG_GUI_COLOR_COUNT; color_idx += 2U){
				mu_layout_row(ctx, 4, (int[]){ 140, 78, 140, 78 }, 20);
				mu_label(ctx, mui_gui_color_labels[color_idx]);
				snprintf(buf, sizeof(buf), "#%02X%02X%02X", style->colors[color_idx].r, style->colors[color_idx].g, style->colors[color_idx].b);
				mu_push_id(ctx, &color_idx, (int)sizeof(color_idx));
				if (mui_color_swatch_button(ctx, style->colors[color_idx], buf)){
					mui_gui_color_picker_open(ctx, (int)color_idx);
				}
				mu_pop_id(ctx);
				if ((color_idx + 1U) < APPCFG_GUI_COLOR_COUNT){
					mu_label(ctx, mui_gui_color_labels[color_idx + 1U]);
					snprintf(buf, sizeof(buf), "#%02X%02X%02X", style->colors[color_idx + 1U].r, style->colors[color_idx + 1U].g, style->colors[color_idx + 1U].b);
					state = (int)(color_idx + 1U);
					mu_push_id(ctx, &state, (int)sizeof(state));
					if (mui_color_swatch_button(ctx, style->colors[color_idx + 1U], buf)){
						mui_gui_color_picker_open(ctx, (int)(color_idx + 1U));
					}
					mu_pop_id(ctx);
				}else{
					mu_label(ctx, "");
					mu_label(ctx, "");
				}
			}
		}

		mui_separator(ctx);
		mu_layout_row(ctx, 3, (int[]){ mui_button_width("SAVE"), mui_button_width("RELOAD"), mui_button_width("DEFAULTS") }, 22);
		if (mu_button(ctx, "SAVE")){
			mainui_config_save_now();
			mui_sync_config_buffers();
		}
		if (mu_button(ctx, "RELOAD")){
			mainui_config_reload_now();
			mui_sync_config_buffers();
		}
		if (mu_button(ctx, "DEFAULTS")){
			mainui_config_defaults_now();
			mui_sync_config_buffers();
		}
		mui_window_end(ctx, page_win);
	}
	mui_draw_gui_color_picker_window(ctx);
}

static void mui_build_sd_write_warning_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_SD_WRITE_WARNING)){ return; }
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, "WARNING: THIS GIVES EMULATED ROMS FULL FILE-WRITE ACCESS");
	mu_label(ctx, "WITHIN THE SD CARD TREE: CREATE, RESIZE, RENAME, DELETE FILES/FOLDERS.");
	mu_label(ctx, "A MALICIOUS OR UNTRUSTED ROM COULD DAMAGE OR ERASE FILES,");
	mu_label(ctx, "FILL DISK SPACE, OR ATTEMPT TO EXPLOIT VULNERABILITIES");
	mu_label(ctx, "IN HOST FILE HANDLING. ENABLE ONLY FOR ROMS YOU TRUST.");
	mu_label(ctx, "NESTED DIRECTORIES ARE INCLUDED.");
	mu_label(ctx, "TOTAL WRITABLE SD CONTENT IS CAPPED AT 4 GIB.");
	mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
	mu_label(ctx, "NOTE: NORMAL WRITES TO EXISTING SD FILES ARE ALREADY EMULATED.");
	mu_layout_row(ctx, 2, (int[]){ mui_button_width("ALLOW"), mui_button_width("CANCEL") }, 24);
	if (mu_button(ctx, "ALLOW")){
		mainui_set_sd_allow_new_files(TRUE);
		mui_window_close(ctx, MUI_WIN_SD_WRITE_WARNING);
		mui_system_message("FULL SD FILE WRITES ENABLED");
	}
	if (mu_button(ctx, "CANCEL")){
		mui_window_close(ctx, MUI_WIN_SD_WRITE_WARNING);
	}
	mui_window_end(ctx, MUI_WIN_SD_WRITE_WARNING);
}

static boole mui_find_first_used_cheat(auint* out_idx)
{
	auint         i;
	cheat_entry_t ent;
	for (i = 0U; i < CHEATS_MAX; i++){
		if (!mainui_get_cheat_entry(i, &ent)){ continue; }
		if (!ent.used){ continue; }
		if (out_idx != NULL){ *out_idx = i; }
		return TRUE;
	}
	return FALSE;
}

static boole mui_find_prev_used_cheat(auint cur, auint* out_idx)
{
	cheat_entry_t ent;
	if (cur == 0U){ return FALSE; }
	while (cur > 0U){
		cur--;
		if (!mainui_get_cheat_entry(cur, &ent)){ continue; }
		if (!ent.used){ continue; }
		if (out_idx != NULL){ *out_idx = cur; }
		return TRUE;
	}
	return FALSE;
}

static boole mui_find_next_used_cheat(auint cur, auint* out_idx)
{
	cheat_entry_t ent;
	cur++;
	while (cur < CHEATS_MAX){
		if (!mainui_get_cheat_entry(cur, &ent)){ cur++; continue; }
		if (!ent.used){ cur++; continue; }
		if (out_idx != NULL){ *out_idx = cur; }
		return TRUE;
	}
	return FALSE;
}

static void mui_format_cheat_code(char* out, auint out_size, cheat_entry_t const* ent)
{
	auint width;
	auint start;
	auint end;
	if ((out == NULL) || (out_size == 0U)){
		return;
	}
	out[0] = 0;
	if ((ent == NULL) || (!ent->used)){
		return;
	}
	width = ent->width_bytes;
	if (width >= 4U){ width = 4U; }
	else if (width >= 2U){ width = 2U; }
	else{ width = 1U; }
	start = ent->addr & 0x0FFFU;
	end = start + width - 1U;
	if (width > 1U){
		if (ent->compare_used){
			snprintf(out, out_size, "%03X-%03X?%0*X:%0*X",
				(unsigned)start,
				(unsigned)end,
				(int)(width * 2U),
				(unsigned)(ent->compare & mui_cheat_width_mask(width)),
				(int)(width * 2U),
				(unsigned)(ent->value & mui_cheat_width_mask(width)));
		}else{
			snprintf(out, out_size, "%03X-%03X:%0*X",
				(unsigned)start,
				(unsigned)end,
				(int)(width * 2U),
				(unsigned)(ent->value & mui_cheat_width_mask(width)));
		}
	}else{
		if (ent->compare_used){
			snprintf(out, out_size, "%03X?%02X:%X", (unsigned)start, (unsigned)(ent->compare & 0xFFU), (unsigned)(ent->value & 0xFFU));
		}else{
			snprintf(out, out_size, "%03X:%X", (unsigned)start, (unsigned)(ent->value & 0xFFU));
		}
	}
}

static void mui_sync_cheat_editor(auint idx)
{
	cheat_entry_t ent;
	if (!mainui_get_cheat_entry(idx, &ent) || !ent.used){
		mui_cheat_desc[0] = 0;
		strcpy(mui_cheat_addr, "000");
		strcpy(mui_cheat_value, "0");
		strcpy(mui_cheat_compare, "0");
		mui_cheat_enabled = 1;
		mui_cheat_compare_used = 0;
		mui_cheat_value_type = 0;
		mui_cheat_editor_synced = TRUE;
		return;
	}
	strncpy(mui_cheat_desc, ent.desc, sizeof(mui_cheat_desc) - 1U);
	mui_cheat_desc[sizeof(mui_cheat_desc) - 1U] = 0;
	snprintf(mui_cheat_addr, sizeof(mui_cheat_addr), "%03X", (unsigned)(ent.addr & 0x0FFFU));
	snprintf(mui_cheat_value, sizeof(mui_cheat_value), "%u", (unsigned)(ent.value & 0x00FFU));
	snprintf(mui_cheat_compare, sizeof(mui_cheat_compare), "%u", (unsigned)(ent.compare & mui_cheat_width_mask(ent.width_bytes)));
	mui_cheat_enabled = ent.enabled ? 1 : 0;
	mui_cheat_compare_used = ent.compare_used ? 1 : 0;
	mui_cheat_value_type = mui_cheat_type_from_entry(&ent);
	mui_cheat_editor_synced = TRUE;
}

static auint mui_parse_dec_field(char const* text, auint mask)
{
	char* endp;
	unsigned long v;
	v = strtoul((text == NULL) ? "0" : text, &endp, 10);
	(void)endp;
	return ((auint)v) & mask;
}

static auint mui_cheat_type_width_bytes(int type)
{
	switch (type){
		case 2:
		case 3: return 2U;
		case 4:
		case 5: return 4U;
		default: return 1U;
	}
}

static boole mui_cheat_type_signed(int type)
{
	return ((type & 1) != 0) ? TRUE : FALSE;
}

static int mui_cheat_type_from_entry(cheat_entry_t const* ent)
{
	auint width = (ent == NULL) ? 1U : ent->width_bytes;
	int   base;
	if (width >= 4U){ base = 4; }
	else if (width >= 2U){ base = 2; }
	else{ base = 0; }
	if ((ent != NULL) && ent->signed_value){ base += 1; }
	return base;
}

static uint32 mui_cheat_width_mask(auint width)
{
	switch (width){
		case 4U: return 0xFFFFFFFFU;
		case 2U: return 0x0000FFFFU;
		default: return 0x000000FFU;
	}
}

static uint32 mui_read_sram_typed_value(auint addr, auint width)
{
	uint32 value = 0U;
	auint  i;
	if (width >= 4U){ width = 4U; }
	else if (width >= 2U){ width = 2U; }
	else{ width = 1U; }
	for (i = 0U; i < width; i++){
		value |= (uint32)(mainui_debug_get_mem_region_byte(MAINUI_DBG_MEM_SRAM, addr + i) & 0xFFU) << (i * 8U);
	}
	return value & mui_cheat_width_mask(width);
}

static sint32 mui_value_to_signed(uint32 value, auint width)
{
	if (width >= 4U){
		return (sint32)value;
	}else if (width >= 2U){
		return (sint16)(value & 0xFFFFU);
	}else{
		return (sint8)(value & 0xFFU);
	}
}

static void mui_format_ascii_bytes(char* out, auint out_size, auint addr, auint width)
{
	auint i;
	if ((out == NULL) || (out_size == 0U)){ return; }
	if (width == 0U){ width = 1U; }
	if (width > 4U){ width = 4U; }
	for (i = 0U; (i < width) && ((i + 1U) < out_size); i++){
		out[i] = mui_ascii_byte(mainui_debug_get_mem_region_byte(MAINUI_DBG_MEM_SRAM, addr + i));
	}
	out[i] = 0;
}

static void mui_cheat_search_set_type(int type)
{
	if (type < 0){ type = 0; }
	if (type > 5){ type = 5; }
	if (mui_cheat_search_value_type != type){
		mui_cheat_search_value_type = type;
		mui_cheat_search_reset_all();
	}
}

static char mui_ascii_byte(auint v)
{
	v &= 0xFFU;
	if ((v >= 32U) && (v <= 126U)){
		return (char)v;
	}
	return '.';
}

static void mui_cheat_search_select_first(void)
{
	auint i;
	for (i = 0U; i < 0x1000U; i++){
		if (mui_cheat_search_hits[i] != 0U){
			mui_cheat_search_selected_addr = i;
			return;
		}
	}
	mui_cheat_search_selected_addr = 0U;
}

static boole mui_cheat_search_addr_valid(auint addr)
{
	auint width = mui_cheat_type_width_bytes(mui_cheat_search_value_type);
	return ((addr + width) <= 0x1000U) ? TRUE : FALSE;
}

static void mui_cheat_search_reset_all(void)
{
	auint i;
	auint width = mui_cheat_type_width_bytes(mui_cheat_search_value_type);
	for (i = 0U; i < 0x1000U; i++){
		if (mui_cheat_search_addr_valid(i)){
			mui_cheat_search_hits[i] = 1U;
			mui_cheat_search_prev[i] = mui_read_sram_typed_value(i, width);
		}else{
			mui_cheat_search_hits[i] = 0U;
			mui_cheat_search_prev[i] = 0U;
		}
	}
	mui_cheat_search_count = 0x1000U - width + 1U;
	mui_cheat_search_selected_addr = 0U;
	mui_cheat_search_initialized = TRUE;
	snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u addresses (%u-byte)", (unsigned)mui_cheat_search_count, (unsigned)width);
}

static void mui_cheat_search_filter_known(void)
{
	auint  i;
	auint  width = mui_cheat_type_width_bytes(mui_cheat_search_value_type);
	uint32 want;
	auint  count = 0U;
	if (!mui_cheat_search_initialized){ mui_cheat_search_reset_all(); }
	want = (uint32)mui_parse_hex_field(mui_cheat_search_value, (auint)mui_cheat_width_mask(width));
	for (i = 0U; i < 0x1000U; i++){
		uint32 cur;
		if (mui_cheat_search_hits[i] == 0U){ continue; }
		cur = mui_read_sram_typed_value(i, width);
		if (cur == want){
			mui_cheat_search_prev[i] = cur;
			count++;
		}else{
			mui_cheat_search_hits[i] = 0U;
		}
	}
	mui_cheat_search_count = count;
	mui_cheat_search_select_first();
	snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u match known $%0*X", (unsigned)count, (int)(width * 2U), (unsigned)want);
}

static void mui_cheat_search_filter_compare(int mode)
{
	auint  i;
	auint  width = mui_cheat_type_width_bytes(mui_cheat_search_value_type);
	auint  count = 0U;
	uint32 byv = (uint32)mui_parse_hex_field(mui_cheat_search_by_value, (auint)mui_cheat_width_mask(width));
	if (!mui_cheat_search_initialized){ mui_cheat_search_reset_all(); }
	for (i = 0U; i < 0x1000U; i++){
		uint32 prev;
		uint32 cur;
		int    keep = 0;
		if (mui_cheat_search_hits[i] == 0U){ continue; }
		prev = mui_cheat_search_prev[i] & mui_cheat_width_mask(width);
		cur = mui_read_sram_typed_value(i, width);
		switch (mode){
			case 0: keep = (cur == prev); break;
			case 1: keep = (cur != prev); break;
			case 2:
				if (mui_cheat_search_use_by != 0){ keep = (cur > prev) && ((cur - prev) == byv); }
				else{ keep = (cur > prev); }
				break;
			case 3:
				if (mui_cheat_search_use_by != 0){ keep = (prev > cur) && ((prev - cur) == byv); }
				else{ keep = (cur < prev); }
				break;
			default: break;
		}
		if (keep){
			mui_cheat_search_prev[i] = cur;
			count++;
		}else{
			mui_cheat_search_hits[i] = 0U;
		}
	}
	mui_cheat_search_count = count;
	mui_cheat_search_select_first();
	switch (mode){
		case 0:
			snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u equal to last", (unsigned)count);
			break;
		case 1:
			snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u not equal to last", (unsigned)count);
			break;
		case 2:
			if (mui_cheat_search_use_by != 0){
				snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u greater by $%0*X", (unsigned)count, (int)(width * 2U), (unsigned)byv);
			}else{
				snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u greater than last", (unsigned)count);
			}
			break;
		case 3:
			if (mui_cheat_search_use_by != 0){
				snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u less by $%0*X", (unsigned)count, (int)(width * 2U), (unsigned)byv);
			}else{
				snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u less than last", (unsigned)count);
			}
			break;
		default:
			snprintf(mui_cheat_search_status, sizeof(mui_cheat_search_status), "%u matches", (unsigned)count);
			break;
	}
}

static void mui_cheat_search_sync_pause(void)
{
	boole want = (mui_state.show_tool_cheatsearch && (mui_cheat_search_pause_while_active != 0)) ? TRUE : FALSE;
	if (want){
		if ((!mui_cheat_search_forced_pause) && (!mainui_debug_is_paused())){
			mainui_debug_set_paused(TRUE);
			mui_cheat_search_forced_pause = TRUE;
		}
	}else if (mui_cheat_search_forced_pause){
		mainui_debug_set_paused(FALSE);
		mui_cheat_search_forced_pause = FALSE;
	}
}

static void mui_apply_cheat_editor(void)
{
	cheat_entry_t ent;
	if (!mainui_get_cheat_entry(mui_cheat_selected, &ent)){
		memset(&ent, 0, sizeof(ent));
		ent.used = TRUE;
	}
	ent.used = TRUE;
	ent.enabled = (mui_cheat_enabled != 0) ? TRUE : FALSE;
	ent.compare_used = (mui_cheat_compare_used != 0) ? TRUE : FALSE;
	ent.signed_value = mui_cheat_type_signed(mui_cheat_value_type);
	ent.width_bytes = (uint8)mui_cheat_type_width_bytes(mui_cheat_value_type);
	ent.addr = (auint)strtoul(mui_cheat_addr, NULL, 16) & 0x0FFFU;
	ent.value = (uint32)mui_parse_dec_field(mui_cheat_value, (auint)mui_cheat_width_mask(ent.width_bytes));
	ent.compare = (uint32)mui_parse_dec_field(mui_cheat_compare, (auint)mui_cheat_width_mask(ent.width_bytes));
	strncpy(ent.desc, mui_cheat_desc, sizeof(ent.desc) - 1U);
	ent.desc[sizeof(ent.desc) - 1U] = 0;
	mainui_set_cheat_entry(mui_cheat_selected, &ent);
	mui_sync_cheat_editor(mui_cheat_selected);
}

static void mui_build_cheats_window(mu_Context* ctx)
{
	char          buf[192];
	char          codebuf[64];
	cheat_entry_t ent;
	auint         i;
	int           state;
	boole         have_sel;

	if (!mui_window_is_open(ctx, MUI_WIN_VIEW_CHEATS)){ return; }
	if ((!mui_cheat_editor_synced) || (!mainui_get_cheat_entry(mui_cheat_selected, &ent)) || (!ent.used)){
		if (mui_find_first_used_cheat(&mui_cheat_selected)){
			mui_sync_cheat_editor(mui_cheat_selected);
		}else{
			mui_cheat_selected = 0U;
			mui_sync_cheat_editor(mui_cheat_selected);
		}
	}
	if (!mui_window_begin(ctx, MUI_WIN_VIEW_CHEATS)){ return; }
#ifdef ENABLE_NETPLAY
	{
		netplay_status_t npst;
		netplay_get_status(&npst);
		if (npst.enabled){
			mainui_set_cheats_enabled(FALSE);
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "CHEATS ARE DISABLED DURING NETPLAY.");
			mu_label(ctx, "DISCONNECT NETPLAY TO EDIT OR APPLY CHEATS.");
			mui_window_end(ctx, MUI_WIN_VIEW_CHEATS);
			return;
		}
	}
#endif

		mu_layout_row(ctx, 2, (int[]){ mui_button_width("Enable"), mui_button_width("Auto Load/Save") }, 20);
		state = mainui_get_cheats_enabled() ? 1 : 0;
		if (mu_checkbox(ctx, "Enable", &state)){
			mainui_set_cheats_enabled(state ? TRUE : FALSE);
		}
		state = mainui_get_cheats_autoloadsave() ? 1 : 0;
		if (mu_checkbox(ctx, "Auto Load/Save", &state)){
			mainui_set_cheats_autoloadsave(state ? TRUE : FALSE);
		}

		mu_layout_row(ctx, 6, (int[]){ mui_button_width("U8"), mui_button_width("S8"), mui_button_width("U16"), mui_button_width("S16"), mui_button_width("U32"), mui_button_width("S32") }, 20);
		if (mu_button_ex(ctx, "U8", 0, (mui_cheat_value_type == 0) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_value_type = 0; }
		if (mu_button_ex(ctx, "S8", 0, (mui_cheat_value_type == 1) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_value_type = 1; }
		if (mu_button_ex(ctx, "U16", 0, (mui_cheat_value_type == 2) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_value_type = 2; }
		if (mu_button_ex(ctx, "S16", 0, (mui_cheat_value_type == 3) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_value_type = 3; }
		if (mu_button_ex(ctx, "U32", 0, (mui_cheat_value_type == 4) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_value_type = 4; }
		if (mu_button_ex(ctx, "S32", 0, (mui_cheat_value_type == 5) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_value_type = 5; }

		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		snprintf(buf, sizeof(buf), "%s: %s", mainui_get_cheat_dirty() ? "unsaved" : "saved", mainui_get_cheat_path());
		mu_label(ctx, buf);

		mu_layout_row(ctx, 1, (int[]){ -1 }, 18);
		mu_label(ctx, "CODE | NAME");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 110);
		mu_begin_panel(ctx, "cheat_list_panel");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
			for (i = 0U; i < CHEATS_MAX; i++){
				if (!mainui_get_cheat_entry(i, &ent) || !ent.used){ continue; }
				char namebuf[36];
				mui_format_cheat_code(codebuf, sizeof(codebuf), &ent);
				strncpy(namebuf, ent.desc[0] ? ent.desc : "(no name)", sizeof(namebuf) - 1U);
				namebuf[sizeof(namebuf) - 1U] = 0;
				if (strlen(namebuf) > 24U){
					namebuf[24] = 0;
					strcat(namebuf, "...");
				}
				snprintf(buf, sizeof(buf), "%s | %s", codebuf, namebuf);
				if (mu_button_ex(ctx, buf, 0, (i == mui_cheat_selected) ? MU_OPT_HOLDFOCUS : 0)){
					mui_cheat_selected = i;
					mui_sync_cheat_editor(i);
				}
			}
		mu_end_panel(ctx);

		mu_layout_row(ctx, 4, (int[]){ mui_button_width("UP"), mui_button_width("DOWN"), mui_button_width("LOAD"), mui_button_width("SAVE") }, 22);
		if (mu_button(ctx, "UP")){
			auint idx;
			if (mui_find_prev_used_cheat(mui_cheat_selected, &idx)){
				mui_cheat_selected = idx;
				mui_sync_cheat_editor(idx);
			}
		}
		if (mu_button(ctx, "DOWN")){
			auint idx;
			if (mui_find_next_used_cheat(mui_cheat_selected, &idx)){
				mui_cheat_selected = idx;
				mui_sync_cheat_editor(idx);
			}
		}
		if (mu_button(ctx, "LOAD")){
			mainui_cheats_reload_now();
			mui_cheat_editor_synced = FALSE;
		}
		if (mu_button(ctx, "SAVE")){
			if (have_sel){
				mui_apply_cheat_editor();
			}
			mainui_cheats_save_now();
		}

		have_sel = mainui_get_cheat_entry(mui_cheat_selected, &ent) && ent.used;
		if (!have_sel){
			memset(&ent, 0, sizeof(ent));
			ent.used = TRUE;
			mui_format_cheat_code(codebuf, sizeof(codebuf), &ent);
		}
		mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("NAME:", -1) + 8, -1, mui_button_width("ENABLED"), 24 }, 22);
		mu_label(ctx, "NAME:");
		mu_textbox(ctx, mui_cheat_desc, (int)sizeof(mui_cheat_desc));
		mu_label(ctx, "ENABLED");
		state = mui_cheat_enabled;
		if (mu_checkbox(ctx, "", &state)){
			mui_cheat_enabled = state;
		}

		mu_layout_row(ctx, 7, (int[]){ mui_get_text_width("ADDR:", -1) + 8, 62, mui_get_text_width("VAL:", -1) + 8, 54, mui_get_text_width("COMP", -1) + 8, mui_button_width("X"), 54 }, 22);
		mu_label(ctx, "ADDR:");
		mu_textbox(ctx, mui_cheat_addr, (int)sizeof(mui_cheat_addr));
		mu_label(ctx, "VAL:");
		mu_textbox(ctx, mui_cheat_value, (int)sizeof(mui_cheat_value));
		mu_label(ctx, "COMP");
		state = mui_cheat_compare_used;
		if (mu_checkbox(ctx, "X", &state)){
			mui_cheat_compare_used = state;
		}
		mu_textbox(ctx, mui_cheat_compare, (int)sizeof(mui_cheat_compare));

		memset(&ent, 0, sizeof(ent));
		ent.used = TRUE;
		ent.enabled = (mui_cheat_enabled != 0) ? TRUE : FALSE;
		ent.compare_used = (mui_cheat_compare_used != 0) ? TRUE : FALSE;
		ent.signed_value = mui_cheat_type_signed(mui_cheat_value_type);
		ent.width_bytes = (uint8)mui_cheat_type_width_bytes(mui_cheat_value_type);
		ent.addr = (auint)strtoul(mui_cheat_addr, NULL, 16) & 0x0FFFU;
		ent.value = (uint32)mui_parse_dec_field(mui_cheat_value, (auint)mui_cheat_width_mask(ent.width_bytes));
		ent.compare = (uint32)mui_parse_dec_field(mui_cheat_compare, (auint)mui_cheat_width_mask(ent.width_bytes));
		strncpy(ent.desc, mui_cheat_desc, sizeof(ent.desc) - 1U);
		ent.desc[sizeof(ent.desc) - 1U] = 0;
		mui_format_cheat_code(codebuf, sizeof(codebuf), &ent);
		mu_layout_row(ctx, 2, (int[]){ mui_get_text_width("CODE:", -1) + 8, -1 }, 20);
		mu_label(ctx, "CODE:");
		mu_label(ctx, codebuf);

		mu_layout_row(ctx, 3, (int[]){ mui_button_width("ADD"), mui_button_width("DEL"), mui_button_width("UPDATE") }, 20);
		if (mu_button(ctx, "ADD")){
			auint newidx;
			if (mainui_add_cheat_entry(&newidx)){
				mui_cheat_selected = newidx;
				mui_apply_cheat_editor();
			}
		}
		if (mu_button(ctx, "DEL")){
			if (have_sel){
				mainui_remove_cheat_entry(mui_cheat_selected);
				mui_cheat_editor_synced = FALSE;
			}
		}
		if (mu_button(ctx, "UPDATE")){
			if (!have_sel){
				mainui_add_cheat_entry(&mui_cheat_selected);
			}
			mui_apply_cheat_editor();
		}
	mui_window_end(ctx, MUI_WIN_VIEW_CHEATS);
}

static void mui_build_cheat_search_window(mu_Context* ctx)
{
	char   buf[192];
	char   asciibuf[8];
	auint  i;
	auint  shown;
	auint  width;
	uint32 curv;
	int    state;
	if (!mui_window_is_open(ctx, MUI_WIN_SEARCH_CHEATS)){
		mui_cheat_search_sync_pause();
		return;
	}
	if (!mui_cheat_search_initialized){
		mui_cheat_search_reset_all();
	}
	width = mui_cheat_type_width_bytes(mui_cheat_search_value_type);
	mui_cheat_search_sync_pause();
	if (!mui_window_begin(ctx, MUI_WIN_SEARCH_CHEATS)){
		mui_cheat_search_sync_pause();
		return;
	}

		mu_layout_row(ctx, 6, (int[]){ mui_button_width("U8"), mui_button_width("S8"), mui_button_width("U16"), mui_button_width("S16"), mui_button_width("U32"), mui_button_width("S32") }, 20);
		if (mu_button_ex(ctx, "U8", 0, (mui_cheat_search_value_type == 0) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_search_set_type(0); }
		if (mu_button_ex(ctx, "S8", 0, (mui_cheat_search_value_type == 1) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_search_set_type(1); }
		if (mu_button_ex(ctx, "U16", 0, (mui_cheat_search_value_type == 2) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_search_set_type(2); }
		if (mu_button_ex(ctx, "S16", 0, (mui_cheat_search_value_type == 3) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_search_set_type(3); }
		if (mu_button_ex(ctx, "U32", 0, (mui_cheat_search_value_type == 4) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_search_set_type(4); }
		if (mu_button_ex(ctx, "S32", 0, (mui_cheat_search_value_type == 5) ? MU_OPT_HOLDFOCUS : 0)){ mui_cheat_search_set_type(5); }

		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
		state = mui_cheat_search_pause_while_active;
		if (mu_checkbox(ctx, "PAUSE WHILE ACTIVE", &state)){
			mui_cheat_search_pause_while_active = state;
			mui_cheat_search_sync_pause();
		}

		mu_layout_row(ctx, 1, (int[]){ -1 }, 208);
		mu_begin_panel(ctx, "cheat_search_panel");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
			shown = 0U;
			for (i = 0U; i < 0x1000U; i++){
				sint32 decv;
				if (mui_cheat_search_hits[i] == 0U){ continue; }
				curv = mui_read_sram_typed_value(i, width);
				decv = mui_cheat_type_signed(mui_cheat_search_value_type) ? mui_value_to_signed(curv, width) : (sint32)curv;
				mui_format_ascii_bytes(asciibuf, sizeof(asciibuf), i, width);
				snprintf(buf, sizeof(buf), "%03X  %0*X  %ld  %s", (unsigned)i, (int)(width * 2U), (unsigned)curv, (long)decv, asciibuf);
				if (mu_button_ex(ctx, buf, 0, (i == mui_cheat_search_selected_addr) ? MU_OPT_HOLDFOCUS : 0)){
					mui_cheat_search_selected_addr = i;
				}
				shown++;
			}
			if (shown == 0U){
				mu_label(ctx, "No matching SRAM addresses.");
			}
		mu_end_panel(ctx);

		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, mui_cheat_search_status[0] ? mui_cheat_search_status : "Iteratively filter SRAM values.");

		mu_layout_row(ctx, 2, (int[]){ mui_button_width("RESET"), -1 }, 24);
		if (mu_button(ctx, "RESET")){
			mui_cheat_search_reset_all();
		}
		mu_label(ctx, "");

		mu_layout_row(ctx, 2, (int[]){ mui_button_width("KNOWN"), 74 }, 24);
		if (mu_button(ctx, "KNOWN")){
			mui_cheat_search_filter_known();
		}
		mu_textbox_ex(ctx, mui_cheat_search_value, (int)sizeof(mui_cheat_search_value), MU_OPT_ALIGNCENTER);

		mu_layout_row(ctx, 4, (int[]){ mui_button_width("EQUAL"), mui_button_width("!EQUAL"), mui_button_width("GREATER"), mui_button_width("LESS") }, 24);
		if (mu_button(ctx, "EQUAL")){
			mui_cheat_search_filter_compare(0);
		}
		if (mu_button(ctx, "!EQUAL")){
			mui_cheat_search_filter_compare(1);
		}
		if (mu_button(ctx, "GREATER")){
			mui_cheat_search_filter_compare(2);
		}
		if (mu_button(ctx, "LESS")){
			mui_cheat_search_filter_compare(3);
		}

		mu_layout_row(ctx, 4, (int[]){ mui_checkbox_width("BY"), 74, mui_button_width("ADD CHEAT"), mui_button_width("MEMORY WATCH") }, 24);
		state = mui_cheat_search_use_by;
		if (mu_checkbox(ctx, "BY", &state)){
			mui_cheat_search_use_by = state;
		}
		mu_textbox_ex(ctx, mui_cheat_search_by_value, (int)sizeof(mui_cheat_search_by_value), MU_OPT_ALIGNCENTER);

		if (mu_button(ctx, "ADD CHEAT")){
			auint idx;
			cheat_entry_t ent;
			if (mainui_add_cheat_entry(&idx)){
				memset(&ent, 0, sizeof(ent));
				ent.used = TRUE;
				ent.enabled = TRUE;
				ent.signed_value = mui_cheat_type_signed(mui_cheat_search_value_type);
				ent.width_bytes = (uint8)width;
				ent.addr = mui_cheat_search_selected_addr & 0x0FFFU;
				ent.value = mui_read_sram_typed_value(ent.addr, width);
				snprintf(ent.desc, sizeof(ent.desc), "SRAM $%03X", (unsigned)ent.addr);
				mainui_set_cheat_entry(idx, &ent);
				mui_cheat_selected = idx;
				mui_sync_cheat_editor(idx);
				mui_window_open(ctx, MUI_WIN_VIEW_CHEATS);
				mui_state.show_tool_cheatsearch = FALSE;
				mui_cheat_search_sync_pause();
			}
		}
		if (mu_button(ctx, "MEMORY WATCH")){
			snprintf(mui_tool_mem_base_sram, sizeof(mui_tool_mem_base_sram), "%03X", (unsigned)(mui_cheat_search_selected_addr & 0x0FF0U));
			snprintf(mui_tool_mem_edit_addr, sizeof(mui_tool_mem_edit_addr), "%03X", (unsigned)(mui_cheat_search_selected_addr & 0x0FFFU));
			snprintf(mui_tool_mem_edit_value, sizeof(mui_tool_mem_edit_value), "%02X", (unsigned)(mainui_debug_get_mem_region_byte(MAINUI_DBG_MEM_SRAM, mui_cheat_search_selected_addr) & 0xFFU));
			mui_open_tool_mem_window(ctx, MAINUI_DBG_MEM_SRAM);
		}
		mu_layout_row(ctx, 1, (int[]){ mui_button_width("RAM WATCH") }, 24);
		if (mu_button(ctx, "RAM WATCH")){
#ifdef ENABLE_DEBUGGER
			mainui_open_web_tool("debugger");
#else
			snprintf(mui_tool_mem_base_sram, sizeof(mui_tool_mem_base_sram), "%03X", (unsigned)(mui_cheat_search_selected_addr & 0x0FF0U));
			mui_open_tool_mem_window(ctx, MAINUI_DBG_MEM_SRAM);
#endif
		}
	mui_window_end(ctx, MUI_WIN_SEARCH_CHEATS);
}

static auint mui_parse_hex_field(char const* text, auint mask)
{
	char* endp;
	unsigned long v;
	v = strtoul((text == NULL) ? "0" : text, &endp, 16);
	(void)endp;
	return ((auint)v) & mask;
}

#ifdef ENABLE_DEBUGGER
static char mui_ascii_char(auint v)
{
	v &= 0xFFU;
	if ((v >= 32U) && (v <= 126U)){
		return (char)v;
	}
	return '.';
}

static char const* mui_dbg_region_name(auint region)
{
	switch (region){
		case MAINUI_DBG_MEM_SRAM:   return "SRAM";
		case MAINUI_DBG_MEM_SPIRAM: return "SPI RAM";
		case MAINUI_DBG_MEM_EEPROM: return "EEPROM";
		case MAINUI_DBG_MEM_FLASH:  return "Flash";
		default:                    return "?";
	}
}

static auint mui_dbg_region_digits(auint region)
{
	auint size = mainui_debug_get_mem_region_size(region);
	if (size > 0x10000U){ return 6U; }
	if (size > 0x1000U){ return 4U; }
	return 3U;
}

static auint mui_dbg_clamp_addr(auint region, auint addr)
{
	auint size = mainui_debug_get_mem_region_size(region);
	if (size == 0U){ return 0U; }
	if (addr >= size){ return size - 1U; }
	return addr;
}

static char const* mui_dbg_watch_region_name(auint region)
{
	switch (region){
		case MAINUI_DBG_WATCH_REGION_SRAM: return "SRAM";
		case MAINUI_DBG_WATCH_REGION_IO:   return "I/O";
		default:                          return "?";
	}
}

static auint mui_dbg_watch_digits(auint region)
{
	return (region == MAINUI_DBG_WATCH_REGION_IO) ? 2U : 3U;
}

static auint mui_dbg_watch_mask(auint region)
{
	return (region == MAINUI_DBG_WATCH_REGION_IO) ? 0x00FFU : 0x0FFFU;
}

static auint mui_dbg_watch_read_byte(auint region, auint addr)
{
	if (region == MAINUI_DBG_WATCH_REGION_IO){
		return mainui_debug_get_io_byte(addr & 0x00FFU);
	}
	return mainui_debug_get_sram_byte(addr & 0x0FFFU);
}

static void mui_dbg_load_watchpoint_slot(auint slot)
{
	boole enable = FALSE;
	auint region = MAINUI_DBG_WATCH_REGION_SRAM;
	auint flags = MAINUI_DBG_WATCH_WRITE;
	auint start_addr = 0U;
	auint end_addr = 0U;
	mainui_debug_watchpoint_get(slot, &enable, &region, &flags, &start_addr, &end_addr);
	mui_dbg_wp_slot = (int)(slot % MAINUI_DBG_WATCH_SLOTS);
	mui_dbg_wp_enable = enable ? 1 : 0;
	mui_dbg_wp_region = (int)region;
	mui_dbg_wp_read = ((flags & MAINUI_DBG_WATCH_READ) != 0U) ? 1 : 0;
	mui_dbg_wp_write = ((flags & MAINUI_DBG_WATCH_WRITE) != 0U) ? 1 : 0;
	snprintf(mui_dbg_wp_start, sizeof(mui_dbg_wp_start), "%0*X", (int)mui_dbg_watch_digits(region), (unsigned)start_addr);
	snprintf(mui_dbg_wp_end, sizeof(mui_dbg_wp_end), "%0*X", (int)mui_dbg_watch_digits(region), (unsigned)end_addr);
}

static auint mui_dbg_pixel_gray(auint region, auint base, auint bpp, auint pixel_index)
{
	auint bit_index;
	auint byte_index;
	auint shift;
	auint raw;
	auint mask;
	auint bytev;

	if ((bpp != 1U) && (bpp != 2U) && (bpp != 4U) && (bpp != 8U)){
		return 0U;
	}
	bit_index = pixel_index * bpp;
	byte_index = base + (bit_index >> 3);
	bytev = mainui_debug_get_mem_region_byte(region, byte_index);
	shift = 8U - bpp - (bit_index & 7U);
	mask = (((auint)1U << bpp) - 1U);
	raw = (bytev >> shift) & mask;
	switch (bpp){
		case 8U: return raw;
		case 4U: return raw * 17U;
		case 2U: return raw * 85U;
		default: return raw ? 255U : 0U;
	}
}

static void mui_draw_gfx_preview(mu_Context* ctx, mu_Rect rect, auint region, auint base, auint bpp, auint width_px, auint zoom)
{
	auint x;
	auint y;
	auint rows;
	auint gray;
	mu_Color c;
	mu_draw_rect(ctx, rect, ctx->style->colors[MU_COLOR_PANELBG]);
	mu_draw_box(ctx, rect, ctx->style->colors[MU_COLOR_BORDER]);
	if (zoom == 0U){ zoom = 1U; }
	if (width_px == 0U){ width_px = 1U; }
	if ((auint)rect.w <= 2U || (auint)rect.h <= 2U){ return; }
	if ((width_px * zoom) > (auint)(rect.w - 2)){
		width_px = (auint)(rect.w - 2) / zoom;
		if (width_px == 0U){ width_px = 1U; }
	}
	rows = (auint)(rect.h - 2) / zoom;
	for (y = 0U; y < rows; y++){
		for (x = 0U; x < width_px; x++){
			gray = mui_dbg_pixel_gray(region, base, bpp, (y * width_px) + x);
			c = mu_color(gray, gray, gray, 255);
			mui_draw_rect(mu_rect(rect.x + 1 + (int)(x * zoom), rect.y + 1 + (int)(y * zoom), (int)zoom, (int)zoom), c);
		}
	}
}
#endif

static void mui_format_sreg(char* buf, auint size, uint8 sreg)
{
	if ((buf == NULL) || (size == 0U)){ return; }
	snprintf(buf, size, "%c%c%c%c%c%c%c%c",
	         (sreg & 0x80U) ? 'I' : 'i',
	         (sreg & 0x40U) ? 'T' : 't',
	         (sreg & 0x20U) ? 'H' : 'h',
	         (sreg & 0x10U) ? 'S' : 's',
	         (sreg & 0x08U) ? 'V' : 'v',
	         (sreg & 0x04U) ? 'N' : 'n',
	         (sreg & 0x02U) ? 'Z' : 'z',
	         (sreg & 0x01U) ? 'C' : 'c');
}

#ifdef ENABLE_DEBUGGER
static auint mui_dbg_get_prog_base_word(void)
{
	return mui_parse_hex_field(mui_dbg_prog_base, 0x7FFFU) & 0x7FFFU;
}

static void mui_dbg_set_prog_base_word(auint word_addr)
{
	snprintf(mui_dbg_prog_base, sizeof(mui_dbg_prog_base), "%04X", (unsigned)(word_addr & 0x7FFFU));
}

static auint mui_dbg_prev_prog_addr(auint addr)
{
	char  disasm[8];
	auint cand;
	auint words;

	addr &= 0x7FFFU;
	if (addr == 0U){ return 0U; }

	cand = (addr - 1U) & 0x7FFFU;
	mainui_debug_get_disasm(cand, disasm, sizeof(disasm), &words);
	if (((cand + ((words == 0U) ? 1U : words)) & 0x7FFFU) == addr){
		return cand;
	}

	if (addr >= 2U){
		cand = (addr - 2U) & 0x7FFFU;
		mainui_debug_get_disasm(cand, disasm, sizeof(disasm), &words);
		if (((cand + ((words == 0U) ? 1U : words)) & 0x7FFFU) == addr){
			return cand;
		}
	}

	return (addr - 1U) & 0x7FFFU;
}

static auint mui_dbg_next_prog_addr(auint addr)
{
	char  disasm[8];
	auint words;

	addr &= 0x7FFFU;
	mainui_debug_get_disasm(addr, disasm, sizeof(disasm), &words);
	return (addr + ((words == 0U) ? 1U : words)) & 0x7FFFU;
}

static auint mui_dbg_move_prog_addr(auint addr, int lines)
{
	addr &= 0x7FFFU;
	while (lines < 0){
		addr = mui_dbg_prev_prog_addr(addr);
		lines++;
	}
	while (lines > 0){
		addr = mui_dbg_next_prog_addr(addr);
		lines--;
	}
	return addr;
}

static auint mui_dbg_prog_base_from_pc(auint pc, auint lines_before)
{
	auint base = pc & 0x7FFFU;
	while (lines_before > 0U){
		base = mui_dbg_prev_prog_addr(base);
		lines_before--;
	}
	return base;
}

static void mui_dbg_reset_panel_scroll(mu_Context* ctx, const char* name)
{
	mu_Container* cnt;
	if ((ctx == NULL) || (name == NULL)){ return; }
	cnt = mu_get_container(ctx, name);
	if (cnt == NULL){ return; }
	cnt->scroll.x = 0;
	cnt->scroll.y = 0;
}

static void mui_build_debugger_window(mu_Context* ctx)
{
	mainui_debug_cpu_t dbg;
	char               buf[512];
	char               flags[16];
	char               disasm[384];
	char               hexbuf[128];
	char               asciibuf[32];
	auint              i;
	auint              j;
	auint              base;
	auint              size;
	auint              word_base;
	auint              words;
	auint              addr;
	auint              sp_line_base;
	auint              edit_addr;
	auint              bp_addr;
	auint              bp_hit_addr;
	auint              bp_list_addr;
	auint              bp_list_idx;
	auint              wp_slot;
	auint              wp_region;
	auint              wp_flags;
	auint              wp_addr;
	auint              wp_value;
	auint              wp_pc;
	auint              watch_flags;
	auint              watch_addr;
	auint              watch_value;
	auint              digits;
	auint              gfx_base;
	auint              gfx_width;
	auint              gfx_zoom;
	auint              gfx_bpp;
	mu_Rect            preview_rect;
	int                state;
	if (!mui_window_begin(ctx, MUI_WIN_DEBUGGER)){ return; }
	(void)mainui_debug_profile_ensure_loaded();
	mainui_debug_get_cpu(&dbg);
	if (mui_dbg_follow_pc){
		word_base = (dbg.pc >= 4U) ? (dbg.pc - 4U) : 0U;
		snprintf(mui_dbg_prog_base, sizeof(mui_dbg_prog_base), "%04X", (unsigned)(word_base & 0x7FFFU));
	}
	{
	
		mui_format_sreg(flags, sizeof(flags), dbg.sreg);
		mu_layout_row(ctx, 6, (int[]){ mui_button_width(mainui_debug_is_paused() ? "Run" : "Pause"), mui_button_width("Step Inst"), mui_button_width("Step Frame"), mui_button_width("Step Over"), mui_button_width("Step Out"), mui_checkbox_width("Follow PC") }, 24);
		if (mu_button(ctx, mainui_debug_is_paused() ? "Run" : "Pause")){
			boole paused = mainui_debug_is_paused() ? FALSE : TRUE;
			mainui_debug_set_paused(paused);
		}
		if (mu_button(ctx, "Step Inst")){
			mainui_debug_set_paused(TRUE);
			mainui_debug_step_instruction();
		}
		if (mu_button(ctx, "Step Frame")){
			mainui_debug_set_paused(TRUE);
			mainui_debug_step_frame();
		}
		if (mu_button(ctx, "Step Over")){
			mainui_debug_set_paused(TRUE);
			(void)mainui_debug_step_over();
		}
		if (mu_button(ctx, "Step Out")){
			mainui_debug_set_paused(TRUE);
			(void)mainui_debug_step_out();
		}
		state = mui_dbg_follow_pc;
		if (mu_checkbox(ctx, "Follow PC", &state)){
			mui_dbg_follow_pc = state;
		}

		snprintf(buf, sizeof(buf), "PC: %04X  SP: %04X  CYC: %u",
		         (unsigned)(dbg.pc & 0x7FFFU),
		         (unsigned)(dbg.sp & 0xFFFFU),
		         (unsigned)dbg.cycle);
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "SREG: %02X [%s]  PULSE: %u",
		         (unsigned)dbg.sreg, flags,
		         (unsigned)dbg.row_pulse);
		mu_label(ctx, buf);
		if (mainui_debug_get_last_break_hit(&bp_hit_addr)) {
			snprintf(buf, sizeof(buf), "Last breakpoint hit: %04X", (unsigned)(bp_hit_addr & 0x7FFFU));
			mu_label(ctx, buf);
		}
		if (mainui_debug_get_run_target(&bp_hit_addr)){
			snprintf(buf, sizeof(buf), "Pending run target: %04X", (unsigned)(bp_hit_addr & 0x7FFFU));
			mu_label(ctx, buf);
		}
		mu_label(ctx, mainui_debug_get_run_control_status());
		if (mainui_debug_get_last_watch_hit(&wp_slot, &wp_region, &wp_flags, &wp_addr, &wp_value, &wp_pc)) {
			snprintf(buf, sizeof(buf), "Last watchpoint hit: slot %u  %s %s %0*X=%02X  @PC %04X",
			         (unsigned)wp_slot,
			         mui_dbg_watch_region_name(wp_region),
			         ((wp_flags & MAINUI_DBG_WATCH_WRITE) != 0U) ? "W" : "R",
			         (int)mui_dbg_watch_digits(wp_region),
			         (unsigned)wp_addr,
			         (unsigned)(wp_value & 0xFFU),
			         (unsigned)(wp_pc & 0x7FFFU));
			mu_label(ctx, buf);
		}

		if (mu_header(ctx, "Registers")){
			for (i = 0U; i < 32U; i += 4U){
				snprintf(buf, sizeof(buf),
				         "R%02u=%02X  R%02u=%02X  R%02u=%02X  R%02u=%02X",
				         (unsigned)i, (unsigned)dbg.regs[i],
				         (unsigned)(i + 1U), (unsigned)dbg.regs[i + 1U],
				         (unsigned)(i + 2U), (unsigned)dbg.regs[i + 2U],
				         (unsigned)(i + 3U), (unsigned)dbg.regs[i + 3U]);
				mu_label(ctx, buf);
			}
			snprintf(buf, sizeof(buf),
			         "X=%04X  Y=%04X  Z=%04X",
			         (unsigned)(((auint)dbg.regs[27] << 8) | dbg.regs[26]),
			         (unsigned)(((auint)dbg.regs[29] << 8) | dbg.regs[28]),
			         (unsigned)(((auint)dbg.regs[31] << 8) | dbg.regs[30]));
			mu_label(ctx, buf);
		}

		if (mu_header(ctx, "Profile")){
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("Save"), mui_button_width("Reload"), -1 }, 24);
			if (mu_button(ctx, "Save")){ (void)mainui_debug_profile_save_now(); }
			if (mu_button(ctx, "Reload")){ (void)mainui_debug_profile_reload(); strncpy(mui_dbg_symbols_file, mainui_debug_get_symbols_file(), sizeof(mui_dbg_symbols_file) - 1U); mui_dbg_symbols_file[sizeof(mui_dbg_symbols_file) - 1U] = 0; }
			mu_label(ctx, mainui_debug_get_profile_dir());
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, mainui_debug_get_profile_status());
		}

		if (mu_header(ctx, "Symbols")){
			mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("File", -1) + 8, -1, mui_button_width("Browse"), mui_button_width("Load") }, 24);
			mu_label(ctx, "File");
			if (mu_textbox(ctx, mui_dbg_symbols_file, (int)sizeof(mui_dbg_symbols_file))){
				mainui_debug_set_symbols_file(mui_dbg_symbols_file);
				mainui_debug_profile_mark_dirty();
			}
			if (mu_button(ctx, "Browse")){
				char startdir[MUI_FILEDIALOG_PATH_CAP];
				mui_file_dialog_start_dir(startdir, sizeof(startdir), mui_dbg_symbols_file);
				mui_pending_file_target = MUI_FILE_TARGET_DEBUG_SYMBOL;
				mui_filedialog_open(&mui_cfg_file_dialog, "SELECT SYMBOL / ELF / MAP FILE", startdir, ".sym;.txt;.map;.elf;*");
			}
			if (mu_button(ctx, "Load")){
				mainui_debug_set_symbols_file(mui_dbg_symbols_file);
				mainui_debug_load_symbols_file(mui_dbg_symbols_file);
			}
			mu_layout_row(ctx, 3, (int[]){ mui_button_width("Reload"), mui_button_width("Clear"), -1 }, 24);
			if (mu_button(ctx, "Reload")){
				strncpy(mui_dbg_symbols_file, mainui_debug_get_symbols_file(), sizeof(mui_dbg_symbols_file) - 1U);
				mui_dbg_symbols_file[sizeof(mui_dbg_symbols_file) - 1U] = 0;
				mainui_debug_load_symbols_file(mui_dbg_symbols_file);
			}
			if (mu_button(ctx, "Clear")){
				mainui_debug_clear_symbols();
			}
			snprintf(buf, sizeof(buf), "Loaded symbols: %u", (unsigned)mainui_debug_get_symbol_count());
			mu_label(ctx, buf);
			mu_label(ctx, mainui_debug_get_symbols_status());
			mu_label(ctx, "Format: text prog/data/io...  |  .elf auto-import  |  .map auto-import");
		}

		if (mu_header(ctx, "Program")){
			int prog_reset_scroll = 0;
			mu_layout_row(ctx, 7, (int[]){ mui_get_text_width("Word base", -1) + 8, mui_button_width("0000"), mui_button_width("Use PC"), mui_button_width("Up"), mui_button_width("Down"), mui_button_width("PgUp"), mui_button_width("PgDn") }, 24);
			mu_label(ctx, "Word base");
			if (mu_textbox(ctx, mui_dbg_prog_base, (int)sizeof(mui_dbg_prog_base))){
				mui_dbg_follow_pc = 0;
				prog_reset_scroll = 1;
			}
			if (mu_button(ctx, "Use PC")){
				mui_dbg_follow_pc = 0;
				mui_dbg_set_prog_base_word(mui_dbg_prog_base_from_pc(dbg.pc, 4U));
				prog_reset_scroll = 1;
			}
			if (mu_button(ctx, "Up")){
				mui_dbg_follow_pc = 0;
				mui_dbg_set_prog_base_word(mui_dbg_move_prog_addr(mui_dbg_get_prog_base_word(), -1));
				prog_reset_scroll = 1;
			}
			if (mu_button(ctx, "Down")){
				mui_dbg_follow_pc = 0;
				mui_dbg_set_prog_base_word(mui_dbg_move_prog_addr(mui_dbg_get_prog_base_word(), 1));
				prog_reset_scroll = 1;
			}
			if (mu_button(ctx, "PgUp")){
				mui_dbg_follow_pc = 0;
				mui_dbg_set_prog_base_word(mui_dbg_move_prog_addr(mui_dbg_get_prog_base_word(), -16));
				prog_reset_scroll = 1;
			}
			if (mu_button(ctx, "PgDn")){
				mui_dbg_follow_pc = 0;
				mui_dbg_set_prog_base_word(mui_dbg_move_prog_addr(mui_dbg_get_prog_base_word(), 16));
				prog_reset_scroll = 1;
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "B=breakpoint  >= current PC");
			word_base = mui_dbg_follow_pc ? mui_dbg_prog_base_from_pc(dbg.pc, 4U) : mui_dbg_get_prog_base_word();
			if (mui_dbg_follow_pc){
				mui_dbg_set_prog_base_word(word_base);
				prog_reset_scroll = 1;
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 300);
			mu_begin_panel(ctx, "dbg_prog_panel");
			if (prog_reset_scroll){
				mui_dbg_reset_panel_scroll(ctx, "dbg_prog_panel");
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
			addr = word_base & 0x7FFFU;
			for (i = 0U; i < 96U; i++){
				mainui_debug_get_disasm(addr, disasm, sizeof(disasm), &words);
				if (words > 1U){
					snprintf(buf, sizeof(buf), "%c%c %04X: %04X %04X  %s",
					         mainui_debug_get_breakpoint(addr) ? 'B' : ' ',
					         (((dbg.pc & 0x7FFFU) >= addr) && ((dbg.pc & 0x7FFFU) < (addr + words))) ? '>' : ' ',
					         (unsigned)addr,
					         (unsigned)(mainui_debug_get_prog_word(addr) & 0xFFFFU),
					         (unsigned)(mainui_debug_get_prog_word(addr + 1U) & 0xFFFFU),
					         disasm);
				}else{
					snprintf(buf, sizeof(buf), "%c%c %04X: %04X       %s",
					         mainui_debug_get_breakpoint(addr) ? 'B' : ' ',
					         (((dbg.pc & 0x7FFFU) >= addr) && ((dbg.pc & 0x7FFFU) < (addr + words))) ? '>' : ' ',
					         (unsigned)addr,
					         (unsigned)(mainui_debug_get_prog_word(addr) & 0xFFFFU),
					         disasm);
				}
				mu_label(ctx, buf);
				addr = (addr + ((words == 0U) ? 1U : words)) & 0x7FFFU;
			}
			mu_end_panel(ctx);
		}

		if (mu_header(ctx, "Stack")){
			sp_line_base = ((dbg.sp & 0x0FFFU) >= 16U) ? ((dbg.sp & 0x0FFFU) - 16U) : 0U;
			sp_line_base &= ~0x07U;
			for (i = 0U; i < 6U; i++){
				auint a = (sp_line_base + (i * 8U)) & 0x0FFFU;
				snprintf(buf, sizeof(buf),
				         "%c%03X: %02X %02X %02X %02X %02X %02X %02X %02X",
				         (((dbg.sp & 0x0FFFU) >= a) && ((dbg.sp & 0x0FFFU) < (a + 8U))) ? '>' : ' ',
				         (unsigned)a,
				         (unsigned)mainui_debug_get_sram_byte(a + 0U),
				         (unsigned)mainui_debug_get_sram_byte(a + 1U),
				         (unsigned)mainui_debug_get_sram_byte(a + 2U),
				         (unsigned)mainui_debug_get_sram_byte(a + 3U),
				         (unsigned)mainui_debug_get_sram_byte(a + 4U),
				         (unsigned)mainui_debug_get_sram_byte(a + 5U),
				         (unsigned)mainui_debug_get_sram_byte(a + 6U),
				         (unsigned)mainui_debug_get_sram_byte(a + 7U));
				mu_label(ctx, buf);
			}
			mu_label(ctx, "> line contains SP");
		}

		if (mu_header(ctx, "Watches")){
			mu_label(ctx, "Fixed live memory watches. Labels come from imported symbols when available.");
			for (i = 0U; i < MAINUI_DBG_VALUE_WATCHES; i++){
				int en = mui_dbg_watch_enable[i];
				char const* wlabel;
				mu_layout_row(ctx, 6, (int[]){ 20, 28, mui_button_width("SRAM"), mui_button_width("I/O"), 76, -1 }, 24);
				snprintf(buf, sizeof(buf), "%u", (unsigned)(i + 1U));
				mu_label(ctx, buf);
				if (mu_checkbox(ctx, "", &en)){ mui_dbg_watch_enable[i] = en; mainui_debug_profile_mark_dirty(); }
				if (mu_button(ctx, "SRAM")){ mui_dbg_watch_region[i] = MAINUI_DBG_WATCH_REGION_SRAM; mainui_debug_profile_mark_dirty(); }
				if (mu_button(ctx, "I/O")){ mui_dbg_watch_region[i] = MAINUI_DBG_WATCH_REGION_IO; mainui_debug_profile_mark_dirty(); }
				if (mu_textbox(ctx, mui_dbg_watch_addr[i], (int)sizeof(mui_dbg_watch_addr[i]))){ mainui_debug_profile_mark_dirty(); }
				if (mui_dbg_watch_enable[i]){
					watch_addr = mui_parse_hex_field(mui_dbg_watch_addr[i], mui_dbg_watch_mask(mui_dbg_watch_region[i]));
					watch_value = mui_dbg_watch_read_byte(mui_dbg_watch_region[i], watch_addr);
					watch_flags = (mui_dbg_watch_seen[i] && (mui_dbg_watch_last[i] != watch_value)) ? 1U : 0U;
					mui_dbg_watch_last[i] = watch_value;
					mui_dbg_watch_seen[i] = 1;
					wlabel = mainui_debug_get_data_label(watch_addr);
					if ((wlabel != NULL) && (wlabel[0] != 0)){
						snprintf(buf, sizeof(buf), "%c%s %0*X  %s=%02X",
						         (watch_flags != 0U) ? '*' : ' ',
						         mui_dbg_watch_region_name(mui_dbg_watch_region[i]),
						         (int)mui_dbg_watch_digits(mui_dbg_watch_region[i]),
						         (unsigned)watch_addr,
						         wlabel,
						         (unsigned)(watch_value & 0xFFU));
					}else{
						snprintf(buf, sizeof(buf), "%c%s %0*X  =%02X",
						         (watch_flags != 0U) ? '*' : ' ',
						         mui_dbg_watch_region_name(mui_dbg_watch_region[i]),
						         (int)mui_dbg_watch_digits(mui_dbg_watch_region[i]),
						         (unsigned)watch_addr,
						         (unsigned)(watch_value & 0xFFU));
					}
				}else{
					snprintf(buf, sizeof(buf), "disabled");
				}
				mu_label(ctx, buf);
			}
			mu_label(ctx, "*=value changed since previous debugger refresh");
		}

		if (mu_header(ctx, "Watchpoints")){
			mu_layout_row(ctx, 7, (int[]){ mui_get_text_width("Slot", -1) + 8, 40, mui_button_width("Prev"), mui_button_width("Next"), mui_button_width("Load"), mui_button_width("Apply"), mui_button_width("Clear All") }, 24);
			mu_label(ctx, "Slot");
			snprintf(buf, sizeof(buf), "%u", (unsigned)(mui_dbg_wp_slot + 1));
			mu_label(ctx, buf);
			if (mu_button(ctx, "Prev")){ mui_dbg_load_watchpoint_slot((auint)((mui_dbg_wp_slot + MAINUI_DBG_WATCH_SLOTS - 1) % MAINUI_DBG_WATCH_SLOTS)); }
			if (mu_button(ctx, "Next")){ mui_dbg_load_watchpoint_slot((auint)((mui_dbg_wp_slot + 1) % MAINUI_DBG_WATCH_SLOTS)); }
			if (mu_button(ctx, "Load")){ mui_dbg_load_watchpoint_slot((auint)mui_dbg_wp_slot); }
			if (mu_button(ctx, "Apply")){
				wp_flags = 0U;
				if (mui_dbg_wp_read != 0){ wp_flags |= MAINUI_DBG_WATCH_READ; }
				if (mui_dbg_wp_write != 0){ wp_flags |= MAINUI_DBG_WATCH_WRITE; }
				mainui_debug_watchpoint_set((auint)mui_dbg_wp_slot, mui_dbg_wp_enable != 0, (auint)mui_dbg_wp_region, wp_flags,
					mui_parse_hex_field(mui_dbg_wp_start, mui_dbg_watch_mask((auint)mui_dbg_wp_region)),
					mui_parse_hex_field(mui_dbg_wp_end, mui_dbg_watch_mask((auint)mui_dbg_wp_region)));
			}
			if (mu_button(ctx, "Clear All")){
				mainui_debug_clear_watchpoints();
			}

			mu_layout_row(ctx, 6, (int[]){ mui_checkbox_width("Enable"), mui_button_width("SRAM"), mui_button_width("I/O"), mui_checkbox_width("Read"), mui_checkbox_width("Write"), -1 }, 24);
			state = mui_dbg_wp_enable;
			if (mu_checkbox(ctx, "Enable", &state)){ mui_dbg_wp_enable = state; }
			if (mu_button(ctx, "SRAM")){ mui_dbg_wp_region = MAINUI_DBG_WATCH_REGION_SRAM; }
			if (mu_button(ctx, "I/O")){ mui_dbg_wp_region = MAINUI_DBG_WATCH_REGION_IO; }
			state = mui_dbg_wp_read;
			if (mu_checkbox(ctx, "Read", &state)){ mui_dbg_wp_read = state; }
			state = mui_dbg_wp_write;
			if (mu_checkbox(ctx, "Write", &state)){ mui_dbg_wp_write = state; }

			mu_layout_row(ctx, 5, (int[]){ mui_get_text_width("Start", -1) + 8, 84, mui_get_text_width("End", -1) + 8, 84, -1 }, 24);
			mu_label(ctx, "Start");
			mu_textbox(ctx, mui_dbg_wp_start, (int)sizeof(mui_dbg_wp_start));
			mu_label(ctx, "End");
			mu_textbox(ctx, mui_dbg_wp_end, (int)sizeof(mui_dbg_wp_end));
			snprintf(buf, sizeof(buf), "%s watchpoint on %s range %s..%s", mui_dbg_wp_enable ? "Enabled" : "Disabled", mui_dbg_watch_region_name((auint)mui_dbg_wp_region), mui_dbg_wp_start, mui_dbg_wp_end);
			mu_label(ctx, buf);

			bp_list_idx = 0U;
			bp_list_addr = 0U;
			while ((bp_list_idx < MAINUI_DBG_WATCH_SLOTS) && mainui_debug_watchpoint_next(bp_list_addr, &wp_slot)){
				boole wp_enable = FALSE;
				auint wp_start = 0U;
				auint wp_end = 0U;
				mainui_debug_watchpoint_get(wp_slot, &wp_enable, &wp_region, &wp_flags, &wp_start, &wp_end);
				snprintf(buf, sizeof(buf), "%u: %s %s %0*X..%0*X",
				         (unsigned)(wp_slot + 1U),
				         mui_dbg_watch_region_name(wp_region),
				         ((wp_flags & MAINUI_DBG_WATCH_READ) && (wp_flags & MAINUI_DBG_WATCH_WRITE)) ? "RW" : ((wp_flags & MAINUI_DBG_WATCH_READ) ? "R" : "W"),
				         (int)mui_dbg_watch_digits(wp_region), (unsigned)wp_start,
				         (int)mui_dbg_watch_digits(wp_region), (unsigned)wp_end);
				mu_label(ctx, buf);
				bp_list_idx++;
				bp_list_addr = wp_slot + 1U;
			}
			if (bp_list_idx == 0U){ mu_label(ctx, "No watchpoints set."); }
		}

		if (mu_header(ctx, "SRAM")){
			mu_layout_row(ctx, 3, (int[]){ mui_get_text_width("Base hex", -1) + 8, mui_button_width("000"), mui_button_width("Use SP") }, 24);
			mu_label(ctx, "Base hex");
			mu_textbox(ctx, mui_dbg_sram_base, (int)sizeof(mui_dbg_sram_base));
			if (mu_button(ctx, "Use SP")){
				edit_addr = dbg.sp & 0x0FFFU;
				snprintf(mui_dbg_sram_base, sizeof(mui_dbg_sram_base), "%03X", (unsigned)(edit_addr & ~0x0FU));
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			base = mui_parse_hex_field(mui_dbg_sram_base, 0x0FFFU) & ~0x0FU;
			for (i = 0U; i < 8U; i++){
				auint a = (base + (i * 16U)) & 0x0FFFU;
				snprintf(buf, sizeof(buf),
				         "%03X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned)a,
				         (unsigned)mainui_debug_get_sram_byte(a + 0U),
				         (unsigned)mainui_debug_get_sram_byte(a + 1U),
				         (unsigned)mainui_debug_get_sram_byte(a + 2U),
				         (unsigned)mainui_debug_get_sram_byte(a + 3U),
				         (unsigned)mainui_debug_get_sram_byte(a + 4U),
				         (unsigned)mainui_debug_get_sram_byte(a + 5U),
				         (unsigned)mainui_debug_get_sram_byte(a + 6U),
				         (unsigned)mainui_debug_get_sram_byte(a + 7U),
				         (unsigned)mainui_debug_get_sram_byte(a + 8U),
				         (unsigned)mainui_debug_get_sram_byte(a + 9U),
				         (unsigned)mainui_debug_get_sram_byte(a + 10U),
				         (unsigned)mainui_debug_get_sram_byte(a + 11U),
				         (unsigned)mainui_debug_get_sram_byte(a + 12U),
				         (unsigned)mainui_debug_get_sram_byte(a + 13U),
				         (unsigned)mainui_debug_get_sram_byte(a + 14U),
				         (unsigned)mainui_debug_get_sram_byte(a + 15U));
				mu_label(ctx, buf);
			}
		}

		if (mu_header(ctx, "I/O")){
			mu_layout_row(ctx, 2, (int[]){ mui_get_text_width("Base hex", -1) + 8, -1 }, 24);
			mu_label(ctx, "Base hex");
			mu_textbox(ctx, mui_dbg_io_base, (int)sizeof(mui_dbg_io_base));
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			base = mui_parse_hex_field(mui_dbg_io_base, 0x00FFU) & ~0x0FU;
			for (i = 0U; i < 4U; i++){
				auint a = (base + (i * 16U)) & 0x00FFU;
				snprintf(buf, sizeof(buf),
				         "%02X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned)a,
				         (unsigned)mainui_debug_get_io_byte(a + 0U),
				         (unsigned)mainui_debug_get_io_byte(a + 1U),
				         (unsigned)mainui_debug_get_io_byte(a + 2U),
				         (unsigned)mainui_debug_get_io_byte(a + 3U),
				         (unsigned)mainui_debug_get_io_byte(a + 4U),
				         (unsigned)mainui_debug_get_io_byte(a + 5U),
				         (unsigned)mainui_debug_get_io_byte(a + 6U),
				         (unsigned)mainui_debug_get_io_byte(a + 7U),
				         (unsigned)mainui_debug_get_io_byte(a + 8U),
				         (unsigned)mainui_debug_get_io_byte(a + 9U),
				         (unsigned)mainui_debug_get_io_byte(a + 10U),
				         (unsigned)mainui_debug_get_io_byte(a + 11U),
				         (unsigned)mainui_debug_get_io_byte(a + 12U),
				         (unsigned)mainui_debug_get_io_byte(a + 13U),
				         (unsigned)mainui_debug_get_io_byte(a + 14U),
				         (unsigned)mainui_debug_get_io_byte(a + 15U));
				mu_label(ctx, buf);
			}
		}

		if (mu_header(ctx, "Memory Viewer")){
			int mem_reset_scroll = 0;
			size = mainui_debug_get_mem_region_size(mui_dbg_mem_region);
			digits = mui_dbg_region_digits(mui_dbg_mem_region);
			mu_layout_row(ctx, 5, (int[]){ 64, 72, 72, 64, -1 }, 20);
			if (mu_button(ctx, "SRAM")){ mui_dbg_mem_region = MAINUI_DBG_MEM_SRAM; mem_reset_scroll = 1; }
			if (mu_button(ctx, "SPI RAM")){ mui_dbg_mem_region = MAINUI_DBG_MEM_SPIRAM; mem_reset_scroll = 1; }
			if (mu_button(ctx, "EEPROM")){ mui_dbg_mem_region = MAINUI_DBG_MEM_EEPROM; mem_reset_scroll = 1; }
			if (mu_button(ctx, "Flash")){ mui_dbg_mem_region = MAINUI_DBG_MEM_FLASH; mem_reset_scroll = 1; }
			snprintf(buf, sizeof(buf), "%s size=%u bytes", mui_dbg_region_name(mui_dbg_mem_region), (unsigned)size);
			mu_label(ctx, buf);

			mu_layout_row(ctx, 7, (int[]){ 72, 112, 88, mui_button_width("Up"), mui_button_width("Down"), mui_button_width("PgUp"), mui_button_width("PgDn") }, 24);
			mu_label(ctx, "Base hex");
			if (mu_textbox(ctx, mui_dbg_mem_base, (int)sizeof(mui_dbg_mem_base))){
				mem_reset_scroll = 1;
			}
			if (mu_button(ctx, (mui_dbg_mem_region == MAINUI_DBG_MEM_FLASH) ? "Use PC" : ((mui_dbg_mem_region == MAINUI_DBG_MEM_SRAM) ? "Use SP" : "Use 000"))){
				if (mui_dbg_mem_region == MAINUI_DBG_MEM_FLASH){
					base = (dbg.pc & 0x7FFFU) << 1;
				}else if (mui_dbg_mem_region == MAINUI_DBG_MEM_SRAM){
					base = dbg.sp & 0x0FFFU;
				}else{
					base = 0U;
				}
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, base);
				snprintf(mui_dbg_mem_base, sizeof(mui_dbg_mem_base), "%0*X", (int)digits, (unsigned)base);
				mem_reset_scroll = 1;
			}
			if (mu_button(ctx, "Up")){
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_mem_base, 0xFFFFFFU));
				base = (base >= 16U) ? (base - 16U) : 0U;
				snprintf(mui_dbg_mem_base, sizeof(mui_dbg_mem_base), "%0*X", (int)digits, (unsigned)base);
				mem_reset_scroll = 1;
			}
			if (mu_button(ctx, "Down")){
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_mem_base, 0xFFFFFFU));
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, base + 16U);
				snprintf(mui_dbg_mem_base, sizeof(mui_dbg_mem_base), "%0*X", (int)digits, (unsigned)base);
				mem_reset_scroll = 1;
			}
			if (mu_button(ctx, "PgUp")){
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_mem_base, 0xFFFFFFU));
				base = (base >= 0x100U) ? (base - 0x100U) : 0U;
				snprintf(mui_dbg_mem_base, sizeof(mui_dbg_mem_base), "%0*X", (int)digits, (unsigned)base);
				mem_reset_scroll = 1;
			}
			if (mu_button(ctx, "PgDn")){
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_mem_base, 0xFFFFFFU));
				base = mui_dbg_clamp_addr(mui_dbg_mem_region, base + 0x100U);
				snprintf(mui_dbg_mem_base, sizeof(mui_dbg_mem_base), "%0*X", (int)digits, (unsigned)base);
				mem_reset_scroll = 1;
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "Hex + ASCII from byte offset");

			mu_layout_row(ctx, 1, (int[]){ -1 }, 188);
			mu_begin_panel(ctx, "dbg_mem_panel");
			if (mem_reset_scroll){
				mui_dbg_reset_panel_scroll(ctx, "dbg_mem_panel");
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
			base = mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_mem_base, 0xFFFFFFU));
			base &= ~0x0FU;
			for (i = 0U; i < 64U; i++){
				auint line_addr = base + (i * 16U);
				hexbuf[0] = 0;
				for (j = 0U; j < 16U; j++){
					auint pos = line_addr + j;
					if (pos < size){
						snprintf(hexbuf + strlen(hexbuf), sizeof(hexbuf) - strlen(hexbuf), "%02X ", (unsigned)(mainui_debug_get_mem_region_byte(mui_dbg_mem_region, pos) & 0xFFU));
						asciibuf[j] = mui_ascii_char(mainui_debug_get_mem_region_byte(mui_dbg_mem_region, pos));
					}else{
						snprintf(hexbuf + strlen(hexbuf), sizeof(hexbuf) - strlen(hexbuf), "   ");
						asciibuf[j] = ' ';
					}
				}
				asciibuf[16] = 0;
				snprintf(buf, sizeof(buf), "%0*X: %-48s |%s|", (int)digits, (unsigned)line_addr, hexbuf, asciibuf);
				mu_label(ctx, buf);
			}
			mu_end_panel(ctx);

			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "Graphics Preview");
			mu_layout_row(ctx, 7, (int[]){ mui_get_text_width("Offset", -1) + 8, 112, mui_get_text_width("W", -1) + 8, 64, mui_get_text_width("Z", -1) + 8, 64, mui_button_width("Zero") }, 24);
			mu_label(ctx, "Offset");
			mu_textbox(ctx, mui_dbg_gfx_base, (int)sizeof(mui_dbg_gfx_base));
			mu_label(ctx, "W");
			mu_textbox(ctx, mui_dbg_gfx_width, (int)sizeof(mui_dbg_gfx_width));
			mu_label(ctx, "Z");
			mu_textbox(ctx, mui_dbg_gfx_zoom, (int)sizeof(mui_dbg_gfx_zoom));
			if (mu_button(ctx, (mui_dbg_mem_region == MAINUI_DBG_MEM_FLASH) ? "PC" : ((mui_dbg_mem_region == MAINUI_DBG_MEM_SRAM) ? "SP" : "Zero"))){
				if (mui_dbg_mem_region == MAINUI_DBG_MEM_FLASH){
					gfx_base = (dbg.pc & 0x7FFFU) << 1;
				}else if (mui_dbg_mem_region == MAINUI_DBG_MEM_SRAM){
					gfx_base = dbg.sp & 0x0FFFU;
				}else{
					gfx_base = 0U;
				}
				gfx_base = mui_dbg_clamp_addr(mui_dbg_mem_region, gfx_base);
				snprintf(mui_dbg_gfx_base, sizeof(mui_dbg_gfx_base), "%0*X", (int)digits, (unsigned)gfx_base);
			}
			mu_layout_row(ctx, 5, (int[]){ mui_button_width("8bpp"), mui_button_width("4bpp"), mui_button_width("2bpp"), mui_button_width("1bpp"), -1 }, 24);
			if (mu_button(ctx, "8bpp")){ mui_dbg_gfx_bpp = 0; }
			if (mu_button(ctx, "4bpp")){ mui_dbg_gfx_bpp = 1; }
			if (mu_button(ctx, "2bpp")){ mui_dbg_gfx_bpp = 2; }
			if (mu_button(ctx, "1bpp")){ mui_dbg_gfx_bpp = 3; }
			gfx_bpp = (mui_dbg_gfx_bpp == 0) ? 8U : (mui_dbg_gfx_bpp == 1) ? 4U : (mui_dbg_gfx_bpp == 2) ? 2U : 1U;
			snprintf(buf, sizeof(buf), "%s @ %0*X, grayscale decode", mui_dbg_region_name(mui_dbg_mem_region), (int)digits, (unsigned)mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_gfx_base, 0xFFFFFFU)));
			mu_label(ctx, buf);
			gfx_base = mui_dbg_clamp_addr(mui_dbg_mem_region, mui_parse_hex_field(mui_dbg_gfx_base, 0xFFFFFFU));
			gfx_width = mui_parse_hex_field(mui_dbg_gfx_width, 0x03FFU);
			if (gfx_width == 0U){ gfx_width = 64U; }
			gfx_zoom = mui_parse_hex_field(mui_dbg_gfx_zoom, 0x0FU);
			if (gfx_zoom == 0U){ gfx_zoom = 1U; }
			mu_layout_row(ctx, 1, (int[]){ -1 }, 156);
			preview_rect = mu_layout_next(ctx);
			mui_draw_gfx_preview(ctx, preview_rect, mui_dbg_mem_region, gfx_base, gfx_bpp, gfx_width, gfx_zoom);
		}

		if (mu_header(ctx, "Breakpoints")){
			mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("Word addr", -1) + 8, 112, mui_button_width("Use PC"), mui_button_width("Run To"), mui_button_width("Toggle"), mui_button_width("Clear All") }, 24);
			mu_label(ctx, "Word addr");
			mu_textbox(ctx, mui_dbg_break_addr, (int)sizeof(mui_dbg_break_addr));
			if (mu_button(ctx, "Use PC")){
				snprintf(mui_dbg_break_addr, sizeof(mui_dbg_break_addr), "%04X", (unsigned)(dbg.pc & 0x7FFFU));
			}
			if (mu_button(ctx, "Run To")){
				bp_addr = mui_parse_hex_field(mui_dbg_break_addr, 0x7FFFU);
				mainui_debug_set_paused(TRUE);
				(void)mainui_debug_run_to_word(bp_addr);
			}
			if (mu_button(ctx, "Toggle")){
				bp_addr = mui_parse_hex_field(mui_dbg_break_addr, 0x7FFFU);
				mainui_debug_set_breakpoint(bp_addr, mainui_debug_get_breakpoint(bp_addr) ? FALSE : TRUE);
			}
			if (mu_button(ctx, "Clear All")){
				mainui_debug_clear_breakpoints();
				mainui_debug_clear_last_break_hit();
			}
			bp_addr = mui_parse_hex_field(mui_dbg_break_addr, 0x7FFFU);
			snprintf(buf, sizeof(buf), "Selected %04X is %s", (unsigned)bp_addr, mainui_debug_get_breakpoint(bp_addr) ? "set" : "clear");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, buf);
			bp_list_idx = 0U;
			bp_list_addr = 0U;
			while ((bp_list_idx < 8U) && mainui_debug_next_breakpoint(bp_list_addr, &bp_addr)){
				snprintf(buf, sizeof(buf), "%u: %04X%s", (unsigned)(bp_list_idx + 1U), (unsigned)(bp_addr & 0x7FFFU), ((dbg.pc & 0x7FFFU) == (bp_addr & 0x7FFFU)) ? "  <PC>" : "");
				mu_label(ctx, buf);
				bp_list_idx++;
				bp_list_addr = (bp_addr + 1U) & 0x7FFFU;
				if (bp_list_addr == 0U){ break; }
			}
			if (bp_list_idx == 0U){
				mu_label(ctx, "No breakpoints set.");
			}
		}

		if (mu_header(ctx, "Edit / Poke")){
			if (!mainui_debug_is_paused()){
				mu_label(ctx, "Pause execution to edit registers or memory.");
			}else{
				mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("Reg", -1) + 8, 80, mui_get_text_width("Val", -1) + 8, 80, mui_button_width("Write"), -1 }, 24);
				mu_label(ctx, "Reg");
				mu_textbox(ctx, mui_dbg_reg_index, (int)sizeof(mui_dbg_reg_index));
				mu_label(ctx, "Val");
				mu_textbox(ctx, mui_dbg_reg_value, (int)sizeof(mui_dbg_reg_value));
				if (mu_button(ctx, "Write")){
					mainui_debug_set_reg_byte(mui_parse_hex_field(mui_dbg_reg_index, 0x1FU), mui_parse_hex_field(mui_dbg_reg_value, 0xFFU));
				}
				mu_label(ctx, "General regs only (00-1F)");

				mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("SRAM", -1) + 8, 80, mui_get_text_width("Val", -1) + 8, 80, mui_button_width("Write"), -1 }, 24);
				mu_label(ctx, "SRAM");
				mu_textbox(ctx, mui_dbg_sram_edit_addr, (int)sizeof(mui_dbg_sram_edit_addr));
				mu_label(ctx, "Val");
				mu_textbox(ctx, mui_dbg_sram_edit_value, (int)sizeof(mui_dbg_sram_edit_value));
				if (mu_button(ctx, "Write")){
					mainui_debug_set_sram_byte(mui_parse_hex_field(mui_dbg_sram_edit_addr, 0x0FFFU), mui_parse_hex_field(mui_dbg_sram_edit_value, 0xFFU));
				}
				mu_label(ctx, "Raw SRAM byte");

				mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("I/O", -1) + 8, 80, mui_get_text_width("Val", -1) + 8, 80, mui_button_width("Write"), -1 }, 24);
				mu_label(ctx, "I/O");
				mu_textbox(ctx, mui_dbg_io_edit_addr, (int)sizeof(mui_dbg_io_edit_addr));
				mu_label(ctx, "Val");
				mu_textbox(ctx, mui_dbg_io_edit_value, (int)sizeof(mui_dbg_io_edit_value));
				if (mu_button(ctx, "Write")){
					mainui_debug_set_io_byte(mui_parse_hex_field(mui_dbg_io_edit_addr, 0x00FFU), mui_parse_hex_field(mui_dbg_io_edit_value, 0xFFU));
				}
				mu_label(ctx, "Calls cu_avr_io_update()");
			}
		}
		mui_window_end(ctx, MUI_WIN_DEBUGGER);
	}
}

#endif

static void mui_build_netplay_start_window(mu_Context* ctx)
{
#ifdef ENABLE_NETPLAY
	if (!mui_window_begin(ctx, MUI_WIN_NETPLAY_START)){ return; }
	{
		netplay_status_t st;
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "DIRECT UDP");
		mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("HOST", -1) + 8, -1, mui_get_text_width("PORT", -1) + 8, 76, mui_get_text_width("LOCAL", -1) + 8, 76 }, 24);
		mu_label(ctx, "HOST");
		mu_textbox(ctx, mui_np_host, (int)sizeof(mui_np_host));
		mu_label(ctx, "PORT");
		mu_textbox(ctx, mui_np_port, (int)sizeof(mui_np_port));
		mu_label(ctx, "LOCAL");
		mu_textbox(ctx, mui_np_local_port, (int)sizeof(mui_np_local_port));
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "LEAVE LOCAL BLANK FOR AUTO CLIENT PORT.");
		mu_layout_row(ctx, 4, (int[]){ mui_button_width("HOST GAME"), mui_button_width("JOIN GAME"), mui_button_width("LOBBY..."), -1 }, 24);
		if (mu_button(ctx, "HOST GAME")){
			mui_apply_netplay_config();
			if (netplay_open_server(mui_parse_u32_field(mui_np_local_port, 43800U, 65535U))){
				snprintf(mui_np_status, sizeof(mui_np_status), "HOSTING ON UDP PORT %u", (unsigned)mui_parse_u32_field(mui_np_local_port, 43800U, 65535U));
			}else{
				snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO HOST ON UDP PORT %u", (unsigned)mui_parse_u32_field(mui_np_local_port, 43800U, 65535U));
			}
		}
		if (mu_button(ctx, "JOIN GAME")){
			auint remote_port = mui_parse_u32_field(mui_np_port, 43800U, 65535U);
			auint local_port = mui_parse_u32_field(mui_np_local_port, 0U, 65535U);
			mui_apply_netplay_config();
			if (netplay_open_client(mui_np_host, remote_port, local_port)){
				netplay_get_status(&st);
				if (local_port == 0U){
					snprintf(mui_np_status, sizeof(mui_np_status), "JOINING %s:%u FROM AUTO UDP PORT %u (RETRYING)", mui_np_host, (unsigned)remote_port, (unsigned)st.local_port);
				}else{
					snprintf(mui_np_status, sizeof(mui_np_status), "JOINING %s:%u FROM UDP PORT %u (RETRYING)", mui_np_host, (unsigned)remote_port, (unsigned)st.local_port);
				}
			}else{
				if (local_port == 0U){
					snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO JOIN %s:%u WITH AUTO LOCAL UDP PORT", mui_np_host, (unsigned)remote_port);
				}else{
					snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO JOIN %s:%u FROM UDP PORT %u", mui_np_host, (unsigned)remote_port, (unsigned)local_port);
				}
			}
		}
		if (mu_button(ctx, "LOBBY...")){
			mui_window_open(ctx, MUI_WIN_LOBBY);
		}

		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "UZENET RELAY");
		mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("SERVER", -1) + 8, -1, mui_get_text_width("PORT", -1) + 8, 76, mui_get_text_width("ROOM", -1) + 8, 96 }, 24);
		mu_label(ctx, "SERVER");
		mu_textbox(ctx, mui_np_relay_host, (int)sizeof(mui_np_relay_host));
		mu_label(ctx, "PORT");
		mu_textbox(ctx, mui_np_relay_port, (int)sizeof(mui_np_relay_port));
		mu_label(ctx, "ROOM");
		mu_textbox(ctx, mui_np_room_code, (int)sizeof(mui_np_room_code));
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "HOST RELAY CREATES A ROOM.");
		mu_label(ctx, "JOIN RELAY USES THE ROOM CODE.");
		mu_layout_row(ctx, 4, (int[]){ mui_button_width("HOST RELAY"), mui_button_width("JOIN RELAY"), mui_button_width("LOBBY..."), -1 }, 24);
		if (mu_button(ctx, "HOST RELAY")){
			auint relay_port = mui_parse_u32_field(mui_np_relay_port, 43810U, 65535U);
			auint local_port = mui_parse_u32_field(mui_np_local_port, 0U, 65535U);
			mui_apply_netplay_config();
			if (netplay_open_relay_host(mui_np_relay_host, relay_port, local_port, mui_np_room_code[0] ? mui_np_room_code : NULL, TRUE, mainui_get_current_rom_name())){
				snprintf(mui_np_status, sizeof(mui_np_status), "CREATING RELAY ROOM VIA %s:%u", mui_np_relay_host, (unsigned)relay_port);
			}else{
				snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO OPEN RELAY HOST VIA %s:%u", mui_np_relay_host, (unsigned)relay_port);
			}
		}
		if (mu_button(ctx, "JOIN RELAY")){
			auint relay_port = mui_parse_u32_field(mui_np_relay_port, 43810U, 65535U);
			auint local_port = mui_parse_u32_field(mui_np_local_port, 0U, 65535U);
			mui_apply_netplay_config();
			if (mui_np_room_code[0] == 0){
				snprintf(mui_np_status, sizeof(mui_np_status), "ENTER A RELAY ROOM CODE FIRST");
			}else if (netplay_open_relay_join(mui_np_relay_host, relay_port, mui_np_room_code, local_port)){
				snprintf(mui_np_status, sizeof(mui_np_status), "JOINING RELAY ROOM %s VIA %s:%u", mui_np_room_code, mui_np_relay_host, (unsigned)relay_port);
			}else{
				snprintf(mui_np_status, sizeof(mui_np_status), "FAILED TO JOIN RELAY ROOM %s VIA %s:%u", mui_np_room_code, mui_np_relay_host, (unsigned)relay_port);
			}
		}
		if (mu_button(ctx, "LOBBY...")){
			mui_window_open(ctx, MUI_WIN_LOBBY);
		}

		netplay_get_status(&st);
		if (st.enabled){
			char transport[96];
			mui_np_format_transport(transport, sizeof(transport), &st);
			if (st.relay_mode && st.relay_room_code[0] != 0){
				snprintf(mui_np_status, sizeof(mui_np_status), "%s  ROOM %s", transport, st.relay_room_code);
			}else{
				snprintf(mui_np_status, sizeof(mui_np_status), "%s", transport);
			}
		}
		if (st.notice[0] != 0){
			snprintf(mui_np_status, sizeof(mui_np_status), "%s", st.notice);
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, mui_np_status);
		mui_window_end(ctx, MUI_WIN_NETPLAY_START);
	}
#else
	(void)ctx;
#endif
}

static void mui_build_netplay_settings_window(mu_Context* ctx)
{
#ifdef ENABLE_NETPLAY
	int state;
	if (!mui_window_begin(ctx, MUI_WIN_NETPLAY_SETTINGS)){ return; }
	{
		if (!mui_np_synced){
			mui_sync_netplay_buffers();
		}
		mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("NAME", -1) + 8, 120, mui_button_width("APPLY"), mui_button_width("PULL LIVE") }, 24);
		mu_label(ctx, "NAME");
		mu_textbox(ctx, mui_np_name, (int)sizeof(mui_np_name));
		if (mu_button(ctx, "APPLY")){
			mui_apply_netplay_config();
			snprintf(mui_np_status, sizeof(mui_np_status), "Applied netplay settings");
		}
		if (mu_button(ctx, "PULL LIVE")){
			mui_sync_netplay_buffers();
			snprintf(mui_np_status, sizeof(mui_np_status), "Pulled config from live netplay state");
		}
		{
			auint p;
			for (p = 0U; p < ROLLBACK_MAX_PLAYERS; p += 4U){
				mu_layout_row(ctx, 8, (int[]){ mui_get_text_width("PAD1", -1) + 8, 82, mui_get_text_width("PAD2", -1) + 8, 82, mui_get_text_width("PAD3", -1) + 8, 82, mui_get_text_width("PAD4", -1) + 8, 82 }, 24);
				mu_label(ctx, (p + 0U) < ROLLBACK_MAX_PLAYERS ? ((p + 0U) == 0U ? "PAD1" : (p + 0U) == 1U ? "PAD2" : (p + 0U) == 2U ? "PAD3" : (p + 0U) == 3U ? "PAD4" : (p + 0U) == 4U ? "PAD5" : (p + 0U) == 5U ? "PAD6" : (p + 0U) == 6U ? "PAD7" : "PAD8") : "");
				if ((p + 0U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 0U], (int)sizeof(mui_np_pad_labels[p + 0U])); else mu_label(ctx, "");
				mu_label(ctx, (p + 1U) < ROLLBACK_MAX_PLAYERS ? ((p + 1U) == 0U ? "PAD1" : (p + 1U) == 1U ? "PAD2" : (p + 1U) == 2U ? "PAD3" : (p + 1U) == 3U ? "PAD4" : (p + 1U) == 4U ? "PAD5" : (p + 1U) == 5U ? "PAD6" : (p + 1U) == 6U ? "PAD7" : "PAD8") : "");
				if ((p + 1U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 1U], (int)sizeof(mui_np_pad_labels[p + 1U])); else mu_label(ctx, "");
				mu_label(ctx, (p + 2U) < ROLLBACK_MAX_PLAYERS ? ((p + 2U) == 0U ? "PAD1" : (p + 2U) == 1U ? "PAD2" : (p + 2U) == 2U ? "PAD3" : (p + 2U) == 3U ? "PAD4" : (p + 2U) == 4U ? "PAD5" : (p + 2U) == 5U ? "PAD6" : (p + 2U) == 6U ? "PAD7" : "PAD8") : "");
				if ((p + 2U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 2U], (int)sizeof(mui_np_pad_labels[p + 2U])); else mu_label(ctx, "");
				mu_label(ctx, (p + 3U) < ROLLBACK_MAX_PLAYERS ? ((p + 3U) == 0U ? "PAD1" : (p + 3U) == 1U ? "PAD2" : (p + 3U) == 2U ? "PAD3" : (p + 3U) == 3U ? "PAD4" : (p + 3U) == 4U ? "PAD5" : (p + 3U) == 5U ? "PAD6" : (p + 3U) == 6U ? "PAD7" : "PAD8") : "");
				if ((p + 3U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 3U], (int)sizeof(mui_np_pad_labels[p + 3U])); else mu_label(ctx, "");
			}
			for (p = 0U; p < ROLLBACK_MAX_PLAYERS; p += 4U){
				mu_layout_row(ctx, 5, (int[]){ 96, mui_checkbox_width("P1"), mui_checkbox_width("P2"), mui_checkbox_width("P3"), mui_checkbox_width("P4") }, 24);
				mu_label(ctx, (p == 0U) ? "Owned" : "");
				if ((p + 0U) < ROLLBACK_MAX_PLAYERS) mu_checkbox(ctx, (p + 0U) == 0U ? "P1" : (p + 0U) == 1U ? "P2" : (p + 0U) == 2U ? "P3" : (p + 0U) == 3U ? "P4" : (p + 0U) == 4U ? "P5" : (p + 0U) == 5U ? "P6" : (p + 0U) == 6U ? "P7" : "P8", &mui_np_local_mask_bits[p + 0U]); else mu_label(ctx, "");
				if ((p + 1U) < ROLLBACK_MAX_PLAYERS) mu_checkbox(ctx, (p + 1U) == 0U ? "P1" : (p + 1U) == 1U ? "P2" : (p + 1U) == 2U ? "P3" : (p + 1U) == 3U ? "P4" : (p + 1U) == 4U ? "P5" : (p + 1U) == 5U ? "P6" : (p + 1U) == 6U ? "P7" : "P8", &mui_np_local_mask_bits[p + 1U]); else mu_label(ctx, "");
				if ((p + 2U) < ROLLBACK_MAX_PLAYERS) mu_checkbox(ctx, (p + 2U) == 0U ? "P1" : (p + 2U) == 1U ? "P2" : (p + 2U) == 2U ? "P3" : (p + 2U) == 3U ? "P4" : (p + 2U) == 4U ? "P5" : (p + 2U) == 5U ? "P6" : (p + 2U) == 6U ? "P7" : "P8", &mui_np_local_mask_bits[p + 2U]); else mu_label(ctx, "");
				if ((p + 3U) < ROLLBACK_MAX_PLAYERS) mu_checkbox(ctx, (p + 3U) == 0U ? "P1" : (p + 3U) == 1U ? "P2" : (p + 3U) == 2U ? "P3" : (p + 3U) == 3U ? "P4" : (p + 3U) == 4U ? "P5" : (p + 3U) == 5U ? "P6" : (p + 3U) == 6U ? "P7" : "P8", &mui_np_local_mask_bits[p + 3U]); else mu_label(ctx, "");
			}
		}

		mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("Max", -1) + 8, 64, mui_get_text_width("TCP", -1) + 8, 72, mui_get_text_width("ROM MB", -1) + 8, 72 }, 24);
		mu_label(ctx, "Max");
		mu_textbox(ctx, mui_np_max_players, (int)sizeof(mui_np_max_players));
		mu_label(ctx, "TCP");
		mu_textbox(ctx, mui_np_tcp_port, (int)sizeof(mui_np_tcp_port));
		mu_label(ctx, "ROM MB");
		mu_textbox(ctx, mui_np_max_rom_mb, (int)sizeof(mui_np_max_rom_mb));

		mu_layout_row(ctx, 3, (int[]){ 116, 244, mui_button_width("REFRESH IFACES") }, 24);
		mu_label(ctx, "NETWORK IFACE");
		if (mui_choice_popup_inline(ctx, "np_iface_popup", (mui_np_iface_choice < mui_np_iface_count) ? mui_np_iface_name_ptrs[mui_np_iface_choice] : "AUTO", mui_np_iface_name_ptrs, mui_np_iface_count, &mui_np_iface_choice, 244)){
			mui_apply_netplay_config();
			snprintf(mui_np_status, sizeof(mui_np_status), "Applied network interface %s", (mui_np_iface_choice < mui_np_iface_count) ? mui_np_iface_name_ptrs[mui_np_iface_choice] : "AUTO");
		}
		if (mu_button(ctx, "REFRESH IFACES")){
			mui_np_refresh_interfaces();
			snprintf(mui_np_status, sizeof(mui_np_status), "Refreshed network interface list");
		}

		mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("Rollback", -1) + 8, 64, mui_button_width("-1"), mui_button_width("+1"), mui_button_width("+4"), -1 }, 24);
		mu_label(ctx, "Rollback");
		mu_textbox(ctx, mui_np_rb_window, (int)sizeof(mui_np_rb_window));
		if (mu_button(ctx, "-1")){
			auint v = mui_parse_u32_field(mui_np_rb_window, mainui_get_netplay_rollback_window(), 255U);
			if (v > 1U){ v--; }
			snprintf(mui_np_rb_window, sizeof(mui_np_rb_window), "%u", (unsigned)v);
			mainui_set_netplay_rollback_window(v);
		}
		if (mu_button(ctx, "+1")){
			auint v = mui_parse_u32_field(mui_np_rb_window, mainui_get_netplay_rollback_window(), 255U);
			v++;
			snprintf(mui_np_rb_window, sizeof(mui_np_rb_window), "%u", (unsigned)v);
			mainui_set_netplay_rollback_window(v);
		}
		if (mu_button(ctx, "+4")){
			auint v = mui_parse_u32_field(mui_np_rb_window, mainui_get_netplay_rollback_window(), 255U);
			v += 4U;
			snprintf(mui_np_rb_window, sizeof(mui_np_rb_window), "%u", (unsigned)v);
			mainui_set_netplay_rollback_window(v);
		}
		mu_label(ctx, "Frames predicted ahead before stall");

		mu_layout_row(ctx, 6, (int[]){ mui_get_text_width("Delay", -1) + 8, 64, mui_button_width("-1"), mui_button_width("+1"), mui_button_width("+2"), -1 }, 24);
		mu_label(ctx, "Delay");
		mu_textbox(ctx, mui_np_input_delay, (int)sizeof(mui_np_input_delay));
		if (mu_button(ctx, "-1")){
			auint v = mui_parse_u32_field(mui_np_input_delay, mainui_get_netplay_input_delay(), 8U);
			if (v > 0U){ v--; }
			snprintf(mui_np_input_delay, sizeof(mui_np_input_delay), "%u", (unsigned)v);
			mainui_set_netplay_input_delay(v);
		}
		if (mu_button(ctx, "+1")){
			auint v = mui_parse_u32_field(mui_np_input_delay, mainui_get_netplay_input_delay(), 8U);
			v++;
			snprintf(mui_np_input_delay, sizeof(mui_np_input_delay), "%u", (unsigned)v);
			mainui_set_netplay_input_delay(v);
		}
		if (mu_button(ctx, "+2")){
			auint v = mui_parse_u32_field(mui_np_input_delay, mainui_get_netplay_input_delay(), 8U);
			v += 2U;
			snprintf(mui_np_input_delay, sizeof(mui_np_input_delay), "%u", (unsigned)v);
			mainui_set_netplay_input_delay(v);
		}
		mu_label(ctx, "Fixed local input delay in frames");

		mu_layout_row(ctx, 2, (int[]){ mui_checkbox_width("SEND ROMS"), mui_checkbox_width("RECEIVE ROMS") }, 22);
		state = mui_np_send_rom;
		if (mu_checkbox(ctx, "SEND ROMS", &state)){
			mui_np_send_rom = state;
		}
		state = mui_np_recv_rom;
		if (mu_checkbox(ctx, "RECEIVE ROMS", &state)){
			mui_np_recv_rom = state;
		}

		{
			auint sync_choice = (mui_np_sync_mode >= NETPLAY_ROM_SYNC_OFF && mui_np_sync_mode <= NETPLAY_ROM_SYNC_ALWAYS) ? (auint)mui_np_sync_mode : (auint)NETPLAY_ROM_SYNC_MISMATCH;
			if (mui_labeled_choice_popup(ctx, "SYNC", "np_sync_mode_popup", mui_np_sync_mode_names[sync_choice], mui_np_sync_mode_names, 4U, &sync_choice, 64, 148)){
				mui_np_sync_mode = (int)sync_choice;
			}
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, mui_np_status);
		mui_window_end(ctx, MUI_WIN_NETPLAY_SETTINGS);
	}
#else
	(void)ctx;
#endif
}

#ifdef ENABLE_NETPLAY
static void mui_np_ping_track(uint32 ping_ms)
{
	if ((ping_ms == 0U) || (ping_ms > 9999U)){
		return;
	}
	if ((mui_np_ping_history_count != 0U) && (mui_np_ping_history_last == ping_ms)){
		return;
	}
	mui_np_ping_history[mui_np_ping_history_head] = (uint16)ping_ms;
	mui_np_ping_history_head = (mui_np_ping_history_head + 1U) % MUI_NP_PING_HISTORY;
	if (mui_np_ping_history_count < MUI_NP_PING_HISTORY){
		mui_np_ping_history_count++;
	}
	mui_np_ping_history_last = ping_ms;
}

static void mui_np_ping_labels(mu_Context* ctx)
{
	char  line[256];
	auint i;
	auint cols = 6U;
	auint idx;
	auint start;
	mu_label(ctx, "PING TRACKER (OLD->NEW)");
	if (mui_np_ping_history_count == 0U){
		mu_label(ctx, "NO SAMPLES YET");
		return;
	}
	start = (mui_np_ping_history_head + MUI_NP_PING_HISTORY - mui_np_ping_history_count) % MUI_NP_PING_HISTORY;
	for (i = 0U; i < mui_np_ping_history_count; i += cols){
		auint j;
		int pos = 0;
		line[0] = 0;
		for (j = 0U; (j < cols) && ((i + j) < mui_np_ping_history_count); j++){
			idx = (start + i + j) % MUI_NP_PING_HISTORY;
			pos += snprintf(line + pos, sizeof(line) - (size_t)pos, j == 0U ? "%4u" : "  %4u", (unsigned)mui_np_ping_history[idx]);
			if (pos >= (int)sizeof(line)){
				break;
			}
		}
		line[sizeof(line) - 1U] = 0;
		mu_label(ctx, line);
	}
}
#endif

static void mui_build_netplay_status_window(mu_Context* ctx)
{
#ifdef ENABLE_NETPLAY
	netplay_status_t         st;
	mainui_netplay_runtime_t rt;
	char                     buf[320];
	if (!mui_window_begin(ctx, MUI_WIN_NETPLAY_STATUS)){ return; }
	{
		netplay_get_status(&st);
		mainui_get_netplay_runtime(&rt);
		mui_np_ping_track(st.ping_smoothed_ms != 0U ? st.ping_smoothed_ms : st.ping_ms);
		mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
		mu_begin_panel(ctx, "netplay_status");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		snprintf(buf, sizeof(buf), "MODE %u   ENABLED %u",
				(unsigned)st.mode, (unsigned)st.enabled);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "CONNECTED %u   SESSION %u",
				(unsigned)st.connected, (unsigned)st.session_started);
		mu_label(ctx, buf);
		mui_np_format_transport(buf, sizeof(buf), &st);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "LOCAL UDP %u   SOCKET %s",
				(unsigned)st.local_port, mui_np_af_name(st.socket_ipv6));
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "IFACE %s", st.network_interface_label[0] ? st.network_interface_label : "AUTO");
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "IPv4 %s%s%s", st.local_ipv4[0] ? st.local_ipv4 : "(NONE)", st.local_ipv4_scope[0] ? " (" : "", st.local_ipv4_scope[0] ? st.local_ipv4_scope : "");
		if (st.local_ipv4_scope[0] != 0){ strncat(buf, ")", sizeof(buf) - strlen(buf) - 1U); }
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "IPv6 %s%s%s", st.local_ipv6[0] ? st.local_ipv6 : "(NONE)", st.local_ipv6_scope[0] ? " (" : "", st.local_ipv6_scope[0] ? st.local_ipv6_scope : "");
		if (st.local_ipv6_scope[0] != 0){ strncat(buf, ")", sizeof(buf) - strlen(buf) - 1U); }
		mu_label(ctx, buf);
		if (st.active_local_addr[0] != 0){
			snprintf(buf, sizeof(buf), "ACTIVE LOCAL %s%s%s", st.active_local_addr, st.active_local_scope[0] ? " (" : "", st.active_local_scope[0] ? st.active_local_scope : "");
			if (st.active_local_scope[0] != 0){ strncat(buf, ")", sizeof(buf) - strlen(buf) - 1U); }
			mu_label(ctx, buf);
		}
		if (st.relay_mode){
			snprintf(buf, sizeof(buf), "RELAY ROOM %s", st.relay_room_code[0] ? st.relay_room_code : "(PENDING)");
			mu_label(ctx, buf);
			snprintf(buf, sizeof(buf), "RELAY SERVER %s:%u   %s", st.relay_server_host[0] ? st.relay_server_host : "(NONE)", (unsigned)st.relay_server_port, mui_np_af_name(st.relay_addr_ipv6));
			mu_label(ctx, buf);
			if (st.relay_direct_established){
				snprintf(buf, sizeof(buf), "DIRECT PATH %s ACTIVE", mui_np_af_name(st.peer_addr_ipv6));
			}else if (st.relay_peer_present){
				snprintf(buf, sizeof(buf), "DIRECT PUNCH %s PENDING", mui_np_af_name(st.peer_addr_ipv6 ? TRUE : st.socket_ipv6));
			}else{
				snprintf(buf, sizeof(buf), "WAITING FOR RELAY PEER");
			}
			mu_label(ctx, buf);
		}
		snprintf(buf, sizeof(buf), "HELLO %u/%u   CAPS %u",
				(unsigned)st.hello_sent, (unsigned)st.hello_acked, (unsigned)st.caps_sent);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "COMPAT %u   SYNC %s",
				(unsigned)st.compat_ok, mui_np_sync_name((int)st.rom_sync_mode));
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "LOCAL %s",
				st.local_name[0] ? st.local_name : "Player");
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "PRIMARY %u/%u   MASK %08X",
				(unsigned)st.local_player, (unsigned)st.max_players, (unsigned)st.local_player_mask);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "PEER %s   %s:%u   %s",
				st.peer_name[0] ? st.peer_name : "(PEER)", st.peer_host[0] ? st.peer_host : "(NONE)", (unsigned)st.peer_port, mui_np_af_name(st.peer_addr_ipv6));
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "PEER PRIMARY %u   PEER MASK %08X",
				(unsigned)st.peer_player, (unsigned)st.peer_player_mask);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "ROM %s", st.rom_name[0] ? st.rom_name : "(NONE)");
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "PEER ROM %s", st.peer_rom_name[0] ? st.peer_rom_name : "(NONE)");
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "ROM CRC %08X   PEER CRC %08X",
				(unsigned)st.rom_crc, (unsigned)st.peer_rom_crc);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "ROM SIZE %u   PEER SIZE %u",
				(unsigned)st.rom_size, (unsigned)st.peer_rom_size);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "TRANSFER %u   READY %u/%u",
				(unsigned)st.rom_transfer_needed, (unsigned)st.local_ready, (unsigned)st.peer_ready);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "SEQ TX %u   RX %u",
				(unsigned)st.tx_seq, (unsigned)st.rx_last_seq);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "DUP %u   OOO %u",
				(unsigned)st.rx_dup_count, (unsigned)st.rx_ooo_count);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "GAP %u   ACTIVE %u",
				(unsigned)st.rx_gap_count, (unsigned)rt.active);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "FRAME %u   STALLED %u",
				(unsigned)rt.frame, (unsigned)rt.stalled);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "PREDICTED %u   RESIM %u",
				(unsigned)rt.predicted_any, (unsigned)rt.resim_applied);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "LOCAL P %u   REMOTE P %u",
				(unsigned)rt.local_player, (unsigned)rt.remote_player);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "MASKS %08X / %08X",
				(unsigned)rt.local_player_mask, (unsigned)rt.remote_player_mask);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "ROLLBACK %u   DELAY %u",
				(unsigned)rt.rollback_window, (unsigned)rt.input_delay);
		mu_label(ctx, buf);
		if (rt.resim_applied){
			snprintf(buf, sizeof(buf), "RESIM WINDOW %u..%u", (unsigned)rt.resim_from_frame, (unsigned)rt.resim_to_frame);
			mu_label(ctx, buf);
		}
		snprintf(buf, sizeof(buf), "LOBBY READY %u/%u   SEAT MAP %u",
				(unsigned)st.lobby_local_ready, (unsigned)st.lobby_peer_ready, (unsigned)st.lobby_seat_map_valid);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "PING %u MS   AVG %u MS",
				(unsigned)st.ping_ms, (unsigned)st.ping_smoothed_ms);
		mu_label(ctx, buf);
		snprintf(buf, sizeof(buf), "JITTER %u MS", (unsigned)st.ping_jitter_ms);
		mu_label(ctx, buf);
		mui_np_ping_labels(ctx);
		if (st.compat_reason[0] != 0){
			snprintf(buf, sizeof(buf), "REASON %s", st.compat_reason);
			mu_label(ctx, buf);
		}
		mu_end_panel(ctx);
		mu_layout_row(ctx, 2, (int[]){ mui_button_width("PULL LIVE"), -1 }, 24);
		if (mu_button(ctx, "PULL LIVE")){
			mui_sync_netplay_buffers();
			snprintf(mui_np_status, sizeof(mui_np_status), "PULLED CONFIG FROM LIVE NETPLAY STATE");
		}
		mu_label(ctx, mui_np_status);
		mui_window_end(ctx, MUI_WIN_NETPLAY_STATUS);
	}
#else
	(void)ctx;
#endif
}

static void mui_build_netplay_disconnect_window(mu_Context* ctx)
{
#ifdef ENABLE_NETPLAY
	if (!mui_window_begin(ctx, MUI_WIN_NETPLAY_DISCONNECT)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "QUIT NET GAME?");
		mu_layout_row(ctx, 2, (int[]){ mui_button_width("DISCONNECT"), mui_button_width("CANCEL") }, 24);
		if (mu_button(ctx, "DISCONNECT")){
			netplay_disconnect();
			snprintf(mui_np_status, sizeof(mui_np_status), "DISCONNECTED");
			textgui_log_add("NETPLAY DISCONNECTED");
			mui_window_close(ctx, MUI_WIN_NETPLAY_DISCONNECT);
		}
		if (mu_button(ctx, "CANCEL")){
			mui_window_close(ctx, MUI_WIN_NETPLAY_DISCONNECT);
		}
		mui_window_end(ctx, MUI_WIN_NETPLAY_DISCONNECT);
	}
#else
	(void)ctx;
#endif
}

static void mui_build_netplay_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_NETPLAY)){ return; }
	{
#ifdef ENABLE_NETPLAY
		netplay_status_t st;
		netplay_get_status(&st);
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_button_ex(ctx, "1. SIMPLE NETPLAY", 0, 0)){
			mui_window_open(ctx, MUI_WIN_PLAY_ONLINE);
		}
		if (mu_button_ex(ctx, "2. MANUAL ONLINE", 0, 0)){
			mui_window_open(ctx, MUI_WIN_NETPLAY_START);
		}
		if (mu_button_ex(ctx, "3. LOBBY", 0, 0)){
			mui_window_open(ctx, MUI_WIN_LOBBY);
		}
		if (mu_button_ex(ctx, "4. SETTINGS", 0, 0)){
			mui_window_open(ctx, MUI_WIN_NETPLAY_SETTINGS);
		}
		if (mu_button_ex(ctx, "5. STATUS", 0, 0)){
			mui_window_open(ctx, MUI_WIN_NETPLAY_STATUS);
		}
		if (st.connected){
			if (mu_button_ex(ctx, "6. DISCONNECT", 0, 0)){
				mui_window_open(ctx, MUI_WIN_NETPLAY_DISCONNECT);
			}
		}else{
			(void)mui_disabled_button(ctx, "6. DISCONNECT");
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, mui_np_status);
#else
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "Netplay is disabled in this build.");
		mu_label(ctx, "Enable FLAG_NETPLAY and FLAG_CHECKPOINTS to use it.");
#endif
		mui_window_end(ctx, MUI_WIN_NETPLAY);
	}
}

static void mui_build_lobby_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_LOBBY)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
#ifdef ENABLE_NETPLAY
		{
			netplay_lobby_status_t lb;
			netplay_status_t st;
			char buf[256];
			char line[NETPLAY_LOBBY_CHAT_LINE_CHARS + 8];
			auint i;
			netplay_lobby_get_status(&lb);
			netplay_get_status(&st);
			if ((lb.local_name[0] != 0) && (strncmp(mui_np_name, lb.local_name, sizeof(mui_np_name)) != 0)){
				strncpy(mui_np_name, lb.local_name, sizeof(mui_np_name) - 1U);
				mui_np_name[sizeof(mui_np_name) - 1U] = 0;
			}
			mu_label(ctx, "PRE-MATCH LOBBY. HOST ASSIGNS SEATS. BOTH SIDES READY. HOST STARTS MATCH. SEAT CHANGES CLEAR READY.");
			if (!st.connected){
				mu_label(ctx, "CONNECT IN NETPLAY FIRST.");
			}else{
				snprintf(buf, sizeof(buf), "LOCAL %s   PEER %s",
					lb.local_name[0] ? lb.local_name : "Player", lb.peer_name[0] ? lb.peer_name : "(PEER)");
				mu_label(ctx, buf);
				mui_np_format_transport(buf, sizeof(buf), &st);
				mu_label(ctx, buf);
				snprintf(buf, sizeof(buf), "PEER HOST %s:%u   %s", st.peer_host[0] ? st.peer_host : "(PEER PENDING)", (unsigned)st.peer_port, mui_np_af_name(st.peer_addr_ipv6));
				mu_label(ctx, buf);
				if (st.relay_mode){
					snprintf(buf, sizeof(buf), "RELAY ROOM %s   SERVER %s:%u", st.relay_room_code[0] ? st.relay_room_code : "(PENDING)", st.relay_server_host[0] ? st.relay_server_host : "(NONE)", (unsigned)st.relay_server_port);
					mu_label(ctx, buf);
					if (st.relay_direct_established){
						snprintf(buf, sizeof(buf), "DIRECT PATH %s ACTIVE", mui_np_af_name(st.peer_addr_ipv6));
					}else if (st.relay_peer_present){
						snprintf(buf, sizeof(buf), "DIRECT PUNCH %s PENDING", mui_np_af_name(st.peer_addr_ipv6 ? TRUE : st.socket_ipv6));
					}else{
						snprintf(buf, sizeof(buf), "WAITING FOR RELAY PEER");
					}
					mu_label(ctx, buf);
				}
				snprintf(buf, sizeof(buf), "ASSETS READY %u/%u   READY %u/%u",
					(unsigned)lb.asset_local_ready, (unsigned)lb.asset_peer_ready,
					(unsigned)lb.local_ready, (unsigned)lb.peer_ready);
				mu_label(ctx, buf);
				snprintf(buf, sizeof(buf), "PING %u MS   AVG %u MS   JIT %u MS",
					(unsigned)lb.ping_ms, (unsigned)lb.ping_smoothed_ms, (unsigned)lb.ping_jitter_ms);
				mu_label(ctx, buf);
				snprintf(buf, sizeof(buf), "ROLE %s   SEAT MAP %u   MAX PLAYERS %u   SESSION %u",
					lb.is_host ? "HOST" : "GUEST", (unsigned)lb.seat_map_valid, (unsigned)lb.max_players, (unsigned)st.session_started);
				mu_label(ctx, buf);

				mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("NAME", -1) + 8, 120, mui_button_width("APPLY LABELS"), -1 }, 24);
				mu_label(ctx, "NAME");
				mu_textbox(ctx, mui_np_name, (int)sizeof(mui_np_name));
				if (mu_button(ctx, "APPLY LABELS")){
					mui_apply_netplay_config();
					snprintf(mui_lobby_status, sizeof(mui_lobby_status), "UPDATED NAME AND PAD LABELS");
				}
				mu_label(ctx, "");
				{
					auint p;
					for (p = 0U; p < ROLLBACK_MAX_PLAYERS; p += 4U){
						mu_layout_row(ctx, 8, (int[]){ mui_get_text_width("PAD1", -1) + 8, 82, mui_get_text_width("PAD2", -1) + 8, 82, mui_get_text_width("PAD3", -1) + 8, 82, mui_get_text_width("PAD4", -1) + 8, 82 }, 24);
						mu_label(ctx, (p + 0U) == 0U ? "PAD1" : (p + 0U) == 1U ? "PAD2" : (p + 0U) == 2U ? "PAD3" : (p + 0U) == 3U ? "PAD4" : (p + 0U) == 4U ? "PAD5" : (p + 0U) == 5U ? "PAD6" : (p + 0U) == 6U ? "PAD7" : "PAD8");
						if ((p + 0U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 0U], (int)sizeof(mui_np_pad_labels[p + 0U])); else mu_label(ctx, "");
						mu_label(ctx, (p + 1U) == 0U ? "PAD1" : (p + 1U) == 1U ? "PAD2" : (p + 1U) == 2U ? "PAD3" : (p + 1U) == 3U ? "PAD4" : (p + 1U) == 4U ? "PAD5" : (p + 1U) == 5U ? "PAD6" : (p + 1U) == 6U ? "PAD7" : "PAD8");
						if ((p + 1U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 1U], (int)sizeof(mui_np_pad_labels[p + 1U])); else mu_label(ctx, "");
						mu_label(ctx, (p + 2U) == 0U ? "PAD1" : (p + 2U) == 1U ? "PAD2" : (p + 2U) == 2U ? "PAD3" : (p + 2U) == 3U ? "PAD4" : (p + 2U) == 4U ? "PAD5" : (p + 2U) == 5U ? "PAD6" : (p + 2U) == 6U ? "PAD7" : "PAD8");
						if ((p + 2U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 2U], (int)sizeof(mui_np_pad_labels[p + 2U])); else mu_label(ctx, "");
						mu_label(ctx, (p + 3U) == 0U ? "PAD1" : (p + 3U) == 1U ? "PAD2" : (p + 3U) == 2U ? "PAD3" : (p + 3U) == 3U ? "PAD4" : (p + 3U) == 4U ? "PAD5" : (p + 3U) == 5U ? "PAD6" : (p + 3U) == 6U ? "PAD7" : "PAD8");
						if ((p + 3U) < ROLLBACK_MAX_PLAYERS) mu_textbox(ctx, mui_np_pad_labels[p + 3U], (int)sizeof(mui_np_pad_labels[p + 3U])); else mu_label(ctx, "");
					}
				}

				if (lb.is_host){
					mu_layout_row(ctx, 4, (int[]){ mui_button_width(lb.local_ready ? "Unready" : "Ready"), mui_button_width("Auto Split"), mui_button_width("Start Match"), -1 }, 24);
				}else{
					mu_layout_row(ctx, 3, (int[]){ mui_button_width(lb.local_ready ? "Unready" : "Ready"), mui_button_width("Auto Split"), -1 }, 24);
				}
				if (mu_button(ctx, lb.local_ready ? "UNREADY" : "READY")){
					netplay_lobby_set_ready(lb.local_ready ? FALSE : TRUE);
					mui_lobby_status[0] = 0;
				}
				if (lb.is_host){
					if (mu_button(ctx, "AUTO SPLIT")){
						auint hp = 0U;
						auint gp = 0U;
						for (i = 0U; i < lb.max_players; i++){
							if ((i & 1U) == 0U){
								(void)netplay_lobby_set_seat(i, NETPLAY_LOBBY_OWNER_HOST, hp < ROLLBACK_MAX_PLAYERS ? hp : 0U);
								hp++;
							}else{
								(void)netplay_lobby_set_seat(i, NETPLAY_LOBBY_OWNER_GUEST, gp < ROLLBACK_MAX_PLAYERS ? gp : 0U);
								gp++;
							}
						}
						snprintf(mui_lobby_status, sizeof(mui_lobby_status), "APPLIED ALTERNATING HOST/GUEST SPLIT; READY RESET");
					}
					if (mu_button(ctx, "START MATCH")){
						if (netplay_lobby_start_match()){
							snprintf(mui_lobby_status, sizeof(mui_lobby_status), "MATCH STARTED");
						}else{
							snprintf(mui_lobby_status, sizeof(mui_lobby_status), "START FAILED");
						}
					}
				}else{
					(void)mui_disabled_button(ctx, "AUTO SPLIT");
				}
				mu_label(ctx, mui_lobby_status);

				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				mu_label(ctx, lb.is_host ? "SEATS (HOST EDITS AND APPROVES GUEST REQUESTS):" : "SEATS (GUEST CAN REQUEST CLAIM/RELEASE AND CHOOSE LOCAL PAD):");
				for (i = 0U; i < lb.max_players; i++){
					char seatbuf[160];
					mui_lobby_format_seat(seatbuf, sizeof(seatbuf), &lb, i, lb.seat_owner_peer[i], lb.seat_pad_index[i]);
					if (lb.is_host){
						mu_layout_row(ctx, 6, (int[]){ 190, mui_button_width("HOST"), mui_button_width("GUEST"), mui_button_width("NONE"), mui_button_width("PAD+"), -1 }, 24);
						mu_label(ctx, seatbuf);
						if (mu_button(ctx, "HOST")){
							(void)netplay_lobby_set_seat(i, NETPLAY_LOBBY_OWNER_HOST, lb.seat_pad_index[i]);
							snprintf(mui_lobby_status, sizeof(mui_lobby_status), "Host took P%u; ready reset", (unsigned)(i + 1U));
						}
						if (mu_button(ctx, "GUEST")){
							(void)netplay_lobby_set_seat(i, NETPLAY_LOBBY_OWNER_GUEST, lb.seat_pad_index[i]);
							snprintf(mui_lobby_status, sizeof(mui_lobby_status), "Guest assigned to P%u; ready reset", (unsigned)(i + 1U));
						}
						if (mu_button(ctx, "NONE")){
							(void)netplay_lobby_set_seat(i, NETPLAY_LOBBY_OWNER_NONE, 0U);
							snprintf(mui_lobby_status, sizeof(mui_lobby_status), "Cleared P%u; ready reset", (unsigned)(i + 1U));
						}
						if (mu_button(ctx, "PAD+")){
							(void)netplay_lobby_set_seat(i, lb.seat_owner_peer[i], (lb.seat_pad_index[i] + 1U) % ROLLBACK_MAX_PLAYERS);
							snprintf(mui_lobby_status, sizeof(mui_lobby_status), "Advanced pad on P%u; ready reset", (unsigned)(i + 1U));
						}
						mu_label(ctx, (lb.seat_owner_peer[i] == NETPLAY_LOBBY_OWNER_NONE) ? "UNASSIGNED" : "");
						if (lb.pending_request_valid[i]){
							char reqbuf[128];
							if (lb.pending_request_owner_peer[i] == NETPLAY_LOBBY_OWNER_NONE){
								snprintf(reqbuf, sizeof(reqbuf), "PENDING: %s RELEASE REQUEST FOR P%u", lb.peer_name[0] ? lb.peer_name : "Guest", (unsigned)(i + 1U));
							}else{
								snprintf(reqbuf, sizeof(reqbuf), "PENDING: %s WANTS P%u -> %s", lb.peer_name[0] ? lb.peer_name : "Guest", (unsigned)(i + 1U), mui_lobby_pad_label(&lb, NETPLAY_LOBBY_OWNER_GUEST, lb.pending_request_pad_index[i]));
							}
							mu_layout_row(ctx, 4, (int[]){ 190, mui_button_width("APPROVE"), mui_button_width("DENY"), -1 }, 24);
							mu_label(ctx, reqbuf);
							if (mu_button(ctx, "APPROVE")){
								(void)netplay_lobby_approve_request(i, TRUE);
								snprintf(mui_lobby_status, sizeof(mui_lobby_status), "APPROVED REQUEST FOR P%u; READY RESET", (unsigned)(i + 1U));
							}
							if (mu_button(ctx, "DENY")){
								(void)netplay_lobby_approve_request(i, FALSE);
								snprintf(mui_lobby_status, sizeof(mui_lobby_status), "DENIED REQUEST FOR P%u", (unsigned)(i + 1U));
							}
							mu_label(ctx, "");
						}
					}else{
						if (mui_lobby_req_pad[i] >= ROLLBACK_MAX_PLAYERS){
							mui_lobby_req_pad[i] = 0U;
						}
						mu_layout_row(ctx, 5, (int[]){ 190, mui_button_width("CLAIM"), mui_button_width("RELEASE"), mui_button_width("PAD+"), -1 }, 24);
						mu_label(ctx, seatbuf);
						if (mu_button(ctx, "CLAIM")){
							if (netplay_lobby_request_seat(i, NETPLAY_LOBBY_OWNER_GUEST, mui_lobby_req_pad[i])){
								snprintf(mui_lobby_status, sizeof(mui_lobby_status), "REQUESTED P%u -> GUEST PAD %u", (unsigned)(i + 1U), (unsigned)(mui_lobby_req_pad[i] + 1U));
							}else{
								snprintf(mui_lobby_status, sizeof(mui_lobby_status), "REQUEST FAILED FOR P%u", (unsigned)(i + 1U));
							}
						}
						if (mu_button(ctx, "RELEASE")){
							if (netplay_lobby_request_seat(i, NETPLAY_LOBBY_OWNER_NONE, 0U)){
								snprintf(mui_lobby_status, sizeof(mui_lobby_status), "REQUESTED RELEASE OF P%u", (unsigned)(i + 1U));
							}else{
								snprintf(mui_lobby_status, sizeof(mui_lobby_status), "RELEASE REQUEST FAILED FOR P%u", (unsigned)(i + 1U));
							}
						}
						if (mu_button(ctx, "PAD+")){
							mui_lobby_req_pad[i] = (mui_lobby_req_pad[i] + 1U) % ROLLBACK_MAX_PLAYERS;
						}
						{
							char reqbuf[64];
							snprintf(reqbuf, sizeof(reqbuf), "REQUEST %s", mui_np_pad_labels[mui_lobby_req_pad[i]][0] ? mui_np_pad_labels[mui_lobby_req_pad[i]] : "Pad");
							mu_label(ctx, reqbuf);
						}
					}
				}

				mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
				mu_begin_panel(ctx, "lobby_chat");
				mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
				{
					auint count = netplay_lobby_get_chat_count();
					auint start = (count > 10U) ? (count - 10U) : 0U;
					for (i = start; i < count; i++){
						if (netplay_lobby_get_chat_line(i, line, (auint)sizeof(line))){
							mu_label(ctx, line);
						}
					}
				}
				mu_end_panel(ctx);
				mu_layout_row(ctx, 3, (int[]){ mui_get_text_width("Chat", -1) + 8, -1, mui_button_width("Send") }, 24);
				mu_label(ctx, "CHAT");
				mu_textbox(ctx, mui_lobby_chat_input, (int)sizeof(mui_lobby_chat_input));
				if (mu_button(ctx, "SEND")){
					if (netplay_lobby_send_chat(mui_lobby_chat_input)){
						mui_lobby_chat_input[0] = 0;
						snprintf(mui_lobby_status, sizeof(mui_lobby_status), "CHAT SENT");
					}else{
						snprintf(mui_lobby_status, sizeof(mui_lobby_status), "CHAT SEND FAILED");
					}
				}
			}
		}
#else
		mu_label(ctx, "Netplay is disabled in this build.");
#endif
		mui_window_end(ctx, MUI_WIN_LOBBY);
	}
}

static void mui_open_tool_mem_window(mu_Context* ctx, auint region)
{
	mui_state.show_tool_romview = FALSE;
	mui_state.show_tool_sram = FALSE;
	mui_state.show_tool_spiram = FALSE;
	mui_state.show_tool_eeprom = FALSE;
	if (region == MAINUI_DBG_MEM_FLASH){
		mui_tool_sync_edit_fields(region, mui_tool_parse_hex(mui_tool_mem_base_rom, 0U));
		mui_window_open(ctx, MUI_WIN_ROM_VIEW);
	}else if (region == MAINUI_DBG_MEM_SRAM){
		mui_tool_sync_edit_fields(region, mui_tool_parse_hex(mui_tool_mem_base_sram, 0U));
		mui_window_open(ctx, MUI_WIN_SRAM_EDIT);
	}else if (region == MAINUI_DBG_MEM_SPIRAM){
		mui_tool_sync_edit_fields(region, mui_tool_parse_hex(mui_tool_mem_base_spiram, 0U));
		mui_window_open(ctx, MUI_WIN_SPIRAM_EDIT);
	}else if (region == MAINUI_DBG_MEM_EEPROM){
		mui_tool_sync_edit_fields(region, mui_tool_parse_hex(mui_tool_mem_base_eeprom, 0U));
		mui_window_open(ctx, MUI_WIN_EEPROM_EDIT);
	}
}

static boole mui_try_open_tool_mem_window(mu_Context* ctx, auint region)
{
	if (mainui_debug_get_mem_region_size(region) == 0U){
		snprintf(mui_tools_status, sizeof(mui_tools_status), "LOAD A ROM FIRST");
		return FALSE;
	}
	if (((region == MAINUI_DBG_MEM_SRAM) || (region == MAINUI_DBG_MEM_EEPROM)) &&
	    (mainui_debug_get_mem_region_size(MAINUI_DBG_MEM_FLASH) == 0U)){
		snprintf(mui_tools_status, sizeof(mui_tools_status), "LOAD A GAME ROM FIRST");
		return FALSE;
	}
	snprintf(mui_tools_status, sizeof(mui_tools_status), "%s", "");
	mui_open_tool_mem_window(ctx, region);
	return TRUE;
}

static auint mui_tool_parse_hex(char const* text, auint defv)
{
	char* endp;
	unsigned long v;
	if ((text == NULL) || (text[0] == 0)){ return defv; }
	v = strtoul(text, &endp, 16);
	if ((endp == text) || (*endp != 0)){ return defv; }
	return (auint)v;
}

static void mui_tool_sync_edit_fields(auint region, auint addr)
{
	auint size = mainui_debug_get_mem_region_size(region);
	auint digits = (size > 0x10000U) ? 6U : ((size > 0x1000U) ? 4U : 3U);
	if (size != 0U){
		if (addr >= size){ addr = size - 1U; }
	}else{
		addr = 0U;
	}
	snprintf(mui_tool_mem_edit_addr, sizeof(mui_tool_mem_edit_addr), "%0*X", (int)digits, (unsigned)addr);
	snprintf(mui_tool_mem_edit_value, sizeof(mui_tool_mem_edit_value), "%02X", (unsigned)(mainui_debug_get_mem_region_byte(region, addr) & 0xFFU));
}

static void mui_tool_select_byte(auint region, auint addr)
{
	mui_tool_sync_edit_fields(region, addr);
}

static void mui_build_tool_mem_window(mu_Context* ctx, mui_window_id_t win_id, auint region, char* base_text, boole editable)
{
	char ascii[17];
	char buf[32];
	char cell[8];
	auint base;
	auint size;
	auint row;
	auint col;
	auint addr;
	auint sel_addr;
	auint b;
	auint digits;
	int widths[18];
	char const* panel_name;
	if (!mui_window_begin(ctx, win_id)){ return; }
	size = mainui_debug_get_mem_region_size(region);
	digits = (size > 0x10000U) ? 6U : ((size > 0x1000U) ? 4U : 3U);
	if (size == 0U){
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		mu_label(ctx, "Memory region unavailable for current ROM/runtime state.");
		mui_window_end(ctx, win_id);
		return;
	}
	panel_name = (region == MAINUI_DBG_MEM_FLASH) ? "tool_mem_rom" : ((region == MAINUI_DBG_MEM_SRAM) ? "tool_mem_sram" : ((region == MAINUI_DBG_MEM_SPIRAM) ? "tool_mem_spiram" : "tool_mem_eeprom"));
	for (col = 0U; col < 18U; col++){
		widths[col] = 20;
	}
	widths[0] = 54;
	widths[17] = -1;
	mu_layout_row(ctx, 5, (int[]){ mui_button_width("PgUp"), mui_button_width("Up"), mui_button_width("Down"), mui_button_width("PgDn"), -1 }, 24);
	if (mu_button(ctx, "PgUp")){
		base = mui_tool_parse_hex(base_text, 0U);
		base = (base >= 0x100U) ? (base - 0x100U) : 0U;
		snprintf(base_text, 15, "%0*X", (int)digits, (unsigned)base);
	}
	if (mu_button(ctx, "Up")){
		base = mui_tool_parse_hex(base_text, 0U);
		base = (base >= 0x10U) ? (base - 0x10U) : 0U;
		snprintf(base_text, 15, "%0*X", (int)digits, (unsigned)base);
	}
	if (mu_button(ctx, "Down")){
		base = mui_tool_parse_hex(base_text, 0U) + 0x10U;
		if (size != 0U){
			auint maxb = (size > 0x10U) ? (size - 0x10U) : 0U;
			if (base > maxb){ base = maxb; }
		}
		snprintf(base_text, 15, "%0*X", (int)digits, (unsigned)base);
	}
	if (mu_button(ctx, "PgDn")){
		base = mui_tool_parse_hex(base_text, 0U) + 0x100U;
		if (size != 0U){
			auint maxb = (size > 0x10U) ? (size - 0x10U) : 0U;
			if (base > maxb){ base = maxb; }
		}
		snprintf(base_text, 15, "%0*X", (int)digits, (unsigned)base);
	}
	mu_label(ctx, "");
	mu_layout_row(ctx, 4, (int[]){ mui_get_text_width("Base", -1) + 8, 86, mui_get_text_width("Sel", -1) + 8, -1 }, 24);
	mu_label(ctx, "Base");
	mu_textbox_ex(ctx, base_text, 15, MU_OPT_ALIGNCENTER);
	mu_label(ctx, "Sel");
	sel_addr = mui_tool_parse_hex(mui_tool_mem_edit_addr, 0U);
	if (size != 0U){
		if (sel_addr >= size){ sel_addr = size - 1U; }
		snprintf(buf, sizeof(buf), "%0*X = %02X", (int)digits, (unsigned)sel_addr, (unsigned)(mainui_debug_get_mem_region_byte(region, sel_addr) & 0xFFU));
	}else{
		snprintf(buf, sizeof(buf), "%0*X = 00", (int)digits, 0);
	}
	mu_label(ctx, buf);
	mu_layout_row(ctx, 18, widths, 22);
	snprintf(buf, sizeof(buf), "%*s", (int)digits, "");
	mu_label(ctx, buf);
	for (col = 0U; col < 16U; col++){
		snprintf(cell, sizeof(cell), "%X", (unsigned)col);
		mu_label(ctx, cell);
	}
	mu_label(ctx, "ASCII");
	mu_layout_row(ctx, 1, (int[]){ -1 }, 260);
	mu_begin_panel(ctx, panel_name);
	for (row = 0U; row < 16U; row++){
		addr = mui_tool_parse_hex(base_text, 0U) + (row * 16U);
		mu_layout_row(ctx, 18, widths, 22);
		snprintf(buf, sizeof(buf), "%0*X:", (int)digits, (unsigned)(addr & ((digits >= 6U) ? 0xFFFFFFU : ((digits == 4U) ? 0xFFFFU : 0x0FFFU))));
		mu_label(ctx, buf);
		for (col = 0U; col < 16U; col++){
			ascii[col] = '.';
			if ((size != 0U) && ((addr + col) < size)){
				b = mainui_debug_get_mem_region_byte(region, addr + col);
				if ((b >= 32U) && (b <= 126U)){ ascii[col] = (char)b; }
				snprintf(cell, sizeof(cell), "%02X", (unsigned)b);
				{
					auint cell_id = addr + col;
					mu_push_id(ctx, &cell_id, (int)sizeof(cell_id));
					if (mu_button_ex(ctx, cell, 0, (((addr + col) == sel_addr) ? MU_OPT_HOLDFOCUS : 0) | MU_OPT_ALIGNCENTER)){
						mui_tool_select_byte(region, addr + col);
					}
					mu_pop_id(ctx);
				}
			}else{
				mu_label(ctx, "");
			}
		}
		ascii[16] = 0;
		mu_label(ctx, ascii);
	}
	mu_end_panel(ctx);
	if (editable){
		mu_layout_row(ctx, 7, (int[]){ mui_button_width("Sel"), mui_get_text_width("Addr", -1) + 8, 76, mui_get_text_width("Val", -1) + 8, 54, mui_button_width("Write"), -1 }, 24);
		if (mu_button(ctx, "Sel")){
			mui_tool_sync_edit_fields(region, mui_tool_parse_hex(mui_tool_mem_edit_addr, 0U));
		}
		mu_label(ctx, "Addr");
		mu_textbox_ex(ctx, mui_tool_mem_edit_addr, (int)sizeof(mui_tool_mem_edit_addr), MU_OPT_ALIGNCENTER);
		mu_label(ctx, "Val");
		mu_textbox_ex(ctx, mui_tool_mem_edit_value, (int)sizeof(mui_tool_mem_edit_value), MU_OPT_ALIGNCENTER);
		if (mu_button(ctx, "Write")){
			auint write_addr = mui_tool_parse_hex(mui_tool_mem_edit_addr, 0U);
			mainui_debug_set_mem_region_byte(region, write_addr, mui_tool_parse_hex(mui_tool_mem_edit_value, 0U));
			mui_tool_sync_edit_fields(region, write_addr);
		}
		mu_label(ctx, editable ? "Click any byte above to load Addr/Val." : "");
	}
	mui_window_end(ctx, win_id);
}

static void mui_build_tool_screenshot_window(mu_Context* ctx)
{
	char pathbuf[MUI_CFG_PATH_CAP + 64];
	if (!mui_window_begin(ctx, MUI_WIN_SCREENSHOT)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
		mu_label(ctx, "INCLUDE GUI");
		if (mu_button(ctx, "SAVE NOW")){
			if (mainui_save_screenshot_auto(pathbuf, (auint)sizeof(pathbuf))){
				snprintf(mui_toolshot_status, sizeof(mui_toolshot_status), "%s", mui_path_basename(pathbuf));
			}else{
				snprintf(mui_toolshot_status, sizeof(mui_toolshot_status), "SCREENSHOT FAILED");
			}
		}
		mu_label(ctx, mui_toolshot_status);
		mui_window_end(ctx, MUI_WIN_SCREENSHOT);
	}
}

static void mui_build_tool_video_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_DUMP_VIDEO)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
#ifdef ENABLE_VCAP
		mu_label(ctx, "OUTPUT FILE");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_textbox(ctx, mui_toolvideo_file, (int)sizeof(mui_toolvideo_file)) & MU_RES_CHANGE){
			mainui_set_video_dump_file(mui_toolvideo_file);
		}
		mu_layout_row(ctx, 1, (int[]){ mui_button_width("BROWSE") }, 24);
		if (mu_button(ctx, "BROWSE")){
			mui_pending_file_target = MUI_FILE_TARGET_VIDEO_DUMP;
			mui_filedialog_open_save(&mui_cfg_file_dialog, "SELECT VIDEO OUTPUT FILE", mui_toolvideo_file, ".mp4");
		}
		mu_layout_row(ctx, 2, (int[]){ 150, -1 }, 24);
		{
			int auto_inc = mainui_get_video_dump_autoinc() ? 1 : 0;
			if (mu_checkbox(ctx, "AUTO INCREMENT", &auto_inc)){
				mainui_set_video_dump_autoinc(auto_inc ? TRUE : FALSE);
			}
		}
		{
			int reset_first = mainui_get_video_dump_reset_first() ? 1 : 0;
			if (mu_checkbox(ctx, "RESET FIRST", &reset_first)){
				mainui_set_video_dump_reset_first(reset_first ? TRUE : FALSE);
			}
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
		mu_label(ctx, mainui_get_video_dump_status());
		if (mainui_get_video_dump_active_file()[0] != 0){
			char line[APPCFG_PATH_MAX + 32];
			snprintf(line, sizeof(line), "%s FILE: %s", mainui_get_video_dump_active() ? "ACTIVE" : "LAST", mainui_get_video_dump_active_file());
			mu_label(ctx, line);
		}
		if (mu_button(ctx, mainui_get_video_dump_active() ? "STOP VIDEO DUMP" : "START VIDEO DUMP")){
			boole next = mainui_get_video_dump_active() ? FALSE : TRUE;
			mainui_set_video_dump_file(mui_toolvideo_file);
			mainui_set_video_dump_active(next);
			snprintf(mui_toolvideo_status, sizeof(mui_toolvideo_status), next ? "VIDEO DUMP RUNNING" : "VIDEO DUMP FINALIZED");
		}
		mu_label(ctx, mui_toolvideo_status);
#else
		mu_label(ctx, "VIDEO DUMP IS DISABLED IN THIS BUILD.");
#endif
		mui_window_end(ctx, MUI_WIN_DUMP_VIDEO);
	}
}

static void mui_build_tool_inputcap_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_INPUT_CAPTURE)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
#ifdef ENABLE_ICAP
#ifndef ENABLE_IREP
		mu_label(ctx, "Build mode: RECORD");
#else
		mu_label(ctx, "Build mode: REPLAY");
#endif
		{
			char line[160];
			snprintf(line, sizeof(line), "File: %s", mainui_get_input_capture_file());
			mu_label(ctx, line);
		}
		if (mu_button(ctx, mainui_get_input_capture_active() ? "STOP" : "START")){
			boole next = mainui_get_input_capture_active() ? FALSE : TRUE;
			mainui_set_input_capture_active(next);
			snprintf(mui_toolcap_status, sizeof(mui_toolcap_status), next ? "Input capture active" : "Input capture stopped");
		}
		mu_label(ctx, mui_toolcap_status);
#else
		mu_label(ctx, "Input capture is disabled in this build.");
#endif
		mui_window_end(ctx, MUI_WIN_INPUT_CAPTURE);
	}
}


static void mui_build_tool_esp_monitor_window(mu_Context* ctx)
{
	cu_state_esp_t const* es;
	char line[320];
	char flags[256];
	char sockbuf[32];
	auint i;
	auint open_links;
	auint aps;

	if (!mui_window_begin(ctx, MUI_WIN_ESP_MONITOR)){ return; }
	es = cu_esp_get_state();
	open_links = mui_esp_count_open_links(es);
	aps = mui_esp_count_discovered_aps(es);
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		mu_label(ctx, "LIVE ESP RUNTIME VIEW. USE THE TABS BELOW TO KEEP IT READABLE ON SMALLER DISPLAYS.");
		mu_layout_row(ctx, 4, (int[]){ mui_button_width("CORE"), mui_button_width("NETWORK"), mui_button_width("LINKS"), -1 }, 22);
		if (mu_button(ctx, "CORE")){ mui_esp_monitor_tab = 0U; }
		if (mu_button(ctx, "NETWORK")){ mui_esp_monitor_tab = 1U; }
		if (mu_button(ctx, "LINKS")){ mui_esp_monitor_tab = 2U; }
		if (mu_button(ctx, "OPEN TRACE")){
			mainui_open_web_tool("serial");
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		if (es == NULL){
			mu_label(ctx, "ESP STATE UNAVAILABLE.");
			mui_window_end(ctx, MUI_WIN_ESP_MONITOR);
			return;
		}
		if (mui_esp_monitor_tab == 0U){
			mu_label(ctx, "CORE");
			snprintf(line, sizeof(line), "ROUTE: %s", mui_serial_route_name(mainui_get_serial_route()));
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "MODEL: %s", mui_serial_esp_name(mainui_get_serial_esp_model()));
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "UART: %s", mui_uart_profile_name(mainui_get_uart_profile()));
			mu_label(ctx, line);
			mui_esp_state_flags_string(es, flags, (auint)sizeof(flags));
			snprintf(line, sizeof(line), "FLAGS: %s", flags);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "WIFI MODE: %s", mui_esp_wifi_mode_name(es->wifi_mode));
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "USER MODE: %s", mui_esp_user_mode_name(es->user_input_mode));
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "READY: %u", (unsigned)es->ready);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "FLASH DIRTY: %u", (unsigned)es->flash_dirty);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "ACTIVE SOCK: %u", (unsigned)es->active_socket);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "SEND SOCK: %u", (unsigned)es->send_to_socket);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "CIP MODE: %u", (unsigned)es->cip_mode);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "CIPBUF: %u", (unsigned)es->cipbuf_recv_mode);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "OPEN LINKS: %u", (unsigned)open_links);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "RX PACKET BYTES: %u", (unsigned)es->rx_packet_bytes);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "AWAIT BYTES: %u", (unsigned)es->rx_await_bytes);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "AWAIT TIME: %u", (unsigned)es->rx_await_time);
			mu_label(ctx, line);
		}
		if (mui_esp_monitor_tab == 1U){
			mu_label(ctx, "NETWORK");
			snprintf(line, sizeof(line), "STA SSID: %s", es->wifi_name[0] ? (char const*)es->wifi_name : "(NONE)");
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "RSSI: %d", (int)es->wifi_rssi);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "CHANNEL: %u", (unsigned)es->wifi_channel);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "JOIN PENDING: %u", (unsigned)es->wifi_join_pending);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "STA IP: %s", es->station_ip[0] ? (char const*)es->station_ip : "(UNSET)");
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "GW: %s", es->station_gateway[0] ? (char const*)es->station_gateway : "(UNSET)");
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "NM: %s", es->station_netmask[0] ? (char const*)es->station_netmask : "(UNSET)");
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "AP SSID: %s", es->soft_ap_name[0] ? (char const*)es->soft_ap_name : "(NONE)");
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "AP IP: %s", es->soft_ap_ip[0] ? (char const*)es->soft_ap_ip : "(UNSET)");
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "DHCP-AP: %u", (unsigned)es->lan.dhcp_enable_ap);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "DHCP-STA: %u", (unsigned)es->lan.dhcp_enable_sta);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "LAN ENABLE: %u", (unsigned)es->lan.enable);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "ADV: %u", (unsigned)es->lan.adv_enable);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "JOINED: %u", (unsigned)es->lan.joined);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "JOIN REQ: %u", (unsigned)es->lan.join_req_pending);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "DISCOVERED APS: %u", (unsigned)aps);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "DNS PENDING: %u", (unsigned)es->dns_pending);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "PING PENDING: %u", (unsigned)es->ping_pending);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "UDP RESOLVE PENDING: %u", (unsigned)es->udp_send_resolve_pending);
			mu_label(ctx, line);
			snprintf(line, sizeof(line), "SERVER MAX CONN: %u", (unsigned)es->server_max_conn);
			mu_label(ctx, line);
			if (mainui_get_serial_route() == CU_ESP_SERIAL_TCP_SERIAL){
				snprintf(line, sizeof(line), "TCP SERIAL MODE: %s", mui_tcp_serial_mode_name(mainui_get_tcp_serial_mode()));
				mu_label(ctx, line);
				snprintf(line, sizeof(line), "TCP SERIAL STATE: %s", mui_tcp_serial_state_name(mainui_get_tcp_serial_state()));
				mu_label(ctx, line);
				snprintf(line, sizeof(line), "LAST ERR: %d", (int)mainui_get_tcp_serial_last_error());
				mu_label(ctx, line);
				snprintf(line, sizeof(line), "TXQ: %u", (unsigned)es->tcp_serial_tx_count);
				mu_label(ctx, line);
				snprintf(line, sizeof(line), "RXQ: %u", (unsigned)es->tcp_serial_rx_count);
				mu_label(ctx, line);
			}
		}
		if (mui_esp_monitor_tab == 2U){
			mu_label(ctx, "LINKS");
			mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
			mu_begin_panel(ctx, "esp_monitor_links_panel");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
			for (i = 0U; i < (auint)ESP_MAX_LINKS; ++i){
				snprintf(sockbuf, sizeof(sockbuf), "%s", (es->socks[i] == ESP_INVALID_SOCKET) ? "CLOSED" : "OPEN");
				snprintf(line, sizeof(line), "LINK %u  %s", (unsigned)i, sockbuf);
				if (mu_begin_treenode(ctx, line)){
					mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
					snprintf(line, sizeof(line), "STATE: %s", mui_esp_link_state_name(es->link_state[i]));
					mu_label(ctx, line);
					snprintf(line, sizeof(line), "PROTO: %s", mui_esp_proto_name(es->link_type[i]));
					mu_label(ctx, line);
					snprintf(line, sizeof(line), "PORT: %u", (unsigned)es->link_port[i]);
					mu_label(ctx, line);
					snprintf(line, sizeof(line), "SOCK: %s", sockbuf);
					mu_label(ctx, line);
					snprintf(line, sizeof(line), "HOST: %s", (es->link_host[i][0] != 0) ? es->link_host[i] : "(NONE)");
					mu_label(ctx, line);
					snprintf(line, sizeof(line), "ERR: %d", (int)es->link_err[i]);
					mu_label(ctx, line);
					snprintf(line, sizeof(line), "READY MS: %u", (unsigned)es->link_ready_ms[i]);
					mu_label(ctx, line);
					mu_end_treenode(ctx);
				}
			}
			mu_end_panel(ctx);
		}
		mui_window_end(ctx, MUI_WIN_ESP_MONITOR);
	}
}

static boole mui_input_trace_line_visible(char const* line)
{
	boole is_evt;
	boole is_tap;
	boole is_kbd;
	boole is_frame;
	boole hit_p1;
	boole hit_p2;
	if (line == NULL){ return FALSE; }
	is_evt = (strstr(line, " EVT ") != NULL) ? TRUE : FALSE;
	is_tap = ((strstr(line, " EVT TAP_SELECT ") != NULL) || (strstr(line, " EVT TAP_PROBE ") != NULL)) ? TRUE : FALSE;
	is_kbd = ((strstr(line, " KBD_VISIBLE ") != NULL) || (strstr(line, " EVT KBD_BYTES ") != NULL)) ? TRUE : FALSE;
	is_frame = (!is_evt && !is_kbd) ? TRUE : FALSE;
	hit_p1 = (strstr(line, " P1") != NULL) ? TRUE : FALSE;
	hit_p2 = (strstr(line, " P2") != NULL) ? TRUE : FALSE;
	if (is_frame && !mui_input_trace_show_frames){ return FALSE; }
	if (is_tap && !mui_input_trace_show_tap){ return FALSE; }
	if (is_kbd && !mui_input_trace_show_kbd){ return FALSE; }
	if (!hit_p1 && !hit_p2){ return TRUE; }
	if (hit_p1 && mui_input_trace_show_p1){ return TRUE; }
	if (hit_p2 && mui_input_trace_show_p2){ return TRUE; }
	return FALSE;
}

static boole mui_input_trace_export_filtered(char const* path, boole filtered_only)
{
	FILE* fp;
	char trace_line[320];
	auint line_count;
	auint i;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	fp = fopen(path, "wb");
	if (fp == NULL){ return FALSE; }
	line_count = mainui_get_input_trace_line_count();
	for (i = 0U; i < line_count; ++i){
		mainui_format_input_trace_line(i, trace_line, (auint)sizeof(trace_line));
		if (filtered_only && !mui_input_trace_line_visible(trace_line)){
			continue;
		}
		fprintf(fp, "%s\n", trace_line);
	}
	fclose(fp);
	return TRUE;
}

static void mui_build_tool_input_trace_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_INPUT_TRACE)){ return; }
	{
		char status_line[160];
		char trace_line[320];
		auint line_count = mainui_get_input_trace_line_count();
		auint cap = mainui_get_input_trace_capacity();
		auint start_line;
		auint i;

		mu_layout_row(ctx, 4, (int[]){ 120, 80, 110, 96 }, 24);
		if (mu_button(ctx, mainui_get_input_trace_enabled() ? "STOP TRACE" : "ARM TRACE")){
			mainui_set_input_trace_enabled(mainui_get_input_trace_enabled() ? FALSE : TRUE);
			snprintf(mui_input_trace_status, sizeof(mui_input_trace_status), "%s", mainui_get_input_trace_enabled() ? "INPUT TRACE ARMED" : "INPUT TRACE STOPPED");
			textgui_log_add(mui_input_trace_status);
		}
		if (mu_button(ctx, "CLEAR")){
			mainui_clear_input_trace();
			snprintf(mui_input_trace_status, sizeof(mui_input_trace_status), "TRACE CLEARED");
			textgui_log_add("INPUT TRACE CLEARED");
		}
		if (mu_button(ctx, "EXPORT VIEW")){
			if (mui_input_trace_export_filtered("input-trace.txt", TRUE))
				snprintf(mui_input_trace_status, sizeof(mui_input_trace_status), "WROTE INPUT-TRACE.TXT");
			else
				snprintf(mui_input_trace_status, sizeof(mui_input_trace_status), "FAILED TO WRITE INPUT-TRACE.TXT");
			textgui_log_add(mui_input_trace_status);
		}
		if (mu_button(ctx, "EXPORT ALL")){
			if (mui_input_trace_export_filtered("input-trace-all.txt", FALSE))
				snprintf(mui_input_trace_status, sizeof(mui_input_trace_status), "WROTE INPUT-TRACE-ALL.TXT");
			else
				snprintf(mui_input_trace_status, sizeof(mui_input_trace_status), "FAILED TO WRITE INPUT-TRACE-ALL.TXT");
			textgui_log_add(mui_input_trace_status);
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		snprintf(status_line, sizeof(status_line), "FRAMES/EVENTS: %u / %u", (unsigned)line_count, (unsigned)cap);
		mu_label(ctx, status_line);
		mu_label(ctx, "SHOWS PER-FRAME PACKET SNAPSHOTS");
		mu_label(ctx, "PLUS APPLIED TAP/KEYBOARD EVENTS.");
		mu_label(ctx, mainui_get_input_trace_enabled() ? "CAPTURE IS RUNNING." : "CAPTURE IS STOPPED.");
		if (mui_input_trace_status[0] != 0){ mu_label(ctx, mui_input_trace_status); }

		mu_layout_row(ctx, 5, (int[]){ 90, 76, 76, 56, 56 }, 20);
		mu_checkbox(ctx, "FRAMES", &mui_input_trace_show_frames);
		mu_checkbox(ctx, "TAP", &mui_input_trace_show_tap);
		mu_checkbox(ctx, "KBD", &mui_input_trace_show_kbd);
		mu_checkbox(ctx, "P1", &mui_input_trace_show_p1);
		mu_checkbox(ctx, "P2", &mui_input_trace_show_p2);

		mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
		mu_begin_panel(ctx, "input_trace_panel");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
		if (line_count == 0u){
			mu_label(ctx, "TRACE IS EMPTY.");
			mu_label(ctx, "ARM IT, THEN EXERCISE DEVICES");
			mu_label(ctx, "OR RUN A FEW FRAMES.");
		}else{
			start_line = (line_count > 220u) ? (line_count - 220u) : 0u;
			for (i = start_line; i < line_count; ++i){
				mainui_format_input_trace_line(i, trace_line, (auint)sizeof(trace_line));
				if (!mui_input_trace_line_visible(trace_line)){
					continue;
				}
				mu_label(ctx, trace_line);
			}
		}
		mu_end_panel(ctx);
		mui_window_end(ctx, MUI_WIN_INPUT_TRACE);
	}
}

static void mui_build_tool_serial_trace_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_SERIAL_TRACE)){ return; }
	{
		char status_line[320];
		char uart_line[256];
		char route_line[160];
		char profile_line[96];
		char trace_line[256];
		auint line_count = cu_esp_serial_trace_get_line_count();
		auint evt_count = cu_esp_serial_trace_get_event_count();
		auint cap = cu_esp_serial_trace_get_capacity();
		auint drop_count = cu_esp_serial_trace_get_drop_count();
		auint avr_tx_count = 0u;
		auint avr_rx_count = 0u;
		auint backend_tx_count = 0u;
		auint backend_rx_count = 0u;
		auint cfg_count = 0u;
		auint start_line;
		auint i;
		char* cp;
		cu_esp_tcp_serial_diag_t tcp_diag;

		mu_layout_row(ctx, 4, (int[]){ 120, 80, 140, 120 }, 24);
		if (mu_button(ctx, cu_esp_serial_trace_get_enabled() ? "STOP TRACE" : "ARM TRACE")){
			cu_esp_serial_trace_set_enabled(cu_esp_serial_trace_get_enabled() ? FALSE : TRUE);
			snprintf(mui_trace_status, sizeof(mui_trace_status), "%s", cu_esp_serial_trace_get_enabled() ? "TRACE ARMED" : "TRACE STOPPED");
			textgui_log_add(cu_esp_serial_trace_get_enabled() ? "SERIAL TRACE ARMED" : "SERIAL TRACE STOPPED");
		}
		if (mu_button(ctx, "CLEAR")){
			cu_esp_serial_trace_clear();
			snprintf(mui_trace_status, sizeof(mui_trace_status), "TRACE CLEARED");
			textgui_log_add("SERIAL TRACE CLEARED");
		}
		if (mu_button(ctx, "EXPORT COOKED")){
			if (cu_esp_serial_trace_export("serial-trace-cooked.txt", FALSE))
				snprintf(mui_trace_status, sizeof(mui_trace_status), "WROTE SERIAL-TRACE-COOKED.TXT");
			else
				snprintf(mui_trace_status, sizeof(mui_trace_status), "FAILED TO WRITE SERIAL-TRACE-COOKED.TXT");
			textgui_log_add(mui_trace_status);
		}
		if (mu_button(ctx, "EXPORT RAW")){
			if (cu_esp_serial_trace_export("serial-trace-raw.txt", TRUE))
				snprintf(mui_trace_status, sizeof(mui_trace_status), "WROTE SERIAL-TRACE-RAW.TXT");
			else
				snprintf(mui_trace_status, sizeof(mui_trace_status), "FAILED TO WRITE SERIAL-TRACE-RAW.TXT");
			textgui_log_add(mui_trace_status);
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		if (cu_esp_serial_trace_get_enabled()){
			mu_label(ctx, "TRACE ARMED.");
			mu_label(ctx, "CAPTURE STOPS AUTOMATICALLY");
			mu_label(ctx, "WHEN RAM FILLS.");
		}else if (cu_esp_serial_trace_is_full()){
			mu_label(ctx, "TRACE BUFFER FULL.");
			mu_label(ctx, "CAPTURE STOPPED.");
		}else{
			mu_label(ctx, "TRACE STOPPED.");
		}
		if (mui_trace_status[0] != 0){ mu_label(ctx, mui_trace_status); }

		snprintf(route_line, sizeof(route_line), "ROUTE: %s", mui_serial_route_name(mainui_get_serial_route()));
		mu_label(ctx, route_line);
		snprintf(profile_line, sizeof(profile_line), "PROFILE: %s", mui_uart_profile_name(mainui_get_uart_profile()));
		mu_label(ctx, profile_line);
		cu_esp_serial_trace_get_uart_status(uart_line, (auint)sizeof(uart_line));
		for (cp = uart_line; *cp != 0; ++cp){ *cp = (char)toupper((unsigned char)(*cp)); }
		mu_label(ctx, uart_line);
		cu_esp_serial_trace_get_counts(&avr_tx_count, &avr_rx_count, &backend_tx_count, &backend_rx_count, &cfg_count);
		snprintf(status_line, sizeof(status_line), "EVENTS: %u / %u", (unsigned)evt_count, (unsigned)cap);
		mu_label(ctx, status_line);
		snprintf(status_line, sizeof(status_line), "LINES: %u   DROPPED: %u", (unsigned)line_count, (unsigned)drop_count);
		mu_label(ctx, status_line);
		snprintf(status_line, sizeof(status_line), "AVR TX:%u RX:%u   LINK TX:%u RX:%u   CFG:%u",
			(unsigned)avr_tx_count, (unsigned)avr_rx_count,
			(unsigned)backend_tx_count, (unsigned)backend_rx_count,
			(unsigned)cfg_count);
		mu_label(ctx, status_line);
		if (mainui_get_serial_route() == CU_ESP_SERIAL_TCP_SERIAL){
			snprintf(status_line, sizeof(status_line), "TCP MODE: %s", mui_tcp_serial_mode_name(mainui_get_tcp_serial_mode()));
			mu_label(ctx, status_line);
			snprintf(status_line, sizeof(status_line), "TCP STATE: %s", mui_tcp_serial_state_name(mainui_get_tcp_serial_state()));
			mu_label(ctx, status_line);
			snprintf(status_line, sizeof(status_line), "LAST ERROR: %d", (int)mainui_get_tcp_serial_last_error());
			mu_label(ctx, status_line);
			cu_esp_tcp_serial_diag_get(&tcp_diag);
			snprintf(status_line, sizeof(status_line), "AVR->BACKEND %u  BACKEND->AVR %u  SOCKET TX/RX %u/%u",
				(unsigned)tcp_diag.avr_to_backend_bytes, (unsigned)tcp_diag.backend_to_avr_bytes,
				(unsigned)tcp_diag.socket_tx_bytes, (unsigned)tcp_diag.socket_rx_bytes);
			mu_label(ctx, status_line);
			snprintf(status_line, sizeof(status_line), "QUEUES TX %u/%u RX %u/%u PENDING %u",
				(unsigned)tcp_diag.tx_queue_bytes, (unsigned)tcp_diag.tx_queue_high_water,
				(unsigned)tcp_diag.rx_queue_bytes, (unsigned)tcp_diag.rx_queue_high_water,
				(unsigned)tcp_diag.socket_rx_pending_bytes);
			mu_label(ctx, status_line);
			snprintf(status_line, sizeof(status_line), "CALLS SEND/RX %u/%u  WOULD-BLOCK %u/%u  ERRORS %u/%u",
				(unsigned)tcp_diag.send_calls, (unsigned)tcp_diag.recv_calls,
				(unsigned)tcp_diag.send_would_block, (unsigned)tcp_diag.recv_would_block,
				(unsigned)tcp_diag.send_errors, (unsigned)tcp_diag.recv_errors);
			mu_label(ctx, status_line);
			snprintf(status_line, sizeof(status_line), "SERVICE MAX GAP %u MS >50 %u >250 %u  LOG %s",
				(unsigned)tcp_diag.max_service_gap_ms,
				(unsigned)tcp_diag.service_gap_over_50ms,
				(unsigned)tcp_diag.service_gap_over_250ms,
				cu_esp_tcp_serial_diag_get_log_path());
			mu_label(ctx, status_line);
		}
		mu_label(ctx, "TRACE USES A FIXED RING BUFFER AND IS CHEAP WHEN STOPPED.");

		mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
		mu_begin_panel(ctx, "serial_trace_panel");
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
		if (line_count == 0u){
			mu_label(ctx, "TRACE IS EMPTY.");
			mu_label(ctx, "ARM IT, THEN RESET");
			mu_label(ctx, "OR EXERCISE THE SERIAL PATH.");
		}else{
			start_line = (line_count > 200u) ? (line_count - 200u) : 0u;
			for (i = start_line; i < line_count; ++i){
				cu_esp_serial_trace_format_line(i, trace_line, (auint)sizeof(trace_line));
				mu_label(ctx, trace_line);
			}
		}
		mu_end_panel(ctx);
		mui_window_end(ctx, MUI_WIN_SERIAL_TRACE);
	}
}

static void mui_build_tools_window(mu_Context* ctx)
{
	if (!mui_window_begin(ctx, MUI_WIN_TOOLS)){ return; }
	{
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		if (mu_button_ex(ctx, "1. SCREENSHOT", 0, 0)){
			mui_window_open(ctx, MUI_WIN_SCREENSHOT);
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "2. DUMP VIDEO", 0, 0)){
			mui_sync_toolvideo_file();
			mui_window_open(ctx, MUI_WIN_DUMP_VIDEO);
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "3. INP. CAPTURE", 0, 0)){
			mui_window_open(ctx, MUI_WIN_INPUT_CAPTURE);
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "4. ROM VIEW", 0, 0)){
			if (mui_try_open_tool_mem_window(ctx, MAINUI_DBG_MEM_FLASH)){
				mui_state.show_tools = FALSE;
			}
		}
		if (mu_button_ex(ctx, "5. SRAM EDIT", 0, 0)){
			if (mui_try_open_tool_mem_window(ctx, MAINUI_DBG_MEM_SRAM)){
				mui_state.show_tools = FALSE;
			}
		}
		if (mu_button_ex(ctx, "6. SPI RAM EDIT", 0, 0)){
			if (mui_try_open_tool_mem_window(ctx, MAINUI_DBG_MEM_SPIRAM)){
				mui_state.show_tools = FALSE;
			}
		}
		if (mu_button_ex(ctx, "7. EEPROM EDIT", 0, 0)){
			if (mui_try_open_tool_mem_window(ctx, MAINUI_DBG_MEM_EEPROM)){
				mui_state.show_tools = FALSE;
			}
		}
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
		if (mu_button_ex(ctx, "8. WEB DEBUGGER", 0, 0)){
			mainui_open_web_tool("debugger");
			mui_state.show_tools = FALSE;
		}
#else
		mu_label(ctx, "8. WEB DEBUGGER (DISABLED)");
#endif
		if (mu_button_ex(ctx, "9. VIEW CHEATS", 0, 0)){
			mui_window_open(ctx, MUI_WIN_VIEW_CHEATS);
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "0. SEARCH CHEATS", 0, 0)){
			mui_window_open(ctx, MUI_WIN_SEARCH_CHEATS);
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "A. INPUT TRACE", 0, 0)){
			mui_window_open(ctx, MUI_WIN_INPUT_TRACE);
			mui_state.show_tools = FALSE;
		}
#ifdef ENABLE_API_SERVER
		if (mu_button_ex(ctx, "B. WEB SERIAL", 0, 0)){
			mainui_open_web_tool("serial");
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "C. WEB NETWORK", 0, 0)){
			mainui_open_web_tool("network");
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "D. WEB SD CARD", 0, 0)){
			mainui_open_web_tool("sd");
			mui_state.show_tools = FALSE;
		}
		if (mu_button_ex(ctx, "E. WEB AUDIO", 0, 0)){
			mainui_open_web_tool("audio");
			mui_state.show_tools = FALSE;
		}
#else
		mu_label(ctx, "B. WEB SERIAL (API DISABLED)");
		mu_label(ctx, "C. WEB NETWORK (API DISABLED)");
		mu_label(ctx, "D. WEB SD CARD (API DISABLED)");
		mu_label(ctx, "E. WEB AUDIO (API DISABLED)");
#endif
		mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
		mu_label(ctx, mui_tools_status);
		mui_window_end(ctx, MUI_WIN_TOOLS);
	}
}

static void mui_build_quick_window(mu_Context* ctx)
{
	auint i;
	char label[160];
	char pathbuf[MUI_FILEDIALOG_PATH_CAP];
	boole have_items;
	if (!mui_window_begin(ctx, MUI_WIN_RECENT_ROMS)){ return; }
	{
		have_items = FALSE;
		mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
		for (i = 0U; i < MUI_RECENT_ROMS; i++){
			if (!mainui_get_recent_rom_path(i, pathbuf, sizeof(pathbuf))){ continue; }
			have_items = TRUE;
			snprintf(label, sizeof(label), "%u. %s", (unsigned)(i + 1U), mui_path_basename(pathbuf));
			if (mu_button(ctx, label)){
				mui_recent_launch_index(i);
			}
		}
		if (!have_items){
			mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
			mu_label(ctx, "No recent ROMs");
			mu_layout_row(ctx, 1, (int[]){ -1 }, 224);
			mu_label(ctx, "");
		}
		mui_separator(ctx);
		mu_layout_row(ctx, 2, (int[]){ mui_button_width("FREEZE LIST: OFF"), mui_button_width("CLEAR LIST") }, 24);
		if (mu_button(ctx, mainui_get_recent_roms_frozen() ? "FREEZE LIST: ON" : "FREEZE LIST: OFF")){
			mainui_set_recent_roms_frozen(mainui_get_recent_roms_frozen() ? FALSE : TRUE);
		}
		if (mu_button(ctx, "CLEAR LIST")){
			mainui_clear_recent_roms();
		}
		mui_window_end(ctx, MUI_WIN_RECENT_ROMS);
	}
}

boole mui_get_style_snapshot(mu_Style* out)
{
	if ((out == NULL) || (mui_state.ctx == NULL) || (mui_state.ctx->style == NULL)){ return FALSE; }
	memcpy(out, mui_state.ctx->style, sizeof(*out));
	return TRUE;
}

void mui_apply_style_snapshot(mu_Style const* in)
{
	if ((in == NULL) || (mui_state.ctx == NULL) || (mui_state.ctx->style == NULL)){ return; }
	memcpy(mui_state.ctx->style, in, sizeof(*in));
}

#ifdef ENABLE_DEBUGGER
void mui_debug_value_watches_reset_defaults(void)
{
	mui_dbg_reset_value_watches_defaults();
}

boole mui_debug_value_watch_get(auint idx, boole* out_enable, auint* out_region, char* out_addr, auint out_size)
{
	if (idx >= MAINUI_DBG_VALUE_WATCHES){ return FALSE; }
	if (out_enable != NULL){ *out_enable = (mui_dbg_watch_enable[idx] != 0); }
	if (out_region != NULL){ *out_region = (auint)mui_dbg_watch_region[idx]; }
	if ((out_addr != NULL) && (out_size != 0U)){
		strncpy(out_addr, mui_dbg_watch_addr[idx], out_size - 1U);
		out_addr[out_size - 1U] = 0;
	}
	return TRUE;
}

void mui_debug_value_watch_set(auint idx, boole enable, auint region, char const* addr)
{
	if (idx >= MAINUI_DBG_VALUE_WATCHES){ return; }
	mui_dbg_watch_enable[idx] = enable ? 1 : 0;
	mui_dbg_watch_region[idx] = (int)region;
	strncpy(mui_dbg_watch_addr[idx], (addr != NULL) ? addr : "000", sizeof(mui_dbg_watch_addr[idx]) - 1U);
	mui_dbg_watch_addr[idx][sizeof(mui_dbg_watch_addr[idx]) - 1U] = 0;
	mui_dbg_watch_seen[idx] = 0;
	mui_dbg_watch_last[idx] = 0U;
}
#endif

int mui_init(void)
{
	memset(&mui_state, 0, sizeof(mui_state));
	mui_filedialog_init(&mui_rom_dialog);
	mui_filedialog_init(&mui_dir_dialog);
	mui_filedialog_init(&mui_cfg_file_dialog);
	mui_state.ctx = malloc(sizeof(mu_Context));
	if (mui_state.ctx == NULL){
		return 0;
	}
	mu_init(mui_state.ctx);
	mui_state.ctx->text_width = mui_text_width;
	mui_state.ctx->text_height = mui_text_height;
	mui_state.ctx->style->padding = (int)APPCFG_GUI_PADDING;
	mui_state.ctx->style->spacing = (int)APPCFG_GUI_SPACING;
	mui_state.ctx->style->title_height = (int)APPCFG_GUI_TITLE_HEIGHT;
	mui_state.ctx->style->scrollbar_size = (int)APPCFG_GUI_SCROLLBAR_SIZE;
	mui_state.ctx->style->thumb_size = (int)APPCFG_GUI_THUMB_SIZE;
	mui_state.clip = mu_rect(0, 0, 0, 0);
	SDL_StartTextInput();
#ifdef ENABLE_DEBUGGER
	mui_dbg_reset_value_watches_defaults();
	mui_dbg_load_watchpoint_slot(0U);
#endif
	mui_sync_toolvideo_file();
	return 1;
}

void mui_shutdown(void)
{
	mui_filedialog_destroy(&mui_rom_dialog);
	mui_filedialog_destroy(&mui_dir_dialog);
	mui_filedialog_destroy(&mui_cfg_file_dialog);
	if (mui_state.ctx != NULL){
		free(mui_state.ctx);
		mui_state.ctx = NULL;
	}
}

boole mui_is_visible(void)
{
	return mui_state.bar_visible;
}

boole mui_is_open(void)
{
	return mui_state.bar_pinned;
}

boole mui_wants_mouse(void)
{
	if (mui_state.bar_pinned){ return TRUE; }
	if (!mui_state.bar_visible){ return FALSE; }
	return (mui_state.mouse_y >= 0) && (mui_state.mouse_y < MUI_BAR_HEIGHT);
}

boole mui_wants_keyboard(void)
{
	return mui_state.bar_pinned;
}

void mui_open_rom_load_window(void)
{
	if (mui_state.ctx == NULL){ return; }
	mui_state.bar_visible = TRUE;
	mui_state.bar_pinned = TRUE;
	mui_window_open(mui_state.ctx, MUI_WIN_GAME);
	mui_filedialog_open(&mui_rom_dialog, "ROM LOAD", mainui_get_rom_path(), ".uze;.hex");
	mui_set_game_status("SELECT ROM");
}

int mui_handle_event(SDL_Event const* ev)
{
	int   b;
	boole inside;
	boole wants_mouse;

	if (mui_state.ctx == NULL){ return 0; }

	wants_mouse = FALSE;
	inside = mui_state.mouse_inside;

	switch (ev->type){
		case SDL_MOUSEMOTION:
			inside = mui_update_mouse_position(ev->motion.x, ev->motion.y);
			if (!inside){ return 0; }
			wants_mouse = mui_wants_mouse();
			if (wants_mouse){
				mu_input_mousemove(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y);
				return 1;
			}
			return 0;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			inside = mui_update_mouse_position(ev->button.x, ev->button.y);
			if (!inside){
				if ((ev->type == SDL_MOUSEBUTTONDOWN) && mui_state.bar_pinned){
					mui_close_all_windows();
					mui_refresh_visibility();
					return 1;
				}
				return 0;
			}
			if ((ev->type == SDL_MOUSEBUTTONDOWN) && mui_state.bar_pinned && !mui_point_in_any_ui(mui_state.mouse_x, mui_state.mouse_y)){
				mui_close_all_windows();
				mui_refresh_visibility();
				return 1;
			}
			wants_mouse = mui_wants_mouse();
			if (!wants_mouse){ return 0; }
			b = button_map[ev->button.button & 0xFF];
			if (b != 0){
				if (ev->type == SDL_MOUSEBUTTONDOWN){
					mu_input_mousedown(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y, b);
				}else{
					mu_input_mouseup(mui_state.ctx, mui_state.mouse_x, mui_state.mouse_y, b);
				}
			}
			return 1;
		case SDL_MOUSEWHEEL:
			if (!mui_wants_mouse()){ return 0; }
			mu_input_scroll(mui_state.ctx, 0, ev->wheel.y * -30);
			return 1;
		case SDL_TEXTINPUT:
			if (!mui_wants_keyboard()){ return 0; }
			mu_input_text(mui_state.ctx, ev->text.text);
			return 1;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (mui_state.show_quick && (ev->type == SDL_KEYDOWN)){
				if ((ev->key.keysym.sym >= (int)'1') && (ev->key.keysym.sym <= (int)'9')){
					if (mui_recent_launch_index((auint)(ev->key.keysym.sym - (int)'1'))){
						mui_refresh_visibility();
						return 1;
					}
				}else if (ev->key.keysym.sym == (int)'0'){
					if (mui_recent_launch_index(9U)){
						mui_refresh_visibility();
						return 1;
					}
				}
			}
			if ((ev->key.keysym.sym == SDLK_ESCAPE) && (ev->type == SDL_KEYDOWN)){
				if (mui_close_topmost_window()){
					mui_refresh_visibility();
					return 1;
				}
			}
			if (!mui_wants_keyboard()){ return 0; }
			b = key_map[ev->key.keysym.sym & 0xFF];
			if (b != 0){
				if (ev->type == SDL_KEYDOWN){
					mu_input_keydown(mui_state.ctx, b);
				}else{
					mu_input_keyup(mui_state.ctx, b);
				}
				return 1;
			}
			return 0;
		default:
			return 0;
	}
}

void mui_render_overlay(uint32* dest, auint pitch, auint texw, auint texh)
{
	mu_Command* cmd;

	if (mui_state.ctx == NULL){ return; }
	mui_state.dest = dest;
	mui_state.pitch = pitch;
	mui_state.texw = texw;
	mui_state.texh = texh;
	mui_state.clip = mu_rect(0, 0, (int)texw, (int)texh);
	memset(mui_window_seen_frame, 0, sizeof(mui_window_seen_frame));
#ifdef ENABLE_GUI_VKEYBOARD
	mui_vkbd_begin_frame();
#endif
	mui_gamepad_tick();
	mui_sync_all_windows(mui_state.ctx);
#ifdef ENABLE_NETPLAY
	mui_netplay_auto_open_lobby(mui_state.ctx);
#endif
	mui_refresh_visibility();
	if (!mui_state.bar_visible && !mui_state.bar_pinned){ return; }

	mu_begin(mui_state.ctx);
	mui_build_topbar(mui_state.ctx);
	if (mui_filedialog_is_open(&mui_rom_dialog) || mui_filedialog_is_open(&mui_dir_dialog) || mui_filedialog_is_open(&mui_cfg_file_dialog)){
		mui_build_rom_dialog(mui_state.ctx);
		mui_build_dir_dialog(mui_state.ctx);
		mui_build_cfg_file_dialog(mui_state.ctx);
#ifdef ENABLE_GUI_VKEYBOARD
		mui_build_virtual_keyboard(mui_state.ctx);
#endif
		mu_end(mui_state.ctx);
		mui_sync_all_windows(mui_state.ctx);
		mui_apply_pending_dir_change();
		mui_apply_pending_file_change();
		mui_apply_pending_rom_load();
		cmd = NULL;
		while (mu_next_command(mui_state.ctx, &cmd)){
			switch (cmd->type){
				case MU_COMMAND_TEXT:
					mui_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color);
					break;
				case MU_COMMAND_RECT:
					mui_draw_rect(cmd->rect.rect, cmd->rect.color);
					break;
				case MU_COMMAND_ICON:
					mui_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
					break;
				case MU_COMMAND_CLIP:
					mui_set_clip_rect(cmd->clip.rect);
					break;
				default:
					break;
			}
		}
		mui_gamepad_draw_cursor();
		return;
	}
	mui_build_game_window(mui_state.ctx);
	mui_build_statepick_window(mui_state.ctx);
	mui_build_config_window(mui_state.ctx);
	mui_build_config_page_window(mui_state.ctx);
	mui_build_sd_write_warning_window(mui_state.ctx);
	mui_build_serial_window(mui_state.ctx);
	mui_build_tools_window(mui_state.ctx);
	if (mui_tools_ui_active()){
		mui_build_cheats_window(mui_state.ctx);
		mui_build_cheat_search_window(mui_state.ctx);
	}
	if (mui_netplay_ui_active()){
		mui_build_play_online_window(mui_state.ctx);
#ifdef ENABLE_NETPLAY
		mui_build_online_games_window(mui_state.ctx);
#endif
		mui_build_netplay_window(mui_state.ctx);
		mui_build_netplay_start_window(mui_state.ctx);
		mui_build_netplay_settings_window(mui_state.ctx);
		mui_build_netplay_status_window(mui_state.ctx);
		mui_build_lobby_window(mui_state.ctx);
		mui_build_netplay_disconnect_window(mui_state.ctx);
	}
	if (mui_tools_ui_active()){
		mui_build_tool_screenshot_window(mui_state.ctx);
		mui_build_tool_video_window(mui_state.ctx);
		mui_build_tool_mem_window(mui_state.ctx, MUI_WIN_ROM_VIEW, MAINUI_DBG_MEM_FLASH, mui_tool_mem_base_rom, FALSE);
		mui_build_tool_mem_window(mui_state.ctx, MUI_WIN_SRAM_EDIT, MAINUI_DBG_MEM_SRAM, mui_tool_mem_base_sram, TRUE);
		mui_build_tool_mem_window(mui_state.ctx, MUI_WIN_SPIRAM_EDIT, MAINUI_DBG_MEM_SPIRAM, mui_tool_mem_base_spiram, TRUE);
		mui_build_tool_mem_window(mui_state.ctx, MUI_WIN_EEPROM_EDIT, MAINUI_DBG_MEM_EEPROM, mui_tool_mem_base_eeprom, TRUE);
		mui_build_tool_inputcap_window(mui_state.ctx);
		mui_build_tool_input_trace_window(mui_state.ctx);
		mui_build_tool_serial_trace_window(mui_state.ctx);
		mui_build_tool_esp_monitor_window(mui_state.ctx);
		mui_build_remote_window(mui_state.ctx);
	}
#ifdef ENABLE_DEBUGGER
	if (mui_debugger_ui_active()){
		mui_build_debugger_window(mui_state.ctx);
	}
#endif
	mui_build_quick_window(mui_state.ctx);
	mui_build_rom_dialog(mui_state.ctx);
	mui_build_dir_dialog(mui_state.ctx);
	mui_build_cfg_file_dialog(mui_state.ctx);
#ifdef ENABLE_GUI_VKEYBOARD
	mui_build_virtual_keyboard(mui_state.ctx);
#endif
	mu_end(mui_state.ctx);
	mui_sync_all_windows(mui_state.ctx);
	mui_apply_pending_dir_change();
	mui_apply_pending_file_change();
	mui_apply_pending_rom_load();

	cmd = NULL;
	while (mu_next_command(mui_state.ctx, &cmd)){
		switch (cmd->type){
			case MU_COMMAND_TEXT:
				mui_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color);
				break;
			case MU_COMMAND_RECT:
				mui_draw_rect(cmd->rect.rect, cmd->rect.color);
				break;
			case MU_COMMAND_ICON:
				mui_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
				break;
			case MU_COMMAND_CLIP:
				mui_set_clip_rect(cmd->clip.rect);
				break;
			default:
				break;
		}
	}
	mui_gamepad_draw_cursor();
	mui_refresh_visibility();
}
