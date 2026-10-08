/*
 *  ESP8266 peripheral (on UART)
 *
 *  Copyright (C) 2016
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

#ifndef CU_ESP_H
#define CU_ESP_H

#include "types.h"
#include <stdio.h> /* FILE */

/* ------------------------------------------------------------------------- */
/* Platform headers                                                           */
/* ------------------------------------------------------------------------- */

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
#else
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
	#include <pthread.h>
#endif

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	#include <openssl/ssl.h>
#endif

/* ------------------------------------------------------------------------- */
/* Core constants                                                             */
/* ------------------------------------------------------------------------- */

#define ESP_GPIO_PIN_COUNT	17	/* not all are usable... */

#define ESP_AP_CONNECTED	1
#define ESP_AWAITING_SEND	2
#define ESP_START_UP		8
#define ESP_READY		16
#define ESP_MUX			32
#define ESP_ECHO		64
#define ESP_PROTO_TCP		128
#define ESP_PROTO_SSL		256
#define ESP_PROTO_UDP		512
#define ESP_PROTO_IPV6		1024
#define ESP_ALL_CONNECTIONS	1024
#define ESP_INTERNET_ACCESS	2048
#define ESP_LIST_APS		4096
#define ESP_CIPDINFO		8192
#define ESP_SMARTCONFIG_ACTIVE	16384
#define ESP_AUTOCONNECT		32768

#define ESP_AS_AP			1
#define ESP_AS_STA			2
#define ESP_AS_AP_STA			3

#define ESP_MODE_TEXT			0

#define ESP_USER_MODE_AT		0
#define ESP_USER_MODE_SEND		1
#define ESP_USER_MODE_UNVARNISHED	2
#define ESP_USER_MODE_PASSTHROUGH	3

#define ESP_WIFI_MODE_STATION		1
#define ESP_WIFI_MODE_SOFTAP		2
#define ESP_WIFI_MODE_SOFTAP_STATION	3

#define ESP_FACTORY_DEFAULT_BAUD_BITS	30	/* default “baud bits” value */
#define ESP_FACTORY_BAUD_RATE		9600

#define ESP_BIN_ECHO			1
#define ESP_BIN_CONNECT			2
#define ESP_BIN_DISCONNECT		3
#define ESP_BIN_SEND			4
#define ESP_BIN_SEND_BIG		5

#define ESP_SERIAL_OPEN_ERROR		-1

#define ESP_UART_AWAITING_COMMAND	4096
#define ESP_UART_AWAITING_TIME		8192
#define ESP_UART_AWAITING_PAYLOAD	16384

#define ESP_DID_FIRST_TICK				32768
#define ESP_UZEBOX_ACKNOWLEDGED_THREAD		65536

/* Timing */
#define ESP_UZEBOX_CORE_FREQUENCY	28636363UL
#define ESP_RESET_BOOT_DELAY		(ESP_UZEBOX_CORE_FREQUENCY + (ESP_UZEBOX_CORE_FREQUENCY >> 2))
#define ESP_AT_MS_DELAY			(ESP_UZEBOX_CORE_FREQUENCY / 1000UL)
#define ESP_AT_OK_DELAY			(ESP_AT_MS_DELAY)
#define ESP_AT_CWLAP_DELAY		(4000UL * ESP_AT_MS_DELAY)
#define ESP_AT_CWLAP_INTER_DELAY	(6UL * ESP_AT_MS_DELAY)
#define ESP_AT_CWJAP_DELAY		(1800UL * ESP_AT_MS_DELAY)
#define ESP_AT_IP_DELAY			(100UL * ESP_AT_MS_DELAY)
#define ESP_UNVARNISHED_DELAY		(20UL * ESP_AT_MS_DELAY * 3UL)
#define ESP_AT_RST_DELAY		(ESP_AT_OK_DELAY)
#define ESP_SNTP_NET_DELAY		(40UL * ESP_AT_MS_DELAY)

#define ESP_DEFAULT_CHANNEL		7
#define ESP_DEFAULT_RECONN_INTERVAL	1
#define ESP_DEFAULT_LISTEN_INTERVAL	3
#define ESP_DEFAULT_SCAN_MODE		1
#define ESP_DEFAULT_JAP_TIMEOUT		15
#define ESP_DEFAULT_PMF			0

#define ESP_DEFAULT_COUNTRY_POLICY	1
#define ESP_DEFAULT_COUNTRY_CODE	"US"
#define ESP_DEFAULT_COUNTRY_START_CH	1
#define ESP_DEFAULT_COUNTRY_COUNT	11

/* Link IDs: ESP-AT supports 0..4 */
#define ESP_LINK_MAX_ID		4u
#define ESP_LINK_COUNT		(ESP_LINK_MAX_ID + 1u)	/* 5 */

#define ESP_MAX_LINKS		ESP_LINK_COUNT		/* ESP-AT 2.3: 0..4 */
#define ESP_CIPBUF_MAX_LINKS	ESP_MAX_LINKS
#define ESP_SSL_SNI_MAX		65			/* 64 bytes + NUL */

#define ESP_CIPBUF_RX_CAP	2048u			/* per-link buffered RX */
#define ESP_SSL_SLOT_COUNT	8u
#define ESP_SSL_PATH_MAX	256u

/* ------------------------------------------------------------------------- */
/* Sockets                                                                    */
/* ------------------------------------------------------------------------- */

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	typedef SOCKET ESP_SOCKET;
	#define ESP_SOCKET_ERROR	INVALID_SOCKET
	#define ESP_INVALID_SOCKET	INVALID_SOCKET
#else
	typedef int ESP_SOCKET;
	#define ESP_SOCKET_ERROR	-1
	#define ESP_INVALID_SOCKET	-1
#endif

#ifdef _WIN32
	#define ESP_EAGAIN		WSAEWOULDBLOCK
	#define ESP_WOULD_BLOCK		WSAEWOULDBLOCK
	#define ESP_ECONNREFUSED	WSAECONNREFUSED
#else
	#define ESP_EAGAIN		EAGAIN
	#define ESP_WOULD_BLOCK		EWOULDBLOCK
	#define ESP_ECONNREFUSED	ECONNREFUSED
#endif

/* ------------------------------------------------------------------------- */
/* Strings (defined in .c)                                                    */
/* ------------------------------------------------------------------------- */

extern const char start_up_string[];
extern const char at_gmr_string[];

#define ESP_NUM_FAKE_APS	10
extern const char * const fake_ap_name[ESP_NUM_FAKE_APS];
extern const char * const fake_ap_mac[ESP_NUM_FAKE_APS];

extern const char default_sntp_server0[];
extern const char default_sntp_server1[];
extern const char default_sntp_server2[];

#define ESP_DEFAULT_TIMEZONE	8

/* ------------------------------------------------------------------------- */
/* Virtual LAN overlay (emulator-to-emulator SoftAP discovery / DHCP)         */
/* ------------------------------------------------------------------------- */

#define CU_ESP_LAN_MCAST_ADDR	"239.255.67.112"
#define CU_ESP_LAN_MCAST_PORT	38626

#define CU_ESP_LAN_MAX_APS	32u
#define CU_ESP_LAN_DHCP_MAX_LEASES	8u
#define CU_ESP_LAN_MAX_RELAYS	8u
#define CU_ESP_LAN_DATA_MAX	1024u
#define CU_ESP_LAN_VRX_MAX	2048u
#define CU_ESP_LAN_RELAY_TX_MAX	4096u
#define CU_ESP_LAN_HOST_MAX	128u

#define CU_ESP_LAN_SSID_MAX	64u
#define CU_ESP_LAN_AP_EXPIRE_MS	3000u

typedef struct{
	uint8	in_use;
	uint8	bssid[6];
	sint8	ssid[CU_ESP_LAN_SSID_MAX];
	uint8	channel;
	sint8	rssi;		/* simulated */
	uint8	enc;		/* 0=open, 2/3/4… */
	uint16	adv_port;	/* sender unicast port */
	struct sockaddr_in adv_addr;
	uint32	last_seen_ms;
}cu_esp_lan_ap_entry_t;

