#ifndef NETPLAY_H
#define NETPLAY_H

#include "types.h"
#include "rollback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
	NETPLAY_MODE_NONE = 0,
	NETPLAY_MODE_CLIENT,
	NETPLAY_MODE_SERVER
} netplay_mode_t;

typedef enum{
	NETPLAY_ROM_SYNC_OFF = 0,
	NETPLAY_ROM_SYNC_MISSING = 1,
	NETPLAY_ROM_SYNC_MISMATCH = 2,
	NETPLAY_ROM_SYNC_ALWAYS = 3
} netplay_rom_sync_mode_t;

enum{
	NETPLAY_FEATURE_COMPAT   = 0x00000001UL,
	NETPLAY_FEATURE_IPV6     = 0x00000002UL,
	NETPLAY_FEATURE_ROM_SYNC = 0x00000004UL,
	NETPLAY_FEATURE_ROM_TCP  = 0x00000008UL
};


#define NETPLAY_LOBBY_OWNER_HOST	0U
#define NETPLAY_LOBBY_OWNER_GUEST	1U
#define NETPLAY_LOBBY_OWNER_NONE	0xFFU
#define NETPLAY_LOBBY_CHAT_LINES	24U
#define NETPLAY_LOBBY_CHAT_LINE_CHARS	96U
#define NETPLAY_LOBBY_NAME_LEN	12U
#define NETPLAY_LOBBY_PAD_LABEL_LEN	12U
#define NETPLAY_RELAY_HOST_LEN	255U
#define NETPLAY_RELAY_ROOM_CODE_LEN	8U
#define NETPLAY_RELAY_GAME_TITLE_LEN	48U
#define NETPLAY_RELAY_BROWSE_MAX	8U
#define NETPLAY_IFACE_MAX		32U
#define NETPLAY_IFACE_VALUE_LEN	64U
#define NETPLAY_IFACE_LABEL_LEN	128U
#define NETPLAY_ADDR_STR_LEN	64U
#define NETPLAY_SCOPE_STR_LEN	24U

typedef struct{
	char	room_code[NETPLAY_RELAY_ROOM_CODE_LEN + 1U];
	char	host_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	game_title[NETPLAY_RELAY_GAME_TITLE_LEN + 1U];
	auint	member_count;
	boole	is_public;
} netplay_relay_room_entry_t;

typedef struct{
	char	value[NETPLAY_IFACE_VALUE_LEN];
	char	label[NETPLAY_IFACE_LABEL_LEN];
} netplay_interface_entry_t;

typedef struct{
	boole	active;
	boole	is_host;
	boole	asset_local_ready;
	boole	asset_peer_ready;
	boole	local_ready;
	boole	peer_ready;
	boole	seat_map_valid;
	boole	can_start;
	auint	max_players;
	auint	ping_ms;
	auint	ping_smoothed_ms;
	auint	ping_jitter_ms;
	uint8	seat_owner_peer[ROLLBACK_MAX_PLAYERS];
	uint8	seat_pad_index[ROLLBACK_MAX_PLAYERS];
	boole	pending_request_valid[ROLLBACK_MAX_PLAYERS];
	uint8	pending_request_owner_peer[ROLLBACK_MAX_PLAYERS];
	uint8	pending_request_pad_index[ROLLBACK_MAX_PLAYERS];
	char	local_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	peer_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	local_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
	char	peer_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
} netplay_lobby_status_t;

