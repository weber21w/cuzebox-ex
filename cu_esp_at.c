/*
 *  ESP8266 peripheral AT/System
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

#include "cu_esp.h"
#include "cu_uart.h"
#include "midi.h"
#include "cu_types.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
#else /* POSIX system (Linux, OSX, etc) */
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <sys/ioctl.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
	#include <termios.h>
	#include <fcntl.h>
	#include <sys/time.h>
#endif

void cu_esp_apply_serial_route_live(void);

#ifdef __EMSCRIPTEN__
	#include <emscripten.h>
	#include <emscripten/websocket.h>
	#include <emscripten/threading.h>
	#include <emscripten/posix_socket.h>
	static EMSCRIPTEN_WEBSOCKET_T bridgeSocket = 0;
#endif

#if !defined(ENABLE_ESP)

static cu_state_esp_t esp_state;

#ifndef CU_ESP_AT_TRACE
#define CU_ESP_AT_TRACE 0
#endif
#if CU_ESP_AT_TRACE
#define CU_ESP_AT_LOG(...) print_trace(__VA_ARGS__)
#else
#define CU_ESP_AT_LOG(...) ((void)0)
#endif

cu_state_esp_t* cu_esp_get_state(void){ return &esp_state; }

void cu_esp_reset(auint cycle){ (void)cycle; memset(&esp_state, 0, sizeof(esp_state)); }
void cu_esp_send(auint data, auint cycle){ (void)data; (void)cycle; }
auint cu_esp_recv(auint cycle){ (void)cycle; return 0xFFu; }
void cu_esp_clear_at_command(void){}
void cu_esp_reset_pin(uint8 state, auint cycle){ (void)state; (void)cycle; }

void cu_esp_reset_network(void){}
auint cu_esp_verify_mac_string(auint pos){ (void)pos; return 0u; }
void cu_esp_external_write(uint8 val){ (void)val; }
void cu_esp_external_read(void){}
auint cu_esp_net_last_error(void){ return 0u; }
sint32 cu_esp_net_connect(sint8 *hostname, uint32 sock, uint32 port, uint32 type){ (void)hostname; (void)sock; (void)port; (void)type; return ESP_SOCKET_ERROR; }
sint32 cu_esp_net_connect_ex(sint8 *hostname, uint32 sock, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode){ (void)hostname; (void)sock; (void)port; (void)type; (void)local_port; (void)udp_mode; return ESP_SOCKET_ERROR; }
sint32 cu_esp_net_send(uint32 sock, sint8 *buf, sint32 len, sint32 flags){ (void)sock; (void)buf; (void)len; (void)flags; return ESP_SOCKET_ERROR; }
sint32 cu_esp_listen(uint32 port){ (void)port; return ESP_SOCKET_ERROR; }
uint8 cu_esp_ssl_link_busy(uint32 except_sock){ (void)except_sock; return 0u; }
sint32 cu_esp_net_recv(uint32 sock, sint8 *buf, sint32 len, sint32 flags){ (void)sock; (void)buf; (void)len; (void)flags; return ESP_SOCKET_ERROR; }
void cu_esp_net_send_unvarnished(sint8 *buf, auint len){ (void)buf; (void)len; }
auint cu_esp_atoi(char *str){ return (auint)atoi(str ? str : "0"); }
sint32 cu_esp_get_last_error(void){ return 0; }
sint32 cu_esp_init_sockets(void){ return 0; }
void cu_esp_net_cleanup(void){}
void cu_esp_close_socket(uint32 sock){ (void)sock; }

void cu_esp_update(void){}
void cu_esp_txi(sint32 i){ (void)i; }
void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txl(const char *s, auint len){ (void)s; (void)len; }
void cu_esp_txp_ok(void){}
void cu_esp_txp_error(void){}
void cu_esp_at_txp_bad_command(void){}
void cu_esp_timed_stall(auint cycles){ (void)cycles; }
auint cu_esp_update_timer_counts(auint cycle){ return cycle; }
void cu_esp_service_host_state(void){}
sint32 cu_esp_process_ipd(void){ return 0; }
void cu_esp_save_config(void){}
auint cu_esp_load_config(void){ return 0u; }
auint cu_esp_reload_config_runtime(void){ return 0u; }
void cu_esp_runtime_shutdown(void){}
boole cu_esp_config_live_ready(void){ return FALSE; }
boole cu_esp_append_live_config(FILE *f, boole with_comments){ (void)f; (void)with_comments; return FALSE; }
boole cu_esp_append_default_config(FILE *f, boole with_comments){ (void)f; (void)with_comments; return FALSE; }
boole cu_esp_append_config_from_path(FILE *f, const char *path, boole with_comments){ (void)f; (void)path; (void)with_comments; return FALSE; }
void cu_esp_process_at(sint8 *cmd_buf){ (void)cmd_buf; }

void cu_esp_reset_factory(void){}
void cu_esp_save_translink(void){}
void cu_esp_save_uart(void){}
void cu_esp_save_station_mac(void){}
void cu_esp_save_station_ip(void){}
void cu_esp_save_soft_ap_mac(void){}
void cu_esp_save_soft_ap_ip(void){}
void cu_esp_save_soft_ap_credentials(void){}
void cu_esp_save_dhcp(void){}
void cu_esp_save_mode(void){}
void cu_esp_save_wifi_credentials(void){}
void cu_esp_save_auto_conn(void){}

sint16 ping_icmp_raw(const char *host, sint16 timeout_ms){ (void)host; (void)timeout_ms; return -1; }
sint16 ping_udp(const char *host, sint16 timeout_ms){ (void)host; (void)timeout_ms; return -1; }
sint16 ping_tcp(const char *host, sint16 timeout_ms){ (void)host; (void)timeout_ms; return -1; }
uint16 cu_esp_checksum_oc(void *b, sint32 len){ (void)b; (void)len; return 0u; }

sint32 cu_esp_host_serial_start(void){ return ESP_SERIAL_OPEN_ERROR; }
void cu_esp_host_serial_end(void){}
void cu_esp_host_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_host_serial_read(void){ return 0u; }
auint cu_esp_host_serial_rx_bytes_ready(void){ return 0u; }
sint32 cu_esp_tcp_serial_start(void){ return ESP_SERIAL_OPEN_ERROR; }
void cu_esp_tcp_serial_end(void){}
void cu_esp_tcp_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_tcp_serial_read(void){ return 0u; }
auint cu_esp_tcp_serial_rx_bytes_ready(void){ return 0u; }
void cu_esp_tcp_serial_diag_get(cu_esp_tcp_serial_diag_t *out){ if(out != NULL){ memset(out, 0, sizeof(*out)); } }
void cu_esp_tcp_serial_diag_reset(void){}
void cu_esp_tcp_serial_diag_flush_log(void){}
char const* cu_esp_tcp_serial_diag_get_log_path(void){ return ""; }
void cu_esp_endpoint_host_tick(void){}
auint cu_esp_get_tcp_serial_role(void){ return 0u; }
void cu_esp_link_set_rom_id(uint32 rom_id){ (void)rom_id; }
char const* cu_esp_link_get_room(void){ return ""; }
void cu_esp_link_set_room(char const* room){ (void)room; }
char const* cu_esp_link_get_relay_host(void){ return ""; }
void cu_esp_link_set_relay_host(char const* host){ (void)host; }
auint cu_esp_link_get_relay_port(void){ return 0u; }
void cu_esp_link_set_relay_port(auint port){ (void)port; }
void cu_esp_link_get_impair(cu_esp_link_impair_t* out){ if(out != NULL){ memset(out, 0, sizeof(*out)); } }
void cu_esp_link_set_impair(cu_esp_link_impair_t const* in){ (void)in; }
char const* cu_esp_link_get_notice(void){ return ""; }
auint cu_esp_link_lan_peers(cu_esp_link_lan_peer_t* out, auint max){ (void)out; (void)max; return 0u; }
void cu_esp_link_lan_choose(uint32 id){ (void)id; }
auint cu_esp_link_status_json(char* buf, auint cap){ if(buf != NULL && cap != 0u){ buf[0] = 0; } return 0u; }
auint cu_esp_get_tcp_serial_state(void){ return CU_ESP_TCP_SERIAL_STATE_DISCONNECTED; }
sint32 cu_esp_get_tcp_serial_last_error(void){ return 0; }
char const* cu_esp_get_tcp_serial_host(void){ return ""; }
void cu_esp_set_tcp_serial_host(char const* host){ (void)host; }
auint cu_esp_get_tcp_serial_port(void){ return 0u; }
void cu_esp_set_tcp_serial_port(auint port){ (void)port; }
boole cu_esp_get_tcp_serial_auto_reconnect(void){ return FALSE; }
void cu_esp_set_tcp_serial_auto_reconnect(boole enable){ (void)enable; }
auint cu_esp_get_serial_route(void){ return CU_ESP_SERIAL_DISCONNECTED; }
void cu_esp_set_serial_route(auint route){ (void)route; }
auint cu_esp_get_serial_esp_model(void){ return 3u; }
void cu_esp_set_serial_esp_model(auint model){ (void)model; }
auint cu_esp_get_at_firmware_profile(void){ return CU_ESP_AT_FW_LEGACY_17; }
void cu_esp_set_at_firmware_profile(auint profile){ (void)profile; }
auint cu_esp_get_tcp_serial_mode(void){ return CU_ESP_TCP_SERIAL_MODE_AUTO; }
void cu_esp_set_tcp_serial_mode(auint mode){ (void)mode; }
boole cu_esp_get_softap_enabled(void){ return TRUE; }
void cu_esp_set_softap_enabled(boole enable){ (void)enable; }
char const* cu_esp_get_host_serial_device_name(void){ return ""; }
void cu_esp_set_host_serial_device_name(char const* name){ (void)name; }
char const* cu_esp_get_host_midi_port_name(void){ return ""; }
void cu_esp_set_host_midi_port_name(char const* name){ (void)name; }
auint cu_esp_get_virtual_midi_mode(void){ return CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL; }
void cu_esp_set_virtual_midi_mode(auint mode){ (void)mode; }
char const* cu_esp_get_virtual_midi_port_name(void){ return ""; }
void cu_esp_set_virtual_midi_port_name(char const* name){ (void)name; }
auint cu_esp_get_uart_profile(void){ return CU_UART_PROFILE_FAST; }
void cu_esp_set_uart_profile(auint profile){ (void)profile; }
auint cu_esp_get_time_seconds(void){ return (auint)time(NULL); }

