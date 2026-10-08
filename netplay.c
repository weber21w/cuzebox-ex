#include "netplay.h"

#include "rollback.h"
#include "netplay_host.h"
#include "mainui.h"

#if !defined(ENABLE_NETPLAY)

#include <string.h>

static netplay_config_t netplay_cfg_stub = {
	1U,
	1U,
	2U,
	0U,
	0U,
	FALSE,
	FALSE,
	NETPLAY_ROM_SYNC_OFF,
	0U,
	0U,
	0U,
	FALSE,
	TRUE,
	"",
	"",
	"Player",
	{ "Pad 1", "Pad 2", "Pad 3", "Pad 4" }
};

void netplay_init(void){}
void netplay_shutdown(void){}
void netplay_reset(void){}

void netplay_set_local_identity(uint32 rom_crc, uint32 build_id, uint32 feature_flags, auint local_player, auint max_players)
{
	(void)rom_crc;
	(void)build_id;
	(void)feature_flags;
	if (local_player != 0U){
		netplay_cfg_stub.local_player = local_player;
	}
	if (max_players != 0U){
		netplay_cfg_stub.max_players = max_players;
	}
}

void netplay_get_local_identity(uint32 *rom_crc, uint32 *build_id, uint32 *feature_flags, auint *local_player, auint *max_players)
{
	if (rom_crc != NULL){ *rom_crc = 0U; }
	if (build_id != NULL){ *build_id = 0U; }
	if (feature_flags != NULL){ *feature_flags = netplay_cfg_stub.feature_flags; }
	if (local_player != NULL){ *local_player = netplay_cfg_stub.local_player; }
	if (max_players != NULL){ *max_players = netplay_cfg_stub.max_players; }
}

void netplay_get_config(netplay_config_t *cfg)
{
	if (cfg != NULL){
		*cfg = netplay_cfg_stub;
	}
}

void netplay_set_config(netplay_config_t const *cfg)
{
	if (cfg != NULL){
		netplay_cfg_stub = *cfg;
	}
}

void netplay_set_rom_sync_mode(netplay_rom_sync_mode_t mode)
{
	netplay_cfg_stub.rom_sync_mode = mode;
}

netplay_rom_sync_mode_t netplay_get_rom_sync_mode(void)
{
	return netplay_cfg_stub.rom_sync_mode;
}

boole netplay_open_client(char const *host, auint port, auint local_port)
{
	(void)host;
	(void)port;
	(void)local_port;
	return FALSE;
}

boole netplay_open_server(auint local_port)
{
	(void)local_port;
	return FALSE;
}

boole netplay_open_relay_host(char const *relay_host, auint relay_port, auint local_port, char const *room_code, boole public_room, char const *game_title)
{
	(void)relay_host;
	(void)relay_port;
	(void)local_port;
	(void)room_code;
	(void)public_room;
	(void)game_title;
	return FALSE;
}

boole netplay_open_relay_join(char const *relay_host, auint relay_port, char const *room_code, auint local_port)
{
	(void)relay_host;
	(void)relay_port;
	(void)room_code;
	(void)local_port;
	return FALSE;
}

boole netplay_relay_fetch_public_rooms(char const *relay_host, auint relay_port, netplay_relay_room_entry_t *entries, auint max_entries, auint *out_count, char *status, auint status_size)
{
	(void)relay_host;
	(void)relay_port;
	(void)entries;
	(void)max_entries;
	if (out_count != NULL){ *out_count = 0U; }
	if ((status != NULL) && (status_size != 0U)){
		strncpy(status, "Netplay disabled at build time", status_size - 1U);
		status[status_size - 1U] = 0;
	}
	return FALSE;
}

void netplay_disconnect(void){}
void netplay_poll(void){}

boole netplay_send_input(auint frame, auint player, auint buttons)
{
	(void)frame;
	(void)player;
	(void)buttons;
	return FALSE;
}

boole netplay_send_device_event(auint frame, rollback_device_event_t const *ev)
{
	(void)frame;
	(void)ev;
	return FALSE;
}

boole netplay_is_connected(void)
{
	return FALSE;
}

void netplay_get_status(netplay_status_t *status)
{
	if (status != NULL){
		memset(status, 0, sizeof(*status));
		status->compat_ok = FALSE;
		strncpy(status->compat_reason, "Netplay disabled at build time", sizeof(status->compat_reason) - 1U);
		status->compat_reason[sizeof(status->compat_reason) - 1U] = 0;
	}
}

void netplay_lobby_get_status(netplay_lobby_status_t *status)
{
	if (status != NULL){
		memset(status, 0, sizeof(*status));
		strncpy(status->local_name, netplay_cfg_stub.local_name, sizeof(status->local_name) - 1U);
		status->local_name[sizeof(status->local_name) - 1U] = 0;
		memcpy(status->local_pad_labels, netplay_cfg_stub.local_pad_labels, sizeof(status->local_pad_labels));
	}
}

boole netplay_lobby_set_seat(auint seat, auint owner_peer, auint pad_index)
{
	(void)seat;
	(void)owner_peer;
	(void)pad_index;
	return FALSE;
}

boole netplay_lobby_request_seat(auint seat, auint owner_peer, auint pad_index)
{
	(void)seat;
	(void)owner_peer;
	(void)pad_index;
	return FALSE;
}

boole netplay_lobby_approve_request(auint seat, boole approve)
{
	(void)seat;
	(void)approve;
	return FALSE;
}

void netplay_lobby_set_ready(boole ready)
{
	(void)ready;
}

boole netplay_lobby_start_match(void)
{
	return FALSE;
}

boole netplay_lobby_send_chat(char const *text)
{
	(void)text;
	return FALSE;
}

auint netplay_lobby_get_chat_count(void)
{
	return 0U;
}

boole netplay_lobby_get_chat_line(auint index, char *dst, auint dst_size)
{
	(void)index;
	if ((dst != NULL) && (dst_size != 0U)){
		dst[0] = 0;
	}
	return FALSE;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "uzenet_relay_protocol.h"
#if !defined(WIN32) && !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MINGW32__)
#include <sys/time.h>
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#define NETPLAY_CLOSESOCK closesocket
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define NETPLAY_CLOSESOCK close
typedef int SOCKET;
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
static int netplay_inet_pton_compat(int af, char const *src, void *dst)
{
	struct sockaddr_storage ss;
	int sslen = (int)sizeof(ss);
	char tmp[128];
	if ((src == NULL) || (dst == NULL)){ return -1; }
	memset(&ss, 0, sizeof(ss));
	strncpy(tmp, src, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;
	if (WSAStringToAddressA(tmp, af, NULL, (LPSOCKADDR)&ss, &sslen) != 0){
		return 0;
	}
	if (af == AF_INET){
		memcpy(dst, &((struct sockaddr_in*)&ss)->sin_addr, sizeof(struct in_addr));
		return 1;
	}else if (af == AF_INET6){
		memcpy(dst, &((struct sockaddr_in6*)&ss)->sin6_addr, sizeof(struct in6_addr));
		return 1;
	}
	return -1;
}

static char const* netplay_inet_ntop_compat(int af, void const *src, char *dst, size_t dsz)
{
	struct sockaddr_storage ss;
	if ((src == NULL) || (dst == NULL) || (dsz == 0U)){ return NULL; }
	memset(&ss, 0, sizeof(ss));
	if (af == AF_INET){
		struct sockaddr_in *a4 = (struct sockaddr_in*)&ss;
		a4->sin_family = AF_INET;
		memcpy(&a4->sin_addr, src, sizeof(a4->sin_addr));
		if (getnameinfo((struct sockaddr*)a4, (socklen_t)sizeof(*a4), dst, (DWORD)dsz, NULL, 0, NI_NUMERICHOST) != 0){ return NULL; }
	}else if (af == AF_INET6){
		struct sockaddr_in6 *a6 = (struct sockaddr_in6*)&ss;
		a6->sin6_family = AF_INET6;
		memcpy(&a6->sin6_addr, src, sizeof(a6->sin6_addr));
		if (getnameinfo((struct sockaddr*)a6, (socklen_t)sizeof(*a6), dst, (DWORD)dsz, NULL, 0, NI_NUMERICHOST) != 0){ return NULL; }
	}else{
		return NULL;
	}
	return dst;
}
#else
#define netplay_inet_pton_compat inet_pton
#define netplay_inet_ntop_compat inet_ntop
#endif

typedef struct{
	uint32	magic;
	uint32	session;
	uint32	frame;
	uint32	buttons;
	uint32	seq;
	uint32	rom_crc;
	uint32	rom_size;
	uint32	build_id;
	uint32	feature_flags;
	uint32	aux0;
	uint16	max_players;
	uint16	aux1;
	uint8	version;
	uint8	type;
	uint8	player;
	uint8	flags;
	uint8	sync_mode;
	uint8	result;
	uint16	transfer_port;
	char	rom_name[64];
} netplay_pkt_t;

typedef struct{
	uint32	frame;
	uint32	buttons;
} netplay_input_hist_pkt_t;

typedef struct{
	boole	valid;
	uint32	frame;
	uint32	buttons;
} netplay_input_hist_t;

typedef struct{
	uint32	frame;
	uint16	seq;
	uint8	port;
	uint8	slot;
	uint8	type;
	uint8	len;
	uint8	data[ROLLBACK_DEVICE_EVENT_DATA_MAX];
} netplay_dev_hist_pkt_t;

typedef struct{
	boole	valid;
	uint32	frame;
	rollback_device_event_t	ev;
} netplay_dev_hist_t;

typedef struct{
	uint32	magic;
	uint16	version;
	uint16	reserved;
	uint32	rom_size;
	uint32	rom_crc;
	char	rom_name[64];
} netplay_rom_tcp_header_t;

typedef struct{
	boole	configured;
	uint32	rom_crc;
	uint32	rom_size;
	uint32	build_id;
	uint32	feature_flags;
	auint	local_player;
	auint	max_players;
	boole	rom_present;
	boole	rom_sendable;
	uint32	local_player_mask;
	netplay_rom_sync_mode_t	rom_sync_mode;
	boole	rom_send_enabled;
	boole	rom_receive_enabled;
	auint	rom_tcp_port;
	uint32	rom_max_size;
	auint	relay_server_port;
	boole	relay_allow_direct;
	boole	relay_allow_relay;
	char	relay_server_host[NETPLAY_RELAY_HOST_LEN + 1U];
	char	network_interface[NETPLAY_IFACE_VALUE_LEN];
	char	local_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	local_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
	char	rom_name[64];
} netplay_cfg_t;

typedef struct{
	boole	initialized;
	boole	enabled;
	boole	connected;
	boole	hello_sent;
	boole	hello_acked;
	boole	compat_ok;
	boole	caps_sent;
	boole	peer_caps;
	boole	local_ready;
	boole	peer_ready;
	boole	session_started;
	boole	rom_request_sent;
	boole	rom_transfer_needed;
	boole	relay_mode;
	boole	relay_room_ready;
	boole	relay_peer_present;
	boole	relay_direct_candidate;
	boole	relay_direct_established;
	boole	relay_punch_ok_sent;
	boole	relay_peer_direct_allowed;
	boole	relay_peer_relay_allowed;
	uint8	relay_pending_req_type;
	netplay_mode_t	mode;
	SOCKET	sock;
	int		sock_family;
	struct sockaddr_storage	peer_addr;
	socklen_t	peer_addr_len;
	struct sockaddr_storage	relay_addr;
	socklen_t	relay_addr_len;
	boole	peer_valid;
	uint32	local_session;
	uint32	peer_session;
	uint32	relay_session_id;
	uint32	relay_room_id;
	uint32	relay_tx_seq;
	uint32	tx_seq;
	uint32	rx_last_seq;
	boole	rx_seq_init;
	uint32	rx_dup_count;
	uint32	rx_ooo_count;
	uint32	rx_gap_count;
	auint	local_port;
	auint	peer_port;
	char	peer_host[256];
	char	compat_reason[128];
	uint32	peer_rom_crc;
	uint32	peer_rom_size;
	uint32	peer_build_id;
	uint32	peer_feature_flags;
	auint	peer_player;
	uint32	peer_player_mask;
	auint	peer_max_players;
	boole	peer_rom_present;
	boole	peer_rom_sendable;
	netplay_rom_sync_mode_t	peer_sync_mode;
	auint	peer_transfer_port;
	char	peer_rom_name[64];
	SOCKET	rom_listen_sock;
	auint	rom_listen_port;
	boole	lobby_local_ready;
	boole	lobby_peer_ready;
	boole	lobby_valid;
	boole	lobby_dirty;
	uint8	self_peer_id;
	uint8	lobby_owner_peer[ROLLBACK_MAX_PLAYERS];
	uint8	lobby_pad_index[ROLLBACK_MAX_PLAYERS];
	boole	lobby_request_valid[ROLLBACK_MAX_PLAYERS];
	uint8	lobby_request_owner_peer[ROLLBACK_MAX_PLAYERS];
	uint8	lobby_request_pad_index[ROLLBACK_MAX_PLAYERS];
	char	peer_name[NETPLAY_LOBBY_NAME_LEN + 1U];
	char	peer_pad_labels[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U];
	boole	lobby_labels_dirty;
	boole	lobby_labels_sent;
	uint32	last_labels_send_ms;
	uint32	ping_token;
	uint32	ping_sent_ms;
	uint32	ping_ms;
	uint32	ping_smoothed_ms;
	uint32	ping_jitter_ms;
	uint32	last_ping_send_ms;
	uint32	last_lobby_send_ms;
	uint32	last_rx_ms;
	uint32	last_hello_send_ms;
	uint32	relay_last_heartbeat_ms;
	uint32	relay_last_req_send_ms;
	uint32	relay_last_punch_send_ms;
	char	last_notice[128];
	char	relay_server_host[NETPLAY_RELAY_HOST_LEN + 1U];
	auint	relay_server_port;
	char	relay_room_code[NETPLAY_RELAY_ROOM_CODE_LEN + 1U];
	boole	relay_room_public;
	char	relay_game_title[NETPLAY_RELAY_GAME_TITLE_LEN + 1U];
#define NETPLAY_DEVICE_HISTORY_SEND	8U
	netplay_input_hist_t	local_input_hist[ROLLBACK_MAX_PLAYERS][4U];
	auint	local_input_hist_head[ROLLBACK_MAX_PLAYERS];
	netplay_dev_hist_t	local_dev_hist[NETPLAY_DEVICE_HISTORY_SEND];
	auint	local_dev_hist_head;
	auint	last_dev_hist_send_frame;
	char	chat_lines[NETPLAY_LOBBY_CHAT_LINES][NETPLAY_LOBBY_CHAT_LINE_CHARS];
	auint	chat_head;
	auint	chat_count;
} netplay_state_t;

#define NETPLAY_MAGIC	0x435a5242UL	/* CZRB */
#define NETPLAY_VERSION	7U

#define NETPLAY_TYPE_HELLO		1U
#define NETPLAY_TYPE_HELLO_ACK		2U
#define NETPLAY_TYPE_INPUT		3U
#define NETPLAY_TYPE_CAPS		4U
#define NETPLAY_TYPE_ROM_REQUEST	5U
#define NETPLAY_TYPE_ROM_OFFER		6U
#define NETPLAY_TYPE_ROM_READY		7U
#define NETPLAY_TYPE_SESSION_START	8U
#define NETPLAY_TYPE_ROM_ERROR		9U
#define NETPLAY_TYPE_LOBBY_STATE	10U
#define NETPLAY_TYPE_LOBBY_READY	11U
#define NETPLAY_TYPE_LOBBY_CHAT	12U
#define NETPLAY_TYPE_PING		13U
#define NETPLAY_TYPE_PONG		14U
#define NETPLAY_TYPE_LOBBY_REQUEST	15U
#define NETPLAY_TYPE_LOBBY_LABELS	16U
#define NETPLAY_TYPE_DISCONNECT	17U
#define NETPLAY_TYPE_DEVICE_EVENT	18U
#define NETPLAY_TYPE_DEVICE_EVENT_HISTORY	19U

#define NETPLAY_PKT_FLAG_ROM_PRESENT	0x01U
#define NETPLAY_PKT_FLAG_ROM_SENDABLE	0x02U

#define NETPLAY_ROM_TCP_MAGIC	0x43524f4dUL	/* CROM */
#define NETPLAY_ROM_TCP_VERSION	1U
#define NETPLAY_PING_INTERVAL_MS	1000U
#define NETPLAY_LOBBY_BROADCAST_MS	500U
#define NETPLAY_LABEL_FIELD_COUNT	(1U + ROLLBACK_MAX_PLAYERS)
#define NETPLAY_LABEL_FIELD_SIZE	12U
#define NETPLAY_LABEL_SEND_MS	500U
#define NETPLAY_CONNECT_TIMEOUT_MS	5000U
#define NETPLAY_PEER_TIMEOUT_MS	5000U
#define NETPLAY_HELLO_RETRY_MS	750U
#define NETPLAY_RELAY_REQ_RETRY_MS	1000U
#define NETPLAY_RELAY_HEARTBEAT_MS	3000U
#define NETPLAY_RELAY_SETUP_TIMEOUT_MS	10000U
#define NETPLAY_RELAY_PUNCH_RETRY_MS	250U
#define NETPLAY_INPUT_HISTORY_SEND	4U
#define NETPLAY_INPUT_HISTORY_MAX	((auint)(sizeof(((netplay_pkt_t*)0)->rom_name) / sizeof(netplay_input_hist_pkt_t)))
#define NETPLAY_DEVICE_HISTORY_MAX	((auint)(sizeof(((netplay_pkt_t*)0)->rom_name) / sizeof(netplay_dev_hist_pkt_t)))
#define NETPLAY_UZRL_PEER_INFO_LEGACY_SIZE	28U

static netplay_cfg_t   netplay_cfg = { FALSE, 0U, 0U, 0U, (NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP), 1U, 2U, FALSE, FALSE, 0x00000001UL, NETPLAY_ROM_SYNC_MISMATCH, TRUE, TRUE, 0U, (4U * 1024U * 1024U), 43810U, TRUE, TRUE, "uzenet.us", "", "", { "", "", "", "" } };
static netplay_state_t netplay_state;
static void netplay_init_pkt(netplay_pkt_t *pkt, uint8 type);
static boole netplay_send_raw(netplay_pkt_t const *pkt);
static boole netplay_send_chat_pkt(char const *text);
static boole netplay_send_lobby_labels_pkt(void);
static boole netplay_send_name_override_pkt(char const *name);
static boole netplay_send_disconnect_pkt(void);
static void netplay_drop_connection(char const *reason);
static void netplay_record_local_input(auint player, auint frame, auint buttons);
static void netplay_record_local_device_event(auint frame, rollback_device_event_t const *ev);
static auint netplay_pack_local_input_history(auint player, auint frame, netplay_pkt_t *pkt);
static auint netplay_pack_local_device_history(auint frame, netplay_pkt_t *pkt);
static void netplay_handle_input_history(netplay_pkt_t const *pkt);
static void netplay_handle_device_history(netplay_pkt_t const *pkt);
static boole netplay_send_device_history(auint frame);
static uint32 netplay_pack_u32_from_bytes(uint8 const *src);
static void netplay_unpack_u32_to_bytes(uint32 value, uint8 *dst);
static boole netplay_send_relay_control(uint8 type, uint8 flags, void const *payload, auint payload_len);
static boole netplay_send_relay_request(uint8 type, char const *room_code);
static boole netplay_send_relay_heartbeat(void);
static boole netplay_send_relay_leave(void);
static boole netplay_send_direct_raw(netplay_pkt_t const *pkt);
static boole netplay_send_direct_probe(void);
static void netplay_mark_direct_established(void);
static void netplay_handle_relay_packet(uint8 const *buf, auint got, struct sockaddr const *from, socklen_t from_len);
static boole netplay_decode_relay_peer_endpoint(void const *payload, auint plen, struct sockaddr_storage *out_addr, socklen_t *out_len, boole *direct_ok);
static boole netplay_addr_is_v4mapped(struct in6_addr const *a6);

typedef struct{
	char	value[NETPLAY_IFACE_VALUE_LEN];
	char	label[NETPLAY_IFACE_LABEL_LEN];
	char	scope[NETPLAY_SCOPE_STR_LEN];
	int	family;
} netplay_iface_cache_entry_t;

static netplay_iface_cache_entry_t netplay_iface_cache[NETPLAY_IFACE_MAX];
static auint netplay_iface_cache_count = 0U;
static char netplay_default_ipv4[NETPLAY_ADDR_STR_LEN];
static char netplay_default_ipv6[NETPLAY_ADDR_STR_LEN];
static char netplay_default_ipv4_scope[NETPLAY_SCOPE_STR_LEN];
static char netplay_default_ipv6_scope[NETPLAY_SCOPE_STR_LEN];
static char netplay_route_local_addr[NETPLAY_ADDR_STR_LEN];
static char netplay_route_local_scope[NETPLAY_SCOPE_STR_LEN];

static void netplay_copy_str(char *dst, auint dsz, char const *src)
{
	if ((dst == NULL) || (dsz == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, dsz - 1U);
	dst[dsz - 1U] = 0;
}

static boole netplay_scope_ipv4(struct in_addr const *a4, char *dst, auint dsz)
{
	uint32 v;
	if ((a4 == NULL) || (dst == NULL) || (dsz == 0U)){ return FALSE; }
	v = ntohl(a4->s_addr);
	if ((v & 0xFF000000UL) == 0x7F000000UL){ netplay_copy_str(dst, dsz, "loopback"); return TRUE; }
	if ((v & 0xFFFF0000UL) == 0xA9FE0000UL){ netplay_copy_str(dst, dsz, "link-local"); return TRUE; }
	if ((v & 0xFF000000UL) == 0x0A000000UL){ netplay_copy_str(dst, dsz, "private"); return TRUE; }
	if ((v & 0xFFF00000UL) == 0xAC100000UL){ netplay_copy_str(dst, dsz, "private"); return TRUE; }
	if ((v & 0xFFFF0000UL) == 0xC0A80000UL){ netplay_copy_str(dst, dsz, "private"); return TRUE; }
	if ((v & 0xFFC00000UL) == 0x64400000UL){ netplay_copy_str(dst, dsz, "CGNAT"); return TRUE; }
	netplay_copy_str(dst, dsz, "public");
	return TRUE;
}

static boole netplay_scope_ipv6(struct in6_addr const *a6, char *dst, auint dsz)
{
	if ((a6 == NULL) || (dst == NULL) || (dsz == 0U)){ return FALSE; }
	if (IN6_IS_ADDR_LOOPBACK(a6)){ netplay_copy_str(dst, dsz, "loopback"); return TRUE; }
	if (IN6_IS_ADDR_LINKLOCAL(a6)){ netplay_copy_str(dst, dsz, "link-local"); return TRUE; }
	if (((a6->s6_addr[0] & 0xFEU) == 0xFCU)){ netplay_copy_str(dst, dsz, "ULA"); return TRUE; }
	netplay_copy_str(dst, dsz, "global");
	return TRUE;
}

static boole netplay_parse_ip_literal(char const *text, int *out_family, struct sockaddr_storage *out, socklen_t *out_len, char *out_scope, auint out_scope_sz)
{
	struct sockaddr_in a4;
	struct sockaddr_in6 a6;
	if ((text == NULL) || (text[0] == 0)){ return FALSE; }
	memset(&a4, 0, sizeof(a4));
	if (netplay_inet_pton_compat(AF_INET, text, &a4.sin_addr) == 1){
		a4.sin_family = AF_INET;
		if (out_family != NULL){ *out_family = AF_INET; }
		if ((out != NULL) && (out_len != NULL)){ memcpy(out, &a4, sizeof(a4)); *out_len = (socklen_t)sizeof(a4); }
		if (out_scope != NULL){ (void)netplay_scope_ipv4(&a4.sin_addr, out_scope, out_scope_sz); }
		return TRUE;
	}
	memset(&a6, 0, sizeof(a6));
	if (netplay_inet_pton_compat(AF_INET6, text, &a6.sin6_addr) == 1){
		a6.sin6_family = AF_INET6;
		if (out_family != NULL){ *out_family = AF_INET6; }
		if ((out != NULL) && (out_len != NULL)){ memcpy(out, &a6, sizeof(a6)); *out_len = (socklen_t)sizeof(a6); }
		if (out_scope != NULL){ (void)netplay_scope_ipv6(&a6.sin6_addr, out_scope, out_scope_sz); }
		return TRUE;
	}
	return FALSE;
}

static boole netplay_addr_to_string(struct sockaddr const *sa, char *dst, auint dsz, char *scope, auint ssz)
{
	if ((sa == NULL) || (dst == NULL) || (dsz == 0U)){ return FALSE; }
	dst[0] = 0;
	if ((scope != NULL) && (ssz != 0U)){ scope[0] = 0; }
	if (sa->sa_family == AF_INET){
		struct sockaddr_in const *a4 = (struct sockaddr_in const*)sa;
		if (netplay_inet_ntop_compat(AF_INET, &a4->sin_addr, dst, dsz) == NULL){ return FALSE; }
		if (scope != NULL){ (void)netplay_scope_ipv4(&a4->sin_addr, scope, ssz); }
		return TRUE;
	}else if (sa->sa_family == AF_INET6){
		struct sockaddr_in6 const *a6 = (struct sockaddr_in6 const*)sa;
		if (netplay_inet_ntop_compat(AF_INET6, &a6->sin6_addr, dst, dsz) == NULL){ return FALSE; }
		if (scope != NULL){ (void)netplay_scope_ipv6(&a6->sin6_addr, scope, ssz); }
		return TRUE;
	}
	return FALSE;
}

static boole netplay_iface_cache_add(char const *value, char const *label, char const *scope, int family)
{
	auint i;
	if ((value == NULL) || (value[0] == 0) || (label == NULL)){ return FALSE; }
	for (i = 0U; i < netplay_iface_cache_count; ++i){
		if (strcmp(netplay_iface_cache[i].value, value) == 0){ return FALSE; }
	}
	if (netplay_iface_cache_count >= NETPLAY_IFACE_MAX){ return FALSE; }
	netplay_copy_str(netplay_iface_cache[netplay_iface_cache_count].value, sizeof(netplay_iface_cache[netplay_iface_cache_count].value), value);
	netplay_copy_str(netplay_iface_cache[netplay_iface_cache_count].label, sizeof(netplay_iface_cache[netplay_iface_cache_count].label), label);
	netplay_copy_str(netplay_iface_cache[netplay_iface_cache_count].scope, sizeof(netplay_iface_cache[netplay_iface_cache_count].scope), scope);
	netplay_iface_cache[netplay_iface_cache_count].family = family;
	netplay_iface_cache_count++;
	return TRUE;
}

static void netplay_clear_interface_cache(void)
{
	netplay_iface_cache_count = 0U;
	netplay_default_ipv4[0] = 0;
	netplay_default_ipv6[0] = 0;
	netplay_default_ipv4_scope[0] = 0;
	netplay_default_ipv6_scope[0] = 0;
}

static void netplay_set_default_addr_from_entry(int family, char const *value, char const *scope)
{
	if (family == AF_INET){
		if (netplay_default_ipv4[0] == 0){
			netplay_copy_str(netplay_default_ipv4, sizeof(netplay_default_ipv4), value);
			netplay_copy_str(netplay_default_ipv4_scope, sizeof(netplay_default_ipv4_scope), scope);
		}
	}else if (family == AF_INET6){
		if (netplay_default_ipv6[0] == 0){
			netplay_copy_str(netplay_default_ipv6, sizeof(netplay_default_ipv6), value);
			netplay_copy_str(netplay_default_ipv6_scope, sizeof(netplay_default_ipv6_scope), scope);
		}
	}
}

static void netplay_set_default_addr_preferred(int family, char const *value, char const *scope)
{
	if ((scope == NULL) || (scope[0] == 0)){
		netplay_set_default_addr_from_entry(family, value, scope);
		return;
	}
	if ((strcmp(scope, "loopback") == 0) || (strcmp(scope, "link-local") == 0)){
		if (((family == AF_INET) && (netplay_default_ipv4[0] == 0)) || ((family == AF_INET6) && (netplay_default_ipv6[0] == 0))){
			netplay_set_default_addr_from_entry(family, value, scope);
		}
		return;
	}
	if (family == AF_INET){
		netplay_copy_str(netplay_default_ipv4, sizeof(netplay_default_ipv4), value);
		netplay_copy_str(netplay_default_ipv4_scope, sizeof(netplay_default_ipv4_scope), scope);
	}else if (family == AF_INET6){
		netplay_copy_str(netplay_default_ipv6, sizeof(netplay_default_ipv6), value);
		netplay_copy_str(netplay_default_ipv6_scope, sizeof(netplay_default_ipv6_scope), scope);
	}
}

auint netplay_refresh_interfaces(void)
{
	netplay_clear_interface_cache();
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		ULONG size = 0UL;
		DWORD rv;
		IP_ADAPTER_ADDRESSES *aa = NULL;
		rv = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &size);
		if (rv == ERROR_BUFFER_OVERFLOW){
			aa = (IP_ADAPTER_ADDRESSES*)malloc((size_t)size);
			if (aa != NULL){
				rv = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &size);
				if (rv == NO_ERROR){
					IP_ADAPTER_ADDRESSES *ad;
					for (ad = aa; ad != NULL; ad = ad->Next){
						IP_ADAPTER_UNICAST_ADDRESS *ua;
						char ifname[96];
						ifname[0] = 0;
						if (ad->FriendlyName != NULL){
							int wrv = WideCharToMultiByte(CP_UTF8, 0, ad->FriendlyName, -1, ifname, (int)sizeof(ifname), NULL, NULL);
							if ((wrv <= 0) || (ifname[0] == 0)){
								ifname[0] = 0;
							}
						}
						if (ifname[0] == 0){
							netplay_copy_str(ifname, sizeof(ifname), ad->AdapterName != NULL ? ad->AdapterName : "interface");
						}
						for (ua = ad->FirstUnicastAddress; ua != NULL; ua = ua->Next){
							char addr[NETPLAY_ADDR_STR_LEN];
							char scope[NETPLAY_SCOPE_STR_LEN];
							char label[NETPLAY_IFACE_LABEL_LEN];
							if ((ua->Address.lpSockaddr == NULL) || (!netplay_addr_to_string(ua->Address.lpSockaddr, addr, sizeof(addr), scope, sizeof(scope)))){
								continue;
							}
							if (scope[0] != 0){
								snprintf(label, sizeof(label), "%s - %s (%s)", ifname, addr, scope);
							}else{
								snprintf(label, sizeof(label), "%s - %s", ifname, addr);
							}
							if (netplay_iface_cache_add(addr, label, scope, ua->Address.lpSockaddr->sa_family)){
								netplay_set_default_addr_preferred(ua->Address.lpSockaddr->sa_family, addr, scope);
							}
						}
					}
				}
				free(aa);
			}
		}
	}
#else
	{
		struct ifaddrs *ifa = NULL;
		struct ifaddrs *it;
		if (getifaddrs(&ifa) == 0){
			for (it = ifa; it != NULL; it = it->ifa_next){
				char addr[NETPLAY_ADDR_STR_LEN];
				char scope[NETPLAY_SCOPE_STR_LEN];
				char label[NETPLAY_IFACE_LABEL_LEN];
				if ((it->ifa_addr == NULL) || ((it->ifa_addr->sa_family != AF_INET) && (it->ifa_addr->sa_family != AF_INET6))){
					continue;
				}
				if (!netplay_addr_to_string(it->ifa_addr, addr, sizeof(addr), scope, sizeof(scope))){
					continue;
				}
				if (scope[0] != 0){
					snprintf(label, sizeof(label), "%s - %s (%s)", it->ifa_name != NULL ? it->ifa_name : "interface", addr, scope);
				}else{
					snprintf(label, sizeof(label), "%s - %s", it->ifa_name != NULL ? it->ifa_name : "interface", addr);
				}
				if (netplay_iface_cache_add(addr, label, scope, it->ifa_addr->sa_family)){
					netplay_set_default_addr_preferred(it->ifa_addr->sa_family, addr, scope);
				}
			}
			freeifaddrs(ifa);
		}
	}
#endif
	return netplay_iface_cache_count;
}