typedef struct{
	uint8	in_use;
	uint8	mac[6];
	uint32	ip_be;		/* IPv4 (network byte order) */
	uint32	expire_ms;
	struct sockaddr_in peer_addr;
}cu_esp_lan_lease_t;

typedef struct{
	uint8	active;
	uint8	pending;
	uint8	closed_remote;
	uint8	overlay_id;
	uint16	rx_head;
	uint16	rx_len;
	uint16	local_port;
	uint8	udp_mode;
	uint32	open_req_id;
	uint32	open_deadline_ms;
	uint32	last_io_ms;
	uint32	type;
	struct sockaddr_in peer_addr;
	uint8	rx_buf[CU_ESP_LAN_VRX_MAX];
}cu_esp_lan_vlink_t;

typedef struct{
	uint8	in_use;
	uint8	state;
	uint8	vlink_id;
	uint8	overlay_id;
	uint8	udp_mode;
	uint8	open_resp_pending;
	uint8	close_sent;
	uint8	pad0;
	uint16	port;
	uint16	local_port;
	uint16	tx_len;
	uint16	tx_off;
	uint32	type;
	uint32	last_io_ms;
	uint32	open_seq;
	uint32	open_deadline_ms;
	sint32	last_err;
	uint8	sta_mac[6];
	uint8	bssid[6];
	struct sockaddr_in sta_addr;
	ESP_SOCKET sock;
	struct sockaddr_in peer_addr;
	sint8	host[CU_ESP_LAN_HOST_MAX];
	uint8	tx_buf[CU_ESP_LAN_RELAY_TX_MAX];
}cu_esp_lan_relay_t;

typedef struct{
	/* persistent identity */
	uint8	local_mac[6];		/* generated once, persisted via config */
	uint8	pad0[2];

	/* sockets */
	ESP_SOCKET	mcast_sock;	/* discovery multicast */
	ESP_SOCKET	ucast_sock;	/* per-instance unicast for control/data */
	uint16		ucast_port;
	uint16		pad1;

	/* runtime flags */
	uint8		enable;
	uint8		adv_enable;	/* advertising SoftAP beacons */
	uint8		joined;		/* joined another SoftAP */
	uint8		join_req_pending;

	/* timebase */
	uint32		now_ms;
	uint32		last_adv_ms;
	uint32		join_req_id;
	uint32		join_req_sent_ms;
	uint32		join_deadline_ms;
	uint32		lease_renew_ms;

	/* discovered APs */
	cu_esp_lan_ap_entry_t aps[CU_ESP_LAN_MAX_APS];

	/* DHCP (AP-side, /24 only for now) */
	uint8		dhcp_enable_ap;
	uint8		dhcp_enable_sta;
	uint16		pad3;

	uint32		ap_ip_be;	/* from soft_ap_ip */
	uint32		netmask_be;
	uint32		pool_start_be;
	uint32		pool_end_be;
	uint32		dns_ip_be;

	cu_esp_lan_lease_t leases[CU_ESP_LAN_DHCP_MAX_LEASES];

	/* join target (when CWJAP to LAN-discovered AP) */
	struct sockaddr_in join_target_addr;
	struct sockaddr_in joined_ap_addr;
	uint8	join_target_bssid[6];
	uint8	joined_ap_bssid[6];
	uint8	join_result;		/* 0=pending/none, 1=accept, 2=reject */
	uint8	join_fail_code;
	uint8	pad4[2];

	/* virtual transport overlay */
	cu_esp_lan_vlink_t	vlinks[ESP_MAX_LINKS];
	cu_esp_lan_relay_t	relays[CU_ESP_LAN_MAX_RELAYS];
}cu_esp_lan_t;

/* ------------------------------------------------------------------------- */
/* Async worker / host-net helpers                                            */
/* ------------------------------------------------------------------------- */

#define CU_ESP_ASYNC_QSZ	32
#define CU_ESP_HOST_MAX		128

#define CU_JOB_NONE		0
#define CU_JOB_CONNECT		1
#define CU_JOB_PING		2
#define CU_JOB_DNS		3
#define CU_JOB_UDP_SEND_RESOLVE	4

#define CU_EVT_NONE		0
#define CU_EVT_CONNECT_DONE	1
#define CU_EVT_PING_DONE	2
#define CU_EVT_DNS_DONE	3
#define CU_EVT_UDP_SEND_RESOLVE_DONE	4

typedef struct{
	uint8	kind;
	uint8	sock;
	uint16	local_port;
	uint32	port;
	uint32	type;
	sint16	timeout_ms;
	uint8	udp_mode;
	uint8	ip_network;	/* 0/1 preferred, 2 IPv4 only, 3 IPv6 only */
	uint32	timeout_ms32;
	char	host[CU_ESP_HOST_MAX];
	char	local_ip[64];
} cu_async_job_t;

typedef struct{
	uint8	kind;
	uint8	sock;
	uint8	peer_valid;
	uint8	pad;
	uint32	type;
	sint32	err;
	sint16	rtt_ms;
	uint16	peer_port;
	uint32	ready_ms;
	uint32	ipv4_be;
	uint32	peer_ipv4_be;
	char	ip_text[64];
	char	peer_ip[64];
	ESP_SOCKET	new_sock;
} cu_async_evt_t;

typedef enum{
	CU_PING_NONE = 0,
	CU_PING_ICMP_WIN,
	CU_PING_ICMP_RAW,
	CU_PING_TCP_ONLY
} cu_ping_mode_t;

#define CU_ESP_AT_FW_LEGACY_17	0u
#define CU_ESP_AT_FW_ESPAT_230	1u

#define CU_ESP_TRANSLINK_TCP	1u
#define CU_ESP_TRANSLINK_UDP	2u
#define CU_ESP_TRANSLINK_SSL	3u
#define CU_ESP_TRANSLINK_TCP6	4u
#define CU_ESP_TRANSLINK_UDP6	5u
#define CU_ESP_TRANSLINK_SSL6	6u

#define CU_ESP_SYSREG_SLOTS	64u
#define CU_ESP_SYSFLASH_PART_SIZE	(64u * 1024u)
#define CU_ESP_SYSFLASH_PART_NAME	"mfg_nvs"

#define CU_ESP_MQTT_STRING_MAX	1024u
#define CU_ESP_MQTT_HOST_MAX	128u
#define CU_ESP_MQTT_PATH_MAX	32u
#define CU_ESP_MQTT_TOPIC_MAX	128u
#define CU_ESP_MQTT_EVENT_DATA_MAX	2048u
#define CU_ESP_MQTT_INPUT_NONE	0u
#define CU_ESP_MQTT_INPUT_CLIENTID	1u
#define CU_ESP_MQTT_INPUT_USERNAME	2u
#define CU_ESP_MQTT_INPUT_PASSWORD	3u
#define CU_ESP_MQTT_INPUT_PUBRAW	4u
#define CU_ESP_MQTT_INPUT_SYSFLASH	5u
#define CU_ESP_MQTT_EVT_CONNECTED	1u
#define CU_ESP_MQTT_EVT_DISCONNECTED	2u
#define CU_ESP_MQTT_EVT_MESSAGE	3u

