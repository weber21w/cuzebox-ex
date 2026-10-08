/*
 *  ESP8266 peripheral SoftAP / LAN overlay helpers
 */

#include "cu_esp.h"

#if !defined(ENABLE_ESP)
/* ESP8266 emulation disabled: translation unit intentionally empty. */
#else
#if !defined(ENABLE_ESP_SOFTAP)

#include <string.h>

void cu_esp_lan_init(cu_state_esp_t *es)
{
	(void)es;
}

void cu_esp_lan_shutdown(cu_state_esp_t *es)
{
	(void)es;
}

void cu_esp_lan_tick(cu_state_esp_t *es, uint32 now_ms)
{
	(void)es;
	(void)now_ms;
}

void cu_esp_lan_on_cwmode_change(cu_state_esp_t *es)
{
	(void)es;
}

void cu_esp_lan_on_softap_change(cu_state_esp_t *es)
{
	(void)es;
}

void cu_esp_lan_on_station_change(cu_state_esp_t *es)
{
	(void)es;
}

uint32 cu_esp_lan_cwlap_emit(cu_state_esp_t *es)
{
	(void)es;
	return 0U;
}

void cu_esp_ap_scan_emit(cu_state_esp_t *es, const cu_esp_cwlap_query_t *q)
{
	(void)es;
	(void)q;
}

sint32 cu_esp_ap_find_discovered(cu_state_esp_t *es, const char *ssid, const char *bssid)
{
	(void)es;
	(void)ssid;
	(void)bssid;
	return -1;
}

sint32 cu_esp_ap_begin_join(cu_state_esp_t *es, const char *ssid, const char *pwd, const char *bssid)
{
	(void)es;
	(void)ssid;
	(void)pwd;
	(void)bssid;
	return -1;
}

sint32 cu_esp_ap_start_join(cu_state_esp_t *es, const char *ssid, const char *pwd, const char *bssid)
{
	(void)es;
	(void)ssid;
	(void)pwd;
	(void)bssid;
	return -1;
}

void cu_esp_ap_join_tick(cu_state_esp_t *es, auint tdelta)
{
	(void)es;
	(void)tdelta;
}

sint32 cu_esp_ap_query_join(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	(void)es;
	if ((out != NULL) && (out_sz != 0U)){
		out[0] = 0;
	}
	return -1;
}

sint32 cu_esp_ap_query_station_mac(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	(void)es;
	if ((out != NULL) && (out_sz != 0U)){
		out[0] = 0;
	}
	return -1;
}

sint32 cu_esp_ap_set_station_mac(cu_state_esp_t *es, const sint8 *mac)
{
	(void)es;
	(void)mac;
	return -1;
}

sint32 cu_esp_ap_query_station_ip(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	(void)es;
	if ((out != NULL) && (out_sz != 0U)){
		out[0] = 0;
	}
	return -1;
}

sint32 cu_esp_ap_set_station_ip(cu_state_esp_t *es, const sint8 *ip, const sint8 *gw, const sint8 *nm)
{
	(void)es;
	(void)ip;
	(void)gw;
	(void)nm;
	return -1;
}

sint32 cu_esp_ap_query_softap_mac(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	(void)es;
	if ((out != NULL) && (out_sz != 0U)){
		out[0] = 0;
	}
	return -1;
}

sint32 cu_esp_ap_set_softap_mac(cu_state_esp_t *es, const sint8 *mac)
{
	(void)es;
	(void)mac;
	return -1;
}

sint32 cu_esp_ap_query_softap_ip(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	(void)es;
	if ((out != NULL) && (out_sz != 0U)){
		out[0] = 0;
	}
	return -1;
}

sint32 cu_esp_ap_set_softap_ip(cu_state_esp_t *es, const sint8 *ip, const sint8 *gw, const sint8 *nm)
{
	(void)es;
	(void)ip;
	(void)gw;
	(void)nm;
	return -1;
}

uint32 cu_esp_ap_emit_station_list(cu_state_esp_t *es)
{
	(void)es;
	return 0U;
}

void cu_esp_ap_disconnect(cu_state_esp_t *es, uint8 close_sockets)
{
	(void)es;
	(void)close_sockets;
}

void cu_esp_ap_leave(cu_state_esp_t *es)
{
	(void)es;
}

void cu_esp_ap_join_complete(cu_state_esp_t *es, sint32 ok)
{
	(void)es;
	(void)ok;
}

void cu_esp_ap_reset_leases(cu_state_esp_t *es)
{
	(void)es;
}

sint32 cu_esp_ap_allocate_lease(cu_state_esp_t *es, const uint8 sta_mac[6], uint32 *out_ip_be)
{
	(void)es;
	(void)sta_mac;
	if (out_ip_be != NULL){
		*out_ip_be = 0U;
	}
	return -1;
}

void cu_esp_ap_release_lease(cu_state_esp_t *es, const uint8 sta_mac[6])
{
	(void)es;
	(void)sta_mac;
}

void cu_esp_ap_age_leases(cu_state_esp_t *es, uint32 now_ms)
{
	(void)es;
	(void)now_ms;
}

uint8 cu_esp_lan_virtual_link_present(cu_state_esp_t *es, uint32 sock)
{
	(void)es;
	(void)sock;
	return 0U;
}

uint8 cu_esp_lan_virtual_link_open(cu_state_esp_t *es, uint32 sock)
{
	(void)es;
	(void)sock;
	return 0U;
}

sint32 cu_esp_lan_virtual_open(cu_state_esp_t *es, uint32 sock, const char *host, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode)
{
	(void)es;
	(void)sock;
	(void)host;
	(void)port;
	(void)type;
	(void)local_port;
	(void)udp_mode;
	return -1;
}

sint32 cu_esp_lan_virtual_send(cu_state_esp_t *es, uint32 sock, const uint8 *buf, uint16 len)
{
	(void)es;
	(void)sock;
	(void)buf;
	(void)len;
	return -1;
}

sint32 cu_esp_lan_virtual_recv(cu_state_esp_t *es, uint32 sock, uint8 *buf, uint16 len)
{
	(void)es;
	(void)sock;
	(void)buf;
	(void)len;
	return -1;
}

void cu_esp_lan_virtual_close(cu_state_esp_t *es, uint32 sock, uint8 notify_remote)
{
	(void)es;
	(void)sock;
	(void)notify_remote;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef __EMSCRIPTEN__
	#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		#include <winsock2.h>
		#include <ws2tcpip.h>
	#else
		#include <unistd.h>
		#include <errno.h>
		#include <fcntl.h>
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <arpa/inet.h>
		#include <netinet/in.h>
		#include <netdb.h>
	#endif
#endif

#define CU_ESP_LAN_ADV_MAGIC		0x43554150u /* 'CUAP' */
#define CU_ESP_LAN_ADV_VERSION	1u
#define CU_ESP_LAN_ADV_INTERVAL_MS	1000u
#define CU_ESP_LAN_LEASE_TIME_MS	300000u

typedef struct{
	uint32	magic_be;
	uint8	version;
	uint8	channel;
	sint8	rssi;
	uint8	enc;
	uint16	ucast_port_be;
	uint8	bssid[6];
	sint8	ssid[CU_ESP_LAN_SSID_MAX];
}cu_esp_lan_adv_pkt_t;

#define CU_ESP_LAN_CTL_MAGIC		0x43554c4eu /* 'CULN' */
#define CU_ESP_LAN_CTL_VERSION	1u
#define CU_ESP_LAN_CTL_JOIN_REQ	1u
#define CU_ESP_LAN_CTL_JOIN_RESP	2u
#define CU_ESP_LAN_CTL_LEASE_RENEW	3u
#define CU_ESP_LAN_CTL_LEAVE	4u
#define CU_ESP_LAN_CTL_OPEN_REQ	5u
#define CU_ESP_LAN_CTL_OPEN_RESP	6u
#define CU_ESP_LAN_CTL_DATA	7u
#define CU_ESP_LAN_CTL_CLOSE	8u

#define CU_ESP_LAN_JOIN_RETRY_MS	700u
#define CU_ESP_LAN_JOIN_TIMEOUT_MS	5000u
#define CU_ESP_LAN_OPEN_TIMEOUT_MS	5000u
#define CU_ESP_LAN_RENEW_INTERVAL_MS	60000u

#define CU_ESP_LAN_JOIN_OK		0u
#define CU_ESP_LAN_JOIN_BAD_SSID	1u
#define CU_ESP_LAN_JOIN_BAD_AUTH	2u
#define CU_ESP_LAN_JOIN_NO_LEASE	3u
#define CU_ESP_LAN_JOIN_BAD_BSSID	4u
#define CU_ESP_LAN_JOIN_BAD_IPCFG	5u

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	uint32	magic_be;
	uint8	version;
	uint8	type;
	uint16	len_be;
	uint32	seq_be;
}cu_esp_lan_ctl_hdr_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	sta_mac[6];
	uint8	bssid[6];
	sint8	ssid[CU_ESP_LAN_SSID_MAX];
	sint8	pwd[64];
}cu_esp_lan_join_req_pkt_t;

#define CU_ESP_LAN_JOIN_REQ_IPMODE_DHCP	0u
#define CU_ESP_LAN_JOIN_REQ_IPMODE_STATIC	1u

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_join_req_pkt_t	base;
	uint8	ip_mode;
	uint8	reserved0[3];
	uint32	req_ip_be;
	uint32	req_gw_be;
	uint32	req_mask_be;
	uint32	req_dns_be;
}cu_esp_lan_join_req_ex_pkt_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	status;
	uint8	channel;
	uint8	enc;
	uint8	reserved0;
	uint8	sta_mac[6];
	uint8	bssid[6];
	uint32	ip_be;
	uint32	gw_be;
	uint32	mask_be;
	uint32	dns_be;
}cu_esp_lan_join_resp_pkt_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	sta_mac[6];
	uint8	bssid[6];
}cu_esp_lan_simple_sta_pkt_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	sta_mac[6];
	uint8	bssid[6];
	uint8	vlink_id;
	uint8	reserved0;
	uint32	type_be;
	uint16	port_be;
	uint16	local_port_be;
	uint8	udp_mode;
	uint8	reserved1[3];
	sint8	host[CU_ESP_LAN_HOST_MAX];
}cu_esp_lan_open_req_pkt_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	status;
	uint8	vlink_id;
	uint8	overlay_id;
	uint8	reserved0;
	uint32	type_be;
	uint32	peer_ipv4_be;
	uint16	peer_port_be;
	uint16	reserved1;
}cu_esp_lan_open_resp_pkt_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	vlink_id;
	uint8	overlay_id;
	uint16	data_len_be;
	uint8	data[CU_ESP_LAN_DATA_MAX];
}cu_esp_lan_data_pkt_t;

typedef struct
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
{
	cu_esp_lan_ctl_hdr_t	h;
	uint8	sta_mac[6];
	uint8	bssid[6];
	uint8	vlink_id;
	uint8	overlay_id;
}cu_esp_lan_close_pkt_t;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

static void cu_esp_lan_close_socket_pair(cu_state_esp_t *es);
static sint32 cu_esp_lan_open_socket_pair(cu_state_esp_t *es);
static void cu_esp_lan_update_mode(cu_state_esp_t *es);
static uint8 cu_esp_lan_mode_uses_sta(const cu_state_esp_t *es);
static uint8 cu_esp_lan_mode_uses_softap(const cu_state_esp_t *es);
static void cu_esp_lan_age_aps(cu_state_esp_t *es, uint32 now_ms);
static void cu_esp_lan_poll_mcast(cu_state_esp_t *es, uint32 now_ms);
static void cu_esp_lan_poll_ucast(cu_state_esp_t *es, uint32 now_ms);
static void cu_esp_lan_handle_adv(cu_state_esp_t *es, const cu_esp_lan_adv_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_ctl(cu_state_esp_t *es, const void *pkt, sint32 len,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_join_req(cu_state_esp_t *es, const cu_esp_lan_join_req_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms, sint32 len);
static void cu_esp_lan_handle_join_resp(cu_state_esp_t *es, const cu_esp_lan_join_resp_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_lease_renew(cu_state_esp_t *es, const cu_esp_lan_simple_sta_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_leave(cu_state_esp_t *es, const cu_esp_lan_simple_sta_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_open_req(cu_state_esp_t *es, const cu_esp_lan_open_req_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_open_resp(cu_state_esp_t *es, const cu_esp_lan_open_resp_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_handle_data(cu_state_esp_t *es, const cu_esp_lan_data_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms, sint32 len);
static void cu_esp_lan_handle_close(cu_state_esp_t *es, const cu_esp_lan_close_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms);
static void cu_esp_lan_send_adv(cu_state_esp_t *es);
static sint32 cu_esp_lan_send_join_req(cu_state_esp_t *es);
static sint32 cu_esp_lan_send_join_resp(cu_state_esp_t *es, const struct sockaddr_in *to, uint32 seq,
	uint8 status, const uint8 sta_mac[6], uint32 ip_be, uint32 gw_be, uint32 mask_be, uint32 dns_be);
static sint32 cu_esp_lan_send_simple_sta(cu_state_esp_t *es, uint8 type, const struct sockaddr_in *to,
	uint32 seq, const uint8 sta_mac[6], const uint8 bssid[6]);
static sint32 cu_esp_lan_send_open_req(cu_state_esp_t *es, uint32 sock, const char *host, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode);
static sint32 cu_esp_lan_send_open_resp(cu_state_esp_t *es, const struct sockaddr_in *to, uint32 seq, uint8 status, uint8 vlink_id, uint8 overlay_id, uint32 type, const struct sockaddr_in *peer);
static sint32 cu_esp_lan_send_data(cu_state_esp_t *es, const struct sockaddr_in *to, uint8 vlink_id, uint8 overlay_id, const uint8 *data, uint16 len);
static sint32 cu_esp_lan_send_close(cu_state_esp_t *es, const struct sockaddr_in *to, const uint8 sta_mac[6], const uint8 bssid[6], uint8 vlink_id, uint8 overlay_id);
static sint32 cu_esp_lan_find_ap_slot(cu_state_esp_t *es, const uint8 bssid[6]);
static sint32 cu_esp_lan_find_free_ap_slot(cu_state_esp_t *es);
static uint8 cu_esp_parse_mac6_local(const char *s, uint8 mac[6]);
static void cu_esp_format_mac6_local(const uint8 mac[6], char *out, size_t out_sz);
static void cu_esp_format_sockaddr_local(const struct sockaddr_in *sa, char *out, size_t out_sz);
static char const* cu_esp_lan_proto_name_local(uint32 type) CU_UNUSED_FN;
static char const* cu_esp_lan_join_status_name(uint8 status) CU_UNUSED_FN;
static uint8 cu_esp_mac_equal6(const uint8 a[6], const uint8 b[6]);
static uint8 cu_esp_ascii_ieq_char_local(int c0, int c1);
static uint8 cu_esp_ascii_ieq_str_local(const char *a, const char *b);
typedef struct{
	const char *ssid;
	char	mac[24];
	sint16	rssi;
	uint8	ecn;
	uint8	channel;
	sint16	freq_offset;
	sint16	freqcal_val;
	uint8	pairwise_cipher;
	uint8	group_cipher;
	uint8	bgn;
	uint8	wps;
}cu_esp_cwlap_rec_t;

static uint8 cu_esp_cwlap_field_on_local(uint32 mask, uint8 bit);
static sint32 cu_esp_cwlap_emit_adv(const cu_esp_lan_ap_entry_t *ap, uint32 print_mask);
static uint8 cu_esp_cwlap_match_adv(const cu_esp_lan_ap_entry_t *ap, const cu_esp_cwlap_query_t *q);
static void cu_esp_cwlap_sort_idx(cu_state_esp_t *es, sint32 *idx, sint32 count);
static sint32 cu_esp_cwlap_emit_one_local(const cu_esp_cwlap_rec_t *rec, uint32 print_mask);
static uint8 cu_esp_cwlap_match_rec_local(const cu_esp_cwlap_rec_t *rec, const cu_esp_cwlap_query_t *q);
static void cu_esp_cwlap_sort_recs_local(cu_esp_cwlap_rec_t *recs, sint32 count);
static void cu_esp_ap_add_fake_scan_recs(cu_state_esp_t *es, cu_esp_cwlap_rec_t *recs, sint32 *rec_count, sint32 rec_max, const cu_esp_cwlap_query_t *q);
static void cu_esp_ap_add_softap_scan_rec(cu_state_esp_t *es, cu_esp_cwlap_rec_t *recs, sint32 *rec_count, sint32 rec_max, const cu_esp_cwlap_query_t *q);
static uint32 cu_esp_ipv4_to_be_local(const char *ipstr, uint32 fallback_be);
static void cu_esp_ipv4_be_to_str_local(uint32 be, char *out, size_t out_sz);
static uint8 cu_esp_ipv4_be_is_zero_local(uint32 be);
static uint8 cu_esp_lan_reserve_ip(cu_state_esp_t *es, const uint8 sta_mac[6], uint32 ip_be, const struct sockaddr_in *from, uint32 now_ms);
static uint8 cu_esp_lan_validate_static_join(cu_state_esp_t *es, uint32 req_ip_be, uint32 req_gw_be, uint32 req_mask_be, uint32 *out_ip_be, uint32 *out_gw_be, uint32 *out_mask_be, uint32 *out_dns_be);
static void cu_esp_ap_refresh_softap_ipv4_state(cu_state_esp_t *es);
static uint8 cu_esp_ap_get_station_mac_bytes(cu_state_esp_t *es, uint8 mac[6]);
static void cu_esp_lan_begin_local_join_success(cu_state_esp_t *es);
static sint32 cu_esp_lan_find_free_relay(cu_state_esp_t *es);
static sint32 cu_esp_lan_find_relay(cu_state_esp_t *es, const struct sockaddr_in *from, const uint8 sta_mac[6], uint8 vlink_id, uint8 overlay_id);
static sint32 cu_esp_lan_find_station_relay(cu_state_esp_t *es, const struct sockaddr_in *from, uint8 vlink_id, uint8 overlay_id);
static void cu_esp_lan_close_relay(cu_state_esp_t *es, sint32 idx, uint8 notify_sta, const char *reason);
static void cu_esp_lan_poll_relays(cu_state_esp_t *es, uint32 now_ms);
static sint32 cu_esp_lan_queue_vrx(cu_state_esp_t *es, uint32 sock, const uint8 *data, uint16 len);
static sint32 cu_esp_lan_open_relay_socket(cu_state_esp_t *es, cu_esp_lan_relay_t *rl, const char *host, uint32 port, uint32 type);
static sint32 cu_esp_lan_finish_relay_connect(cu_state_esp_t *es, sint32 idx, uint32 now_ms);
static sint32 cu_esp_lan_flush_relay_tx(cu_state_esp_t *es, sint32 idx, uint32 now_ms);
static sint32 cu_esp_lan_queue_relay_tx(cu_state_esp_t *es, sint32 idx, const uint8 *data, uint16 len, uint32 now_ms);
static uint8 cu_esp_socket_connect_pending_local(void);
static uint8 cu_esp_lan_set_would_block_local(void);
static sint32 cu_esp_cwlap_emit_one_local(const cu_esp_cwlap_rec_t *rec, uint32 print_mask)
{
	char ap_buffer[256];
	char body[220];
	size_t pos = 0u;
	uint8 first = 1u;

	if(rec == NULL)
		return 0;

#define CWLAP_ADD_COMMA_LOCAL() do{ if(!first) body[pos++] = ','; first = 0u; }while(0)
	if(cu_esp_cwlap_field_on_local(print_mask, 0)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)rec->ecn); }
	if(cu_esp_cwlap_field_on_local(print_mask, 1)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "\"%s\"", rec->ssid ? rec->ssid : ""); }
	if(cu_esp_cwlap_field_on_local(print_mask, 2)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%d", (int)rec->rssi); }
	if(cu_esp_cwlap_field_on_local(print_mask, 3)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "\"%s\"", rec->mac[0] ? rec->mac : "00:00:00:00:00:00"); }
	if(cu_esp_cwlap_field_on_local(print_mask, 4)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)rec->channel); }
	if(cu_esp_cwlap_field_on_local(print_mask, 5)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%d", (int)rec->freq_offset); }
	if(cu_esp_cwlap_field_on_local(print_mask, 6)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%d", (int)rec->freqcal_val); }
	if(cu_esp_cwlap_field_on_local(print_mask, 7)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)rec->pairwise_cipher); }
	if(cu_esp_cwlap_field_on_local(print_mask, 8)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)rec->group_cipher); }
	if(cu_esp_cwlap_field_on_local(print_mask, 9)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)rec->bgn); }
	if(cu_esp_cwlap_field_on_local(print_mask, 10)){ CWLAP_ADD_COMMA_LOCAL(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)rec->wps); }