auint netplay_get_interface_count(void)
{
	return netplay_iface_cache_count;
}

char const* netplay_get_interface_label(auint idx)
{
	if (idx >= netplay_iface_cache_count){ return ""; }
	return netplay_iface_cache[idx].label;
}

char const* netplay_get_interface_value(auint idx)
{
	if (idx >= netplay_iface_cache_count){ return ""; }
	return netplay_iface_cache[idx].value;
}

auint netplay_find_interface_value(char const *value)
{
	auint i;
	if ((value == NULL) || (value[0] == 0)){ return 0U; }
	for (i = 0U; i < netplay_iface_cache_count; ++i){
		if (strcmp(netplay_iface_cache[i].value, value) == 0){ return i + 1U; }
	}
	return 0U;
}

static void netplay_update_route_local_addr(struct sockaddr const *dest, socklen_t dest_len, int family, char const *manual_bind)
{
	SOCKET sock;
	struct sockaddr_storage local;
	socklen_t local_len = (socklen_t)sizeof(local);
	char scope[NETPLAY_SCOPE_STR_LEN];
	netplay_route_local_addr[0] = 0;
	netplay_route_local_scope[0] = 0;
	if ((manual_bind != NULL) && (manual_bind[0] != 0)){
		(void)netplay_parse_ip_literal(manual_bind, NULL, NULL, NULL, scope, sizeof(scope));
		netplay_copy_str(netplay_route_local_addr, sizeof(netplay_route_local_addr), manual_bind);
		netplay_copy_str(netplay_route_local_scope, sizeof(netplay_route_local_scope), scope);
		return;
	}
	if ((dest == NULL) || (dest_len == 0U) || ((family != AF_INET) && (family != AF_INET6))){
		if (family == AF_INET){
			netplay_copy_str(netplay_route_local_addr, sizeof(netplay_route_local_addr), netplay_default_ipv4);
			netplay_copy_str(netplay_route_local_scope, sizeof(netplay_route_local_scope), netplay_default_ipv4_scope);
		}else if (family == AF_INET6){
			netplay_copy_str(netplay_route_local_addr, sizeof(netplay_route_local_addr), netplay_default_ipv6);
			netplay_copy_str(netplay_route_local_scope, sizeof(netplay_route_local_scope), netplay_default_ipv6_scope);
		}
		return;
	}
	sock = socket(family, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET){
		return;
	}
	if ((manual_bind != NULL) && (manual_bind[0] != 0)){
		struct sockaddr_storage bind_ss;
		socklen_t bind_len = 0U;
		int bind_family = AF_UNSPEC;
		if (netplay_parse_ip_literal(manual_bind, &bind_family, &bind_ss, &bind_len, NULL, 0U) && (bind_family == family)){
			if (bind_family == AF_INET){ ((struct sockaddr_in*)&bind_ss)->sin_port = 0; }
			else if (bind_family == AF_INET6){ ((struct sockaddr_in6*)&bind_ss)->sin6_port = 0; }
			(void)bind(sock, (struct sockaddr*)&bind_ss, bind_len);
		}
	}
	if (connect(sock, dest, dest_len) == 0){
		if (getsockname(sock, (struct sockaddr*)&local, &local_len) == 0){
			(void)netplay_addr_to_string((struct sockaddr const*)&local, netplay_route_local_addr, sizeof(netplay_route_local_addr), netplay_route_local_scope, sizeof(netplay_route_local_scope));
		}
	}
	NETPLAY_CLOSESOCK(sock);
}

static int netplay_selected_bind_family(void)
{
	int family = AF_UNSPEC;
	(void)netplay_parse_ip_literal(netplay_cfg.network_interface, &family, NULL, NULL, NULL, 0U);
	return family;
}

static uint32 netplay_rand32(void)
{
	uint32 a = (uint32)rand();
	uint32 b = (uint32)rand();
	return (a << 16) ^ b ^ (uint32)time(NULL);
}

static void netplay_record_local_input(auint player, auint frame, auint buttons)
{
	netplay_input_hist_t *slot;
	auint head;

	if (player >= ROLLBACK_MAX_PLAYERS){
		return;
	}
	head = netplay_state.local_input_hist_head[player] % NETPLAY_INPUT_HISTORY_SEND;
	slot = &(netplay_state.local_input_hist[player][head]);
	slot->valid = TRUE;
	slot->frame = (uint32)frame;
	slot->buttons = (uint32)buttons;
	netplay_state.local_input_hist_head[player] = (head + 1U) % NETPLAY_INPUT_HISTORY_SEND;
}

static void netplay_record_local_device_event(auint frame, rollback_device_event_t const *ev)
{
	netplay_dev_hist_t *slot;
	auint head;
	if (ev == NULL){
		return;
	}
	head = netplay_state.local_dev_hist_head % NETPLAY_DEVICE_HISTORY_SEND;
	slot = &(netplay_state.local_dev_hist[head]);
	memset(slot, 0, sizeof(*slot));
	slot->valid = TRUE;
	slot->frame = (uint32)frame;
	slot->ev = *ev;
	if (slot->ev.len > ROLLBACK_DEVICE_EVENT_DATA_MAX){
		slot->ev.len = ROLLBACK_DEVICE_EVENT_DATA_MAX;
	}
	netplay_state.local_dev_hist_head = (head + 1U) % NETPLAY_DEVICE_HISTORY_SEND;
}

static auint netplay_pack_local_input_history(auint player, auint frame, netplay_pkt_t *pkt)
{
	netplay_input_hist_pkt_t hist[NETPLAY_INPUT_HISTORY_MAX];
	auint count = 0U;
	auint n;

	if ((pkt == NULL) || (player >= ROLLBACK_MAX_PLAYERS)){
		return 0U;
	}
	memset(hist, 0, sizeof(hist));
	for (n = 0U; n < NETPLAY_INPUT_HISTORY_SEND; n++){
		auint idx = (netplay_state.local_input_hist_head[player] + NETPLAY_INPUT_HISTORY_SEND - 1U - n) % NETPLAY_INPUT_HISTORY_SEND;
		netplay_input_hist_t const *src = &(netplay_state.local_input_hist[player][idx]);
		if (!src->valid){
			continue;
		}
		if (src->frame >= (uint32)frame){
			continue;
		}
		if (count >= NETPLAY_INPUT_HISTORY_MAX){
			break;
		}
		hist[count].frame = htonl(src->frame);
		hist[count].buttons = htonl(src->buttons);
		count++;
	}
	if (count != 0U){
		memcpy(pkt->rom_name, hist, count * sizeof(hist[0]));
	}
	pkt->aux1 = htons((uint16)count);
	return count;
}

static void netplay_handle_input_history(netplay_pkt_t const *pkt)
{
	netplay_input_hist_pkt_t hist[NETPLAY_INPUT_HISTORY_MAX];
	auint count;
	auint i;

	if (pkt == NULL){
		return;
	}
	count = (auint)ntohs(pkt->aux1);
	if (count > NETPLAY_INPUT_HISTORY_MAX){
		count = NETPLAY_INPUT_HISTORY_MAX;
	}
	if (count == 0U){
		return;
	}
	memcpy(hist, pkt->rom_name, count * sizeof(hist[0]));
	for (i = 0U; i < count; i++){
		(void)rollback_queue_remote_input((auint)ntohl(hist[i].frame), (auint)pkt->player, (auint)ntohl(hist[i].buttons));
	}
}

static auint netplay_pack_local_device_history(auint frame, netplay_pkt_t *pkt)
{
	netplay_dev_hist_pkt_t hist[NETPLAY_DEVICE_HISTORY_MAX];
	auint count = 0U;
	auint n;
	if (pkt == NULL){
		return 0U;
	}
	memset(hist, 0, sizeof(hist));
	for (n = 0U; n < NETPLAY_DEVICE_HISTORY_SEND; ++n){
		auint idx = (netplay_state.local_dev_hist_head + NETPLAY_DEVICE_HISTORY_SEND - 1U - n) % NETPLAY_DEVICE_HISTORY_SEND;
		netplay_dev_hist_t const *src = &(netplay_state.local_dev_hist[idx]);
		if (!src->valid){
			continue;
		}
		if (src->frame >= (uint32)frame){
			continue;
		}
		if (count >= NETPLAY_DEVICE_HISTORY_MAX){
			break;
		}
		hist[count].frame = htonl(src->frame);
		hist[count].seq = htons(src->ev.seq);
		hist[count].port = src->ev.port;
		hist[count].slot = src->ev.slot;
		hist[count].type = src->ev.type;
		hist[count].len = (uint8)(src->ev.len & 0x0FU);
		memcpy(hist[count].data, src->ev.data, ROLLBACK_DEVICE_EVENT_DATA_MAX);
		count++;
	}
	if (count != 0U){
		memcpy(pkt->rom_name, hist, count * sizeof(hist[0]));
	}
	pkt->aux1 = htons((uint16)count);
	return count;
}

static void netplay_handle_device_history(netplay_pkt_t const *pkt)
{
	netplay_dev_hist_pkt_t hist[NETPLAY_DEVICE_HISTORY_MAX];
	auint count;
	auint i;
	if (pkt == NULL){
		return;
	}
	count = (auint)ntohs(pkt->aux1);
	if (count > NETPLAY_DEVICE_HISTORY_MAX){
		count = NETPLAY_DEVICE_HISTORY_MAX;
	}
	if (count == 0U){
		return;
	}
	memcpy(hist, pkt->rom_name, count * sizeof(hist[0]));
	for (i = 0U; i < count; ++i){
		rollback_device_event_t ev;
		memset(&ev, 0, sizeof(ev));
		ev.seq = (uint16)ntohs(hist[i].seq);
		ev.port = hist[i].port;
		ev.slot = hist[i].slot;
		ev.type = hist[i].type;
		ev.len = hist[i].len;
		if (ev.len > ROLLBACK_DEVICE_EVENT_DATA_MAX){ ev.len = ROLLBACK_DEVICE_EVENT_DATA_MAX; }
		memcpy(ev.data, hist[i].data, ROLLBACK_DEVICE_EVENT_DATA_MAX);
		(void)rollback_queue_remote_device_event((auint)ntohl(hist[i].frame), &ev);
	}
}

static boole netplay_send_device_history(auint frame)
{
	netplay_pkt_t pkt;
	auint count;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (!netplay_state.hello_acked) || (!netplay_state.compat_ok) || (!netplay_state.session_started)){
		return FALSE;
	}
	if (netplay_state.last_dev_hist_send_frame == frame){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_DEVICE_EVENT_HISTORY);
	pkt.frame = htonl((uint32)frame);
	count = netplay_pack_local_device_history(frame, &pkt);
	if (count == 0U){
		return FALSE;
	}
	netplay_state.last_dev_hist_send_frame = frame;
	return netplay_send_raw(&pkt);
}

static auint netplay_get_env_u32(char const *name, auint defv)
{
	char const *src = getenv(name);
	char *endp;
	unsigned long val;
	if ((src == NULL) || (src[0] == 0)){
		return defv;
	}
	val = strtoul(src, &endp, 0);
	if ((endp == src) || (*endp != 0)){
		return defv;
	}
	return (auint)val;
}

static boole netplay_get_env_bool(char const *name, boole defv)
{
	char const *src = getenv(name);
	if ((src == NULL) || (src[0] == 0)){
		return defv;
	}
	if ((strcmp(src, "1") == 0) || (strcmp(src, "true") == 0) || (strcmp(src, "yes") == 0) || (strcmp(src, "on") == 0)){
		return TRUE;
	}
	if ((strcmp(src, "0") == 0) || (strcmp(src, "false") == 0) || (strcmp(src, "no") == 0) || (strcmp(src, "off") == 0)){
		return FALSE;
	}
	return defv;
}

static netplay_rom_sync_mode_t netplay_parse_sync_mode(char const *src, netplay_rom_sync_mode_t defv)
{
	if ((src == NULL) || (src[0] == 0)){
		return defv;
	}
	if ((strcmp(src, "0") == 0) || (strcmp(src, "off") == 0)){
		return NETPLAY_ROM_SYNC_OFF;
	}
	if ((strcmp(src, "1") == 0) || (strcmp(src, "missing") == 0)){
		return NETPLAY_ROM_SYNC_MISSING;
	}
	if ((strcmp(src, "2") == 0) || (strcmp(src, "mismatch") == 0)){
		return NETPLAY_ROM_SYNC_MISMATCH;
	}
	if ((strcmp(src, "3") == 0) || (strcmp(src, "always") == 0) || (strcmp(src, "force") == 0)){
		return NETPLAY_ROM_SYNC_ALWAYS;
	}
	return defv;
}

static uint32 netplay_mask_for_player(auint player_one_based)
{
	if ((player_one_based == 0U) || (player_one_based > 32U)){
		return 0U;
	}
	return ((uint32)1UL << (player_one_based - 1U));
}

static uint32 netplay_active_player_mask(auint max_players)
{
	if (max_players == 0U){
		return 0U;
	}
	if (max_players >= 32U){
		return 0xFFFFFFFFUL;
	}
	return (((uint32)1UL << max_players) - 1UL);
}

static uint32 netplay_sanitize_player_mask(uint32 mask, auint max_players)
{
	uint32 active;
	if (max_players == 0U){
		max_players = 1U;
	}
	if (max_players > ROLLBACK_MAX_PLAYERS){
		max_players = ROLLBACK_MAX_PLAYERS;
	}
	active = netplay_active_player_mask(max_players);
	mask &= active;
	if (mask == 0U){
		mask = 1UL;
	}
	return mask;
}

static auint netplay_primary_player_from_mask(uint32 mask, auint max_players)
{
	auint i;
	uint32 active = netplay_active_player_mask(max_players);
	mask &= active;
	for (i = 0U; i < max_players; i++){
		if ((mask & ((uint32)1UL << i)) != 0U){
			return i + 1U;
		}
	}
	return 1U;
}

static uint32 netplay_parse_mask_env(char const *name, uint32 defv)
{
	char const *src = getenv(name);
	char *endp;
	unsigned long val;
	if ((src == NULL) || (src[0] == 0)){
		return defv;
	}
	val = strtoul(src, &endp, 0);
	if ((endp == src) || (*endp != 0)){
		return defv;
	}
	return (uint32)val;
}

