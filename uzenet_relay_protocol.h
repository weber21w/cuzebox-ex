#ifndef UZENET_RELAY_PROTOCOL_H
#define UZENET_RELAY_PROTOCOL_H

#include <stdint.h>

#define UZRL_MAGIC_U32 0x555A524CUL
#define UZRL_VERSION 1
#define UZRL_ROOM_CODE_LEN 8
#define UZRL_NAME_LEN 16
#define UZRL_MAX_RELAY_PAYLOAD 1200
#define UZRL_ADDRLEN_MAX 16
#define UZRL_GAME_TITLE_LEN 48
#define UZRL_ROOM_LIST_MAX 8

#pragma pack(push, 1)
typedef struct {
	uint32_t magic_be;
	uint8_t version;
	uint8_t type;
	uint8_t flags;
	uint8_t header_len;
	uint32_t session_id_be;
	uint32_t room_id_be;
	uint32_t seq_be;
	uint16_t payload_len_be;
	uint16_t reserved_be;
} uzrl_header_t;
#pragma pack(pop)

enum {
	UZRL_FLAG_ALLOW_DIRECT = 0x01,
	UZRL_FLAG_ALLOW_RELAY  = 0x02,
	UZRL_FLAG_ROOM_OWNER   = 0x04,
	UZRL_FLAG_PEER_IPV6    = 0x08,
	UZRL_FLAG_PUBLIC_ROOM  = 0x10
};

enum {
	UZRLT_CREATE_REQ        = 0x10,
	UZRLT_JOIN_REQ          = 0x11,
	UZRLT_HEARTBEAT         = 0x12,
	UZRLT_LEAVE             = 0x13,
	UZRLT_PUNCH_OK          = 0x14,
	UZRLT_LIST_REQ          = 0x15,
	UZRLT_RELAY_DATA        = 0x20,
	UZRLT_ROOM_CREATED      = 0x30,
	UZRLT_ROOM_JOINED       = 0x31,
	UZRLT_PEER_INFO         = 0x32,
	UZRLT_PEER_LEFT         = 0x33,
	UZRLT_PUNCH_STATUS      = 0x34,
	UZRLT_RELAY_FROM_PEER   = 0x35,
	UZRLT_HEARTBEAT_ACK     = 0x36,
	UZRLT_ROOM_LIST         = 0x37,
	UZRLT_ERROR             = 0x3F
};

enum {
	UZRL_ERR_NONE               = 0,
	UZRL_ERR_BAD_PACKET         = 1,
	UZRL_ERR_UNSUPPORTED        = 2,
	UZRL_ERR_ROOM_EXISTS        = 3,
	UZRL_ERR_ROOM_NOT_FOUND     = 4,
	UZRL_ERR_ROOM_FULL          = 5,
	UZRL_ERR_APP_MISMATCH       = 6,
	UZRL_ERR_NO_ROOM            = 7,
	UZRL_ERR_NO_PEER            = 8,
	UZRL_ERR_INTERNAL           = 9
};

enum {
	UZRL_AF_UNSPEC = 0,
	UZRL_AF_IPV4   = 4,
	UZRL_AF_IPV6   = 6
};

#pragma pack(push, 1)
typedef struct {
	char room_code[UZRL_ROOM_CODE_LEN];
	uint32_t app_id_be;
	char name[UZRL_NAME_LEN];
	uint8_t want_direct;
	uint8_t want_relay;
	uint8_t reserved[2];
} uzrl_room_req_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uzrl_room_req_t base;
	uint8_t room_flags;
	uint8_t reserved2[3];
	char game_title[UZRL_GAME_TITLE_LEN];
} uzrl_room_req_v2_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	char room_code[UZRL_ROOM_CODE_LEN];
	uint32_t app_id_be;
	uint8_t seat_index;
	uint8_t member_count;
	uint8_t direct_allowed;
	uint8_t relay_allowed;
	uint32_t assigned_session_id_be;
} uzrl_room_resp_t;
#pragma pack(pop)

/*
 * The first fields are kept compatible with the original IPv4-only layout.
 * For IPv4 peers, peer_ipv4_be / peer_port_be remain valid legacy fields.
 * For IPv6 peers, peer_ipv4_be is zero, peer_port_be remains valid, and the
 * addr_family / peer_addr fields carry the real endpoint.
 */
#pragma pack(push, 1)
typedef struct {
	uint32_t app_id_be;
	uint8_t limit;
	uint8_t reserved[3];
} uzrl_room_list_req_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	char room_code[UZRL_ROOM_CODE_LEN];
	char host_name[UZRL_NAME_LEN];
	char game_title[UZRL_GAME_TITLE_LEN];
	uint8_t member_count;
	uint8_t flags;
	uint8_t reserved[2];
} uzrl_room_list_entry_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint8_t count;
	uint8_t reserved[3];
	uzrl_room_list_entry_t entries[UZRL_ROOM_LIST_MAX];
} uzrl_room_list_resp_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32_t peer_session_id_be;
	uint32_t peer_ipv4_be;
	uint16_t peer_port_be;
	uint8_t peer_seat_index;
	uint8_t flags;
	char peer_name[UZRL_NAME_LEN];
	uint8_t addr_family;
	uint8_t addr_len;
	uint8_t reserved[2];
	uint8_t peer_addr[UZRL_ADDRLEN_MAX];
} uzrl_peer_info_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint8_t peer_ready;
	uint8_t direct_established;
	uint8_t relay_allowed;
	uint8_t reserved;
} uzrl_punch_status_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint32_t echo_tag_be;
	uint32_t server_time_ms_be;
} uzrl_heartbeat_ack_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint16_t code_be;
	char message[48];
} uzrl_error_t;
#pragma pack(pop)

#endif