#undef CWLAP_ADD_COMMA_LOCAL

	body[(pos < sizeof(body)) ? pos : (sizeof(body) - 1u)] = '\0';
	snprintf(ap_buffer, sizeof(ap_buffer), "+CWLAP:(%s)\r\n", body);
	cu_esp_txp(ap_buffer);
	return 1;
}

static uint8 cu_esp_cwlap_match_rec_local(const cu_esp_cwlap_rec_t *rec, const cu_esp_cwlap_query_t *q)
{
	if(rec == NULL || q == NULL)
		return 0u;
	if(rec->rssi < q->rssi_filter)
		return 0u;
	if(q->authmask != 0u && (q->authmask & (uint32)(1u << rec->ecn)) == 0u)
		return 0u;
	if(q->ssid != NULL && q->ssid[0] != 0){
		if(strcmp(q->ssid, rec->ssid ? rec->ssid : "") != 0)
			return 0u;
	}
	if(q->bssid != NULL && q->bssid[0] != 0){
		if(!cu_esp_ascii_ieq_str_local(q->bssid, rec->mac))
			return 0u;
	}
	if(q->channel > 0 && rec->channel != (uint8)q->channel)
		return 0u;
	return 1u;
}

static void cu_esp_cwlap_sort_recs_local(cu_esp_cwlap_rec_t *recs, sint32 count)
{
	sint32 i;
	sint32 j;
	for(i = 0; i < count; ++i){
		for(j = i + 1; j < count; ++j){
			if(recs[j].rssi > recs[i].rssi){
				cu_esp_cwlap_rec_t tmp = recs[i];
				recs[i] = recs[j];
				recs[j] = tmp;
			}
		}
	}
}

static void cu_esp_ap_add_fake_scan_recs(cu_state_esp_t *es, cu_esp_cwlap_rec_t *recs, sint32 *rec_count, sint32 rec_max, const cu_esp_cwlap_query_t *q)
{
	sint32 i;
	(void)es;
	if(recs == NULL || rec_count == NULL || q == NULL)
		return;
	for(i = 0; i < ESP_NUM_FAKE_APS; ++i){
		cu_esp_cwlap_rec_t rec;
		if(*rec_count >= rec_max)
			break;
		memset(&rec, 0, sizeof(rec));
		rec.ssid = fake_ap_name[i];
		snprintf(rec.mac, sizeof(rec.mac), "%s", fake_ap_mac[i]);
		rec.rssi = (sint16)(-30 - ((i * 7) % 55));
		rec.ecn = (uint8)(i % 5);
		rec.channel = (uint8)(1 + (i % 11));
		rec.freq_offset = (sint16)((i & 1) ? -2 : 1);
		rec.freqcal_val = (sint16)(40 + i);
		rec.pairwise_cipher = (uint8)((rec.ecn == 0u) ? 0u : ((rec.ecn >= 4u) ? 4u : rec.ecn));
		rec.group_cipher = rec.pairwise_cipher;
		rec.bgn = 7u;
		rec.wps = (uint8)((i & 1) ? 1u : 0u);
		if(cu_esp_cwlap_match_rec_local(&rec, q))
			recs[(*rec_count)++] = rec;
	}
}

static void cu_esp_ap_add_softap_scan_rec(cu_state_esp_t *es, cu_esp_cwlap_rec_t *recs, sint32 *rec_count, sint32 rec_max, const cu_esp_cwlap_query_t *q)
{
	cu_esp_cwlap_rec_t rec;

	if(es == NULL || recs == NULL || rec_count == NULL || q == NULL)
		return;
	if(*rec_count >= rec_max)
		return;
	if(!(es->wifi_mode == ESP_WIFI_MODE_SOFTAP || es->wifi_mode == ESP_WIFI_MODE_SOFTAP_STATION))
		return;
	if(es->soft_ap_enabled == 0u)
		return;
	if(es->soft_ap_name[0] == 0)
		return;

	memset(&rec, 0, sizeof(rec));
	rec.ssid = (const char *)es->soft_ap_name;
	snprintf(rec.mac, sizeof(rec.mac), "%s", (char *)es->soft_ap_mac);
	rec.rssi = -18;
	rec.ecn = (uint8)((es->soft_ap_encryption <= 7u) ? es->soft_ap_encryption : 3u);
	rec.channel = (uint8)((es->soft_ap_channel != 0u) ? es->soft_ap_channel : 1u);
	rec.freq_offset = 0;
	rec.freqcal_val = 48;
	rec.pairwise_cipher = (uint8)((rec.ecn == 0u) ? 0u : ((rec.ecn >= 4u) ? 4u : rec.ecn));
	rec.group_cipher = rec.pairwise_cipher;
	rec.bgn = 7u;
	rec.wps = 0u;
	if(cu_esp_cwlap_match_rec_local(&rec, q))
		recs[(*rec_count)++] = rec;
}

static uint8 cu_esp_socket_would_block(void);
static void cu_esp_socket_close_local(ESP_SOCKET s);
static sint32 cu_esp_socket_set_nonblock_local(ESP_SOCKET s);

void cu_esp_lan_init(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	es->lan.now_ms = 0u;
	es->lan.last_adv_ms = 0u;
	es->lan.join_req_pending = 0u;
	es->lan.join_result = 0u;
	es->lan.join_fail_code = 0u;
	es->lan.join_req_id = 0u;
	es->lan.join_req_sent_ms = 0u;
	es->lan.join_deadline_ms = 0u;
	es->lan.lease_renew_ms = 0u;
	memset(es->lan.vlinks, 0, sizeof(es->lan.vlinks));
	memset(es->lan.relays, 0, sizeof(es->lan.relays));
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++)
		es->lan.relays[i].sock = ESP_INVALID_SOCKET;
	cu_esp_ap_refresh_softap_ipv4_state(es);
	cu_esp_lan_update_mode(es);
}

void cu_esp_lan_shutdown(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	cu_esp_lan_close_socket_pair(es);
}

void cu_esp_lan_tick(cu_state_esp_t *es, uint32 now_ms)
{
	if(es == NULL)
		return;

	es->lan.now_ms = now_ms;
	cu_esp_lan_update_mode(es);
	cu_esp_lan_age_aps(es, now_ms);
	cu_esp_ap_age_leases(es, now_ms);

#ifndef __EMSCRIPTEN__
	if(es->lan.enable){
		cu_esp_lan_poll_mcast(es, now_ms);
		cu_esp_lan_poll_ucast(es, now_ms);
		cu_esp_lan_poll_relays(es, now_ms);
		if(es->lan.adv_enable){
			if((now_ms - es->lan.last_adv_ms) >= CU_ESP_LAN_ADV_INTERVAL_MS){
				cu_esp_lan_send_adv(es);
				es->lan.last_adv_ms = now_ms;
			}
		}
		if(es->lan.join_req_pending && es->lan.join_target_addr.sin_family != 0){
			if(es->lan.join_req_sent_ms == 0u || (now_ms - es->lan.join_req_sent_ms) >= CU_ESP_LAN_JOIN_RETRY_MS){
				(void)cu_esp_lan_send_join_req(es);
			}
		}
		if(es->lan.joined && es->lan.joined_ap_addr.sin_family != 0 &&
		   	(es->lan.lease_renew_ms == 0u || (now_ms - es->lan.lease_renew_ms) >= CU_ESP_LAN_RENEW_INTERVAL_MS)){
			uint8 sta_mac[6];
			if(cu_esp_ap_get_station_mac_bytes(es, sta_mac)){
				(void)cu_esp_lan_send_simple_sta(es, CU_ESP_LAN_CTL_LEASE_RENEW,
					&es->lan.joined_ap_addr, 0u, sta_mac, es->lan.joined_ap_bssid);
				es->lan.lease_renew_ms = now_ms;
			}
		}
		for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
			cu_esp_lan_vlink_t *vl = &es->lan.vlinks[i];
			if(vl->pending && vl->open_deadline_ms != 0u && now_ms >= vl->open_deadline_ms){
				vl->pending = 0u;
				es->link_state[i] = CU_ESP_LS_ERROR;
				es->link_err[i] = -30;
				es->link_notice_err[i] = 1u;
				memset(vl, 0, sizeof(*vl));
			}
		}
	}
#endif
}

void cu_esp_lan_on_cwmode_change(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	if(!cu_esp_lan_mode_uses_sta(es))
		cu_esp_ap_leave(es);
	if(!cu_esp_lan_mode_uses_softap(es))
		cu_esp_ap_reset_leases(es);
	cu_esp_lan_update_mode(es);
}

void cu_esp_lan_on_softap_change(cu_state_esp_t *es)
{
	uint8 tmp_mac[6];

	if(es == NULL)
		return;

	if(cu_esp_parse_mac6_local((const char *)es->soft_ap_mac, tmp_mac))
		memcpy(es->lan.local_mac, tmp_mac, 6u);

	cu_esp_ap_refresh_softap_ipv4_state(es);
	cu_esp_lan_update_mode(es);
	es->lan.last_adv_ms = 0u;
}

void cu_esp_lan_on_station_change(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	cu_esp_lan_update_mode(es);
}

uint32 cu_esp_lan_cwlap_emit(cu_state_esp_t *es)
{
	cu_esp_cwlap_query_t q;

	if(es == NULL)
		return 0u;

	memset(&q, 0, sizeof(q));
	q.rssi_filter = es->cwlap_rssi_filter;
	q.authmask = es->cwlap_authmask;
	q.print_mask = es->cwlap_print_mask;
	q.sort_enable = es->cwlap_sort_rssi;
	cu_esp_ap_scan_emit(es, &q);
	return 0u;
}

void cu_esp_ap_scan_emit(cu_state_esp_t *es, const cu_esp_cwlap_query_t *q)
{
	sint32 idx[CU_ESP_LAN_MAX_APS];
	sint32 count = 0;
	sint32 i;
	uint32 print_mask;
	cu_esp_cwlap_rec_t recs[ESP_NUM_FAKE_APS + 1];
	sint32 rec_count = 0;

	if(es == NULL || q == NULL)
		return;

	print_mask = q->print_mask;
	if(print_mask == 0u)
		print_mask = es->cwlap_print_mask;

	for(i = 0; i < (sint32)CU_ESP_LAN_MAX_APS; ++i){
		if(cu_esp_cwlap_match_adv(&es->lan.aps[i], q))
			idx[count++] = i;
	}

	cu_esp_ap_add_fake_scan_recs(es, recs, &rec_count, (sint32)(sizeof(recs) / sizeof(recs[0])), q);
	cu_esp_ap_add_softap_scan_rec(es, recs, &rec_count, (sint32)(sizeof(recs) / sizeof(recs[0])), q);

	if(q->sort_enable){
		if(count > 1)
			cu_esp_cwlap_sort_idx(es, idx, count);
		if(rec_count > 1)
			cu_esp_cwlap_sort_recs_local(recs, rec_count);
	}

	for(i = 0; i < count; ++i){
		(void)cu_esp_cwlap_emit_adv(&es->lan.aps[idx[i]], print_mask);
		cu_esp_timed_stall(ESP_AT_CWLAP_INTER_DELAY);
	}
	for(i = 0; i < rec_count; ++i){
		(void)cu_esp_cwlap_emit_one_local(&recs[i], print_mask);
		cu_esp_timed_stall(ESP_AT_CWLAP_INTER_DELAY);
	}
}

sint32 cu_esp_ap_find_discovered(cu_state_esp_t *es, const char *ssid, const char *bssid)
{
	uint8 mac[6];
	uint8 have_mac = 0u;
	sint32 i;

	if(es == NULL)
		return -1;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	/*
	 * On Windows, Winsock must be initialized before any socket() call.
	 * ESP reset reaches the LAN overlay path before the normal network reset
	 * path, so initialize sockets here if needed.
	 */
	if(!es->winsock_enabled){
		if(cu_esp_init_sockets() != 0)
			return -1;
	}
#endif

	if(bssid != NULL && bssid[0] != '\0')
		have_mac = cu_esp_parse_mac6_local(bssid, mac);

	for(i = 0; i < (sint32)CU_ESP_LAN_MAX_APS; ++i){
		cu_esp_lan_ap_entry_t *ap = &es->lan.aps[i];
		if(!ap->in_use)
			continue;
		if(ssid != NULL && ssid[0] != '\0'){
			if(strcmp((const char *)ap->ssid, ssid) != 0)
				continue;
		}
		if(have_mac && !cu_esp_mac_equal6(ap->bssid, mac))
			continue;
		return i;
	}

	return -1;
}

sint32 cu_esp_ap_begin_join(cu_state_esp_t *es, const char *ssid, const char *pwd, const char *bssid)
{
	sint32 idx;

	(void)pwd;

	if(es == NULL || ssid == NULL || ssid[0] == '\0')
		return -1;

	idx = cu_esp_ap_find_discovered(es, ssid, bssid);
	if(idx < 0)
		return -1;

	memcpy(&es->lan.join_target_addr, &es->lan.aps[idx].adv_addr, sizeof(es->lan.join_target_addr));
	memcpy(es->lan.join_target_bssid, es->lan.aps[idx].bssid, 6u);
	memset(&es->lan.joined_ap_addr, 0, sizeof(es->lan.joined_ap_addr));
	memset(es->lan.joined_ap_bssid, 0, sizeof(es->lan.joined_ap_bssid));
	es->lan.joined = 0u;
	es->lan.join_result = 0u;
	es->lan.join_fail_code = 0u;
	return idx;
}