static void netplay_copy_name_field(char *dst, size_t dsz, char const *src, char const *fallback)
{
	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	if ((src == NULL) || (src[0] == 0)){
		src = fallback;
	}
	if (src == NULL){
		src = "";
	}
	strncpy(dst, src, dsz - 1U);
	dst[dsz - 1U] = 0;
}

static boole netplay_name_equals_ci(char const *a, char const *b)
{
	unsigned char ca;
	unsigned char cb;
	if ((a == NULL) || (b == NULL)){
		return FALSE;
	}
	for (;;){
		ca = (unsigned char)(*a);
		cb = (unsigned char)(*b);
		if ((ca >= 'A') && (ca <= 'Z')){ ca = (unsigned char)(ca - 'A' + 'a'); }
		if ((cb >= 'A') && (cb <= 'Z')){ cb = (unsigned char)(cb - 'A' + 'a'); }
		if (ca != cb){
			return FALSE;
		}
		if (ca == 0U){
			return TRUE;
		}
		a++;
		b++;
	}
}

static boole netplay_name_is_taken(char const *candidate, char const *taken0, char const *taken1)
{
	if ((candidate == NULL) || (candidate[0] == 0)){
		return FALSE;
	}
	if ((taken0 != NULL) && (taken0[0] != 0) && netplay_name_equals_ci(candidate, taken0)){
		return TRUE;
	}
	if ((taken1 != NULL) && (taken1[0] != 0) && netplay_name_equals_ci(candidate, taken1)){
		return TRUE;
	}
	return FALSE;
}

static void netplay_make_unique_name(char *dst, size_t dsz, char const *requested, char const *taken0, char const *taken1)
{
	char base[NETPLAY_LOBBY_NAME_LEN + 1U];
	char root[NETPLAY_LOBBY_NAME_LEN + 1U];
	char cand[NETPLAY_LOBBY_NAME_LEN + 1U];
	size_t root_len;
	unsigned suffix;

	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	netplay_copy_name_field(base, sizeof(base), requested, "Player");
	if (!netplay_name_is_taken(base, taken0, taken1)){
		memcpy(dst, base, dsz);
		dst[dsz - 1U] = 0;
		return;
	}
	memcpy(root, base, sizeof(root));
	root[sizeof(root) - 1U] = 0;
	root_len = strlen(root);
	while ((root_len > 0U) && (root[root_len - 1U] >= '0') && (root[root_len - 1U] <= '9')){
		root_len--;
	}
	if (root_len == 0U){
		strncpy(root, "Player", sizeof(root) - 1U);
		root[sizeof(root) - 1U] = 0;
		root_len = strlen(root);
	}else{
		root[root_len] = 0;
	}
	for (suffix = 2U; suffix < 1000U; suffix++){
		char suffixbuf[8];
		size_t suffix_len;
		snprintf(suffixbuf, sizeof(suffixbuf), "%u", suffix);
		suffix_len = strlen(suffixbuf);
		if ((root_len + suffix_len) <= NETPLAY_LOBBY_NAME_LEN){
			snprintf(cand, sizeof(cand), "%s%s", root, suffixbuf);
		}else{
			size_t keep_len = NETPLAY_LOBBY_NAME_LEN;
			if (suffix_len < keep_len){
				keep_len -= suffix_len;
			}else{
				keep_len = 0U;
			}
			memset(cand, 0, sizeof(cand));
			if (keep_len > 0U){
				memcpy(cand, root, keep_len);
			}
			memcpy(cand + keep_len, suffixbuf, suffix_len);
			cand[NETPLAY_LOBBY_NAME_LEN] = 0;
		}
		if (!netplay_name_is_taken(cand, taken0, taken1)){
			memcpy(dst, cand, dsz);
			dst[dsz - 1U] = 0;
			return;
		}
	}
	memcpy(dst, base, dsz);
	dst[dsz - 1U] = 0;
}

static void netplay_default_local_labels(void)
{
	auint i;
	char envname[32];
	char deflabel[16];
	if (netplay_cfg.local_name[0] == 0){
		netplay_copy_name_field(netplay_cfg.local_name, sizeof(netplay_cfg.local_name), getenv("CZ_NETPLAY_NAME"), "Player");
	}
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		if (netplay_cfg.local_pad_labels[i][0] == 0){
			snprintf(envname, sizeof(envname), "CZ_NETPLAY_PAD%u", (unsigned)(i + 1U));
			snprintf(deflabel, sizeof(deflabel), "Pad %u", (unsigned)(i + 1U));
			netplay_copy_name_field(netplay_cfg.local_pad_labels[i], sizeof(netplay_cfg.local_pad_labels[i]), getenv(envname), deflabel);
		}
	}
}

static void netplay_pack_label_fields(char *dst, size_t dsz)
{
	auint i;
	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	memset(dst, 0, dsz);
	if (dsz < (NETPLAY_LABEL_FIELD_COUNT * NETPLAY_LABEL_FIELD_SIZE)){
		return;
	}
	memcpy(dst, netplay_cfg.local_name, strnlen(netplay_cfg.local_name, NETPLAY_LABEL_FIELD_SIZE));
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		memcpy(dst + ((i + 1U) * NETPLAY_LABEL_FIELD_SIZE),
		       netplay_cfg.local_pad_labels[i],
		       strnlen(netplay_cfg.local_pad_labels[i], NETPLAY_LABEL_FIELD_SIZE));
	}
}

static void netplay_unpack_label_fields(char const *src, char *name_dst, char pad_dst[ROLLBACK_MAX_PLAYERS][NETPLAY_LOBBY_PAD_LABEL_LEN + 1U])
{
	auint i;
	if (src == NULL){
		return;
	}
	if (name_dst != NULL){
		memcpy(name_dst, src, NETPLAY_LABEL_FIELD_SIZE);
		name_dst[NETPLAY_LOBBY_NAME_LEN] = 0;
	}
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		memcpy(pad_dst[i], src + ((i + 1U) * NETPLAY_LABEL_FIELD_SIZE), NETPLAY_LABEL_FIELD_SIZE);
		pad_dst[i][NETPLAY_LOBBY_PAD_LABEL_LEN] = 0;
	}
}

static void netplay_mark_labels_dirty(void)
{
	netplay_state.lobby_labels_dirty = TRUE;
	netplay_state.lobby_labels_sent = FALSE;
	netplay_state.last_labels_send_ms = 0U;
}

static uint32 netplay_now_ms(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	return (uint32)GetTickCount();
#else
	struct timeval tv;
	if (gettimeofday(&tv, NULL) != 0){
		return 0U;
	}
	return (uint32)(((uint32)tv.tv_sec * 1000U) + (uint32)(tv.tv_usec / 1000));
#endif
}

static uint32 netplay_ms_delta(uint32 newer, uint32 older)
{
	return (uint32)(newer - older);
}

static void netplay_chat_add_line(char const *text)
{
	char *dst;
	if (text == NULL){
		text = "";
	}
	dst = netplay_state.chat_lines[netplay_state.chat_head];
	strncpy(dst, text, NETPLAY_LOBBY_CHAT_LINE_CHARS - 1U);
	dst[NETPLAY_LOBBY_CHAT_LINE_CHARS - 1U] = 0;
	netplay_state.chat_head = (netplay_state.chat_head + 1U) % NETPLAY_LOBBY_CHAT_LINES;
	if (netplay_state.chat_count < NETPLAY_LOBBY_CHAT_LINES){
		netplay_state.chat_count++;
	}
}

static void netplay_lobby_default_layout(void)
{
	auint i;
	uint32 host_mask;
	uint32 active_mask;
	auint host_pad = 0U;
	auint guest_pad = 0U;
	active_mask = netplay_active_player_mask(netplay_cfg.max_players);
	host_mask = netplay_sanitize_player_mask(netplay_cfg.local_player_mask, netplay_cfg.max_players) & active_mask;
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		netplay_state.lobby_owner_peer[i] = NETPLAY_LOBBY_OWNER_NONE;
		netplay_state.lobby_pad_index[i] = 0U;
	}
	for (i = 0U; i < netplay_cfg.max_players; i++){
		if ((host_mask & ((uint32)1UL << i)) != 0U){
			netplay_state.lobby_owner_peer[i] = NETPLAY_LOBBY_OWNER_HOST;
			netplay_state.lobby_pad_index[i] = (uint8)host_pad;
			host_pad = (host_pad + 1U) % ROLLBACK_MAX_PLAYERS;
		}else{
			netplay_state.lobby_owner_peer[i] = NETPLAY_LOBBY_OWNER_GUEST;
			netplay_state.lobby_pad_index[i] = (uint8)guest_pad;
			guest_pad = (guest_pad + 1U) % ROLLBACK_MAX_PLAYERS;
		}
	}
	netplay_state.lobby_valid = TRUE;
	netplay_state.lobby_dirty = TRUE;
}

static uint32 netplay_lobby_local_mask(void)
{
	auint i;
	uint32 mask = 0U;
	uint8 self_id = (netplay_state.mode == NETPLAY_MODE_SERVER) ? NETPLAY_LOBBY_OWNER_HOST : NETPLAY_LOBBY_OWNER_GUEST;
	for (i = 0U; i < netplay_cfg.max_players; i++){
		if (netplay_state.lobby_owner_peer[i] == self_id){
			mask |= ((uint32)1UL << i);
		}
	}
	return mask;
}

static uint32 netplay_lobby_peer_mask(void)
{
	auint i;
	uint32 mask = 0U;
	uint8 peer_id = (netplay_state.mode == NETPLAY_MODE_SERVER) ? NETPLAY_LOBBY_OWNER_GUEST : NETPLAY_LOBBY_OWNER_HOST;
	for (i = 0U; i < netplay_cfg.max_players; i++){
		if (netplay_state.lobby_owner_peer[i] == peer_id){
			mask |= ((uint32)1UL << i);
		}
	}
	return mask;
}

static boole netplay_lobby_seat_map_valid(void)
{
	auint i;
	uint32 active_mask = netplay_active_player_mask(netplay_cfg.max_players);
	uint32 seen_host = 0U;
	uint32 seen_guest = 0U;
	uint32 covered = 0U;
	for (i = 0U; i < netplay_cfg.max_players; i++){
		uint8 owner = netplay_state.lobby_owner_peer[i];
		uint8 pad = netplay_state.lobby_pad_index[i];
		if (owner == NETPLAY_LOBBY_OWNER_NONE){
			return FALSE;
		}
		if (pad >= ROLLBACK_MAX_PLAYERS){
			return FALSE;
		}
		covered |= ((uint32)1UL << i);
		if (owner == NETPLAY_LOBBY_OWNER_HOST){
			if ((seen_host & ((uint32)1UL << pad)) != 0U){
				return FALSE;
			}
			seen_host |= ((uint32)1UL << pad);
		}else if (owner == NETPLAY_LOBBY_OWNER_GUEST){
			if ((seen_guest & ((uint32)1UL << pad)) != 0U){
				return FALSE;
			}
			seen_guest |= ((uint32)1UL << pad);
		}else{
			return FALSE;
		}
	}
	return ((covered & active_mask) == active_mask);
}

static void netplay_lobby_encode(char *dst, size_t dsz)
{
	auint i;
	char *p = dst;
	size_t rem = dsz;
	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	dst[0] = 0;
	for (i = 0U; i < netplay_cfg.max_players; i++){
		char owner = '-';
		char pad = '0';
		if (netplay_state.lobby_owner_peer[i] == NETPLAY_LOBBY_OWNER_HOST){ owner = 'H'; }
		else if (netplay_state.lobby_owner_peer[i] == NETPLAY_LOBBY_OWNER_GUEST){ owner = 'G'; }
		if (netplay_state.lobby_pad_index[i] < 9U){
			pad = (char)('1' + netplay_state.lobby_pad_index[i]);
		}
		if (rem <= 2U){ break; }
		*p++ = owner;
		*p++ = pad;
		rem -= 2U;
	}
	if (rem > 3U){
		*p++ = ';';
		*p++ = netplay_state.lobby_local_ready ? '1' : '0';
		*p++ = netplay_state.lobby_peer_ready ? '1' : '0';
		rem -= 3U;
	}
	*p = 0;
}

static boole netplay_lobby_decode(char const *src, uint8 *owners, uint8 *pads, boole *host_ready, boole *guest_ready)
{
	auint i;
	if (src == NULL){
		return FALSE;
	}
	for (i = 0U; i < netplay_cfg.max_players; i++){
		char owner = src[i * 2U];
		char pad = src[i * 2U + 1U];
		if ((owner == 0) || (pad == 0)){
			return FALSE;
		}
		if (owner == 'H'){ owners[i] = NETPLAY_LOBBY_OWNER_HOST; }
		else if (owner == 'G'){ owners[i] = NETPLAY_LOBBY_OWNER_GUEST; }
		else { owners[i] = NETPLAY_LOBBY_OWNER_NONE; }
		if ((pad >= '1') && (pad <= '4')){ pads[i] = (uint8)(pad - '1'); }
		else { pads[i] = 0U; }
	}
	if (host_ready != NULL){
		*host_ready = FALSE;
	}
	if (guest_ready != NULL){
		*guest_ready = FALSE;
	}
	src += netplay_cfg.max_players * 2U;
	if (*src == ';'){
		if (host_ready != NULL){ *host_ready = (src[1] == '1'); }
		if (guest_ready != NULL){ *guest_ready = (src[2] == '1'); }
	}
	return TRUE;
}

static void netplay_lobby_clear_request(auint seat)
{
	if (seat >= ROLLBACK_MAX_PLAYERS){
		return;
	}
	netplay_state.lobby_request_valid[seat] = FALSE;
	netplay_state.lobby_request_owner_peer[seat] = NETPLAY_LOBBY_OWNER_NONE;
	netplay_state.lobby_request_pad_index[seat] = 0U;
}

static void netplay_lobby_clear_all_requests(void)
{
	auint i;
	for (i = 0U; i < ROLLBACK_MAX_PLAYERS; i++){
		netplay_lobby_clear_request(i);
	}
}

static void netplay_lobby_note_local(char const *text)
{
	if ((text != NULL) && (text[0] != 0)){
		netplay_chat_add_line(text);
	}
}

static void netplay_lobby_note_remote(char const *text)
{
	if ((text == NULL) || (text[0] == 0)){
		return;
	}
	netplay_chat_add_line(text);
	(void)netplay_send_chat_pkt(text);
}

static void netplay_lobby_mark_dirty(void)
{
	netplay_state.lobby_dirty = TRUE;
	netplay_state.last_lobby_send_ms = 0U;
}

static void netplay_lobby_invalidate_ready(char const *reason)
{
	boole changed = FALSE;
	if (netplay_state.lobby_local_ready){
		netplay_state.lobby_local_ready = FALSE;
		changed = TRUE;
	}
	if (netplay_state.lobby_peer_ready){
		netplay_state.lobby_peer_ready = FALSE;
		changed = TRUE;
	}
	if (!changed){
		return;
	}
	if ((reason != NULL) && (reason[0] != 0)){
		if (netplay_state.peer_valid){
			netplay_lobby_note_remote(reason);
		}else{
			netplay_lobby_note_local(reason);
		}
	}
	netplay_lobby_mark_dirty();
}

static boole netplay_send_lobby_state(void)
{
	netplay_pkt_t pkt;
	if ((netplay_state.mode != NETPLAY_MODE_SERVER) || (!netplay_state.peer_valid)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_LOBBY_STATE);
	netplay_lobby_encode(pkt.rom_name, sizeof(pkt.rom_name));
	pkt.aux0 = htonl(netplay_lobby_local_mask());
	pkt.buttons = htonl(netplay_lobby_peer_mask());
	if (netplay_send_raw(&pkt)){
		netplay_state.lobby_dirty = FALSE;
		netplay_state.last_lobby_send_ms = netplay_now_ms();
		return TRUE;
	}
	return FALSE;
}

static boole netplay_send_lobby_ready_pkt(boole ready)
{
	netplay_pkt_t pkt;
	if ((netplay_state.mode != NETPLAY_MODE_CLIENT) || (!netplay_state.peer_valid)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_LOBBY_READY);
	pkt.result = ready ? 1U : 0U;
	return netplay_send_raw(&pkt);
}

static boole netplay_send_chat_pkt(char const *text)
{
	netplay_pkt_t pkt;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (text == NULL) || (text[0] == 0)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_LOBBY_CHAT);
	strncpy(pkt.rom_name, text, sizeof(pkt.rom_name) - 1U);
	pkt.rom_name[sizeof(pkt.rom_name) - 1U] = 0;
	return netplay_send_raw(&pkt);
}

static boole netplay_send_lobby_labels_pkt(void)
{
	netplay_pkt_t pkt;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (!netplay_state.hello_acked)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_LOBBY_LABELS);
	netplay_pack_label_fields(pkt.rom_name, sizeof(pkt.rom_name));
	if (netplay_send_raw(&pkt)){
		netplay_state.lobby_labels_dirty = FALSE;
		netplay_state.lobby_labels_sent = TRUE;
		netplay_state.last_labels_send_ms = netplay_now_ms();
		return TRUE;
	}
	return FALSE;
}

static boole netplay_send_name_override_pkt(char const *name)
{
	netplay_pkt_t pkt;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (!netplay_state.hello_acked) || (name == NULL) || (name[0] == 0)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_LOBBY_LABELS);
	pkt.result = 1U;
	strncpy(pkt.rom_name, name, sizeof(pkt.rom_name) - 1U);
	pkt.rom_name[sizeof(pkt.rom_name) - 1U] = 0;
	return netplay_send_raw(&pkt);
}

static boole netplay_send_lobby_request_pkt(auint seat, auint owner_peer, auint pad_index)
{
	netplay_pkt_t pkt;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (seat >= netplay_cfg.max_players)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_LOBBY_REQUEST);
	pkt.frame = htonl((uint32)seat);
	pkt.aux0 = htonl((uint32)owner_peer);
	pkt.buttons = htonl((uint32)pad_index);
	pkt.result = (owner_peer == NETPLAY_LOBBY_OWNER_NONE) ? 2U : 1U;
	return netplay_send_raw(&pkt);
}

static boole netplay_send_ping(void)
{
	netplay_pkt_t pkt;
	uint32 now = netplay_now_ms();
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (!netplay_state.hello_acked)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_PING);
	netplay_state.ping_token = now;
	netplay_state.ping_sent_ms = now;
	pkt.frame = htonl(now);
	if (netplay_send_raw(&pkt)){
		netplay_state.last_ping_send_ms = now;
		return TRUE;
	}
	return FALSE;
}

static boole netplay_send_pong(uint32 token)
{
	netplay_pkt_t pkt;
	netplay_init_pkt(&pkt, NETPLAY_TYPE_PONG);
	pkt.frame = htonl(token);
	return netplay_send_raw(&pkt);
}