/* ------------------------------------------------------------------------- */
/* ESP state                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct{
	uint32	rx_await_bytes;
	uint32	rx_await_time;

	uint8	cmd_buf[16UL*1024UL];

	uint32	server_timer;
	uint32	server_timeout;
	uint32	wifi_timer;
	uint32	wifi_delay;

	ESP_SOCKET	active_socket;

	sint8	rx_packet[16UL*1024UL];
	uint32	rx_packet_bytes;
	uint8	reset_pin;
	uint8	reset_pin_prev;
	uint8	pad;
	uint8	rf_power;

	uint32	state;
	uint8	flash_dirty;
	uint8	ready;

	auint	emulation_model;	/* 0=disabled, 1=ESP8266, 2=ESP32, 3=ESP32-ETH01 */

	auint	user_input_mode;
	auint	baud_rate;
	auint	baud_divisor;
	auint	baud_divisor_module;
	uint8	write_enabled;
	uint8	read_enabled;
	auint	write_ready_cycle;
	auint	read_ready_cycle;
	auint	unvarnished_end_cycle;
	auint	unvarnished_bytes;
	auint	remaining_send_bytes;
	auint	busy_ready_cycle;

	sint8	write_buf[1024*4];
	sint8	read_buf[1024*4];
	auint	write_buf_pos_in;
	auint	read_buf_pos_in;
	auint	read_buf_pos_out;
	sint8	last_read_byte;

	ESP_SOCKET	socks[ESP_LINK_COUNT];
	auint	proto[ESP_LINK_COUNT];

	/* CIPBUF passive receive */
	uint8	cipbuf_recv_mode;	/* 0=active(+IPD w/data), 1=passive(buffered) */
	uint8	pad_cipbuf0;

	uint8	cipbuf_rx[ESP_MAX_LINKS + 1u][ESP_CIPBUF_RX_CAP];
	uint16	cipbuf_in[ESP_MAX_LINKS + 1u];
	uint16	cipbuf_out[ESP_MAX_LINKS + 1u];
	uint16	cipbuf_len[ESP_MAX_LINKS + 1u];

	/* SSL config storage */
	uint16	ssl_rx_buf_size; /* AT+CIPSSLSIZE */
	uint8	ssl_auth_mode[ESP_MAX_LINKS];
	uint8	ssl_pki_num[ESP_MAX_LINKS];
	uint8	ssl_ca_num[ESP_MAX_LINKS];
	uint16	ssl_keep_alive[ESP_MAX_LINKS];
	sint8	ssl_sni[ESP_MAX_LINKS][ESP_SSL_SNI_MAX];

	/* MDNS config storage */
	uint8	mdns_enable;
	uint8	pad_mdns0;
	uint16	mdns_port;
	sint8	mdns_host[64];
	sint8	mdns_service[64];

	/* RFVDD (emulated supply) */
	uint16	vdd33;

	auint	delay_pos[8];
	auint	delay_len[8];

	auint	last_cycle_tick;
	auint	ip_delay_timer;
	auint	join_delay_timer;

	auint	uart_ucsr0a, uart_ucsr0b, uart_ucsr0c, uart_ubrr0l, uart_ubrr0h, uart_scramble;
	uint8	uart_double_speed, uart_synchronous, uart_data_bits, uart_rx_enabled, uart_tx_enabled;
	uint8	uart_parity, uart_stop_bits;
	auint	uart_baud_bits;
	auint	uart_baud_bits_module;
	auint	uart_baud_bits_module_default;
	uint8	uart_profile;
	uint8	uart_rx_pending_valid;
	uint8	uart_rx_pending_byte;
	uint8	uart_overrun;
	uint8	uart_rx_error_flags;
	uint8	uart_txc_pending;
	uint8	uart_tx_udr_valid;
	uint8	uart_tx_udr_byte;
	uint8	uart_tx_shift_valid;
	uint8	uart_tx_shift_byte;
	auint	uart_rx_pending_cycle;
	auint	uart_tx_complete_cycle;

	uint32	uart_logging;
	uint32	uart_logging_started;
	FILE	*uart_logging_file;
	sint8	uart_logging_fname[64];

	auint	last_plus;
	auint	num_plus;

	uint32	uart_playback;
	uint32	uart_playback_started;
	FILE	*uart_playback_file;
	sint8	uart_playback_fname[64];

	uint32	serial_route;	/* 0=disconnected, 1=ESP module, 2=host serial, 3=host MIDI, 4=virtual MIDI, 5=TCP serial, 6=loopback */
	uint32	serial_esp_model;	/* 1=ESP8266, 2=ESP32, 3=ESP32-ETH01 */
	uint32	host_serial_bypass;
	sint8	host_serial_device_name[256];
	uint32	host_midi_bypass;
	sint8	host_midi_port_name[256];
	uint32	virtual_midi_mode;	/* 1=instrument, 2=controller, 3=bidirectional */
	sint8	virtual_midi_port_name[256];
	sint8	tcp_serial_host[256];
	uint32	tcp_serial_port;
	uint32	tcp_serial_auto_reconnect;
	uint32	tcp_serial_mode;	/* 0=client, 1=server */
	uint32	tcp_serial_retry_interval_ms;
	uint32	tcp_serial_retry_at_ms;
	sint32	tcp_serial_last_error;
	uint32	tcp_serial_connect_pending;
	uint32	tcp_serial_state;
	uint32	tcp_serial_tx_head;
	uint32	tcp_serial_tx_tail;
	uint32	tcp_serial_tx_count;
	uint32	tcp_serial_rx_head;
	uint32	tcp_serial_rx_tail;
	uint32	tcp_serial_rx_count;
	uint32	tcp_serial_tx_drops;
	uint32	tcp_serial_rx_drops;
	uint8	tcp_serial_tx_buf[4096];
	uint8	tcp_serial_rx_buf[4096];
	uint32	loopback_head;
	uint32	loopback_tail;
	uint32	loopback_count;
	uint32	loopback_drops;
	uint8	loopback_buf[1024];
	ESP_SOCKET	tcp_serial_sock;
	ESP_SOCKET	tcp_serial_listen_sock;
	uint8	tcp_serial_enabled;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	HANDLE	host_serial_port;
#else
	sint32	host_serial_port;
#endif
	uint8	host_serial_enabled;
	uint8	host_midi_enabled;
	uint8	virtual_midi_enabled;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	auint	winsock_enabled;
#endif

	uint32	uart_at_state;	/* 0 = AT, 1 = binary mode */
	uint8	baud_bits0, baud_bits1;

	uint32	send_to_socket;
	uint32	cip_mode;

	ESP_SOCKET	listen_socket;
	uint8	tcp_send_buf[ESP_MAX_LINKS][2048];
	uint32	tcp_seg_id[ESP_MAX_LINKS];
	ESP_SOCKET	ping_sock[3];

	uint32	protocol[ESP_LINK_COUNT];

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	SOCKADDR_IN	sock_info[ESP_LINK_COUNT];
#else
	struct sockaddr_in	sock_info[ESP_LINK_COUNT];
#endif

#ifdef __EMSCRIPTEN__
	uint32	ws_proxy_enabled;