void cu_esp_at(void){}
void cu_esp_at_bad_command(void){}
void cu_esp_at_debug(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysmsg(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysadc(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysram(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysgpiowrite(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysgpioread(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysiogetcfg(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysiosetcfg(sint8 *cmd_buf){ (void)cmd_buf; }

uint32 cu_esp_now_ms(void){ return 0u; }
void cu_esp_net_tick(void){}
uint32 cu_esp_net_rx_ready_mask(void){ return 0u; }
auint cu_esp_net_link_state(uint32 sock){ (void)sock; return CU_ESP_LS_IDLE; }
auint cu_esp_net_link_take_connected(uint32 sock){ (void)sock; return 0u; }
auint cu_esp_net_link_take_error(uint32 sock, sint32 *err_out){ (void)sock; if(err_out) *err_out = 0; return 0u; }
sint32 cu_esp_ping_async_start(const char *host, sint16 timeout_ms){ (void)host; (void)timeout_ms; return -1; }
sint32 cu_esp_ping_async_poll(sint16 *rtt_ms){ if(rtt_ms) *rtt_ms = -1; return -1; }
sint32 cu_esp_dns_async_start(const char *host){ (void)host; return -1; }
sint32 cu_esp_dns_async_poll(uint32 *ipv4_be){ if(ipv4_be) *ipv4_be = 0u; return -1; }
sint32 cu_esp_udp_send_resolve_async_start(uint32 sock, const char *host, uint16 port){ (void)sock; (void)host; (void)port; return -1; }

#else

#ifndef CU_ARRLEN
	#define CU_ARRLEN(a)	(sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ESP_CIPBUF_MAX_LINKS
	#define ESP_CIPBUF_MAX_LINKS	ESP_MAX_LINKS
#endif

/* ------------------------------------------------------------------------- */
/* Escape guard time (real ESP uses ~1s before and after "+++")               */
/* ------------------------------------------------------------------------- */
#define CU_ESP_ESCAPE_GUARD_CYCLES	(ESP_UZEBOX_CORE_FREQUENCY)	/* 1 second */

/* ------------------------------------------------------------------------- */
/* Module state                                                               */
/* ------------------------------------------------------------------------- */
cu_state_esp_t esp_state;

#ifndef CU_ESP_AT_TRACE
#define CU_ESP_AT_TRACE 0
#endif
#if CU_ESP_AT_TRACE
#define CU_ESP_AT_LOG(...) print_trace(__VA_ARGS__)
#else
#define CU_ESP_AT_LOG(...) ((void)0)
#endif

/* Collapsed AT/runtime config now lives in esp_state. */
/* If you already define cu_esp_get_state() elsewhere, delete this one. */
cu_state_esp_t* cu_esp_get_state(void){
	return &esp_state;
}

/* ------------------------------------------------------------------------- */
/* Small helpers (keep local to this unit)                                    */
/* ------------------------------------------------------------------------- */
static inline const sint8* cu_esp_skip_ws(const sint8 *p){
	while(*p == ' ' || *p == '\t')
		p++;
	return p;
}

static inline const sint8* cu_esp_skip_ws8(const sint8 *p){
	return cu_esp_skip_ws(p);
}

static inline int cu_esp_expect_char(const sint8 **pp, char c){
	const sint8 *p = cu_esp_skip_ws(*pp);
	if(*p != (sint8)c)
		return 0;
	*pp = p + 1;
	return 1;
}

static inline int cu_esp_parse_comma(const sint8 **pp){
	const sint8 *p = cu_esp_skip_ws(*pp);
	if(*p != ',')
		return 0;
	*pp = cu_esp_skip_ws(p + 1);
	return 1;
}

static inline int cu_esp_expect_crlf(const sint8 *p){
	p = cu_esp_skip_ws(p);
	return (p[0] == '\r' && p[1] == '\n');
}

static int cu_esp_parse_u32_8(const sint8 *p, auint *out, const sint8 **endp){
	auint v = 0;
	auint any = 0;

	p = cu_esp_skip_ws8(p);

	while(*p >= '0' && *p <= '9'){
		any = 1;
		v = (v * 10u) + (auint)(*p - '0');
		p++;
	}
	if(!any)
		return 0;

	*out = v;
	if(endp)
		*endp = p;
	return 1;
}

static int cu_esp_parse_u16_dec(const sint8 **pp, int *out){
	int v = 0;
	const sint8 *p = cu_esp_skip_ws(*pp);

	if(*p < '0' || *p > '9')
		return 0;

	while(*p >= '0' && *p <= '9'){
		v = (v * 10) + (int)(*p - '0');
		if(v > 65535){
			v = 65535;
			while(*p >= '0' && *p <= '9')
				p++;
			break;
		}
		p++;
	}

	*pp = p;
	*out = v;
	return 1;
}

static int cu_esp_parse_s32_dec(const sint8 **pp, sint32 *out){
	const sint8 *p = cu_esp_skip_ws(*pp);
	sint32 neg = 0;
	sint32 v = 0;
	sint32 any = 0;

	if(*p == '-'){
		neg = 1;
		p++;
	}

	while(*p >= '0' && *p <= '9'){
		v = (v * 10) + (sint32)(*p - '0');
		p++;
		any = 1;
	}

	if(!any)
		return 0;

	*out = neg ? -v : v;
	*pp = cu_esp_skip_ws(p);
	return 1;
}

static uint8 cu_esp_at23_enabled(void);
static void cu_esp_at23_store_if_enabled(void);

static int cu_esp_parse_quoted_str(const sint8 **pp, sint8 *dst, auint dst_sz, auint allow_empty){
	const sint8 *p = cu_esp_skip_ws(*pp);
	auint i = 0;

	if(dst_sz == 0 || *p != '"')
		return 0;
	p++;

	while(*p && *p != '"'){
		sint8 c = *p++;
		/* ESP-AT quoted fields use backslash to escape ',', '"' and '\\'.
		 * Accept a generic escaped byte as well; this matches the firmware's
		 * permissive command lexer and is important for SSIDs/passwords. */
		if(c == '\\'){
			if(!*p) return 0;
			c = *p++;
		}
		if(i + 1u >= dst_sz)
			return 0;
		dst[i++] = c;
	}

	if(*p != '"')
		return 0;

	if(!allow_empty && i == 0)
		return 0;

	dst[i] = 0;
	p++; /* closing quote */

	*pp = p;
	return 1;
}

/* allow-empty variant used in a few places */
static int cu_esp_parse_quoted_str0(const sint8 **pp, sint8 *dst, auint dst_sz){
	return cu_esp_parse_quoted_str(pp, dst, dst_sz, 1);
}

static int cu_esp_parse_quoted_mac(const sint8 **pp, char out[18]){
	const sint8 *p = cu_esp_skip_ws(*pp);
	int i;

	if(*p != '"')
		return 0;
	p++;

	for(i = 0; i < 17; i++){
		char c = (char)*p++;
		if(i == 2 || i == 5 || i == 8 || i == 11 || i == 14){
			if(c != ':')
				return 0;
			out[i] = c;
			continue;
		}
		if(!isxdigit((unsigned char)c))
			return 0;
		if(c >= 'a' && c <= 'f')
			c = (char)(c - ('a' - 'A'));
		out[i] = c;
	}
	out[17] = 0;

	if(*p != '"')
		return 0;
	p++;

	*pp = p;
	return 1;
}

static inline int cu_esp_parse_bool01(const sint8 **pp, int *out){
	int v = 0;
	if(!cu_esp_parse_u16_dec(pp, &v))
		return 0;
	if(v != 0 && v != 1)
		return 0;
	*out = v;
	return 1;
}

static int cu_esp_ssl_parse_target(const sint8 **pp, uint8 *target_out){
	/* ESP-AT SSL setters omit link ID in single-connection mode.  In MUX
	 * mode the prefix is mandatory; ID 5 means all physical link slots. */
	const sint8 *p=*pp; int target=0;
	if(!(esp_state.state&ESP_MUX)){*target_out=0u;return 1;}
	if(!cu_esp_parse_u16_dec(&p,&target)||target<0||target>(int)ESP_MAX_LINKS||!cu_esp_expect_char(&p,','))return 0;
	*target_out=(uint8)target;*pp=p;return 1;
}
static uint8 cu_esp_ssl_query_links(void){return (esp_state.state&ESP_MUX)?(uint8)ESP_MAX_LINKS:1u;}
static uint8 cu_esp_ssl_target_first(uint8 target){return target==ESP_MAX_LINKS?0u:target;}
static uint8 cu_esp_ssl_target_last(uint8 target){return target==ESP_MAX_LINKS?(uint8)(ESP_MAX_LINKS-1u):target;}

/* ------------------------------------------------------------------------- */
/* MUX-aware CONNECT/CLOSED notifications (ESP8266 style)                     */
/* ------------------------------------------------------------------------- */
static void cu_esp_tx_evt_connect(uint8 link){
	if(esp_state.state & ESP_MUX){
		cu_esp_txi((sint32)link);
		cu_esp_txp(",CONNECT\r\n");
	}else{
		cu_esp_txp("CONNECT\r\n");
	}
}

static void cu_esp_tx_evt_closed(uint8 link){
	if(esp_state.state & ESP_MUX){
		cu_esp_txi((sint32)link);
		cu_esp_txp(",CLOSED\r\n");
	}else{
		cu_esp_txp("CLOSED\r\n");
	}
}

static void cu_esp_begin_send_prompt(uint8 conn, uint32 len){
	esp_state.send_to_socket = (uint32)conn;
	esp_state.rx_await_bytes = len;
	esp_state.remaining_send_bytes = (auint)len;
	esp_state.write_buf_pos_in = 0;
	esp_state.user_input_mode = ESP_USER_MODE_SEND;
	cu_esp_txp("OK\r\n>");
}

static uint8 cu_esp_at_is_busy(void){
	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
		if(esp_state.link_wait_ok[i])
			return 1u;
	}
	if(esp_state.wifi_join_pending || esp_state.dns_pending || esp_state.ping_pending || esp_state.send_prep_pending || esp_state.udp_send_resolve_pending)
		return 1u;
	return 0u;
}

void cu_esp_service_host_state(void){
	sint32 err = 0;

	cu_esp_net_tick();

	{ uint8 mk=0,md[2048];char mt[CU_ESP_MQTT_TOPIC_MAX+1u];uint32 ml=0;
		if(cu_esp_mqtt_take_event(&mk,mt,sizeof(mt),md,sizeof(md),&ml)>0){
			if(mk==CU_ESP_MQTT_EVT_CONNECTED){cu_esp_txp("+MQTTCONNECTED:0,");cu_esp_txi(esp_state.mqtt_scheme?esp_state.mqtt_scheme:1);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.mqtt_host);cu_esp_txp("\",");cu_esp_txi(esp_state.mqtt_port);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.mqtt_path);cu_esp_txp("\",");cu_esp_txi(esp_state.mqtt_reconnect);cu_esp_txp("\r\n");}
			else if(mk==CU_ESP_MQTT_EVT_DISCONNECTED)cu_esp_txp("+MQTTDISCONNECTED:0\r\n");
			else if(mk==CU_ESP_MQTT_EVT_MESSAGE){cu_esp_txp("+MQTTSUBRECV:0,\"");cu_esp_txp(mt);cu_esp_txp("\",");cu_esp_txi((sint32)ml);cu_esp_txp(",");if(ml)cu_esp_txl((char*)md,ml);cu_esp_txp("\r\n");}
		}
	}

	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
		if(cu_esp_net_link_take_connected(i)){
			cu_esp_tx_evt_connect((uint8)i);
			if(esp_state.link_wait_ok[i]){
				esp_state.link_wait_ok[i] = 0u;
				cu_esp_txp_ok();
			}
		}
		if(cu_esp_net_link_take_error(i, &err)){
			(void)err;
			if(esp_state.link_wait_ok[i]){
				esp_state.link_wait_ok[i] = 0u;
				cu_esp_txp_error();
			}else{
				cu_esp_tx_evt_closed((uint8)i);
			}
		}
	}

	{
		sint16 prtt = -1;
		sint32 pr = cu_esp_ping_async_poll(&prtt);
		if(pr > 0){
			cu_esp_txp("+PING:");
			cu_esp_txi((sint32)prtt);
			cu_esp_txp("\r\n");
			cu_esp_txp_ok();
		}else if(pr < 0){
			cu_esp_txp("+PING:TIMEOUT\r\n");
			cu_esp_txp_error();
		}
	}

	{
		char dip[64]; sint32 dr=cu_esp_dns_async_poll_text(dip,sizeof(dip));
		if(dr>0){cu_esp_txp("+CIPDOMAIN:");cu_esp_txp(dip);cu_esp_txp("\r\n");cu_esp_txp_ok();}
		else if(dr<0) cu_esp_txp_error();
	}

	if(esp_state.send_prep_pending && esp_state.udp_send_resolve_ready){
		if(esp_state.udp_send_resolve_ready_ms && cu_esp_now_ms() < esp_state.udp_send_resolve_ready_ms)
			return;

		esp_state.udp_send_resolve_ready = 0u;

		if(esp_state.udp_send_resolve_err == 0 &&
		   esp_state.udp_send_resolve_sock < ESP_MAX_LINKS &&
		   esp_state.udp_send_resolve_sock == esp_state.send_to_socket){
			memset(&esp_state.udp_send_info[esp_state.udp_send_resolve_sock], 0,
			       sizeof(esp_state.udp_send_info[esp_state.udp_send_resolve_sock]));
			memcpy(&esp_state.udp_send_info[esp_state.udp_send_resolve_sock],&esp_state.udp_send_resolve_info,sizeof(esp_state.udp_send_info[esp_state.udp_send_resolve_sock]));
			if(esp_state.udp_resolve_ipv6_valid){uint8 u=esp_state.udp_send_resolve_sock;esp_state.udp_send_ipv6_valid[u]=1u;memcpy(esp_state.udp_send_ipv6[u],esp_state.udp_resolve_ipv6,16);esp_state.udp_send_ipv6_port[u]=esp_state.udp_resolve_ipv6_port;}
			esp_state.udp_send_override[esp_state.udp_send_resolve_sock] = 1u;
			cu_esp_begin_send_prompt(esp_state.udp_send_resolve_sock, esp_state.send_prep_len);
		}else{
			cu_esp_txp_error();
		}

		esp_state.send_prep_pending = 0u;
		esp_state.send_prep_len = 0u;
		memset(&esp_state.udp_send_resolve_info, 0, sizeof(esp_state.udp_send_resolve_info));
	}
}

/* "bad command" helper (some code calls cu_esp_at_txp_bad_command()) */
void cu_esp_at_txp_bad_command(void){
	cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* Time                                                                       */
/* ------------------------------------------------------------------------- */
auint cu_esp_get_time_seconds(void){
	return (auint)time(NULL);
}


/* ------------------------------------------------------------------------- */
/* CIPBUF ring buffer helpers                                                 */
/* ------------------------------------------------------------------------- */
static void cu_esp_cipbuf_clear_link(cu_state_esp_t *es, uint8 link){
	if(link >= ESP_MAX_LINKS) return;
	es->cipbuf_in[link]	 = 0;
	es->cipbuf_out[link] = 0;
	es->cipbuf_len[link] = 0;
}

static void cu_esp_cipbuf_clear_all(cu_state_esp_t *es){
	for(uint8 i = 0; i < ESP_MAX_LINKS; i++)
		cu_esp_cipbuf_clear_link(es, i);
}

/* Drop-oldest policy if full; returns bytes accepted (always == len if link valid) */
static uint16 cu_esp_cipbuf_push(cu_state_esp_t *es, uint8 link, const uint8 *src, uint16 len){
	uint16 in_len = len;

	if(link >= ESP_MAX_LINKS)
		return 0;

	while(len--){
		if(es->cipbuf_len[link] >= ESP_CIPBUF_RX_CAP){
			es->cipbuf_out[link]++;
			if(es->cipbuf_out[link] >= ESP_CIPBUF_RX_CAP) es->cipbuf_out[link] = 0;
			es->cipbuf_len[link]--;
		}

		es->cipbuf_rx[link][es->cipbuf_in[link]] = *src++;
		es->cipbuf_in[link]++;
		if(es->cipbuf_in[link] >= ESP_CIPBUF_RX_CAP) es->cipbuf_in[link] = 0;
		es->cipbuf_len[link]++;
	}

	return in_len;
}

static uint16 cu_esp_cipbuf_avail(cu_state_esp_t *es, uint8 link){
	if(link >= ESP_MAX_LINKS) return 0;
	return es->cipbuf_len[link];
}

static uint16 cu_esp_cipbuf_pop(cu_state_esp_t *es, uint8 link, uint8 *dst, uint16 want){
	uint16 got = 0;

	if(link >= ESP_MAX_LINKS)
		return 0;

	while(got < want && es->cipbuf_len[link]){
		dst[got++] = es->cipbuf_rx[link][es->cipbuf_out[link]];

		es->cipbuf_out[link]++;
		if(es->cipbuf_out[link] >= ESP_CIPBUF_RX_CAP)
			es->cipbuf_out[link] = 0;

		es->cipbuf_len[link]--;
	}

	return got;
}

/* ------------------------------------------------------------------------- */
/* Timer update (delays + transparent flush)                                  */
/* ------------------------------------------------------------------------- */
auint cu_esp_update_timer_counts(auint cycle){

	if(cu_uart_route_is_host_serial() || cu_uart_route_is_tcp_serial() || cu_uart_route_is_midi())
		return 0;
	if(cu_uart_runtime_is_inert()){
		esp_state.last_cycle_tick = cycle;
		return esp_state.read_buf_pos_in;
	}

	auint tdelta = WRAP32(cycle - esp_state.last_cycle_tick);
	esp_state.last_cycle_tick = cycle;

	cu_esp_ap_join_tick(&esp_state, tdelta);

	/* Transparent mode flush / escape decision */
	if(esp_state.user_input_mode == ESP_USER_MODE_UNVARNISHED && esp_state.unvarnished_bytes){

		if(cycle < esp_state.unvarnished_end_cycle &&
		   (esp_state.unvarnished_end_cycle - cycle) > 100000UL){
			esp_state.unvarnished_end_cycle = cycle;
		}

		if(cycle >= esp_state.unvarnished_end_cycle){
			if(esp_state.num_plus == 3 && esp_state.unvarnished_bytes == 3){
				if(esp_state.write_buf[0] == '+' && esp_state.write_buf[1] == '+' && esp_state.write_buf[2] == '+'){
					print_message("ESP: +++ escape from transparent mode\n");
					esp_state.user_input_mode = ESP_USER_MODE_AT;
					esp_state.write_buf_pos_in = 0;
					esp_state.unvarnished_bytes = 0;
					esp_state.unvarnished_end_cycle = 0;
					esp_state.num_plus = 0;
					return esp_state.read_buf_pos_in;
				}
			}

			esp_state.unvarnished_end_cycle = WRAP32(cycle + ESP_UNVARNISHED_DELAY);
			cu_esp_net_send_unvarnished(esp_state.write_buf, esp_state.unvarnished_bytes);

			esp_state.write_buf_pos_in = 0;
			esp_state.unvarnished_bytes = 0;
			esp_state.num_plus = 0;
		}
	}

	for(uint8 i = 0; i < (uint8)CU_ARRLEN(esp_state.delay_pos); i++){
		if(!esp_state.delay_len[i])
			continue;
		if(esp_state.delay_len[i] <= tdelta)
			esp_state.delay_len[i] = 0;
		else
			esp_state.delay_len[i] -= tdelta;
	}

	for(uint8 i = 0; i < (uint8)CU_ARRLEN(esp_state.delay_pos); i++){
		if(esp_state.delay_len[i] && esp_state.delay_pos[i] == esp_state.read_buf_pos_out)
			return esp_state.read_buf_pos_out;
	}

	return esp_state.read_buf_pos_in;
}

auint cu_esp_atoi(char *str){
	return (auint)atoi(str);
}

/* ------------------------------------------------------------------------- */
/* Reset pin                                                                   */
/* ------------------------------------------------------------------------- */
void cu_esp_reset_pin(uint8 state, auint cycle){
	/*
	 * Raw UART routes (TCP SERIAL / HOST SERIAL / LOOPBACK / MIDI serial paths)
	 * are just external TX/RX links. They must not be coupled to the Uzebox-side
	 * ESP reset pin wiring on PORTD.3, otherwise ordinary ROM GPIO activity can
	 * tear down and recreate the UART backend mid-game.
	 */
	if(!cu_esp_config_live_ready())
		cu_esp_load_config();
	if(esp_state.serial_route != CU_ESP_SERIAL_ESP_MODULE)
		return;

	if(state && esp_state.reset_pin)
		return;

	if(!state){
		if(esp_state.reset_pin){
			esp_state.reset_pin = 0;
			esp_state.last_cycle_tick = cycle;
			cu_esp_reset_uart();
			if(esp_state.serial_route == CU_ESP_SERIAL_ESP_MODULE){
				cu_esp_runtime_shutdown();
				print_message("ESP: Shutdown\n");
			}
		}
	}else if(!esp_state.reset_pin){
		esp_state.reset_pin = 1;

		cu_esp_load_config();
		cu_esp_reset_uart();

		/*
		 * Raw UART endpoint routes are not tied to the emulated ESP reset pin.
		 * They represent a generic external serial device hanging off TX/RX, so
		 * bring them up immediately even if the ROM never drives the ESP reset
		 * line. This keeps HOST SERIAL / TCP SERIAL / MIDI usable for generic
		 * UART-linked software.
		 */
		if(esp_state.serial_route != CU_ESP_SERIAL_ESP_MODULE){
			cu_esp_apply_serial_route_live();
			return;
		}

		/*
		 * SerialRoute=0 only disconnects the UART route. EspEmulationModel=0 is
		 * the actual master disable for ESP module emulation. When both are
		 * disabled, keep the ESP backend fully inert and do not touch
		 * LAN/network/MIDI state.
		 */
		if((esp_state.serial_route == CU_ESP_SERIAL_DISCONNECTED) &&
		   (esp_state.emulation_model == 0u)){
			print_message("ESP: Disabled\n");
			return;
		}

		/* Real ESP8266 boots with CIPMUX=0 (single connection). */
		esp_state.state &= ~ESP_MUX;

		memset(esp_state.link_is_ssl, 0, sizeof(esp_state.link_is_ssl));
		/* CIPSSLCCONF is NVS-backed when SYSSTORE=1. cu_esp_load_config()
		 * above has just restored that persisted state; do not erase it on
		 * reset. The other SSL client decorations are RAM-only in ESP-AT. */
		memset(esp_state.ssl_sni, 0, sizeof(esp_state.ssl_sni));
		memset(esp_state.ssl_common_name, 0, sizeof(esp_state.ssl_common_name));
		memset(esp_state.ssl_alpn_count, 0, sizeof(esp_state.ssl_alpn_count));
		memset(esp_state.ssl_alpn0, 0, sizeof(esp_state.ssl_alpn0));
		memset(esp_state.ssl_alpn1, 0, sizeof(esp_state.ssl_alpn1));
		memset(esp_state.ssl_alpn2, 0, sizeof(esp_state.ssl_alpn2));
		memset(esp_state.ssl_alpn3, 0, sizeof(esp_state.ssl_alpn3));
		memset(esp_state.ssl_alpn4, 0, sizeof(esp_state.ssl_alpn4));
		memset(esp_state.ssl_psk_id, 0, sizeof(esp_state.ssl_psk_id));
		memset(esp_state.ssl_psk_key, 0, sizeof(esp_state.ssl_psk_key));
		memset(esp_state.ssl_psk_bin, 0, sizeof(esp_state.ssl_psk_bin));
		memset(esp_state.ssl_psk_bin_len, 0, sizeof(esp_state.ssl_psk_bin_len));
		memset(esp_state.ssl_ca_path, 0, sizeof(esp_state.ssl_ca_path));
		memset(esp_state.ssl_pki_cert_path, 0, sizeof(esp_state.ssl_pki_cert_path));
		memset(esp_state.ssl_pki_key_path, 0, sizeof(esp_state.ssl_pki_key_path));
		esp_state.server_max_conn = ESP_MAX_LINKS;

		for(int i = 0; i < ESP_GPIO_PIN_COUNT; i++){
			if(i == 0 || i == 1 || i == 3 || i == 9 || i == 10 || i == 16)
				esp_state.gpio_pullup[i] = 1;
			else
				esp_state.gpio_pullup[i] = 0;
		}

		cu_esp_lan_init(&esp_state);

		cu_esp_txp(start_up_string);

		if(esp_state.serial_route == CU_ESP_SERIAL_HOST_SERIAL)
			print_message("ESP: Start[Host Serial Mode]\n");
		else if(esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL)
			print_message("ESP: Start[TCP Serial Mode]\n");
		else if(esp_state.serial_route == CU_ESP_SERIAL_HOST_MIDI)
			print_message("ESP: Start[Host MIDI Mode]\n");
		else if(esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI)
			print_message("ESP: Start[Virtual MIDI Mode]\n");
		else if(esp_state.emulation_model == 0)
			print_message("ESP: Disabled\n");
		else if(esp_state.emulation_model == 1)
			print_message("ESP: Start[ESP8266 Mode]\n");
		else if(esp_state.emulation_model == 2)
			print_message("ESP: Start[ESP32 Mode]\n");
		else{
			print_message("ESP: Start[ESP32-ETH01 Mode]\n");
		}
	}
}

static void cu_esp_at_persist_setting(const sint8 *cmd_buf)
{
	if(esp_state.at_firmware_profile == CU_ESP_AT_FW_ESPAT_230){
		if(esp_state.sysstore_mode){
			esp_state.flash_dirty = 1u;
			cu_esp_save_config();
		}
	}else if(!cmd_buf || !strstr((const char*)cmd_buf, "_CUR")){
		esp_state.flash_dirty = 1u;
	}
}

/* ------------------------------------------------------------------------- */
/* AT: CWSAP / CWSAP_CUR / CWSAP_DEF                                          */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwsap(sint8 *cmd_buf){
	const sint8 *q = (const sint8*)strchr((const char*)cmd_buf, '?');
	const sint8 *eq = (const sint8*)strchr((const char*)cmd_buf, '=');
	const uint8 modern = (esp_state.at_firmware_profile == CU_ESP_AT_FW_ESPAT_230);
	sint8 ssid[33];
	sint8 pass[65];
	int ch = 0, enc = 0, max_conn = esp_state.soft_ap_max_conn ? esp_state.soft_ap_max_conn : 4;
	int hidden = esp_state.soft_ap_hidden ? 1 : 0;

	if(q && (!eq || q < eq)){
		if(modern && esp_state.wifi_mode != 2u && esp_state.wifi_mode != 3u){ cu_esp_txp_error(); return; }
		cu_esp_txp("+CWSAP:\""); cu_esp_txp((char*)esp_state.soft_ap_name);
		cu_esp_txp("\",\""); cu_esp_txp((char*)esp_state.soft_ap_pass); cu_esp_txp("\",");
		cu_esp_txi((sint32)esp_state.soft_ap_channel); cu_esp_txp(",");
		cu_esp_txi((sint32)esp_state.soft_ap_encryption); cu_esp_txp(",");
		cu_esp_txi(max_conn); cu_esp_txp(","); cu_esp_txi(hidden); cu_esp_txp("\r\n");
		cu_esp_txp_ok(); return;
	}
	if(!eq){ cu_esp_txp_error(); return; }
	if(modern && esp_state.wifi_mode != 2u && esp_state.wifi_mode != 3u){ cu_esp_txp_error(); return; }
	{
		const sint8 *p = eq + 1;
		if(!cu_esp_parse_quoted_str(&p, ssid, (auint)sizeof(ssid), 0) ||
		   !cu_esp_expect_char(&p, ',') ||
		   !cu_esp_parse_quoted_str(&p, pass, (auint)sizeof(pass), 1) ||
		   !cu_esp_expect_char(&p, ',') || !cu_esp_parse_u16_dec(&p, &ch) ||
		   !cu_esp_expect_char(&p, ',') || !cu_esp_parse_u16_dec(&p, &enc)){
			cu_esp_txp_error(); return;
		}
		p=cu_esp_skip_ws(p);
		if(*p==','){
			p++; if(!cu_esp_parse_u16_dec(&p,&max_conn)){cu_esp_txp_error();return;}
			p=cu_esp_skip_ws(p);
			if(*p==','){ p++; if(!cu_esp_parse_u16_dec(&p,&hidden)){cu_esp_txp_error();return;} }
		}
		if(!cu_esp_expect_crlf(p) || ch<1 || ch>14 ||
		   !(enc==0 || enc==2 || enc==3 || enc==4) || max_conn<1 || max_conn>10 || hidden<0 || hidden>1 ||
		   (enc!=0 && (strlen((char*)pass)<8u || strlen((char*)pass)>64u))){ cu_esp_txp_error(); return; }
	}
	snprintf((char *)esp_state.soft_ap_name, sizeof(esp_state.soft_ap_name), "%s", (char *)ssid);
	snprintf((char *)esp_state.soft_ap_pass, sizeof(esp_state.soft_ap_pass), "%s", (char *)pass);
	esp_state.soft_ap_channel = (uint32)ch;
	esp_state.soft_ap_encryption = (uint32)enc;
	esp_state.soft_ap_max_conn = (uint8)max_conn;
	esp_state.soft_ap_hidden = (uint8)hidden;
	cu_esp_at_persist_setting(cmd_buf);
	cu_esp_lan_on_softap_change(&esp_state);
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT: CIPMODE                                                                */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipmode(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '?');
	const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');
	int v = 0;

	if(p && (!e || p < e)){
		cu_esp_txp("+CIPMODE:");
		cu_esp_txi((sint32)esp_state.cip_mode);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	if(!cu_esp_parse_bool01(&e, &v) || !cu_esp_expect_crlf(e)){
		cu_esp_txp_error();
		return;
	}

	esp_state.cip_mode = (uint8)v;
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT: CWLAP                                                                  */
/* ------------------------------------------------------------------------- */
static uint32 cu_esp_cwlap_scan_delay_cycles(sint32 channel_filter, sint32 scan_type, sint32 scan_time_min, sint32 scan_time_max){
	sint32 channels = (channel_filter > 0) ? 1 : 11;
	sint32 per_chan_ms;

	if(scan_type == 1){
		per_chan_ms = ((scan_time_max > 0) ? scan_time_max : 360);
	}else{
		if(scan_time_min <= 0 || scan_time_max <= 0 || scan_time_min != scan_time_max)
			per_chan_ms = 120;
		else
			per_chan_ms = scan_time_max;
	}

	if(per_chan_ms < 1)
		per_chan_ms = 1;
	if(channels < 1)
		channels = 1;
	return (uint32)per_chan_ms * (uint32)channels * ESP_AT_MS_DELAY;
}

void cu_esp_at_cwlapopt(sint8 *cmd_buf){
	const sint8 *p = cmd_buf + 12; /* after "AT+CWLAPOPT" */
	sint32 reserved = 0;
	sint32 print_mask = 0;
	sint32 rssi_filter = -100;
	sint32 auth_mask = 0xFFFF;

	p = cu_esp_skip_ws(p);
	if(*p != '='){
		cu_esp_txp_error();
		return;
	}
	p++;

	if(!cu_esp_parse_s32_dec(&p, &reserved) || !cu_esp_parse_comma(&p) || !cu_esp_parse_s32_dec(&p, &print_mask)){
		cu_esp_txp_error();
		return;
	}
	if(cu_esp_parse_comma(&p)){
		if(!cu_esp_parse_s32_dec(&p, &rssi_filter)){
			cu_esp_txp_error();
			return;
		}
		if(cu_esp_parse_comma(&p)){
			if(!cu_esp_parse_s32_dec(&p, &auth_mask)){
				cu_esp_txp_error();
				return;
			}
		}
	}
	if(!cu_esp_expect_crlf(p)){
		cu_esp_txp_error();
		return;
	}
	if((reserved != 0 && reserved != 1) || print_mask < 0 || print_mask > 0x07FF ||
	   rssi_filter < -100 || rssi_filter > 40 || auth_mask < 0 || auth_mask > 0xFFFF){
		cu_esp_txp_error();
		return;
	}

	esp_state.cwlap_sort_rssi = (uint8)reserved;
	esp_state.cwlap_print_mask = (uint16)print_mask;
	esp_state.cwlap_rssi_filter = (sint16)rssi_filter;
	esp_state.cwlap_authmask = (uint16)auth_mask;
	cu_esp_txp_ok();
}

void cu_esp_at_cwlap(sint8 *cmd_buf){
	const sint8 *p = cmd_buf + 8; /* after "AT+CWLAP" */
	sint8 ssid_filter[129];
	char mac_filter[18];
	sint32 channel_filter = -1;
	sint32 scan_type = 0;
	sint32 scan_time_min = 0;
	sint32 scan_time_max = 0;
	uint8 have_scan_type = 0u;
	uint8 have_scan_time_min = 0u;
	uint8 have_scan_time_max = 0u;
	cu_esp_cwlap_query_t q;

	memset(&q, 0, sizeof(q));
	ssid_filter[0] = '\0';
	mac_filter[0] = '\0';
	p = cu_esp_skip_ws(p);

	if(*p == '='){
		p++;
		p = cu_esp_skip_ws(p);
		if(*p == '"'){
			if(!cu_esp_parse_quoted_str0(&p, ssid_filter, (auint)sizeof(ssid_filter))){
				cu_esp_txp_error();
				return;
			}
			if(ssid_filter[0] != '\0')
				q.ssid = (const char *)ssid_filter;
		}
		if(*p == ','){
			p++;
			p = cu_esp_skip_ws(p);
			if(*p == '"'){
				if(!cu_esp_parse_quoted_mac(&p, mac_filter)){
					cu_esp_txp_error();
					return;
				}
				if(mac_filter[0] != '\0')
					q.bssid = (const char *)mac_filter;
			}
			if(*p == ','){
				p++;
				p = cu_esp_skip_ws(p);
				if((*p >= '0' && *p <= '9') || *p == '-'){
					if(!cu_esp_parse_s32_dec(&p, &channel_filter)){
						cu_esp_txp_error();
						return;
					}
					q.channel = channel_filter;
				}
				if(*p == ','){
					p++;
					p = cu_esp_skip_ws(p);
					if((*p >= '0' && *p <= '9') || *p == '-'){
						if(!cu_esp_parse_s32_dec(&p, &scan_type)){
							cu_esp_txp_error();
							return;
						}
						have_scan_type = 1u;
						q.scan_type = scan_type;
					}
					if(*p == ','){
						p++;
						p = cu_esp_skip_ws(p);
						if((*p >= '0' && *p <= '9') || *p == '-'){
							if(!cu_esp_parse_s32_dec(&p, &scan_time_min)){
								cu_esp_txp_error();
								return;
							}
							have_scan_time_min = 1u;
							q.scan_time_min = scan_time_min;
						}
						if(*p == ','){
							p++;
							p = cu_esp_skip_ws(p);
							if((*p >= '0' && *p <= '9') || *p == '-'){
								if(!cu_esp_parse_s32_dec(&p, &scan_time_max)){
									cu_esp_txp_error();
									return;
								}
								have_scan_time_max = 1u;
								q.scan_time_max = scan_time_max;
							}
						}
					}
				}
			}
		}
		if(!cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}
	}else if(!cu_esp_expect_crlf(p)){
		cu_esp_txp_error();
		return;
	}

	if(channel_filter != -1 && (channel_filter < 1 || channel_filter > 14)){
		cu_esp_txp_error();
		return;
	}
	if(have_scan_type && scan_type != 0 && scan_type != 1){
		cu_esp_txp_error();
		return;
	}
	if(have_scan_time_min && (scan_time_min < 0 || scan_time_min > 1500)){
		cu_esp_txp_error();
		return;
	}
	if(have_scan_time_max && (scan_time_max < 0 || scan_time_max > 1500)){
		cu_esp_txp_error();
		return;
	}
	if((have_scan_time_min || have_scan_time_max) && !have_scan_type){
		cu_esp_txp_error();
		return;
	}

	q.rssi_filter = esp_state.cwlap_rssi_filter;
	q.authmask = esp_state.cwlap_authmask;
	q.print_mask = esp_state.cwlap_print_mask;
	q.sort_enable = esp_state.cwlap_sort_rssi;

	esp_state.state |= ESP_LIST_APS;
	cu_esp_timed_stall(cu_esp_cwlap_scan_delay_cycles(channel_filter,
		have_scan_type ? scan_type : 0,
		have_scan_time_min ? scan_time_min : 0,
		have_scan_time_max ? scan_time_max : 0));
	cu_esp_ap_scan_emit(&esp_state, &q);
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT: CIPSEND                                                                */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipsend(sint8 *cmd_buf){
	uint8 is_ex = (!strncmp((const char *)cmd_buf, "AT+CIPSENDEX", 12)) ? 1u : 0u;
	const sint8 *p = cmd_buf + (is_ex ? 12 : 10);
	int conn = 0;
	int len = 0;
	sint8 host[129];
	int port = 0;
	uint8 have_override = 0u;

	p = cu_esp_skip_ws(p);

	if(p[0] == '?' && p[1] == '\r' && p[2] == '\n'){
		if((esp_state.state & ESP_AP_CONNECTED) && (esp_state.socks[0] != ESP_INVALID_SOCKET))
			cu_esp_txp_ok();
		else
			cu_esp_txp_error();
		return;
	}

	if(p[0] == '\r' && p[1] == '\n'){
		if(!(esp_state.state & ESP_AP_CONNECTED) || esp_state.socks[0] == ESP_INVALID_SOCKET){
			cu_esp_txp_error();
			return;
		}

		print_message("ESP Starting Transparent Transmission\n");

		esp_state.user_input_mode = ESP_USER_MODE_UNVARNISHED;
		esp_state.unvarnished_end_cycle = 0;
		esp_state.write_buf_pos_in = 0;
		esp_state.unvarnished_bytes = 0;

		esp_state.last_plus = esp_state.last_cycle_tick;
		esp_state.num_plus = 0;

		cu_esp_txp(">");
		return;
	}

	if(!cu_esp_expect_char(&p, '=')){
		cu_esp_txp_error();
		return;
	}

	if(esp_state.state & ESP_MUX){
		if(!cu_esp_parse_u16_dec(&p, &conn) || conn < 0 || conn >= (int)ESP_MAX_LINKS || !cu_esp_expect_char(&p, ',')){
			cu_esp_txp_error();
			return;
		}
	}else{
		conn = 0;
	}

	if(!cu_esp_parse_u16_dec(&p, &len) || len <= 0 || len >= (int)sizeof(esp_state.write_buf) || (is_ex && len > 2048)){
		cu_esp_txp_error();
		return;
	}

	if(*p == ','){
		p++;
		if((esp_state.protocol[conn] & ESP_PROTO_UDP) == 0u){
			cu_esp_txp_error();
			return;
		}
		if(!cu_esp_parse_quoted_str0(&p, host, (auint)sizeof(host)) || !cu_esp_expect_char(&p, ',') ||
		   !cu_esp_parse_u16_dec(&p, &port) || port < 0){
			cu_esp_txp_error();
			return;
		}
		have_override = 1u;
	}

	if(!cu_esp_expect_crlf(p)){
		cu_esp_txp_error();
		return;
	}

	if(!(esp_state.state & ESP_AP_CONNECTED) ||
	   (esp_state.socks[conn] == ESP_INVALID_SOCKET && !cu_esp_lan_virtual_link_open(&esp_state, (uint32)conn))){
		cu_esp_txp_error();
		return;
	}

	if(esp_state.send_prep_pending || esp_state.udp_send_resolve_pending){
		cu_esp_txp_error();
		return;
	}

	if(have_override){
		if(cu_esp_lan_virtual_link_open(&esp_state, (uint32)conn)){
			cu_esp_txp_error();
			return;
		}
		if(cu_esp_udp_send_resolve_async_start((uint32)conn, (const char *)host, (uint16)port) != 0){
			cu_esp_txp_error();
			return;
		}
		esp_state.send_to_socket = (uint32)conn;
		esp_state.send_prep_pending = 1u;
		esp_state.send_prep_len = (uint32)len;
		return;
	}

	esp_state.send_extended = is_ex;
	esp_state.send_extended_escape = 0u;
	cu_esp_begin_send_prompt((uint8)conn, (uint32)len);
}



/* ------------------------------------------------------------------------- */
/* AT: CWJAP / CWJAP_CUR / CWJAP_DEF                                          */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwjap(sint8 *cmd_buf){
	const sint8*p=cmd_buf+8; sint8 ssid[65],pass[65],bssid[24]; int save_def=0;
	if(!strncmp((char*)p,"?\r\n",3)||!strncmp((char*)p,"_CUR?\r\n",7)||!strncmp((char*)p,"_DEF?\r\n",7)){
		if(!(esp_state.state&ESP_AP_CONNECTED)){cu_esp_txp_error();return;}
		if(cu_esp_at23_enabled()){
			cu_esp_txp("+CWJAP:\"");cu_esp_txp((char*)esp_state.wifi_name);cu_esp_txp("\",\"");cu_esp_txp((char*)esp_state.wifi_mac);cu_esp_txp("\",");cu_esp_txi(esp_state.wifi_channel);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_rssi);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_pci_en);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_reconn_interval);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_listen_interval);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_scan_mode);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_pmf);cu_esp_txp("\r\nOK\r\n");
		}else{ sint8 b[256]; if(cu_esp_ap_query_join(&esp_state,b,sizeof(b))==0)cu_esp_txp((char*)b);else cu_esp_txp_error(); }
		return;
	}
	if(!strncmp((char*)p,"\r\n",2)){ if(cu_esp_ap_start_join(&esp_state,(const char*)esp_state.wifi_name,(const char*)esp_state.wifi_pass,NULL)!=0)cu_esp_txp_error(); return; }
	if(*p=='=')p++;else if(!strncmp((char*)p,"_CUR=",5))p+=5;else if(!strncmp((char*)p,"_DEF=",5)){p+=5;save_def=1;}else{cu_esp_txp_error();return;}
	ssid[0]=pass[0]=bssid[0]=0;
	/* ESP-AT 2.3 permits omitted SSID/password fields; keep the current one. */
	if(*p==',')snprintf((char*)ssid,sizeof(ssid),"%s",(char*)esp_state.wifi_name);else if(!cu_esp_parse_quoted_str(&p,ssid,sizeof(ssid),0)){cu_esp_txp_error();return;}
	if(!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}
	if(*p==',')snprintf((char*)pass,sizeof(pass),"%s",(char*)esp_state.wifi_pass);else if(*p=='\r')snprintf((char*)pass,sizeof(pass),"%s",(char*)esp_state.wifi_pass);else if(!cu_esp_parse_quoted_str0(&p,pass,sizeof(pass))){cu_esp_txp_error();return;}
	if(cu_esp_at23_enabled()){
		int pci=esp_state.wifi_pci_en,reconn=esp_state.wifi_reconn_interval,listen=esp_state.wifi_listen_interval,scan=esp_state.wifi_scan_mode,jtout=esp_state.wifi_jap_timeout,pmf=esp_state.wifi_pmf;
		if(*p==','){p++;if(*p!=','){if(!cu_esp_parse_quoted_str0(&p,bssid,sizeof(bssid))){cu_esp_txp_error();return;}}
			if(*p==','){p++;if(*p!=','){if(!cu_esp_parse_u16_dec(&p,&pci)||pci<0||pci>1){cu_esp_txp_error();return;}}
				if(*p==','){p++;if(*p!=','){if(!cu_esp_parse_u16_dec(&p,&reconn)||reconn<0||reconn>7200){cu_esp_txp_error();return;}}
					if(*p==','){p++;if(*p!=','){if(!cu_esp_parse_u16_dec(&p,&listen)||listen<1||listen>100){cu_esp_txp_error();return;}}
						if(*p==','){p++;if(*p!=','){if(!cu_esp_parse_u16_dec(&p,&scan)||scan<0||scan>1){cu_esp_txp_error();return;}}
							if(*p==','){p++;if(*p!=','){if(!cu_esp_parse_u16_dec(&p,&jtout)||jtout<3||jtout>600){cu_esp_txp_error();return;}}
								if(*p==','){p++;if(!cu_esp_parse_u16_dec(&p,&pmf)||pmf<0||pmf>3){cu_esp_txp_error();return;}}
							}
						}
					}
				}
			}
		}
		if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.wifi_pci_en=(uint8)pci;esp_state.wifi_reconn_interval=(uint16)reconn;esp_state.wifi_listen_interval=(uint8)listen;esp_state.wifi_scan_mode=(uint8)scan;esp_state.wifi_jap_timeout=(uint16)jtout;esp_state.wifi_pmf=(uint8)pmf;
	}else if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	if(save_def||esp_state.sysstore_mode)esp_state.flash_dirty=1;
	if(cu_esp_ap_start_join(&esp_state,(char*)ssid,(char*)pass,bssid[0]?(char*)bssid:NULL)!=0){cu_esp_txp_error();return;}
	if(esp_state.sysstore_mode)cu_esp_save_config();
}

/* ------------------------------------------------------------------------- */
/* AT: CWMODE / CWMODE_CUR / CWMODE_DEF                                       */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwmode(sint8 *cmd_buf){
	const sint8*p=cmd_buf+9;int mode=0,auto_conn=1,save_def=0;p=cu_esp_skip_ws(p);
	if(p[0]=='?'&&p[1]=='\r'){cu_esp_txp("+CWMODE:");cu_esp_txi(esp_state.wifi_mode);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(p[0]=='='&&p[1]=='?'&&p[2]=='\r'){cu_esp_txp(cu_esp_at23_enabled()?"+CWMODE:(0-3)\r\n":"+CWMODE:(1-3)\r\n");cu_esp_txp_ok();return;}
	if(p[0]=='=')p++;else if(!strncmp((char*)p,"_CUR=",5))p+=5;else if(!strncmp((char*)p,"_DEF=",5)){p+=5;save_def=1;}else{cu_esp_txp_error();return;}
	if(!cu_esp_parse_u16_dec(&p,&mode)||mode<(cu_esp_at23_enabled()?0:1)||mode>3){cu_esp_txp_error();return;}
	if(*p==','){p++;if(!cu_esp_parse_u16_dec(&p,&auto_conn)||(auto_conn!=0&&auto_conn!=1)){cu_esp_txp_error();return;}}
	if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.wifi_mode=(uint8)mode;esp_state.wifi_autoconn=(uint8)auto_conn;if(auto_conn)esp_state.state|=ESP_AUTOCONNECT;else esp_state.state&=~ESP_AUTOCONNECT;if(save_def||esp_state.sysstore_mode)esp_state.flash_dirty=1;cu_esp_lan_on_cwmode_change(&esp_state);if(esp_state.sysstore_mode)cu_esp_save_config();cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT / ATE                                                                   */
/* ------------------------------------------------------------------------- */
void cu_esp_at(void){
	cu_esp_txp_ok();
}

void cu_esp_at_ate(sint8 *cmd_buf){
	if(!strncmp("0\r\n", (const char*)&cmd_buf[3], 3)){
		esp_state.state &= ~ESP_ECHO;
		cu_esp_txp_ok();
	}else if(!strncmp("1\r\n", (const char*)&cmd_buf[3], 3)){
		esp_state.state |= ESP_ECHO;
		cu_esp_txp_ok();
	}else if(!strncmp("?\r\n", (const char*)&cmd_buf[3], 3)){
		cu_esp_txp_ok();
	}else{
		cu_esp_txp_error();
	}
}

/* ------------------------------------------------------------------------- */
/* AT: CIPSTART                                                               */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipstart(sint8 *cmd_buf){
	const sint8*p=cmd_buf+11;int conn=0,port=0,local_port=0,udp_mode=0,keepalive=0;uint32 type=0,timeout=0;uint8 is_udp=0,want_ssl=0;sint8 proto[12],host[129],local_ip[64];local_ip[0]=0;
	if(strstr((char*)cmd_buf,"?\r\n")){if(esp_state.state&ESP_AP_CONNECTED)cu_esp_txp_ok();else cu_esp_txp_error();return;}
	if(!(esp_state.state&ESP_AP_CONNECTED)||!cu_esp_expect_char(&p,'=')){cu_esp_txp_error();return;}
	if(esp_state.state&ESP_MUX){if(!cu_esp_parse_u16_dec(&p,&conn)||conn<0||conn>=(int)ESP_MAX_LINKS||!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}}
	if(!cu_esp_parse_quoted_str(&p,proto,sizeof(proto),0)||!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}
	if(!strcmp((char*)proto,"TCP"))type=ESP_PROTO_TCP;else if(!strcmp((char*)proto,"UDP")){type=ESP_PROTO_UDP;is_udp=1;}else if(!strcmp((char*)proto,"SSL")){type=ESP_PROTO_SSL;want_ssl=1;}else if(!strcmp((char*)proto,"TCPv6")){type=ESP_PROTO_TCP|ESP_PROTO_IPV6;}else if(!strcmp((char*)proto,"UDPv6")){type=ESP_PROTO_UDP|ESP_PROTO_IPV6;is_udp=1;}else if(!strcmp((char*)proto,"SSLv6")){type=ESP_PROTO_SSL|ESP_PROTO_IPV6;want_ssl=1;}else{cu_esp_txp_error();return;}
	if((type&ESP_PROTO_IPV6)&&!esp_state.ipv6_enabled){cu_esp_txp_error();return;}
	if(!(is_udp?cu_esp_parse_quoted_str0(&p,host,sizeof(host)):cu_esp_parse_quoted_str(&p,host,sizeof(host),0))||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&port)||port<0||port>65535){cu_esp_txp_error();return;}
	if(is_udp){
		if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&local_port)||local_port<0||local_port>65535){cu_esp_txp_error();return;}}
			if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&udp_mode)||udp_mode<0||udp_mode>2){cu_esp_txp_error();return;}}
				if(*p==','){p++;if(*p!='\r'){if(!cu_esp_parse_quoted_str0(&p,local_ip,sizeof(local_ip))){cu_esp_txp_error();return;}}}
			}
		}
	}else{
		if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&keepalive)||keepalive<0||keepalive>7200){cu_esp_txp_error();return;}}
			if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_quoted_str0(&p,local_ip,sizeof(local_ip))){cu_esp_txp_error();return;}}
				if(*p==','){p++;if(*p!='\r'){auint t;if(!cu_esp_parse_u32_8(p,&t,&p)||t>60000u){cu_esp_txp_error();return;}timeout=t;}}
			}
		}
	}
	if(!cu_esp_expect_crlf(p)||(!is_udp&&port<=0)||(is_udp&&udp_mode!=0&&local_port<=0)){cu_esp_txp_error();return;}
	if(esp_state.socks[conn]!=ESP_INVALID_SOCKET||cu_esp_lan_virtual_link_present(&esp_state,(uint32)conn)||(want_ssl&&cu_esp_ssl_link_busy((uint32)conn))){cu_esp_txp_error();return;}
	esp_state.protocol[conn]=type;esp_state.link_is_ssl[conn]=want_ssl;esp_state.link_wait_ok[conn]=1u;esp_state.tcp_keepalive[conn]=(uint16)keepalive;esp_state.link_timeout_ms[conn]=timeout;snprintf((char*)esp_state.link_local_ip[conn],sizeof(esp_state.link_local_ip[conn]),"%s",(char*)local_ip);
	if(cu_esp_net_connect_ex(host,(uint32)conn,(uint32)port,type,(uint32)local_port,(uint32)udp_mode)!=0){esp_state.link_wait_ok[conn]=0;esp_state.link_is_ssl[conn]=0;cu_esp_txp_error();}
}