static void netplay_format_addr(char *dst, size_t dsz, struct sockaddr const *addr, socklen_t addrlen, auint *port_out)
{
	char host[NI_MAXHOST];
	char serv[NI_MAXSERV];
	int rv;
	if (port_out != NULL){
		*port_out = 0U;
	}
	if ((dst == NULL) || (dsz == 0U)){
		return;
	}
	dst[0] = 0;
	if (addr == NULL){
		return;
	}
	if ((addr->sa_family == AF_INET6) && (addrlen >= (socklen_t)sizeof(struct sockaddr_in6))){
		struct sockaddr_in6 const *addr6 = (struct sockaddr_in6 const*)addr;
		if (netplay_addr_is_v4mapped(&(addr6->sin6_addr))){
			struct sockaddr_in addr4;
			memset(&addr4, 0, sizeof(addr4));
			addr4.sin_family = AF_INET;
			addr4.sin_port = addr6->sin6_port;
			memcpy(&(addr4.sin_addr.s_addr), &(addr6->sin6_addr.s6_addr[12]), 4U);
			rv = getnameinfo((struct sockaddr const*)&addr4, (socklen_t)sizeof(addr4), host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
			if (rv == 0){
				strncpy(dst, host, dsz - 1U);
				dst[dsz - 1U] = 0;
				if (port_out != NULL){
					*port_out = (auint)strtoul(serv, NULL, 10);
				}
				return;
			}
		}
	}
	rv = getnameinfo(addr, addrlen, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
	if (rv != 0){
		return;
	}
	strncpy(dst, host, dsz - 1U);
	dst[dsz - 1U] = 0;
	if (port_out != NULL){
		*port_out = (auint)strtoul(serv, NULL, 10);
	}
}

static boole netplay_addr_equal(struct sockaddr const *a, socklen_t alen, struct sockaddr const *b, socklen_t blen)
{
	struct sockaddr_in const *a4;
	struct sockaddr_in const *b4;
	struct sockaddr_in6 const *a6;
	struct sockaddr_in6 const *b6;
	if ((a == NULL) || (b == NULL)){
		return FALSE;
	}
	if (a->sa_family != b->sa_family){
		return FALSE;
	}
	if ((a->sa_family == AF_INET) && (alen >= (socklen_t)sizeof(struct sockaddr_in)) && (blen >= (socklen_t)sizeof(struct sockaddr_in))){
		a4 = (struct sockaddr_in const*)a;
		b4 = (struct sockaddr_in const*)b;
		return (a4->sin_port == b4->sin_port) && (a4->sin_addr.s_addr == b4->sin_addr.s_addr);
	}
	if ((a->sa_family == AF_INET6) && (alen >= (socklen_t)sizeof(struct sockaddr_in6)) && (blen >= (socklen_t)sizeof(struct sockaddr_in6))){
		a6 = (struct sockaddr_in6 const*)a;
		b6 = (struct sockaddr_in6 const*)b;
		return (a6->sin6_port == b6->sin6_port) &&
		       (a6->sin6_scope_id == b6->sin6_scope_id) &&
		       (memcmp(&(a6->sin6_addr), &(b6->sin6_addr), sizeof(a6->sin6_addr)) == 0);
	}
	return FALSE;
}


static boole netplay_addr_is_v4mapped(struct in6_addr const *a6)
{
	if (a6 == NULL){
		return FALSE;
	}
	return (memcmp(a6->s6_addr, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF", 12) == 0);
}

static void netplay_map_v4_to_v6(struct sockaddr_in6 *dst, uint32 ipv4_be, uint16 port_be)
{
	memset(dst, 0, sizeof(*dst));
	dst->sin6_family = AF_INET6;
	dst->sin6_port = port_be;
	dst->sin6_addr.s6_addr[10] = 0xFFU;
	dst->sin6_addr.s6_addr[11] = 0xFFU;
	memcpy(&(dst->sin6_addr.s6_addr[12]), &ipv4_be, 4U);
}

static boole netplay_socket_allows_addr(int sock_family, struct sockaddr_storage const *addr)
{
	if ((addr == NULL) || (sock_family == AF_UNSPEC)){
		return FALSE;
	}
	if (sock_family == addr->ss_family){
		return TRUE;
	}
	if ((sock_family == AF_INET6) && (addr->ss_family == AF_INET)){
		return TRUE;
	}
	return FALSE;
}

static boole netplay_prepare_peer_addr_for_socket(int sock_family, struct sockaddr_storage *addr, socklen_t *addr_len)
{
	struct sockaddr_in const *src4;
	struct sockaddr_in6 mapped6;
	if ((addr == NULL) || (addr_len == NULL)){
		return FALSE;
	}
	if (sock_family == AF_INET6){
		if (addr->ss_family == AF_INET6){
			return TRUE;
		}
		if (addr->ss_family == AF_INET){
			src4 = (struct sockaddr_in const*)addr;
			netplay_map_v4_to_v6(&mapped6, src4->sin_addr.s_addr, src4->sin_port);
			memcpy(addr, &mapped6, sizeof(mapped6));
			*addr_len = (socklen_t)sizeof(mapped6);
			return TRUE;
		}
		return FALSE;
	}
	if (sock_family == AF_INET){
		return (addr->ss_family == AF_INET);
	}
	return FALSE;
}

static void netplay_disable_v6only(SOCKET sock)
{
#if defined(IPV6_V6ONLY)
	int off = 0;
	(void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char const*)&off, sizeof(off));
#else
	(void)sock;
#endif
}

static void netplay_set_nonblocking(SOCKET sock)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	u_long mode = 1UL;
	ioctlsocket(sock, FIONBIO, &mode);
#else
	int flags = fcntl(sock, F_GETFL, 0);
	if (flags >= 0){
		(void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	}
#endif
}

static boole netplay_prepare_socket_common(SOCKET sock)
{
	netplay_set_nonblocking(sock);
	return TRUE;
}

static boole netplay_resolve_peer(char const *host, auint port, int family, int socktype, struct sockaddr_storage *out, socklen_t *out_len)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *rp;
	char portbuf[16];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = socktype;
#ifdef AI_ADDRCONFIG
	hints.ai_flags = AI_ADDRCONFIG;
#endif
	snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);
	if (getaddrinfo(host, portbuf, &hints, &res) != 0){
		return FALSE;
	}
	for (rp = res; rp != NULL; rp = rp->ai_next){
		if ((rp->ai_family == AF_INET) || (rp->ai_family == AF_INET6)){
			if (rp->ai_addrlen <= (socklen_t)sizeof(*out)){
				memcpy(out, rp->ai_addr, rp->ai_addrlen);
				*out_len = (socklen_t)rp->ai_addrlen;
				freeaddrinfo(res);
				return TRUE;
			}
		}
	}
	freeaddrinfo(res);
	return FALSE;
}

static boole netplay_bind_local_udp(SOCKET sock, int family, auint local_port, char const *bind_ip)
{
	if (family == AF_INET6){
		struct sockaddr_in6 bind_addr6;
		netplay_disable_v6only(sock);
		memset(&bind_addr6, 0, sizeof(bind_addr6));
		bind_addr6.sin6_family = AF_INET6;
		bind_addr6.sin6_addr = in6addr_any;
		bind_addr6.sin6_port = htons((uint16)local_port);
		if ((bind_ip != NULL) && (bind_ip[0] != 0)){
			if (netplay_inet_pton_compat(AF_INET6, bind_ip, &bind_addr6.sin6_addr) != 1){
				return FALSE;
			}
		}
		return (bind(sock, (struct sockaddr*)&bind_addr6, sizeof(bind_addr6)) != SOCKET_ERROR);
	}else{
		struct sockaddr_in bind_addr4;
		memset(&bind_addr4, 0, sizeof(bind_addr4));
		bind_addr4.sin_family = AF_INET;
		bind_addr4.sin_addr.s_addr = htonl(INADDR_ANY);
		bind_addr4.sin_port = htons((uint16)local_port);
		if ((bind_ip != NULL) && (bind_ip[0] != 0)){
			if (netplay_inet_pton_compat(AF_INET, bind_ip, &bind_addr4.sin_addr) != 1){
				return FALSE;
			}
		}
		return (bind(sock, (struct sockaddr*)&bind_addr4, sizeof(bind_addr4)) != SOCKET_ERROR);
	}
}

static auint netplay_query_local_udp_port(SOCKET sock, int family)
{
	if (family == AF_INET6){
		struct sockaddr_in6 addr6;
		socklen_t len = (socklen_t)sizeof(addr6);
		if (getsockname(sock, (struct sockaddr*)&addr6, &len) == 0){
			return (auint)ntohs(addr6.sin6_port);
		}
	}else{
		struct sockaddr_in addr4;
		socklen_t len = (socklen_t)sizeof(addr4);
		if (getsockname(sock, (struct sockaddr*)&addr4, &len) == 0){
			return (auint)ntohs(addr4.sin_port);
		}
	}
	return 0U;
}

static void netplay_close_rom_listener(void)
{
	if (netplay_state.rom_listen_sock != INVALID_SOCKET){
		NETPLAY_CLOSESOCK(netplay_state.rom_listen_sock);
	}
	netplay_state.rom_listen_sock = INVALID_SOCKET;
	netplay_state.rom_listen_port = 0U;
}

static void netplay_store_peer(struct sockaddr const *addr, socklen_t addrlen)
{
	if ((addr == NULL) || (addrlen > (socklen_t)sizeof(netplay_state.peer_addr))){
		return;
	}
	memcpy(&(netplay_state.peer_addr), addr, addrlen);
	netplay_state.peer_addr_len = addrlen;
	netplay_state.peer_valid = TRUE;
	netplay_format_addr(netplay_state.peer_host, sizeof(netplay_state.peer_host), addr, addrlen, &(netplay_state.peer_port));
}

static void netplay_refresh_local_rom_info(void)
{
	boole present;
	boole sendable;
	present = main_get_loaded_rom_info(&(netplay_cfg.rom_crc), &(netplay_cfg.rom_size), &(netplay_cfg.rom_name[0]), (auint)sizeof(netplay_cfg.rom_name), &sendable);
	netplay_cfg.rom_present = present;
	netplay_cfg.rom_sendable = present && sendable;
	if (!present){
		netplay_cfg.rom_crc = 0U;
		netplay_cfg.rom_size = 0U;
		netplay_cfg.rom_name[0] = 0;
	}
}

static void netplay_load_cfg_defaults(void)
{
	char const *sync;
	if (!netplay_cfg.configured){
		netplay_cfg.local_player = netplay_get_env_u32("CZ_NETPLAY_PLAYER", 1U);
		netplay_cfg.max_players = netplay_get_env_u32("CZ_NETPLAY_MAX_PLAYERS", 2U);
		if (netplay_cfg.max_players == 0U){
			netplay_cfg.max_players = 2U;
		}
		if (netplay_cfg.max_players > ROLLBACK_MAX_PLAYERS){
			netplay_cfg.max_players = ROLLBACK_MAX_PLAYERS;
		}
		netplay_cfg.local_player_mask = netplay_parse_mask_env("CZ_NETPLAY_PLAYER_MASK", netplay_mask_for_player(netplay_cfg.local_player));
		netplay_cfg.local_player_mask = netplay_sanitize_player_mask(netplay_cfg.local_player_mask, netplay_cfg.max_players);
		netplay_cfg.local_player = netplay_primary_player_from_mask(netplay_cfg.local_player_mask, netplay_cfg.max_players);
		netplay_cfg.feature_flags = netplay_get_env_u32("CZ_NETPLAY_FEATURES", (auint)(NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP));
		netplay_cfg.rom_send_enabled = netplay_get_env_bool("CZ_NETPLAY_ROM_SEND", TRUE);
		netplay_cfg.rom_receive_enabled = netplay_get_env_bool("CZ_NETPLAY_ROM_RECEIVE", TRUE);
		netplay_cfg.rom_tcp_port = netplay_get_env_u32("CZ_NETPLAY_ROM_TCP_PORT", 0U);
		netplay_cfg.rom_max_size = netplay_get_env_u32("CZ_NETPLAY_ROM_MAX_SIZE", (4U * 1024U * 1024U));
		netplay_cfg.relay_server_port = netplay_get_env_u32("CZ_NETPLAY_RELAY_PORT", 43810U);
		netplay_cfg.relay_allow_direct = netplay_get_env_bool("CZ_NETPLAY_RELAY_DIRECT", TRUE);
		netplay_cfg.relay_allow_relay = netplay_get_env_bool("CZ_NETPLAY_RELAY_ENABLE", TRUE);
		strncpy(netplay_cfg.relay_server_host, "uzenet.us", sizeof(netplay_cfg.relay_server_host) - 1U);
		netplay_cfg.relay_server_host[sizeof(netplay_cfg.relay_server_host) - 1U] = 0;
		{
			char const *rhost = getenv("CZ_NETPLAY_RELAY_HOST");
			if ((rhost != NULL) && (rhost[0] != 0)){
				strncpy(netplay_cfg.relay_server_host, rhost, sizeof(netplay_cfg.relay_server_host) - 1U);
				netplay_cfg.relay_server_host[sizeof(netplay_cfg.relay_server_host) - 1U] = 0;
			}
		}
		sync = getenv("CZ_NETPLAY_ROM_SYNC");
		netplay_cfg.rom_sync_mode = netplay_parse_sync_mode(sync, NETPLAY_ROM_SYNC_MISMATCH);
		netplay_cfg.network_interface[0] = 0;
		netplay_cfg.local_name[0] = 0;
		memset(netplay_cfg.local_pad_labels, 0, sizeof(netplay_cfg.local_pad_labels));
		netplay_default_local_labels();
		netplay_cfg.configured = TRUE;
	}
	(void)netplay_refresh_interfaces();
	netplay_refresh_local_rom_info();
}

static void netplay_set_error(char const *reason)
{
	if (reason == NULL){
		reason = "netplay error";
	}
	strncpy(netplay_state.compat_reason, reason, sizeof(netplay_state.compat_reason) - 1U);
	netplay_state.compat_reason[sizeof(netplay_state.compat_reason) - 1U] = 0;
	print_message("Netplay: %s\n", netplay_state.compat_reason);
}

static void netplay_set_incompat(char const *reason)
{
	netplay_set_error(reason);
	netplay_state.compat_ok = FALSE;
}

static boole netplay_check_compat(netplay_pkt_t const *pkt)
{
	uint32 build_id = ntohl(pkt->build_id);
	uint32 feature_flags = ntohl(pkt->feature_flags);
	auint peer_player = (auint)pkt->player;
	auint max_players = (auint)ntohs(pkt->max_players);
	uint32 peer_mask = ntohl(pkt->aux0);
	uint32 local_mask;
	uint32 active_mask;

	if ((netplay_cfg.build_id != 0U) && (build_id != 0U) && (build_id != netplay_cfg.build_id)){
		netplay_set_incompat("build ID mismatch");
		return FALSE;
	}
	if (feature_flags != netplay_cfg.feature_flags){
		netplay_set_incompat("feature flags mismatch");
		return FALSE;
	}
	if ((max_players != 0U) && (netplay_cfg.max_players != 0U) && (max_players != netplay_cfg.max_players)){
		netplay_set_incompat("max players mismatch");
		return FALSE;
	}
	if (max_players == 0U){
		max_players = netplay_cfg.max_players;
	}
	if (max_players == 0U){
		max_players = 1U;
	}
	if (max_players > ROLLBACK_MAX_PLAYERS){
		netplay_set_incompat("max players exceeds rollback limit");
		return FALSE;
	}
	active_mask = netplay_active_player_mask(max_players);
	local_mask = netplay_sanitize_player_mask(netplay_cfg.local_player_mask, max_players);
	if (peer_mask == 0U){
		peer_mask = netplay_mask_for_player(peer_player);
	}
	peer_mask &= active_mask;
	/*
	 * Ownership masks are only hints during the initial HELLO exchange. The
	 * authoritative seat map is negotiated in the lobby, so do not reject the
	 * connection just because both endpoints still advertise the same default
	 * local player mask.
	 */
	if (peer_mask == 0U){
		peer_mask = (active_mask & (~local_mask));
		if (peer_mask == 0U){
			peer_mask = netplay_mask_for_player((peer_player != 0U) ? peer_player : 1U);
			peer_mask &= active_mask;
		}
	}
	if ((pkt->version == NETPLAY_VERSION) && ((feature_flags & NETPLAY_FEATURE_COMPAT) == 0U)){
		netplay_set_incompat("peer lacks compatibility support");
		return FALSE;
	}

	netplay_state.peer_build_id = build_id;
	netplay_state.peer_feature_flags = feature_flags;
	netplay_state.peer_player = netplay_primary_player_from_mask(peer_mask, max_players);
	netplay_state.peer_player_mask = peer_mask;
	netplay_state.peer_max_players = max_players;
	netplay_state.compat_ok = TRUE;
	netplay_state.compat_reason[0] = 0;
	return TRUE;
}

static void netplay_init_pkt(netplay_pkt_t *pkt, uint8 type)
{
	uint8 flags = 0U;
	memset(pkt, 0, sizeof(*pkt));
	if (netplay_cfg.rom_present){ flags |= NETPLAY_PKT_FLAG_ROM_PRESENT; }
	if (netplay_cfg.rom_sendable){ flags |= NETPLAY_PKT_FLAG_ROM_SENDABLE; }
	pkt->magic = htonl(NETPLAY_MAGIC);
	pkt->session = htonl((type == NETPLAY_TYPE_HELLO_ACK) ? netplay_state.peer_session : netplay_state.local_session);
	pkt->seq = htonl(netplay_state.tx_seq++);
	pkt->rom_crc = htonl(netplay_cfg.rom_crc);
	pkt->rom_size = htonl(netplay_cfg.rom_size);
	pkt->build_id = htonl(netplay_cfg.build_id);
	pkt->feature_flags = htonl(netplay_cfg.feature_flags);
	pkt->aux0 = htonl(netplay_cfg.local_player_mask);
	pkt->max_players = htons((uint16)netplay_cfg.max_players);
	pkt->version = NETPLAY_VERSION;
	pkt->type = type;
	pkt->player = (uint8)netplay_primary_player_from_mask(netplay_cfg.local_player_mask, netplay_cfg.max_players);
	pkt->flags = flags;
	pkt->sync_mode = (uint8)netplay_cfg.rom_sync_mode;
	pkt->transfer_port = htons((uint16)netplay_state.rom_listen_port);
	strncpy(pkt->rom_name, netplay_cfg.rom_name, sizeof(pkt->rom_name) - 1U);
	pkt->rom_name[sizeof(pkt->rom_name) - 1U] = 0;
}

static boole netplay_send_relay_control(uint8 type, uint8 flags, void const *payload, auint payload_len)
{
	uint8 buf[sizeof(uzrl_header_t) + UZRL_MAX_RELAY_PAYLOAD];
	uzrl_header_t hdr;
	int sent;
	if ((!netplay_state.enabled) || (netplay_state.sock == INVALID_SOCKET) || (!netplay_state.relay_mode) || (netplay_state.relay_addr_len == 0U)){
		return FALSE;
	}
	if (payload_len > UZRL_MAX_RELAY_PAYLOAD){
		return FALSE;
	}
	memset(&hdr, 0, sizeof(hdr));
	hdr.magic_be = htonl(UZRL_MAGIC_U32);
	hdr.version = UZRL_VERSION;
	hdr.type = type;
	hdr.flags = flags;
	hdr.header_len = (uint8)sizeof(hdr);
	hdr.session_id_be = htonl(netplay_state.relay_session_id);
	hdr.room_id_be = htonl(netplay_state.relay_room_id);
	hdr.seq_be = htonl(netplay_state.relay_tx_seq++);
	hdr.payload_len_be = htons((uint16)payload_len);
	memcpy(buf, &hdr, sizeof(hdr));
	if ((payload != NULL) && (payload_len != 0U)){
		memcpy(buf + sizeof(hdr), payload, payload_len);
	}
	sent = (int)sendto(netplay_state.sock,
		(char const*)buf,
		(int)(sizeof(hdr) + payload_len),
		0,
		(struct sockaddr*)&(netplay_state.relay_addr),
		netplay_state.relay_addr_len);
	return (sent == (int)(sizeof(hdr) + payload_len));
}

static boole netplay_send_relay_request(uint8 type, char const *room_code)
{
	uzrl_room_req_t req;
	memset(&req, 0, sizeof(req));
	if ((room_code != NULL) && (room_code[0] != 0)){
		strncpy(req.room_code, room_code, sizeof(req.room_code));
		req.room_code[sizeof(req.room_code) - 1U] = 0;
	}
	req.app_id_be = htonl(netplay_cfg.rom_crc);
	strncpy(req.name, netplay_cfg.local_name[0] ? netplay_cfg.local_name : "Player", sizeof(req.name));
	req.name[sizeof(req.name) - 1U] = 0;
	req.want_direct = netplay_cfg.relay_allow_direct ? 1U : 0U;
	req.want_relay = netplay_cfg.relay_allow_relay ? 1U : 0U;
	if (type == UZRLT_CREATE_REQ){
		uzrl_room_req_v2_t req2;
		memset(&req2, 0, sizeof(req2));
		req2.base = req;
		req2.room_flags = netplay_state.relay_room_public ? UZRL_FLAG_PUBLIC_ROOM : 0U;
		if (netplay_state.relay_game_title[0] != 0){
			strncpy(req2.game_title, netplay_state.relay_game_title, sizeof(req2.game_title) - 1U);
			req2.game_title[sizeof(req2.game_title) - 1U] = 0;
		}
		return netplay_send_relay_control(type, 0U, &req2, sizeof(req2));
	}
	return netplay_send_relay_control(type, 0U, &req, sizeof(req));
}

static boole netplay_send_relay_heartbeat(void)
{
	uint32 echo = htonl(netplay_now_ms());
	return netplay_send_relay_control(UZRLT_HEARTBEAT, 0U, &echo, sizeof(echo));
}

static boole netplay_send_relay_leave(void)
{
	return netplay_send_relay_control(UZRLT_LEAVE, 0U, NULL, 0U);
}

static boole netplay_send_direct_raw(netplay_pkt_t const *pkt)
{
	int sent;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid) || (netplay_state.sock == INVALID_SOCKET)){
		return FALSE;
	}
	sent = (int)sendto(netplay_state.sock,
		(char const*)pkt,
		(int)sizeof(*pkt),
		0,
		(struct sockaddr*)&(netplay_state.peer_addr),
		netplay_state.peer_addr_len);
	return (sent == (int)sizeof(*pkt));
}

static boole netplay_send_direct_probe(void)
{
	netplay_pkt_t pkt;
	if ((!netplay_state.relay_mode) || (!netplay_state.relay_direct_candidate) || (netplay_state.relay_direct_established)){
		return FALSE;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_HELLO);
	return netplay_send_direct_raw(&pkt);
}

static void netplay_mark_direct_established(void)
{
	if ((!netplay_state.relay_mode) || (netplay_state.relay_direct_established)){
		return;
	}
	netplay_state.relay_direct_established = TRUE;
	strncpy(netplay_state.last_notice, "direct udp established", sizeof(netplay_state.last_notice) - 1U);
	netplay_state.last_notice[sizeof(netplay_state.last_notice) - 1U] = 0;
	print_message("Netplay: direct UDP established via relay rendezvous\n");
	if ((!netplay_state.relay_punch_ok_sent) && (netplay_state.relay_session_id != 0U)){
		if (netplay_send_relay_control(UZRLT_PUNCH_OK, 0U, NULL, 0U)){
			netplay_state.relay_punch_ok_sent = TRUE;
		}
	}
}


static uint32 netplay_pack_u32_from_bytes(uint8 const *src)
{
	return ((uint32)src[0]) | (((uint32)src[1]) << 8U) | (((uint32)src[2]) << 16U) | (((uint32)src[3]) << 24U);
}

static void netplay_unpack_u32_to_bytes(uint32 value, uint8 *dst)
{
	dst[0] = (uint8)(value & 0xFFU);
	dst[1] = (uint8)((value >> 8U) & 0xFFU);
	dst[2] = (uint8)((value >> 16U) & 0xFFU);
	dst[3] = (uint8)((value >> 24U) & 0xFFU);
}

static boole netplay_send_raw(netplay_pkt_t const *pkt)
{
	int sent;
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid)){
		return FALSE;
	}
	if (netplay_state.relay_mode && (!netplay_state.relay_direct_established)){
		return netplay_send_relay_control(UZRLT_RELAY_DATA, 0U, pkt, sizeof(*pkt));
	}
	sent = (int)sendto(netplay_state.sock,
		(char const*)pkt,
		(int)sizeof(*pkt),
		0,
		(struct sockaddr*)&(netplay_state.peer_addr),
		netplay_state.peer_addr_len);
	return (sent == (int)sizeof(*pkt));
}

static boole netplay_send_pkt(uint8 type, auint frame, auint player, auint buttons)
{
	netplay_pkt_t pkt;
	boole ok;
	netplay_init_pkt(&pkt, type);
	pkt.frame = htonl((uint32)frame);
	pkt.buttons = htonl((uint32)buttons);
	if (type == NETPLAY_TYPE_INPUT){
		pkt.player = (uint8)player;
		(void)netplay_pack_local_input_history(player, frame, &pkt);
	}
	ok = netplay_send_raw(&pkt);
	if (ok && (type == NETPLAY_TYPE_INPUT)){
		(void)netplay_send_device_history(frame);
	}
	return ok;
}

static boole netplay_send_disconnect_pkt(void)
{
	netplay_pkt_t pkt;
	netplay_init_pkt(&pkt, NETPLAY_TYPE_DISCONNECT);
	pkt.result = 1U;
	return netplay_send_raw(&pkt);
}

static void netplay_drop_connection(char const *reason)
{
	if ((reason != NULL) && (reason[0] != 0)){
		strncpy(netplay_state.last_notice, reason, sizeof(netplay_state.last_notice) - 1U);
		netplay_state.last_notice[sizeof(netplay_state.last_notice) - 1U] = 0;
		print_message("Netplay: %s\n", reason);
	}
	netplay_disconnect();
}

static boole netplay_send_caps(void)
{
	netplay_pkt_t pkt;
	netplay_refresh_local_rom_info();
	netplay_init_pkt(&pkt, NETPLAY_TYPE_CAPS);
	pkt.aux0 = htonl(mainui_get_input_topology_hash());
	if (netplay_state.mode == NETPLAY_MODE_SERVER){
		netplay_state.local_ready = TRUE;
	}
	if (netplay_send_raw(&pkt)){
		netplay_state.caps_sent = TRUE;
		return TRUE;
	}
	return FALSE;
}

static boole netplay_send_rom_ready(uint8 result)
{
	netplay_pkt_t pkt;
	netplay_refresh_local_rom_info();
	netplay_init_pkt(&pkt, NETPLAY_TYPE_ROM_READY);
	pkt.result = result;
	pkt.rom_crc = htonl(netplay_cfg.rom_crc);
	pkt.rom_size = htonl(netplay_cfg.rom_size);
	strncpy(pkt.rom_name, netplay_cfg.rom_name, sizeof(pkt.rom_name) - 1U);
	pkt.rom_name[sizeof(pkt.rom_name) - 1U] = 0;
	return netplay_send_raw(&pkt);
}

static boole netplay_send_session_start(void)
{
	netplay_pkt_t pkt;
	netplay_init_pkt(&pkt, NETPLAY_TYPE_SESSION_START);
	pkt.result = 1U;
	if (netplay_send_raw(&pkt)){
		netplay_state.session_started = TRUE;
		print_message("Netplay: session start granted.\n");
		return TRUE;
	}
	return FALSE;
}