#endif

	/* Station / WiFi */
	sint8	wifi_name[64];
	sint8	wifi_pass[64];
	sint8	wifi_mac[32];
	sint8	wifi_ip[24];
	uint8	wifi_channel;
	uint8	wifi_mode;
	uint8	wifi_enc;
	sint8	wifi_rssi;
	uint8	wifi_pci_en;
	uint16	wifi_reconn_interval;
	uint8	wifi_listen_interval;
	uint8	wifi_scan_mode;
	uint16	wifi_jap_timeout;
	uint8	wifi_pmf;
	uint8	wps;

	/* Country */
	uint8	country_policy;		/* 0: follow AP, 1: fixed */
	sint8	country_code[4];	/* "US", NUL-terminated */
	uint8	country_start_ch;	/* 1..14 */
	uint8	country_count;		/* 1..14 */

	/* Other interfaces */
	sint8	ethernet_mac[32];
	sint8	ethernet_ip[24];
	sint8	bluetooth_mac[32];
	sint8	bluetooth_ip[24];

	/* SoftAP */
	sint8	soft_ap_name[64];
	sint8	soft_ap_pass[65];
	sint8	soft_ap_mac[32];
	sint8	soft_ap_ip[24];
	sint8	soft_ap_gateway[24];
	sint8	soft_ap_netmask[24];
	uint32	soft_ap_channel;
	uint32	soft_ap_encryption;
	uint8	soft_ap_enabled;

	/* SAVETRANSLINK */
	sint8	translink_host[128];
	uint8	translink_proto;
	uint16	translink_port;

	/* Station MAC/IP (AT+CIPSTAMAC / AT+CIPSTA) */
	sint8	station_mac[32];
	sint8	station_ip[24];
	sint8	station_gateway[24];
	sint8	station_netmask[24];

	sint8	uzenet_pass[16];

	/* SNTP */
	uint32	sntp_enabled;
	sint32	sntp_timezone;
	sint8	sntp_server[3][48];
	sint8	sntp_lasttime[3][64];

	/* SYS */
	uint8	sysmsg_flags;
	sint32	sleep_mode;
	uint16	adc;

	/* GPIO */
	uint8	gpio_mode[ESP_GPIO_PIN_COUNT];
	uint8	gpio_pullup[ESP_GPIO_PIN_COUNT];
	uint8	gpio_dir[ESP_GPIO_PIN_COUNT];
	uint8	gpio_level[ESP_GPIO_PIN_COUNT];

	/* WeChat */
	uint8	wechat_enable;
	sint8	wechat_number[32];
	uint32	wechat_type;
	uint32	wechat_time;

	/* Virtual LAN overlay */
	cu_esp_lan_t	lan;

	/* DNS config (for AT+CIPDNS) */
	uint8	dns_enable;
	uint8	pad_dns0;
	sint8	dns_server[3][48];

	/* Server max conn (AT+CIPSERVERMAXCONN) */
	uint8	server_max_conn;
	uint8	pad_srv0;

	/* CIPCHECKSEQ storage */
	uint8	cipcheckseq;
	uint8	pad_seq0;

	uint8 link_is_server[ESP_MAX_LINKS];
	uint8 link_is_ssl[ESP_MAX_LINKS];

	/* Expanded SSL config storage */
	sint8	ssl_common_name[ESP_MAX_LINKS][96];
	uint8	ssl_alpn_count[ESP_MAX_LINKS];
	sint8	ssl_alpn0[ESP_MAX_LINKS][32];
	sint8	ssl_alpn1[ESP_MAX_LINKS][32];
	sint8	ssl_psk_id[ESP_MAX_LINKS][64];
	sint8	ssl_psk_key[ESP_MAX_LINKS][64];
	/* ESP-AT names the CIPSSLCPSK parameters <psk>,<hint>.  OpenSSL's
	 * callback needs those as binary key + client identity respectively.
	 * Keep a binary copy so CIPSSLCPSKHEX can represent embedded NULs. */
	uint8	ssl_psk_bin[ESP_MAX_LINKS][32];
	uint8	ssl_psk_bin_len[ESP_MAX_LINKS];
	sint8	ssl_ca_path[ESP_SSL_SLOT_COUNT][ESP_SSL_PATH_MAX];
	sint8	ssl_pki_cert_path[ESP_SSL_SLOT_COUNT][ESP_SSL_PATH_MAX];
	sint8	ssl_pki_key_path[ESP_SSL_SLOT_COUNT][ESP_SSL_PATH_MAX];

	/* Collapsed host-net runtime state */
	uint8	link_state[ESP_MAX_LINKS];
	sint32	link_err[ESP_MAX_LINKS];
	uint8	link_notice_ok[ESP_MAX_LINKS];
	uint8	link_notice_err[ESP_MAX_LINKS];
	uint8	link_wait_ok[ESP_MAX_LINKS];
	uint8	wifi_join_pending;
	uint32	link_ready_ms[ESP_MAX_LINKS];
	ESP_SOCKET	link_pending_sock[ESP_MAX_LINKS];
	uint32	link_type[ESP_MAX_LINKS];
	uint32	link_port[ESP_MAX_LINKS];
	char	link_host[ESP_MAX_LINKS][CU_ESP_HOST_MAX];
	uint16	udp_local_port[ESP_MAX_LINKS];
	uint8	udp_mode[ESP_MAX_LINKS];
	uint8	udp_mode1_latched[ESP_MAX_LINKS];
	uint8	udp_send_override[ESP_MAX_LINKS];
	uint8	pad_udp0[ESP_MAX_LINKS];
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	SOCKADDR_IN	udp_send_info[ESP_LINK_COUNT];
#else
	struct sockaddr_in	udp_send_info[ESP_LINK_COUNT];
#endif

	uint8	ping_pending;
	uint8	ping_ready;
	sint16	ping_rtt;
	sint16	pad_ping0;
	sint32	ping_err;
	uint32	ping_ready_ms;

	uint8	cwlap_sort_rssi;
	uint8	pad_cwlap0;
	uint16	cwlap_print_mask;
	sint16	cwlap_rssi_filter;
	uint16	cwlap_authmask;

	uint8	dns_pending;
	uint8	dns_ready;
	uint16	pad_dnsq0;
	sint32	dns_err;
	uint32	dns_ready_ms;
	uint32	dns_ipv4_be;
	sint8	dns_host[CU_ESP_HOST_MAX];

	uint8	udp_send_resolve_pending;
	uint8	udp_send_resolve_ready;
	uint8	udp_send_resolve_sock;
	uint8	send_prep_pending;
	sint32	udp_send_resolve_err;
	uint32	udp_send_resolve_ready_ms;
	uint32	send_prep_len;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	SOCKADDR_IN	udp_send_resolve_info;
#else
	struct sockaddr_in	udp_send_resolve_info;
#endif

	cu_ping_mode_t	ping_mode;
	uint8	pad_ping1[4];
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	HANDLE	ping_icmp_h;
#else
	ESP_SOCKET	ping_icmp_sock;
	uint16	ping_seq;
	uint16	pad_ping2;
#endif

#ifndef __EMSCRIPTEN__
	volatile uint8	async_run;
	uint8	async_sync_init;
	uint16	pad_async0;
	cu_async_job_t	async_jobs[CU_ESP_ASYNC_QSZ];
	uint32	async_job_r;
	uint32	async_job_w;
	cu_async_evt_t	async_evts[CU_ESP_ASYNC_QSZ];
	uint32	async_evt_r;
	uint32	async_evt_w;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	HANDLE	async_thread;
	HANDLE	async_event;
	CRITICAL_SECTION	async_cs;
#else
	pthread_t	async_thread;
	pthread_mutex_t	async_mtx;
	pthread_cond_t	async_cv;
#endif
#endif

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	SSL_CTX	*tls_ctx[ESP_MAX_LINKS];
	SSL	*tls_ssl[ESP_MAX_LINKS];