typedef struct{
	boole	initialized;
	boole	relay_mode;
	boole	relay_room_ready;
	boole	relay_peer_present;
	boole	relay_direct_established;
	boole	enabled;
	boole	connected;
	boole	hello_sent;
	boole	hello_acked;
	boole	query_discovery;
	boole	compat_ok;
	boole	caps_sent;
	boole	peer_caps;
	boole	local_ready;
	boole	peer_ready;
	boole	session_started;
	boole	rom_present;
	boole	rom_sendable;
	boole	rom_transfer_needed;
	netplay_mode_t	mode;
	netplay_rom_sync_mode_t	rom_sync_mode;
	uint32	local_session;
	uint32	peer_session;
	uint32	tx_seq;
	uint32	rx_last_seq;
	boole	rx_seq_init;
	uint32	rx_dup_count;
	uint32	rx_ooo_count;
	uint32	rx_gap_count;
	auint	local_port;
	auint	peer_port;
	boole	socket_ipv6;
	boole	peer_addr_ipv6;
	boole	relay_addr_ipv6;
	auint	local_player;
	auint	peer_player;
	uint32	local_player_mask;
	uint32	peer_player_mask;
	auint	max_players;
	boole	lobby_active;
	boole	lobby_is_host;
	boole	lobby_local_ready;
	boole	lobby_peer_ready;
	boole	lobby_seat_map_valid;
	auint	ping_ms;
	auint	ping_smoothed_ms;
	auint	ping_jitter_ms;
	uint8	lobby_seat_owner_peer[ROLLBACK_MAX_PLAYERS];
	uint8	lobby_seat_pad_index[ROLLBACK_MAX_PLAYERS];
	uint32	rom_crc;
	uint32	peer_rom_crc;
	uint32	rom_size;
	uint32	peer_rom_size;
	uint32	build_id;
	uint32	peer_build_id;
	uint32	feature_flags;
	uint32	peer_feature_flags;
	auint	rom_tcp_port;
	auint	peer_rom_tcp_port;
	char	rom_name[64];
	char	peer_rom_name[64];
	char	peer_host[256];
	auint	relay_server_port;
	char	relay_server_host[NETPLAY_RELAY_HOST_LEN + 1U];
	char	relay_room_code[NETPLAY_RELAY_ROOM_CODE_LEN + 1U];
	char	network_interface[NETPLAY_IFACE_VALUE_LEN];
	char	network_interface_label[NETPLAY_IFACE_LABEL_LEN];
	char	local_ipv4[NETPLAY_ADDR_STR_LEN];
	char	local_ipv6[NETPLAY_ADDR_STR_LEN];
	char	local_ipv4_scope[NETPLAY_SCOPE_STR_LEN];
	char	local_ipv6_scope[NETPLAY_SCOPE_STR_LEN];
	char	active_local_addr[NETPLAY_ADDR_STR_LEN];
	char	active_local_scope[NETPLAY_SCOPE_STR_LEN];
	char	notice[128];
	char	local_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	peer_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	local_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
	char	peer_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
	char	compat_reason[128];
} netplay_status_t;

typedef struct{
	auint	local_player;
	uint32	local_player_mask;
	auint	max_players;
	uint32	feature_flags;
	uint32	build_id;
	boole	rom_send_enabled;
	boole	rom_receive_enabled;
	netplay_rom_sync_mode_t	rom_sync_mode;
	auint	rom_tcp_port;
	uint32	rom_max_size;
	auint	relay_server_port;
	boole	relay_allow_direct;
	boole	relay_allow_relay;
	char	relay_server_host[NETPLAY_RELAY_HOST_LEN + 1U];
	char	network_interface[NETPLAY_IFACE_VALUE_LEN];
	char	local_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	local_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
} netplay_config_t;

void	netplay_init(void);
void	netplay_shutdown(void);
void	netplay_reset(void);

void	netplay_set_local_identity(uint32 rom_crc, uint32 build_id, uint32 feature_flags, auint local_player, auint max_players);
void	netplay_get_local_identity(uint32 *rom_crc, uint32 *build_id, uint32 *feature_flags, auint *local_player, auint *max_players);
void	netplay_get_config(netplay_config_t *cfg);
void	netplay_set_config(netplay_config_t const *cfg);
void	netplay_set_rom_sync_mode(netplay_rom_sync_mode_t mode);
netplay_rom_sync_mode_t netplay_get_rom_sync_mode(void);

boole	netplay_open_client(char const *host, auint port, auint local_port);
boole	netplay_open_server(auint local_port);
boole	netplay_open_relay_host(char const *relay_host, auint relay_port, auint local_port, char const *room_code, boole public_room, char const *game_title);
boole	netplay_open_relay_join(char const *relay_host, auint relay_port, char const *room_code, auint local_port);
boole	netplay_relay_fetch_public_rooms(char const *relay_host, auint relay_port, netplay_relay_room_entry_t *entries, auint max_entries, auint *out_count, char *status, auint status_size);
auint	netplay_refresh_interfaces(void);
auint	netplay_get_interface_count(void);
char const*	netplay_get_interface_label(auint idx);
char const*	netplay_get_interface_value(auint idx);
auint	netplay_find_interface_value(char const *value);
void	netplay_disconnect(void);
void	netplay_poll(void);

boole	netplay_send_input(auint frame, auint player, auint buttons);
boole	netplay_send_device_event(auint frame, rollback_device_event_t const *ev);
boole	netplay_is_connected(void);
void	netplay_get_status(netplay_status_t *status);

void	netplay_lobby_get_status(netplay_lobby_status_t *status);
boole	netplay_lobby_set_seat(auint seat, auint owner_peer, auint pad_index);
boole	netplay_lobby_request_seat(auint seat, auint owner_peer, auint pad_index);
boole	netplay_lobby_approve_request(auint seat, boole approve);
void	netplay_lobby_set_ready(boole ready);
boole	netplay_lobby_start_match(void);
boole	netplay_lobby_send_chat(char const *text);
auint	netplay_lobby_get_chat_count(void);
boole	netplay_lobby_get_chat_line(auint index, char *dst, auint dst_size);

#ifdef __cplusplus
}
#endif

#endif