sint32 cu_esp_ap_start_join(cu_state_esp_t *es, const char *ssid, const char *pwd, const char *bssid)
{
	sint32 ap_idx;

	if(es == NULL || ssid == NULL || ssid[0] == '\0' || pwd == NULL)
		return -1;

	snprintf((char *)es->wifi_name, sizeof(es->wifi_name), "%s", ssid);
	snprintf((char *)es->wifi_pass, sizeof(es->wifi_pass), "%s", pwd);

	ap_idx = cu_esp_ap_begin_join(es, ssid, pwd, bssid);
	if(ap_idx >= 0){
		cu_esp_lan_ap_entry_t *ap = &es->lan.aps[ap_idx];
		es->wifi_channel = ap->channel ? ap->channel : ESP_DEFAULT_CHANNEL;
		es->wifi_rssi = ap->rssi;
		snprintf((char *)es->wifi_mac, sizeof(es->wifi_mac),
			"%02x:%02x:%02x:%02x:%02x:%02x",
			(unsigned int)ap->bssid[0], (unsigned int)ap->bssid[1], (unsigned int)ap->bssid[2],
			(unsigned int)ap->bssid[3], (unsigned int)ap->bssid[4], (unsigned int)ap->bssid[5]);
		es->lan.join_req_pending = 1u;
		es->lan.join_req_id = (uint32)rand();
		if(es->lan.join_req_id == 0u)
			es->lan.join_req_id = 1u;
		es->lan.join_req_sent_ms = 0u;
		es->lan.join_deadline_ms = es->lan.now_ms + CU_ESP_LAN_JOIN_TIMEOUT_MS;
		es->lan.join_result = 0u;
		es->lan.join_fail_code = 0u;
		es->join_delay_timer = 0u;
		es->ip_delay_timer = 0u;
	}else{
		memset(&es->lan.join_target_addr, 0, sizeof(es->lan.join_target_addr));
		memset(es->lan.join_target_bssid, 0, sizeof(es->lan.join_target_bssid));
		memset(&es->lan.joined_ap_addr, 0, sizeof(es->lan.joined_ap_addr));
		memset(es->lan.joined_ap_bssid, 0, sizeof(es->lan.joined_ap_bssid));
		es->lan.join_req_pending = 0u;
		es->lan.join_result = 0u;
		es->lan.join_fail_code = 0u;
		es->wifi_channel = ESP_DEFAULT_CHANNEL;
		es->wifi_rssi = -30;
		if(es->wifi_mac[0] == '\0')
			snprintf((char *)es->wifi_mac, sizeof(es->wifi_mac), "00:00:00:00:00:00");
		es->join_delay_timer = ESP_AT_CWJAP_DELAY + ((auint)(rand() % 1000) * ESP_AT_MS_DELAY);
		es->ip_delay_timer = es->join_delay_timer + ESP_AT_CWJAP_DELAY;
	}

	es->state &= ~ESP_AP_CONNECTED;
	es->state &= ~ESP_INTERNET_ACCESS;
	es->wifi_join_pending = 1u;
	es->lan.lease_renew_ms = 0u;

	cu_esp_lan_on_station_change(es);
	return 0;
}

void cu_esp_ap_join_tick(cu_state_esp_t *es, auint tdelta)
{
	if(es == NULL)
		return;

	if(es->lan.join_req_pending && es->lan.join_deadline_ms != 0u){
		if((es->lan.now_ms - es->lan.join_deadline_ms) < 0x80000000u){
			es->lan.join_req_pending = 0u;
			es->wifi_join_pending = 0u;
			cu_esp_ap_leave(es);
			cu_esp_txp("+CWJAP:1\r\nERROR\r\n");
		}
	}
	if(es->lan.join_result == 2u){
		uint8 code = es->lan.join_fail_code;
		es->lan.join_result = 0u;
		es->lan.join_fail_code = 0u;
		es->wifi_join_pending = 0u;
		cu_esp_ap_leave(es);
		if(code == CU_ESP_LAN_JOIN_BAD_AUTH)
			cu_esp_txp("+CWJAP:2\r\nERROR\r\n");
		else
			cu_esp_txp("+CWJAP:1\r\nERROR\r\n");
	}

	if(es->ip_delay_timer){
		if(es->ip_delay_timer <= tdelta){
			es->ip_delay_timer = 0;
			es->state |= ESP_AP_CONNECTED;
			es->state |= ESP_INTERNET_ACCESS;
			print_message("ESP: Wifi Got IP\n");
			cu_esp_txp("WIFI GOT IP\r\n");
			if(es->wifi_join_pending){
				es->wifi_join_pending = 0u;
				cu_esp_txp_ok();
			}
		}else{
			es->ip_delay_timer -= tdelta;
		}
	}

	if(es->join_delay_timer){
		if(es->join_delay_timer <= tdelta){
			es->join_delay_timer = 0;
			cu_esp_txp("WIFI CONNECTED\r\n");
		}else{
			es->join_delay_timer -= tdelta;
		}
	}
}

static uint32 cu_esp_ipv4_to_be_local(const char *ipstr, uint32 fallback_be)
{
	if(ipstr == NULL || ipstr[0] == 0)
		return fallback_be;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		unsigned long x = inet_addr(ipstr);
		if(x == INADDR_NONE && strcmp(ipstr, "255.255.255.255") != 0)
			return fallback_be;
		return (uint32)x;
	}
#else
	{
		struct in_addr addr;
		if(inet_pton(AF_INET, ipstr, &addr) == 1)
			return addr.s_addr;
	}
	return fallback_be;
#endif
}

static void cu_esp_ipv4_be_to_str_local(uint32 be, char *out, size_t out_sz)
{
	struct in_addr addr;
	const char *p;

	if(out == NULL || out_sz == 0u)
		return;
	addr.s_addr = be;
	p = inet_ntoa(addr);
	if(p == NULL)
		p = "0.0.0.0";
	snprintf(out, out_sz, "%s", p);
}

static uint8 cu_esp_ipv4_be_is_zero_local(uint32 be)
{
	return (uint8)((ntohl(be) == 0u) ? 1u : 0u);
}

static uint8 cu_esp_lan_reserve_ip(cu_state_esp_t *es, const uint8 sta_mac[6], uint32 ip_be, const struct sockaddr_in *from, uint32 now_ms)
{
	sint32 free_idx = -1;
	sint32 i;

	if(es == NULL || sta_mac == NULL || ip_be == 0u)
		return 0u;

	for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
		if(es->lan.leases[i].in_use){
			if(cu_esp_mac_equal6(es->lan.leases[i].mac, sta_mac)){
				memcpy(es->lan.leases[i].mac, sta_mac, 6u);
				es->lan.leases[i].ip_be = ip_be;
				es->lan.leases[i].expire_ms = now_ms + CU_ESP_LAN_LEASE_TIME_MS;
				if(from != NULL)
					memcpy(&es->lan.leases[i].peer_addr, from, sizeof(*from));
				return 1u;
			}
			if(es->lan.leases[i].ip_be == ip_be)
				return 0u;
		}else if(free_idx < 0){
			free_idx = i;
		}
	}

	if(free_idx < 0)
		return 0u;

	memset(&es->lan.leases[free_idx], 0, sizeof(es->lan.leases[free_idx]));
	es->lan.leases[free_idx].in_use = 1u;
	memcpy(es->lan.leases[free_idx].mac, sta_mac, 6u);
	es->lan.leases[free_idx].ip_be = ip_be;
	es->lan.leases[free_idx].expire_ms = now_ms + CU_ESP_LAN_LEASE_TIME_MS;
	{ char macbuf[32]; char ipbuf[24]; cu_esp_format_mac6_local(sta_mac, macbuf, sizeof(macbuf)); cu_esp_ipv4_be_to_str_local(ip_be, ipbuf, sizeof(ipbuf)); //CZ_DEBUG_INFO("esp.lan", "reserve ip mac=%s ip=%s", macbuf, ipbuf);
}
	if(from != NULL)
		memcpy(&es->lan.leases[free_idx].peer_addr, from, sizeof(*from));
	return 1u;
}

static uint8 cu_esp_lan_validate_static_join(cu_state_esp_t *es, uint32 req_ip_be, uint32 req_gw_be, uint32 req_mask_be, uint32 *out_ip_be, uint32 *out_gw_be, uint32 *out_mask_be, uint32 *out_dns_be)
{
	uint32 ap_ip_be;
	uint32 ap_mask_be;
	uint32 ap_ip_host;
	uint32 ap_mask_host;
	uint32 req_ip_host;
	uint32 net_host;
	uint32 bcast_host;
	uint32 req_gw_eff;
	uint32 req_mask_eff;

	if(es == NULL || out_ip_be == NULL || out_gw_be == NULL || out_mask_be == NULL || out_dns_be == NULL)
		return 0u;
	if(cu_esp_ipv4_be_is_zero_local(req_ip_be))
		return 0u;

	ap_ip_be = es->lan.ap_ip_be;
	ap_mask_be = es->lan.netmask_be;
	if(cu_esp_ipv4_be_is_zero_local(ap_ip_be))
		ap_ip_be = htonl(0x0A000001u);
	if(cu_esp_ipv4_be_is_zero_local(ap_mask_be))
		ap_mask_be = htonl(0xFFFFFF00u);

	req_mask_eff = cu_esp_ipv4_be_is_zero_local(req_mask_be) ? ap_mask_be : req_mask_be;
	if(req_mask_eff != ap_mask_be)
		return 0u;

	req_gw_eff = cu_esp_ipv4_be_is_zero_local(req_gw_be) ? ap_ip_be : req_gw_be;
	if(req_gw_eff != ap_ip_be)
		return 0u;

	ap_ip_host = ntohl(ap_ip_be);
	ap_mask_host = ntohl(ap_mask_be);
	req_ip_host = ntohl(req_ip_be);
	net_host = ap_ip_host & ap_mask_host;
	bcast_host = net_host | (~ap_mask_host);

	if((req_ip_host & ap_mask_host) != net_host)
		return 0u;
	if(req_ip_host == net_host || req_ip_host == bcast_host || req_ip_host == ap_ip_host)
		return 0u;

	*out_ip_be = req_ip_be;
	*out_gw_be = req_gw_eff;
	*out_mask_be = req_mask_eff;
	*out_dns_be = cu_esp_ipv4_be_is_zero_local(es->lan.dns_ip_be) ? ap_ip_be : es->lan.dns_ip_be;
	return 1u;
}

static uint8 cu_esp_ap_get_station_mac_bytes(cu_state_esp_t *es, uint8 mac[6])
{
	if(es == NULL || mac == NULL)
		return 0u;
	if(cu_esp_parse_mac6_local((const char *)es->station_mac, mac))
		return 1u;
	if(cu_esp_parse_mac6_local((const char *)es->soft_ap_mac, mac))
		return 1u;
	memcpy(mac, es->lan.local_mac, 6u);
	return 1u;
}

static void cu_esp_lan_begin_local_join_success(cu_state_esp_t *es)
{
	if(es == NULL)
		return;
	es->join_delay_timer = ESP_AT_CWJAP_DELAY;
	es->ip_delay_timer = es->join_delay_timer + ESP_AT_CWJAP_DELAY;
}

static void cu_esp_ap_refresh_softap_ipv4_state(cu_state_esp_t *es)
{
	uint32 ap_be;
	uint32 mask_be;
	uint32 ap_host;
	uint32 mask_host;
	uint32 net_host;

	if(es == NULL)
		return;

	ap_be = cu_esp_ipv4_to_be_local((const char *)es->soft_ap_ip, htonl(0x0A000001u));
	mask_be = cu_esp_ipv4_to_be_local((const char *)es->soft_ap_netmask, htonl(0xFFFFFF00u));

	es->lan.ap_ip_be = ap_be;
	es->lan.netmask_be = mask_be;
	es->lan.dns_ip_be = ap_be;

	ap_host = ntohl(ap_be);
	mask_host = ntohl(mask_be);
	net_host = ap_host & mask_host;
	es->lan.pool_start_be = htonl(net_host | 100u);
	es->lan.pool_end_be = htonl(net_host | 111u);
}

sint32 cu_esp_ap_query_station_ip(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	if(es == NULL || out == NULL || out_sz == 0u)
		return -1;

	snprintf((char *)out, (size_t)out_sz,
		"+CIPSTA:ip:\"%s\"\r\n+CIPSTA:gateway:\"%s\"\r\n+CIPSTA:netmask:\"%s\"\r\nOK\r\n",
		(char *)es->station_ip,
		(char *)es->station_gateway,
		(char *)es->station_netmask);
	return 0;
}

sint32 cu_esp_ap_set_station_ip(cu_state_esp_t *es, const sint8 *ip, const sint8 *gw, const sint8 *nm)
{
	if(es == NULL || ip == NULL || ip[0] == 0)
		return -1;

	snprintf((char *)es->station_ip, sizeof(es->station_ip), "%s", (const char *)ip);
	if(gw != NULL && gw[0] != 0)
		snprintf((char *)es->station_gateway, sizeof(es->station_gateway), "%s", (const char *)gw);
	if(nm != NULL && nm[0] != 0)
		snprintf((char *)es->station_netmask, sizeof(es->station_netmask), "%s", (const char *)nm);

	cu_esp_lan_on_station_change(es);
	return 0;
}

sint32 cu_esp_ap_query_softap_ip(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	if(es == NULL || out == NULL || out_sz == 0u)
		return -1;

	snprintf((char *)out, (size_t)out_sz,
		"+CIPAP:ip:\"%s\"\r\n+CIPAP:gateway:\"%s\"\r\n+CIPAP:netmask:\"%s\"\r\nOK\r\n",
		(char *)es->soft_ap_ip,
		(char *)es->soft_ap_gateway,
		(char *)es->soft_ap_netmask);
	return 0;
}

sint32 cu_esp_ap_set_softap_ip(cu_state_esp_t *es, const sint8 *ip, const sint8 *gw, const sint8 *nm)
{
	if(es == NULL || ip == NULL || ip[0] == 0)
		return -1;

	snprintf((char *)es->soft_ap_ip, sizeof(es->soft_ap_ip), "%s", (const char *)ip);
	if(gw != NULL && gw[0] != 0)
		snprintf((char *)es->soft_ap_gateway, sizeof(es->soft_ap_gateway), "%s", (const char *)gw);
	if(nm != NULL && nm[0] != 0)
		snprintf((char *)es->soft_ap_netmask, sizeof(es->soft_ap_netmask), "%s", (const char *)nm);

	cu_esp_ap_refresh_softap_ipv4_state(es);
	cu_esp_lan_on_softap_change(es);
	return 0;
}

sint32 cu_esp_ap_query_join(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	if(es == NULL || out == NULL || out_sz == 0u)
		return -1;

	if((es->state & ESP_AP_CONNECTED) == 0u)
		return -1;

	snprintf((char *)out, (size_t)out_sz,
		"+CWJAP:\"%s\",\"%s\",%u,%d\r\nOK\r\n",
		(char *)es->wifi_name,
		(char *)es->wifi_mac,
		(unsigned int)es->wifi_channel,
		(int)es->wifi_rssi);
	return 0;
}

sint32 cu_esp_ap_query_station_mac(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	if(es == NULL || out == NULL || out_sz == 0u)
		return -1;

	snprintf((char *)out, (size_t)out_sz,
		"+CIPSTAMAC:\"%s\"\r\nOK\r\n",
		(char *)es->station_mac);
	return 0;
}

sint32 cu_esp_ap_set_station_mac(cu_state_esp_t *es, const sint8 *mac)
{
	uint8 tmp_mac[6];
	char norm[24];

	if(es == NULL || mac == NULL || mac[0] == 0)
		return -1;
	if(!cu_esp_parse_mac6_local((const char *)mac, tmp_mac))
		return -1;

	cu_esp_format_mac6_local(tmp_mac, norm, sizeof(norm));
	snprintf((char *)es->station_mac, sizeof(es->station_mac), "%s", norm);
	cu_esp_lan_on_station_change(es);
	return 0;
}

sint32 cu_esp_ap_query_softap_mac(cu_state_esp_t *es, sint8 *out, auint out_sz)
{
	if(es == NULL || out == NULL || out_sz == 0u)
		return -1;

	snprintf((char *)out, (size_t)out_sz,
		"+CIPAPMAC:\"%s\"\r\nOK\r\n",
		(char *)es->soft_ap_mac);
	return 0;
}

sint32 cu_esp_ap_set_softap_mac(cu_state_esp_t *es, const sint8 *mac)
{
	uint8 tmp_mac[6];
	char norm[24];

	if(es == NULL || mac == NULL || mac[0] == 0)
		return -1;
	if(!cu_esp_parse_mac6_local((const char *)mac, tmp_mac))
		return -1;

	cu_esp_format_mac6_local(tmp_mac, norm, sizeof(norm));
	snprintf((char *)es->soft_ap_mac, sizeof(es->soft_ap_mac), "%s", norm);
	cu_esp_lan_on_softap_change(es);
	return 0;
}