#endif

	uint16	listen_port;
	uint16	server_last_port;
	uint8	server_enabled;
	uint8	pad_srv1;

	/* ESP-AT 2.3 extension.  Keep append-only from here forward. */
	uint8	at_firmware_profile;
	uint8	sysstore_mode;
	uint8	syslog_enabled;
	uint8	ipv6_enabled;
	uint16	cip_reconn_interval;
	uint16	wifi_reconn_repeat;
	uint8	wifi_ap_proto;
	uint8	wifi_sta_proto;
	uint8	wifi_autoconn;
	uint8	pad_at23_0;
	uint32	systimestamp_value;
	uint32	systimestamp_host;
	sint8	station_hostname[33];

	sint16	tcp_linger[ESP_MAX_LINKS];
	uint8	tcp_nodelay[ESP_MAX_LINKS];
	uint32	tcp_sndtimeo[ESP_MAX_LINKS];
	uint16	tcp_keepalive[ESP_MAX_LINKS];
	sint8	link_local_ip[ESP_MAX_LINKS][64];
	uint32	link_timeout_ms[ESP_MAX_LINKS];

	uint8	ssl_cipher_count[ESP_MAX_LINKS];
	uint16	ssl_cipher[ESP_MAX_LINKS][12];
	sint8	ssl_alpn2[ESP_MAX_LINKS][32];
	sint8	ssl_alpn3[ESP_MAX_LINKS][32];
	sint8	ssl_alpn4[ESP_MAX_LINKS][32];

	uint8	send_extended;
	uint8	send_extended_escape;
	uint8	pad_send_extended[2];

	uint8	translink_enabled;
	uint8	translink_kind;
	uint16	translink_local_port;
	uint16	translink_keepalive;
	uint8	translink_boot_pending;
	uint8	translink_udp_mode;
	uint32	translink_retry_ms;

	uint8	link_peer_ipv6_valid[ESP_MAX_LINKS];
	uint8	link_peer_ipv6[ESP_MAX_LINKS][16];
	uint16	link_peer_ipv6_port[ESP_MAX_LINKS];
	uint8	udp_send_ipv6_valid[ESP_MAX_LINKS];
	uint8	udp_send_ipv6[ESP_MAX_LINKS][16];
	uint16	udp_send_ipv6_port[ESP_MAX_LINKS];
	uint8	udp_resolve_ipv6_valid;
	uint8	udp_resolve_ipv6[16];
	uint16	udp_resolve_ipv6_port;
	char	dns_result_text[64];

	uint8	sleepwk_source;
	uint8	sleepwk_gpio;
	uint8	sleepwk_level;
	uint8	pad_sleepwk;
	uint32	sysreg_addr[CU_ESP_SYSREG_SLOTS];
	uint32	sysreg_value[CU_ESP_SYSREG_SLOTS];
	uint8	sysreg_valid[CU_ESP_SYSREG_SLOTS];
	uint8	sysflash_erased;
	uint8	sysflash_write_active;
	uint16	sysflash_write_offset;
	uint32	sysflash_write_length;
	uint8	sysflash_data[CU_ESP_SYSFLASH_PART_SIZE];

	/* ESP-AT MQTT serial-visible configuration/input state. */
	uint8	mqtt_scheme;
	uint8	mqtt_user_configured;
	uint8	mqtt_conn_configured;
	uint8	mqtt_cert_key_id;
	uint8	mqtt_ca_id;
	uint8	mqtt_reconnect;
	uint8	mqtt_disable_clean_session;
	uint8	mqtt_lwt_qos;
	uint8	mqtt_lwt_retain;
	uint8	mqtt_input_kind;
	uint16	mqtt_keepalive;
	uint16	mqtt_port;
	uint16	mqtt_input_expected;
	uint8	mqtt_raw_qos;
	uint8	mqtt_raw_retain;
	uint8	soft_ap_max_conn;
	uint8	soft_ap_hidden;
	uint16	soft_ap_dhcp_lease_min;
	sint8	mqtt_client_id[CU_ESP_MQTT_STRING_MAX + 1u];
	sint8	mqtt_username[CU_ESP_MQTT_STRING_MAX + 1u];
	sint8	mqtt_password[CU_ESP_MQTT_STRING_MAX + 1u];
	sint8	mqtt_path[CU_ESP_MQTT_PATH_MAX + 1u];
	sint8	mqtt_host[CU_ESP_MQTT_HOST_MAX + 1u];
	sint8	mqtt_lwt_topic[CU_ESP_MQTT_TOPIC_MAX + 1u];
	sint8	mqtt_lwt_msg[CU_ESP_MQTT_TOPIC_MAX + 1u];
	sint8	mqtt_raw_topic[CU_ESP_MQTT_TOPIC_MAX + 1u];

}cu_state_esp_t;

/* ------------------------------------------------------------------------- */
/* Core API                                                                   */
/* ------------------------------------------------------------------------- */

void	cu_esp_reset(auint cycle);
void	cu_esp_send(auint data, auint cycle);
auint	cu_esp_recv(auint cycle);

cu_state_esp_t* cu_esp_get_state(void);

void	cu_esp_clear_at_command(void);
void	cu_esp_reset_pin(uint8 state, auint cycle);

void	cu_esp_uzebox_write(uint8 val, auint cycle);
auint	cu_esp_uzebox_read(auint cycle);
void	cu_esp_uzebox_modify(auint port, auint val, auint cycle);
auint	cu_esp_uzebox_status(auint cycle);
auint	cu_esp_uzebox_read_ready(auint cycle);
auint	cu_esp_uzebox_write_ready(auint cycle);

void	cu_esp_reset_network(void);

auint	cu_esp_verify_mac_string(auint pos);

void	cu_esp_external_write(uint8 val);
void	cu_esp_external_read(void);

auint	cu_esp_net_last_error(void);
sint32	cu_esp_net_connect(sint8 *hostname, uint32 sock, uint32 port, uint32 type);
sint32	cu_esp_net_connect_ex(sint8 *hostname, uint32 sock, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode);
void	cu_esp_net_reapply_tcp_options(uint32 link);
uint16	cu_esp_net_get_local_port(uint32 link);
sint32	cu_esp_net_get_peer_text(uint32 link, char *out, auint out_sz, uint16 *port);
sint32	cu_esp_net_send(uint32 sock, sint8 *buf, sint32 len, sint32 flags);
sint32	cu_esp_listen(uint32 port);
uint8	cu_esp_ssl_link_busy(uint32 except_sock);
sint32	cu_esp_net_recv(uint32 sock, sint8 *buf, sint32 len, sint32 flags);
void	cu_esp_net_send_unvarnished(sint8 *buf, auint len);

auint	cu_esp_atoi(char *str);
sint32	cu_esp_get_last_error(void);

sint32	cu_esp_init_sockets(void);
void	cu_esp_net_cleanup(void);
void	cu_esp_close_socket(uint32 sock);

void	cu_esp_reset_uart(void);
void	cu_esp_update(void);

void	cu_esp_txi(sint32 i);
void	cu_esp_txp(const char *s);
void	cu_esp_txl(const char *s, auint len);
void	cu_esp_txp_ok(void);
void	cu_esp_txp_error(void);

void	cu_esp_at_txp_bad_command(void); /* optional alias; you can route to cu_esp_at_bad_command */
void	cu_esp_timed_stall(auint cycles);
auint	cu_esp_update_timer_counts(auint cycle);
void	cu_esp_service_host_state(void);
sint32	cu_esp_process_ipd(void);

void	cu_esp_save_config(void);
auint	cu_esp_load_config(void);
auint	cu_esp_reload_config_runtime(void);
auint	cu_esp_get_at_firmware_profile(void);
void	cu_esp_set_at_firmware_profile(auint profile);
void	cu_esp_runtime_shutdown(void);
boole	cu_esp_config_live_ready(void);
boole	cu_esp_append_live_config(FILE *f, boole with_comments);
boole	cu_esp_append_default_config(FILE *f, boole with_comments);
boole	cu_esp_append_config_from_path(FILE *f, const char *path, boole with_comments);
void	cu_esp_process_at(sint8 *cmd_buf);

/* ------------------------------------------------------------------------- */
/* AT command handlers                                                        */
/* ------------------------------------------------------------------------- */