/* ------------------------------------------------------------------------- */
/* AT: IPR (deprecated wrapper for CIOBAUD)                                   */
/* ------------------------------------------------------------------------- */
void cu_esp_at_ipr(sint8 *cmd_buf){
	auint narg = 0;
	sscanf((char *)&cmd_buf[7], "%u", (unsigned *)&narg);
	snprintf((char *)cmd_buf, 256, "AT+CIOBAUD=%u\r\n", (unsigned)narg);
	cu_esp_at_ciobaud(cmd_buf);
}

/* ------------------------------------------------------------------------- */
/* AT: CIPCLOSE                                                               */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipclose(sint8 *cmd_buf){

	if(!(esp_state.state & ESP_MUX)){
		if(strstr((const char*)cmd_buf, "\r\n") == NULL){
			cu_esp_txp_error();
			return;
		}
		cu_esp_close_socket(0);
		esp_state.link_is_ssl[0] = 0;
		cu_esp_tx_evt_closed(0);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	{
		const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');
		const sint8 *ep = NULL;
		auint link = 0;

		if(!e){
			cu_esp_txp_error();
			return;
		}
		e++;

		if(!cu_esp_parse_u32_8(e, &link, &ep) || !cu_esp_expect_crlf(ep) || ((link >= ESP_MAX_LINKS) && (link != 5u))){
			cu_esp_txp_error();
			return;
		}

		if(link == 5u){
			for(uint32 i = 0; i < ESP_MAX_LINKS; i++){
				cu_esp_close_socket(i);
				esp_state.link_is_ssl[i] = 0;
			}
			for(uint8 i = 0; i < ESP_MAX_LINKS; i++)
				cu_esp_tx_evt_closed(i);
			cu_esp_txp_ok();
			return;
		}

		cu_esp_close_socket((uint32)link);
		esp_state.link_is_ssl[link] = 0;
		cu_esp_tx_evt_closed((uint8)link);
		cu_esp_txp_ok();
	}
}

/* ------------------------------------------------------------------------- */
/* AT: CIPSTATUS                                                              */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipstatus(sint8 *cmd_buf){
	(void)cmd_buf;
	uint32 stat = 1u;
	uint8 any_link = 0u;

	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
		if(esp_state.socks[i] != ESP_INVALID_SOCKET || esp_state.link_state[i] != CU_ESP_LS_IDLE){
			any_link = 1u;
			break;
		}
	}

	if(esp_state.state & ESP_AP_CONNECTED)
		stat = any_link ? 3u : 2u;
	else if(any_link)
		stat = 4u;

	cu_esp_txp("STATUS:");
	cu_esp_txi((sint32)stat);
	cu_esp_txp("\r\n");

	for(uint32 i = 0; i < ESP_MAX_LINKS; i++){
		char line[192];
		char ripbuf[80]="0.0.0.0"; const char *rip=ripbuf;
		uint16 rport = 0u;
		uint16 lport = 0u;
		uint8 tetype = 0u;

		if(esp_state.socks[i] == ESP_INVALID_SOCKET)
			continue;

		(void)cu_esp_net_get_peer_text(i,ripbuf,sizeof(ripbuf),&rport);
		lport=cu_esp_net_get_local_port(i);

		if(esp_state.link_is_server[i])
			tetype = 1u;

		if(esp_state.link_is_ssl[i]){
			snprintf(line, sizeof(line), "+CIPSTATUS:%lu,\"SSL\",\"%s\",%u,%u,%u\r\n",
				(unsigned long)i, rip ? rip : "0.0.0.0", (unsigned)rport, (unsigned)lport, (unsigned)tetype);
		}else if(esp_state.protocol[i] == ESP_PROTO_UDP){
			snprintf(line, sizeof(line), "+CIPSTATUS:%lu,\"UDP\",\"%s\",%u,%u,%u\r\n",
				(unsigned long)i, rip ? rip : "0.0.0.0", (unsigned)rport, (unsigned)lport, (unsigned)tetype);
		}else{
			snprintf(line, sizeof(line), "+CIPSTATUS:%lu,\"TCP\",\"%s\",%u,%u,%u\r\n",
				(unsigned long)i, rip ? rip : "0.0.0.0", (unsigned)rport, (unsigned)lport, (unsigned)tetype);
		}
		cu_esp_txp(line);
	}

	cu_esp_txp_ok();
}