static boole netplay_peer_matches(struct sockaddr const *from, socklen_t from_len)
{
	if (!netplay_state.peer_valid){
		return FALSE;
	}
	return netplay_addr_equal((struct sockaddr const*)&(netplay_state.peer_addr), netplay_state.peer_addr_len, from, from_len);
}

static void netplay_accept_peer(struct sockaddr const *addr, socklen_t addrlen)
{
	if (addr == NULL){
		return;
	}
	netplay_store_peer(addr, addrlen);
	netplay_update_route_local_addr(addr, addrlen, netplay_state.sock_family, netplay_cfg.network_interface);
	netplay_state.connected = TRUE;
	netplay_state.last_notice[0] = 0;
	netplay_state.last_rx_ms = netplay_now_ms();
}

static boole netplay_tcp_open_listener(void)
{
	SOCKET sock = INVALID_SOCKET;
	int family = (netplay_state.sock_family == AF_INET6) ? AF_INET6 : AF_INET;
	if (netplay_state.rom_listen_sock != INVALID_SOCKET){
		return TRUE;
	}
	sock = socket(family, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET){
		return FALSE;
	}
#if defined(IPV6_V6ONLY)
	if (family == AF_INET6){
		int off = 0;
		(void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char const*)&off, sizeof(off));
	}
#endif
	if (family == AF_INET6){
		struct sockaddr_in6 addr6;
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_addr = in6addr_any;
		if ((netplay_cfg.network_interface[0] != 0) && (netplay_inet_pton_compat(AF_INET6, netplay_cfg.network_interface, &addr6.sin6_addr) != 1)){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
		addr6.sin6_port = htons((uint16)netplay_cfg.rom_tcp_port);
		if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) == SOCKET_ERROR){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
	}else{
		struct sockaddr_in addr4;
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = AF_INET;
		addr4.sin_addr.s_addr = htonl(INADDR_ANY);
		if ((netplay_cfg.network_interface[0] != 0) && (netplay_inet_pton_compat(AF_INET, netplay_cfg.network_interface, &addr4.sin_addr) != 1)){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
		addr4.sin_port = htons((uint16)netplay_cfg.rom_tcp_port);
		if (bind(sock, (struct sockaddr*)&addr4, sizeof(addr4)) == SOCKET_ERROR){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
	}
	if (listen(sock, 1) == SOCKET_ERROR){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	netplay_set_nonblocking(sock);
	netplay_state.rom_listen_sock = sock;
	if (family == AF_INET6){
		struct sockaddr_in6 addr6;
		socklen_t len = (socklen_t)sizeof(addr6);
		if (getsockname(sock, (struct sockaddr*)&addr6, &len) == 0){
			netplay_state.rom_listen_port = ntohs(addr6.sin6_port);
		}
	}else{
		struct sockaddr_in addr4;
		socklen_t len = (socklen_t)sizeof(addr4);
		if (getsockname(sock, (struct sockaddr*)&addr4, &len) == 0){
			netplay_state.rom_listen_port = ntohs(addr4.sin_port);
		}
	}
	return (netplay_state.rom_listen_port != 0U);
}

static boole netplay_tcp_send_all(SOCKET sock, uint8 const *data, auint size)
{
	auint done = 0U;
	while (done < size){
		int rv = send(sock, (char const*)(data + done), (int)(size - done), 0);
		if (rv <= 0){
			return FALSE;
		}
		done += (auint)rv;
	}
	return TRUE;
}

static boole netplay_tcp_recv_all(SOCKET sock, uint8 *data, auint size)
{
	auint done = 0U;
	while (done < size){
		int rv = recv(sock, (char*)(data + done), (int)(size - done), 0);
		if (rv <= 0){
			return FALSE;
		}
		done += (auint)rv;
	}
	return TRUE;
}

static boole netplay_tcp_receive_rom(char const *host, auint port, uint32 expected_crc, uint32 expected_size, char const *expected_name)
{
	struct sockaddr_storage peer_addr;
	socklen_t peer_len;
	SOCKET sock;
	netplay_rom_tcp_header_t hdr;
	uint8* img;
	boole ok;
	int family;
	uint32 hdr_size;
	uint32 hdr_crc;

	if (!netplay_cfg.rom_receive_enabled){
		netplay_set_error("ROM receive disabled");
		return FALSE;
	}
	if (!netplay_resolve_peer(host, port, AF_UNSPEC, SOCK_STREAM, &peer_addr, &peer_len)){
		netplay_set_error("ROM TCP resolve failed");
		return FALSE;
	}
	family = ((struct sockaddr const*)&peer_addr)->sa_family;
	sock = socket(family, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET){
		netplay_set_error("ROM TCP socket failed");
		return FALSE;
	}
	if (connect(sock, (struct sockaddr*)&peer_addr, peer_len) == SOCKET_ERROR){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM TCP connect failed");
		return FALSE;
	}
	if (!netplay_tcp_recv_all(sock, (uint8*)&hdr, (auint)sizeof(hdr))){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM TCP header receive failed");
		return FALSE;
	}
	if (ntohl(hdr.magic) != NETPLAY_ROM_TCP_MAGIC){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM TCP header magic mismatch");
		return FALSE;
	}
	if (ntohs(hdr.version) != NETPLAY_ROM_TCP_VERSION){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM TCP header version mismatch");
		return FALSE;
	}
	hdr_size = ntohl(hdr.rom_size);
	hdr_crc = ntohl(hdr.rom_crc);
	if ((hdr_size == 0U) || (hdr_size > netplay_cfg.rom_max_size)){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM image too large");
		return FALSE;
	}
	if ((expected_size != 0U) && (hdr_size != expected_size)){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM size mismatch");
		return FALSE;
	}
	if ((expected_crc != 0U) && (hdr_crc != expected_crc)){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM CRC mismatch");
		return FALSE;
	}
	img = (uint8*)malloc(hdr_size);
	if (img == NULL){
		NETPLAY_CLOSESOCK(sock);
		netplay_set_error("ROM allocation failed");
		return FALSE;
	}
	ok = netplay_tcp_recv_all(sock, img, (auint)hdr_size);
	NETPLAY_CLOSESOCK(sock);
	if (!ok){
		free(img);
		netplay_set_error("ROM TCP receive failed");
		return FALSE;
	}
	ok = main_load_rom_from_memory(img, hdr_size, hdr.rom_name[0] ? hdr.rom_name : expected_name);
	free(img);
	if (!ok){
		netplay_set_error("ROM load failed");
		return FALSE;
	}
	netplay_refresh_local_rom_info();
	if ((expected_crc != 0U) && (netplay_cfg.rom_crc != expected_crc)){
		netplay_set_error("Loaded ROM CRC mismatch");
		return FALSE;
	}
	return TRUE;
}

static void netplay_tcp_service_sender(void)
{
	SOCKET client;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	uint8* img;
	uint32 img_size;
	uint32 img_crc;
	char   img_name[64];
	netplay_rom_tcp_header_t hdr;

	if (netplay_state.rom_listen_sock == INVALID_SOCKET){
		return;
	}
	addrlen = (socklen_t)sizeof(addr);
	client = accept(netplay_state.rom_listen_sock, (struct sockaddr*)&addr, &addrlen);
	if (client == INVALID_SOCKET){
		return;
	}
	if (!main_get_loaded_rom_image(&img, &img_size, &img_crc, &(img_name[0]), (auint)sizeof(img_name))){
		NETPLAY_CLOSESOCK(client);
		return;
	}
	memset(&hdr, 0, sizeof(hdr));
	hdr.magic = htonl(NETPLAY_ROM_TCP_MAGIC);
	hdr.version = htons(NETPLAY_ROM_TCP_VERSION);
	hdr.rom_size = htonl(img_size);
	hdr.rom_crc = htonl(img_crc);
	strncpy(hdr.rom_name, img_name, sizeof(hdr.rom_name) - 1U);
	hdr.rom_name[sizeof(hdr.rom_name) - 1U] = 0;
	if (!netplay_tcp_send_all(client, (uint8 const*)&hdr, (auint)sizeof(hdr)) || !netplay_tcp_send_all(client, img, img_size)){
		print_error("Netplay: ROM TCP send failed.\n");
	}
	free(img);
	NETPLAY_CLOSESOCK(client);
}

static void netplay_send_rom_error(char const *reason)
{
	netplay_pkt_t pkt;
	netplay_init_pkt(&pkt, NETPLAY_TYPE_ROM_ERROR);
	pkt.result = 1U;
	if (reason != NULL){
		strncpy(pkt.rom_name, reason, sizeof(pkt.rom_name) - 1U);
		pkt.rom_name[sizeof(pkt.rom_name) - 1U] = 0;
	}
	(void)netplay_send_raw(&pkt);
}

static void netplay_try_finish_bootstrap(void)
{
	if ((netplay_state.mode == NETPLAY_MODE_SERVER) && netplay_state.local_ready && netplay_state.peer_ready){
		netplay_lobby_mark_dirty();
		if (netplay_state.peer_valid){
			(void)netplay_send_lobby_state();
		}
	}
}

static void netplay_handle_caps(netplay_pkt_t const *pkt)
{
	boole need_transfer = FALSE;
	uint32 peer_topology_hash = ntohl(pkt->aux0);
	uint32 local_topology_hash = mainui_get_input_topology_hash();
	netplay_state.peer_caps = TRUE;
	if ((peer_topology_hash != 0U) && (local_topology_hash != 0U) && (peer_topology_hash != local_topology_hash)){
		netplay_set_error("input topology mismatch");
		netplay_disconnect();
		return;
	}
	netplay_state.peer_rom_crc = ntohl(pkt->rom_crc);
	netplay_state.peer_rom_size = ntohl(pkt->rom_size);
	netplay_state.peer_rom_present = ((pkt->flags & NETPLAY_PKT_FLAG_ROM_PRESENT) != 0U);
	netplay_state.peer_rom_sendable = ((pkt->flags & NETPLAY_PKT_FLAG_ROM_SENDABLE) != 0U);
	netplay_state.peer_sync_mode = (netplay_rom_sync_mode_t)pkt->sync_mode;
	netplay_state.peer_transfer_port = ntohs(pkt->transfer_port);
	strncpy(netplay_state.peer_rom_name, pkt->rom_name, sizeof(netplay_state.peer_rom_name) - 1U);
	netplay_state.peer_rom_name[sizeof(netplay_state.peer_rom_name) - 1U] = 0;

	if (netplay_state.mode == NETPLAY_MODE_CLIENT){
		switch (netplay_state.peer_sync_mode){
			case NETPLAY_ROM_SYNC_OFF:
				need_transfer = FALSE;
				break;
			case NETPLAY_ROM_SYNC_MISSING:
				need_transfer = !netplay_cfg.rom_present;
				break;
			case NETPLAY_ROM_SYNC_MISMATCH:
				need_transfer = (!netplay_cfg.rom_present) ||
				                (netplay_cfg.rom_crc != netplay_state.peer_rom_crc);
				break;
			case NETPLAY_ROM_SYNC_ALWAYS:
			default:
				need_transfer = TRUE;
				break;
		}
		netplay_state.rom_transfer_needed = need_transfer;
		if (need_transfer){
			if (!netplay_cfg.rom_receive_enabled){
				netplay_set_error("ROM sync required but receive disabled");
				netplay_send_rom_error("receive disabled");
				return;
			}
			if (!netplay_state.rom_request_sent){
				(void)netplay_send_pkt(NETPLAY_TYPE_ROM_REQUEST, 0U, 0U, 0U);
				netplay_state.rom_request_sent = TRUE;
				print_message("Netplay: requesting ROM sync from host.\n");
			}
		}else{
			netplay_state.local_ready = TRUE;
			(void)netplay_send_rom_ready(1U);
		}
	}else{
		netplay_try_finish_bootstrap();
	}
}

static void netplay_handle_rom_request(void)
{
	netplay_pkt_t pkt;
	if (netplay_state.mode != NETPLAY_MODE_SERVER){
		return;
	}
	if (!netplay_cfg.rom_send_enabled){
		netplay_send_rom_error("send disabled");
		return;
	}
	if (!netplay_cfg.rom_sendable){
		netplay_send_rom_error("no sendable UZE ROM");
		return;
	}
	if (!netplay_tcp_open_listener()){
		netplay_send_rom_error("tcp listener failed");
		return;
	}
	netplay_init_pkt(&pkt, NETPLAY_TYPE_ROM_OFFER);
	pkt.transfer_port = htons((uint16)netplay_state.rom_listen_port);
	pkt.rom_crc = htonl(netplay_cfg.rom_crc);
	pkt.rom_size = htonl(netplay_cfg.rom_size);
	strncpy(pkt.rom_name, netplay_cfg.rom_name, sizeof(pkt.rom_name) - 1U);
	pkt.rom_name[sizeof(pkt.rom_name) - 1U] = 0;
	(void)netplay_send_raw(&pkt);
	print_message("Netplay: offering ROM sync on TCP port %u.\n", (unsigned)netplay_state.rom_listen_port);
}

static void netplay_handle_rom_offer(netplay_pkt_t const *pkt)
{
	uint32 offer_crc = ntohl(pkt->rom_crc);
	uint32 offer_size = ntohl(pkt->rom_size);
	auint  offer_port = ntohs(pkt->transfer_port);
	if (netplay_state.mode != NETPLAY_MODE_CLIENT){
		return;
	}
	if (offer_port == 0U){
		netplay_set_error("ROM offer missing TCP port");
		return;
	}
	if (netplay_tcp_receive_rom(netplay_state.peer_host, offer_port, offer_crc, offer_size, pkt->rom_name)){
		netplay_state.local_ready = TRUE;
		netplay_state.rom_transfer_needed = FALSE;
		(void)netplay_send_rom_ready(1U);
		print_message("Netplay: ROM sync complete.\n");
	}else{
		netplay_send_rom_error("rom transfer failed");
	}
}

static void netplay_handle_rom_ready(netplay_pkt_t const *pkt)
{
	uint32 ready_crc = ntohl(pkt->rom_crc);
	if (pkt->result == 0U){
		netplay_set_error("peer reported ROM sync failure");
		return;
	}
	if (netplay_state.mode == NETPLAY_MODE_SERVER){
		if (netplay_cfg.rom_crc != 0U){
			if ((ready_crc != 0U) && (ready_crc != netplay_cfg.rom_crc)){
				netplay_set_error("peer loaded wrong ROM CRC");
				return;
			}
		}
		netplay_state.peer_ready = TRUE;
		netplay_try_finish_bootstrap();
	}
}

static void netplay_handle_session_start(void)
{
	netplay_state.local_ready = TRUE;
	netplay_state.peer_ready = TRUE;
	if (!netplay_state.session_started){
		netplay_state.session_started = TRUE;
		netplay_state.last_notice[0] = 0;
		print_message("Netplay: session started.\n");
	}
}

static void netplay_handle_lobby_state(netplay_pkt_t const *pkt)
{
	uint8 owners[ROLLBACK_MAX_PLAYERS];
	uint8 pads[ROLLBACK_MAX_PLAYERS];
	boole host_ready = FALSE;
	boole guest_ready = FALSE;
	auint i;
	if (netplay_state.mode != NETPLAY_MODE_CLIENT){
		return;
	}
	if (!netplay_lobby_decode(pkt->rom_name, owners, pads, &host_ready, &guest_ready)){
		return;
	}
	for (i = 0U; i < netplay_cfg.max_players; i++){
		netplay_state.lobby_owner_peer[i] = owners[i];
		netplay_state.lobby_pad_index[i] = pads[i];
	}
	netplay_state.lobby_local_ready = guest_ready;
	netplay_state.lobby_peer_ready = host_ready;
	netplay_state.lobby_valid = netplay_lobby_seat_map_valid();
	netplay_state.peer_player_mask = ntohl(pkt->aux0);
}

static void netplay_handle_lobby_ready(netplay_pkt_t const *pkt)
{
	if (netplay_state.mode != NETPLAY_MODE_SERVER){
		return;
	}
	netplay_state.lobby_peer_ready = (pkt->result != 0U) ? TRUE : FALSE;
	netplay_lobby_mark_dirty();
	(void)netplay_send_lobby_state();
}

static void netplay_handle_lobby_chat(netplay_pkt_t const *pkt)
{
	char line[NETPLAY_LOBBY_CHAT_LINE_CHARS];
	snprintf(line, sizeof(line), "%s: %s",
			(netplay_state.peer_name[0] != 0) ? netplay_state.peer_name : ((netplay_state.mode == NETPLAY_MODE_SERVER) ? "Guest" : "Host"),
			pkt->rom_name[0] ? pkt->rom_name : "");
	netplay_chat_add_line(line);
}

static void netplay_handle_lobby_request(netplay_pkt_t const *pkt)
{
	auint seat;
	auint owner_peer;
	auint pad_index;
	char note[128];
	if (netplay_state.mode != NETPLAY_MODE_SERVER){
		return;
	}
	seat = (auint)ntohl(pkt->frame);
	owner_peer = (auint)ntohl(pkt->aux0);
	pad_index = (auint)ntohl(pkt->buttons);
	if (seat >= netplay_cfg.max_players){
		return;
	}
	if ((owner_peer != NETPLAY_LOBBY_OWNER_GUEST) && (owner_peer != NETPLAY_LOBBY_OWNER_NONE)){
		return;
	}
	if ((owner_peer != NETPLAY_LOBBY_OWNER_NONE) && (pad_index >= ROLLBACK_MAX_PLAYERS)){
		return;
	}
	netplay_state.lobby_request_valid[seat] = TRUE;
	netplay_state.lobby_request_owner_peer[seat] = (uint8)owner_peer;
	netplay_state.lobby_request_pad_index[seat] = (uint8)((owner_peer == NETPLAY_LOBBY_OWNER_NONE) ? 0U : pad_index);
	if (owner_peer == NETPLAY_LOBBY_OWNER_NONE){
		snprintf(note, sizeof(note), "System: %s requested release of P%u", netplay_state.peer_name[0] ? netplay_state.peer_name : "Guest", (unsigned)(seat + 1U));
	}else{
		snprintf(note, sizeof(note), "System: %s requested P%u -> %s", netplay_state.peer_name[0] ? netplay_state.peer_name : "Guest", (unsigned)(seat + 1U), netplay_state.peer_pad_labels[pad_index][0] ? netplay_state.peer_pad_labels[pad_index] : "Guest Pad");
	}
	netplay_lobby_note_local(note);
}

static void netplay_handle_lobby_labels(netplay_pkt_t const *pkt)
{
	if (pkt->result == 1U){
		if (netplay_state.mode == NETPLAY_MODE_CLIENT){
			netplay_copy_name_field(netplay_cfg.local_name, sizeof(netplay_cfg.local_name), pkt->rom_name, "Player");
			netplay_mark_labels_dirty();
			print_message("Netplay: host renamed local player to %s.\n", netplay_cfg.local_name);
		}
		return;
	}
	netplay_unpack_label_fields(pkt->rom_name, netplay_state.peer_name, netplay_state.peer_pad_labels);
	if (netplay_state.mode == NETPLAY_MODE_SERVER){
		char forced_name[NETPLAY_LOBBY_NAME_LEN + 1U];
		netplay_make_unique_name(forced_name, sizeof(forced_name), netplay_state.peer_name, netplay_cfg.local_name, NULL);
		if (!netplay_name_equals_ci(forced_name, netplay_state.peer_name)){
			char note[128];
			netplay_copy_name_field(netplay_state.peer_name, sizeof(netplay_state.peer_name), forced_name, "Player2");
			(void)netplay_send_name_override_pkt(netplay_state.peer_name);
			snprintf(note, sizeof(note), "[system] Guest renamed to %s to avoid duplicate names", netplay_state.peer_name);
			netplay_lobby_note_local(note);
		}
	}
}

static void netplay_handle_ping(netplay_pkt_t const *pkt)
{
	(void)netplay_send_pong(ntohl(pkt->frame));
}

static void netplay_handle_pong(netplay_pkt_t const *pkt)
{
	uint32 token = ntohl(pkt->frame);
	uint32 now = netplay_now_ms();
	uint32 sample;
	if (token != netplay_state.ping_token){
		return;
	}
	sample = netplay_ms_delta(now, token);
	netplay_state.ping_ms = sample;
	if (netplay_state.ping_smoothed_ms == 0U){
		netplay_state.ping_smoothed_ms = sample;
		netplay_state.ping_jitter_ms = 0U;
	}else{
		uint32 prev = netplay_state.ping_smoothed_ms;
		uint32 diff = (prev > sample) ? (prev - sample) : (sample - prev);
		netplay_state.ping_smoothed_ms = ((prev * 3U) + sample) / 4U;
		netplay_state.ping_jitter_ms = ((netplay_state.ping_jitter_ms * 3U) + diff) / 4U;
	}
}

static void netplay_handle_pkt(netplay_pkt_t const *pkt, struct sockaddr const *from, socklen_t from_len, boole via_relay)
{
	uint32 seq;
	uint32 session;
	auint frame;
	auint buttons;

	if (ntohl(pkt->magic) != NETPLAY_MAGIC){
		return;
	}
	if (pkt->version != NETPLAY_VERSION){
		netplay_set_incompat("protocol version mismatch");
		return;
	}
	if (!via_relay){
		if ((netplay_state.mode == NETPLAY_MODE_CLIENT) && netplay_state.peer_valid && (!netplay_peer_matches(from, from_len))){
			return;
		}
		if ((netplay_state.mode == NETPLAY_MODE_SERVER) && netplay_state.peer_valid && netplay_state.connected && (!netplay_peer_matches(from, from_len))){
			return;
		}
	}
	if ((pkt->type == NETPLAY_TYPE_HELLO) || (pkt->type == NETPLAY_TYPE_HELLO_ACK)){
		if (!netplay_check_compat(pkt)){
			return;
		}
	}

	seq = ntohl(pkt->seq);
	if (netplay_state.rx_seq_init){
		if (seq == netplay_state.rx_last_seq){
			netplay_state.rx_dup_count++;
			return;
		}
		if (seq < netplay_state.rx_last_seq){
			netplay_state.rx_ooo_count++;
			return;
		}
		if (seq > (netplay_state.rx_last_seq + 1U)){
			netplay_state.rx_gap_count++;
		}
	}
	netplay_state.rx_seq_init = TRUE;
	netplay_state.rx_last_seq = seq;

	if (!via_relay){
		if ((!netplay_state.peer_valid) || (!netplay_peer_matches(from, from_len))){
			netplay_accept_peer(from, from_len);
		}
		if (netplay_state.relay_mode){
			netplay_mark_direct_established();
		}
	}else{
		netplay_state.peer_valid = TRUE;
		netplay_state.connected = TRUE;
	}
	netplay_state.last_rx_ms = netplay_now_ms();
	session = ntohl(pkt->session);

	switch (pkt->type){
		case NETPLAY_TYPE_HELLO:
			netplay_state.peer_session = session;
			netplay_state.connected = TRUE;
			(void)netplay_send_pkt(NETPLAY_TYPE_HELLO_ACK, 0U, 0U, 0U);
			break;
		case NETPLAY_TYPE_HELLO_ACK:
			if (session == netplay_state.local_session){
				netplay_state.hello_acked = TRUE;
				netplay_state.connected = TRUE;
			}
			break;
		case NETPLAY_TYPE_INPUT:
			if (!netplay_state.session_started){
				break;
			}
			if ((netplay_state.peer_session != 0U) && (session != netplay_state.peer_session)){
				break;
			}
			frame = (auint)ntohl(pkt->frame);
			buttons = (auint)ntohl(pkt->buttons);
			(void)rollback_queue_remote_input(frame, (auint)pkt->player, buttons);
			netplay_handle_input_history(pkt);
			break;
		case NETPLAY_TYPE_CAPS:
			netplay_handle_caps(pkt);
			break;
		case NETPLAY_TYPE_ROM_REQUEST:
			netplay_handle_rom_request();
			break;
		case NETPLAY_TYPE_ROM_OFFER:
			netplay_handle_rom_offer(pkt);
			break;
		case NETPLAY_TYPE_ROM_READY:
			netplay_handle_rom_ready(pkt);
			break;
		case NETPLAY_TYPE_SESSION_START:
			netplay_handle_session_start();
			break;
		case NETPLAY_TYPE_ROM_ERROR:
			netplay_set_error(pkt->rom_name[0] ? pkt->rom_name : "peer ROM sync error");
			break;
		case NETPLAY_TYPE_LOBBY_STATE:
			netplay_handle_lobby_state(pkt);
			break;
		case NETPLAY_TYPE_LOBBY_READY:
			netplay_handle_lobby_ready(pkt);
			break;
		case NETPLAY_TYPE_LOBBY_CHAT:
			netplay_handle_lobby_chat(pkt);
			break;
		case NETPLAY_TYPE_DEVICE_EVENT:
			if (netplay_state.session_started){
				rollback_device_event_t ev;
				frame = (auint)ntohl(pkt->frame);
				memset(&ev, 0, sizeof(ev));
				ev.seq = (uint16)ntohs(pkt->aux1);
				ev.port = (uint8)pkt->player;
				ev.slot = (uint8)pkt->sync_mode;
				ev.type = (uint8)pkt->result;
				ev.len = (uint8)(pkt->flags & 0x0FU);
				if (ev.len > ROLLBACK_DEVICE_EVENT_DATA_MAX){ ev.len = ROLLBACK_DEVICE_EVENT_DATA_MAX; }
				netplay_unpack_u32_to_bytes(ntohl(pkt->buttons), &(ev.data[0]));
				netplay_unpack_u32_to_bytes(ntohl(pkt->aux0), &(ev.data[4]));
				(void)rollback_queue_remote_device_event(frame, &ev);
			}
			break;
		case NETPLAY_TYPE_DEVICE_EVENT_HISTORY:
			if (netplay_state.session_started){
				netplay_handle_device_history(pkt);
			}
			break;
		case NETPLAY_TYPE_LOBBY_REQUEST:
			netplay_handle_lobby_request(pkt);
			break;
		case NETPLAY_TYPE_LOBBY_LABELS:
			netplay_handle_lobby_labels(pkt);
			break;
		case NETPLAY_TYPE_PING:
			netplay_handle_ping(pkt);
			break;
		case NETPLAY_TYPE_PONG:
			netplay_handle_pong(pkt);
			break;
		case NETPLAY_TYPE_DISCONNECT:
			netplay_drop_connection("peer disconnected");
			return;
		default:
			break;
	}
}


static boole netplay_decode_relay_peer_endpoint(void const *payload, auint plen, struct sockaddr_storage *out_addr, socklen_t *out_len, boole *direct_ok)
{
	uzrl_peer_info_t info;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	uint8 af = UZRL_AF_UNSPEC;
	uint8 alen = 0U;
	if (direct_ok != NULL){
		*direct_ok = FALSE;
	}
	if ((payload == NULL) || (out_addr == NULL) || (out_len == NULL) || (plen < NETPLAY_UZRL_PEER_INFO_LEGACY_SIZE)){
		return FALSE;
	}
	memset(&info, 0, sizeof(info));
	memcpy(&info, payload, (plen < (auint)sizeof(info)) ? plen : (auint)sizeof(info));
	if ((plen >= (auint)sizeof(info)) && (info.addr_family != UZRL_AF_UNSPEC) && (info.addr_len <= UZRL_ADDRLEN_MAX)){
		af = info.addr_family;
		alen = info.addr_len;
	}else if ((info.flags & UZRL_FLAG_PEER_IPV6) != 0U){
		af = UZRL_AF_IPV6;
		alen = 16U;
	}else if (info.peer_ipv4_be != 0U){
		af = UZRL_AF_IPV4;
		alen = 4U;
	}
	memset(out_addr, 0, sizeof(*out_addr));
	if ((af == UZRL_AF_IPV6) && (alen == 16U)){
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = info.peer_port_be;
		memcpy(&(addr6.sin6_addr), info.peer_addr, 16U);
		memcpy(out_addr, &addr6, sizeof(addr6));
		*out_len = (socklen_t)sizeof(addr6);
	}else if ((af == UZRL_AF_IPV4) && ((alen == 4U) || (info.peer_ipv4_be != 0U))){
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = AF_INET;
		addr4.sin_port = info.peer_port_be;
		if (alen == 4U){
			memcpy(&(addr4.sin_addr.s_addr), info.peer_addr, 4U);
		}else{
			addr4.sin_addr.s_addr = info.peer_ipv4_be;
		}
		memcpy(out_addr, &addr4, sizeof(addr4));
		*out_len = (socklen_t)sizeof(addr4);
	}else{
		return FALSE;
	}
	if (!netplay_prepare_peer_addr_for_socket(netplay_state.sock_family, out_addr, out_len)){
		return TRUE;
	}
	if (direct_ok != NULL){
		*direct_ok = netplay_socket_allows_addr(netplay_state.sock_family, out_addr);
	}
	return TRUE;
}

static void netplay_handle_relay_packet(uint8 const *buf, auint got, struct sockaddr const *from, socklen_t from_len)
{
	uzrl_header_t hdr;
	uint16 plen;
	uint8 const *payload;
	if ((!netplay_state.relay_mode) || (got < (auint)sizeof(hdr))){
		return;
	}
	memcpy(&hdr, buf, sizeof(hdr));
	if ((ntohl(hdr.magic_be) != UZRL_MAGIC_U32) || (hdr.version != UZRL_VERSION) || (hdr.header_len != sizeof(hdr))){
		return;
	}
	plen = ntohs(hdr.payload_len_be);
	if (((auint)hdr.header_len + (auint)plen) > got){
		return;
	}
	payload = buf + hdr.header_len;
	if ((netplay_state.relay_addr_len != 0U) && (!netplay_addr_equal((struct sockaddr const*)&(netplay_state.relay_addr), netplay_state.relay_addr_len, from, from_len))){
		return;
	}
	switch (hdr.type){
		case UZRLT_ROOM_CREATED:
		case UZRLT_ROOM_JOINED:
			if (plen >= sizeof(uzrl_room_resp_t)){
				uzrl_room_resp_t resp;
				memcpy(&resp, payload, sizeof(resp));
				netplay_state.relay_session_id = ntohl(resp.assigned_session_id_be);
				netplay_state.relay_room_id = ntohl(hdr.room_id_be);
				memcpy(netplay_state.relay_room_code, resp.room_code, UZRL_ROOM_CODE_LEN);
				netplay_state.relay_room_code[NETPLAY_RELAY_ROOM_CODE_LEN] = 0;
				netplay_state.relay_room_public = ((hdr.flags & UZRL_FLAG_PUBLIC_ROOM) != 0U) ? TRUE : FALSE;
				netplay_state.relay_room_ready = TRUE;
				netplay_state.relay_pending_req_type = 0U;
				netplay_state.last_notice[0] = 0;
				if (hdr.type == UZRLT_ROOM_CREATED){
					print_message("Netplay: relay room created: %s\n", netplay_state.relay_room_code);
				}else{
					print_message("Netplay: relay room joined: %s\n", netplay_state.relay_room_code);
				}
			}
			break;
		case UZRLT_PEER_INFO:
			if (plen >= NETPLAY_UZRL_PEER_INFO_LEGACY_SIZE){
				uzrl_peer_info_t info;
				struct sockaddr_storage peer_addr;
				socklen_t peer_len = 0;
				boole direct_ok = FALSE;
				memset(&info, 0, sizeof(info));
				memcpy(&info, payload, (plen < (auint)sizeof(info)) ? plen : (auint)sizeof(info));
				netplay_state.relay_peer_present = TRUE;
				netplay_state.relay_peer_direct_allowed = ((info.flags & UZRL_FLAG_ALLOW_DIRECT) != 0U) ? TRUE : FALSE;
				netplay_state.relay_peer_relay_allowed = ((info.flags & UZRL_FLAG_ALLOW_RELAY) != 0U) ? TRUE : FALSE;
				if (netplay_decode_relay_peer_endpoint(payload, plen, &peer_addr, &peer_len, &direct_ok)){
					netplay_store_peer((struct sockaddr const*)&peer_addr, peer_len);
				}else{
					netplay_state.peer_valid = FALSE;
					netplay_state.peer_addr_len = 0;
					netplay_state.peer_host[0] = 0;
					netplay_state.peer_port = 0U;
				}
				netplay_state.relay_direct_candidate = (netplay_cfg.relay_allow_direct && netplay_state.relay_peer_direct_allowed && direct_ok) ? TRUE : FALSE;
				strncpy(netplay_state.peer_name, info.peer_name, sizeof(netplay_state.peer_name) - 1U);
				netplay_state.peer_name[sizeof(netplay_state.peer_name) - 1U] = 0;
				netplay_state.last_rx_ms = netplay_now_ms();
				print_message("Netplay: relay peer ready: %s\n", netplay_state.peer_name[0] ? netplay_state.peer_name : netplay_state.peer_host);
				if (netplay_state.relay_direct_candidate){
					print_message("Netplay: attempting direct UDP punch to %s:%u\n", netplay_state.peer_host, (unsigned)netplay_state.peer_port);
				}else if (netplay_cfg.relay_allow_direct && netplay_state.relay_peer_direct_allowed){
					print_message("Netplay: direct UDP punch unavailable for peer address family; using relay\n");
				}
			}
			break;
		case UZRLT_PUNCH_STATUS:
			if (plen >= sizeof(uzrl_punch_status_t)){
				uzrl_punch_status_t st;
				memcpy(&st, payload, sizeof(st));
				if ((st.direct_established != 0U) && (!netplay_state.relay_direct_established)){
					strncpy(netplay_state.last_notice, "direct punch acknowledged", sizeof(netplay_state.last_notice) - 1U);
					netplay_state.last_notice[sizeof(netplay_state.last_notice) - 1U] = 0;
				}
			}
			break;
		case UZRLT_RELAY_FROM_PEER:
			if (plen == sizeof(netplay_pkt_t)){
				netplay_handle_pkt((netplay_pkt_t const*)payload, from, from_len, TRUE);
			}
			break;
		case UZRLT_PEER_LEFT:
			netplay_drop_connection("peer left relay room");
			return;
		case UZRLT_HEARTBEAT_ACK:
			break;
		case UZRLT_ERROR:
			if (plen >= sizeof(uzrl_error_t)){
				uzrl_error_t err;
				memcpy(&err, payload, sizeof(err));
				err.message[sizeof(err.message) - 1U] = 0;
				strncpy(netplay_state.last_notice, err.message[0] ? err.message : "relay error", sizeof(netplay_state.last_notice) - 1U);
				netplay_state.last_notice[sizeof(netplay_state.last_notice) - 1U] = 0;
				print_message("Netplay relay: %s\n", netplay_state.last_notice);
				if ((!netplay_state.connected) || (netplay_state.relay_pending_req_type != 0U)){
					netplay_disconnect();
				}
			}
			break;
		default:
			break;
	}
}

void netplay_init(void)
{
	netplay_load_cfg_defaults();
	memset(&netplay_state, 0, sizeof(netplay_state));
	netplay_state.sock = INVALID_SOCKET;
	netplay_state.rom_listen_sock = INVALID_SOCKET;
	netplay_state.sock_family = AF_UNSPEC;
	netplay_state.local_session = netplay_rand32();
	netplay_state.last_notice[0] = 0;
	if (netplay_state.local_session == 0U){
		netplay_state.local_session = 1U;
	}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2,2), &wsa) == 0){
			netplay_state.initialized = TRUE;
		}
	}