uint32 cu_esp_ap_emit_station_list(cu_state_esp_t *es)
{
	uint32 count = 0u;
	sint32 i;
	char ipbuf[32];
	char macbuf[24];
	char line[96];

	if(es == NULL)
		return 0u;

	for(i = 0; i < CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
		struct in_addr ina;

		if(!es->lan.leases[i].in_use)
			continue;

		ina.s_addr = es->lan.leases[i].ip_be;
		snprintf(ipbuf, sizeof(ipbuf), "%s", inet_ntoa(ina));
		cu_esp_format_mac6_local(es->lan.leases[i].mac, macbuf, sizeof(macbuf));
		snprintf(line, sizeof(line), "%s,%s\r\n", ipbuf, macbuf);
		cu_esp_txp(line);
		++count;
	}

	return count;
}

void cu_esp_ap_disconnect(cu_state_esp_t *es, uint8 close_sockets)
{
	uint8 sta_mac[6];

	if(es == NULL)
		return;

	if(close_sockets != 0u){
		cu_esp_close_socket(ESP_ALL_CONNECTIONS);
		memset(es->link_is_ssl, 0, sizeof(es->link_is_ssl));
	}
	if(es->lan.joined && es->lan.joined_ap_addr.sin_family != 0 && cu_esp_ap_get_station_mac_bytes(es, sta_mac))
		(void)cu_esp_lan_send_simple_sta(es, CU_ESP_LAN_CTL_LEAVE, &es->lan.joined_ap_addr, 0u, sta_mac, es->lan.joined_ap_bssid);

	cu_esp_ap_leave(es);
	es->state &= ~ESP_AP_CONNECTED;
	es->state &= ~ESP_INTERNET_ACCESS;
	cu_esp_lan_on_station_change(es);
}

void cu_esp_ap_leave(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	es->lan.joined = 0u;
	es->lan.join_req_pending = 0u;
	es->lan.join_result = 0u;
	es->lan.join_fail_code = 0u;
	es->lan.join_req_id = 0u;
	es->lan.join_req_sent_ms = 0u;
	es->lan.join_deadline_ms = 0u;
	es->lan.lease_renew_ms = 0u;
	es->wifi_join_pending = 0u;
	es->join_delay_timer = 0;
	es->ip_delay_timer = 0;
	memset(&es->lan.join_target_addr, 0, sizeof(es->lan.join_target_addr));
	memset(&es->lan.joined_ap_addr, 0, sizeof(es->lan.joined_ap_addr));
	memset(es->lan.join_target_bssid, 0, sizeof(es->lan.join_target_bssid));
	memset(es->lan.joined_ap_bssid, 0, sizeof(es->lan.joined_ap_bssid));
}

void cu_esp_ap_join_complete(cu_state_esp_t *es, sint32 ok)
{
	if(es == NULL)
		return;

	es->lan.joined = (ok != 0) ? 1u : 0u;
	es->lan.join_req_pending = 0u;
	es->lan.join_result = (ok != 0) ? 1u : 2u;
	es->lan.join_fail_code = (ok != 0) ? 0u : CU_ESP_LAN_JOIN_BAD_AUTH;
	es->lan.join_req_sent_ms = 0u;
	es->lan.join_deadline_ms = 0u;
	es->lan.lease_renew_ms = es->lan.now_ms;
}

uint8 cu_esp_lan_virtual_link_present(cu_state_esp_t *es, uint32 sock)
{
	if(es == NULL || sock >= ESP_MAX_LINKS)
		return 0u;
	if(es->lan.vlinks[sock].active || es->lan.vlinks[sock].pending)
		return 1u;
	if(es->lan.vlinks[sock].rx_len != 0u)
		return 1u;
	if(es->lan.vlinks[sock].closed_remote)
		return 1u;
	return 0u;
}

uint8 cu_esp_lan_virtual_link_open(cu_state_esp_t *es, uint32 sock)
{
	if(es == NULL || sock >= ESP_MAX_LINKS)
		return 0u;
	return (uint8)((es->lan.vlinks[sock].active && !es->lan.vlinks[sock].closed_remote) ? 1u : 0u);
}

sint32 cu_esp_lan_virtual_open(cu_state_esp_t *es, uint32 sock, const char *host, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode)
{
	cu_esp_lan_vlink_t *vl;

	if(es == NULL || host == NULL || host[0] == 0 || sock >= ESP_MAX_LINKS)
		return -1;
	if(!es->lan.joined || es->lan.joined_ap_addr.sin_family != AF_INET)
		return -1;

	vl = &es->lan.vlinks[sock];
	memset(vl, 0, sizeof(*vl));
	vl->pending = 1u;
	vl->type = type;
	vl->local_port = (uint16)local_port;
	vl->udp_mode = (uint8)udp_mode;
	vl->open_req_id = ((uint32)rand() << 1) ^ (uint32)(sock + 1u) ^ es->lan.now_ms;
	if(vl->open_req_id == 0u)
		vl->open_req_id = (uint32)(sock + 1u);
	vl->open_deadline_ms = es->lan.now_ms + CU_ESP_LAN_OPEN_TIMEOUT_MS;
	vl->last_io_ms = es->lan.now_ms;

	es->proto[sock] = type;
	es->protocol[sock] = type;
	es->link_type[sock] = type;
	es->link_port[sock] = port;
	es->udp_local_port[sock] = (uint16)local_port;
	es->udp_mode[sock] = (uint8)udp_mode;
	es->udp_mode1_latched[sock] = 0u;
	es->udp_send_override[sock] = 0u;
	memset(&es->udp_send_info[sock], 0, sizeof(es->udp_send_info[sock]));
	memset(&es->sock_info[sock], 0, sizeof(es->sock_info[sock]));
	es->link_state[sock] = CU_ESP_LS_CONNECT;
	es->link_err[sock] = 0;
	es->link_notice_ok[sock] = 0u;
	es->link_notice_err[sock] = 0u;
	es->link_ready_ms[sock] = 0u;
	memset(es->link_host[sock], 0, sizeof(es->link_host[sock]));
	strncpy(es->link_host[sock], host, sizeof(es->link_host[sock]) - 1u);

	if(cu_esp_lan_send_open_req(es, sock, host, port, type, local_port, udp_mode) != 0){
		memset(vl, 0, sizeof(*vl));
		es->link_state[sock] = CU_ESP_LS_ERROR;
		es->link_err[sock] = -31;
		es->link_notice_err[sock] = 1u;
		return -1;
	}

	return 0;
}

sint32 cu_esp_lan_virtual_send(cu_state_esp_t *es, uint32 sock, const uint8 *buf, uint16 len)
{
	cu_esp_lan_vlink_t *vl;
	uint16 sent = 0u;

	if(es == NULL || buf == NULL || sock >= ESP_MAX_LINKS)
		return -1;
	vl = &es->lan.vlinks[sock];
	if(!vl->active || vl->closed_remote)
		return -1;
	while(sent < len){
		uint16 chunk = (uint16)(len - sent);
		if(chunk > CU_ESP_LAN_DATA_MAX)
			chunk = CU_ESP_LAN_DATA_MAX;
		if(cu_esp_lan_send_data(es, &es->lan.joined_ap_addr, (uint8)sock, vl->overlay_id, buf + sent, chunk) != 0)
			return (sent != 0u) ? (sint32)sent : -1;
		sent = (uint16)(sent + chunk);
		vl->last_io_ms = es->lan.now_ms;
	}
	return (sint32)sent;
}

sint32 cu_esp_lan_virtual_recv(cu_state_esp_t *es, uint32 sock, uint8 *buf, uint16 len)
{
	cu_esp_lan_vlink_t *vl;
	uint16 take;

	if(es == NULL || buf == NULL || sock >= ESP_MAX_LINKS)
		return -1;
	vl = &es->lan.vlinks[sock];
	if(vl->rx_len == 0u){
		if(vl->closed_remote){
			es->link_state[sock] = CU_ESP_LS_ERROR;
			es->link_err[sock] = 0;
			es->link_notice_err[sock] = 1u;
			memset(vl, 0, sizeof(*vl));
			return 0;
		}
		(void)cu_esp_lan_set_would_block_local();
		return -1;
	}
	take = (len < vl->rx_len) ? len : vl->rx_len;
	if(vl->rx_head + take <= CU_ESP_LAN_VRX_MAX){
		memcpy(buf, &vl->rx_buf[vl->rx_head], take);
	}else{
		uint16 first = (uint16)(CU_ESP_LAN_VRX_MAX - vl->rx_head);
		memcpy(buf, &vl->rx_buf[vl->rx_head], first);
		memcpy(buf + first, &vl->rx_buf[0], (uint16)(take - first));
	}
	vl->rx_head = (uint16)((vl->rx_head + take) % CU_ESP_LAN_VRX_MAX);
	vl->rx_len = (uint16)(vl->rx_len - take);
	vl->last_io_ms = es->lan.now_ms;
	if(vl->rx_len == 0u && vl->closed_remote){
		es->link_state[sock] = CU_ESP_LS_ERROR;
		es->link_err[sock] = 0;
		es->link_notice_err[sock] = 1u;
		memset(vl, 0, sizeof(*vl));
	}
	return (sint32)take;
}

void cu_esp_lan_virtual_close(cu_state_esp_t *es, uint32 sock, uint8 notify_remote)
{
	cu_esp_lan_vlink_t *vl;
	uint8 sta_mac[6];

	if(es == NULL || sock >= ESP_MAX_LINKS)
		return;
	vl = &es->lan.vlinks[sock];
	if(notify_remote && es->lan.joined && es->lan.joined_ap_addr.sin_family == AF_INET &&
	   (vl->active || vl->pending) && cu_esp_ap_get_station_mac_bytes(es, sta_mac))
		(void)cu_esp_lan_send_close(es, &es->lan.joined_ap_addr, sta_mac, es->lan.joined_ap_bssid, (uint8)sock, vl->overlay_id);
	memset(vl, 0, sizeof(*vl));
}

void cu_esp_ap_reset_leases(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	memset(es->lan.leases, 0, sizeof(es->lan.leases));
}

sint32 cu_esp_ap_allocate_lease(cu_state_esp_t *es, const uint8 sta_mac[6], uint32 *out_ip_be)
{
	uint32 start_host;
	uint32 end_host;
	uint32 host_ip;
	sint32 i;
	sint32 free_idx = -1;

	if(es == NULL || sta_mac == NULL || out_ip_be == NULL)
		return -1;

	start_host = ntohl(es->lan.pool_start_be);
	end_host = ntohl(es->lan.pool_end_be);
	if(start_host == 0u || end_host < start_host){
		uint32 base = ntohl(es->lan.ap_ip_be);
		if(base == 0u)
			base = (192u << 24) | (168u << 16) | (4u << 8) | 1u;
		start_host = (base & 0xFFFFFF00u) | 100u;
		end_host = start_host + (uint32)CU_ESP_LAN_DHCP_MAX_LEASES - 1u;
	}

	for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
		if(es->lan.leases[i].in_use && cu_esp_mac_equal6(es->lan.leases[i].mac, sta_mac)){
			es->lan.leases[i].expire_ms = es->lan.now_ms + CU_ESP_LAN_LEASE_TIME_MS;
			*out_ip_be = es->lan.leases[i].ip_be;
			{ char macbuf[32]; char ipbuf[24]; cu_esp_format_mac6_local(sta_mac, macbuf, sizeof(macbuf)); cu_esp_ipv4_be_to_str_local(*out_ip_be, ipbuf, sizeof(ipbuf));
//CZ_DEBUG_VERBOSE("esp.lan", "reuse lease mac=%s ip=%s", macbuf, ipbuf);
}
			return 0;
		}
		if(!es->lan.leases[i].in_use && free_idx < 0)
			free_idx = i;
	}

	if(free_idx < 0)
		return -1;

	for(host_ip = start_host; host_ip <= end_host; ++host_ip){
		uint32 cand_be = htonl(host_ip);
		uint8 used = 0u;
		for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
			if(es->lan.leases[i].in_use && es->lan.leases[i].ip_be == cand_be){
				used = 1u;
				break;
			}
		}
		if(!used){
			memset(&es->lan.leases[free_idx], 0, sizeof(es->lan.leases[free_idx]));
			es->lan.leases[free_idx].in_use = 1u;
			memcpy(es->lan.leases[free_idx].mac, sta_mac, 6u);
			es->lan.leases[free_idx].ip_be = cand_be;
			es->lan.leases[free_idx].expire_ms = es->lan.now_ms + CU_ESP_LAN_LEASE_TIME_MS;
			*out_ip_be = cand_be;
			{ char macbuf[32]; char ipbuf[24]; cu_esp_format_mac6_local(sta_mac, macbuf, sizeof(macbuf)); cu_esp_ipv4_be_to_str_local(*out_ip_be, ipbuf, sizeof(ipbuf)); 
//CZ_DEBUG_INFO("esp.lan", "allocate dhcp lease mac=%s ip=%s", macbuf, ipbuf);
}
			return 0;
		}
		if(host_ip == 0xFFFFFFFFu)
			break;
	}

	return -1;
}

void cu_esp_ap_release_lease(cu_state_esp_t *es, const uint8 sta_mac[6])
{
	sint32 i;

	if(es == NULL || sta_mac == NULL)
		return;

	for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
		if(es->lan.leases[i].in_use && cu_esp_mac_equal6(es->lan.leases[i].mac, sta_mac)){
			char macbuf[32];
			char ipbuf[24];
			cu_esp_format_mac6_local(sta_mac, macbuf, sizeof(macbuf));
			cu_esp_ipv4_be_to_str_local(es->lan.leases[i].ip_be, ipbuf, sizeof(ipbuf));
			//CZ_DEBUG_INFO("esp.lan", "release lease mac=%s ip=%s", macbuf, ipbuf);
			memset(&es->lan.leases[i], 0, sizeof(es->lan.leases[i]));
			return;
		}
	}
}

void cu_esp_ap_age_leases(cu_state_esp_t *es, uint32 now_ms)
{
	sint32 i;

	if(es == NULL)
		return;

	for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
		if(es->lan.leases[i].in_use && ((now_ms - es->lan.leases[i].expire_ms) < 0x80000000u)){
			char macbuf[32];
			char ipbuf[24];
			cu_esp_format_mac6_local(es->lan.leases[i].mac, macbuf, sizeof(macbuf));
			cu_esp_ipv4_be_to_str_local(es->lan.leases[i].ip_be, ipbuf, sizeof(ipbuf));
			//CZ_DEBUG_INFO("esp.lan", "lease expired mac=%s ip=%s", macbuf, ipbuf);
			memset(&es->lan.leases[i], 0, sizeof(es->lan.leases[i]));
		}
	}
}

static void cu_esp_lan_close_socket_pair(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

#ifndef __EMSCRIPTEN__
	if(es->lan.mcast_sock != ESP_INVALID_SOCKET && es->lan.mcast_sock != ESP_SOCKET_ERROR)
		cu_esp_socket_close_local(es->lan.mcast_sock);
	if(es->lan.ucast_sock != ESP_INVALID_SOCKET && es->lan.ucast_sock != ESP_SOCKET_ERROR)
		cu_esp_socket_close_local(es->lan.ucast_sock);
#endif
	es->lan.mcast_sock = ESP_INVALID_SOCKET;
	es->lan.ucast_sock = ESP_INVALID_SOCKET;
	es->lan.ucast_port = 0u;
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++){
		if(es->lan.relays[i].sock != ESP_INVALID_SOCKET && es->lan.relays[i].sock != ESP_SOCKET_ERROR)
			cu_esp_socket_close_local(es->lan.relays[i].sock);
		memset(&es->lan.relays[i], 0, sizeof(es->lan.relays[i]));
		es->lan.relays[i].sock = ESP_INVALID_SOCKET;
	}
}