void	cu_esp_at(void);
void	cu_esp_at_ate(sint8 *cmd_buf);
void	cu_esp_at_rst(void);
void	cu_esp_at_gmr(void);
void	cu_esp_at_cmd(sint8 *cmd_buf);
void	cu_esp_at_sysstore(sint8 *cmd_buf);
void	cu_esp_at_systimestamp(sint8 *cmd_buf);
void	cu_esp_at_syslog(sint8 *cmd_buf);
void	cu_esp_at_sysflash(sint8 *cmd_buf);
void	cu_esp_at_sysrollback(sint8 *cmd_buf);
void	cu_esp_at_sleepwkcfg(sint8 *cmd_buf);
void	cu_esp_at_sysreg(sint8 *cmd_buf);
void	cu_esp_at_cwstate(sint8 *cmd_buf);
void	cu_esp_at_cwreconncfg(sint8 *cmd_buf);
void	cu_esp_at_cwapproto(sint8 *cmd_buf);
void	cu_esp_at_cwstaproto(sint8 *cmd_buf);
void	cu_esp_at_cwqif(sint8 *cmd_buf);
void	cu_esp_at_cipstate(sint8 *cmd_buf);
void	cu_esp_at_cipstartex(sint8 *cmd_buf);
void	cu_esp_at_cipreconnintv(sint8 *cmd_buf);
void	cu_esp_at_ciptcpopt(sint8 *cmd_buf);
void	cu_esp_at_cipv6(sint8 *cmd_buf);
void	cu_esp_at_cipsslccipher(sint8 *cmd_buf);
void	cu_esp_at_mqttusercfg(sint8 *cmd_buf);
void	cu_esp_at_mqttlongclientid(sint8 *cmd_buf);
void	cu_esp_at_mqttlongusername(sint8 *cmd_buf);
void	cu_esp_at_mqttlongpassword(sint8 *cmd_buf);
void	cu_esp_at_mqttconncfg(sint8 *cmd_buf);
void	cu_esp_at_mqttconn(sint8 *cmd_buf);
void	cu_esp_at_mqttpub(sint8 *cmd_buf);
void	cu_esp_at_mqttpubraw(sint8 *cmd_buf);
void	cu_esp_at_mqttsub(sint8 *cmd_buf);
void	cu_esp_at_mqttunsub(sint8 *cmd_buf);
void	cu_esp_at_mqttclean(sint8 *cmd_buf);
void	cu_esp_at_ipr(sint8 *cmd_buf);
void	cu_esp_at_gslp(sint8 *cmd_buf);
void	cu_esp_at_wps(sint8 *cmd_buf);
void	cu_esp_at_rfpower(sint8 *cmd_buf);
void	cu_esp_at_rfvdd(sint8 *cmd_buf);
void	cu_esp_at_wakeupgpio(sint8 *cmd_buf);
void	cu_esp_at_mdns(sint8 *cmd_buf);
void	cu_esp_at_savetranslink(sint8 *cmd_buf);

void	cu_esp_at_ciobaud(sint8 *cmd_buf);
void	cu_esp_at_cifsr(sint8 *cmd_buf);
void	cu_esp_at_ciupdate(sint8 *cmd_buf);

void	cu_esp_at_cwmode(sint8 *cmd_buf);
void	cu_esp_at_cwjap(sint8 *cmd_buf);
void	cu_esp_at_cwlap(sint8 *cmd_buf);
void	cu_esp_at_cwlapopt(sint8 *cmd_buf);
void	cu_esp_at_cwqap(sint8 *cmd_buf);
void	cu_esp_at_cwsap(sint8 *cmd_buf);
void	cu_esp_at_cwlif(sint8 *cmd_buf);
void	cu_esp_at_cwdhcp(sint8 *cmd_buf);
void	cu_esp_at_cwdhcps(sint8 *cmd_buf);
void	cu_esp_at_cwhostname(sint8 *cmd_buf);
void	cu_esp_at_cwstartdiscover(sint8 *cmd_buf);
void	cu_esp_at_cwstopdiscover(sint8 *cmd_buf);
void	cu_esp_at_cwcountry(sint8 *cmd_buf);

void	cu_esp_at_cipstamac(sint8 *cmd_buf);
void	cu_esp_at_cipapmac(sint8 *cmd_buf);
void	cu_esp_at_cipsta(sint8 *cmd_buf);
void	cu_esp_at_cipap(sint8 *cmd_buf);
void	cu_esp_at_cipstatus(sint8 *cmd_buf);
void	cu_esp_at_cipstart(sint8 *cmd_buf);
void	cu_esp_at_cipsend(sint8 *cmd_buf);
void	cu_esp_at_cipclose(sint8 *cmd_buf);
void	cu_esp_at_cipmux(sint8 *cmd_buf);
void	cu_esp_at_cipserver(sint8 *cmd_buf);
void	cu_esp_at_cipmode(sint8 *cmd_buf);
void	cu_esp_at_cipsto(sint8 *cmd_buf);

void	cu_esp_at_cipsslconf(sint8 *cmd_buf);
void	cu_esp_at_cipsslsize(sint8 *cmd_buf);
void	cu_esp_at_cipsslcconf(sint8 *cmd_buf);
void	cu_esp_at_cipsslcsni(sint8 *cmd_buf);
void	cu_esp_at_cipsslccn(sint8 *cmd_buf);
void	cu_esp_at_cipsslcalpn(sint8 *cmd_buf);
void	cu_esp_at_cipsslcpsk(sint8 *cmd_buf);

void	cu_esp_at_cipservermaxconn(sint8 *cmd_buf);
void	cu_esp_at_cipdinfo(sint8 *cmd_buf);

void	cu_esp_at_cipdns(sint8 *cmd_buf);
void	cu_esp_at_cipdomain(sint8 *cmd_buf);

void	cu_esp_at_cipbufreset(sint8 *cmd_buf);
void	cu_esp_at_cipcheckseq(sint8 *cmd_buf);
void	cu_esp_at_cipbufstatus(sint8 *cmd_buf);
void	cu_esp_at_cipsendbuf(sint8 *cmd_buf);
void	cu_esp_at_cipsendex(sint8 *cmd_buf);

void	cu_esp_at_ciprecvmode(sint8 *cmd_buf);
void	cu_esp_at_ciprecvlen(sint8 *cmd_buf);
void	cu_esp_at_ciprecvdata(sint8 *cmd_buf);

void	cu_esp_at_cipbufrecvmode(sint8 *cmd_buf);
void	cu_esp_at_cipbufrecvlen(sint8 *cmd_buf);
void	cu_esp_at_cipbufrecvdata(sint8 *cmd_buf);

void	cu_esp_at_cipsntptime(sint8 *cmd_buf);
void	cu_esp_at_cipsntpcfg(sint8 *cmd_buf);

void	cu_esp_at_uart(sint8 *cmd_buf);
void	cu_esp_at_uart_cur(sint8 *cmd_buf);

void	cu_esp_at_restore(sint8 *cmd_buf);
void	cu_esp_at_ping(sint8 *cmd_buf);
void	cu_esp_at_cwautoconn(sint8 *cmd_buf);
void	cu_esp_at_cwstartsmart(sint8 *cmd_buf);
void	cu_esp_at_cwstopsmart(sint8 *cmd_buf);

void	cu_esp_at_bad_command(void);
void	cu_esp_at_debug(sint8 *cmd_buf);

/* ------------------------------------------------------------------------- */
/* Misc / persistence hooks                                                   */
/* ------------------------------------------------------------------------- */

void	cu_esp_reset_factory(void);
void	cu_esp_save_translink(void);
void	cu_esp_save_uart(void);
void	cu_esp_save_station_mac(void);
void	cu_esp_save_station_ip(void);
void	cu_esp_save_soft_ap_mac(void);
void	cu_esp_save_soft_ap_ip(void);
void	cu_esp_save_soft_ap_credentials(void);
void	cu_esp_save_dhcp(void);
void	cu_esp_save_mode(void);
void	cu_esp_save_wifi_credentials(void);
void	cu_esp_save_auto_conn(void);

/* ------------------------------------------------------------------------- */
/* Ping helpers                                                               */
/* ------------------------------------------------------------------------- */

sint16	ping_icmp_raw(const char *host, sint16 timeout_ms);
sint16	ping_udp(const char *host, sint16 timeout_ms);
sint16	ping_tcp(const char *host, sint16 timeout_ms);

uint16	cu_esp_checksum_oc(void *b, sint32 len);

/* ------------------------------------------------------------------------- */
/* Host serial passthrough                                                    */
/* ------------------------------------------------------------------------- */