/* ------------------------------------------------------------------------- */
/* AT: CIPSERVERMAXCONN                                                       */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipservermaxconn(sint8 *cmd_buf){
	const sint8 *q = (const sint8*)strchr((const char*)cmd_buf, '?');
	const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');

	if(q && (!e || q < e)){
		cu_esp_txp("+CIPSERVERMAXCONN:");
		cu_esp_txi((sint32)esp_state.server_max_conn);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	{
		int v = 0;
		if(!cu_esp_parse_u16_dec(&e, &v) || !cu_esp_expect_crlf(e) || v < 1 || v > (int)ESP_MAX_LINKS){
			cu_esp_txp_error();
			return;
		}
		esp_state.server_max_conn = (uint8)v;
		cu_esp_txp_ok();
	}
}

/* ------------------------------------------------------------------------- */
/* AT: CIPSERVER                                                              */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipserver(sint8 *cmd_buf){
	const char *cmd = (const char*)cmd_buf;
	const char *p;
	uint32 mode_u32 = 0;
	uint32 port_u32 = 0;
	uint16 port;

	p = strstr(cmd, "AT+CIPSERVER");
	if(!p){
		cu_esp_at_txp_bad_command();
		return;
	}
	p += 12;

	while(*p == ' ' || *p == '	') p++;

	if(*p == '=' && p[1] == '?'){
		cu_esp_txp("+CIPSERVER:(0,1),(1-65535)\r\n\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(*p == '?'){
		uint16 qport = esp_state.server_last_port ? esp_state.server_last_port : 333u;
		uint32 running = (esp_state.listen_socket != ESP_INVALID_SOCKET) ? 1u : 0u;
		if(esp_state.listen_socket != ESP_INVALID_SOCKET && esp_state.listen_port != 0u)
			qport = esp_state.listen_port;
		else if(esp_state.server_last_port != 0u)
			qport = esp_state.server_last_port;
		{
			char out[64];
			snprintf(out, sizeof(out), "+CIPSERVER:%lu,%u\r\n\r\n", (unsigned long)running, (unsigned)qport);
			cu_esp_txp(out);
		}
		cu_esp_txp_ok();
		return;
	}

	if(*p != '='){
		cu_esp_at_txp_bad_command();
		return;
	}
	p++;

	while(*p == ' ' || *p == '	') p++;
	if(*p < '0' || *p > '9'){
		cu_esp_at_txp_bad_command();
		return;
	}
	while(*p >= '0' && *p <= '9'){
		mode_u32 = (mode_u32 * 10u) + (uint32)(*p - '0');
		p++;
	}
	while(*p == ' ' || *p == '	') p++;

	port_u32 = (uint32)(esp_state.server_last_port ? esp_state.server_last_port : 333u);
	if(*p == ','){
		p++;
		while(*p == ' ' || *p == '	') p++;
		if(*p < '0' || *p > '9'){
			cu_esp_at_txp_bad_command();
			return;
		}
		port_u32 = 0;
		while(*p >= '0' && *p <= '9'){
			port_u32 = (port_u32 * 10u) + (uint32)(*p - '0');
			p++;
		}
		while(*p == ' ' || *p == '	') p++;
	}

	while(*p && *p != '\r' && *p != '\n') p++;

	if(mode_u32 > 1u || port_u32 < 1u || port_u32 > 65535u){
		cu_esp_txp_error();
		return;
	}
	port = (uint16)port_u32;

	if(!(esp_state.state & ESP_MUX)){
		cu_esp_txp_error();
		return;
	}

	if(mode_u32 == 0u){
		(void)cu_esp_listen(0u);
		for(uint32 i = 0; i < ESP_MAX_LINKS; i++){
			cu_esp_close_socket(i);
			esp_state.link_is_ssl[i] = 0;
		}
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp_ok();
		return;
	}

	if(cu_esp_listen((uint32)port) != 0){
		cu_esp_txp_error();
		return;
	}

	esp_state.server_last_port = port;
	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT: CIPMUX                                                                 */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipmux(sint8 *cmd_buf){
	const sint8 *p = cmd_buf + 9;
	int v = 0;

	p = cu_esp_skip_ws(p);
	if(p[0] == '?' && p[1] == '\r' && p[2] == '\n'){
		cu_esp_txp("+CIPMUX:");
		cu_esp_txi((esp_state.state & ESP_MUX) ? 1 : 0);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!cu_esp_expect_char(&p, '=') || !cu_esp_parse_bool01(&p, &v) || !cu_esp_expect_crlf(p)){
		cu_esp_txp_error();
		return;
	}

	if(v == 0){
		esp_state.state &= ~ESP_MUX;
		for(sint32 s = 1; s < (sint32)ESP_MAX_LINKS; s++){
			cu_esp_close_socket((uint32)s);
			cu_esp_cipbuf_clear_link(&esp_state, (uint8)s);
			esp_state.link_is_ssl[s] = 0;
		}
	}else{
		esp_state.state |= ESP_MUX;
	}

	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT: "this no fun"                                                          */
/* ------------------------------------------------------------------------- */
void cu_esp_at_bad_command(void){
	cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* AT: CWQAP                                                                  */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwqap(sint8 *cmd_buf){
	(void)cmd_buf;

	cu_esp_ap_disconnect(&esp_state, 1u);
	cu_esp_txp("WIFI DISCONNECT\r\n");
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* AT: GSLP                                                                   */
/* ------------------------------------------------------------------------- */
void cu_esp_at_gslp(sint8 *cmd_buf){
	const sint8 *p=(const sint8*)strchr((const char*)cmd_buf,'='); auint ms=0;
	if(!p){cu_esp_txp_error();return;} p++;
	if(!cu_esp_parse_u32_8(p,&ms,&p)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	cu_esp_txi((sint32)ms);cu_esp_txp("\r\n");cu_esp_txp_ok();
	/* Simulate passage into deep sleep without blocking the emulator for hours. */
	if(ms && ms < 10000u) cu_esp_timed_stall(ESP_AT_MS_DELAY*ms);
}

/* ------------------------------------------------------------------------- */
/* IPv4 helpers for DHCP AT commands                                          */
/* ------------------------------------------------------------------------- */
static uint32 cu_esp_at_ipv4_to_be(const char *ipstr, uint32 fallback_be)
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

static void cu_esp_at_ipv4_be_to_str(uint32 be, char *out, size_t out_sz)
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

/* ------------------------------------------------------------------------- */
/* AT: CWLIF / CWDHCP / CWDHCPS                                               */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwlif(sint8 *cmd_buf){
	(void)cmd_buf;
	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	cu_esp_ap_emit_station_list(&esp_state);
	cu_esp_txp_ok();
}

void cu_esp_at_cwdhcp(sint8 *cmd_buf){
	const sint8 *p = cmd_buf + 9; /* after "AT+CWDHCP" */
	sint32 operate = 0;
	sint32 mode = 0;
	uint32 state = 0u;

	if(!strncmp((const char *)p, "?\r\n", 3) ||
	   !strncmp((const char *)p, "_CUR?\r\n", 7) ||
	   !strncmp((const char *)p, "_DEF?\r\n", 7)){
		if(esp_state.lan.dhcp_enable_sta)
			state |= 1u;
		if(esp_state.lan.dhcp_enable_ap)
			state |= 2u;
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CWDHCP:");
		cu_esp_txi((sint32)state);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!strncmp((const char *)p, "=?\r\n", 4) ||
	   !strncmp((const char *)p, "_CUR=?\r\n", 8) ||
	   !strncmp((const char *)p, "_DEF=?\r\n", 8)){
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CWDHCP:(0-1),(1-3)\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(p[0] == '='){
		p++;
	}else if(!strncmp((const char *)p, "_CUR=", 5)){
		p += 5;
	}else if(!strncmp((const char *)p, "_DEF=", 5)){
		p += 5;
	}else{
		cu_esp_txp_error();
		return;
	}

	if(!cu_esp_parse_u16_dec(&p, &operate) ||
	   !cu_esp_expect_char(&p, ',') ||
	   !cu_esp_parse_u16_dec(&p, &mode) ||
	   !cu_esp_expect_crlf(p) ||
	   (operate < 0) || (operate > 1) ||
	   (mode < 1) || (mode > 3)){
		cu_esp_txp_error();
		return;
	}

	if(mode & 1){
		esp_state.lan.dhcp_enable_sta = (uint8)operate;
	}
	if(mode & 2){
		esp_state.lan.dhcp_enable_ap = (uint8)operate;
	}

	cu_esp_at_persist_setting(cmd_buf);

	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	cu_esp_txp_ok();
}

void cu_esp_at_cwdhcps(sint8 *cmd_buf){
	const sint8 *p = cmd_buf + 10; /* after "AT+CWDHCPS" */
	char ip0[24];
	char ip1[24];

	if(!strncmp((const char *)p, "?\r\n", 3)){
		cu_esp_at_ipv4_be_to_str(esp_state.lan.pool_start_be, ip0, sizeof(ip0));
		cu_esp_at_ipv4_be_to_str(esp_state.lan.pool_end_be, ip1, sizeof(ip1));
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CWDHCPS:");
		cu_esp_txi(esp_state.soft_ap_dhcp_lease_min ? esp_state.soft_ap_dhcp_lease_min : 5);
		cu_esp_txp(",\"");
		cu_esp_txp(ip0);
		cu_esp_txp("\",\"");
		cu_esp_txp(ip1);
		cu_esp_txp("\"\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!strncmp((const char *)p, "=?\r\n", 4)){
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CWDHCPS:(0-1),(1-2880),<start IP>,<end IP>\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(p[0] == '='){
		sint32 enable = 0;
		sint32 lease_min = 0;
		sint8 start_ip[64];
		sint8 end_ip[64];
		uint32 start_be;
		uint32 end_be;
		uint32 ap_be;
		uint32 mask_be;
		uint32 net_host;

		p++;
		if(!cu_esp_parse_u16_dec(&p, &enable)){
			cu_esp_txp_error();
			return;
		}
		if(enable == 0){
			if(!cu_esp_expect_crlf(p)){
				cu_esp_txp_error();
				return;
			}
			ap_be = cu_esp_at_ipv4_to_be((const char *)esp_state.soft_ap_ip, htonl(0x0A000001u));
			mask_be = cu_esp_at_ipv4_to_be((const char *)esp_state.soft_ap_netmask, htonl(0xFFFFFF00u));
			net_host = ntohl(ap_be) & ntohl(mask_be);
			esp_state.lan.pool_start_be = htonl(net_host | 100u);
			esp_state.lan.pool_end_be = htonl(net_host | 111u);
			esp_state.soft_ap_dhcp_lease_min = 5u;
			cu_esp_at_persist_setting(cmd_buf);
			cu_esp_timed_stall(ESP_AT_OK_DELAY);
			cu_esp_txp_ok();
			return;
		}

		if(enable != 1 ||
		   !cu_esp_expect_char(&p, ',') ||
		   !cu_esp_parse_u16_dec(&p, &lease_min) ||
		   !cu_esp_expect_char(&p, ',') ||
		   !cu_esp_parse_quoted_str(&p, start_ip, (auint)sizeof(start_ip), 0) ||
		   !cu_esp_expect_char(&p, ',') ||
		   !cu_esp_parse_quoted_str(&p, end_ip, (auint)sizeof(end_ip), 0) ||
		   !cu_esp_expect_crlf(p) ||
		   lease_min < 1 || lease_min > 2880){
			cu_esp_txp_error();
			return;
		}

		start_be = cu_esp_at_ipv4_to_be((const char *)start_ip, 0u);
		end_be = cu_esp_at_ipv4_to_be((const char *)end_ip, 0u);
		ap_be = cu_esp_at_ipv4_to_be((const char *)esp_state.soft_ap_ip, htonl(0x0A000001u));
		mask_be = cu_esp_at_ipv4_to_be((const char *)esp_state.soft_ap_netmask, htonl(0xFFFFFF00u));

		if(start_be == 0u || end_be == 0u ||
		   ntohl(start_be) > ntohl(end_be) ||
		   ((ntohl(start_be) & ntohl(mask_be)) != (ntohl(ap_be) & ntohl(mask_be))) ||
		   ((ntohl(end_be) & ntohl(mask_be)) != (ntohl(ap_be) & ntohl(mask_be)))){
			cu_esp_txp_error();
			return;
		}

		esp_state.lan.pool_start_be = start_be;
		esp_state.lan.pool_end_be = end_be;
		esp_state.soft_ap_dhcp_lease_min = (uint16)lease_min;
		cu_esp_at_persist_setting(cmd_buf);
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* CIPSTAMAC / CIPAPMAC (support _CUR/_DEF)                                   */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipstamac(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		sint8 out[96];
		if(cu_esp_ap_query_station_mac(&esp_state, out, sizeof(out)) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp((const char *)out);
		return;
	}

	if(e){
		const sint8 *p = (const sint8*)(e + 1);
		char mac[18];

		if(!cu_esp_parse_quoted_mac(&p, mac) || !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		if(cu_esp_ap_set_station_mac(&esp_state, (const sint8 *)mac) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_at_persist_setting(cmd_buf);
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

void cu_esp_at_cipapmac(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		sint8 out[96];
		if(cu_esp_ap_query_softap_mac(&esp_state, out, sizeof(out)) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp((const char *)out);
		return;
	}

	if(e){
		const sint8 *p = (const sint8*)(e + 1);
		char mac[18];

		if(!cu_esp_parse_quoted_mac(&p, mac) || !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		if(cu_esp_ap_set_softap_mac(&esp_state, (const sint8 *)mac) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_at_persist_setting(cmd_buf);
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* CIPSTA / CIPAP (support _CUR/_DEF)                                         */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipsta(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		sint8 out[192];
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		if(cu_esp_ap_query_station_ip(&esp_state, out, (auint)sizeof(out)) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_txp((char *)out);
		return;
	}

	if(e){
		const sint8 *p = (const sint8*)(e + 1);
		sint8 ip[64];
		sint8 gw[64];
		sint8 nm[64];
		gw[0] = 0;
		nm[0] = 0;
		if(!cu_esp_parse_quoted_str(&p, ip, (auint)sizeof(ip), 0)){
			cu_esp_txp_error();
			return;
		}
		p = cu_esp_skip_ws(p);
		if(*p == ','){
			p++;
			if(!cu_esp_parse_quoted_str(&p, gw, (auint)sizeof(gw), 0)){
				cu_esp_txp_error();
				return;
			}
			if(!cu_esp_expect_char(&p, ',') || !cu_esp_parse_quoted_str(&p, nm, (auint)sizeof(nm), 0)){
				cu_esp_txp_error();
				return;
			}
		}
		if(!cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}
		if(cu_esp_ap_set_station_ip(&esp_state, ip, gw, nm) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_at_persist_setting(cmd_buf);
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}


void cu_esp_at_cipap(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		sint8 out[192];
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		if(cu_esp_ap_query_softap_ip(&esp_state, out, (auint)sizeof(out)) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_txp((char *)out);
		return;
	}

	if(e){
		const sint8 *p = (const sint8*)(e + 1);
		sint8 ip[64];
		sint8 gw[64];
		sint8 nm[64];
		gw[0] = 0;
		nm[0] = 0;
		if(!cu_esp_parse_quoted_str(&p, ip, (auint)sizeof(ip), 0)){
			cu_esp_txp_error();
			return;
		}
		p = cu_esp_skip_ws(p);
		if(*p == ','){
			p++;
			if(!cu_esp_parse_quoted_str(&p, gw, (auint)sizeof(gw), 0)){
				cu_esp_txp_error();
				return;
			}
			if(!cu_esp_expect_char(&p, ',') || !cu_esp_parse_quoted_str(&p, nm, (auint)sizeof(nm), 0)){
				cu_esp_txp_error();
				return;
			}
		}
		if(!cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}
		if(cu_esp_ap_set_softap_ip(&esp_state, ip, gw, nm) != 0){
			cu_esp_txp_error();
			return;
		}
		cu_esp_at_persist_setting(cmd_buf);
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}


/* ------------------------------------------------------------------------- */
/* CIPDNS (newer AT command)                                                  */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipdns(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		cu_esp_txp("+CIPDNS:");
		cu_esp_txi((sint32)esp_state.dns_enable);
		cu_esp_txp(",\"");
		cu_esp_txp(((char*)esp_state.dns_server[0]));
		cu_esp_txp("\",\"");
		cu_esp_txp(((char*)esp_state.dns_server[1]));
		cu_esp_txp("\",\"");
		cu_esp_txp(((char*)esp_state.dns_server[2]));
		cu_esp_txp("\"\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	{
		const sint8 *p = (const sint8*)e;
		int en = 0;
		sint8 d1[64], d2[64], d3[64];

		d1[0] = d2[0] = d3[0] = 0;

		if(!cu_esp_parse_bool01(&p, &en)){
			cu_esp_txp_error();
			return;
		}

		p = cu_esp_skip_ws(p);
		if(*p == ','){
			p++;
			if(!cu_esp_parse_quoted_str(&p, d1, (auint)sizeof(d1), 0)){
				cu_esp_txp_error();
				return;
			}

			p = cu_esp_skip_ws(p);
			if(*p == ','){
				p++;
				if(!cu_esp_parse_quoted_str(&p, d2, (auint)sizeof(d2), 0)){
					cu_esp_txp_error();
					return;
				}

				p = cu_esp_skip_ws(p);
				if(*p == ','){
					p++;
					if(!cu_esp_parse_quoted_str(&p, d3, (auint)sizeof(d3), 0)){
						cu_esp_txp_error();
						return;
					}
				}
			}
		}

		if(!cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		esp_state.dns_enable = (uint8)en;
		if(d1[0]) snprintf((char*)esp_state.dns_server[0], sizeof esp_state.dns_server[0], "%s", (char*)d1);
		if(d2[0]) snprintf((char*)esp_state.dns_server[1], sizeof esp_state.dns_server[1], "%s", (char*)d2);
		if(d3[0]) snprintf((char*)esp_state.dns_server[2], sizeof esp_state.dns_server[2], "%s", (char*)d3);
		cu_esp_at_persist_setting(cmd_buf);

		cu_esp_txp_ok();
	}
}

/* ------------------------------------------------------------------------- */
/* CIPDOMAIN (newer AT command)                                               */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipdomain(sint8 *cmd_buf){
	const sint8 *e=(const sint8*)strchr((const char*)cmd_buf,'='); sint8 host[129]; uint32 net=1u;
	if(!e){cu_esp_txp_error();return;} e++; {const sint8*p=e;if(!cu_esp_parse_quoted_str0(&p,host,sizeof(host))){cu_esp_txp_error();return;}
		if(*p==','){p++;if(!cu_esp_parse_u32_8(p,&net,&p)||net<1u||net>3u){cu_esp_txp_error();return;}}
		if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}}
	if(net==3u && !esp_state.ipv6_enabled){cu_esp_txp_error();return;}
	if(cu_esp_dns_async_start_ex((const char*)host,(uint8)net)!=0)cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* CIPSTO                                                                     */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipsto(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '?');
	const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');

	if(p && (!e || p < e)){
		cu_esp_txp("+CIPSTO:");
		cu_esp_txi((sint32)esp_state.server_timeout);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(e){
		int v = 0;
		e++;
		if(!cu_esp_parse_u16_dec(&e, &v) || v > 7200 || !cu_esp_expect_crlf(e)){
			cu_esp_txp_error();
			return;
		}
		esp_state.server_timeout = (uint16)v;
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* CIFSR (simple)                                                             */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cifsr(sint8 *cmd_buf){
	(void)cmd_buf;
	if((esp_state.wifi_mode == ESP_WIFI_MODE_STATION) || (esp_state.wifi_mode == ESP_WIFI_MODE_SOFTAP_STATION)){
		cu_esp_txp("+CIFSR:STAIP,\"");
		cu_esp_txp((char *)esp_state.station_ip);
		cu_esp_txp("\"\r\n");
		cu_esp_txp("+CIFSR:STAMAC,\"");
		cu_esp_txp((char *)esp_state.station_mac);
		cu_esp_txp("\"\r\n");
	}
	if((esp_state.wifi_mode == ESP_WIFI_MODE_SOFTAP) || (esp_state.wifi_mode == ESP_WIFI_MODE_SOFTAP_STATION)){
		cu_esp_txp("+CIFSR:APIP,\"");
		cu_esp_txp((char *)esp_state.soft_ap_ip);
		cu_esp_txp("\"\r\n");
		cu_esp_txp("+CIFSR:APMAC,\"");
		cu_esp_txp((char *)esp_state.soft_ap_mac);
		cu_esp_txp("\"\r\n");
	}
	cu_esp_txp_ok();
}


/* ------------------------------------------------------------------------- */
/* SYSMSG / SLEEP                                                             */
/* ------------------------------------------------------------------------- */
void cu_esp_at_sysmsg(sint8 *cmd_buf){
	const char *q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+SYSMSG:");cu_esp_txi((sint32)esp_state.sysmsg_flags);cu_esp_txp("\r\n");cu_esp_txp_ok();return;
	}
	if(e){
		const sint8 *p=(const sint8*)e+1;int v=0;
		if(!cu_esp_parse_u16_dec(&p,&v)||v<0||v>7||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
		esp_state.sysmsg_flags=(uint8)v;cu_esp_at23_store_if_enabled();cu_esp_txp_ok();return;
	}
	cu_esp_txp_error();
}

void cu_esp_at_sleep(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp("+SLEEP:");cu_esp_txi((esp_state.sleep_mode==2)?3:esp_state.sleep_mode);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;int v;if(!cu_esp_parse_u16_dec(&p,&v)||v<0||v>3||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}if(v!=0&&!(esp_state.wifi_mode&ESP_WIFI_MODE_STATION)){cu_esp_txp_error();return;}esp_state.sleep_mode=v;cu_esp_txp_ok();return;}cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* CIPSNTPCFG (fixed: query/clear/set)                                        */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipsntpcfg(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CIPSNTPCFG:");
		cu_esp_txi((sint32)esp_state.sntp_enabled);
		cu_esp_txp(",");
		cu_esp_txi((sint32)esp_state.sntp_timezone);
		cu_esp_txp(",\"");
		cu_esp_txp((char*)esp_state.sntp_server[0]);
		cu_esp_txp("\",\"");
		cu_esp_txp((char*)esp_state.sntp_server[1]);
		cu_esp_txp("\",\"");
		cu_esp_txp((char*)esp_state.sntp_server[2]);
		cu_esp_txp("\"\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!e){
		if(strstr(s, "\r\n") != NULL){
			esp_state.sntp_enabled = 0;
			esp_state.sntp_timezone = ESP_DEFAULT_TIMEZONE;
			snprintf((char*)esp_state.sntp_server[0], sizeof(esp_state.sntp_server[0]), "%s", default_sntp_server0);
			snprintf((char*)esp_state.sntp_server[1], sizeof(esp_state.sntp_server[1]), "%s", default_sntp_server1);
			snprintf((char*)esp_state.sntp_server[2], sizeof(esp_state.sntp_server[2]), "%s", default_sntp_server2);
			esp_state.flash_dirty = 1;
			cu_esp_timed_stall(ESP_AT_OK_DELAY);
			cu_esp_txp_ok();
			return;
		}
		cu_esp_txp_error();
		return;
	}

	{
		const sint8 *p = (const sint8*)(e + 1);
		int en = 0;
		sint32 tz = 0;
		sint8 s0[48], s1[48], s2[48];

		s0[0] = s1[0] = s2[0] = 0;

		if(!cu_esp_parse_u16_dec(&p, &en) || (en != 0 && en != 1) || !cu_esp_expect_char(&p, ',')){
			cu_esp_txp_error();
			return;
		}

		if(!cu_esp_parse_s32_dec(&p, &tz)){
			cu_esp_txp_error();
			return;
		}

		p = cu_esp_skip_ws(p);
		if(*p == ','){
			p++;
			if(!cu_esp_parse_quoted_str(&p, s0, (auint)sizeof(s0), 0)){
				cu_esp_txp_error();
				return;
			}

			p = cu_esp_skip_ws(p);
			if(*p == ','){
				p++;
				if(!cu_esp_parse_quoted_str(&p, s1, (auint)sizeof(s1), 0)){
					cu_esp_txp_error();
					return;
				}

				p = cu_esp_skip_ws(p);
				if(*p == ','){
					p++;
					if(!cu_esp_parse_quoted_str(&p, s2, (auint)sizeof(s2), 0)){
						cu_esp_txp_error();
						return;
					}
				}
			}
		}

		if(!cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		if(tz < -11 || tz > 13){
			cu_esp_txp_error();
			return;
		}

		esp_state.sntp_enabled = (uint32)en;
		esp_state.sntp_timezone = (sint32)tz;

		if(s0[0]) snprintf((char*)esp_state.sntp_server[0], sizeof(esp_state.sntp_server[0]), "%s", (char*)s0);
		if(s1[0]) snprintf((char*)esp_state.sntp_server[1], sizeof(esp_state.sntp_server[1]), "%s", (char*)s1);
		if(s2[0]) snprintf((char*)esp_state.sntp_server[2], sizeof(esp_state.sntp_server[2]), "%s", (char*)s2);

		esp_state.flash_dirty = 1;
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp_ok();
	}
}

/* ------------------------------------------------------------------------- */
/* CIPSNTPTIME                                                                */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipsntptime(sint8 *cmd_buf){
	(void)cmd_buf;

	time_t current_time;
	time(&current_time);
	struct tm *curr_t_struct = gmtime(&current_time);

	cu_esp_timed_stall(ESP_SNTP_NET_DELAY);
	snprintf((char *)esp_state.sntp_lasttime[0], sizeof(esp_state.sntp_lasttime[0]), "%s", asctime(curr_t_struct));
	cu_esp_txp((char *)esp_state.sntp_lasttime[0]);
}

/* ------------------------------------------------------------------------- */
/* MDNS (newer AT command)                                                    */
/* ------------------------------------------------------------------------- */
void cu_esp_at_mdns(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		cu_esp_txp("+MDNS:");
		cu_esp_txi((sint32)esp_state.mdns_enable);
		cu_esp_txp(",\"");
		cu_esp_txp(((char*)esp_state.mdns_host));
		cu_esp_txp("\",\"");
		cu_esp_txp(((char*)esp_state.mdns_service));
		cu_esp_txp("\",");
		cu_esp_txi((sint32)esp_state.mdns_port);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	{
		const sint8 *p = (const sint8*)e;
		int en = 0;
		int port = 0;
		sint8 h[64], svc[64];

		h[0] = svc[0] = 0;

		if(!cu_esp_parse_bool01(&p, &en)){
			cu_esp_txp_error();
			return;
		}

		p = cu_esp_skip_ws(p);
		if(*p == ','){
			p++;
			if(!cu_esp_parse_quoted_str(&p, h, (auint)sizeof(h), 0) ||
			   !cu_esp_expect_char(&p, ',') ||
			   !cu_esp_parse_quoted_str(&p, svc, (auint)sizeof(svc), 0) ||
			   !cu_esp_expect_char(&p, ',') ||
			   !cu_esp_parse_u16_dec(&p, &port)){
				cu_esp_txp_error();
				return;
			}
		}

		if(!cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		esp_state.mdns_enable = (uint8)en;
		if(h[0]) snprintf((char*)esp_state.mdns_host, sizeof esp_state.mdns_host, "%s", (char*)h);
		if(svc[0]) snprintf((char*)esp_state.mdns_service, sizeof esp_state.mdns_service, "%s", (char*)svc);
		if(port > 0 && port <= 65535) esp_state.mdns_port = (uint16)port;

		cu_esp_txp_ok();
	}
}

/* ------------------------------------------------------------------------- */
/* SYSADC / SYSRAM / CWLAPOPT / CWHOSTNAME stubs                              */
/* ------------------------------------------------------------------------- */
void cu_esp_at_sysadc(sint8 *cmd_buf){ (void)cmd_buf; }
void cu_esp_at_sysram(sint8 *cmd_buf){
	if(strstr((char*)cmd_buf,"AT+SYSRAM?\r\n")!=(char*)cmd_buf){cu_esp_txp_error();return;}
	cu_esp_txp("+SYSRAM:148408,84044\r\n");
	cu_esp_txp_ok();
}
void cu_esp_at_cwhostname(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(!(esp_state.wifi_mode&ESP_WIFI_MODE_STATION)){cu_esp_txp_error();return;}
	if(q&&(!e||q<e)){cu_esp_txp("+CWHOSTNAME:");cu_esp_txp((char*)esp_state.station_hostname);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;sint8 h[33];if(!cu_esp_parse_quoted_str(&p,h,sizeof(h),0)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}snprintf((char*)esp_state.station_hostname,sizeof(esp_state.station_hostname),"%s",(char*)h);cu_esp_txp_ok();return;}cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* WPS                                                                        */
/* ------------------------------------------------------------------------- */
void cu_esp_at_wps(sint8 *cmd_buf){
	const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');
	int v = 0;

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	if(!cu_esp_parse_u16_dec(&e, &v) || (v != 0 && v != 1) || !cu_esp_expect_crlf(e)){
		cu_esp_txp_error();
		return;
	}

	if(esp_state.wifi_mode != ESP_WIFI_MODE_STATION){
		cu_esp_txp_error();
		return;
	}

	esp_state.wps = (uint8)v;
	cu_esp_txp_ok();
}

void cu_esp_at_wakeupgpio(sint8 *cmd_buf){ (void)cmd_buf; }

/* ------------------------------------------------------------------------- */
/* GPIO SYS*                                                                  */
/* ------------------------------------------------------------------------- */
void cu_esp_at_sysgpioread(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '=');
	int pin = 0;

	if(!p){
		cu_esp_txp_error();
		return;
	}
	p++;

	if(!cu_esp_parse_u16_dec(&p, &pin) || !cu_esp_expect_crlf(p) || pin < 0 || pin >= ESP_GPIO_PIN_COUNT){
		cu_esp_txp_error();
		return;
	}

	if(esp_state.gpio_mode[pin] != 3){
		cu_esp_txp("NOT GPIO MODE!\r\n");
		cu_esp_txp_error();
		return;
	}

	cu_esp_txp("+SYSGPIOREAD:");
	cu_esp_txi(pin);
	cu_esp_txp(",");
	cu_esp_txi((sint32)esp_state.gpio_dir[pin]);
	cu_esp_txp(",");
	cu_esp_txi((sint32)esp_state.gpio_level[pin]);
	cu_esp_txp("\r\n\r\n");
	cu_esp_txp_ok();
}

void cu_esp_at_sysgpiowrite(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '=');
	int pin = 0;
	int val = 0;

	if(!p){
		cu_esp_txp_error();
		return;
	}
	p++;

	if(!cu_esp_parse_u16_dec(&p, &pin) ||
	   !cu_esp_expect_char(&p, ',') ||
	   !cu_esp_parse_bool01(&p, &val) ||
	   !cu_esp_expect_crlf(p) ||
	   pin < 0 || pin >= ESP_GPIO_PIN_COUNT){
		cu_esp_txp_error();
		return;
	}

	if(esp_state.gpio_mode[pin] != 3 || esp_state.gpio_dir[pin] != 1){
		cu_esp_txp_error();
		return;
	}

	esp_state.gpio_level[pin] = (uint8)val;
	cu_esp_txp_ok();
}

void cu_esp_at_sysgpiodir(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '=');
	int pin = 0;
	int dir = 0;

	if(!p){
		cu_esp_txp_error();
		return;
	}
	p++;

	if(!cu_esp_parse_u16_dec(&p, &pin) ||
	   !cu_esp_expect_char(&p, ',') ||
	   !cu_esp_parse_bool01(&p, &dir) ||
	   !cu_esp_expect_crlf(p) ||
	   pin < 0 || pin >= ESP_GPIO_PIN_COUNT){
		cu_esp_txp_error();
		return;
	}

	if(esp_state.gpio_mode[pin] != 3){
		cu_esp_txp("NOT GPIO MODE!\r\n");
		cu_esp_txp_error();
		return;
	}

	esp_state.gpio_dir[pin] = (uint8)dir;
	cu_esp_txp_ok();
}

void cu_esp_at_sysiogetcfg(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '=');
	int pin = 0;

	if(!p){
		cu_esp_txp_error();
		return;
	}
	p++;

	if(!cu_esp_parse_u16_dec(&p, &pin) || !cu_esp_expect_crlf(p) || pin < 0 || pin >= ESP_GPIO_PIN_COUNT){
		cu_esp_txp_error();
		return;
	}

	cu_esp_txp("+SYSIOGETCFG:");
	cu_esp_txi(pin);
	cu_esp_txp(",");
	cu_esp_txi((sint32)esp_state.gpio_mode[pin]);
	cu_esp_txp(",");
	cu_esp_txi((sint32)esp_state.gpio_pullup[pin]);
	cu_esp_txp("\r\n\r\n");
	cu_esp_txp_ok();
}

void cu_esp_at_sysiosetcfg(sint8 *cmd_buf){
	const sint8 *p = (const sint8*)strchr((const char*)cmd_buf, '=');
	int pin = 0;
	int mode = 0;
	int pull = 0;

	if(!p){
		cu_esp_txp_error();
		return;
	}
	p++;

	if(!cu_esp_parse_u16_dec(&p, &pin) ||
	   !cu_esp_expect_char(&p, ',') ||
	   !cu_esp_parse_u16_dec(&p, &mode) ||
	   !cu_esp_expect_char(&p, ',') ||
	   !cu_esp_parse_u16_dec(&p, &pull) ||
	   !cu_esp_expect_crlf(p) ||
	   pin < 0 || pin >= ESP_GPIO_PIN_COUNT ||
	   mode < 0 || mode > 3 ||
	   pull < 0 || pull > 1){
		cu_esp_txp_error();
		return;
	}

	esp_state.gpio_mode[pin] = (uint8)mode;
	esp_state.gpio_pullup[pin] = (uint8)pull;
	if(mode == 3)
		esp_state.gpio_dir[pin] = 0;

	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* CIPBUFRECVMODE / CIPBUFRECVLEN / CIPBUFRECVDATA                            */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipbufrecvmode(sint8 *cmd_buf){
	cu_state_esp_t *es = cu_esp_get_state();
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		cu_esp_txp("+CIPBUFRECVMODE:");
		cu_esp_txi((sint32)es->cipbuf_recv_mode);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(e){
		const sint8 *p = (const sint8*)(e + 1);
		int mode = 0;
		if(!cu_esp_parse_bool01(&p, &mode) || !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}
		es->cipbuf_recv_mode = (uint8)mode;
		cu_esp_cipbuf_clear_all(es);
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

void cu_esp_at_cipbufrecvlen(sint8 *cmd_buf){
	cu_state_esp_t *es = cu_esp_get_state();
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		if(es->state & ESP_MUX){
			for(uint8 i = 0; i < ESP_CIPBUF_MAX_LINKS; i++){
				uint16 n = cu_esp_cipbuf_avail(es, i);
				if(!n) continue;
				cu_esp_txp("+CIPBUFRECVLEN:");
				cu_esp_txi((sint32)i);
				cu_esp_txp(",");
				cu_esp_txi((sint32)n);
				cu_esp_txp("\r\n");
			}
			cu_esp_txp_ok();
			return;
		}else{
			uint16 n = cu_esp_cipbuf_avail(es, 0);
			cu_esp_txp("+CIPBUFRECVLEN:");
			cu_esp_txi((sint32)n);
			cu_esp_txp("\r\n");
			cu_esp_txp_ok();
			return;
		}
	}

	if((es->state & ESP_MUX) && e){
		const sint8 *p = (const sint8*)(e + 1);
		auint id = 0;
		if(!cu_esp_parse_u32_8(p, &id, &p) || !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}
		if(id >= ESP_CIPBUF_MAX_LINKS){
			cu_esp_txp_error();
			return;
		}
		cu_esp_txp("+CIPBUFRECVLEN:");
		cu_esp_txi((sint32)id);
		cu_esp_txp(",");
		cu_esp_txi((sint32)cu_esp_cipbuf_avail(es, (uint8)id));
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

void cu_esp_at_cipbufrecvdata(sint8 *cmd_buf){
	cu_state_esp_t *es = cu_esp_get_state();
	const sint8 *s = (const sint8*)cmd_buf;
	const sint8 *e = (const sint8*)strchr((const char*)s, '=');

	if(!es->cipbuf_recv_mode){
		cu_esp_txp_error();
		return;
	}
	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	if(es->state & ESP_MUX){
		const sint8 *p = e;
		auint id = 0, req = 0;
		uint16 avail, take;
		uint8 tmp[256];

		if(!cu_esp_parse_u32_8(p, &id, &p) || !cu_esp_expect_char(&p, ',')){
			cu_esp_txp_error();
			return;
		}
		if(!cu_esp_parse_u32_8(p, &req, &p) || !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		if(id >= ESP_CIPBUF_MAX_LINKS || req == 0u){
			cu_esp_txp_error();
			return;
		}

		avail = cu_esp_cipbuf_avail(es, (uint8)id);
		take = (uint16)req;
		if(take > avail) take = avail;
		if(take > (uint16)sizeof(tmp)) take = (uint16)sizeof(tmp);

		take = cu_esp_cipbuf_pop(es, (uint8)id, tmp, take);

		cu_esp_txp("+CIPBUFRECVDATA,");
		cu_esp_txi((sint32)id);
		cu_esp_txp(",");
		cu_esp_txi((sint32)take);
		cu_esp_txp(":");
		if(take)
			cu_esp_txl((const char*)tmp, (auint)take);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}else{
		const sint8 *p = e;
		auint req = 0;
		uint16 avail, take;
		uint8 tmp[256];

		if(!cu_esp_parse_u32_8(p, &req, &p) || !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		if(req == 0u){
			cu_esp_txp_error();
			return;
		}

		avail = cu_esp_cipbuf_avail(es, 0);
		take = (uint16)req;
		if(take > avail) take = avail;
		if(take > (uint16)sizeof(tmp)) take = (uint16)sizeof(tmp);

		take = cu_esp_cipbuf_pop(es, 0, tmp, take);

		cu_esp_txp("+CIPBUFRECVDATA,");
		cu_esp_txi((sint32)take);
		cu_esp_txp(":");
		if(take)
			cu_esp_txl((const char*)tmp, (auint)take);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}
}

/* ------------------------------------------------------------------------- */
/* CIPRECV* aliases (newer naming)                                            */
/* ------------------------------------------------------------------------- */
void cu_esp_at_ciprecvmode(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp("+CIPRECVMODE:");cu_esp_txi(esp_state.cipbuf_recv_mode);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;int v;if(!cu_esp_parse_bool01(&p,&v)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.cipbuf_recv_mode=(uint8)v;cu_esp_cipbuf_clear_all(&esp_state);cu_esp_txp_ok();return;}cu_esp_txp_error();
}

void cu_esp_at_ciprecvlen(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		if(esp_state.state&ESP_MUX){cu_esp_txp("+CIPRECVLEN:");for(uint8 i=0;i<ESP_MAX_LINKS;i++){if(i)cu_esp_txp(",");uint16 n=cu_esp_cipbuf_avail(&esp_state,i);if(n)cu_esp_txi(n);}cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
		cu_esp_txp("+CIPRECVLEN:");cu_esp_txi(cu_esp_cipbuf_avail(&esp_state,0));cu_esp_txp("\r\n");cu_esp_txp_ok();return;
	}
	if(e&&(esp_state.state&ESP_MUX)){const sint8*p=(const sint8*)e+1;auint id;if(!cu_esp_parse_u32_8(p,&id,&p)||id>=ESP_MAX_LINKS||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}cu_esp_txp("+CIPRECVLEN:");cu_esp_txi(id);cu_esp_txp(",");cu_esp_txi(cu_esp_cipbuf_avail(&esp_state,(uint8)id));cu_esp_txp("\r\n");cu_esp_txp_ok();return;}cu_esp_txp_error();
}

void cu_esp_at_ciprecvdata(sint8 *cmd_buf){
	const char*e=strchr((char*)cmd_buf,'=');if(!e||!esp_state.cipbuf_recv_mode){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;auint id=0,req;
	if(esp_state.state&ESP_MUX){if(!cu_esp_parse_u32_8(p,&id,&p)||id>=ESP_MAX_LINKS||!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}}
	if(!cu_esp_parse_u32_8(p,&req,&p)||req==0||req>ESP_CIPBUF_RX_CAP||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}uint16 take=(uint16)req,av=cu_esp_cipbuf_avail(&esp_state,(uint8)id);if(take>av)take=av;uint8 tmp[ESP_CIPBUF_RX_CAP];take=cu_esp_cipbuf_pop(&esp_state,(uint8)id,tmp,take);cu_esp_txp("+CIPRECVDATA:");cu_esp_txi(take);cu_esp_txp(",");if(esp_state.state&ESP_CIPDINFO){char ip[80]="0.0.0.0";uint16 pt=0;cu_esp_net_get_peer_text((uint32)id,ip,sizeof(ip),&pt);cu_esp_txp("\"");cu_esp_txp(ip);cu_esp_txp("\",");cu_esp_txi(pt);cu_esp_txp(",");}if(take)cu_esp_txl((char*)tmp,take);cu_esp_txp("\r\nOK\r\n");
}

/* ------------------------------------------------------------------------- */
/* SSL config commands (newer AT command stubs + stored values)               */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipsslconf(sint8 *cmd_buf){
	cu_esp_at_cipsslcconf(cmd_buf);
}

void cu_esp_at_cipsslsize(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');
	if(q && (!e || q < e)){
		cu_esp_txp("+CIPSSLSIZE:");
		cu_esp_txi((sint32)esp_state.ssl_rx_buf_size);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}
	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;
	{
		const sint8 *p = (const sint8*)e;
		int v = 0;
		if(!cu_esp_parse_u16_dec(&p, &v) || !cu_esp_expect_crlf(p) || v < 2048 || v > 16384){
			cu_esp_txp_error();
			return;
		}
		esp_state.ssl_rx_buf_size = (uint16)v;
		cu_esp_txp_ok();
	}
}

void cu_esp_at_cipsslcconf(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){for(uint8 i=0;i<cu_esp_ssl_query_links();i++){cu_esp_txp("+CIPSSLCCONF:");cu_esp_txi(i);cu_esp_txp(",");cu_esp_txi(esp_state.ssl_auth_mode[i]);cu_esp_txp(",");cu_esp_txi(esp_state.ssl_pki_num[i]);cu_esp_txp(",");cu_esp_txi(esp_state.ssl_ca_num[i]);cu_esp_txp("\r\n");}cu_esp_txp_ok();return;}
	if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;uint8 target=0;int auth=0,pki=0,ca=0;
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_u16_dec(&p,&auth)||auth<0||auth>3){cu_esp_txp_error();return;}
	if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&pki)||pki<0||pki>=(int)ESP_SSL_SLOT_COUNT){cu_esp_txp_error();return;}}if(*p==','){p++;if(*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&ca)||ca<0||ca>=(int)ESP_SSL_SLOT_COUNT){cu_esp_txp_error();return;}}}}
	if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++){esp_state.ssl_auth_mode[i]=(uint8)auth;esp_state.ssl_pki_num[i]=(uint8)pki;esp_state.ssl_ca_num[i]=(uint8)ca;}
	cu_esp_at23_store_if_enabled();cu_esp_txp_ok();
}

void cu_esp_at_cipsslcsni(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){for(uint8 i=0;i<cu_esp_ssl_query_links();i++){cu_esp_txp("+CIPSSLCSNI:");cu_esp_txi(i);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.ssl_sni[i]);cu_esp_txp("\"\r\n");}cu_esp_txp_ok();return;}
	if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;uint8 target=0;sint8 value[ESP_SSL_SNI_MAX];
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_quoted_str0(&p,value,sizeof(value))||strlen((char*)value)>64u||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++)snprintf((char*)esp_state.ssl_sni[i],sizeof(esp_state.ssl_sni[i]),"%s",(char*)value);
	cu_esp_txp_ok();
}

void cu_esp_at_cipsslccn(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){for(uint8 i=0;i<cu_esp_ssl_query_links();i++){cu_esp_txp("+CIPSSLCCN:");cu_esp_txi(i);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.ssl_common_name[i]);cu_esp_txp("\"\r\n");}cu_esp_txp_ok();return;}
	if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;uint8 target=0;sint8 value[65];
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_quoted_str0(&p,value,sizeof(value))||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++)snprintf((char*)esp_state.ssl_common_name[i],sizeof(esp_state.ssl_common_name[i]),"%s",(char*)value);
	cu_esp_txp_ok();
}

void cu_esp_at_cipsslcalpn(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){for(uint8 i=0;i<cu_esp_ssl_query_links();i++){const sint8*a[5]={esp_state.ssl_alpn0[i],esp_state.ssl_alpn1[i],esp_state.ssl_alpn2[i],esp_state.ssl_alpn3[i],esp_state.ssl_alpn4[i]};cu_esp_txp("+CIPSSLCALPN:");cu_esp_txi(i);for(uint8 j=0;j<esp_state.ssl_alpn_count[i]&&j<5u;j++){cu_esp_txp(",\"");cu_esp_txp((char*)a[j]);cu_esp_txp("\"");}cu_esp_txp("\r\n");}cu_esp_txp_ok();return;}
	if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;uint8 target=0;int cnt=0;sint8 vals[5][32];memset(vals,0,sizeof(vals));
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_u16_dec(&p,&cnt)||cnt<0||cnt>5){cu_esp_txp_error();return;}for(int j=0;j<cnt;j++){if(!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,vals[j],sizeof(vals[j]),0)){cu_esp_txp_error();return;}}if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++){sint8*a[5]={esp_state.ssl_alpn0[i],esp_state.ssl_alpn1[i],esp_state.ssl_alpn2[i],esp_state.ssl_alpn3[i],esp_state.ssl_alpn4[i]};esp_state.ssl_alpn_count[i]=(uint8)cnt;for(uint8 j=0;j<5u;j++){a[j][0]=0;if(j<(uint8)cnt)snprintf((char*)a[j],32,"%s",(char*)vals[j]);}}
	cu_esp_txp_ok();
}

void cu_esp_at_cipsslcpsk(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){for(uint8 i=0;i<cu_esp_ssl_query_links();i++){cu_esp_txp("+CIPSSLCPSK:");cu_esp_txi(i);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.ssl_psk_id[i]);cu_esp_txp("\",\"");cu_esp_txp((char*)esp_state.ssl_psk_key[i]);cu_esp_txp("\"\r\n");}cu_esp_txp_ok();return;}
	if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;uint8 target=0;sint8 psk[33],hint[33];
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_quoted_str0(&p,psk,sizeof(psk))||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,hint,sizeof(hint))||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++){
		size_t n=strlen((char*)psk);
		snprintf((char*)esp_state.ssl_psk_id[i],sizeof(esp_state.ssl_psk_id[i]),"%s",(char*)psk);
		snprintf((char*)esp_state.ssl_psk_key[i],sizeof(esp_state.ssl_psk_key[i]),"%s",(char*)hint);
		memset(esp_state.ssl_psk_bin[i],0,sizeof(esp_state.ssl_psk_bin[i]));
		memcpy(esp_state.ssl_psk_bin[i],psk,n);
		esp_state.ssl_psk_bin_len[i]=(uint8)n;
	}
	cu_esp_txp_ok();
}

void cu_esp_at_cipsslcpskhex(sint8 *cmd_buf){
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		for(uint8 i=0;i<cu_esp_ssl_query_links();i++){
			char hex[65]; static const char hd[]="0123456789ABCDEF"; uint8 n=esp_state.ssl_psk_bin_len[i];
			for(uint8 j=0;j<n;j++){hex[j*2]=hd[esp_state.ssl_psk_bin[i][j]>>4];hex[j*2+1]=hd[esp_state.ssl_psk_bin[i][j]&15u];}
			hex[(size_t)n*2u]=0;
			cu_esp_txp("+CIPSSLCPSKHEX:");cu_esp_txi(i);cu_esp_txp(",\"");cu_esp_txp(hex);cu_esp_txp("\",\"");cu_esp_txp((char*)esp_state.ssl_psk_key[i]);cu_esp_txp("\"\r\n");
		}
		cu_esp_txp_ok();return;
	}
	if(!e){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1;uint8 target=0;sint8 hex[65],hint[33];uint8 bin[32];uint8 n=0;
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_quoted_str0(&p,hex,sizeof(hex))||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,hint,sizeof(hint))||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	size_t hl=strlen((char*)hex);if((hl&1u)!=0u||hl>64u){cu_esp_txp_error();return;}
	for(size_t j=0;j<hl;j+=2u){int hi,lo;char a=(char)hex[j],b=(char)hex[j+1u];hi=(a>='0'&&a<='9')?a-'0':(a>='a'&&a<='f')?a-'a'+10:(a>='A'&&a<='F')?a-'A'+10:-1;lo=(b>='0'&&b<='9')?b-'0':(b>='a'&&b<='f')?b-'a'+10:(b>='A'&&b<='F')?b-'A'+10:-1;if(hi<0||lo<0){cu_esp_txp_error();return;}bin[n++]=(uint8)((hi<<4)|lo);}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++){
		memset(esp_state.ssl_psk_bin[i],0,sizeof(esp_state.ssl_psk_bin[i]));memcpy(esp_state.ssl_psk_bin[i],bin,n);esp_state.ssl_psk_bin_len[i]=n;
		/* Keep the human-readable PSK slot empty when binary data cannot be
		 * represented faithfully by CIPSSLCPSK. */
		esp_state.ssl_psk_id[i][0]=0;snprintf((char*)esp_state.ssl_psk_key[i],sizeof(esp_state.ssl_psk_key[i]),"%s",(char*)hint);
	}
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* RFPOWER (rewritten concise)                                                */
/* ------------------------------------------------------------------------- */

void cu_esp_at_rfpower(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		cu_esp_txp("+RFPOWER:");
		cu_esp_txi((sint32)esp_state.rf_power);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
		return;
	}

	if(e){
		auint v = (auint)atoi(e + 1);
		if(v > 82u){
			cu_esp_txp_error();
			return;
		}
		esp_state.rf_power = (uint8)v;
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

/* ------------------------------------------------------------------------- */
/* RFVDD                                                                      */
/* ------------------------------------------------------------------------- */
void cu_esp_at_rfvdd(sint8 *cmd_buf){
	const char *s = (const char *)cmd_buf;
	const char *eq;

	if(strchr(s, '?') != NULL){
		cu_esp_txp_error();
		return;
	}

	eq = strchr(s, '=');
	if(eq != NULL){
		sint32 vdd = (sint32)cu_esp_atoi((char *)(eq + 1));
		if(vdd < 1900 || vdd > 3300){
			cu_esp_txp_error();
			return;
		}
		esp_state.vdd33 = (uint16)vdd;
		cu_esp_txp_ok();
		return;
	}

	{
		uint16 vdd = esp_state.vdd33 ? esp_state.vdd33 : 3300;
		cu_esp_txp("+RFVDD:");
		cu_esp_txi((sint32)vdd);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
	}
}

/* ------------------------------------------------------------------------- */
/* RESTORE                                                                    */
/* ------------------------------------------------------------------------- */
void cu_esp_at_restore(sint8 *cmd_buf){
	(void)cmd_buf;
	cu_esp_reset_factory();
	cu_esp_save_config();
	cu_esp_txp_ok();
	cu_esp_at_rst();
}

/* ------------------------------------------------------------------------- */
/* CIPDINFO                                                                   */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cipdinfo(sint8 *cmd_buf){
	const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');
	int v = 0;

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	if(!cu_esp_parse_bool01(&e, &v) || !cu_esp_expect_crlf(e)){
		cu_esp_txp_error();
		return;
	}

	if(v == 0)
		esp_state.state &= ~ESP_CIPDINFO;
	else
		esp_state.state |= ESP_CIPDINFO;

	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* PING                                                                       */
/* ------------------------------------------------------------------------- */
void cu_esp_at_ping(sint8 *cmd_buf){
	sint8 *p = cmd_buf + 8; /* "AT+PING=" */
	char host[256];
	sint16 timeout_ms = 5000;
	int i = 0;

	while(*p == ' ')
		p++;

	if(*p != '"'){
		cu_esp_txp_error();
		return;
	}
	p++;

	while(*p && *p != '"' && i < (int)(sizeof(host) - 1))
		host[i++] = *p++;
	host[i] = '\0';

	if(*p != '"' || i == 0){
		cu_esp_txp_error();
		return;
	}
	p++;

	while(*p == ' ')
		p++;

	if(*p == ','){
		sint32 t = 0;
		p++;
		while(*p == ' ')
			p++;
		if(*p < '0' || *p > '9'){
			cu_esp_txp_error();
			return;
		}
		while(*p >= '0' && *p <= '9'){
			t = t * 10 + (*p - '0');
			if(t > 65535){
				t = 65535;
				break;
			}
			p++;
		}
		timeout_ms = (sint16)t;
	}

	if(!cu_esp_expect_crlf(p)){
		cu_esp_txp_error();
		return;
	}

	if(cu_esp_ping_async_start(host, timeout_ms) != 0){
		cu_esp_txp_error();
		return;
	}
}

/* ------------------------------------------------------------------------- */
/* CWAUTOCONN                                                                 */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwautoconn(sint8 *cmd_buf){
	const sint8 *e = (const sint8*)strchr((const char*)cmd_buf, '=');
	int v = 0;

	if(!e){
		cu_esp_txp_error();
		return;
	}
	e++;

	if(!cu_esp_parse_bool01(&e, &v) || !cu_esp_expect_crlf(e)){
		cu_esp_txp_error();
		return;
	}

	if(v == 0)
		esp_state.state &= ~ESP_AUTOCONNECT;
	else
		esp_state.state |= ESP_AUTOCONNECT;

	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* CWSTARTSMART / CWSTOPSMART                                                 */
/* ------------------------------------------------------------------------- */
void cu_esp_at_cwstartsmart(sint8 *cmd_buf){
	(void)cmd_buf;
	esp_state.state |= ESP_SMARTCONFIG_ACTIVE;
	cu_esp_txp_ok();
}

void cu_esp_at_cwstopsmart(sint8 *cmd_buf){
	(void)cmd_buf;
	esp_state.state &= ~ESP_SMARTCONFIG_ACTIVE;
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* CIUPDATE (stub)                                                            */
/* ------------------------------------------------------------------------- */
void cu_esp_at_ciupdate(sint8 *cmd_buf){
	(void)cmd_buf;

	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY / 4);
	cu_esp_txp("1: found server\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY / 10);
	cu_esp_txp("2: connect server\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY / 8);
	cu_esp_txp("3: got edition\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY / 10);
	cu_esp_txp("4: start update\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY * 5);
	cu_esp_at_rst();
}

/* ------------------------------------------------------------------------- */
/* RST                                                                        */
/* ------------------------------------------------------------------------- */
void cu_esp_at_rst(void){
	esp_state.state &= ~ESP_DID_FIRST_TICK;
	esp_state.wifi_timer = 1;

	esp_state.wifi_pci_en = 0;
	esp_state.wifi_reconn_interval = ESP_DEFAULT_RECONN_INTERVAL;
	esp_state.wifi_listen_interval = ESP_DEFAULT_LISTEN_INTERVAL;
	esp_state.wifi_scan_mode = ESP_DEFAULT_SCAN_MODE;
	esp_state.wifi_jap_timeout = ESP_DEFAULT_JAP_TIMEOUT;
	esp_state.wifi_pmf = ESP_DEFAULT_PMF;

	esp_state.country_policy = ESP_DEFAULT_COUNTRY_POLICY;
	esp_state.country_start_ch = ESP_DEFAULT_COUNTRY_START_CH;
	esp_state.country_count = ESP_DEFAULT_COUNTRY_COUNT;

	esp_state.cipbuf_recv_mode = 0;
	cu_esp_cipbuf_clear_all(&esp_state);

	esp_state.state &= ~ESP_MUX;
	memset(esp_state.link_is_ssl, 0, sizeof(esp_state.link_is_ssl));
	/* CIPSSLCCONF is persistent state. Keep the active values across
	 * AT+RST; RAM-only SSL decorations below are reset. */
	memset(esp_state.ssl_sni, 0, sizeof(esp_state.ssl_sni));
	memset(esp_state.ssl_common_name, 0, sizeof(esp_state.ssl_common_name));
	memset(esp_state.ssl_alpn_count, 0, sizeof(esp_state.ssl_alpn_count));
	memset(esp_state.ssl_alpn0, 0, sizeof(esp_state.ssl_alpn0));
	memset(esp_state.ssl_alpn1, 0, sizeof(esp_state.ssl_alpn1));
	memset(esp_state.ssl_alpn2, 0, sizeof(esp_state.ssl_alpn2));
	memset(esp_state.ssl_alpn3, 0, sizeof(esp_state.ssl_alpn3));
	memset(esp_state.ssl_alpn4, 0, sizeof(esp_state.ssl_alpn4));
	memset(esp_state.ssl_psk_id, 0, sizeof(esp_state.ssl_psk_id));
	memset(esp_state.ssl_psk_key, 0, sizeof(esp_state.ssl_psk_key));
	memset(esp_state.ssl_psk_bin, 0, sizeof(esp_state.ssl_psk_bin));
	memset(esp_state.ssl_psk_bin_len, 0, sizeof(esp_state.ssl_psk_bin_len));
	memset(esp_state.ssl_ca_path, 0, sizeof(esp_state.ssl_ca_path));
	memset(esp_state.ssl_pki_cert_path, 0, sizeof(esp_state.ssl_pki_cert_path));
	memset(esp_state.ssl_pki_key_path, 0, sizeof(esp_state.ssl_pki_key_path));
	esp_state.server_max_conn = ESP_MAX_LINKS;

	if(esp_state.listen_socket != ESP_INVALID_SOCKET){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.listen_socket);
#else
		close(esp_state.listen_socket);
#endif
		esp_state.listen_socket = ESP_INVALID_SOCKET;
	}

	for(uint32 i = 0; i < ESP_MAX_LINKS; i++){
		cu_esp_close_socket(i);
		cu_esp_cipbuf_clear_link(&esp_state, (uint8)i);
		esp_state.link_is_ssl[i] = 0;
	}

	cu_esp_lan_on_cwmode_change(&esp_state);
	cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* GMR                                                                        */
/* ------------------------------------------------------------------------- */
void cu_esp_at_gmr(void){
	if(cu_esp_at23_enabled()){
		cu_esp_txp("AT version:2.3.0.0(CUzeBox - ESP8266)\r\n");
		cu_esp_txp("SDK version:v3.4-CUzeBox\r\n");
		cu_esp_txp("compile time:Aug 24 2026\r\n");
		cu_esp_txp("Bin version:2.3.0.0(WROOM-02 compatible)\r\n\r\nOK\r\n");
	}else cu_esp_txp(at_gmr_string);
}

/* ------------------------------------------------------------------------- */
/* Baud helpers                                                               */
/* ------------------------------------------------------------------------- */
static auint cu_esp_set_baud_rate(auint baud){
	if(!baud)
		return 1;

	auint denom = baud * 8u;
	auint ubrr;

	if(denom < baud)
		return 1;

	ubrr = ((ESP_UZEBOX_CORE_FREQUENCY + (baud * 4u)) / denom);
	if(!ubrr)
		return 1;
	ubrr -= 1u;

	if(ubrr > 0x0FFFu)
		return 1;

	esp_state.baud_rate = baud;
	esp_state.baud_divisor = ubrr;
	esp_state.uart_baud_bits_module_default = ubrr;
	esp_state.uart_baud_bits_module = ubrr;
	return 0;
}

void cu_esp_at_ciobaud(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *e = strchr(s, '=');
	auint baud;

	if(!e){
		cu_esp_txp_error();
		return;
	}

	baud = (auint)atoi(e + 1);

	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	if(cu_esp_set_baud_rate(baud))
		cu_esp_txp_error();
	else
		cu_esp_txp_ok();
}

/* ------------------------------------------------------------------------- */
/* UART / UART_CUR / UART_DEF (fixed)                                         */
/* ------------------------------------------------------------------------- */
static void cu_esp_uart_emit(void){
	cu_esp_txp("+UART_CUR:");
	cu_esp_txi((sint32)esp_state.baud_rate);
	cu_esp_txp(",8,1,0,0\r\n");
	cu_esp_txp_ok();
}

void cu_esp_at_uart(sint8 *cmd_buf){
	const char *s = (const char*)cmd_buf;
	const char *q = strchr(s, '?');
	const char *e = strchr(s, '=');

	if(q && (!e || q < e)){
		cu_esp_uart_emit();
		return;
	}

	if(e){
		const sint8 *p = (const sint8*)(e + 1);
		auint baud = 0;
		int db = 0, sb = 0, par = 0, flow = 0;

		if(!cu_esp_parse_u32_8(p, &baud, &p)){
			cu_esp_txp_error();
			return;
		}
		if(!cu_esp_expect_char(&p, ',') || !cu_esp_parse_u16_dec(&p, &db) ||
		   !cu_esp_expect_char(&p, ',') || !cu_esp_parse_u16_dec(&p, &sb) ||
		   !cu_esp_expect_char(&p, ',') || !cu_esp_parse_u16_dec(&p, &par) ||
		   !cu_esp_expect_char(&p, ',') || !cu_esp_parse_u16_dec(&p, &flow) ||
		   !cu_esp_expect_crlf(p)){
			cu_esp_txp_error();
			return;
		}

		if(db != 8 || sb != 1 || par != 0){
			cu_esp_txp_error();
			return;
		}

		if(cu_esp_set_baud_rate(baud)){
			cu_esp_txp_error();
			return;
		}

		esp_state.flash_dirty = 1;
		cu_esp_txp_ok();
		return;
	}

	cu_esp_txp_error();
}

void cu_esp_at_uart_cur(sint8 *cmd_buf){ cu_esp_at_uart(cmd_buf); }
void cu_esp_at_uart_def(sint8 *cmd_buf){ cu_esp_at_uart(cmd_buf); }

/* ------------------------------------------------------------------------- */
/* +IPD header + cu_esp_process_ipd()                                         */
/* ------------------------------------------------------------------------- */
static void cu_esp_tx_ipd_hdr(cu_state_esp_t *es, uint8 link, uint16 len, uint8 with_colon){
	cu_esp_txp("+IPD,");
	if(es->state & ESP_MUX){
		cu_esp_txi((sint32)link);
		cu_esp_txp(",");
	}
	cu_esp_txi((sint32)len);

	if(es->state & ESP_CIPDINFO){
		const char *rip = inet_ntoa(es->sock_info[link].sin_addr);
		uint16 rport = (uint16)ntohs(es->sock_info[link].sin_port);

		cu_esp_txp(",\"");
		cu_esp_txp(rip ? rip : "0.0.0.0");
		cu_esp_txp("\",");
		cu_esp_txi((sint32)rport);
	}

	if(with_colon)
		cu_esp_txp(":");
	else
		cu_esp_txp("\r\n");
}

static sint32 cu_esp_ipd_budget_bytes(void)
{
	switch(esp_state.uart_profile){
		case CU_UART_PROFILE_FAST:
			return 1024;
		case CU_UART_PROFILE_DEBUG:
			return 4096;
		case CU_UART_PROFILE_BALANCED:
		default:
			return 2048;
	}
}

sint32 cu_esp_process_ipd(void){
	cu_state_esp_t *es = cu_esp_get_state();
	sint32 ret = 0;
	sint32 budget = cu_esp_ipd_budget_bytes();
	uint32 ready_mask = cu_esp_net_rx_ready_mask();

	for(sint32 i = 0; i < (sint32)ESP_MAX_LINKS && budget > 0; i++){

		if((ready_mask & (1u << (uint32)i)) == 0u)
			continue;
		if(es->socks[i] == ESP_INVALID_SOCKET && !cu_esp_lan_virtual_link_present(es, (uint32)i))
			continue;

		if(es->user_input_mode == ESP_USER_MODE_UNVARNISHED && i > 0)
			break;

		sint32 read_len = budget;
		if(read_len > (sint32)sizeof(es->rx_packet))
			read_len = (sint32)sizeof(es->rx_packet);
		sint32 num_bytes = cu_esp_net_recv((uint32)i, es->rx_packet, read_len, 0);

		if(num_bytes == ESP_SOCKET_ERROR){
			auint last_error = (auint)cu_esp_get_last_error();

			if(last_error != ESP_WOULD_BLOCK && last_error != ESP_EAGAIN){
				print_error("ESP Socket Error, terminating connection %d: %d\n", (int)i, (int)last_error);
				cu_esp_tx_evt_closed((uint8)i);
				cu_esp_close_socket((uint32)i);
				cu_esp_cipbuf_clear_link(es, (uint8)i);
				esp_state.link_is_ssl[i] = 0;
				return 0;
			}
			continue;
		}

		if(num_bytes == 0){
			cu_esp_tx_evt_closed((uint8)i);
			cu_esp_close_socket((uint32)i);
			cu_esp_cipbuf_clear_link(es, (uint8)i);
			esp_state.link_is_ssl[i] = 0;
			return 0;
		}

		if(num_bytes < 0)
			continue;

		ret = num_bytes;
		if(num_bytes >= budget)
			budget = 0;
		else
			budget -= num_bytes;

		if(es->user_input_mode == ESP_USER_MODE_UNVARNISHED){
			cu_esp_txl((const char *)es->rx_packet, (auint)num_bytes);
			continue;
		}

		if(es->cipbuf_recv_mode){
			uint16 pushed = cu_esp_cipbuf_push(es, (uint8)i, (const uint8*)es->rx_packet, (uint16)num_bytes);
			if(pushed)
				cu_esp_tx_ipd_hdr(es, (uint8)i, pushed, 0);
			continue;
		}

		{
			sint32 off = 0;
			while(off < num_bytes){
				uint16 chunk = (uint16)(num_bytes - off);
				if(chunk > 256u) chunk = 256u;

				cu_esp_tx_ipd_hdr(es, (uint8)i, chunk, 1);
				cu_esp_txl((const char *)&es->rx_packet[off], (auint)chunk);

				off += (sint32)chunk;
			}
		}
	}

	return ret;
}

/* ------------------------------------------------------------------------- */
/* Transparent send helper                                                    */
/* ------------------------------------------------------------------------- */
void cu_esp_net_send_unvarnished(sint8 *buf, auint len){
	cu_state_esp_t *es = cu_esp_get_state();

	if(cu_esp_net_send(0, buf, (sint32)len, 0) == ESP_SOCKET_ERROR){
		print_error("ESP ERROR cu_esp_net_send() failed: %d\n", (int)cu_esp_get_last_error());
		es->socks[0] = ESP_INVALID_SOCKET;
		esp_state.link_is_ssl[0] = 0;
		cu_esp_txp_error();
		cu_esp_txp("UNLINKED\r\n");
	}
}

/* ------------------------------------------------------------------------- */
/* Stall / TX buffer                                                          */
/* ------------------------------------------------------------------------- */
void cu_esp_timed_stall(auint cycles){
	uint8 i;
	for(i = 0; i < (uint8)CU_ARRLEN(esp_state.delay_pos); i++){
		if(!esp_state.delay_len[i])
			break;
	}

	if(i == (uint8)CU_ARRLEN(esp_state.delay_pos)){
		cu_esp_txp_error();
		cu_esp_at_bad_command();
		return;
	}

	esp_state.delay_len[i] = cycles;
	esp_state.delay_pos[i] = esp_state.read_buf_pos_in;
}

void cu_esp_txp(const char *s){
	cu_esp_txl(s, (auint)strlen(s));
}

void cu_esp_txl(const char *s, auint len){
	auint buf_sz = (auint)sizeof(esp_state.read_buf);

	auint in = esp_state.read_buf_pos_in;
	auint out = esp_state.read_buf_pos_out;
	auint used = (in >= out) ? (in - out) : ((buf_sz - out) + in);
	auint free = (buf_sz - 1u) - used;

	if(len > free){
		print_message("<<<<BUFFER OVERFLOW>>>>\n");
		esp_state.read_buf_pos_in = esp_state.read_buf_pos_out = 0UL;
		cu_esp_txp_error();
		return;
	}

	while(len){
		auint chunk = buf_sz - esp_state.read_buf_pos_in;
		if(chunk > len)
			chunk = len;

		memcpy((char *)esp_state.read_buf + esp_state.read_buf_pos_in, s, chunk);
		esp_state.read_buf_pos_in += chunk;
		if(esp_state.read_buf_pos_in >= buf_sz)
			esp_state.read_buf_pos_in = 0;

		s += chunk;
		len -= chunk;
	}
}

void cu_esp_txi(sint32 i){
	char buf[12];
	char *p = &buf[sizeof(buf)];
	uint32 v;

	/* Avoid libc printf machinery on common +IPD/status/MQTT integer fields. */
	if(i < 0)
		v = (uint32)(-(i + 1)) + 1u;
	else
		v = (uint32)i;

	do{
		*--p = (char)('0' + (v % 10u));
		v /= 10u;
	}while(v != 0u);

	if(i < 0)
		*--p = '-';
	cu_esp_txl(p, (auint)(&buf[sizeof(buf)] - p));
}

void cu_esp_txp_error(void){ cu_esp_txp("ERROR\r\n"); }
void cu_esp_txp_ok(void){ cu_esp_txp("OK\r\n"); }
static void cu_esp_txp_busy_p(void){ cu_esp_txp("busy p...\r\n"); }

#include "cu_esp_at23.inc"

/* ------------------------------------------------------------------------- */
/* Exact command dispatcher                                                   */
/* ------------------------------------------------------------------------- */
typedef enum{
	CU_AT_ID_NONE = 0,
	CU_AT_ID_CIFSR,
	CU_AT_ID_CIOBAUD,
	CU_AT_ID_CIPAP,
	CU_AT_ID_CIPAPMAC,
	CU_AT_ID_CIPBUFRECVDATA,
	CU_AT_ID_CIPBUFRECVLEN,
	CU_AT_ID_CIPBUFRECVMODE,
	CU_AT_ID_CIPCLOSE,
	CU_AT_ID_CIPDINFO,
	CU_AT_ID_CIPDNS,
	CU_AT_ID_CIPDOMAIN,
	CU_AT_ID_CIPMODE,
	CU_AT_ID_CIPMUX,
	CU_AT_ID_CIPRECONNINTV,
	CU_AT_ID_CIPRECVDATA,
	CU_AT_ID_CIPRECVLEN,
	CU_AT_ID_CIPRECVMODE,
	CU_AT_ID_CIPSEND,
	CU_AT_ID_CIPSERVER,
	CU_AT_ID_CIPSERVERMAXCONN,
	CU_AT_ID_CIPSNTPCFG,
	CU_AT_ID_CIPSNTPTIME,
	CU_AT_ID_CIPSSLCALPN,
	CU_AT_ID_CIPSSLCCIPHER,
	CU_AT_ID_CIPSSLCCN,
	CU_AT_ID_CIPSSLCCONF,
	CU_AT_ID_CIPSSLCONF,
	CU_AT_ID_CIPSSLCPSK,
	CU_AT_ID_CIPSSLCSNI,
	CU_AT_ID_CIPSSLSIZE,
	CU_AT_ID_CIPSTA,
	CU_AT_ID_CIPSTAMAC,
	CU_AT_ID_CIPSTART,
	CU_AT_ID_CIPSTARTEX,
	CU_AT_ID_CIPSTATE,
	CU_AT_ID_CIPSTATUS,
	CU_AT_ID_CIPSTO,
	CU_AT_ID_CIPTCPOPT,
	CU_AT_ID_CIPV6,
	CU_AT_ID_CIUPDATE,
	CU_AT_ID_CMD,
	CU_AT_ID_CWAPPROTO,
	CU_AT_ID_CWAUTOCONN,
	CU_AT_ID_CWDHCP,
	CU_AT_ID_CWDHCPS,
	CU_AT_ID_CWHOSTNAME,
	CU_AT_ID_CWJAP,
	CU_AT_ID_CWLAP,
	CU_AT_ID_CWLAPOPT,
	CU_AT_ID_CWLIF,
	CU_AT_ID_CWMODE,
	CU_AT_ID_CWQAP,
	CU_AT_ID_CWQIF,
	CU_AT_ID_CWRECONNCFG,
	CU_AT_ID_CWSAP,
	CU_AT_ID_CWSTAPROTO,
	CU_AT_ID_CWSTARTSMART,
	CU_AT_ID_CWSTATE,
	CU_AT_ID_CWSTOPSMART,
	CU_AT_ID_GMR,
	CU_AT_ID_GSLP,
	CU_AT_ID_IPR,
	CU_AT_ID_MDNS,
	CU_AT_ID_MQTTCLEAN,
	CU_AT_ID_MQTTCONN,
	CU_AT_ID_MQTTCONNCFG,
	CU_AT_ID_MQTTLONGCLIENTID,
	CU_AT_ID_MQTTLONGPASSWORD,
	CU_AT_ID_MQTTLONGUSERNAME,
	CU_AT_ID_MQTTPUB,
	CU_AT_ID_MQTTPUBRAW,
	CU_AT_ID_MQTTSUB,
	CU_AT_ID_MQTTUNSUB,
	CU_AT_ID_MQTTUSERCFG,
	CU_AT_ID_PING,
	CU_AT_ID_RESTORE,
	CU_AT_ID_RFPOWER,
	CU_AT_ID_RFVDD,
	CU_AT_ID_RST,
	CU_AT_ID_SAVETRANSLINK,
	CU_AT_ID_SLEEP,
	CU_AT_ID_SLEEPWKCFG,
	CU_AT_ID_SYSADC,
	CU_AT_ID_SYSFLASH,
	CU_AT_ID_SYSGPIODIR,
	CU_AT_ID_SYSGPIOREAD,
	CU_AT_ID_SYSGPIOWRITE,
	CU_AT_ID_SYSIOGETCFG,
	CU_AT_ID_SYSIOSETCFG,
	CU_AT_ID_SYSLOG,
	CU_AT_ID_SYSMSG,
	CU_AT_ID_SYSRAM,
	CU_AT_ID_SYSREG,
	CU_AT_ID_SYSROLLBACK,
	CU_AT_ID_SYSSTORE,
	CU_AT_ID_SYSTIMESTAMP,
	CU_AT_ID_UART,
	CU_AT_ID_UART_CUR,
	CU_AT_ID_UART_DEF,
	CU_AT_ID_WPS,
} cu_esp_at_dispatch_id_t;

typedef struct{
	const char *name;
	uint8 len;
	uint8 id;
} cu_esp_at_dispatch_entry_t;

#define CU_AT_ENTRY(n, i) { (n), (uint8)(sizeof(n) - 1u), (uint8)(i) }
static const cu_esp_at_dispatch_entry_t cu_esp_at_dispatch_table[] = {
	CU_AT_ENTRY("CIFSR", CU_AT_ID_CIFSR),
	CU_AT_ENTRY("CIOBAUD", CU_AT_ID_CIOBAUD),
	CU_AT_ENTRY("CIPAP", CU_AT_ID_CIPAP),
	CU_AT_ENTRY("CIPAPMAC", CU_AT_ID_CIPAPMAC),
	CU_AT_ENTRY("CIPBUFRECVDATA", CU_AT_ID_CIPBUFRECVDATA),
	CU_AT_ENTRY("CIPBUFRECVLEN", CU_AT_ID_CIPBUFRECVLEN),
	CU_AT_ENTRY("CIPBUFRECVMODE", CU_AT_ID_CIPBUFRECVMODE),
	CU_AT_ENTRY("CIPCLOSE", CU_AT_ID_CIPCLOSE),
	CU_AT_ENTRY("CIPDINFO", CU_AT_ID_CIPDINFO),
	CU_AT_ENTRY("CIPDNS", CU_AT_ID_CIPDNS),
	CU_AT_ENTRY("CIPDOMAIN", CU_AT_ID_CIPDOMAIN),
	CU_AT_ENTRY("CIPMODE", CU_AT_ID_CIPMODE),
	CU_AT_ENTRY("CIPMUX", CU_AT_ID_CIPMUX),
	CU_AT_ENTRY("CIPRECONNINTV", CU_AT_ID_CIPRECONNINTV),
	CU_AT_ENTRY("CIPRECVDATA", CU_AT_ID_CIPRECVDATA),
	CU_AT_ENTRY("CIPRECVLEN", CU_AT_ID_CIPRECVLEN),
	CU_AT_ENTRY("CIPRECVMODE", CU_AT_ID_CIPRECVMODE),
	CU_AT_ENTRY("CIPSEND", CU_AT_ID_CIPSEND),
	CU_AT_ENTRY("CIPSENDEX", CU_AT_ID_CIPSEND),
	CU_AT_ENTRY("CIPSERVER", CU_AT_ID_CIPSERVER),
	CU_AT_ENTRY("CIPSERVERMAXCONN", CU_AT_ID_CIPSERVERMAXCONN),
	CU_AT_ENTRY("CIPSNTPCFG", CU_AT_ID_CIPSNTPCFG),
	CU_AT_ENTRY("CIPSNTPTIME", CU_AT_ID_CIPSNTPTIME),
	CU_AT_ENTRY("CIPSSLCALPN", CU_AT_ID_CIPSSLCALPN),
	CU_AT_ENTRY("CIPSSLCCIPHER", CU_AT_ID_CIPSSLCCIPHER),
	CU_AT_ENTRY("CIPSSLCCN", CU_AT_ID_CIPSSLCCN),
	CU_AT_ENTRY("CIPSSLCCONF", CU_AT_ID_CIPSSLCCONF),
	CU_AT_ENTRY("CIPSSLCONF", CU_AT_ID_CIPSSLCONF),
	CU_AT_ENTRY("CIPSSLCPSK", CU_AT_ID_CIPSSLCPSK),
	CU_AT_ENTRY("CIPSSLCSNI", CU_AT_ID_CIPSSLCSNI),
	CU_AT_ENTRY("CIPSSLSIZE", CU_AT_ID_CIPSSLSIZE),
	CU_AT_ENTRY("CIPSTA", CU_AT_ID_CIPSTA),
	CU_AT_ENTRY("CIPSTAMAC", CU_AT_ID_CIPSTAMAC),
	CU_AT_ENTRY("CIPSTART", CU_AT_ID_CIPSTART),
	CU_AT_ENTRY("CIPSTARTEX", CU_AT_ID_CIPSTARTEX),
	CU_AT_ENTRY("CIPSTATE", CU_AT_ID_CIPSTATE),
	CU_AT_ENTRY("CIPSTATUS", CU_AT_ID_CIPSTATUS),
	CU_AT_ENTRY("CIPSTO", CU_AT_ID_CIPSTO),
	CU_AT_ENTRY("CIPTCPOPT", CU_AT_ID_CIPTCPOPT),
	CU_AT_ENTRY("CIPV6", CU_AT_ID_CIPV6),
	CU_AT_ENTRY("CIUPDATE", CU_AT_ID_CIUPDATE),
	CU_AT_ENTRY("CMD", CU_AT_ID_CMD),
	CU_AT_ENTRY("CWAPPROTO", CU_AT_ID_CWAPPROTO),
	CU_AT_ENTRY("CWAUTOCONN", CU_AT_ID_CWAUTOCONN),
	CU_AT_ENTRY("CWDHCP", CU_AT_ID_CWDHCP),
	CU_AT_ENTRY("CWDHCPS", CU_AT_ID_CWDHCPS),
	CU_AT_ENTRY("CWHOSTNAME", CU_AT_ID_CWHOSTNAME),
	CU_AT_ENTRY("CWJAP", CU_AT_ID_CWJAP),
	CU_AT_ENTRY("CWLAP", CU_AT_ID_CWLAP),
	CU_AT_ENTRY("CWLAPOPT", CU_AT_ID_CWLAPOPT),
	CU_AT_ENTRY("CWLIF", CU_AT_ID_CWLIF),
	CU_AT_ENTRY("CWMODE", CU_AT_ID_CWMODE),
	CU_AT_ENTRY("CWQAP", CU_AT_ID_CWQAP),
	CU_AT_ENTRY("CWQIF", CU_AT_ID_CWQIF),
	CU_AT_ENTRY("CWRECONNCFG", CU_AT_ID_CWRECONNCFG),
	CU_AT_ENTRY("CWSAP", CU_AT_ID_CWSAP),
	CU_AT_ENTRY("CWSTAPROTO", CU_AT_ID_CWSTAPROTO),
	CU_AT_ENTRY("CWSTARTSMART", CU_AT_ID_CWSTARTSMART),
	CU_AT_ENTRY("CWSTATE", CU_AT_ID_CWSTATE),
	CU_AT_ENTRY("CWSTOPSMART", CU_AT_ID_CWSTOPSMART),
	CU_AT_ENTRY("GMR", CU_AT_ID_GMR),
	CU_AT_ENTRY("GSLP", CU_AT_ID_GSLP),
	CU_AT_ENTRY("IPR", CU_AT_ID_IPR),
	CU_AT_ENTRY("MDNS", CU_AT_ID_MDNS),
	CU_AT_ENTRY("MQTTCLEAN", CU_AT_ID_MQTTCLEAN),
	CU_AT_ENTRY("MQTTCONN", CU_AT_ID_MQTTCONN),
	CU_AT_ENTRY("MQTTCONNCFG", CU_AT_ID_MQTTCONNCFG),
	CU_AT_ENTRY("MQTTLONGCLIENTID", CU_AT_ID_MQTTLONGCLIENTID),
	CU_AT_ENTRY("MQTTLONGPASSWORD", CU_AT_ID_MQTTLONGPASSWORD),
	CU_AT_ENTRY("MQTTLONGUSERNAME", CU_AT_ID_MQTTLONGUSERNAME),
	CU_AT_ENTRY("MQTTPUB", CU_AT_ID_MQTTPUB),
	CU_AT_ENTRY("MQTTPUBRAW", CU_AT_ID_MQTTPUBRAW),
	CU_AT_ENTRY("MQTTSUB", CU_AT_ID_MQTTSUB),
	CU_AT_ENTRY("MQTTUNSUB", CU_AT_ID_MQTTUNSUB),
	CU_AT_ENTRY("MQTTUSERCFG", CU_AT_ID_MQTTUSERCFG),
	CU_AT_ENTRY("PING", CU_AT_ID_PING),
	CU_AT_ENTRY("RESTORE", CU_AT_ID_RESTORE),
	CU_AT_ENTRY("RFPOWER", CU_AT_ID_RFPOWER),
	CU_AT_ENTRY("RFVDD", CU_AT_ID_RFVDD),
	CU_AT_ENTRY("RST", CU_AT_ID_RST),
	CU_AT_ENTRY("SAVETRANSLINK", CU_AT_ID_SAVETRANSLINK),
	CU_AT_ENTRY("SLEEP", CU_AT_ID_SLEEP),
	CU_AT_ENTRY("SLEEPWKCFG", CU_AT_ID_SLEEPWKCFG),
	CU_AT_ENTRY("SYSADC", CU_AT_ID_SYSADC),
	CU_AT_ENTRY("SYSFLASH", CU_AT_ID_SYSFLASH),
	CU_AT_ENTRY("SYSGPIODIR", CU_AT_ID_SYSGPIODIR),
	CU_AT_ENTRY("SYSGPIOREAD", CU_AT_ID_SYSGPIOREAD),
	CU_AT_ENTRY("SYSGPIOWRITE", CU_AT_ID_SYSGPIOWRITE),
	CU_AT_ENTRY("SYSIOGETCFG", CU_AT_ID_SYSIOGETCFG),
	CU_AT_ENTRY("SYSIOSETCFG", CU_AT_ID_SYSIOSETCFG),
	CU_AT_ENTRY("SYSLOG", CU_AT_ID_SYSLOG),
	CU_AT_ENTRY("SYSMSG", CU_AT_ID_SYSMSG),
	CU_AT_ENTRY("SYSRAM", CU_AT_ID_SYSRAM),
	CU_AT_ENTRY("SYSREG", CU_AT_ID_SYSREG),
	CU_AT_ENTRY("SYSROLLBACK", CU_AT_ID_SYSROLLBACK),
	CU_AT_ENTRY("SYSSTORE", CU_AT_ID_SYSSTORE),
	CU_AT_ENTRY("SYSTIMESTAMP", CU_AT_ID_SYSTIMESTAMP),
	CU_AT_ENTRY("UART", CU_AT_ID_UART),
	CU_AT_ENTRY("UART_CUR", CU_AT_ID_UART_CUR),
	CU_AT_ENTRY("UART_DEF", CU_AT_ID_UART_DEF),
	CU_AT_ENTRY("WPS", CU_AT_ID_WPS),
};
#undef CU_AT_ENTRY

static const cu_esp_at_dispatch_entry_t *cu_esp_at_dispatch_find(const char *name, auint len)
{
	auint lo = 0u;
	auint hi = (auint)CU_ARRLEN(cu_esp_at_dispatch_table);
	while(lo < hi){
		auint mid = lo + ((hi - lo) >> 1);
		const cu_esp_at_dispatch_entry_t *e = &cu_esp_at_dispatch_table[mid];
		auint n = (len < (auint)e->len) ? len : (auint)e->len;
		int cmp = memcmp(name, e->name, n);
		if(cmp == 0){
			if(len < (auint)e->len) cmp = -1;
			else if(len > (auint)e->len) cmp = 1;
			else return e;
		}
		if(cmp < 0) hi = mid;
		else lo = mid + 1u;
	}
	return NULL;
}

static void cu_esp_at_dispatch_call(uint8 id, sint8 *cmd_buf)
{
	switch((cu_esp_at_dispatch_id_t)id){
		case CU_AT_ID_CIFSR: cu_esp_at_cifsr(cmd_buf); return;
		case CU_AT_ID_CIOBAUD: cu_esp_at_ciobaud(cmd_buf); return;
		case CU_AT_ID_CIPAP: cu_esp_at_cipap(cmd_buf); return;
		case CU_AT_ID_CIPAPMAC: cu_esp_at_cipapmac(cmd_buf); return;
		case CU_AT_ID_CIPBUFRECVDATA: cu_esp_at_cipbufrecvdata(cmd_buf); return;
		case CU_AT_ID_CIPBUFRECVLEN: cu_esp_at_cipbufrecvlen(cmd_buf); return;
		case CU_AT_ID_CIPBUFRECVMODE: cu_esp_at_cipbufrecvmode(cmd_buf); return;
		case CU_AT_ID_CIPCLOSE: cu_esp_at_cipclose(cmd_buf); return;
		case CU_AT_ID_CIPDINFO: cu_esp_at_cipdinfo(cmd_buf); return;
		case CU_AT_ID_CIPDNS: cu_esp_at_cipdns(cmd_buf); return;
		case CU_AT_ID_CIPDOMAIN: cu_esp_at_cipdomain(cmd_buf); return;
		case CU_AT_ID_CIPMODE: cu_esp_at_cipmode(cmd_buf); return;
		case CU_AT_ID_CIPMUX: cu_esp_at_cipmux(cmd_buf); return;
		case CU_AT_ID_CIPRECONNINTV: cu_esp_at_cipreconnintv(cmd_buf); return;
		case CU_AT_ID_CIPRECVDATA: cu_esp_at_ciprecvdata(cmd_buf); return;
		case CU_AT_ID_CIPRECVLEN: cu_esp_at_ciprecvlen(cmd_buf); return;
		case CU_AT_ID_CIPRECVMODE: cu_esp_at_ciprecvmode(cmd_buf); return;
		case CU_AT_ID_CIPSEND: cu_esp_at_cipsend(cmd_buf); return;
		case CU_AT_ID_CIPSERVER: cu_esp_at_cipserver(cmd_buf); return;
		case CU_AT_ID_CIPSERVERMAXCONN: cu_esp_at_cipservermaxconn(cmd_buf); return;
		case CU_AT_ID_CIPSNTPCFG: cu_esp_at_cipsntpcfg(cmd_buf); return;
		case CU_AT_ID_CIPSNTPTIME: cu_esp_at_cipsntptime(cmd_buf); return;
		case CU_AT_ID_CIPSSLCALPN: cu_esp_at_cipsslcalpn(cmd_buf); return;
		case CU_AT_ID_CIPSSLCCIPHER: cu_esp_at_cipsslccipher(cmd_buf); return;
		case CU_AT_ID_CIPSSLCCN: cu_esp_at_cipsslccn(cmd_buf); return;
		case CU_AT_ID_CIPSSLCCONF: cu_esp_at_cipsslcconf(cmd_buf); return;
		case CU_AT_ID_CIPSSLCONF: cu_esp_at_cipsslconf(cmd_buf); return;
		case CU_AT_ID_CIPSSLCPSK: cu_esp_at_cipsslcpsk(cmd_buf); return;
		case CU_AT_ID_CIPSSLCSNI: cu_esp_at_cipsslcsni(cmd_buf); return;
		case CU_AT_ID_CIPSSLSIZE: cu_esp_at_cipsslsize(cmd_buf); return;
		case CU_AT_ID_CIPSTA: cu_esp_at_cipsta(cmd_buf); return;
		case CU_AT_ID_CIPSTAMAC: cu_esp_at_cipstamac(cmd_buf); return;
		case CU_AT_ID_CIPSTART: cu_esp_at_cipstart(cmd_buf); return;
		case CU_AT_ID_CIPSTARTEX: cu_esp_at_cipstartex(cmd_buf); return;
		case CU_AT_ID_CIPSTATE: cu_esp_at_cipstate(cmd_buf); return;
		case CU_AT_ID_CIPSTATUS: cu_esp_at_cipstatus(cmd_buf); return;
		case CU_AT_ID_CIPSTO: cu_esp_at_cipsto(cmd_buf); return;
		case CU_AT_ID_CIPTCPOPT: cu_esp_at_ciptcpopt(cmd_buf); return;
		case CU_AT_ID_CIPV6: cu_esp_at_cipv6(cmd_buf); return;
		case CU_AT_ID_CIUPDATE: cu_esp_at_ciupdate(cmd_buf); return;
		case CU_AT_ID_CMD: cu_esp_at_cmd(cmd_buf); return;
		case CU_AT_ID_CWAPPROTO: cu_esp_at_cwapproto(cmd_buf); return;
		case CU_AT_ID_CWAUTOCONN: cu_esp_at_cwautoconn(cmd_buf); return;
		case CU_AT_ID_CWDHCP: cu_esp_at_cwdhcp(cmd_buf); return;
		case CU_AT_ID_CWDHCPS: cu_esp_at_cwdhcps(cmd_buf); return;
		case CU_AT_ID_CWHOSTNAME: cu_esp_at_cwhostname(cmd_buf); return;
		case CU_AT_ID_CWJAP: cu_esp_at_cwjap(cmd_buf); return;
		case CU_AT_ID_CWLAP: cu_esp_at_cwlap(cmd_buf); return;
		case CU_AT_ID_CWLAPOPT: cu_esp_at_cwlapopt(cmd_buf); return;
		case CU_AT_ID_CWLIF: cu_esp_at_cwlif(cmd_buf); return;
		case CU_AT_ID_CWMODE: cu_esp_at_cwmode(cmd_buf); return;
		case CU_AT_ID_CWQAP: cu_esp_at_cwqap(cmd_buf); return;
		case CU_AT_ID_CWQIF: cu_esp_at_cwqif(cmd_buf); return;
		case CU_AT_ID_CWRECONNCFG: cu_esp_at_cwreconncfg(cmd_buf); return;
		case CU_AT_ID_CWSAP: cu_esp_at_cwsap(cmd_buf); return;
		case CU_AT_ID_CWSTAPROTO: cu_esp_at_cwstaproto(cmd_buf); return;
		case CU_AT_ID_CWSTARTSMART: cu_esp_at_cwstartsmart(cmd_buf); return;
		case CU_AT_ID_CWSTATE: cu_esp_at_cwstate(cmd_buf); return;
		case CU_AT_ID_CWSTOPSMART: cu_esp_at_cwstopsmart(cmd_buf); return;
		case CU_AT_ID_GMR: cu_esp_at_gmr(); return;
		case CU_AT_ID_GSLP: cu_esp_at_gslp(cmd_buf); return;
		case CU_AT_ID_IPR: cu_esp_at_ipr(cmd_buf); return;
		case CU_AT_ID_MDNS: cu_esp_at_mdns(cmd_buf); return;
		case CU_AT_ID_MQTTCLEAN: cu_esp_at_mqttclean(cmd_buf); return;
		case CU_AT_ID_MQTTCONN: cu_esp_at_mqttconn(cmd_buf); return;
		case CU_AT_ID_MQTTCONNCFG: cu_esp_at_mqttconncfg(cmd_buf); return;
		case CU_AT_ID_MQTTLONGCLIENTID: cu_esp_at_mqttlongclientid(cmd_buf); return;
		case CU_AT_ID_MQTTLONGPASSWORD: cu_esp_at_mqttlongpassword(cmd_buf); return;
		case CU_AT_ID_MQTTLONGUSERNAME: cu_esp_at_mqttlongusername(cmd_buf); return;
		case CU_AT_ID_MQTTPUB: cu_esp_at_mqttpub(cmd_buf); return;
		case CU_AT_ID_MQTTPUBRAW: cu_esp_at_mqttpubraw(cmd_buf); return;
		case CU_AT_ID_MQTTSUB: cu_esp_at_mqttsub(cmd_buf); return;
		case CU_AT_ID_MQTTUNSUB: cu_esp_at_mqttunsub(cmd_buf); return;
		case CU_AT_ID_MQTTUSERCFG: cu_esp_at_mqttusercfg(cmd_buf); return;
		case CU_AT_ID_PING: cu_esp_at_ping(cmd_buf); return;
		case CU_AT_ID_RESTORE: cu_esp_at_restore(cmd_buf); return;
		case CU_AT_ID_RFPOWER: cu_esp_at_rfpower(cmd_buf); return;
		case CU_AT_ID_RFVDD: cu_esp_at_rfvdd(cmd_buf); return;
		case CU_AT_ID_RST: cu_esp_at_rst(); return;
		case CU_AT_ID_SAVETRANSLINK: cu_esp_at_savetranslink(cmd_buf); return;
		case CU_AT_ID_SLEEP: cu_esp_at_sleep(cmd_buf); return;
		case CU_AT_ID_SLEEPWKCFG: cu_esp_at_sleepwkcfg(cmd_buf); return;
		case CU_AT_ID_SYSADC: cu_esp_at_sysadc(cmd_buf); return;
		case CU_AT_ID_SYSFLASH: cu_esp_at_sysflash(cmd_buf); return;
		case CU_AT_ID_SYSGPIODIR: cu_esp_at_sysgpiodir(cmd_buf); return;
		case CU_AT_ID_SYSGPIOREAD: cu_esp_at_sysgpioread(cmd_buf); return;
		case CU_AT_ID_SYSGPIOWRITE: cu_esp_at_sysgpiowrite(cmd_buf); return;
		case CU_AT_ID_SYSIOGETCFG: cu_esp_at_sysiogetcfg(cmd_buf); return;
		case CU_AT_ID_SYSIOSETCFG: cu_esp_at_sysiosetcfg(cmd_buf); return;
		case CU_AT_ID_SYSLOG: cu_esp_at_syslog(cmd_buf); return;
		case CU_AT_ID_SYSMSG: cu_esp_at_sysmsg(cmd_buf); return;
		case CU_AT_ID_SYSRAM: cu_esp_at_sysram(cmd_buf); return;
		case CU_AT_ID_SYSREG: cu_esp_at_sysreg(cmd_buf); return;
		case CU_AT_ID_SYSROLLBACK: cu_esp_at_sysrollback(cmd_buf); return;
		case CU_AT_ID_SYSSTORE: cu_esp_at_sysstore(cmd_buf); return;
		case CU_AT_ID_SYSTIMESTAMP: cu_esp_at_systimestamp(cmd_buf); return;
		case CU_AT_ID_UART: cu_esp_at_uart(cmd_buf); return;
		case CU_AT_ID_UART_CUR: cu_esp_at_uart_cur(cmd_buf); return;
		case CU_AT_ID_UART_DEF: cu_esp_at_uart_def(cmd_buf); return;
		case CU_AT_ID_WPS: cu_esp_at_wps(cmd_buf); return;
		default: cu_esp_at_bad_command(); return;
	}
}

void cu_esp_process_at(sint8 *cmd_buf)
{
	const char *cmd = (const char *)cmd_buf;
	const char *name;
	auint len = 0u;
	const cu_esp_at_dispatch_entry_t *entry;

	CU_ESP_AT_LOG("PROCESS AT [%s]\n", cmd);
	if(cu_esp_at_is_busy()){ cu_esp_txp_busy_p(); return; }

	if(cmd[0] != 'A' || cmd[1] != 'T'){ cu_esp_txp_error(); return; }
	if(cmd[2] != '+'){
		if(cmd[2] == 'E'){ cu_esp_at_ate(cmd_buf); return; }
		if(cmd[2] == '\r' && cmd[3] == '\n'){ cu_esp_at(); return; }
		cu_esp_at_bad_command();
		return;
	}

	name = cmd + 3;
	while(name[len] != '\0' && name[len] != '\r' && name[len] != '\n' &&
	      name[len] != '=' && name[len] != '?')
		len++;
	if(len == 0u){ cu_esp_at_bad_command(); return; }

	entry = cu_esp_at_dispatch_find(name, len);
	/* NONOS AT commonly used _CUR/_DEF spellings. Preserve that compatibility
	 * without accepting arbitrary prefix garbage as the old strncmp chain did. */
	if(entry == NULL && len > 4u && name[len - 4u] == '_' &&
	   ((memcmp(name + len - 3u, "CUR", 3u) == 0) ||
	    (memcmp(name + len - 3u, "DEF", 3u) == 0)))
		entry = cu_esp_at_dispatch_find(name, len - 4u);

	if(entry != NULL){ cu_esp_at_dispatch_call(entry->id, cmd_buf); return; }
	cu_esp_at_bad_command();
}

#endif /* ENABLE_ESP */