#else
	netplay_state.initialized = TRUE;
#endif
}

void netplay_shutdown(void)
{
	netplay_disconnect();
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if (netplay_state.initialized){
		WSACleanup();
	}
#endif
	memset(&netplay_state, 0, sizeof(netplay_state));
	netplay_state.sock = INVALID_SOCKET;
	netplay_state.rom_listen_sock = INVALID_SOCKET;
	netplay_state.sock_family = AF_UNSPEC;
}

void netplay_reset(void)
{
	auint i;
	netplay_close_rom_listener();
	netplay_state.hello_sent = FALSE;
	netplay_state.hello_acked = FALSE;
	netplay_state.connected = FALSE;
	netplay_state.compat_ok = FALSE;
	netplay_state.caps_sent = FALSE;
	netplay_state.peer_caps = FALSE;
	netplay_state.local_ready = FALSE;
	netplay_state.peer_ready = FALSE;
	netplay_state.session_started = FALSE;
	netplay_state.rom_request_sent = FALSE;
	netplay_state.rom_transfer_needed = FALSE;
	netplay_state.peer_session = 0U;
	netplay_state.relay_session_id = 0U;
	netplay_state.relay_room_id = 0U;
	netplay_state.relay_tx_seq = 0U;
	netplay_state.relay_room_ready = FALSE;
	netplay_state.relay_peer_present = FALSE;
	netplay_state.relay_direct_candidate = FALSE;
	netplay_state.relay_direct_established = FALSE;
	netplay_state.relay_punch_ok_sent = FALSE;
	netplay_state.relay_peer_direct_allowed = FALSE;
	netplay_state.relay_peer_relay_allowed = FALSE;
	netplay_state.relay_pending_req_type = 0U;
	netplay_state.tx_seq = 0U;
	netplay_state.rx_last_seq = 0U;
	netplay_state.rx_seq_init = FALSE;
	netplay_state.rx_dup_count = 0U;
	netplay_state.rx_ooo_count = 0U;
	netplay_state.rx_gap_count = 0U;
	netplay_state.peer_port = 0U;
	netplay_state.peer_host[0] = 0;
	netplay_state.relay_room_code[0] = 0;
	netplay_state.relay_room_public = TRUE;
	netplay_state.relay_game_title[0] = 0;
	netplay_state.compat_reason[0] = 0;
	netplay_state.peer_rom_crc = 0U;
	netplay_state.peer_rom_size = 0U;
	netplay_state.peer_build_id = 0U;
	netplay_state.peer_feature_flags = 0U;
	netplay_state.peer_player = 0U;
	netplay_state.peer_player_mask = 0U;
	netplay_state.peer_max_players = 0U;
	netplay_state.peer_rom_present = FALSE;
	netplay_state.peer_rom_sendable = FALSE;
	netplay_state.peer_sync_mode = NETPLAY_ROM_SYNC_OFF;
	netplay_state.peer_transfer_port = 0U;
	netplay_state.peer_rom_name[0] = 0;
	netplay_state.lobby_local_ready = FALSE;
	netplay_state.lobby_peer_ready = FALSE;
	netplay_state.lobby_valid = FALSE;
	netplay_state.lobby_dirty = FALSE;
	netplay_state.ping_token = 0U;
	netplay_state.ping_sent_ms = 0U;
	netplay_state.ping_ms = 0U;
	netplay_state.ping_smoothed_ms = 0U;
	netplay_state.ping_jitter_ms = 0U;
	netplay_state.last_ping_send_ms = 0U;
	netplay_state.last_lobby_send_ms = 0U;
	netplay_state.last_rx_ms = 0U;
	netplay_state.last_hello_send_ms = 0U;
	netplay_state.relay_last_heartbeat_ms = 0U;
	netplay_state.relay_last_req_send_ms = 0U;
	netplay_state.relay_last_punch_send_ms = 0U;
	memset(netplay_state.local_input_hist, 0, sizeof(netplay_state.local_input_hist));
	memset(netplay_state.local_input_hist_head, 0, sizeof(netplay_state.local_input_hist_head));
	memset(netplay_state.local_dev_hist, 0, sizeof(netplay_state.local_dev_hist));
	netplay_state.local_dev_hist_head = 0U;
	netplay_state.last_dev_hist_send_frame = ~0U;
	netplay_state.peer_name[0] = 0;
	memset(netplay_state.peer_pad_labels, 0, sizeof(netplay_state.peer_pad_labels));
	netplay_state.lobby_labels_dirty = TRUE;
	netplay_state.lobby_labels_sent = FALSE;
	netplay_state.last_labels_send_ms = 0U;
	netplay_state.chat_head = 0U;
	netplay_state.chat_count = 0U;
	netplay_lobby_clear_all_requests();
	for (i = 0U; i < NETPLAY_LOBBY_CHAT_LINES; i++){
		netplay_state.chat_lines[i][0] = 0;
	}
	netplay_state.self_peer_id = (netplay_state.mode == NETPLAY_MODE_SERVER) ? NETPLAY_LOBBY_OWNER_HOST : NETPLAY_LOBBY_OWNER_GUEST;
	netplay_lobby_default_layout();
	netplay_refresh_local_rom_info();
}

void netplay_set_local_identity(uint32 rom_crc, uint32 build_id, uint32 feature_flags, auint local_player, auint max_players)
{
	netplay_load_cfg_defaults();
	netplay_cfg.rom_crc = rom_crc;
	netplay_cfg.build_id = build_id;
	netplay_cfg.feature_flags = feature_flags;
	netplay_cfg.local_player = local_player;
	netplay_cfg.max_players = max_players;
	if (netplay_cfg.max_players == 0U){ netplay_cfg.max_players = 2U; }
	if (netplay_cfg.max_players > ROLLBACK_MAX_PLAYERS){ netplay_cfg.max_players = ROLLBACK_MAX_PLAYERS; }
	netplay_cfg.local_player_mask = netplay_sanitize_player_mask(netplay_cfg.local_player_mask ? netplay_cfg.local_player_mask : netplay_mask_for_player(local_player), netplay_cfg.max_players);
	netplay_cfg.local_player = netplay_primary_player_from_mask(netplay_cfg.local_player_mask, netplay_cfg.max_players);
	netplay_cfg.configured = TRUE;
	netplay_refresh_local_rom_info();
	if ((rom_crc != 0U) && netplay_cfg.rom_present){
		netplay_cfg.rom_crc = rom_crc;
	}
}

void netplay_get_local_identity(uint32 *rom_crc, uint32 *build_id, uint32 *feature_flags, auint *local_player, auint *max_players)
{
	netplay_load_cfg_defaults();
	if (rom_crc != NULL){
		*rom_crc = netplay_cfg.rom_crc;
	}
	if (build_id != NULL){
		*build_id = netplay_cfg.build_id;
	}
	if (feature_flags != NULL){
		*feature_flags = netplay_cfg.feature_flags;
	}
	if (local_player != NULL){
		*local_player = netplay_cfg.local_player;
	}
	if (max_players != NULL){
		*max_players = netplay_cfg.max_players;
	}
}

void netplay_get_config(netplay_config_t *cfg)
{
	if (cfg == NULL){
		return;
	}
	netplay_load_cfg_defaults();
	memset(cfg, 0, sizeof(*cfg));
	cfg->local_player = netplay_cfg.local_player;
	cfg->local_player_mask = netplay_cfg.local_player_mask;
	cfg->max_players = netplay_cfg.max_players;
	cfg->feature_flags = netplay_cfg.feature_flags;
	cfg->build_id = netplay_cfg.build_id;
	cfg->rom_send_enabled = netplay_cfg.rom_send_enabled;
	cfg->rom_receive_enabled = netplay_cfg.rom_receive_enabled;
	cfg->rom_sync_mode = netplay_cfg.rom_sync_mode;
	cfg->rom_tcp_port = netplay_cfg.rom_tcp_port;
	cfg->rom_max_size = netplay_cfg.rom_max_size;
	cfg->relay_server_port = netplay_cfg.relay_server_port;
	cfg->relay_allow_direct = netplay_cfg.relay_allow_direct;
	cfg->relay_allow_relay = netplay_cfg.relay_allow_relay;
	memcpy(cfg->relay_server_host, netplay_cfg.relay_server_host, sizeof(cfg->relay_server_host));
	memcpy(cfg->network_interface, netplay_cfg.network_interface, sizeof(cfg->network_interface));
	memcpy(cfg->local_name, netplay_cfg.local_name, sizeof(cfg->local_name));
	memcpy(cfg->local_pad_labels, netplay_cfg.local_pad_labels, sizeof(cfg->local_pad_labels));
}

void netplay_set_config(netplay_config_t const *cfg)
{
	if (cfg == NULL){
		return;
	}
	netplay_load_cfg_defaults();
	netplay_cfg.local_player = cfg->local_player;
	netplay_cfg.local_player_mask = cfg->local_player_mask;
	netplay_cfg.max_players = cfg->max_players;
	netplay_cfg.feature_flags = cfg->feature_flags;
	netplay_cfg.build_id = cfg->build_id;
	netplay_cfg.rom_send_enabled = cfg->rom_send_enabled;
	netplay_cfg.rom_receive_enabled = cfg->rom_receive_enabled;
	netplay_cfg.rom_sync_mode = cfg->rom_sync_mode;
	netplay_cfg.rom_tcp_port = cfg->rom_tcp_port;
	netplay_cfg.rom_max_size = cfg->rom_max_size;
	netplay_cfg.relay_server_port = cfg->relay_server_port;
	netplay_cfg.relay_allow_direct = cfg->relay_allow_direct;
	netplay_cfg.relay_allow_relay = cfg->relay_allow_relay;
	memcpy(netplay_cfg.relay_server_host, cfg->relay_server_host, sizeof(netplay_cfg.relay_server_host));
	memcpy(netplay_cfg.network_interface, cfg->network_interface, sizeof(netplay_cfg.network_interface));
	memcpy(netplay_cfg.local_name, cfg->local_name, sizeof(cfg->local_name));
	memcpy(netplay_cfg.local_pad_labels, cfg->local_pad_labels, sizeof(netplay_cfg.local_pad_labels));
	if (netplay_cfg.local_player == 0U){ netplay_cfg.local_player = 1U; }
	if (netplay_cfg.max_players == 0U){ netplay_cfg.max_players = 2U; }
	if (netplay_cfg.max_players > ROLLBACK_MAX_PLAYERS){ netplay_cfg.max_players = ROLLBACK_MAX_PLAYERS; }
	netplay_cfg.local_player_mask = netplay_sanitize_player_mask(netplay_cfg.local_player_mask ? netplay_cfg.local_player_mask : netplay_mask_for_player(netplay_cfg.local_player), netplay_cfg.max_players);
	netplay_cfg.local_player = netplay_primary_player_from_mask(netplay_cfg.local_player_mask, netplay_cfg.max_players);
	if (netplay_cfg.feature_flags == 0U){
		netplay_cfg.feature_flags = (uint32)(NETPLAY_FEATURE_COMPAT | NETPLAY_FEATURE_IPV6 | NETPLAY_FEATURE_ROM_SYNC | NETPLAY_FEATURE_ROM_TCP);
	}
	if (netplay_cfg.rom_max_size == 0U){
		netplay_cfg.rom_max_size = (4U * 1024U * 1024U);
	}
	if (netplay_cfg.relay_server_port == 0U){
		netplay_cfg.relay_server_port = 43810U;
	}
	if (netplay_cfg.relay_server_host[0] == 0U){
		strncpy(netplay_cfg.relay_server_host, "uzenet.us", sizeof(netplay_cfg.relay_server_host) - 1U);
		netplay_cfg.relay_server_host[sizeof(netplay_cfg.relay_server_host) - 1U] = 0;
	}
	if ((!netplay_cfg.relay_allow_direct) && (!netplay_cfg.relay_allow_relay)){
		netplay_cfg.relay_allow_direct = TRUE;
		netplay_cfg.relay_allow_relay = TRUE;
	}
	netplay_default_local_labels();
	netplay_cfg.configured = TRUE;
	netplay_mark_labels_dirty();
	netplay_refresh_local_rom_info();
}