sint32	cu_esp_host_serial_start(void);
void	cu_esp_host_serial_end(void);
void	cu_esp_host_serial_write(uint8 c);
uint8	cu_esp_host_serial_read(void);
auint	cu_esp_host_serial_rx_bytes_ready(void);

typedef struct{
	uint32 enabled;
	uint32 mode;
	uint32 state;
	sint32 last_error;
	sint32 last_send_error;
	sint32 last_recv_error;
	uint32 socket_valid;
	uint32 listener_valid;
	uint32 tx_queue_bytes;
	uint32 rx_queue_bytes;
	uint32 tx_queue_high_water;
	uint32 rx_queue_high_water;
	uint32 socket_rx_pending_bytes;
	uint32 avr_to_backend_bytes;
	uint32 backend_to_avr_bytes;
	uint32 socket_tx_bytes;
	uint32 socket_rx_bytes;
	uint32 send_calls;
	uint32 recv_calls;
	uint32 send_would_block;
	uint32 recv_would_block;
	uint32 send_errors;
	uint32 recv_errors;
	uint32 send_zero_returns;
	uint32 recv_zero_returns;
	uint32 connect_count;
	uint32 disconnect_count;
	uint32 state_change_count;
	uint32 service_calls;
	uint32 service_gap_over_50ms;
	uint32 service_gap_over_250ms;
	uint32 max_service_gap_ms;
	uint32 current_ms;
	uint32 connected_since_ms;
	uint32 last_state_change_ms;
	uint32 last_avr_tx_ms;
	uint32 last_avr_rx_ms;
	uint32 last_socket_tx_ms;
	uint32 last_socket_rx_ms;
	uint32 log_snapshot_count;
} cu_esp_tcp_serial_diag_t;

sint32	cu_esp_tcp_serial_start(void);
void	cu_esp_tcp_serial_end(void);
void	cu_esp_tcp_serial_write(uint8 c);
uint8	cu_esp_tcp_serial_read(void);
auint	cu_esp_tcp_serial_rx_bytes_ready(void);
void	cu_esp_tcp_serial_diag_get(cu_esp_tcp_serial_diag_t *out);
void	cu_esp_tcp_serial_diag_reset(void);
void	cu_esp_tcp_serial_diag_flush_log(void);
char const* cu_esp_tcp_serial_diag_get_log_path(void);
/* Service host endpoints without changing AVR-visible UART timing. */
typedef struct {
	uint32 enabled;
	uint32 latency_ms;
	uint32 jitter_ms;
	uint32 stall_every_ms;
	uint32 stall_ms;
	uint32 noise_ppm;
	uint32 drop_ppm;
} cu_esp_link_impair_t;

typedef struct {
	uint32 id;
	uint32 same_rom;
	uint32 searching;
	uint32 wants_us;
	char name[16];
	char ip[16];
} cu_esp_link_lan_peer_t;

void	cu_esp_endpoint_host_tick(void);
auint	cu_esp_get_tcp_serial_role(void);
void	cu_esp_link_set_rom_id(uint32 rom_id);
char const*	cu_esp_link_get_room(void);
void	cu_esp_link_set_room(char const* room);
char const*	cu_esp_link_get_relay_host(void);
void	cu_esp_link_set_relay_host(char const* host);
auint	cu_esp_link_get_relay_port(void);
void	cu_esp_link_set_relay_port(auint port);
void	cu_esp_link_get_impair(cu_esp_link_impair_t* out);
void	cu_esp_link_set_impair(cu_esp_link_impair_t const* in);
char const*	cu_esp_link_get_notice(void);
auint	cu_esp_link_lan_peers(cu_esp_link_lan_peer_t* out, auint max);
void	cu_esp_link_lan_choose(uint32 id);
auint	cu_esp_link_status_json(char* buf, auint cap);
auint	cu_esp_get_tcp_serial_state(void);
sint32	cu_esp_get_tcp_serial_last_error(void);
char const* cu_esp_get_tcp_serial_host(void);
void	cu_esp_set_tcp_serial_host(char const* host);
auint	cu_esp_get_tcp_serial_port(void);
void	cu_esp_set_tcp_serial_port(auint port);
boole	cu_esp_get_tcp_serial_auto_reconnect(void);
void	cu_esp_set_tcp_serial_auto_reconnect(boole enable);
auint	cu_esp_get_tcp_serial_mode(void);
void	cu_esp_set_tcp_serial_mode(auint mode);

#define CU_ESP_SERIAL_DISCONNECTED 0U
#define CU_ESP_SERIAL_ESP_MODULE   1U
#define CU_ESP_SERIAL_HOST_SERIAL  2U
#define CU_ESP_SERIAL_HOST_MIDI    3U
#define CU_ESP_SERIAL_VIRTUAL_MIDI 4U
#define CU_ESP_SERIAL_TCP_SERIAL   5U
#define CU_ESP_SERIAL_LOOPBACK     6U

#define CU_ESP_TCP_SERIAL_STATE_DISCONNECTED 0U
#define CU_ESP_TCP_SERIAL_STATE_CONNECTING   1U
#define CU_ESP_TCP_SERIAL_STATE_CONNECTED    2U
#define CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT   3U
#define CU_ESP_TCP_SERIAL_STATE_LISTENING    4U
#define CU_ESP_TCP_SERIAL_STATE_SEARCHING    5U   /* LAN mode: looking for a partner */
#define CU_ESP_TCP_SERIAL_STATE_WAITING_PEER 6U   /* INTERNET mode: in the room, other player not there yet */

#define CU_ESP_TCP_SERIAL_MODE_CLIENT 0U
#define CU_ESP_TCP_SERIAL_MODE_SERVER 1U
/* AUTO: try to connect to host:port; if nobody is listening, listen on port
** instead. Two instances both set to AUTO pair up without choosing roles. */
#define CU_ESP_TCP_SERIAL_MODE_AUTO   2U
/* INTERNET: meet in a room on the uzenet relay (no port forwarding). */
#define CU_ESP_TCP_SERIAL_MODE_INTERNET 3U
/* LAN: find another CUzeBox on the local network and pair automatically. */
#define CU_ESP_TCP_SERIAL_MODE_LAN    4U


#define CU_UART_PROFILE_FAST     0U
#define CU_UART_PROFILE_BALANCED 1U
#define CU_UART_PROFILE_DEBUG    2U

#define CU_ESP_VIRTUAL_MIDI_INSTRUMENT    1U
#define CU_ESP_VIRTUAL_MIDI_CONTROLLER    2U
#define CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL 3U

auint	cu_esp_get_serial_route(void);
void	cu_esp_set_serial_route(auint route);
auint	cu_esp_get_serial_esp_model(void);
void	cu_esp_set_serial_esp_model(auint model);
boole	cu_esp_get_softap_enabled(void);
void	cu_esp_set_softap_enabled(boole enable);
char const* cu_esp_get_host_serial_device_name(void);
void	cu_esp_set_host_serial_device_name(char const* name);
char const* cu_esp_get_host_midi_port_name(void);
void	cu_esp_set_host_midi_port_name(char const* name);
auint	cu_esp_get_virtual_midi_mode(void);
void	cu_esp_set_virtual_midi_mode(auint mode);
char const* cu_esp_get_virtual_midi_port_name(void);
void	cu_esp_set_virtual_midi_port_name(char const* name);
auint	cu_esp_get_uart_profile(void);
void	cu_esp_set_uart_profile(auint profile);