static sint32 cu_esp_lan_open_socket_pair(cu_state_esp_t *es)
{
#ifndef __EMSCRIPTEN__
	struct sockaddr_in sa;
	int reuse = 1;
	struct ip_mreq mreq;
	socklen_t slen;

	if(es == NULL)
		return -1;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	/*
	 * The LAN overlay may open sockets during reset before the normal ESP
	 * network reset path runs. Make sure Winsock is initialized first.
	 */
	if(!es->winsock_enabled){
		if(cu_esp_init_sockets() != 0)
			return -1;
	}
#endif
	if(es->lan.mcast_sock != ESP_INVALID_SOCKET && es->lan.mcast_sock != ESP_SOCKET_ERROR &&
	   es->lan.ucast_sock != ESP_INVALID_SOCKET && es->lan.ucast_sock != ESP_SOCKET_ERROR)
		return 0;

	cu_esp_lan_close_socket_pair(es);

	es->lan.mcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(es->lan.mcast_sock == ESP_INVALID_SOCKET || es->lan.mcast_sock == ESP_SOCKET_ERROR)
		goto fail;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	setsockopt(es->lan.mcast_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#else
	setsockopt(es->lan.mcast_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(CU_ESP_LAN_MCAST_PORT);
	if(bind(es->lan.mcast_sock, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		goto fail;
	if(cu_esp_socket_set_nonblock_local(es->lan.mcast_sock) != 0)
		goto fail;

	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = inet_addr(CU_ESP_LAN_MCAST_ADDR);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	setsockopt(es->lan.mcast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
#else
	setsockopt(es->lan.mcast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
#endif

	es->lan.ucast_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		goto fail;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(0u);
	if(bind(es->lan.ucast_sock, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		goto fail;
	if(cu_esp_socket_set_nonblock_local(es->lan.ucast_sock) != 0)
		goto fail;
	slen = (socklen_t)sizeof(sa);
	if(getsockname(es->lan.ucast_sock, (struct sockaddr *)&sa, &slen) == 0)
		es->lan.ucast_port = ntohs(sa.sin_port);

	return 0;
fail:
	cu_esp_lan_close_socket_pair(es);
	return -1;
#else
	(void)es;
	return -1;
#endif
}

static void cu_esp_lan_update_mode(cu_state_esp_t *es)
{
	if(es == NULL)
		return;

	es->lan.adv_enable = (uint8)((es->lan.enable && cu_esp_lan_mode_uses_softap(es) && es->soft_ap_name[0] != 0) ? 1u : 0u);

#ifndef __EMSCRIPTEN__
	if(es->lan.enable && (cu_esp_lan_mode_uses_sta(es) || es->lan.adv_enable)){
		(void)cu_esp_lan_open_socket_pair(es);
	}else{
		cu_esp_lan_close_socket_pair(es);
	}
#endif
}

static uint8 cu_esp_lan_mode_uses_sta(const cu_state_esp_t *es)
{
	return (uint8)((es->wifi_mode == ESP_WIFI_MODE_STATION || es->wifi_mode == ESP_WIFI_MODE_SOFTAP_STATION) ? 1u : 0u);
}

static uint8 cu_esp_lan_mode_uses_softap(const cu_state_esp_t *es)
{
	if(es == NULL)
		return 0u;
	if(es->soft_ap_enabled == 0u)
		return 0u;
	return (uint8)((es->wifi_mode == ESP_WIFI_MODE_SOFTAP || es->wifi_mode == ESP_WIFI_MODE_SOFTAP_STATION) ? 1u : 0u);
}

static void cu_esp_lan_age_aps(cu_state_esp_t *es, uint32 now_ms)
{
	sint32 i;

	for(i = 0; i < (sint32)CU_ESP_LAN_MAX_APS; ++i){
		if(es->lan.aps[i].in_use && ((now_ms - es->lan.aps[i].last_seen_ms) >= CU_ESP_LAN_AP_EXPIRE_MS))
			memset(&es->lan.aps[i], 0, sizeof(es->lan.aps[i]));
	}
}

static void cu_esp_lan_poll_mcast(cu_state_esp_t *es, uint32 now_ms)
{
#ifndef __EMSCRIPTEN__
	for(;;){
		cu_esp_lan_adv_pkt_t pkt;
		struct sockaddr_in from;
		socklen_t flen = (socklen_t)sizeof(from);
		sint32 rd;

		if(es->lan.mcast_sock == ESP_INVALID_SOCKET || es->lan.mcast_sock == ESP_SOCKET_ERROR)
			return;

		rd = (sint32)recvfrom(es->lan.mcast_sock, (char *)&pkt, sizeof(pkt), 0,
			(struct sockaddr *)&from, &flen);
		if(rd < 0){
			if(cu_esp_socket_would_block())
				break;
			break;
		}
		if((size_t)rd < sizeof(pkt))
			continue;
		cu_esp_lan_handle_adv(es, &pkt, &from, now_ms);
	}
#else
	(void)es;
	(void)now_ms;
#endif
}

static void cu_esp_lan_handle_adv(cu_state_esp_t *es, const cu_esp_lan_adv_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	sint32 idx;

	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(ntohl(pkt->magic_be) != CU_ESP_LAN_ADV_MAGIC)
		return;
	if(pkt->version != CU_ESP_LAN_ADV_VERSION)
		return;
	if(pkt->ssid[0] == 0)
		return;
	if(cu_esp_mac_equal6(pkt->bssid, es->lan.local_mac))
		return;

	idx = cu_esp_lan_find_ap_slot(es, pkt->bssid);
	if(idx < 0)
		idx = cu_esp_lan_find_free_ap_slot(es);
	if(idx < 0)
		return;

	es->lan.aps[idx].in_use = 1u;
	memcpy(es->lan.aps[idx].bssid, pkt->bssid, 6u);
	memcpy(es->lan.aps[idx].ssid, pkt->ssid, CU_ESP_LAN_SSID_MAX);
	es->lan.aps[idx].ssid[CU_ESP_LAN_SSID_MAX - 1u] = 0;
	es->lan.aps[idx].channel = pkt->channel;
	es->lan.aps[idx].rssi = pkt->rssi;
	es->lan.aps[idx].enc = pkt->enc;
	es->lan.aps[idx].adv_port = ntohs(pkt->ucast_port_be);
	memcpy(&es->lan.aps[idx].adv_addr, from, sizeof(*from));
	es->lan.aps[idx].adv_addr.sin_port = htons(es->lan.aps[idx].adv_port);
	es->lan.aps[idx].last_seen_ms = now_ms;
}

static void cu_esp_lan_poll_ucast(cu_state_esp_t *es, uint32 now_ms)
{
#ifndef __EMSCRIPTEN__
	for(;;){
		uint8 buf[256];
		struct sockaddr_in from;
		socklen_t flen = (socklen_t)sizeof(from);
		sint32 rd;

		if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
			return;

		rd = (sint32)recvfrom(es->lan.ucast_sock, (char *)buf, sizeof(buf), 0,
			(struct sockaddr *)&from, &flen);
		if(rd < 0){
			if(cu_esp_socket_would_block())
				break;
			break;
		}
		if(rd == 0)
			break;
		cu_esp_lan_handle_ctl(es, buf, rd, &from, now_ms);
	}
#else
	(void)es;
	(void)now_ms;
#endif
}

static void cu_esp_lan_handle_ctl(cu_state_esp_t *es, const void *pkt, sint32 len,
	const struct sockaddr_in *from, uint32 now_ms)
{
	const cu_esp_lan_ctl_hdr_t *h;
	uint16 body_len;

	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(len < (sint32)sizeof(cu_esp_lan_ctl_hdr_t))
		return;

	h = (const cu_esp_lan_ctl_hdr_t *)pkt;
	if(ntohl(h->magic_be) != CU_ESP_LAN_CTL_MAGIC)
		return;
	if(h->version != CU_ESP_LAN_CTL_VERSION)
		return;
	body_len = ntohs(h->len_be);
	if((sint32)(sizeof(cu_esp_lan_ctl_hdr_t) + body_len) > len)
		return;

	switch(h->type){
		case CU_ESP_LAN_CTL_JOIN_REQ:
			if(len >= (sint32)sizeof(cu_esp_lan_join_req_pkt_t))
				cu_esp_lan_handle_join_req(es, (const cu_esp_lan_join_req_pkt_t *)pkt, from, now_ms, len);
			break;
		case CU_ESP_LAN_CTL_JOIN_RESP:
			if(len >= (sint32)sizeof(cu_esp_lan_join_resp_pkt_t))
				cu_esp_lan_handle_join_resp(es, (const cu_esp_lan_join_resp_pkt_t *)pkt, from, now_ms);
			break;
		case CU_ESP_LAN_CTL_LEASE_RENEW:
			if(len >= (sint32)sizeof(cu_esp_lan_simple_sta_pkt_t))
				cu_esp_lan_handle_lease_renew(es, (const cu_esp_lan_simple_sta_pkt_t *)pkt, from, now_ms);
			break;
		case CU_ESP_LAN_CTL_LEAVE:
			if(len >= (sint32)sizeof(cu_esp_lan_simple_sta_pkt_t))
				cu_esp_lan_handle_leave(es, (const cu_esp_lan_simple_sta_pkt_t *)pkt, from, now_ms);
			break;
		case CU_ESP_LAN_CTL_OPEN_REQ:
			if(len >= (sint32)sizeof(cu_esp_lan_open_req_pkt_t))
				cu_esp_lan_handle_open_req(es, (const cu_esp_lan_open_req_pkt_t *)pkt, from, now_ms);
			break;

		case CU_ESP_LAN_CTL_OPEN_RESP:
			if(len >= (sint32)sizeof(cu_esp_lan_open_resp_pkt_t))
				cu_esp_lan_handle_open_resp(es, (const cu_esp_lan_open_resp_pkt_t *)pkt, from, now_ms);
			break;

		case CU_ESP_LAN_CTL_DATA:
			if(len >= (sint32)sizeof(cu_esp_lan_data_pkt_t))
				cu_esp_lan_handle_data(es, (const cu_esp_lan_data_pkt_t *)pkt, from, now_ms, len);
			break;

		case CU_ESP_LAN_CTL_CLOSE:
			if(len >= (sint32)sizeof(cu_esp_lan_close_pkt_t))
				cu_esp_lan_handle_close(es, (const cu_esp_lan_close_pkt_t *)pkt, from, now_ms);
			break;

		default:
			break;
	}
}

static void cu_esp_lan_handle_join_req(cu_state_esp_t *es, const cu_esp_lan_join_req_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms, sint32 len)
{
	uint8 status = CU_ESP_LAN_JOIN_OK;
	uint32 ip_be = 0u;
	uint32 gw_be = 0u;
	uint32 mask_be = 0u;
	uint32 dns_be = 0u;
	uint8 ip_mode = CU_ESP_LAN_JOIN_REQ_IPMODE_DHCP;
	uint32 req_ip_be = 0u;
	uint32 req_gw_be = 0u;
	uint32 req_mask_be = 0u;
	uint32 req_dns_be = 0u;
	sint32 i;

	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(!es->lan.adv_enable)
		return;
	if(pkt->ssid[0] == 0)
		return;

	gw_be = es->lan.ap_ip_be;
	mask_be = es->lan.netmask_be;
	dns_be = es->lan.dns_ip_be;
	if(len >= (sint32)sizeof(cu_esp_lan_join_req_ex_pkt_t)){
		const cu_esp_lan_join_req_ex_pkt_t *epkt = (const cu_esp_lan_join_req_ex_pkt_t *)pkt;
		ip_mode = epkt->ip_mode;
		req_ip_be = epkt->req_ip_be;
		req_gw_be = epkt->req_gw_be;
		req_mask_be = epkt->req_mask_be;
		req_dns_be = epkt->req_dns_be;
	}
	{
		char macbuf[32];
		cu_esp_format_mac6_local(pkt->sta_mac, macbuf, sizeof(macbuf));
		//CZ_DEBUG_INFO("esp.lan", "join req ssid=%s sta=%s mode=%s from=%s:%u", pkt->ssid, macbuf, (ip_mode == CU_ESP_LAN_JOIN_REQ_IPMODE_STATIC) ? "static" : "dhcp", inet_ntoa(from->sin_addr), (unsigned)ntohs(from->sin_port));
	}
	if(strcmp((const char *)pkt->ssid, (const char *)es->soft_ap_name) != 0)
		status = CU_ESP_LAN_JOIN_BAD_SSID;
	else if(!cu_esp_mac_equal6(pkt->bssid, es->lan.local_mac))
		status = CU_ESP_LAN_JOIN_BAD_BSSID;
	else if(es->soft_ap_encryption != 0u && strcmp((const char *)pkt->pwd, (const char *)es->soft_ap_pass) != 0)
		status = CU_ESP_LAN_JOIN_BAD_AUTH;
	else if(ip_mode == CU_ESP_LAN_JOIN_REQ_IPMODE_STATIC){
		if(!cu_esp_lan_validate_static_join(es, req_ip_be, req_gw_be, req_mask_be, &ip_be, &gw_be, &mask_be, &dns_be))
			status = CU_ESP_LAN_JOIN_BAD_IPCFG;
		else if(!cu_esp_lan_reserve_ip(es, pkt->sta_mac, ip_be, from, now_ms))
			status = CU_ESP_LAN_JOIN_NO_LEASE;
		else if(!cu_esp_ipv4_be_is_zero_local(req_dns_be))
			dns_be = req_dns_be;
	}else if(es->lan.dhcp_enable_ap){
		if(cu_esp_ap_allocate_lease(es, pkt->sta_mac, &ip_be) != 0)
			status = CU_ESP_LAN_JOIN_NO_LEASE;
	}else{
		status = CU_ESP_LAN_JOIN_BAD_IPCFG;
	}

	if(status == CU_ESP_LAN_JOIN_OK){
		for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
			if(es->lan.leases[i].in_use && cu_esp_mac_equal6(es->lan.leases[i].mac, pkt->sta_mac)){
				memcpy(&es->lan.leases[i].peer_addr, from, sizeof(*from));
				es->lan.leases[i].expire_ms = now_ms + CU_ESP_LAN_LEASE_TIME_MS;
				break;
			}
		}
	}
	{
		char macbuf[32];
		char ipbuf[24];
		cu_esp_format_mac6_local(pkt->sta_mac, macbuf, sizeof(macbuf));
		cu_esp_ipv4_be_to_str_local(ip_be, ipbuf, sizeof(ipbuf));
		//CZ_DEBUG_INFO("esp.lan", "join resp sta=%s status=%s ip=%s", macbuf, cu_esp_lan_join_status_name(status), ipbuf);
	}
	(void)cu_esp_lan_send_join_resp(es, from, ntohl(pkt->h.seq_be), status, pkt->sta_mac, ip_be, gw_be, mask_be, dns_be);
}

static void cu_esp_lan_handle_join_resp(cu_state_esp_t *es, const cu_esp_lan_join_resp_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	uint8 sta_mac[6];
	char ipbuf[24];
	char gwbuf[24];
	char nmbuf[24];

	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(!es->lan.join_req_pending)
		return;
	if(ntohl(pkt->h.seq_be) != es->lan.join_req_id)
		return;
	if(!cu_esp_ap_get_station_mac_bytes(es, sta_mac))
		return;
	if(!cu_esp_mac_equal6(pkt->sta_mac, sta_mac))
		return;

	if(pkt->status != CU_ESP_LAN_JOIN_OK){
		//CZ_DEBUG_INFO("esp.lan", "join failed status=%s", cu_esp_lan_join_status_name(pkt->status));
		es->lan.join_req_pending = 0u;
		es->lan.join_result = 2u;
		es->lan.join_fail_code = pkt->status;
		es->lan.join_deadline_ms = 0u;
		return;
	}

	memcpy(&es->lan.joined_ap_addr, from, sizeof(*from));
	memcpy(es->lan.joined_ap_bssid, pkt->bssid, 6u);
	cu_esp_ipv4_be_to_str_local(pkt->ip_be, ipbuf, sizeof(ipbuf));
	cu_esp_ipv4_be_to_str_local(pkt->gw_be, gwbuf, sizeof(gwbuf));
	cu_esp_ipv4_be_to_str_local(pkt->mask_be, nmbuf, sizeof(nmbuf));
	snprintf((char *)es->station_ip, sizeof(es->station_ip), "%s", ipbuf);
	snprintf((char *)es->station_gateway, sizeof(es->station_gateway), "%s", gwbuf);
	snprintf((char *)es->station_netmask, sizeof(es->station_netmask), "%s", nmbuf);
	es->lan.join_req_pending = 0u;
	es->lan.joined = 1u;
	es->lan.join_result = 1u;
	es->lan.join_fail_code = 0u;
	es->lan.join_deadline_ms = 0u;
	es->lan.lease_renew_ms = now_ms;
	//CZ_DEBUG_INFO("esp.lan", "join success ip=%s gw=%s mask=%s", ipbuf, gwbuf, nmbuf);
	cu_esp_lan_begin_local_join_success(es);
}

static void cu_esp_lan_handle_lease_renew(cu_state_esp_t *es, const cu_esp_lan_simple_sta_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	sint32 i;

	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(!es->lan.adv_enable)
		return;
	if(!cu_esp_mac_equal6(pkt->bssid, es->lan.local_mac))
		return;

	for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
		if(es->lan.leases[i].in_use && cu_esp_mac_equal6(es->lan.leases[i].mac, pkt->sta_mac)){
			memcpy(&es->lan.leases[i].peer_addr, from, sizeof(*from));
			es->lan.leases[i].expire_ms = now_ms + CU_ESP_LAN_LEASE_TIME_MS;
			{ char macbuf[32]; cu_esp_format_mac6_local(pkt->sta_mac, macbuf, sizeof(macbuf));
//CZ_DEBUG_TRACE("esp.lan", "renew existing lease sta=%s", macbuf);
}
			return;
		}
	}

	if(es->lan.dhcp_enable_ap){
		uint32 ip_be;
		if(cu_esp_ap_allocate_lease(es, pkt->sta_mac, &ip_be) != 0){
			char macbuf[32];
			cu_esp_format_mac6_local(pkt->sta_mac, macbuf, sizeof(macbuf));
			//CZ_DEBUG_INFO("esp.lan", "renew failed no lease sta=%s", macbuf);
			return;
		}
		for(i = 0; i < (sint32)CU_ESP_LAN_DHCP_MAX_LEASES; ++i){
			if(es->lan.leases[i].in_use && cu_esp_mac_equal6(es->lan.leases[i].mac, pkt->sta_mac)){
				memcpy(&es->lan.leases[i].peer_addr, from, sizeof(*from));
				es->lan.leases[i].expire_ms = now_ms + CU_ESP_LAN_LEASE_TIME_MS;
				break;
			}
		}
	}
}

static void cu_esp_lan_handle_leave(cu_state_esp_t *es, const cu_esp_lan_simple_sta_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	(void)from;
	(void)now_ms;
	if(es == NULL || pkt == NULL)
		return;
	if(!cu_esp_mac_equal6(pkt->bssid, es->lan.local_mac))
		return;
	{ char macbuf[32]; cu_esp_format_mac6_local(pkt->sta_mac, macbuf, sizeof(macbuf));
//CZ_DEBUG_INFO("esp.lan", "leave sta=%s", macbuf);
}
	cu_esp_ap_release_lease(es, pkt->sta_mac);
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++){
		if(es->lan.relays[i].in_use && cu_esp_mac_equal6(es->lan.relays[i].sta_mac, pkt->sta_mac))
			cu_esp_lan_close_relay(es, i, 0u, "station leave");
	}
}

static void cu_esp_lan_send_adv(cu_state_esp_t *es)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_adv_pkt_t pkt;
	struct sockaddr_in to;

	if(es == NULL)
		return;
	if(!es->lan.adv_enable)
		return;
	if(es->lan.mcast_sock == ESP_INVALID_SOCKET || es->lan.mcast_sock == ESP_SOCKET_ERROR)
		return;

	memset(&pkt, 0, sizeof(pkt));
	pkt.magic_be = htonl(CU_ESP_LAN_ADV_MAGIC);
	pkt.version = CU_ESP_LAN_ADV_VERSION;
	pkt.channel = (uint8)((es->soft_ap_channel != 0u) ? es->soft_ap_channel : 1u);
	pkt.rssi = -18;
	pkt.enc = (uint8)((es->soft_ap_encryption <= 7u) ? es->soft_ap_encryption : 3u);
	pkt.ucast_port_be = htons(es->lan.ucast_port);
	memcpy(pkt.bssid, es->lan.local_mac, 6u);
	strncpy((char *)pkt.ssid, (const char *)es->soft_ap_name, sizeof(pkt.ssid) - 1u);
	pkt.ssid[sizeof(pkt.ssid) - 1u] = 0;

	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(CU_ESP_LAN_MCAST_PORT);
	to.sin_addr.s_addr = inet_addr(CU_ESP_LAN_MCAST_ADDR);
	(void)sendto(es->lan.mcast_sock, (const char *)&pkt, sizeof(pkt), 0,
		(struct sockaddr *)&to, (socklen_t)sizeof(to));
#else
	(void)es;
#endif
}

static sint32 cu_esp_lan_send_join_req(cu_state_esp_t *es)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_join_req_ex_pkt_t pkt;
	uint8 sta_mac[6];

	if(es == NULL)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;
	if(es->lan.join_target_addr.sin_family == 0)
		return -1;
	if(!cu_esp_ap_get_station_mac_bytes(es, sta_mac))
		return -1;

	memset(&pkt, 0, sizeof(pkt));
	pkt.base.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.base.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.base.h.type = CU_ESP_LAN_CTL_JOIN_REQ;
	pkt.base.h.len_be = htons((uint16)(sizeof(pkt) - sizeof(pkt.base.h)));
	pkt.base.h.seq_be = htonl(es->lan.join_req_id);
	memcpy(pkt.base.sta_mac, sta_mac, 6u);
	memcpy(pkt.base.bssid, es->lan.join_target_bssid, 6u);
	strncpy((char *)pkt.base.ssid, (const char *)es->wifi_name, sizeof(pkt.base.ssid) - 1u);
	strncpy((char *)pkt.base.pwd, (const char *)es->wifi_pass, sizeof(pkt.base.pwd) - 1u);
	if(es->lan.dhcp_enable_sta){
		pkt.ip_mode = CU_ESP_LAN_JOIN_REQ_IPMODE_DHCP;
	}else{
		pkt.ip_mode = CU_ESP_LAN_JOIN_REQ_IPMODE_STATIC;
		pkt.req_ip_be = cu_esp_ipv4_to_be_local((const char *)es->station_ip, 0u);
		pkt.req_gw_be = cu_esp_ipv4_to_be_local((const char *)es->station_gateway, 0u);
		pkt.req_mask_be = cu_esp_ipv4_to_be_local((const char *)es->station_netmask, 0u);
		pkt.req_dns_be = cu_esp_ipv4_be_is_zero_local(es->lan.dns_ip_be) ? 0u : es->lan.dns_ip_be;
	}
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, sizeof(pkt), 0,
		(struct sockaddr *)&es->lan.join_target_addr, (socklen_t)sizeof(es->lan.join_target_addr));
	es->lan.join_req_sent_ms = es->lan.now_ms;
	return 0;
#else
	(void)es;
	return -1;
#endif
}

static sint32 cu_esp_lan_send_join_resp(cu_state_esp_t *es, const struct sockaddr_in *to, uint32 seq,
	uint8 status, const uint8 sta_mac[6], uint32 ip_be, uint32 gw_be, uint32 mask_be, uint32 dns_be)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_join_resp_pkt_t pkt;

	if(es == NULL || to == NULL || sta_mac == NULL)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;

	memset(&pkt, 0, sizeof(pkt));
	pkt.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.h.type = CU_ESP_LAN_CTL_JOIN_RESP;
	pkt.h.len_be = htons((uint16)(sizeof(pkt) - sizeof(pkt.h)));
	pkt.h.seq_be = htonl(seq);
	pkt.status = status;
	pkt.channel = (uint8)((es->soft_ap_channel != 0u) ? es->soft_ap_channel : 1u);
	pkt.enc = (uint8)((es->soft_ap_encryption <= 7u) ? es->soft_ap_encryption : 3u);
	memcpy(pkt.sta_mac, sta_mac, 6u);
	memcpy(pkt.bssid, es->lan.local_mac, 6u);
	pkt.ip_be = ip_be;
	pkt.gw_be = gw_be;
	pkt.mask_be = mask_be;
	pkt.dns_be = dns_be;
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, sizeof(pkt), 0,
		(struct sockaddr *)to, (socklen_t)sizeof(*to));
	return 0;
#else
	(void)es;
	(void)to;
	(void)seq;
	(void)status;
	(void)sta_mac;
	(void)ip_be;
	(void)gw_be;
	(void)mask_be;
	(void)dns_be;
	return -1;
#endif
}