void netplay_set_rom_sync_mode(netplay_rom_sync_mode_t mode)
{
	netplay_load_cfg_defaults();
	netplay_cfg.rom_sync_mode = mode;
}

netplay_rom_sync_mode_t netplay_get_rom_sync_mode(void)
{
	netplay_load_cfg_defaults();
	return netplay_cfg.rom_sync_mode;
}

boole netplay_open_client(char const *host, auint port, auint local_port)
{
	struct sockaddr_storage peer_addr;
	netplay_state.last_notice[0] = 0;
	socklen_t peer_len;
	SOCKET sock;
	int family;
	if (!netplay_resolve_peer(host, port, (netplay_selected_bind_family() != AF_UNSPEC) ? netplay_selected_bind_family() : AF_UNSPEC, SOCK_DGRAM, &peer_addr, &peer_len)){
		return FALSE;
	}
	family = ((struct sockaddr const*)&peer_addr)->sa_family;
	netplay_disconnect();
	netplay_state.last_notice[0] = 0;
	sock = socket(family, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET){
		return FALSE;
	}
	if (!netplay_prepare_socket_common(sock)){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	if (!netplay_bind_local_udp(sock, family, local_port, netplay_cfg.network_interface)){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	netplay_state.sock = sock;
	netplay_state.sock_family = family;
	netplay_state.mode = NETPLAY_MODE_CLIENT;
	netplay_state.enabled = TRUE;
	netplay_state.local_port = netplay_query_local_udp_port(sock, family);
	netplay_state.self_peer_id = NETPLAY_LOBBY_OWNER_GUEST;
	netplay_reset();
	netplay_chat_add_line("System: joined as Guest");
	netplay_store_peer((struct sockaddr const*)&peer_addr, peer_len);
	netplay_update_route_local_addr((struct sockaddr const*)&peer_addr, peer_len, family, netplay_cfg.network_interface);
	return TRUE;
}

boole netplay_open_server(auint local_port)
{
	SOCKET sock = INVALID_SOCKET;
	boole bound = FALSE;
	int bind_family = netplay_selected_bind_family();
	netplay_disconnect();
	netplay_state.last_notice[0] = 0;
if (bind_family == AF_INET){
		struct sockaddr_in bind_addr4;
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == INVALID_SOCKET){
			return FALSE;
		}
		(void)netplay_prepare_socket_common(sock);
		memset(&bind_addr4, 0, sizeof(bind_addr4));
		bind_addr4.sin_family = AF_INET;
		if ((netplay_cfg.network_interface[0] != 0) && (netplay_inet_pton_compat(AF_INET, netplay_cfg.network_interface, &bind_addr4.sin_addr) != 1)){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
		bind_addr4.sin_port = htons((uint16)local_port);
		if (bind(sock, (struct sockaddr*)&bind_addr4, sizeof(bind_addr4)) == SOCKET_ERROR){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
		bound = TRUE;
		netplay_state.sock_family = AF_INET;
	}else if (bind_family == AF_INET6){
		struct sockaddr_in6 bind_addr6;
		int off = 0;
		sock = socket(AF_INET6, SOCK_DGRAM, 0);
		if (sock == INVALID_SOCKET){
			return FALSE;
		}
		(void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char const*)&off, sizeof(off));
		(void)netplay_prepare_socket_common(sock);
		memset(&bind_addr6, 0, sizeof(bind_addr6));
		bind_addr6.sin6_family = AF_INET6;
		bind_addr6.sin6_addr = in6addr_any;
		if ((netplay_cfg.network_interface[0] != 0) && (netplay_inet_pton_compat(AF_INET6, netplay_cfg.network_interface, &bind_addr6.sin6_addr) != 1)){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
		bind_addr6.sin6_port = htons((uint16)local_port);
		if (bind(sock, (struct sockaddr*)&bind_addr6, sizeof(bind_addr6)) == 0){
			bound = TRUE;
			netplay_state.sock_family = AF_INET6;
		}else{
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
	}else
#if defined(IPV6_V6ONLY)
	{
		struct sockaddr_in6 bind_addr6;
		int off = 0;
		sock = socket(AF_INET6, SOCK_DGRAM, 0);
		if (sock != INVALID_SOCKET){
			(void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char const*)&off, sizeof(off));
			(void)netplay_prepare_socket_common(sock);
			memset(&bind_addr6, 0, sizeof(bind_addr6));
			bind_addr6.sin6_family = AF_INET6;
			bind_addr6.sin6_addr = in6addr_any;
			bind_addr6.sin6_port = htons((uint16)local_port);
			if (bind(sock, (struct sockaddr*)&bind_addr6, sizeof(bind_addr6)) == 0){
				bound = TRUE;
				netplay_state.sock_family = AF_INET6;
			}else{
				NETPLAY_CLOSESOCK(sock);
				sock = INVALID_SOCKET;
			}
		}
	}
#endif
	if (!bound){
		struct sockaddr_in bind_addr4;
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == INVALID_SOCKET){
			return FALSE;
		}
		(void)netplay_prepare_socket_common(sock);
		memset(&bind_addr4, 0, sizeof(bind_addr4));
		bind_addr4.sin_family = AF_INET;
		bind_addr4.sin_addr.s_addr = htonl(INADDR_ANY);
		bind_addr4.sin_port = htons((uint16)local_port);
		if (bind(sock, (struct sockaddr*)&bind_addr4, sizeof(bind_addr4)) == SOCKET_ERROR){
			NETPLAY_CLOSESOCK(sock);
			return FALSE;
		}
		bound = TRUE;
		netplay_state.sock_family = AF_INET;
	}
	netplay_state.sock = sock;
	netplay_state.mode = NETPLAY_MODE_SERVER;
	netplay_state.enabled = TRUE;
	netplay_state.local_port = netplay_query_local_udp_port(sock, netplay_state.sock_family);
	netplay_state.self_peer_id = NETPLAY_LOBBY_OWNER_HOST;
	netplay_reset();
	netplay_chat_add_line("System: hosting lobby as Host");
	netplay_state.local_ready = TRUE;
	netplay_update_route_local_addr(NULL, 0U, netplay_state.sock_family, netplay_cfg.network_interface);
	return bound;
}

boole netplay_relay_fetch_public_rooms(char const *relay_host, auint relay_port, netplay_relay_room_entry_t *entries, auint max_entries, auint *out_count, char *status, auint status_size)
{
	struct sockaddr_storage relay_addr;
	socklen_t relay_len;
	SOCKET sock = INVALID_SOCKET;
	int family;
	fd_set rfds;
	struct timeval tv;
	uint8 buf[sizeof(uzrl_header_t) + sizeof(uzrl_room_list_resp_t)];
	int got;
	if (out_count != NULL) *out_count = 0U;
	if ((status != NULL) && (status_size != 0U)) status[0] = 0;
	if ((relay_host == NULL) || (relay_host[0] == 0) || (entries == NULL) || (max_entries == 0U)){
		if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "INVALID BROWSE REQUEST");
		return FALSE;
	}
	if (!netplay_resolve_peer(relay_host, relay_port, (netplay_selected_bind_family() != AF_UNSPEC) ? netplay_selected_bind_family() : AF_UNSPEC, SOCK_DGRAM, &relay_addr, &relay_len)){
		if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "UNABLE TO RESOLVE RELAY HOST");
		return FALSE;
	}
	family = ((struct sockaddr const*)&relay_addr)->sa_family;
	sock = socket(family, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET){
		if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "UNABLE TO OPEN UDP SOCKET");
		return FALSE;
	}
	if (!netplay_prepare_socket_common(sock) || (!netplay_bind_local_udp(sock, family, 0U, netplay_cfg.network_interface))){
		NETPLAY_CLOSESOCK(sock);
		if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "UNABLE TO PREPARE UDP SOCKET");
		return FALSE;
	}
	{
		uzrl_header_t hdr;
		uzrl_room_list_req_t req;
		memset(&hdr, 0, sizeof(hdr));
		hdr.magic_be = htonl(UZRL_MAGIC_U32);
		hdr.version = UZRL_VERSION;
		hdr.type = UZRLT_LIST_REQ;
		hdr.header_len = (uint8)sizeof(hdr);
		hdr.payload_len_be = htons((uint16)sizeof(req));
		memset(&req, 0, sizeof(req));
		req.app_id_be = htonl(0U);
		req.limit = (uint8)((max_entries > UZRL_ROOM_LIST_MAX) ? UZRL_ROOM_LIST_MAX : max_entries);
		memcpy(buf, &hdr, sizeof(hdr));
		memcpy(buf + sizeof(hdr), &req, sizeof(req));
		if (sendto(sock, (char const*)buf, (int)(sizeof(hdr) + sizeof(req)), 0, (struct sockaddr*)&relay_addr, relay_len) != (int)(sizeof(hdr) + sizeof(req))){
			NETPLAY_CLOSESOCK(sock);
			if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "FAILED TO SEND BROWSE REQUEST");
			return FALSE;
		}
	}
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	tv.tv_sec = 1;
	tv.tv_usec = 500000;
	if (select((int)(sock + 1), &rfds, NULL, NULL, &tv) <= 0){
		NETPLAY_CLOSESOCK(sock);
		if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "NO PUBLIC GAMES FOUND");
		return FALSE;
	}
	got = (int)recvfrom(sock, (char*)buf, (int)sizeof(buf), 0, NULL, NULL);
	NETPLAY_CLOSESOCK(sock);
	if (got < (int)sizeof(uzrl_header_t)){
		if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "SHORT RELAY RESPONSE");
		return FALSE;
	}
	{
		uzrl_header_t hdr;
		uint16 plen;
		memcpy(&hdr, buf, sizeof(hdr));
		if ((ntohl(hdr.magic_be) != UZRL_MAGIC_U32) || (hdr.version != UZRL_VERSION) || (hdr.header_len != sizeof(hdr))){
			if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "INVALID RELAY RESPONSE");
			return FALSE;
		}
		plen = ntohs(hdr.payload_len_be);
		if ((auint)got < (auint)(sizeof(hdr) + plen)){
			if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "TRUNCATED RELAY RESPONSE");
			return FALSE;
		}
		if (hdr.type == UZRLT_ERROR){
			uzrl_error_t err;
			if (plen >= sizeof(err)){
				memcpy(&err, buf + sizeof(hdr), sizeof(err));
				err.message[sizeof(err.message) - 1U] = 0;
				if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "%s", err.message[0] ? err.message : "RELAY ERROR");
			}
			return FALSE;
		}
		if ((hdr.type == UZRLT_ROOM_LIST) && (plen >= sizeof(uzrl_room_list_resp_t))){
			uzrl_room_list_resp_t resp;
			auint count;
			memcpy(&resp, buf + sizeof(hdr), sizeof(resp));
			count = resp.count;
			if (count > max_entries) count = max_entries;
			if (count > UZRL_ROOM_LIST_MAX) count = UZRL_ROOM_LIST_MAX;
			for (auint i = 0U; i < count; ++i){
				memcpy(entries[i].room_code, resp.entries[i].room_code, UZRL_ROOM_CODE_LEN);
				entries[i].room_code[NETPLAY_RELAY_ROOM_CODE_LEN] = 0;
				memcpy(entries[i].host_name, resp.entries[i].host_name, UZRL_NAME_LEN);
				entries[i].host_name[NETPLAY_LOBBY_NAME_LEN] = 0;
				memcpy(entries[i].game_title, resp.entries[i].game_title, UZRL_GAME_TITLE_LEN);
				entries[i].game_title[NETPLAY_RELAY_GAME_TITLE_LEN] = 0;
				entries[i].member_count = resp.entries[i].member_count;
				entries[i].is_public = ((resp.entries[i].flags & UZRL_FLAG_PUBLIC_ROOM) != 0U) ? TRUE : FALSE;
			}
			if (out_count != NULL) *out_count = count;
			if ((status != NULL) && (status_size != 0U)){
				if (count != 0U) snprintf(status, status_size, "FOUND %u PUBLIC GAME%s", (unsigned)count, (count == 1U) ? "" : "S");
				else snprintf(status, status_size, "NO PUBLIC GAMES FOUND");
			}
			return TRUE;
		}
	}
	if ((status != NULL) && (status_size != 0U)) snprintf(status, status_size, "UNEXPECTED RELAY RESPONSE");
	return FALSE;
}