boole	cu_esp_serial_trace_get_enabled(void);
void	cu_esp_serial_trace_set_enabled(boole enable);
void	cu_esp_serial_trace_clear(void);
auint	cu_esp_serial_trace_get_event_count(void);
auint	cu_esp_serial_trace_get_line_count(void);
auint	cu_esp_serial_trace_get_capacity(void);
auint	cu_esp_serial_trace_get_drop_count(void);
boole	cu_esp_serial_trace_is_full(void);
void	cu_esp_serial_trace_get_uart_status(char* out, auint out_size);
void	cu_esp_serial_trace_format_line(auint idx, char* out, auint out_size);
boole	cu_esp_serial_trace_export(char const* path, boole raw);
void	cu_esp_serial_trace_note_backend_tx(uint8 val, auint cycle);
void	cu_esp_serial_trace_note_backend_rx(uint8 val, auint cycle);
void	cu_esp_serial_trace_get_counts(auint* avr_tx, auint* avr_rx, auint* backend_tx, auint* backend_rx, auint* cfg);

auint	cu_esp_get_time_seconds(void);

/* ------------------------------------------------------------------------- */
/* SYS commands                                                               */
/* ------------------------------------------------------------------------- */

void	cu_esp_at_sysmsg(sint8 *cmd_buf);
void	cu_esp_at_sysadc(sint8 *cmd_buf);
void	cu_esp_at_sysram(sint8 *cmd_buf);
void	cu_esp_at_sysgpiowrite(sint8 *cmd_buf);
void	cu_esp_at_sysgpioread(sint8 *cmd_buf);
void	cu_esp_at_sysiogetcfg(sint8 *cmd_buf);
void	cu_esp_at_sysiosetcfg(sint8 *cmd_buf);

/* ------------------------------------------------------------------------- */
/* Async DNS/connect/ping (host-side worker thread)                           */
/* ------------------------------------------------------------------------- */

uint32 cu_esp_now_ms(void);
void	cu_esp_net_tick(void);
uint32	cu_esp_net_rx_ready_mask(void);

auint	cu_esp_net_link_state(uint32 sock);
auint	cu_esp_net_link_take_connected(uint32 sock);
auint	cu_esp_net_link_take_error(uint32 sock, sint32 *err_out);

sint32	cu_esp_ping_async_start(const char *host, sint16 timeout_ms);
sint32	cu_esp_ping_async_poll(sint16 *rtt_ms);
sint32	cu_esp_dns_async_start(const char *host);
sint32	cu_esp_dns_async_start_ex(const char *host, uint8 ip_network);
sint32	cu_esp_dns_async_poll(uint32 *ipv4_be);
sint32	cu_esp_dns_async_poll_text(char *out, auint out_sz);
sint32	cu_esp_udp_send_resolve_async_start(uint32 sock, const char *host, uint16 port);

#define CU_ESP_LS_IDLE		0
#define CU_ESP_LS_DNS		1
#define CU_ESP_LS_CONNECT	2
#define CU_ESP_LS_DELAY		3
#define CU_ESP_LS_TLS		4
#define CU_ESP_LS_OPEN		5
#define CU_ESP_LS_ERROR		6

/* ------------------------------------------------------------------------- */
/* Virtual LAN overlay / SoftAP helpers                                      */
/* ------------------------------------------------------------------------- */

typedef struct{
	const char	*ssid;
	const char	*bssid;
	sint32		channel;
	sint32		scan_type;
	sint32		scan_time_min;
	sint32		scan_time_max;
	sint32		rssi_filter;
	uint32		authmask;
	uint32		print_mask;
	uint8		sort_enable;
}cu_esp_cwlap_query_t;

void	cu_esp_lan_init(cu_state_esp_t *es);
void	cu_esp_lan_shutdown(cu_state_esp_t *es);
void	cu_esp_lan_tick(cu_state_esp_t *es, uint32 now_ms);

void	cu_esp_lan_on_cwmode_change(cu_state_esp_t *es);
void	cu_esp_lan_on_softap_change(cu_state_esp_t *es);
void	cu_esp_lan_on_station_change(cu_state_esp_t *es);

uint32	cu_esp_lan_cwlap_emit(cu_state_esp_t *es);
void	cu_esp_ap_scan_emit(cu_state_esp_t *es, const cu_esp_cwlap_query_t *q);

sint32	cu_esp_ap_find_discovered(cu_state_esp_t *es,
	const char *ssid,
	const char *bssid);

sint32	cu_esp_ap_begin_join(cu_state_esp_t *es,
	const char *ssid,
	const char *pwd,
	const char *bssid);
sint32	cu_esp_ap_start_join(cu_state_esp_t *es,
	const char *ssid,
	const char *pwd,
	const char *bssid);
void	cu_esp_ap_join_tick(cu_state_esp_t *es, auint tdelta);

sint32	cu_esp_ap_query_join(cu_state_esp_t *es, sint8 *out, auint out_sz);
sint32	cu_esp_ap_query_station_mac(cu_state_esp_t *es, sint8 *out, auint out_sz);
sint32	cu_esp_ap_set_station_mac(cu_state_esp_t *es, const sint8 *mac);
sint32	cu_esp_ap_query_station_ip(cu_state_esp_t *es, sint8 *out, auint out_sz);
sint32	cu_esp_ap_set_station_ip(cu_state_esp_t *es, const sint8 *ip, const sint8 *gw, const sint8 *nm);
sint32	cu_esp_ap_query_softap_mac(cu_state_esp_t *es, sint8 *out, auint out_sz);
sint32	cu_esp_ap_set_softap_mac(cu_state_esp_t *es, const sint8 *mac);
sint32	cu_esp_ap_query_softap_ip(cu_state_esp_t *es, sint8 *out, auint out_sz);
sint32	cu_esp_ap_set_softap_ip(cu_state_esp_t *es, const sint8 *ip, const sint8 *gw, const sint8 *nm);
uint32	cu_esp_ap_emit_station_list(cu_state_esp_t *es);
void	cu_esp_ap_disconnect(cu_state_esp_t *es, uint8 close_sockets);
void	cu_esp_ap_leave(cu_state_esp_t *es);
void	cu_esp_ap_join_complete(cu_state_esp_t *es, sint32 ok);

void	cu_esp_ap_reset_leases(cu_state_esp_t *es);
sint32	cu_esp_ap_allocate_lease(cu_state_esp_t *es,
	const uint8 sta_mac[6],
	uint32 *out_ip_be);
void	cu_esp_ap_release_lease(cu_state_esp_t *es, const uint8 sta_mac[6]);
void	cu_esp_ap_age_leases(cu_state_esp_t *es, uint32 now_ms);

uint8	cu_esp_lan_virtual_link_present(cu_state_esp_t *es, uint32 sock);
uint8	cu_esp_lan_virtual_link_open(cu_state_esp_t *es, uint32 sock);
sint32	cu_esp_lan_virtual_open(cu_state_esp_t *es, uint32 sock, const char *host, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode);
sint32	cu_esp_lan_virtual_send(cu_state_esp_t *es, uint32 sock, const uint8 *buf, uint16 len);
sint32	cu_esp_lan_virtual_recv(cu_state_esp_t *es, uint32 sock, uint8 *buf, uint16 len);
void	cu_esp_lan_virtual_close(cu_state_esp_t *es, uint32 sock, uint8 notify_remote);

/* Native MQTT 3.1.1 host backend. */
sint32	cu_esp_mqtt_connect(const char *host, uint16 port, uint8 scheme, const char *path, uint8 reconnect);
void	cu_esp_mqtt_disconnect(void);
uint8	cu_esp_mqtt_state(void);
sint32	cu_esp_mqtt_publish(const char *topic, const uint8 *data, uint32 len, uint8 qos, uint8 retain);
sint32	cu_esp_mqtt_subscribe(const char *topic, uint8 qos);
sint32	cu_esp_mqtt_unsubscribe(const char *topic);
uint8	cu_esp_mqtt_subscription_count(void);
sint32	cu_esp_mqtt_subscription_get(uint8 idx, char *topic, auint topic_sz, uint8 *qos);
sint32	cu_esp_mqtt_take_event(uint8 *kind, char *topic, auint topic_sz, uint8 *data, auint data_sz, uint32 *data_len);
void	cu_esp_mqtt_tick(void);

#endif /* CU_ESP_H */