static sint32 cu_esp_lan_send_simple_sta(cu_state_esp_t *es, uint8 type, const struct sockaddr_in *to,
	uint32 seq, const uint8 sta_mac[6], const uint8 bssid[6])
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_simple_sta_pkt_t pkt;

	if(es == NULL || to == NULL || sta_mac == NULL || bssid == NULL)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;

	memset(&pkt, 0, sizeof(pkt));
	pkt.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.h.type = type;
	pkt.h.len_be = htons((uint16)(sizeof(pkt) - sizeof(pkt.h)));
	pkt.h.seq_be = htonl(seq);
	memcpy(pkt.sta_mac, sta_mac, 6u);
	memcpy(pkt.bssid, bssid, 6u);
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, sizeof(pkt), 0,
		(struct sockaddr *)to, (socklen_t)sizeof(*to));
	return 0;
#else
	(void)es;
	(void)type;
	(void)to;
	(void)seq;
	(void)sta_mac;
	(void)bssid;
	return -1;
#endif
}

static void cu_esp_lan_handle_open_req(cu_state_esp_t *es, const cu_esp_lan_open_req_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	sint32 idx;
	cu_esp_lan_relay_t *rl;
	uint8 status = 1u;
	sint32 open_rc;
	uint32 type;
	uint32 open_seq;

	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(!es->lan.adv_enable)
		return;
	if(!cu_esp_mac_equal6(pkt->bssid, es->lan.local_mac))
		return;

	type = ntohl(pkt->type_be);
	{
		char stabuf[32];
		char frombuf[48];
		cu_esp_format_mac6_local(pkt->sta_mac, stabuf, sizeof(stabuf));
		cu_esp_format_sockaddr_local(from, frombuf, sizeof(frombuf));
		//CZ_DEBUG_INFO("esp.relay", "open req sta=%s from=%s vlink=%u proto=%s host=%s port=%u local_port=%u udp_mode=%u",
		//	stabuf, frombuf, (unsigned)pkt->vlink_id, cu_esp_lan_proto_name_local(type),
		//	(const char *)pkt->host, (unsigned)ntohs(pkt->port_be), (unsigned)ntohs(pkt->local_port_be), (unsigned)pkt->udp_mode);
	}
	idx = cu_esp_lan_find_relay(es, from, pkt->sta_mac, pkt->vlink_id, 0u);
	if(idx >= 0)
		cu_esp_lan_close_relay(es, idx, 0u, "replace relay");
	idx = cu_esp_lan_find_free_relay(es);
	if(idx < 0){
		//CZ_DEBUG_ERROR("esp.relay", "open req rejected no free relay vlink=%u proto=%s", (unsigned)pkt->vlink_id, cu_esp_lan_proto_name_local(type));
		(void)cu_esp_lan_send_open_resp(es, from, ntohl(pkt->h.seq_be), 2u, pkt->vlink_id, 0u, type, NULL);
		return;
	}

	rl = &es->lan.relays[idx];
	memset(rl, 0, sizeof(*rl));
	rl->sock = ESP_INVALID_SOCKET;
	rl->in_use = 1u;
	rl->state = 1u;
	rl->vlink_id = pkt->vlink_id;
	rl->overlay_id = (uint8)(idx + 1);
	rl->type = type;
	rl->port = ntohs(pkt->port_be);
	rl->local_port = ntohs(pkt->local_port_be);
	rl->udp_mode = pkt->udp_mode;
	rl->last_io_ms = now_ms;
	open_seq = ntohl(pkt->h.seq_be);
	rl->open_seq = open_seq;
	rl->open_deadline_ms = now_ms + CU_ESP_LAN_OPEN_TIMEOUT_MS;
	rl->open_resp_pending = 1u;
	memcpy(rl->sta_mac, pkt->sta_mac, 6u);
	memcpy(rl->bssid, pkt->bssid, 6u);
	memcpy(&rl->sta_addr, from, sizeof(*from));
	strncpy((char *)rl->host, (const char *)pkt->host, sizeof(rl->host) - 1u);

	open_rc = cu_esp_lan_open_relay_socket(es, rl, (const char *)rl->host, rl->port, rl->type);
	if(open_rc < 0){
		status = 3u;
		cu_esp_lan_close_relay(es, idx, 0u, "relay open failed");
		(void)cu_esp_lan_send_open_resp(es, from, open_seq, status, pkt->vlink_id, 0u, type, NULL);
		return;
	}

	if(open_rc == 0){
		char peerbuf[48];
		cu_esp_format_sockaddr_local(&rl->peer_addr, peerbuf, sizeof(peerbuf));
		//CZ_DEBUG_INFO("esp.relay", "open ready overlay=%u vlink=%u proto=%s peer=%s", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, cu_esp_lan_proto_name_local(rl->type), peerbuf);
		rl->state = 3u;
		rl->open_resp_pending = 0u;
		(void)cu_esp_lan_send_open_resp(es, from, rl->open_seq, 0u, pkt->vlink_id, rl->overlay_id, rl->type, &rl->peer_addr);
		return;
	}

	//CZ_DEBUG_VERBOSE("esp.relay", "open pending overlay=%u vlink=%u proto=%s", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, cu_esp_lan_proto_name_local(rl->type));
	rl->state = 2u;
}

static void cu_esp_lan_handle_open_resp(cu_state_esp_t *es, const cu_esp_lan_open_resp_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	uint8 vlink_id;
	cu_esp_lan_vlink_t *vl;
	if(es == NULL || pkt == NULL || from == NULL)
		return;
	if(!es->lan.joined || from->sin_addr.s_addr != es->lan.joined_ap_addr.sin_addr.s_addr || from->sin_port != es->lan.joined_ap_addr.sin_port)
		return;
	vlink_id = pkt->vlink_id;
	if(vlink_id >= ESP_MAX_LINKS)
		return;
	vl = &es->lan.vlinks[vlink_id];
	if(!vl->pending)
		return;
	if(ntohl(pkt->h.seq_be) != vl->open_req_id)
		return;
	vl->pending = 0u;
	if(pkt->status != 0u){
		//CZ_DEBUG_ERROR("esp.relay", "open resp failed vlink=%u status=%u", (unsigned)vlink_id, (unsigned)pkt->status);
		es->link_state[vlink_id] = CU_ESP_LS_ERROR;
		es->link_err[vlink_id] = -(sint32)(20 + pkt->status);
		es->link_notice_err[vlink_id] = 1u;
		memset(vl, 0, sizeof(*vl));
		return;
	}
	vl->active = 1u;
	vl->overlay_id = pkt->overlay_id;
	vl->type = ntohl(pkt->type_be);
	vl->peer_addr.sin_family = AF_INET;
	vl->peer_addr.sin_addr.s_addr = pkt->peer_ipv4_be;
	vl->peer_addr.sin_port = pkt->peer_port_be;
	vl->last_io_ms = now_ms;
	{ char peerbuf[48]; cu_esp_format_sockaddr_local(&vl->peer_addr, peerbuf, sizeof(peerbuf));
//CZ_DEBUG_INFO("esp.relay", "open resp ok vlink=%u overlay=%u proto=%s peer=%s", (unsigned)vlink_id, (unsigned)vl->overlay_id, cu_esp_lan_proto_name_local(vl->type), peerbuf);
}
	es->sock_info[vlink_id] = vl->peer_addr;
	es->proto[vlink_id] = vl->type;
	es->protocol[vlink_id] = vl->type;
	if((vl->type & ESP_PROTO_SSL) != 0u){
		es->link_state[vlink_id] = CU_ESP_LS_TLS;
	}else{
		es->link_state[vlink_id] = CU_ESP_LS_OPEN;
		es->link_notice_ok[vlink_id] = 1u;
	}
}

static void cu_esp_lan_handle_data(cu_state_esp_t *es, const cu_esp_lan_data_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms, sint32 len)
{
	uint16 dlen;
	if(es == NULL || pkt == NULL)
		return;
	dlen = ntohs(pkt->data_len_be);
	if(dlen > CU_ESP_LAN_DATA_MAX)
		dlen = CU_ESP_LAN_DATA_MAX;
	if((sint32)(sizeof(cu_esp_lan_ctl_hdr_t) + 4 + dlen) > len)
		return;
	if(from != NULL && es->lan.joined && from->sin_addr.s_addr == es->lan.joined_ap_addr.sin_addr.s_addr && from->sin_port == es->lan.joined_ap_addr.sin_port){
		if(pkt->vlink_id < ESP_MAX_LINKS){
			cu_esp_lan_vlink_t *vl = &es->lan.vlinks[pkt->vlink_id];
			if(vl->active && vl->overlay_id == pkt->overlay_id){
				(void)cu_esp_lan_queue_vrx(es, pkt->vlink_id, pkt->data, dlen);
				//CZ_DEBUG_TRACE("esp.relay", "overlay->sta vlink=%u overlay=%u bytes=%u", (unsigned)pkt->vlink_id, (unsigned)pkt->overlay_id, (unsigned)dlen);
				vl->last_io_ms = now_ms;
			}
		}
	}else{
		sint32 idx = cu_esp_lan_find_station_relay(es, from, pkt->vlink_id, pkt->overlay_id);
		if(idx >= 0){
			//CZ_DEBUG_TRACE("esp.relay", "sta->overlay vlink=%u overlay=%u bytes=%u", (unsigned)pkt->vlink_id, (unsigned)pkt->overlay_id, (unsigned)dlen);
			if(cu_esp_lan_queue_relay_tx(es, idx, pkt->data, dlen, now_ms) < 0)
				cu_esp_lan_close_relay(es, idx, 1u, "station data queue failure");
		}
	}
}

static void cu_esp_lan_handle_close(cu_state_esp_t *es, const cu_esp_lan_close_pkt_t *pkt,
	const struct sockaddr_in *from, uint32 now_ms)
{
	(void)now_ms;
	if(es == NULL || pkt == NULL)
		return;
	if(from != NULL && es->lan.joined && from->sin_addr.s_addr == es->lan.joined_ap_addr.sin_addr.s_addr && from->sin_port == es->lan.joined_ap_addr.sin_port){
		if(pkt->vlink_id < ESP_MAX_LINKS){
			cu_esp_lan_vlink_t *vl = &es->lan.vlinks[pkt->vlink_id];
			if(vl->active && vl->overlay_id == pkt->overlay_id){
				//CZ_DEBUG_INFO("esp.relay", "overlay close recv vlink=%u overlay=%u", (unsigned)pkt->vlink_id, (unsigned)pkt->overlay_id);
				uint8 is_ssl = (uint8)(((vl->type & ESP_PROTO_SSL) != 0u || (es->protocol[pkt->vlink_id] & ESP_PROTO_SSL) != 0u || es->link_is_ssl[pkt->vlink_id] != 0u) ? 1u : 0u);
				vl->closed_remote = 1u;
				vl->active = 0u;
				vl->last_io_ms = now_ms;
				if(vl->rx_len == 0u && !is_ssl){
					es->link_state[pkt->vlink_id] = CU_ESP_LS_ERROR;
					es->link_err[pkt->vlink_id] = 0;
					es->link_notice_err[pkt->vlink_id] = 1u;
					memset(vl, 0, sizeof(*vl));
				}
			}
		}
	}else{
		sint32 idx = cu_esp_lan_find_relay(es, from, pkt->sta_mac, pkt->vlink_id, pkt->overlay_id);
		if(idx >= 0){
			//CZ_DEBUG_INFO("esp.relay", "sta close recv vlink=%u overlay=%u", (unsigned)pkt->vlink_id, (unsigned)pkt->overlay_id);
			cu_esp_lan_close_relay(es, idx, 0u, "station close");
		}
	}
}