boole netplay_open_relay_host(char const *relay_host, auint relay_port, auint local_port, char const *room_code, boole public_room, char const *game_title)
{
	struct sockaddr_storage relay_addr;
	socklen_t relay_len;
	SOCKET sock;
	int family;
	if ((relay_host == NULL) || (relay_host[0] == 0)){
		return FALSE;
	}
	if (!netplay_resolve_peer(relay_host, relay_port, (netplay_selected_bind_family() != AF_UNSPEC) ? netplay_selected_bind_family() : AF_UNSPEC, SOCK_DGRAM, &relay_addr, &relay_len)){
		return FALSE;
	}
	family = ((struct sockaddr const*)&relay_addr)->sa_family;
	netplay_disconnect();
	netplay_state.last_notice[0] = 0;
	sock = socket(family, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET){
		return FALSE;
	}
	if (!netplay_prepare_socket_common(sock)){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	if (!netplay_bind_local_udp(sock, family, local_port, netplay_cfg.network_interface)){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	netplay_state.sock = sock;
	netplay_state.sock_family = family;
	netplay_state.mode = NETPLAY_MODE_SERVER;
	netplay_state.enabled = TRUE;
	netplay_state.local_port = netplay_query_local_udp_port(sock, family);
	netplay_state.self_peer_id = NETPLAY_LOBBY_OWNER_HOST;
	netplay_reset();
	netplay_state.relay_mode = TRUE;
	netplay_state.relay_pending_req_type = UZRLT_CREATE_REQ;
	memcpy(&(netplay_state.relay_addr), &relay_addr, relay_len);
	netplay_state.relay_addr_len = relay_len;
	strncpy(netplay_state.relay_server_host, relay_host, sizeof(netplay_state.relay_server_host) - 1U);
	netplay_state.relay_server_host[sizeof(netplay_state.relay_server_host) - 1U] = 0;
	netplay_state.relay_server_port = relay_port;
	if ((room_code != NULL) && (room_code[0] != 0)){
		strncpy(netplay_state.relay_room_code, room_code, sizeof(netplay_state.relay_room_code) - 1U);
		netplay_state.relay_room_code[sizeof(netplay_state.relay_room_code) - 1U] = 0;
	}
	netplay_state.relay_room_public = public_room;
	if ((game_title != NULL) && (game_title[0] != 0)){
		strncpy(netplay_state.relay_game_title, game_title, sizeof(netplay_state.relay_game_title) - 1U);
		netplay_state.relay_game_title[sizeof(netplay_state.relay_game_title) - 1U] = 0;
	}else{
		strncpy(netplay_state.relay_game_title, "UNKNOWN GAME", sizeof(netplay_state.relay_game_title) - 1U);
		netplay_state.relay_game_title[sizeof(netplay_state.relay_game_title) - 1U] = 0;
	}
	netplay_chat_add_line(public_room ? "System: hosting PUBLIC relay lobby as Host" : "System: hosting PRIVATE relay lobby as Host");
	netplay_state.local_ready = TRUE;
	netplay_update_route_local_addr((struct sockaddr const*)&relay_addr, relay_len, family, netplay_cfg.network_interface);
	netplay_state.relay_last_req_send_ms = netplay_now_ms();
	return netplay_send_relay_request(UZRLT_CREATE_REQ, netplay_state.relay_room_code[0] ? netplay_state.relay_room_code : NULL);
}

boole netplay_open_relay_join(char const *relay_host, auint relay_port, char const *room_code, auint local_port)
{
	struct sockaddr_storage relay_addr;
	socklen_t relay_len;
	SOCKET sock;
	int family;
	if ((relay_host == NULL) || (relay_host[0] == 0) || (room_code == NULL) || (room_code[0] == 0)){
		return FALSE;
	}
	if (!netplay_resolve_peer(relay_host, relay_port, (netplay_selected_bind_family() != AF_UNSPEC) ? netplay_selected_bind_family() : AF_UNSPEC, SOCK_DGRAM, &relay_addr, &relay_len)){
		return FALSE;
	}
	family = ((struct sockaddr const*)&relay_addr)->sa_family;
	netplay_disconnect();
	netplay_state.last_notice[0] = 0;
	sock = socket(family, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET){
		return FALSE;
	}
	if (!netplay_prepare_socket_common(sock)){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	if (!netplay_bind_local_udp(sock, family, local_port, netplay_cfg.network_interface)){
		NETPLAY_CLOSESOCK(sock);
		return FALSE;
	}
	netplay_state.sock = sock;
	netplay_state.sock_family = family;
	netplay_state.mode = NETPLAY_MODE_CLIENT;
	netplay_state.enabled = TRUE;
	netplay_state.local_port = netplay_query_local_udp_port(sock, family);
	netplay_state.self_peer_id = NETPLAY_LOBBY_OWNER_GUEST;
	netplay_reset();
	netplay_state.relay_mode = TRUE;
	netplay_state.relay_pending_req_type = UZRLT_JOIN_REQ;
	netplay_state.relay_room_public = FALSE;
	netplay_state.relay_game_title[0] = 0;
	memcpy(&(netplay_state.relay_addr), &relay_addr, relay_len);
	netplay_state.relay_addr_len = relay_len;
	strncpy(netplay_state.relay_server_host, relay_host, sizeof(netplay_state.relay_server_host) - 1U);
	netplay_state.relay_server_host[sizeof(netplay_state.relay_server_host) - 1U] = 0;
	netplay_state.relay_server_port = relay_port;
	strncpy(netplay_state.relay_room_code, room_code, sizeof(netplay_state.relay_room_code) - 1U);
	netplay_state.relay_room_code[sizeof(netplay_state.relay_room_code) - 1U] = 0;
	netplay_chat_add_line("System: joining relay room as Guest");
	netplay_update_route_local_addr((struct sockaddr const*)&relay_addr, relay_len, family, netplay_cfg.network_interface);
	netplay_state.relay_last_req_send_ms = netplay_now_ms();
	return netplay_send_relay_request(UZRLT_JOIN_REQ, netplay_state.relay_room_code);
}

void netplay_disconnect(void)
{
	if ((netplay_state.last_notice[0] == 0) && netplay_state.enabled){
		strncpy(netplay_state.last_notice, "disconnected", sizeof(netplay_state.last_notice) - 1U);
		netplay_state.last_notice[sizeof(netplay_state.last_notice) - 1U] = 0;
	}
	if (netplay_state.enabled && netplay_state.peer_valid && (netplay_state.sock != INVALID_SOCKET)){
		(void)netplay_send_disconnect_pkt();
	}
	if (netplay_state.enabled && netplay_state.relay_mode && (netplay_state.sock != INVALID_SOCKET) && (netplay_state.relay_session_id != 0U)){
		(void)netplay_send_relay_leave();
	}
	netplay_close_rom_listener();
	if (netplay_state.sock != INVALID_SOCKET){
		NETPLAY_CLOSESOCK(netplay_state.sock);
	}
	netplay_state.sock = INVALID_SOCKET;
	netplay_state.enabled = FALSE;
	netplay_state.peer_valid = FALSE;
	netplay_state.peer_addr_len = 0;
	netplay_state.relay_addr_len = 0;
	netplay_state.relay_mode = FALSE;
	netplay_state.mode = NETPLAY_MODE_NONE;
	netplay_state.sock_family = AF_UNSPEC;
	netplay_route_local_addr[0] = 0;
	netplay_route_local_scope[0] = 0;
	netplay_reset();
}

void netplay_poll(void)
{
	uint8 buf[1600];
	struct sockaddr_storage from;
	socklen_t from_len;
	int got;
	uint32 now;

	if ((!netplay_state.enabled) || (netplay_state.sock == INVALID_SOCKET)){
		return;
	}

	netplay_tcp_service_sender();

	for (;;){
		from_len = (socklen_t)sizeof(from);
		got = (int)recvfrom(netplay_state.sock,
			(char*)buf,
			(int)sizeof(buf),
			0,
			(struct sockaddr*)&from,
			&from_len);
		if (got <= 0){
			break;
		}
		if ((got >= (int)sizeof(uzrl_header_t)) && (ntohl(((uzrl_header_t const*)buf)->magic_be) == UZRL_MAGIC_U32)){
			netplay_handle_relay_packet(buf, (auint)got, (struct sockaddr const*)&from, from_len);
		}else if (got == (int)sizeof(netplay_pkt_t)){
			netplay_handle_pkt((netplay_pkt_t const*)buf, (struct sockaddr const*)&from, from_len, FALSE);
		}
	}

	now = netplay_now_ms();
	if (netplay_state.relay_mode){
		if ((netplay_state.relay_pending_req_type != 0U) && (!netplay_state.relay_room_ready)){
			if ((netplay_state.relay_last_req_send_ms == 0U) || (netplay_ms_delta(now, netplay_state.relay_last_req_send_ms) >= NETPLAY_RELAY_REQ_RETRY_MS)){
				(void)netplay_send_relay_request(netplay_state.relay_pending_req_type, netplay_state.relay_room_code[0] ? netplay_state.relay_room_code : NULL);
				netplay_state.relay_last_req_send_ms = now;
			}
			if (netplay_ms_delta(now, netplay_state.relay_last_req_send_ms) >= NETPLAY_RELAY_SETUP_TIMEOUT_MS){
				netplay_drop_connection("relay setup timed out");
				return;
			}
		}
		if ((netplay_state.relay_session_id != 0U) && ((netplay_state.relay_last_heartbeat_ms == 0U) || (netplay_ms_delta(now, netplay_state.relay_last_heartbeat_ms) >= NETPLAY_RELAY_HEARTBEAT_MS))){
			(void)netplay_send_relay_heartbeat();
			netplay_state.relay_last_heartbeat_ms = now;
		}
		if (netplay_state.relay_direct_candidate && (!netplay_state.relay_direct_established) && (netplay_state.peer_addr_len != 0U)){
			if ((netplay_state.relay_last_punch_send_ms == 0U) || (netplay_ms_delta(now, netplay_state.relay_last_punch_send_ms) >= NETPLAY_RELAY_PUNCH_RETRY_MS)){
				(void)netplay_send_direct_probe();
				netplay_state.relay_last_punch_send_ms = now;
			}
		}
	}

	if (netplay_state.peer_valid){
		if (netplay_state.last_rx_ms != 0U){
			uint32 idle = netplay_ms_delta(now, netplay_state.last_rx_ms);
			if (netplay_state.connected || netplay_state.hello_acked){
				if (idle >= NETPLAY_PEER_TIMEOUT_MS){
					netplay_drop_connection("peer timed out");
					return;
				}
			}else if (netplay_state.hello_sent){
				if (idle >= NETPLAY_CONNECT_TIMEOUT_MS){
					netplay_drop_connection("connection timed out");
					return;
				}
			}
		}
	}

	if ((!netplay_state.hello_sent) && netplay_state.peer_valid){
		if (netplay_send_pkt(NETPLAY_TYPE_HELLO, 0U, 0U, 0U)){
			netplay_state.hello_sent = TRUE;
			netplay_state.last_hello_send_ms = now;
			if (netplay_state.last_rx_ms == 0U){
				netplay_state.last_rx_ms = now;
			}
		}
	}
	if (netplay_state.peer_valid && netplay_state.hello_sent && (!netplay_state.hello_acked)){
		if ((netplay_state.last_hello_send_ms == 0U) || (netplay_ms_delta(now, netplay_state.last_hello_send_ms) >= NETPLAY_HELLO_RETRY_MS)){
			if (netplay_send_pkt(NETPLAY_TYPE_HELLO, 0U, 0U, 0U)){
				netplay_state.last_hello_send_ms = now;
			}
		}
	}
	if (netplay_state.hello_acked && (!netplay_state.caps_sent)){
		(void)netplay_send_caps();
	}
	if (netplay_state.hello_acked && netplay_state.peer_valid){
		if (((!netplay_state.lobby_labels_sent) || netplay_state.lobby_labels_dirty) && ((netplay_state.last_labels_send_ms == 0U) || (netplay_ms_delta(now, netplay_state.last_labels_send_ms) >= NETPLAY_LABEL_SEND_MS))){
			(void)netplay_send_lobby_labels_pkt();
		}
		if ((netplay_state.last_ping_send_ms == 0U) || (netplay_ms_delta(now, netplay_state.last_ping_send_ms) >= NETPLAY_PING_INTERVAL_MS)){
			(void)netplay_send_ping();
		}
		if ((netplay_state.mode == NETPLAY_MODE_SERVER) && netplay_state.lobby_dirty){
			if ((netplay_state.last_lobby_send_ms == 0U) || (netplay_ms_delta(now, netplay_state.last_lobby_send_ms) >= NETPLAY_LOBBY_BROADCAST_MS)){
				(void)netplay_send_lobby_state();
			}
		}
	}
}

boole netplay_send_input(auint frame, auint player, auint buttons)
{
	if ((!netplay_state.enabled) || (!netplay_state.peer_valid)){
		return FALSE;
	}
	if (!netplay_state.hello_sent){
		if (!netplay_send_pkt(NETPLAY_TYPE_HELLO, 0U, 0U, 0U)){
			return FALSE;
		}
		netplay_state.hello_sent = TRUE;
		netplay_state.last_hello_send_ms = netplay_now_ms();
		return FALSE;
	}
	if ((!netplay_state.hello_acked) || (!netplay_state.compat_ok) || (!netplay_state.session_started)){
		return FALSE;
	}
	netplay_record_local_input(player, frame, buttons);
	return netplay_send_pkt(NETPLAY_TYPE_INPUT, frame, player, buttons);
}

boole netplay_send_device_event(auint frame, rollback_device_event_t const *ev)
{
	netplay_pkt_t pkt;
	uint8 tmp[ROLLBACK_DEVICE_EVENT_DATA_MAX] = {0,0,0,0,0,0,0,0};
	auint i;
	if ((ev == NULL) || (!netplay_state.enabled) || (!netplay_state.peer_valid)){
		return FALSE;
	}
	if (!netplay_state.hello_sent){
		if (!netplay_send_pkt(NETPLAY_TYPE_HELLO, 0U, 0U, 0U)){
			return FALSE;
		}
		netplay_state.hello_sent = TRUE;
		netplay_state.last_hello_send_ms = netplay_now_ms();
		return FALSE;
	}
	if ((!netplay_state.hello_acked) || (!netplay_state.compat_ok) || (!netplay_state.session_started)){
		return FALSE;
	}
	netplay_record_local_device_event(frame, ev);
	netplay_init_pkt(&pkt, NETPLAY_TYPE_DEVICE_EVENT);
	pkt.frame = htonl((uint32)frame);
	pkt.player = (uint8)ev->port;
	pkt.sync_mode = (uint8)ev->slot;
	pkt.result = (uint8)ev->type;
	pkt.flags = (uint8)(ev->len & 0x0FU);
	pkt.aux1 = htons(ev->seq);
	for (i = 0U; i < ev->len && i < ROLLBACK_DEVICE_EVENT_DATA_MAX; ++i){ tmp[i] = ev->data[i]; }
	pkt.buttons = htonl(netplay_pack_u32_from_bytes(&(tmp[0])));
	pkt.aux0 = htonl(netplay_pack_u32_from_bytes(&(tmp[4])));
	if (!netplay_send_raw(&pkt)){
		return FALSE;
	}
	(void)netplay_send_device_history(frame + 1U);
	return TRUE;
}

boole netplay_is_connected(void)
{
	return netplay_state.connected && netplay_state.hello_acked && netplay_state.compat_ok && netplay_state.session_started;
}

void netplay_get_status(netplay_status_t *status)
{
	if (status == NULL){
		return;
	}
	if (netplay_iface_cache_count == 0U){
		(void)netplay_refresh_interfaces();
	}
	memset(status, 0, sizeof(*status));
	status->initialized = netplay_state.initialized;
	status->relay_mode = netplay_state.relay_mode;
	status->relay_room_ready = netplay_state.relay_room_ready;
	status->relay_peer_present = netplay_state.relay_peer_present;
	status->relay_direct_established = netplay_state.relay_direct_established;
	status->enabled = netplay_state.enabled;
	status->connected = netplay_state.connected;
	status->hello_sent = netplay_state.hello_sent;
	status->hello_acked = netplay_state.hello_acked;
	status->compat_ok = netplay_state.compat_ok;
	status->caps_sent = netplay_state.caps_sent;
	status->peer_caps = netplay_state.peer_caps;
	status->local_ready = netplay_state.local_ready;
	status->peer_ready = netplay_state.peer_ready;
	status->session_started = netplay_state.session_started;
	status->rom_present = netplay_cfg.rom_present;
	status->rom_sendable = netplay_cfg.rom_sendable;
	status->rom_transfer_needed = netplay_state.rom_transfer_needed;
	status->mode = netplay_state.mode;
	status->rom_sync_mode = netplay_cfg.rom_sync_mode;
	status->local_session = netplay_state.local_session;
	status->peer_session = netplay_state.peer_session;
	status->tx_seq = netplay_state.tx_seq;
	status->rx_last_seq = netplay_state.rx_last_seq;
	status->rx_seq_init = netplay_state.rx_seq_init;
	status->rx_dup_count = netplay_state.rx_dup_count;
	status->rx_ooo_count = netplay_state.rx_ooo_count;
	status->rx_gap_count = netplay_state.rx_gap_count;
	status->local_port = netplay_state.local_port;
	status->peer_port = netplay_state.peer_port;
	status->socket_ipv6 = (netplay_state.sock_family == AF_INET6) ? TRUE : FALSE;
	status->peer_addr_ipv6 = (netplay_state.peer_addr.ss_family == AF_INET6) ? TRUE : FALSE;
	status->relay_addr_ipv6 = (netplay_state.relay_addr.ss_family == AF_INET6) ? TRUE : FALSE;
	status->local_player = netplay_cfg.local_player;
	status->peer_player = netplay_state.peer_player;
	status->local_player_mask = netplay_state.lobby_valid ? netplay_lobby_local_mask() : netplay_cfg.local_player_mask;
	status->peer_player_mask = netplay_state.lobby_valid ? netplay_lobby_peer_mask() : netplay_state.peer_player_mask;
	status->max_players = netplay_cfg.max_players;
	status->lobby_active = netplay_state.connected;
	status->lobby_is_host = (netplay_state.mode == NETPLAY_MODE_SERVER) ? TRUE : FALSE;
	status->lobby_local_ready = netplay_state.lobby_local_ready;
	status->lobby_peer_ready = netplay_state.lobby_peer_ready;
	status->lobby_seat_map_valid = netplay_lobby_seat_map_valid();
	status->ping_ms = netplay_state.ping_ms;
	status->ping_smoothed_ms = netplay_state.ping_smoothed_ms;
	status->ping_jitter_ms = netplay_state.ping_jitter_ms;
	memcpy(status->lobby_seat_owner_peer, netplay_state.lobby_owner_peer, sizeof(status->lobby_seat_owner_peer));
	memcpy(status->lobby_seat_pad_index, netplay_state.lobby_pad_index, sizeof(status->lobby_seat_pad_index));
	status->rom_crc = netplay_cfg.rom_crc;
	status->peer_rom_crc = netplay_state.peer_rom_crc;
	status->rom_size = netplay_cfg.rom_size;
	status->peer_rom_size = netplay_state.peer_rom_size;
	status->build_id = netplay_cfg.build_id;
	status->peer_build_id = netplay_state.peer_build_id;
	status->feature_flags = netplay_cfg.feature_flags;
	status->peer_feature_flags = netplay_state.peer_feature_flags;
	status->rom_tcp_port = netplay_state.rom_listen_port;
	status->peer_rom_tcp_port = netplay_state.peer_transfer_port;
	strncpy(status->rom_name, netplay_cfg.rom_name, sizeof(status->rom_name) - 1U);
	status->rom_name[sizeof(status->rom_name) - 1U] = 0;
	strncpy(status->peer_rom_name, netplay_state.peer_rom_name, sizeof(status->peer_rom_name) - 1U);
	status->peer_rom_name[sizeof(status->peer_rom_name) - 1U] = 0;
	strncpy(status->peer_host, netplay_state.peer_host, sizeof(status->peer_host) - 1U);
	status->peer_host[sizeof(status->peer_host) - 1U] = 0;
	status->relay_server_port = netplay_state.relay_server_port;
	strncpy(status->relay_server_host, netplay_state.relay_server_host, sizeof(status->relay_server_host) - 1U);
	status->relay_server_host[sizeof(status->relay_server_host) - 1U] = 0;
	strncpy(status->relay_room_code, netplay_state.relay_room_code, sizeof(status->relay_room_code) - 1U);
	status->relay_room_code[sizeof(status->relay_room_code) - 1U] = 0;
	strncpy(status->network_interface, netplay_cfg.network_interface, sizeof(status->network_interface) - 1U);
	status->network_interface[sizeof(status->network_interface) - 1U] = 0;
	if (netplay_cfg.network_interface[0] != 0){
		auint ifidx = netplay_find_interface_value(netplay_cfg.network_interface);
		if (ifidx != 0U){
			strncpy(status->network_interface_label, netplay_get_interface_label(ifidx - 1U), sizeof(status->network_interface_label) - 1U);
			status->network_interface_label[sizeof(status->network_interface_label) - 1U] = 0;
		}else{
			strncpy(status->network_interface_label, netplay_cfg.network_interface, sizeof(status->network_interface_label) - 1U);
			status->network_interface_label[sizeof(status->network_interface_label) - 1U] = 0;
		}
	}else{
		strncpy(status->network_interface_label, "AUTO", sizeof(status->network_interface_label) - 1U);
		status->network_interface_label[sizeof(status->network_interface_label) - 1U] = 0;
	}
	strncpy(status->local_ipv4, netplay_default_ipv4, sizeof(status->local_ipv4) - 1U);
	status->local_ipv4[sizeof(status->local_ipv4) - 1U] = 0;
	strncpy(status->local_ipv6, netplay_default_ipv6, sizeof(status->local_ipv6) - 1U);
	status->local_ipv6[sizeof(status->local_ipv6) - 1U] = 0;
	strncpy(status->local_ipv4_scope, netplay_default_ipv4_scope, sizeof(status->local_ipv4_scope) - 1U);
	status->local_ipv4_scope[sizeof(status->local_ipv4_scope) - 1U] = 0;
	strncpy(status->local_ipv6_scope, netplay_default_ipv6_scope, sizeof(status->local_ipv6_scope) - 1U);
	status->local_ipv6_scope[sizeof(status->local_ipv6_scope) - 1U] = 0;
	if (netplay_cfg.network_interface[0] != 0){
		int sfam = AF_UNSPEC;
		char sscope[NETPLAY_SCOPE_STR_LEN];
		sscope[0] = 0;
		if (netplay_parse_ip_literal(netplay_cfg.network_interface, &sfam, NULL, NULL, sscope, sizeof(sscope))){
			if (sfam == AF_INET){
				strncpy(status->local_ipv4, netplay_cfg.network_interface, sizeof(status->local_ipv4) - 1U);
				status->local_ipv4[sizeof(status->local_ipv4) - 1U] = 0;
				strncpy(status->local_ipv4_scope, sscope, sizeof(status->local_ipv4_scope) - 1U);
				status->local_ipv4_scope[sizeof(status->local_ipv4_scope) - 1U] = 0;
			}else if (sfam == AF_INET6){
				strncpy(status->local_ipv6, netplay_cfg.network_interface, sizeof(status->local_ipv6) - 1U);
				status->local_ipv6[sizeof(status->local_ipv6) - 1U] = 0;
				strncpy(status->local_ipv6_scope, sscope, sizeof(status->local_ipv6_scope) - 1U);
				status->local_ipv6_scope[sizeof(status->local_ipv6_scope) - 1U] = 0;
			}
		}
	}
	strncpy(status->active_local_addr, netplay_route_local_addr, sizeof(status->active_local_addr) - 1U);
	status->active_local_addr[sizeof(status->active_local_addr) - 1U] = 0;
	strncpy(status->active_local_scope, netplay_route_local_scope, sizeof(status->active_local_scope) - 1U);
	status->active_local_scope[sizeof(status->active_local_scope) - 1U] = 0;
	strncpy(status->notice, netplay_state.last_notice, sizeof(status->notice) - 1U);
	status->notice[sizeof(status->notice) - 1U] = 0;
	memcpy(status->local_name, netplay_cfg.local_name, sizeof(status->local_name));
	memcpy(status->peer_name, netplay_state.peer_name, sizeof(status->peer_name));
	memcpy(status->local_pad_labels, netplay_cfg.local_pad_labels, sizeof(status->local_pad_labels));
	memcpy(status->peer_pad_labels, netplay_state.peer_pad_labels, sizeof(status->peer_pad_labels));
	if (netplay_state.compat_reason[0] != 0){
		strncpy(status->compat_reason, netplay_state.compat_reason, sizeof(status->compat_reason) - 1U);
	}else{
		strncpy(status->compat_reason, netplay_state.last_notice, sizeof(status->compat_reason) - 1U);
	}
	status->compat_reason[sizeof(status->compat_reason) - 1U] = 0;
}

void netplay_lobby_get_status(netplay_lobby_status_t *status)
{
	if (status == NULL){
		return;
	}
	memset(status, 0, sizeof(*status));
	status->active = netplay_state.connected;
	status->is_host = (netplay_state.mode == NETPLAY_MODE_SERVER) ? TRUE : FALSE;
	status->asset_local_ready = netplay_state.local_ready;
	status->asset_peer_ready = netplay_state.peer_ready;
	status->local_ready = netplay_state.lobby_local_ready;
	status->peer_ready = netplay_state.lobby_peer_ready;
	status->seat_map_valid = netplay_lobby_seat_map_valid();
	status->can_start = status->is_host && status->asset_local_ready && status->asset_peer_ready && status->local_ready && status->peer_ready && status->seat_map_valid;
	status->max_players = netplay_cfg.max_players;
	status->ping_ms = netplay_state.ping_ms;
	status->ping_smoothed_ms = netplay_state.ping_smoothed_ms;
	status->ping_jitter_ms = netplay_state.ping_jitter_ms;
	memcpy(status->seat_owner_peer, netplay_state.lobby_owner_peer, sizeof(status->seat_owner_peer));
	memcpy(status->seat_pad_index, netplay_state.lobby_pad_index, sizeof(status->seat_pad_index));
	memcpy(status->pending_request_valid, netplay_state.lobby_request_valid, sizeof(status->pending_request_valid));
	memcpy(status->pending_request_owner_peer, netplay_state.lobby_request_owner_peer, sizeof(status->pending_request_owner_peer));
	memcpy(status->pending_request_pad_index, netplay_state.lobby_request_pad_index, sizeof(status->pending_request_pad_index));
	memcpy(status->local_name, netplay_cfg.local_name, sizeof(status->local_name));
	memcpy(status->peer_name, netplay_state.peer_name, sizeof(status->peer_name));
	memcpy(status->local_pad_labels, netplay_cfg.local_pad_labels, sizeof(status->local_pad_labels));
	memcpy(status->peer_pad_labels, netplay_state.peer_pad_labels, sizeof(status->peer_pad_labels));
}

boole netplay_lobby_set_seat(auint seat, auint owner_peer, auint pad_index)
{
	boole changed;
	if ((seat >= netplay_cfg.max_players) || (pad_index >= ROLLBACK_MAX_PLAYERS)){
		return FALSE;
	}
	if (netplay_state.mode != NETPLAY_MODE_SERVER){
		return FALSE;
	}
	if ((owner_peer != NETPLAY_LOBBY_OWNER_HOST) && (owner_peer != NETPLAY_LOBBY_OWNER_GUEST) && (owner_peer != NETPLAY_LOBBY_OWNER_NONE)){
		return FALSE;
	}
	changed = ((netplay_state.lobby_owner_peer[seat] != (uint8)owner_peer) ||
			   (netplay_state.lobby_pad_index[seat] != (uint8)pad_index)) ? TRUE : FALSE;
	netplay_state.lobby_owner_peer[seat] = (uint8)owner_peer;
	netplay_state.lobby_pad_index[seat] = (uint8)pad_index;
	netplay_lobby_clear_request(seat);
	if (changed){
		netplay_lobby_invalidate_ready("[system] Seat map changed; ready reset");
	}else{
		netplay_lobby_mark_dirty();
	}
	if (netplay_state.peer_valid){
		(void)netplay_send_lobby_state();
	}
	return TRUE;
}

boole netplay_lobby_request_seat(auint seat, auint owner_peer, auint pad_index)
{
	if (netplay_state.mode != NETPLAY_MODE_CLIENT){
		netplay_set_error("only guest can request seats");
		return FALSE;
	}
	if (seat >= netplay_cfg.max_players){
		return FALSE;
	}
	if ((owner_peer != NETPLAY_LOBBY_OWNER_GUEST) && (owner_peer != NETPLAY_LOBBY_OWNER_NONE)){
		return FALSE;
	}
	if ((owner_peer != NETPLAY_LOBBY_OWNER_NONE) && (pad_index >= ROLLBACK_MAX_PLAYERS)){
		return FALSE;
	}
	return netplay_send_lobby_request_pkt(seat, owner_peer, pad_index);
}

boole netplay_lobby_approve_request(auint seat, boole approve)
{
	char note[128];
	auint owner_peer;
	auint pad_index;
	if (netplay_state.mode != NETPLAY_MODE_SERVER){
		netplay_set_error("only host can approve requests");
		return FALSE;
	}
	if ((seat >= netplay_cfg.max_players) || (!netplay_state.lobby_request_valid[seat])){
		return FALSE;
	}
	owner_peer = netplay_state.lobby_request_owner_peer[seat];
	pad_index = netplay_state.lobby_request_pad_index[seat];
	if (approve){
		if (owner_peer == NETPLAY_LOBBY_OWNER_NONE){
			(void)netplay_lobby_set_seat(seat, NETPLAY_LOBBY_OWNER_NONE, 0U);
			snprintf(note, sizeof(note), "[system] Approved release of P%u", (unsigned)(seat + 1U));
		}else{
			(void)netplay_lobby_set_seat(seat, owner_peer, pad_index);
			snprintf(note, sizeof(note), "[system] Approved P%u -> %s / %s", (unsigned)(seat + 1U), netplay_state.peer_name[0] ? netplay_state.peer_name : "Guest", netplay_state.peer_pad_labels[pad_index][0] ? netplay_state.peer_pad_labels[pad_index] : "Guest Pad");
		}
	}else{
		snprintf(note, sizeof(note), "[system] Denied %s request for P%u", netplay_state.peer_name[0] ? netplay_state.peer_name : "Guest", (unsigned)(seat + 1U));
		netplay_lobby_clear_request(seat);
	}
	netplay_lobby_note_remote(note);
	return TRUE;
}

void netplay_lobby_set_ready(boole ready)
{
	if (netplay_state.mode == NETPLAY_MODE_SERVER){
		netplay_state.lobby_local_ready = ready ? TRUE : FALSE;
		netplay_lobby_mark_dirty();
		if (netplay_state.peer_valid){
			(void)netplay_send_lobby_state();
		}
	}else if (netplay_state.mode == NETPLAY_MODE_CLIENT){
		netplay_state.lobby_local_ready = ready ? TRUE : FALSE;
		(void)netplay_send_lobby_ready_pkt(netplay_state.lobby_local_ready);
	}
}

boole netplay_lobby_start_match(void)
{
	if (netplay_state.mode != NETPLAY_MODE_SERVER){
		netplay_set_error("only host can start match");
		return FALSE;
	}
	if (!netplay_state.local_ready || !netplay_state.peer_ready){
		netplay_set_error("ROM sync not ready");
		return FALSE;
	}
	if (!netplay_state.lobby_local_ready || !netplay_state.lobby_peer_ready){
		netplay_set_error("players not ready");
		return FALSE;
	}
	if (!netplay_lobby_seat_map_valid()){
		netplay_set_error("seat map invalid");
		return FALSE;
	}
	netplay_state.peer_player_mask = netplay_lobby_peer_mask();
	return netplay_send_session_start();
}

boole netplay_lobby_send_chat(char const *text)
{
	char line[NETPLAY_LOBBY_CHAT_LINE_CHARS];
	if ((text == NULL) || (text[0] == 0)){
		return FALSE;
	}
	snprintf(line, sizeof(line), "%s: %s", netplay_cfg.local_name[0] ? netplay_cfg.local_name : ((netplay_state.mode == NETPLAY_MODE_SERVER) ? "Host" : "Guest"), text);
	netplay_chat_add_line(line);
	return netplay_send_chat_pkt(text);
}

auint netplay_lobby_get_chat_count(void)
{
	return netplay_state.chat_count;
}

boole netplay_lobby_get_chat_line(auint index, char *dst, auint dst_size)
{
	auint start;
	auint pos;
	if ((dst == NULL) || (dst_size == 0U) || (index >= netplay_state.chat_count)){
		return FALSE;
	}
	start = (netplay_state.chat_head + NETPLAY_LOBBY_CHAT_LINES - netplay_state.chat_count) % NETPLAY_LOBBY_CHAT_LINES;
	pos = (start + index) % NETPLAY_LOBBY_CHAT_LINES;
	strncpy(dst, netplay_state.chat_lines[pos], dst_size - 1U);
	dst[dst_size - 1U] = 0;
	return TRUE;
}

#endif /* ENABLE_NETPLAY */