static sint32 cu_esp_lan_send_open_req(cu_state_esp_t *es, uint32 sock, const char *host, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_open_req_pkt_t pkt;
	uint8 sta_mac[6];
	if(es == NULL || host == NULL || sock >= ESP_MAX_LINKS)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;
	if(es->lan.joined_ap_addr.sin_family != AF_INET)
		return -1;
	if(!cu_esp_ap_get_station_mac_bytes(es, sta_mac))
		return -1;
	memset(&pkt, 0, sizeof(pkt));
	pkt.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.h.type = CU_ESP_LAN_CTL_OPEN_REQ;
	pkt.h.len_be = htons((uint16)(sizeof(pkt) - sizeof(pkt.h)));
	pkt.h.seq_be = htonl(es->lan.vlinks[sock].open_req_id);
	memcpy(pkt.sta_mac, sta_mac, 6u);
	memcpy(pkt.bssid, es->lan.joined_ap_bssid, 6u);
	pkt.vlink_id = (uint8)sock;
	pkt.type_be = htonl(type);
	pkt.port_be = htons((uint16)port);
	pkt.local_port_be = htons((uint16)local_port);
	pkt.udp_mode = (uint8)udp_mode;
	strncpy((char *)pkt.host, host, sizeof(pkt.host) - 1u);
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, sizeof(pkt), 0, (struct sockaddr *)&es->lan.joined_ap_addr, (socklen_t)sizeof(es->lan.joined_ap_addr));
	//CZ_DEBUG_INFO("esp.relay", "open req send vlink=%u proto=%s host=%s port=%u local_port=%u udp_mode=%u", (unsigned)sock, cu_esp_lan_proto_name_local(type), host, (unsigned)port, (unsigned)local_port, (unsigned)udp_mode);
	return 0;
#else
	(void)es; (void)sock; (void)host; (void)port; (void)type; (void)local_port; (void)udp_mode;
	return -1;
#endif
}

static sint32 cu_esp_lan_send_open_resp(cu_state_esp_t *es, const struct sockaddr_in *to, uint32 seq, uint8 status, uint8 vlink_id, uint8 overlay_id, uint32 type, const struct sockaddr_in *peer)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_open_resp_pkt_t pkt;
	if(es == NULL || to == NULL)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;
	memset(&pkt, 0, sizeof(pkt));
	pkt.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.h.type = CU_ESP_LAN_CTL_OPEN_RESP;
	pkt.h.len_be = htons((uint16)(sizeof(pkt) - sizeof(pkt.h)));
	pkt.h.seq_be = htonl(seq);
	pkt.status = status;
	pkt.vlink_id = vlink_id;
	pkt.overlay_id = overlay_id;
	pkt.type_be = htonl(type);
	if(peer != NULL){
		pkt.peer_ipv4_be = peer->sin_addr.s_addr;
		pkt.peer_port_be = peer->sin_port;
	}
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, sizeof(pkt), 0, (struct sockaddr *)to, (socklen_t)sizeof(*to));
	//CZ_DEBUG_VERBOSE("esp.relay", "open resp send vlink=%u overlay=%u status=%u proto=%s", (unsigned)vlink_id, (unsigned)overlay_id, (unsigned)status, cu_esp_lan_proto_name_local(type));
	return 0;
#else
	(void)es; (void)to; (void)seq; (void)status; (void)vlink_id; (void)overlay_id; (void)type; (void)peer;
	return -1;
#endif
}

static sint32 cu_esp_lan_send_data(cu_state_esp_t *es, const struct sockaddr_in *to, uint8 vlink_id, uint8 overlay_id, const uint8 *data, uint16 len)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_data_pkt_t pkt;
	if(es == NULL || to == NULL || data == NULL || len == 0u)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;
	if(len > CU_ESP_LAN_DATA_MAX)
		len = CU_ESP_LAN_DATA_MAX;
	memset(&pkt, 0, sizeof(cu_esp_lan_ctl_hdr_t) + 4);
	pkt.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.h.type = CU_ESP_LAN_CTL_DATA;
	pkt.h.len_be = htons((uint16)(4u + len));
	pkt.h.seq_be = 0u;
	pkt.vlink_id = vlink_id;
	pkt.overlay_id = overlay_id;
	pkt.data_len_be = htons(len);
	memcpy(pkt.data, data, len);
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, (int)(sizeof(cu_esp_lan_ctl_hdr_t) + 4u + len), 0, (struct sockaddr *)to, (socklen_t)sizeof(*to));
	//CZ_DEBUG_TRACE("esp.relay", "overlay send vlink=%u overlay=%u bytes=%u", (unsigned)vlink_id, (unsigned)overlay_id, (unsigned)len);
	return 0;
#else
	(void)es; (void)to; (void)vlink_id; (void)overlay_id; (void)data; (void)len;
	return -1;
#endif
}

static sint32 cu_esp_lan_send_close(cu_state_esp_t *es, const struct sockaddr_in *to, const uint8 sta_mac[6], const uint8 bssid[6], uint8 vlink_id, uint8 overlay_id)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_close_pkt_t pkt;
	if(es == NULL || to == NULL || sta_mac == NULL || bssid == NULL)
		return -1;
	if(es->lan.ucast_sock == ESP_INVALID_SOCKET || es->lan.ucast_sock == ESP_SOCKET_ERROR)
		return -1;
	memset(&pkt, 0, sizeof(pkt));
	pkt.h.magic_be = htonl(CU_ESP_LAN_CTL_MAGIC);
	pkt.h.version = CU_ESP_LAN_CTL_VERSION;
	pkt.h.type = CU_ESP_LAN_CTL_CLOSE;
	pkt.h.len_be = htons((uint16)(sizeof(pkt) - sizeof(pkt.h)));
	pkt.h.seq_be = 0u;
	memcpy(pkt.sta_mac, sta_mac, 6u);
	memcpy(pkt.bssid, bssid, 6u);
	pkt.vlink_id = vlink_id;
	pkt.overlay_id = overlay_id;
	(void)sendto(es->lan.ucast_sock, (const char *)&pkt, sizeof(pkt), 0, (struct sockaddr *)to, (socklen_t)sizeof(*to));
	//CZ_DEBUG_INFO("esp.relay", "overlay close send vlink=%u overlay=%u", (unsigned)vlink_id, (unsigned)overlay_id);
	return 0;
#else
	(void)es; (void)to; (void)sta_mac; (void)bssid; (void)vlink_id; (void)overlay_id;
	return -1;
#endif
}

static sint32 cu_esp_lan_find_ap_slot(cu_state_esp_t *es, const uint8 bssid[6])
{
	sint32 i;

	for(i = 0; i < (sint32)CU_ESP_LAN_MAX_APS; ++i){
		if(es->lan.aps[i].in_use && cu_esp_mac_equal6(es->lan.aps[i].bssid, bssid))
			return i;
	}
	return -1;
}

static sint32 cu_esp_lan_find_free_ap_slot(cu_state_esp_t *es)
{
	sint32 i;

	for(i = 0; i < (sint32)CU_ESP_LAN_MAX_APS; ++i){
		if(!es->lan.aps[i].in_use)
			return i;
	}
	return -1;
}

static uint8 cu_esp_parse_mac6_local(const char *s, uint8 mac[6])
{
	unsigned int v[6];
	sint32 i;

	if(s == NULL || mac == NULL)
		return 0u;
	if(sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
		return 0u;
	for(i = 0; i < 6; ++i){
		if(v[i] > 0xFFu)
			return 0u;
		mac[i] = (uint8)v[i];
	}
	return 1u;
}

static void cu_esp_format_mac6_local(const uint8 mac[6], char *out, size_t out_sz)
{
	if(out == NULL || out_sz == 0u)
		return;
	if(mac == NULL){
		out[0] = 0;
		return;
	}
	snprintf(out, out_sz, "%02x:%02x:%02x:%02x:%02x:%02x",
		(unsigned int)mac[0], (unsigned int)mac[1], (unsigned int)mac[2],
		(unsigned int)mac[3], (unsigned int)mac[4], (unsigned int)mac[5]);
}

static void cu_esp_format_sockaddr_local(const struct sockaddr_in *sa, char *out, size_t out_sz)
{
	unsigned int port = 0u;
	if(out == NULL || out_sz == 0u)
		return;
	if(sa == NULL || sa->sin_family != AF_INET){
		strncpy(out, "<none>", out_sz - 1u);
		out[out_sz - 1u] = 0;
		return;
	}
	port = (unsigned int)ntohs(sa->sin_port);
	snprintf(out, out_sz, "%s:%u", inet_ntoa(sa->sin_addr), port);
}

static char const* cu_esp_lan_proto_name_local(uint32 type)
{
	if((type & ESP_PROTO_UDP) != 0u)
		return "udp";
	if((type & ESP_PROTO_SSL) != 0u)
		return "ssl";
	return "tcp";
}

static char const* cu_esp_lan_join_status_name(uint8 status)
{
	switch(status){
		case CU_ESP_LAN_JOIN_OK: return "OK";
		case CU_ESP_LAN_JOIN_BAD_SSID: return "BAD_SSID";
		case CU_ESP_LAN_JOIN_BAD_AUTH: return "BAD_AUTH";
		case CU_ESP_LAN_JOIN_NO_LEASE: return "NO_LEASE";
		case CU_ESP_LAN_JOIN_BAD_BSSID: return "BAD_BSSID";
		case CU_ESP_LAN_JOIN_BAD_IPCFG: return "BAD_IPCFG";
		default: return "UNKNOWN";
	}
}

static uint8 cu_esp_mac_equal6(const uint8 a[6], const uint8 b[6])
{
	return (uint8)((memcmp(a, b, 6u) == 0) ? 1u : 0u);
}

static uint8 cu_esp_ascii_ieq_char_local(int c0, int c1)
{
	if(c0 >= 'a' && c0 <= 'z')
		c0 -= ('a' - 'A');
	if(c1 >= 'a' && c1 <= 'z')
		c1 -= ('a' - 'A');
	return (uint8)((c0 == c1) ? 1u : 0u);
}

static uint8 cu_esp_ascii_ieq_str_local(const char *a, const char *b)
{
	if(a == NULL || b == NULL)
		return (uint8)((a == b) ? 1u : 0u);
	while(*a != 0 && *b != 0){
		if(!cu_esp_ascii_ieq_char_local((unsigned char)*a, (unsigned char)*b))
			return 0u;
		++a;
		++b;
	}
	return (uint8)((*a == 0 && *b == 0) ? 1u : 0u);
}

static uint8 cu_esp_cwlap_field_on_local(uint32 mask, uint8 bit)
{
	return (uint8)((mask & (uint32)(1u << bit)) ? 1u : 0u);
}

static sint32 cu_esp_cwlap_emit_adv(const cu_esp_lan_ap_entry_t *ap, uint32 print_mask)
{
	char body[220];
	char line[256];
	char mac[24];
	size_t pos = 0u;
	uint8 first = 1u;

	if(ap == NULL || !ap->in_use)
		return 0;

	cu_esp_format_mac6_local(ap->bssid, mac, sizeof(mac));

#define CWLAP_COMMA() do{ if(!first){ body[pos++] = ','; } first = 0u; }while(0)
	if(cu_esp_cwlap_field_on_local(print_mask, 0)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)ap->enc); }
	if(cu_esp_cwlap_field_on_local(print_mask, 1)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "\"%s\"", (const char *)ap->ssid); }
	if(cu_esp_cwlap_field_on_local(print_mask, 2)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%d", (int)ap->rssi); }
	if(cu_esp_cwlap_field_on_local(print_mask, 3)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "\"%s\"", mac); }
	if(cu_esp_cwlap_field_on_local(print_mask, 4)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)ap->channel); }
	if(cu_esp_cwlap_field_on_local(print_mask, 5)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%d", 0); }
	if(cu_esp_cwlap_field_on_local(print_mask, 6)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%d", 48); }
	if(cu_esp_cwlap_field_on_local(print_mask, 7)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)((ap->enc == 0u) ? 0u : ((ap->enc >= 4u) ? 4u : ap->enc))); }
	if(cu_esp_cwlap_field_on_local(print_mask, 8)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", (unsigned int)((ap->enc == 0u) ? 0u : ((ap->enc >= 4u) ? 4u : ap->enc))); }
	if(cu_esp_cwlap_field_on_local(print_mask, 9)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", 7u); }
	if(cu_esp_cwlap_field_on_local(print_mask, 10)){ CWLAP_COMMA(); pos += (size_t)snprintf(body + pos, sizeof(body) - pos, "%u", 0u); }
#undef CWLAP_COMMA

	body[(pos < sizeof(body)) ? pos : (sizeof(body) - 1u)] = 0;
	snprintf(line, sizeof(line), "+CWLAP:(%s)\r\n", body);
	cu_esp_txp(line);
	return 1;
}

static uint8 cu_esp_cwlap_match_adv(const cu_esp_lan_ap_entry_t *ap, const cu_esp_cwlap_query_t *q)
{
	char mac[24];

	if(ap == NULL || !ap->in_use || q == NULL)
		return 0u;
	if(ap->rssi < q->rssi_filter)
		return 0u;
	if(q->authmask != 0u && (q->authmask & (uint32)(1u << ap->enc)) == 0u)
		return 0u;
	if(q->ssid != NULL && q->ssid[0] != 0 && strcmp((const char *)ap->ssid, q->ssid) != 0)
		return 0u;
	if(q->bssid != NULL && q->bssid[0] != 0){
		cu_esp_format_mac6_local(ap->bssid, mac, sizeof(mac));
		if(!cu_esp_ascii_ieq_str_local(mac, q->bssid))
			return 0u;
	}
	if(q->channel > 0 && ap->channel != (uint8)q->channel)
		return 0u;
	return 1u;
}

static void cu_esp_cwlap_sort_idx(cu_state_esp_t *es, sint32 *idx, sint32 count)
{
	sint32 i;
	sint32 j;

	for(i = 0; i < count; ++i){
		for(j = i + 1; j < count; ++j){
			if(es->lan.aps[idx[j]].rssi > es->lan.aps[idx[i]].rssi){
				sint32 t = idx[i];
				idx[i] = idx[j];
				idx[j] = t;
			}
		}
	}
}

static sint32 cu_esp_lan_find_free_relay(cu_state_esp_t *es)
{
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++)
		if(!es->lan.relays[i].in_use)
			return i;
	return -1;
}

static sint32 cu_esp_lan_find_relay(cu_state_esp_t *es, const struct sockaddr_in *from, const uint8 sta_mac[6], uint8 vlink_id, uint8 overlay_id)
{
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++){
		cu_esp_lan_relay_t *rl = &es->lan.relays[i];
		if(!rl->in_use)
			continue;
		if(rl->vlink_id != vlink_id)
			continue;
		if(overlay_id != 0u && rl->overlay_id != overlay_id)
			continue;
		if(sta_mac != NULL && !cu_esp_mac_equal6(rl->sta_mac, sta_mac))
			continue;
		if(from != NULL){
			if(rl->sta_addr.sin_addr.s_addr != from->sin_addr.s_addr || rl->sta_addr.sin_port != from->sin_port)
				continue;
		}
		return i;
	}
	return -1;
}

static sint32 cu_esp_lan_find_station_relay(cu_state_esp_t *es, const struct sockaddr_in *from, uint8 vlink_id, uint8 overlay_id)
{
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++){
		cu_esp_lan_relay_t *rl = &es->lan.relays[i];
		if(!rl->in_use)
			continue;
		if(rl->vlink_id != vlink_id || rl->overlay_id != overlay_id)
			continue;
		if(from != NULL && (rl->sta_addr.sin_addr.s_addr != from->sin_addr.s_addr || rl->sta_addr.sin_port != from->sin_port))
			continue;
		return i;
	}
	return -1;
}

static void cu_esp_lan_close_relay(cu_state_esp_t *es, sint32 idx, uint8 notify_sta, const char *reason)
{
	cu_esp_lan_relay_t *rl;
	if(es == NULL || idx < 0 || idx >= (sint32)CU_ESP_LAN_MAX_RELAYS)
		return;
	rl = &es->lan.relays[idx];
	if(!rl->in_use)
		return;
	{
		char stabuf[32];
		char stabuf2[48];
		char peerbuf[48];
		cu_esp_format_mac6_local(rl->sta_mac, stabuf, sizeof(stabuf));
		cu_esp_format_sockaddr_local(&rl->sta_addr, stabuf2, sizeof(stabuf2));
		cu_esp_format_sockaddr_local(&rl->peer_addr, peerbuf, sizeof(peerbuf));
		//CZ_DEBUG_INFO("esp.relay", "close relay overlay=%u vlink=%u proto=%s sta=%s via=%s peer=%s notify=%u reason=%s err=%d",
		//	(unsigned)rl->overlay_id, (unsigned)rl->vlink_id, cu_esp_lan_proto_name_local(rl->type),
		//	stabuf, stabuf2, peerbuf, (unsigned)notify_sta, (reason != NULL) ? reason : "", (int)rl->last_err);
	}
	if(notify_sta && rl->sta_addr.sin_family == AF_INET)
		(void)cu_esp_lan_send_close(es, &rl->sta_addr, rl->sta_mac, rl->bssid, rl->vlink_id, rl->overlay_id);
	if(rl->sock != ESP_INVALID_SOCKET && rl->sock != ESP_SOCKET_ERROR)
		cu_esp_socket_close_local(rl->sock);
	memset(rl, 0, sizeof(*rl));
	rl->sock = ESP_INVALID_SOCKET;
}

static sint32 cu_esp_lan_queue_vrx(cu_state_esp_t *es, uint32 sock, const uint8 *data, uint16 len)
{
	cu_esp_lan_vlink_t *vl;
	uint16 tail;
	if(es == NULL || sock >= ESP_MAX_LINKS || data == NULL)
		return -1;
	vl = &es->lan.vlinks[sock];
	if(len > (uint16)(CU_ESP_LAN_VRX_MAX - vl->rx_len))
		len = (uint16)(CU_ESP_LAN_VRX_MAX - vl->rx_len);
	if(len == 0u)
		return 0;
	tail = (uint16)((vl->rx_head + vl->rx_len) % CU_ESP_LAN_VRX_MAX);
	if(tail + len <= CU_ESP_LAN_VRX_MAX){
		memcpy(&vl->rx_buf[tail], data, len);
	}else{
		uint16 first = (uint16)(CU_ESP_LAN_VRX_MAX - tail);
		memcpy(&vl->rx_buf[tail], data, first);
		memcpy(&vl->rx_buf[0], data + first, (uint16)(len - first));
	}
	vl->rx_len = (uint16)(vl->rx_len + len);
	return (sint32)len;
}

static sint32 cu_esp_lan_open_relay_socket(cu_state_esp_t *es, cu_esp_lan_relay_t *rl, const char *host, uint32 port, uint32 type)
{
#ifndef __EMSCRIPTEN__
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	char portbuf[16];
	int stype = ((type & ESP_PROTO_UDP) != 0u) ? SOCK_DGRAM : SOCK_STREAM;
	int proto = ((type & ESP_PROTO_UDP) != 0u) ? IPPROTO_UDP : IPPROTO_TCP;
	int rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = stype;
	hints.ai_protocol = proto;
	snprintf(portbuf, sizeof(portbuf), "%u", (unsigned int)port);
	//CZ_DEBUG_VERBOSE("esp.relay", "resolve host=%s port=%s proto=%s", host, portbuf, cu_esp_lan_proto_name_local(type));
	if(getaddrinfo(host, portbuf, &hints, &res) != 0 || res == NULL){
		//CZ_DEBUG_ERROR("esp.relay", "resolve failed host=%s port=%s proto=%s", host, portbuf, cu_esp_lan_proto_name_local(type));
		return -1;
	}

	rl->sock = socket(AF_INET, stype, proto);
	if(rl->sock == ESP_INVALID_SOCKET || rl->sock == ESP_SOCKET_ERROR){
		freeaddrinfo(res);
		return -1;
	}
	if(cu_esp_socket_set_nonblock_local(rl->sock) != 0){
		cu_esp_socket_close_local(rl->sock);
		rl->sock = ESP_INVALID_SOCKET;
		freeaddrinfo(res);
		return -1;
	}

	if(rl->local_port != 0u){
		struct sockaddr_in local_sa;
		memset(&local_sa, 0, sizeof(local_sa));
		local_sa.sin_family = AF_INET;
		local_sa.sin_addr.s_addr = htonl(INADDR_ANY);
		local_sa.sin_port = htons(rl->local_port);
		if(bind(rl->sock, (struct sockaddr *)&local_sa, (socklen_t)sizeof(local_sa)) != 0){
			cu_esp_socket_close_local(rl->sock);
			rl->sock = ESP_INVALID_SOCKET;
			freeaddrinfo(res);
			return -1;
		}
	}

	memcpy(&rl->peer_addr, res->ai_addr, sizeof(struct sockaddr_in));
	{ char peerbuf[48]; cu_esp_format_sockaddr_local(&rl->peer_addr, peerbuf, sizeof(peerbuf));
//CZ_DEBUG_INFO("esp.relay", "connect start proto=%s host=%s peer=%s local_port=%u", cu_esp_lan_proto_name_local(type), host, peerbuf, (unsigned)rl->local_port);
}
	rc = connect(rl->sock, res->ai_addr, (socklen_t)res->ai_addrlen);
	freeaddrinfo(res);

	if((type & ESP_PROTO_UDP) != 0u){
		if(rc != 0 && !cu_esp_socket_connect_pending_local()){
			//CZ_DEBUG_ERROR("esp.relay", "udp connect failed err=%d", (int)cu_esp_get_last_error());
			cu_esp_socket_close_local(rl->sock);
			rl->sock = ESP_INVALID_SOCKET;
			return -1;
		}
		rl->state = 3u;
		rl->last_err = 0;
		//CZ_DEBUG_INFO("esp.relay", "udp ready overlay=%u vlink=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id);
		(void)es;
		return 0;
	}

	if(rc == 0){
		rl->state = 3u;
		rl->last_err = 0;
		//CZ_DEBUG_INFO("esp.relay", "tcp ready overlay=%u vlink=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id);
		(void)es;
		return 0;
	}
	if(cu_esp_socket_connect_pending_local()){
		rl->state = 2u;
		rl->last_err = 0;
		//CZ_DEBUG_VERBOSE("esp.relay", "tcp connect pending overlay=%u vlink=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id);
		(void)es;
		return 1;
	}

	rl->last_err = cu_esp_get_last_error();
	//CZ_DEBUG_ERROR("esp.relay", "connect failed overlay=%u vlink=%u err=%d", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (int)rl->last_err);
	cu_esp_socket_close_local(rl->sock);
	rl->sock = ESP_INVALID_SOCKET;
	return -1;
#else
	(void)es; (void)rl; (void)host; (void)port; (void)type;
	return -1;
#endif
}

static sint32 cu_esp_lan_finish_relay_connect(cu_state_esp_t *es, sint32 idx, uint32 now_ms)
{
#ifndef __EMSCRIPTEN__
	cu_esp_lan_relay_t *rl;
	fd_set wfds;
	fd_set efds;
	struct timeval tv;
	int sr;
	int soerr = 0;
	#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	int optlen = (int)sizeof(soerr);
	#else
	socklen_t optlen = (socklen_t)sizeof(soerr);
	#endif

	if(es == NULL || idx < 0 || idx >= (sint32)CU_ESP_LAN_MAX_RELAYS)
		return -1;
	rl = &es->lan.relays[idx];
	if(!rl->in_use || rl->sock == ESP_INVALID_SOCKET || rl->sock == ESP_SOCKET_ERROR)
		return -1;
	if(rl->state != 2u)
		return 1;

	FD_ZERO(&wfds);
	FD_ZERO(&efds);
	FD_SET(rl->sock, &wfds);
	FD_SET(rl->sock, &efds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	sr = select((int)(rl->sock + 1), NULL, &wfds, &efds, &tv);
	if(sr == 0)
		return 0;
	if(sr < 0){
		rl->last_err = cu_esp_get_last_error();
		//CZ_DEBUG_ERROR("esp.relay", "connect select failed overlay=%u vlink=%u err=%d", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (int)rl->last_err);
		return -1;
	}
	if(getsockopt(rl->sock, SOL_SOCKET, SO_ERROR, (char *)&soerr, &optlen) != 0)
		soerr = (int)cu_esp_get_last_error();
	if(soerr != 0){
		rl->last_err = soerr;
		//CZ_DEBUG_ERROR("esp.relay", "connect complete failed overlay=%u vlink=%u err=%d", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (int)rl->last_err);
		return -1;
	}
	rl->state = 3u;
	//CZ_DEBUG_INFO("esp.relay", "connect complete overlay=%u vlink=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id);
	rl->last_err = 0;
	rl->last_io_ms = now_ms;
	return 1;
#else
	(void)es; (void)idx; (void)now_ms;
	return -1;
#endif
}

static sint32 cu_esp_lan_flush_relay_tx(cu_state_esp_t *es, sint32 idx, uint32 now_ms)
{
	cu_esp_lan_relay_t *rl;

	if(es == NULL || idx < 0 || idx >= (sint32)CU_ESP_LAN_MAX_RELAYS)
		return -1;
	rl = &es->lan.relays[idx];
	if(!rl->in_use || rl->sock == ESP_INVALID_SOCKET || rl->sock == ESP_SOCKET_ERROR)
		return -1;
	if(rl->state != 3u)
		return 0;

	while(rl->tx_off < rl->tx_len){
		sint32 wr;
		if((rl->type & ESP_PROTO_UDP) != 0u)
			wr = (sint32)send(rl->sock, (const char *)&rl->tx_buf[rl->tx_off], (int)(rl->tx_len - rl->tx_off), 0);
		else
			wr = (sint32)send(rl->sock, (const char *)&rl->tx_buf[rl->tx_off], (int)(rl->tx_len - rl->tx_off), 0);
		if(wr < 0){
			if(cu_esp_socket_would_block())
				break;
			rl->last_err = cu_esp_get_last_error();
			//CZ_DEBUG_ERROR("esp.relay", "relay send failed overlay=%u vlink=%u err=%d", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (int)rl->last_err);
			return -1;
		}
		if(wr == 0)
			break;
		rl->tx_off = (uint16)(rl->tx_off + (uint16)wr);
		//CZ_DEBUG_TRACE("esp.relay", "relay->peer overlay=%u vlink=%u bytes=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (unsigned)wr);
		rl->last_io_ms = now_ms;
	}

	if(rl->tx_off >= rl->tx_len){
		rl->tx_len = 0u;
		rl->tx_off = 0u;
	}else if(rl->tx_off != 0u){
		memmove(rl->tx_buf, &rl->tx_buf[rl->tx_off], (size_t)(rl->tx_len - rl->tx_off));
		rl->tx_len = (uint16)(rl->tx_len - rl->tx_off);
		rl->tx_off = 0u;
	}

	return 0;
}

static sint32 cu_esp_lan_queue_relay_tx(cu_state_esp_t *es, sint32 idx, const uint8 *data, uint16 len, uint32 now_ms)
{
	cu_esp_lan_relay_t *rl;
	uint16 avail;

	if(es == NULL || idx < 0 || idx >= (sint32)CU_ESP_LAN_MAX_RELAYS || data == NULL)
		return -1;
	rl = &es->lan.relays[idx];
	if(!rl->in_use || rl->sock == ESP_INVALID_SOCKET || rl->sock == ESP_SOCKET_ERROR)
		return -1;
	if(rl->state != 3u)
		return -1;

	if(cu_esp_lan_flush_relay_tx(es, idx, now_ms) < 0)
		return -1;

	if(rl->tx_off != 0u && rl->tx_off < rl->tx_len){
		memmove(rl->tx_buf, &rl->tx_buf[rl->tx_off], (size_t)(rl->tx_len - rl->tx_off));
		rl->tx_len = (uint16)(rl->tx_len - rl->tx_off);
		rl->tx_off = 0u;
	}
	if(rl->tx_off >= rl->tx_len){
		rl->tx_len = 0u;
		rl->tx_off = 0u;
	}

	avail = (uint16)(CU_ESP_LAN_RELAY_TX_MAX - rl->tx_len);
	if(len > avail)
		return -1;
	memcpy(&rl->tx_buf[rl->tx_len], data, len);
	//CZ_DEBUG_TRACE("esp.relay", "queue relay tx overlay=%u vlink=%u bytes=%u queued=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (unsigned)len, (unsigned)(rl->tx_len + len));
	rl->tx_len = (uint16)(rl->tx_len + len);
	if(cu_esp_lan_flush_relay_tx(es, idx, now_ms) < 0)
		return -1;
	return (sint32)len;
}

static void cu_esp_lan_poll_relays(cu_state_esp_t *es, uint32 now_ms)
{
	uint8 buf[CU_ESP_LAN_DATA_MAX];
	for(sint32 i = 0; i < (sint32)CU_ESP_LAN_MAX_RELAYS; i++){
		cu_esp_lan_relay_t *rl = &es->lan.relays[i];
		if(!rl->in_use || rl->sock == ESP_INVALID_SOCKET || rl->sock == ESP_SOCKET_ERROR)
			continue;

		if(rl->state == 2u){
			sint32 cr;
			if(rl->open_deadline_ms != 0u && now_ms >= rl->open_deadline_ms){
				uint32 open_seq = rl->open_seq;
				uint8 vlink_id = rl->vlink_id;
				uint32 type = rl->type;
				struct sockaddr_in sta_addr = rl->sta_addr;
				cu_esp_lan_close_relay(es, i, 0u, "relay open timeout");
				(void)cu_esp_lan_send_open_resp(es, &sta_addr, open_seq, 4u, vlink_id, 0u, type, NULL);
				continue;
			}
			cr = cu_esp_lan_finish_relay_connect(es, i, now_ms);
			if(cr < 0){
				uint32 open_seq = rl->open_seq;
				uint8 vlink_id = rl->vlink_id;
				uint32 type = rl->type;
				struct sockaddr_in sta_addr = rl->sta_addr;
				cu_esp_lan_close_relay(es, i, 0u, "relay connect failure");
				(void)cu_esp_lan_send_open_resp(es, &sta_addr, open_seq, 4u, vlink_id, 0u, type, NULL);
				continue;
			}
			if(cr > 0 && rl->open_resp_pending){
				rl->open_resp_pending = 0u;
				//CZ_DEBUG_INFO("esp.relay", "open resp send ok overlay=%u vlink=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id);
				(void)cu_esp_lan_send_open_resp(es, &rl->sta_addr, rl->open_seq, 0u, rl->vlink_id, rl->overlay_id, rl->type, &rl->peer_addr);
			}
		}

		if(rl->state == 3u && rl->tx_len != 0u){
			if(cu_esp_lan_flush_relay_tx(es, i, now_ms) < 0){
				cu_esp_lan_close_relay(es, i, 1u, "relay tx flush failure");
				continue;
			}
		}

		if(rl->state != 3u)
			continue;

		for(;;){
			sint32 rd;
			if((rl->type & ESP_PROTO_UDP) != 0u){
				struct sockaddr_in peer;
				#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
				int plen = (int)sizeof(peer);
				#else
				socklen_t plen = (socklen_t)sizeof(peer);
				#endif
				memset(&peer, 0, sizeof(peer));
				rd = (sint32)recvfrom(rl->sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&peer, &plen);
				if(rd > 0 && peer.sin_family == AF_INET)
					rl->peer_addr = peer;
			}else{
				rd = (sint32)recv(rl->sock, (char *)buf, sizeof(buf), 0);
			}
			if(rd == 0){
				cu_esp_lan_close_relay(es, i, 1u, "relay remote closed");
				break;
			}
			if(rd < 0){
				if(cu_esp_socket_would_block())
					break;
				cu_esp_lan_close_relay(es, i, 1u, "relay recv failure");
				break;
			}
			rl->last_io_ms = now_ms;
			//CZ_DEBUG_TRACE("esp.relay", "peer->relay overlay=%u vlink=%u bytes=%u", (unsigned)rl->overlay_id, (unsigned)rl->vlink_id, (unsigned)rd);
			(void)cu_esp_lan_send_data(es, &rl->sta_addr, rl->vlink_id, rl->overlay_id, buf, (uint16)rd);
		}
	}
}

static uint8 cu_esp_lan_set_would_block_local(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	WSASetLastError(WSAEWOULDBLOCK);
#else
	errno = EWOULDBLOCK;
#endif
	return 1u;
}

static uint8 cu_esp_socket_connect_pending_local(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	sint32 e = WSAGetLastError();
	return (uint8)((e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY) ? 1u : 0u);
#else
	return (uint8)((errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK || errno == EAGAIN) ? 1u : 0u);
#endif
}

static uint8 cu_esp_socket_would_block(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	sint32 e = WSAGetLastError();
	return (uint8)((e == WSAEWOULDBLOCK) ? 1u : 0u);
#else
	return (uint8)((errno == EWOULDBLOCK || errno == EAGAIN) ? 1u : 0u);
#endif
}

static void cu_esp_socket_close_local(ESP_SOCKET s)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	closesocket(s);
#else
	close(s);
#endif
}

static sint32 cu_esp_socket_set_nonblock_local(ESP_SOCKET s)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	u_long mode = 1;
	return (ioctlsocket(s, FIONBIO, &mode) == 0) ? 0 : -1;
#else
	sint32 flags = fcntl(s, F_GETFL, 0);
	if(flags < 0)
		return -1;
	return (fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0) ? 0 : -1;
#endif
}

#endif /* ENABLE_ESP_SOFTAP */

#endif /* ENABLE_ESP */
