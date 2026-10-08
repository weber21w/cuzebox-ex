/*
 *  ESP8266 peripheral Network/Host Serial
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
#include "midi.h"

#if !defined(ENABLE_ESP)
/* ESP8266 emulation disabled: translation unit intentionally empty. */
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#ifndef __EMSCRIPTEN__
	#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		#include <winsock2.h>
		#include <ws2tcpip.h>
		#include <windows.h>
		#include <iphlpapi.h>
		#include <icmpapi.h>
		#if defined(_MSC_VER)
			#pragma comment(lib, "iphlpapi.lib")
			#pragma comment(lib, "ws2_32.lib")
		#endif
	#else
		#include <pthread.h>
		#include <sys/time.h>
		#include <unistd.h>
		#include <errno.h>
		#include <fcntl.h>
		#include <termios.h>
		#include <sys/ioctl.h>
		#include <sys/socket.h>
		#include <netdb.h>
		#include <arpa/inet.h>
		#include <netinet/in.h>
		#include <netinet/ip.h>
		#include <netinet/ip_icmp.h>
	#endif
#endif

#include "configfile.h"

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	#include <openssl/ssl.h>
	#include <openssl/err.h>
#endif

/*
 * Older MinGW headers do not consistently declare cu_esp_inet_pton()/cu_esp_inet_ntop().
 * Keep the ESP network code independent of that SDK detail by using local
 * compatibility helpers on Windows.  This also avoids defining replacements
 * with the system function names, which can collide with newer SDKs.
 */
#if !defined(__EMSCRIPTEN__) && (defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__))
static int cu_esp_inet_pton(int af, const char *src, void *dst)
{
	struct sockaddr_storage ss;
	int sslen = (int)sizeof(ss);
	char tmp[INET6_ADDRSTRLEN + 1];

	if(src == NULL || dst == NULL)
		return -1;
	memset(&ss, 0, sizeof(ss));
	strncpy(tmp, src, sizeof(tmp) - 1u);
	tmp[sizeof(tmp) - 1u] = '\0';
	if(WSAStringToAddressA(tmp, af, NULL, (LPSOCKADDR)&ss, &sslen) != 0)
		return 0;
	if(af == AF_INET){
		memcpy(dst, &((struct sockaddr_in *)&ss)->sin_addr, sizeof(struct in_addr));
		return 1;
	}
	if(af == AF_INET6){
		memcpy(dst, &((struct sockaddr_in6 *)&ss)->sin6_addr, sizeof(struct in6_addr));
		return 1;
	}
	return -1;
}

static const char *cu_esp_inet_ntop(int af, const void *src, char *dst, size_t dst_size)
{
	struct sockaddr_storage ss;

	if(src == NULL || dst == NULL || dst_size == 0u)
		return NULL;
	memset(&ss, 0, sizeof(ss));
	if(af == AF_INET){
		struct sockaddr_in *sa = (struct sockaddr_in *)&ss;
		sa->sin_family = AF_INET;
		memcpy(&sa->sin_addr, src, sizeof(sa->sin_addr));
		if(getnameinfo((struct sockaddr *)sa, (socklen_t)sizeof(*sa),
		               dst, (DWORD)dst_size, NULL, 0, NI_NUMERICHOST) != 0)
			return NULL;
	}else if(af == AF_INET6){
		struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&ss;
		sa->sin6_family = AF_INET6;
		memcpy(&sa->sin6_addr, src, sizeof(sa->sin6_addr));
		if(getnameinfo((struct sockaddr *)sa, (socklen_t)sizeof(*sa),
		               dst, (DWORD)dst_size, NULL, 0, NI_NUMERICHOST) != 0)
			return NULL;
	}else{
		return NULL;
	}
	return dst;
}
#else
#define cu_esp_inet_pton inet_pton
#define cu_esp_inet_ntop inet_ntop
#endif

/* project globals from other CUzeBox units */
#define esp_state (*cu_esp_get_state())

#define CU_ESP_TCP_SERIAL_ASYNC_SOCK 0xFFu

/* ------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* ------------------------------------------------------------------------- */

static void cu_esp_s8cpy(sint8 *dst, size_t dsz, const char *src)
{
	size_t n = 0u;
	if(!dst || dsz == 0u)
		return;
	if(!src){
		dst[0] = '\0';
		return;
	}
	while((n + 1u) < dsz && src[n] != '\0')
		n++;
	if(n)
		memcpy(dst, src, n);
	dst[n] = '\0';
}

static void cu_esp_u8cpy(uint8 *dst, size_t dsz, const char *src)
{
	cu_esp_s8cpy((sint8 *)dst, dsz, src);
}

static void cu_esp_trim(char *s)
{
	size_t n;
	size_t i = 0u;

	if(!s)
		return;

	while(s[i] && isspace((unsigned char)s[i]))
		i++;

	if(i)
		memmove(s, s + i, strlen(s + i) + 1u);

	n = strlen(s);
	while(n && isspace((unsigned char)s[n - 1u])){
		s[n - 1u] = '\0';
		n--;
	}
}

static auint cu_esp_cfg_parse_kv(const char *line, char *key, size_t ksz, char *val, size_t vsz)
{
	/* Accept:
	 *   Key="Value"
	 *   Key = "Value"
	 * Ignore blank/comment lines.
	 */
	const char *p;
	const char *eq;
	const char *q1;
	const char *q2;
	size_t klen;
	size_t vlen;

	if(!line || !key || !val || ksz < 2u || vsz < 1u)
		return 0u;

	key[0] = '\0';
	val[0] = '\0';

	while(*line && isspace((unsigned char)*line))
		line++;

	if(*line == '\0' || *line == '\n' || *line == '#')
		return 0u;

	p = line;
	eq = strchr(p, '=');
	if(!eq)
		return 0u;

	klen = (size_t)(eq - p);
	while(klen && isspace((unsigned char)p[klen - 1u]))
		klen--;

	if(klen == 0u || klen >= ksz)
		return 0u;

	memcpy(key, p, klen);
	key[klen] = '\0';

	q1 = strchr(eq, '"');
	if(!q1)
		return 0u;
	q1++;

	q2 = strrchr(q1, '"');
	if(!q2 || q2 < q1)
		return 0u;

	vlen = (size_t)(q2 - q1);
	if(vlen >= vsz)
		vlen = vsz - 1u;

	memcpy(val, q1, vlen);
	val[vlen] = '\0';

	return 1u;
}

static auint cu_esp_parse_mac6(const char *s, uint8 mac[6])
{
	unsigned int v[6];

	if(!s || !mac)
		return 0u;

	if(sscanf(s, "%x:%x:%x:%x:%x:%x",
		&v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
		return 0u;

	for(int i = 0; i < 6; i++){
		if(v[i] > 0xFFu)
			return 0u;
		mac[i] = (uint8)v[i];
	}
	return 1u;
}

static void cu_esp_format_mac6(const uint8 mac[6], char *out, size_t out_sz)
{
	if(!out || out_sz == 0u)
		return;

	if(!mac){
		out[0] = '\0';
		return;
	}

	snprintf(out, out_sz, "%02x:%02x:%02x:%02x:%02x:%02x",
		(unsigned int)mac[0], (unsigned int)mac[1], (unsigned int)mac[2],
		(unsigned int)mac[3], (unsigned int)mac[4], (unsigned int)mac[5]);
}

static uint32 cu_esp_ipv4_to_be(const char *ipstr, uint32 fallback_be)
{
	if(!ipstr || !*ipstr)
		return fallback_be;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		unsigned long x = inet_addr(ipstr);
		if(x == INADDR_NONE && strcmp(ipstr, "255.255.255.255") != 0)
			return fallback_be;
		return (uint32)x;
	}
#else
	struct in_addr a;
	if(cu_esp_inet_pton(AF_INET, ipstr, &a) != 1)
		return fallback_be;
	return (uint32)a.s_addr;
#endif
}

static void cu_esp_ipv4_be_to_str(uint32 be, char *out, size_t out_sz)
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
/* Time                                                                       */
/* ------------------------------------------------------------------------- */

uint32 cu_esp_now_ms(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	return (uint32)GetTickCount();
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint32)(tv.tv_sec * 1000u + (uint32)(tv.tv_usec / 1000u));
#endif
}


/* ------------------------------------------------------------------------- */
/* TCP serial transport diagnostics                                           */
/* ------------------------------------------------------------------------- */

#define CU_TCP_DIAG_SNAPSHOT_MS 250u
#define CU_TCP_DIAG_FLUSH_MS    2000u

typedef struct{
	cu_esp_tcp_serial_diag_t pub;
	FILE *log_file;
	char log_path[320];
	uint32 next_snapshot_ms;
	uint32 next_flush_ms;
	uint32 last_service_ms;
	uint32 last_observed_state;
	uint32 initialized;
} cu_tcp_diag_state_t;

static cu_tcp_diag_state_t cu_tcp_diag;
static void cu_esp_tcp_serial_service(void);

static unsigned long cu_tcp_diag_pid(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	return (unsigned long)GetCurrentProcessId();
#else
	return (unsigned long)getpid();
#endif
}

static char const* cu_tcp_diag_mode_name(uint32 mode)
{
	if(mode == CU_ESP_TCP_SERIAL_MODE_AUTO)
		return "auto";
	if(mode == CU_ESP_TCP_SERIAL_MODE_INTERNET)
		return "internet";
	if(mode == CU_ESP_TCP_SERIAL_MODE_LAN)
		return "lan";
	return (mode == CU_ESP_TCP_SERIAL_MODE_SERVER) ? "server" : "client";
}

/*
 * AUTO role negotiation. Kept outside cu_state_esp_t so savestates are
 * unaffected. In AUTO mode the endpoint first tries to connect to host:port.
 * If that fails it listens on port. If the port is already taken (the other
 * instance won the race to listen) it goes back to connecting. On loopback
 * only one process can own the port, so the pair always resolves. For a
 * remote host both sides may end up listening at the same moment; a randomized
 * listen dwell then makes them fall back to connecting until one succeeds.
 */
static uint32 cu_tcp_auto_listen = 0u;
static uint32 cu_tcp_auto_dwell_until_ms = 0u;

static uint32 cu_link_lan_server = 0u; /* LAN mode: role decided at pairing */

static uint32 cu_esp_tcp_serial_effective_mode(void)
{
	if(esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_LAN)
		return cu_link_lan_server ? CU_ESP_TCP_SERIAL_MODE_SERVER : CU_ESP_TCP_SERIAL_MODE_CLIENT;
	if(esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_AUTO)
		return cu_tcp_auto_listen ? CU_ESP_TCP_SERIAL_MODE_SERVER : CU_ESP_TCP_SERIAL_MODE_CLIENT;
	return (esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_SERVER) ? CU_ESP_TCP_SERIAL_MODE_SERVER : CU_ESP_TCP_SERIAL_MODE_CLIENT;
}

static auint cu_esp_tcp_serial_is_auto(void)
{
	return (esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_AUTO) ? 1u : 0u;
}

/* AUTO always keeps negotiating; otherwise honor the AUTO RECONNECT option. */
static auint cu_esp_tcp_serial_retry_enabled(void)
{
	return (esp_state.tcp_serial_auto_reconnect || cu_esp_tcp_serial_is_auto() ||
	        esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_LAN) ? 1u : 0u;
}

static auint cu_esp_tcp_serial_host_is_loopback(void)
{
	char const *h = (char const *)esp_state.tcp_serial_host;
	if(h[0] == '\0')
		return 1u;
	if(strncmp(h, "127.", 4u) == 0)
		return 1u;
	if((strcmp(h, "localhost") == 0) || (strcmp(h, "LOCALHOST") == 0))
		return 1u;
	return 0u;
}

static uint32 cu_esp_tcp_serial_auto_jitter_ms(uint32 span)
{
	static uint32 seed = 0u;
	if(seed == 0u)
		seed = (uint32)cu_tcp_diag_pid() * 2654435761u ^ cu_esp_now_ms() ^ 0x9E3779B9u;
	seed = seed * 1103515245u + 12345u;
	return (span != 0u) ? ((seed >> 8) % span) : 0u;
}

#ifndef __EMSCRIPTEN__
#include "cu_link_net.inc"
#endif

/* Link transport configuration (kept out of cu_state_esp_t so savestates are
 * unaffected; saved to config.cfg alongside the other TcpSerial keys). */
static char cu_link_room[UZRL_ROOM_CODE_LEN + 1] = "";
static char cu_link_relay_host[64] = "uzenet.us";
static uint32 cu_link_relay_port = 43810u;
static uint32 cu_link_lan_port = 12002u;
static uint32 cu_link_rom_id = 0u;

static auint cu_esp_tcp_serial_is_internet(void)
{
	return (esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_INTERNET) ? 1u : 0u;
}

static auint cu_esp_tcp_serial_is_lan(void)
{
	return (esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_LAN) ? 1u : 0u;
}

/* TCP transport may only run in INTERNET mode never, in LAN mode once paired. */
static auint cu_esp_tcp_serial_tcp_allowed(void)
{
#ifndef __EMSCRIPTEN__
	if(cu_esp_tcp_serial_is_internet())
		return 0u;
	if(cu_esp_tcp_serial_is_lan())
		return (cu_lan.paired_id != 0u) ? 1u : 0u;
#endif
	return 1u;
}

/* Push bytes that arrived from the network toward the AVR: through the
 * impairment simulator when it is active, else straight into the RX ring. */
static void cu_esp_tcp_serial_rx_put(uint8 const *d, uint32 n)
{
	uint32 i;
	for(i = 0u; i < n && esp_state.tcp_serial_rx_count < (uint32)sizeof(esp_state.tcp_serial_rx_buf); i++){
		esp_state.tcp_serial_rx_buf[esp_state.tcp_serial_rx_tail] = d[i];
		esp_state.tcp_serial_rx_tail = (esp_state.tcp_serial_rx_tail + 1u) % (uint32)sizeof(esp_state.tcp_serial_rx_buf);
		esp_state.tcp_serial_rx_count++;
	}
	if(i < n)
		esp_state.tcp_serial_rx_drops += n - i;
}

static uint32 cu_esp_tcp_serial_rx_room(void)
{
	return (uint32)sizeof(esp_state.tcp_serial_rx_buf) - esp_state.tcp_serial_rx_count;
}

static void cu_esp_tcp_serial_ingest(uint8 const *d, uint32 n, uint32 now)
{
#ifndef __EMSCRIPTEN__
	if(cu_imp_active()){
		cu_imp_push(d, n, now);
		return;
	}
#endif
	(void)now;
	cu_esp_tcp_serial_rx_put(d, n);
}

static void cu_esp_tcp_serial_impair_release(uint32 now)
{
#ifndef __EMSCRIPTEN__
	uint8 tmp[1024];
	uint32 room, n;
	if(cu_imp.count == 0u)
		return;
	room = cu_esp_tcp_serial_rx_room();
	if(room > (uint32)sizeof(tmp))
		room = (uint32)sizeof(tmp);
	n = cu_imp_pop(tmp, room, now);
	cu_esp_tcp_serial_rx_put(tmp, n);
#else
	(void)now;
#endif
}

static uint32 cu_tcp_diag_socket_pending_rx(void)
{
	if(esp_state.tcp_serial_sock == ESP_INVALID_SOCKET || esp_state.tcp_serial_sock == -1)
		return 0u;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		u_long pending = 0u;
		if(ioctlsocket(esp_state.tcp_serial_sock, FIONREAD, &pending) == 0)
			return (uint32)pending;
	}
#else
	{
		int pending = 0;
		if(ioctl(esp_state.tcp_serial_sock, FIONREAD, &pending) == 0 && pending > 0)
			return (uint32)pending;
	}
#endif
	return 0u;
}

static void cu_tcp_diag_update_live(uint32 now)
{
	cu_esp_tcp_serial_diag_t *d = &cu_tcp_diag.pub;
	d->enabled = esp_state.tcp_serial_enabled ? 1u : 0u;
	d->mode = esp_state.tcp_serial_mode;
	d->state = esp_state.tcp_serial_state;
	d->last_error = esp_state.tcp_serial_last_error;
	d->socket_valid = (esp_state.tcp_serial_sock != ESP_INVALID_SOCKET && esp_state.tcp_serial_sock != -1) ? 1u : 0u;
	d->listener_valid = (esp_state.tcp_serial_listen_sock != ESP_INVALID_SOCKET && esp_state.tcp_serial_listen_sock != -1) ? 1u : 0u;
	d->tx_queue_bytes = esp_state.tcp_serial_tx_count;
	d->rx_queue_bytes = esp_state.tcp_serial_rx_count;
	if(d->tx_queue_bytes > d->tx_queue_high_water)
		d->tx_queue_high_water = d->tx_queue_bytes;
	if(d->rx_queue_bytes > d->rx_queue_high_water)
		d->rx_queue_high_water = d->rx_queue_bytes;
	d->socket_rx_pending_bytes = cu_tcp_diag_socket_pending_rx();
	d->current_ms = now;
}

static void cu_tcp_diag_log_line(const char *event, const char *reason, sint32 detail, boole flush_now)
{
	cu_esp_tcp_serial_diag_t *d = &cu_tcp_diag.pub;
	uint32 now = cu_esp_now_ms();

	cu_tcp_diag_update_live(now);
	if(cu_tcp_diag.log_file == NULL)
		return;

	fprintf(cu_tcp_diag.log_file,
		"%u,%s,%s,%d,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s,%d\n",
		(unsigned)now,
		(event != NULL) ? event : "",
		cu_tcp_diag_mode_name(d->mode),
		(int)d->last_error,
		(int)d->last_send_error,
		(int)d->last_recv_error,
		(unsigned)d->state,
		(unsigned)d->socket_valid,
		(unsigned)d->listener_valid,
		(unsigned)d->tx_queue_bytes,
		(unsigned)d->rx_queue_bytes,
		(unsigned)d->tx_queue_high_water,
		(unsigned)d->rx_queue_high_water,
		(unsigned)d->socket_rx_pending_bytes,
		(unsigned)d->avr_to_backend_bytes,
		(unsigned)d->backend_to_avr_bytes,
		(unsigned)d->socket_tx_bytes,
		(unsigned)d->socket_rx_bytes,
		(unsigned)d->send_calls,
		(unsigned)d->recv_calls,
		(unsigned)d->send_would_block,
		(unsigned)d->recv_would_block,
		(unsigned)d->send_errors,
		(unsigned)d->recv_errors,
		(unsigned)d->send_zero_returns,
		(unsigned)d->recv_zero_returns,
		(unsigned)d->connect_count,
		(unsigned)d->disconnect_count,
		(unsigned)d->state_change_count,
		(unsigned)d->service_calls,
		(unsigned)d->service_gap_over_50ms,
		(unsigned)d->service_gap_over_250ms,
		(unsigned)d->max_service_gap_ms,
		(unsigned)d->connected_since_ms,
		(unsigned)d->last_state_change_ms,
		(unsigned)d->last_avr_tx_ms,
		(unsigned)d->last_avr_rx_ms,
		(unsigned)d->last_socket_tx_ms,
		(unsigned)d->last_socket_rx_ms,
		(reason != NULL) ? reason : "",
		(int)detail);

	if(flush_now || now >= cu_tcp_diag.next_flush_ms){
		fflush(cu_tcp_diag.log_file);
		cu_tcp_diag.next_flush_ms = now + CU_TCP_DIAG_FLUSH_MS;
	}
}

static void cu_tcp_diag_open_log(void)
{
	uint32 now = cu_esp_now_ms();
	char const *role = cu_tcp_diag_mode_name(esp_state.tcp_serial_mode);

	if(cu_tcp_diag.log_file != NULL){
		fflush(cu_tcp_diag.log_file);
		fclose(cu_tcp_diag.log_file);
		cu_tcp_diag.log_file = NULL;
	}

	snprintf(cu_tcp_diag.log_path, sizeof(cu_tcp_diag.log_path),
		"tcp-serial-%s-%u-pid%lu.csv",
		role,
		(unsigned)esp_state.tcp_serial_port,
		cu_tcp_diag_pid());
	cu_tcp_diag.log_file = fopen(cu_tcp_diag.log_path, "wb");
	if(cu_tcp_diag.log_file == NULL){
		cu_tcp_diag.log_path[0] = '\0';
		return;
	}
	setvbuf(cu_tcp_diag.log_file, NULL, _IOFBF, 16384u);
	fprintf(cu_tcp_diag.log_file,
		"ms,event,mode,last_error,last_send_error,last_recv_error,state,socket,listener,txq,rxq,txq_hi,rxq_hi,socket_rx_pending,avr_tx,avr_rx,socket_tx,socket_rx,send_calls,recv_calls,send_would_block,recv_would_block,send_errors,recv_errors,send_zero,recv_zero,connects,disconnects,state_changes,service_calls,gap_gt_50,gap_gt_250,max_gap_ms,connected_since_ms,last_state_change_ms,last_avr_tx_ms,last_avr_rx_ms,last_socket_tx_ms,last_socket_rx_ms,reason,detail\n");
	cu_tcp_diag.next_snapshot_ms = now + CU_TCP_DIAG_SNAPSHOT_MS;
	cu_tcp_diag.next_flush_ms = now + CU_TCP_DIAG_FLUSH_MS;
	cu_tcp_diag_log_line("START", "tcp_serial_start", 0, TRUE);
}

static void cu_tcp_diag_close_log(const char *reason)
{
	if(cu_tcp_diag.log_file == NULL)
		return;
	cu_tcp_diag_log_line("END", reason, 0, TRUE);
	fclose(cu_tcp_diag.log_file);
	cu_tcp_diag.log_file = NULL;
}

static void cu_tcp_diag_note_state(uint32 new_state, sint32 err, const char *reason)
{
	cu_esp_tcp_serial_diag_t *d = &cu_tcp_diag.pub;
	uint32 old_state = esp_state.tcp_serial_state;
	uint32 now = cu_esp_now_ms();

	esp_state.tcp_serial_state = new_state;
	esp_state.tcp_serial_last_error = err;
	if(old_state != new_state){
		d->state_change_count++;
		d->last_state_change_ms = now;
		if(new_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED){
			d->connect_count++;
			d->connected_since_ms = now;
		}
		if(old_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED && new_state != CU_ESP_TCP_SERIAL_STATE_CONNECTED)
			d->disconnect_count++;
	}
	cu_tcp_diag.last_observed_state = new_state;
	cu_tcp_diag_log_line("STATE", reason, err, TRUE);
}

static void cu_tcp_diag_service_tick(void)
{
	cu_esp_tcp_serial_diag_t *d = &cu_tcp_diag.pub;
	uint32 now = cu_esp_now_ms();
	uint32 gap;

	d->service_calls++;
	/* Measure the real interval between consecutive transport service calls.
	 * Sampling every Nth call measures accumulated time and falsely reports a
	 * scheduler stall even when service is regular. */
	if(cu_tcp_diag.last_service_ms != 0u){
		gap = now - cu_tcp_diag.last_service_ms;
		if(gap > d->max_service_gap_ms)
			d->max_service_gap_ms = gap;
		if(gap > 50u){
			d->service_gap_over_50ms++;
			if(gap > 250u)
				d->service_gap_over_250ms++;
			cu_tcp_diag_log_line("SERVICE_GAP", "transport_not_serviced", (sint32)gap,
				(gap > 250u) ? TRUE : FALSE);
		}
	}
	cu_tcp_diag.last_service_ms = now;
	cu_tcp_diag_update_live(now);
	if(cu_tcp_diag.log_file != NULL && now >= cu_tcp_diag.next_snapshot_ms){
		d->log_snapshot_count++;
		cu_tcp_diag_log_line("SNAP", "periodic", 0, FALSE);
		cu_tcp_diag.next_snapshot_ms = now + CU_TCP_DIAG_SNAPSHOT_MS;
	}
}

void cu_esp_tcp_serial_diag_get(cu_esp_tcp_serial_diag_t *out)
{
	if(out == NULL)
		return;
	/* Observation must not drive the transport. In particular, UCSR0A is
	 * polled from the Uzebox HSYNC path, so any endpoint query reachable from
	 * that poll must remain a pure queue inspection rather than issuing host
	 * socket calls. */
	cu_tcp_diag_update_live(cu_esp_now_ms());
	*out = cu_tcp_diag.pub;
}

void cu_esp_tcp_serial_diag_reset(void)
{
	uint32 state = esp_state.tcp_serial_state;
	uint32 mode = esp_state.tcp_serial_mode;
	uint32 enabled = esp_state.tcp_serial_enabled ? 1u : 0u;
	memset(&cu_tcp_diag.pub, 0, sizeof(cu_tcp_diag.pub));
	cu_tcp_diag.pub.state = state;
	cu_tcp_diag.pub.mode = mode;
	cu_tcp_diag.pub.enabled = enabled;
	cu_tcp_diag.last_service_ms = 0u;
	cu_tcp_diag_log_line("RESET", "user_reset", 0, TRUE);
}

void cu_esp_tcp_serial_diag_flush_log(void)
{
	if(cu_tcp_diag.log_file != NULL){
		cu_tcp_diag_log_line("FLUSH", "user_flush", 0, TRUE);
		fflush(cu_tcp_diag.log_file);
	}
}

char const* cu_esp_tcp_serial_diag_get_log_path(void)
{
	return cu_tcp_diag.log_path;
}

/* ------------------------------------------------------------------------- */
/* Ping (hybrid)                                                              */
/* ------------------------------------------------------------------------- */

static sint16 cu_ping_icmp_impl(const char *host, sint16 timeout_ms);



static void cu_ping_shutdown(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(esp_state.ping_icmp_h){
		IcmpCloseHandle(esp_state.ping_icmp_h);
		esp_state.ping_icmp_h = NULL;
	}
#else
	if(esp_state.ping_icmp_sock != ESP_INVALID_SOCKET && esp_state.ping_icmp_sock != -1){
		close(esp_state.ping_icmp_sock);
		esp_state.ping_icmp_sock = ESP_INVALID_SOCKET;
	}
#endif
	esp_state.ping_mode = CU_PING_NONE;
}

static sint16 cu_ping_best(const char *host, sint16 timeout_ms)
{
	sint16 rtt;

	if(esp_state.ping_mode == CU_PING_ICMP_WIN || esp_state.ping_mode == CU_PING_ICMP_RAW){
		rtt = cu_ping_icmp_impl(host, timeout_ms);
		if(rtt >= 0)
			return rtt;
	}

	return ping_tcp(host, timeout_ms);
}

/* ------------------------------------------------------------------------- */
/* Async worker                                                               */
/* ------------------------------------------------------------------------- */

#ifndef __EMSCRIPTEN__

/* Async worker state now lives in esp_state. */

static auint cu_q_full(uint32 r, uint32 w)
{
	return (((w + 1u) % CU_ESP_ASYNC_QSZ) == r) ? 1u : 0u;
}

static auint cu_q_empty(uint32 r, uint32 w)
{
	return (r == w) ? 1u : 0u;
}

static void cu_async_lock(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	EnterCriticalSection(&esp_state.async_cs);
#else
	pthread_mutex_lock(&esp_state.async_mtx);
#endif
}

static void cu_async_unlock(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	LeaveCriticalSection(&esp_state.async_cs);
#else
	pthread_mutex_unlock(&esp_state.async_mtx);
#endif
}

static void cu_async_wake(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(esp_state.async_event)
		SetEvent(esp_state.async_event);
#else
	pthread_cond_signal(&esp_state.async_cv);
#endif
}

static auint cu_push_job(const cu_async_job_t *j)
{
	auint ok = 0u;

	cu_async_lock();
	if(!cu_q_full(esp_state.async_job_r, esp_state.async_job_w)){
		esp_state.async_jobs[esp_state.async_job_w] = *j;
		esp_state.async_job_w = (esp_state.async_job_w + 1u) % CU_ESP_ASYNC_QSZ;
		ok = 1u;
	}
	cu_async_unlock();

	if(ok)
		cu_async_wake();

	return ok;
}

static auint cu_pop_job(cu_async_job_t *j)
{
	auint ok = 0u;

	cu_async_lock();
	if(!cu_q_empty(esp_state.async_job_r, esp_state.async_job_w)){
		*j = esp_state.async_jobs[esp_state.async_job_r];
		esp_state.async_job_r = (esp_state.async_job_r + 1u) % CU_ESP_ASYNC_QSZ;
		ok = 1u;
	}
	cu_async_unlock();

	return ok;
}

static auint cu_push_evt(const cu_async_evt_t *e)
{
	auint ok = 0u;

	cu_async_lock();
	if(!cu_q_full(esp_state.async_evt_r, esp_state.async_evt_w)){
		esp_state.async_evts[esp_state.async_evt_w] = *e;
		esp_state.async_evt_w = (esp_state.async_evt_w + 1u) % CU_ESP_ASYNC_QSZ;
		ok = 1u;
	}
	cu_async_unlock();

	return ok;
}

static auint cu_pop_evt(cu_async_evt_t *e)
{
	auint ok = 0u;

	cu_async_lock();
	if(!cu_q_empty(esp_state.async_evt_r, esp_state.async_evt_w)){
		*e = esp_state.async_evts[esp_state.async_evt_r];
		esp_state.async_evt_r = (esp_state.async_evt_r + 1u) % CU_ESP_ASYNC_QSZ;
		ok = 1u;
	}
	cu_async_unlock();

	return ok;
}

static auint cu_esp_tcp_serial_host_valid(void)
{
	if(esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_SERVER)
		return (esp_state.tcp_serial_port != 0u) ? 1u : 0u;
	if(esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_LAN)
		return 1u;
	if(esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_INTERNET)
		return (cu_link_room[0] != '\0') ? 1u : 0u;
	if(esp_state.tcp_serial_mode == CU_ESP_TCP_SERIAL_MODE_AUTO && esp_state.tcp_serial_host[0] == '\0')
		return (esp_state.tcp_serial_port != 0u) ? 1u : 0u; /* empty host = 127.0.0.1 */
	return (esp_state.tcp_serial_host[0] != '\0' && esp_state.tcp_serial_port != 0u) ? 1u : 0u;
}

static void cu_esp_tcp_serial_reset_buffers(void)
{
	esp_state.tcp_serial_tx_head = 0u;
	esp_state.tcp_serial_tx_tail = 0u;
	esp_state.tcp_serial_tx_count = 0u;
	esp_state.tcp_serial_rx_head = 0u;
	esp_state.tcp_serial_rx_tail = 0u;
	esp_state.tcp_serial_rx_count = 0u;
	esp_state.tcp_serial_tx_drops = 0u;
	esp_state.tcp_serial_rx_drops = 0u;
}

static void cu_esp_tcp_serial_close_socket_only(void)
{
	if(esp_state.tcp_serial_sock != ESP_INVALID_SOCKET && esp_state.tcp_serial_sock != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.tcp_serial_sock);
#else
		close(esp_state.tcp_serial_sock);
#endif
	}
	esp_state.tcp_serial_sock = ESP_INVALID_SOCKET;
}

static void cu_esp_tcp_serial_close_listener_only(void)
{
	if(esp_state.tcp_serial_listen_sock != ESP_INVALID_SOCKET && esp_state.tcp_serial_listen_sock != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.tcp_serial_listen_sock);
#else
		close(esp_state.tcp_serial_listen_sock);
#endif
	}
	esp_state.tcp_serial_listen_sock = ESP_INVALID_SOCKET;
}

static void cu_esp_tcp_serial_schedule_retry(sint32 err)
{
	cu_esp_tcp_serial_close_socket_only();
	cu_esp_tcp_serial_close_listener_only();
	/* Preserve bytes already received before the peer closed so the AVR can
	 * consume the final packet. Local-pair mode disables silent reconnect. */
	cu_tcp_diag_log_line("SOCKET_LOSS", "connection_loss", err, TRUE);
	esp_state.tcp_serial_connect_pending = 0u;
	if(!esp_state.tcp_serial_enabled){
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, err, "retry_disabled");
		esp_state.tcp_serial_retry_at_ms = 0u;
		return;
	}
	if(cu_esp_tcp_serial_retry_enabled()){
		uint32 interval = esp_state.tcp_serial_retry_interval_ms;
		if(interval == 0u)
			interval = 1000u;
		esp_state.tcp_serial_retry_at_ms = cu_esp_now_ms() + interval;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT, err, "schedule_retry");
	}else{
		esp_state.tcp_serial_retry_at_ms = 0u;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, err, "disconnect_no_retry");
	}
}

static void cu_esp_tcp_serial_return_to_listen(sint32 err)
{
	cu_esp_tcp_serial_close_socket_only();
	/* Preserve any final bytes received before close. A manual restart calls
	 * cu_esp_tcp_serial_start(), which resets both transport queues. */
	cu_tcp_diag_log_line("SOCKET_LOSS", "client_disconnected", err, TRUE);
	esp_state.tcp_serial_connect_pending = 0u;
	if(!esp_state.tcp_serial_enabled){
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, err, "listen_return_disabled");
		esp_state.tcp_serial_retry_at_ms = 0u;
		return;
	}
	if(cu_esp_tcp_serial_effective_mode() != CU_ESP_TCP_SERIAL_MODE_SERVER){
		cu_esp_tcp_serial_schedule_retry(err);
		return;
	}
	if(!cu_esp_tcp_serial_retry_enabled()){
		cu_esp_tcp_serial_close_listener_only();
		esp_state.tcp_serial_retry_at_ms = 0u;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, err, "server_disconnect_no_retry");
		return;
	}
	if(esp_state.tcp_serial_listen_sock == ESP_INVALID_SOCKET || esp_state.tcp_serial_listen_sock == -1){
		cu_esp_tcp_serial_schedule_retry(err);
		return;
	}
	esp_state.tcp_serial_retry_at_ms = 0u;
	if(cu_esp_tcp_serial_is_auto())
		cu_tcp_auto_dwell_until_ms = cu_esp_now_ms() + 2000u + cu_esp_tcp_serial_auto_jitter_ms(2000u);
	cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_LISTENING, err, "return_to_listen");
}

static void cu_esp_tcp_serial_kick_listen(void);

static void cu_esp_tcp_serial_handle_async_connect_done(const cu_async_evt_t *e)
{
	if(!e)
		return;

	esp_state.tcp_serial_connect_pending = 0u;

	if(!esp_state.tcp_serial_enabled || cu_esp_tcp_serial_effective_mode() != CU_ESP_TCP_SERIAL_MODE_CLIENT){
		if(e->new_sock != ESP_INVALID_SOCKET && e->new_sock != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			closesocket(e->new_sock);
#else
			close(e->new_sock);
#endif
		}
		return;
	}

	if(e->err == 0 && e->new_sock != ESP_INVALID_SOCKET && e->new_sock != -1){
		cu_esp_tcp_serial_close_socket_only();
		esp_state.tcp_serial_sock = e->new_sock;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_CONNECTED, 0, "client_connected");
		esp_state.tcp_serial_retry_at_ms = 0u;
		if(cu_esp_tcp_serial_is_lan()){
			struct in_addr la;
			la.s_addr = cu_lan.paired_ip_be;
			print_message("ESP TCP Serial connected to LAN partner [%s:%u]\n", inet_ntoa(la), (unsigned int)cu_lan.paired_port);
			(void)la;
		}else{
			print_message("ESP TCP Serial connected [%s:%u]\n",
				esp_state.tcp_serial_host,
				(unsigned int)esp_state.tcp_serial_port);
		}
	}else if(cu_esp_tcp_serial_is_auto()){
		/* Nobody is listening at host:port yet: become the listener. */
		esp_state.tcp_serial_last_error = (e->err != 0) ? e->err : -1;
		cu_tcp_auto_listen = 1u;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, esp_state.tcp_serial_last_error, "auto_connect_failed_try_listen");
		cu_esp_tcp_serial_kick_listen();
	}else{
		cu_esp_tcp_serial_schedule_retry((e->err != 0) ? e->err : -1);
	}
}

#ifndef __EMSCRIPTEN__
static void cu_esp_async_dispatch_event(const cu_async_evt_t *e)
{
	if(e->kind == CU_EVT_CONNECT_DONE){
		if(e->sock == CU_ESP_TCP_SERIAL_ASYNC_SOCK){
			cu_esp_tcp_serial_handle_async_connect_done(e);
		}else if(e->sock < ESP_MAX_LINKS){
			if(e->err == 0 && e->new_sock != ESP_INVALID_SOCKET){
				memset(&esp_state.sock_info[e->sock], 0, sizeof(esp_state.sock_info[e->sock]));
				esp_state.link_peer_ipv6_valid[e->sock] = 0u;
				if(e->peer_valid){
					if(e->peer_ip[0] && strchr(e->peer_ip, ':')){
						esp_state.link_peer_ipv6_valid[e->sock] = (cu_esp_inet_pton(AF_INET6, e->peer_ip, esp_state.link_peer_ipv6[e->sock]) == 1) ? 1u : 0u;
						esp_state.link_peer_ipv6_port[e->sock] = e->peer_port;
					}else{
						esp_state.sock_info[e->sock].sin_family = AF_INET;
						esp_state.sock_info[e->sock].sin_addr.s_addr = e->peer_ipv4_be;
						esp_state.sock_info[e->sock].sin_port = htons(e->peer_port);
					}
				}
				esp_state.link_state[e->sock] = CU_ESP_LS_DELAY;
				esp_state.link_pending_sock[e->sock] = e->new_sock;
				esp_state.link_ready_ms[e->sock] = e->ready_ms;
			}else{
				esp_state.link_state[e->sock] = CU_ESP_LS_ERROR;
				esp_state.link_err[e->sock] = e->err;
				esp_state.link_notice_err[e->sock] = 1u;
			}
		}
	}else if(e->kind == CU_EVT_PING_DONE){
		esp_state.ping_rtt = e->rtt_ms;
		esp_state.ping_err = e->err;
		esp_state.ping_ready_ms = e->ready_ms;
		esp_state.ping_pending = 0u;
		esp_state.ping_ready = 1u;
	}else if(e->kind == CU_EVT_DNS_DONE){
		esp_state.dns_err = e->err;
		esp_state.dns_ipv4_be = e->ipv4_be;
		snprintf(esp_state.dns_result_text, sizeof(esp_state.dns_result_text), "%s", e->ip_text);
		esp_state.dns_ready_ms = e->ready_ms;
		esp_state.dns_pending = 0u;
		esp_state.dns_ready = 1u;
	}else if(e->kind == CU_EVT_UDP_SEND_RESOLVE_DONE){
		esp_state.udp_send_resolve_err = e->err;
		esp_state.udp_send_resolve_sock = e->sock;
		esp_state.udp_send_resolve_ready_ms = e->ready_ms;
		memset(&esp_state.udp_send_resolve_info, 0, sizeof(esp_state.udp_send_resolve_info));
	esp_state.udp_resolve_ipv6_valid = 0u;
		if(e->err == 0 && e->peer_valid){
			if(e->peer_ip[0] && strchr(e->peer_ip, ':')){
				esp_state.udp_resolve_ipv6_valid = (cu_esp_inet_pton(AF_INET6, e->peer_ip, esp_state.udp_resolve_ipv6) == 1) ? 1u : 0u;
				esp_state.udp_resolve_ipv6_port = e->peer_port;
			}else{
				esp_state.udp_resolve_ipv6_valid = 0u;
				esp_state.udp_send_resolve_info.sin_family = AF_INET;
				esp_state.udp_send_resolve_info.sin_addr.s_addr = e->peer_ipv4_be;
				esp_state.udp_send_resolve_info.sin_port = htons(e->peer_port);
			}
		}
		esp_state.udp_send_resolve_pending = 0u;
		esp_state.udp_send_resolve_ready = 1u;
	}
}

static void cu_esp_async_drain_events(void)
{
	cu_async_evt_t e;

	while(cu_pop_evt(&e))
		cu_esp_async_dispatch_event(&e);
}
#endif

static void cu_esp_tcp_serial_kick_connect(void)
{
	cu_async_job_t j;

	if(!esp_state.tcp_serial_enabled)
		return;
	if(cu_esp_tcp_serial_effective_mode() != CU_ESP_TCP_SERIAL_MODE_CLIENT)
		return;
	if(!cu_esp_tcp_serial_tcp_allowed())
		return;
	if(esp_state.tcp_serial_connect_pending)
		return;
	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED)
		return;
	if(!cu_esp_tcp_serial_host_valid()){
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, -2, "invalid_target");
		return;
	}

#ifndef __EMSCRIPTEN__
	memset(&j, 0, sizeof(j));
	j.kind = CU_JOB_CONNECT;
	j.sock = CU_ESP_TCP_SERIAL_ASYNC_SOCK;
	j.port = esp_state.tcp_serial_port;
	j.timeout_ms = 4000;
	cu_esp_u8cpy((uint8 *)j.host, sizeof(j.host),
		(esp_state.tcp_serial_host[0] != '\0') ? (const char *)esp_state.tcp_serial_host : "127.0.0.1");
	if(cu_esp_tcp_serial_is_lan()){
		struct in_addr a;
		a.s_addr = cu_lan.paired_ip_be;
		cu_esp_u8cpy((uint8 *)j.host, sizeof(j.host), inet_ntoa(a));
		j.port = cu_lan.paired_port;
	}

	if(cu_push_job(&j)){
		esp_state.tcp_serial_connect_pending = 1u;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_CONNECTING, 0, "connect_queued");
	}else{
		cu_esp_tcp_serial_schedule_retry(-1);
	}
#else
	cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, -1, "connect_unsupported");
#endif
}

static void cu_esp_tcp_serial_kick_listen(void)
{
#ifndef __EMSCRIPTEN__
	ESP_SOCKET s;
	struct sockaddr_in sa;
	int one = 1;

	if(!esp_state.tcp_serial_enabled)
		return;
	if(cu_esp_tcp_serial_effective_mode() != CU_ESP_TCP_SERIAL_MODE_SERVER)
		return;
	if(!cu_esp_tcp_serial_tcp_allowed())
		return;
	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED ||
	   esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_LISTENING)
		return;
	if(esp_state.tcp_serial_port == 0u){
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, -2, "invalid_listen_port");
		return;
	}

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s == ESP_INVALID_SOCKET || s == -1){
		cu_esp_tcp_serial_schedule_retry(-1);
		return;
	}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	/* No SO_REUSEADDR on Windows: it would let a second CUzeBox bind the same
	 * port and silently split incoming connections between the two. */
	(void)one;
	{
		u_long mode = 1u;
		ioctlsocket(s, FIONBIO, &mode);
	}
#else
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one));
	{
		int fl = fcntl(s, F_GETFL, 0);
		if(fl >= 0)
			fcntl(s, F_SETFL, fl | O_NONBLOCK);
	}
#endif
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16)esp_state.tcp_serial_port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(s, (const struct sockaddr *)&sa, (socklen_t)sizeof(sa)) != 0){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(s);
#else
		close(s);
#endif
		if(cu_esp_tcp_serial_is_auto()){
			/* Port already owned (normally by the other instance): connect. */
			cu_tcp_auto_listen = 0u;
			cu_esp_tcp_serial_close_listener_only();
			esp_state.tcp_serial_last_error = -cu_esp_get_last_error();
			cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT, esp_state.tcp_serial_last_error, "auto_port_busy_try_connect");
			esp_state.tcp_serial_retry_at_ms = cu_esp_now_ms() + 150u + cu_esp_tcp_serial_auto_jitter_ms(250u);
			return;
		}
		cu_esp_tcp_serial_schedule_retry(-cu_esp_get_last_error());
		return;
	}
	if(listen(s, 1) != 0){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(s);
#else
		close(s);
#endif
		cu_esp_tcp_serial_schedule_retry(-cu_esp_get_last_error());
		return;
	}
	cu_esp_tcp_serial_close_listener_only();
	esp_state.tcp_serial_listen_sock = s;
	esp_state.tcp_serial_retry_at_ms = 0u;
	if(cu_esp_tcp_serial_is_auto())
		cu_tcp_auto_dwell_until_ms = cu_esp_now_ms() + 2000u + cu_esp_tcp_serial_auto_jitter_ms(2000u);
	cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_LISTENING, 0, "listening");
	print_message("ESP TCP Serial listening on [0.0.0.0:%u]\n", (unsigned int)esp_state.tcp_serial_port);
#else
	cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, -1, "listen_unsupported");
#endif
}

static auint cu_esp_tcp_serial_would_block(sint32 err)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	return ((err == WSAEWOULDBLOCK) || (err == WSAEINPROGRESS) || (err == WSAEALREADY) || (err == WSAEINTR)) ? 1u : 0u;
#else
	return ((err == EAGAIN) || (err == EWOULDBLOCK) || (err == EINPROGRESS) || (err == EALREADY) || (err == EINTR)) ? 1u : 0u;
#endif
}

static void cu_esp_tcp_serial_accept_client(void)
{
#ifndef __EMSCRIPTEN__
	struct sockaddr_in peer;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	int plen = (int)sizeof(peer);
#else
	socklen_t plen = (socklen_t)sizeof(peer);
#endif
	ESP_SOCKET cs;
	int one = 1;

	if(esp_state.tcp_serial_listen_sock == ESP_INVALID_SOCKET || esp_state.tcp_serial_listen_sock == -1){
		cu_esp_tcp_serial_schedule_retry(-1);
		return;
	}
	cs = accept(esp_state.tcp_serial_listen_sock, (struct sockaddr *)&peer, &plen);
	if(cs == ESP_INVALID_SOCKET || cs == -1){
		sint32 err = cu_esp_get_last_error();
		if(cu_esp_tcp_serial_would_block(err))
			return;
		cu_esp_tcp_serial_schedule_retry(-err);
		return;
	}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, (int)sizeof(one));
	{
		u_long mode = 1u;
		ioctlsocket(cs, FIONBIO, &mode);
	}
#else
	setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, &one, (socklen_t)sizeof(one));
	{
		int fl = fcntl(cs, F_GETFL, 0);
		if(fl >= 0)
			fcntl(cs, F_SETFL, fl | O_NONBLOCK);
	}
#endif
	cu_esp_tcp_serial_close_socket_only();
	esp_state.tcp_serial_sock = cs;
	esp_state.tcp_serial_retry_at_ms = 0u;
	cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_CONNECTED, 0, "server_accepted");
	print_message("ESP TCP Serial accepted client [%s:%u]\n",
		inet_ntoa(peer.sin_addr),
		(unsigned int)ntohs(peer.sin_port));
#endif
}

static void cu_esp_tcp_serial_handle_connected_loss(sint32 err)
{
#ifndef __EMSCRIPTEN__
	if(cu_esp_tcp_serial_is_lan()){
		/* LAN partner went away: drop the pairing and search again. */
		cu_esp_tcp_serial_close_socket_only();
		cu_esp_tcp_serial_close_listener_only();
		esp_state.tcp_serial_connect_pending = 0u;
		cu_lan_unpair();
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_SEARCHING, err, "lan_peer_lost");
		return;
	}
#endif
	if(cu_esp_tcp_serial_effective_mode() == CU_ESP_TCP_SERIAL_MODE_SERVER)
		cu_esp_tcp_serial_return_to_listen(err);
	else
		cu_esp_tcp_serial_schedule_retry(err);
}

static uint32 cu_esp_tcp_serial_budget_bytes(void)
{
	switch(esp_state.uart_profile){
		case CU_UART_PROFILE_FAST:
			return 512u;
		case CU_UART_PROFILE_DEBUG:
			return 4096u;
		case CU_UART_PROFILE_BALANCED:
		default:
			return 2048u;
	}
}

static uint32 cu_esp_tcp_serial_budget_calls(void)
{
	switch(esp_state.uart_profile){
		case CU_UART_PROFILE_FAST:
			return 2u;
		case CU_UART_PROFILE_DEBUG:
			return 8u;
		case CU_UART_PROFILE_BALANCED:
		default:
			return 4u;
	}
}

static uint32 cu_esp_server_accept_budget(void)
{
	switch(esp_state.uart_profile){
		case CU_UART_PROFILE_FAST:
			return 1u;
		case CU_UART_PROFILE_DEBUG:
			return 4u;
		case CU_UART_PROFILE_BALANCED:
		default:
			return 2u;
	}
}

static void cu_esp_tcp_serial_service_connected(void)
{
	uint32 tx_budget = cu_esp_tcp_serial_budget_bytes();
	uint32 rx_budget = cu_esp_tcp_serial_budget_bytes();
	uint32 tx_calls = 0u;
	uint32 rx_calls = 0u;
	uint32 max_calls = cu_esp_tcp_serial_budget_calls();

	while(esp_state.tcp_serial_tx_count != 0u && tx_budget != 0u && tx_calls < max_calls){
		uint32 chunk = (uint32)sizeof(esp_state.tcp_serial_tx_buf) - esp_state.tcp_serial_tx_head;
		sint32 rc;
		if(chunk > esp_state.tcp_serial_tx_count)
			chunk = esp_state.tcp_serial_tx_count;
		if(chunk > tx_budget)
			chunk = tx_budget;
		cu_tcp_diag.pub.send_calls++;
		rc = (sint32)send(esp_state.tcp_serial_sock,
			(const char *)&esp_state.tcp_serial_tx_buf[esp_state.tcp_serial_tx_head],
			(int)chunk,
			0);
		tx_calls++;
		if(rc > 0){
			cu_tcp_diag.pub.socket_tx_bytes += (uint32)rc;
			cu_tcp_diag.pub.last_socket_tx_ms = cu_tcp_diag.pub.current_ms;
			esp_state.tcp_serial_tx_head = (esp_state.tcp_serial_tx_head + (uint32)rc) % (uint32)sizeof(esp_state.tcp_serial_tx_buf);
			esp_state.tcp_serial_tx_count -= (uint32)rc;
			if((uint32)rc >= tx_budget)
				tx_budget = 0u;
			else
				tx_budget -= (uint32)rc;
			continue;
		}
		if(rc == 0){
			cu_tcp_diag.pub.send_zero_returns++;
			cu_tcp_diag.pub.last_send_error = -1;
			cu_tcp_diag_log_line("SEND_ZERO", "peer_closed_or_zero", -1, TRUE);
			cu_esp_tcp_serial_handle_connected_loss(-1);
			return;
		}
		{
			sint32 err = cu_esp_get_last_error();
			cu_tcp_diag.pub.last_send_error = err;
			if(cu_esp_tcp_serial_would_block(err)){
				cu_tcp_diag.pub.send_would_block++;
				break;
			}
			cu_tcp_diag.pub.send_errors++;
			cu_tcp_diag_log_line("SEND_ERROR", "send_failed", err, TRUE);
			cu_esp_tcp_serial_handle_connected_loss(-err);
			return;
		}
	}

#ifndef __EMSCRIPTEN__
	if(cu_imp_active()){
		/* Impairment simulator on: bytes go through its delay queue. */
		while(rx_budget != 0u && rx_calls < max_calls && cu_imp.count < CU_IMP_CAP){
			uint8 tmp[512];
			uint32 chunk = CU_IMP_CAP - cu_imp.count;
			sint32 rc;
			if(chunk > (uint32)sizeof(tmp)) chunk = (uint32)sizeof(tmp);
			if(chunk > rx_budget) chunk = rx_budget;
			cu_tcp_diag.pub.recv_calls++;
			rc = (sint32)recv(esp_state.tcp_serial_sock, (char *)tmp, (int)chunk, 0);
			rx_calls++;
			if(rc > 0){
				cu_tcp_diag.pub.socket_rx_bytes += (uint32)rc;
				cu_tcp_diag.pub.last_socket_rx_ms = cu_tcp_diag.pub.current_ms;
				cu_imp_push(tmp, (uint32)rc, cu_esp_now_ms());
				rx_budget = ((uint32)rc >= rx_budget) ? 0u : rx_budget - (uint32)rc;
				continue;
			}
			if(rc == 0){
				cu_tcp_diag.pub.recv_zero_returns++;
				cu_tcp_diag_log_line("RECV_ZERO", "peer_closed", -1, TRUE);
				cu_esp_tcp_serial_handle_connected_loss(-1);
				return;
			}
			{
				sint32 err = cu_esp_get_last_error();
				cu_tcp_diag.pub.last_recv_error = err;
				if(cu_esp_tcp_serial_would_block(err)){
					cu_tcp_diag.pub.recv_would_block++;
					break;
				}
				cu_tcp_diag.pub.recv_errors++;
				cu_tcp_diag_log_line("RECV_ERROR", "recv_failed", err, TRUE);
				cu_esp_tcp_serial_handle_connected_loss(-err);
				return;
			}
		}
		return;
	}
#endif

	while(esp_state.tcp_serial_rx_count < (uint32)sizeof(esp_state.tcp_serial_rx_buf) && rx_budget != 0u && rx_calls < max_calls){
		uint32 avail = (uint32)sizeof(esp_state.tcp_serial_rx_buf) - esp_state.tcp_serial_rx_count;
		uint32 chunk = (uint32)sizeof(esp_state.tcp_serial_rx_buf) - esp_state.tcp_serial_rx_tail;
		sint32 rc;
		if(chunk > avail)
			chunk = avail;
		if(chunk > rx_budget)
			chunk = rx_budget;
		cu_tcp_diag.pub.recv_calls++;
		rc = (sint32)recv(esp_state.tcp_serial_sock,
			(char *)&esp_state.tcp_serial_rx_buf[esp_state.tcp_serial_rx_tail],
			(int)chunk,
			0);
		rx_calls++;
		if(rc > 0){
			cu_tcp_diag.pub.socket_rx_bytes += (uint32)rc;
			cu_tcp_diag.pub.last_socket_rx_ms = cu_tcp_diag.pub.current_ms;
			esp_state.tcp_serial_rx_tail = (esp_state.tcp_serial_rx_tail + (uint32)rc) % (uint32)sizeof(esp_state.tcp_serial_rx_buf);
			esp_state.tcp_serial_rx_count += (uint32)rc;
			if((uint32)rc >= rx_budget)
				rx_budget = 0u;
			else
				rx_budget -= (uint32)rc;
			continue;
		}
		if(rc == 0){
			cu_tcp_diag.pub.recv_zero_returns++;
			cu_tcp_diag.pub.last_recv_error = -1;
			cu_tcp_diag_log_line("RECV_ZERO", "peer_closed", -1, TRUE);
			cu_esp_tcp_serial_handle_connected_loss(-1);
			return;
		}
		{
			sint32 err = cu_esp_get_last_error();
			cu_tcp_diag.pub.last_recv_error = err;
			if(cu_esp_tcp_serial_would_block(err)){
				cu_tcp_diag.pub.recv_would_block++;
				break;
			}
			cu_tcp_diag.pub.recv_errors++;
			cu_tcp_diag_log_line("RECV_ERROR", "recv_failed", err, TRUE);
			cu_esp_tcp_serial_handle_connected_loss(-err);
			return;
		}
	}
}

#ifndef __EMSCRIPTEN__
static uint32 cu_esp_link_state_from_phase(void)
{
	switch(cu_lk.phase){
	case CU_LK_PH_PEER:      return cu_lk.connected ? CU_ESP_TCP_SERIAL_STATE_CONNECTED : CU_ESP_TCP_SERIAL_STATE_CONNECTING;
	case CU_LK_PH_WAIT_PEER: return CU_ESP_TCP_SERIAL_STATE_WAITING_PEER;
	case CU_LK_PH_JOINING:
	case CU_LK_PH_CREATING:  return CU_ESP_TCP_SERIAL_STATE_CONNECTING;
	case CU_LK_PH_RETRY:     return CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT;
	default:                 return CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
	}
}

/* INTERNET mode: move bytes between the endpoint rings and the relay link. */
static void cu_esp_tcp_serial_service_internet(uint32 now)
{
	uint32 st;
	cu_lk_service(now);
	if(cu_lk.connected && esp_state.tcp_serial_tx_count != 0u){
		uint8 tmp[CU_LK_MAX_DATA * 4u];
		uint32 n = 0u, used, i;
		while(n < (uint32)sizeof(tmp) && n < esp_state.tcp_serial_tx_count){
			tmp[n] = esp_state.tcp_serial_tx_buf[(esp_state.tcp_serial_tx_head + n) % (uint32)sizeof(esp_state.tcp_serial_tx_buf)];
			n++;
		}
		used = cu_lk_write(tmp, n, now);
		for(i = 0u; i < used; i++){
			esp_state.tcp_serial_tx_head = (esp_state.tcp_serial_tx_head + 1u) % (uint32)sizeof(esp_state.tcp_serial_tx_buf);
			esp_state.tcp_serial_tx_count--;
		}
		cu_tcp_diag.pub.socket_tx_bytes += used;
		if(used != 0u)
			cu_tcp_diag.pub.last_socket_tx_ms = cu_tcp_diag.pub.current_ms;
	}
	for(;;){
		uint8 tmp[1024];
		uint32 room = cu_imp_active() ? (CU_IMP_CAP - cu_imp.count) : cu_esp_tcp_serial_rx_room();
		uint32 n;
		if(room > (uint32)sizeof(tmp)) room = (uint32)sizeof(tmp);
		if(room == 0u) break;
		n = cu_lk_read(tmp, room);
		if(n == 0u) break;
		cu_tcp_diag.pub.socket_rx_bytes += n;
		cu_tcp_diag.pub.last_socket_rx_ms = cu_tcp_diag.pub.current_ms;
		cu_esp_tcp_serial_ingest(tmp, n, now);
	}
	st = cu_esp_link_state_from_phase();
	if(st != esp_state.tcp_serial_state)
		cu_tcp_diag_note_state(st, 0, cu_lk.notice[0] ? cu_lk.notice : "internet_link");
}

/* LAN mode: discovery decides the partner and the role, TCP does the rest. */
static boole cu_esp_tcp_serial_service_lan(uint32 now)
{
	boole new_pair = cu_lan_service(now);
	if(new_pair){
		cu_esp_tcp_serial_close_socket_only();
		cu_esp_tcp_serial_close_listener_only();
		esp_state.tcp_serial_connect_pending = 0u;
		cu_link_lan_server = cu_lan.paired_server;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, 0, "lan_paired");
		if(cu_link_lan_server)
			cu_esp_tcp_serial_kick_listen();
		else
			cu_esp_tcp_serial_kick_connect();
	}
	if(cu_lan.paired_id == 0u){
		if(esp_state.tcp_serial_state != CU_ESP_TCP_SERIAL_STATE_SEARCHING)
			cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_SEARCHING, 0, "lan_searching");
		return FALSE;
	}
	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED){
		cu_lan.linked = 1u;
	}else if(!cu_lan.linked && CU_LK_TIME_GE(now, cu_lan.paired_at + CU_LAN_PAIR_TIMEOUT)){
		cu_esp_tcp_serial_close_socket_only();
		cu_esp_tcp_serial_close_listener_only();
		esp_state.tcp_serial_connect_pending = 0u;
		cu_lan_unpair();
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_SEARCHING, -1, "lan_pair_timeout");
		return FALSE;
	}
	return TRUE;
}
#endif

static void cu_esp_tcp_serial_service(void)
{
	if(!esp_state.tcp_serial_enabled)
		return;
	cu_tcp_diag_service_tick();
	cu_esp_tcp_serial_impair_release(cu_esp_now_ms());

#ifndef __EMSCRIPTEN__
	if(cu_esp_tcp_serial_is_internet()){
		cu_esp_tcp_serial_service_internet(cu_esp_now_ms());
		cu_esp_tcp_serial_impair_release(cu_esp_now_ms());
		return;
	}
	if(cu_esp_tcp_serial_is_lan()){
		if(!cu_esp_tcp_serial_service_lan(cu_esp_now_ms()))
			return;
	}
#endif

#ifndef __EMSCRIPTEN__
	/*
	 * The raw TCP serial route bypasses the ESP AT timer path. Drain the
	 * shared async queue while its client connect is pending so the worker's
	 * completed socket is adopted even when no ESP-module tick is running.
	 */
	if(esp_state.tcp_serial_connect_pending ||
	   esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_CONNECTING)
		cu_esp_async_drain_events();
#endif

	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_CONNECTED){
		if(esp_state.tcp_serial_sock == ESP_INVALID_SOCKET || esp_state.tcp_serial_sock == -1){
			cu_esp_tcp_serial_handle_connected_loss(-1);
			return;
		}
		cu_esp_tcp_serial_service_connected();
		cu_esp_tcp_serial_impair_release(cu_esp_now_ms());
		return;
	}

	if(cu_esp_tcp_serial_effective_mode() == CU_ESP_TCP_SERIAL_MODE_SERVER){
		if(cu_esp_tcp_serial_is_auto() &&
		   esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_LISTENING &&
		   !cu_esp_tcp_serial_host_is_loopback() &&
		   (sint32)(cu_esp_now_ms() - cu_tcp_auto_dwell_until_ms) >= 0){
			/* Remote AUTO peer: both sides may be listening. Stop listening
			 * for a moment and try connecting to it instead. */
			cu_esp_tcp_serial_close_listener_only();
			cu_tcp_auto_listen = 0u;
			cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, 0, "auto_listen_dwell_try_connect");
			cu_esp_tcp_serial_kick_connect();
			return;
		}
		if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT){
			if(cu_esp_now_ms() < esp_state.tcp_serial_retry_at_ms)
				return;
			cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, esp_state.tcp_serial_last_error, "server_retry_elapsed");
		}
		if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_DISCONNECTED &&
		   cu_esp_tcp_serial_retry_enabled())
			cu_esp_tcp_serial_kick_listen();
		if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_LISTENING)
			cu_esp_tcp_serial_accept_client();
		return;
	}

	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_CONNECTING)
		return;

	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_RETRY_WAIT){
		if(cu_esp_now_ms() < esp_state.tcp_serial_retry_at_ms)
			return;
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, esp_state.tcp_serial_last_error, "client_retry_elapsed");
	}

	if(esp_state.tcp_serial_state == CU_ESP_TCP_SERIAL_STATE_DISCONNECTED &&
	   cu_esp_tcp_serial_retry_enabled())
		cu_esp_tcp_serial_kick_connect();
}

/* ------------------------------------------------------------------------- */
/* Socket helpers                                                             */
/* ------------------------------------------------------------------------- */

static void cu_sock_set_nonblocking(ESP_SOCKET s)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	u_long mode = 1u;
	ioctlsocket(s, FIONBIO, &mode);
#else
	int fl = fcntl(s, F_GETFL, 0);
	if(fl >= 0)
		fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

static sint32 cu_sock_connect_timeout(ESP_SOCKET s, const struct sockaddr *sa, socklen_t salen, sint32 timeout_ms)
{
	sint32 rc;

	cu_sock_set_nonblocking(s);

	rc = connect(s, sa, salen);
	if(rc == 0)
		return 0;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		sint32 e = WSAGetLastError();
		if(e != WSAEWOULDBLOCK && e != WSAEINPROGRESS && e != WSAEALREADY)
			return -e;
	}
#else
	if(errno != EINPROGRESS && errno != EALREADY)
		return -errno;
#endif

	if(timeout_ms <= 0)
		timeout_ms = 5000;

	{
		fd_set wfds;
		struct timeval tv;

		FD_ZERO(&wfds);
		FD_SET(s, &wfds);

		tv.tv_sec = (long)(timeout_ms / 1000);
		tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		rc = select(0, NULL, &wfds, NULL, &tv);
#else
		rc = select((int)(s + 1), NULL, &wfds, NULL, &tv);
#endif
		if(rc <= 0)
			return -1;
	}

	{
		int soerr = 0;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		int slen = (int)sizeof(soerr);
		getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &slen);
#else
		socklen_t slen = (socklen_t)sizeof(soerr);
		getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &slen);
#endif
		if(soerr != 0)
			return -soerr;
	}

	return 0;
}

static uint16 cu_udp_local_port_of(ESP_SOCKET s)
{
	struct sockaddr_storage ss;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	int slen = (int)sizeof(ss);
#else
	socklen_t slen = (socklen_t)sizeof(ss);
#endif
	if(s == ESP_INVALID_SOCKET || s == -1) return 0u;
	memset(&ss,0,sizeof(ss));
	if(getsockname(s,(struct sockaddr*)&ss,&slen)!=0) return 0u;
	if(ss.ss_family==AF_INET) return (uint16)ntohs(((struct sockaddr_in*)&ss)->sin_port);
	if(ss.ss_family==AF_INET6) return (uint16)ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
	return 0u;
}

uint16 cu_esp_net_get_local_port(uint32 link)
{
	if(link>=ESP_MAX_LINKS) return 0u;
	if((esp_state.protocol[link] & ESP_PROTO_UDP) && esp_state.udp_local_port[link]) return esp_state.udp_local_port[link];
	return cu_udp_local_port_of(esp_state.socks[link]);
}

sint32 cu_esp_net_get_peer_text(uint32 link, char *out, auint out_sz, uint16 *port)
{
	if(!out || out_sz==0 || link>=ESP_MAX_LINKS) return -1;
	out[0]=0; if(port) *port=0u;
	if(esp_state.link_peer_ipv6_valid[link]){
		if(!cu_esp_inet_ntop(AF_INET6,esp_state.link_peer_ipv6[link],out,(socklen_t)out_sz)) return -1;
		if(port) *port=esp_state.link_peer_ipv6_port[link];
		return 0;
	}
	if(esp_state.sock_info[link].sin_family==AF_INET){
		if(!cu_esp_inet_ntop(AF_INET,&esp_state.sock_info[link].sin_addr,out,(socklen_t)out_sz)) return -1;
		if(port) *port=(uint16)ntohs(esp_state.sock_info[link].sin_port);
		return 0;
	}
	snprintf(out,(size_t)out_sz,"0.0.0.0"); return -1;
}

void cu_esp_net_reapply_tcp_options(uint32 link)
{
	ESP_SOCKET s;
	if(link>=ESP_MAX_LINKS || !(esp_state.protocol[link] & ESP_PROTO_TCP)) return;
	s=esp_state.socks[link]; if(s==ESP_INVALID_SOCKET || s==-1) return;
	{ struct linger lg; memset(&lg,0,sizeof(lg)); if(esp_state.tcp_linger[link]>=0){lg.l_onoff=1;lg.l_linger=(u_short)esp_state.tcp_linger[link];} setsockopt(s,SOL_SOCKET,SO_LINGER,(const char*)&lg,(socklen_t)sizeof(lg)); }
	{ int v=esp_state.tcp_nodelay[link]?1:0; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&v,(socklen_t)sizeof(v)); }
	if(esp_state.tcp_sndtimeo[link]){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		DWORD ms=(DWORD)esp_state.tcp_sndtimeo[link]; setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&ms,(int)sizeof(ms));
#else
		struct timeval tv; tv.tv_sec=(time_t)(esp_state.tcp_sndtimeo[link]/1000u);tv.tv_usec=(suseconds_t)((esp_state.tcp_sndtimeo[link]%1000u)*1000u);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,(socklen_t)sizeof(tv));
#endif
	}
	if(esp_state.tcp_keepalive[link]){ int one=1;setsockopt(s,SOL_SOCKET,SO_KEEPALIVE,(const char*)&one,(socklen_t)sizeof(one));
#ifdef TCP_KEEPIDLE
		{int sec=(int)esp_state.tcp_keepalive[link];setsockopt(s,IPPROTO_TCP,TCP_KEEPIDLE,(const char*)&sec,(socklen_t)sizeof(sec));}
#endif
	}
}

static int cu_bind_local_addr(ESP_SOCKET s, int family, const char *ip, uint16 port)
{
	if(family == AF_INET6){
		struct sockaddr_in6 sa6; memset(&sa6,0,sizeof(sa6)); sa6.sin6_family=AF_INET6;sa6.sin6_port=htons(port);
		if(ip && *ip){ if(cu_esp_inet_pton(AF_INET6,ip,&sa6.sin6_addr)!=1)return -1; } else sa6.sin6_addr=in6addr_any;
		return bind(s,(const struct sockaddr*)&sa6,(socklen_t)sizeof(sa6));
	}else{
		struct sockaddr_in sa; memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons(port);sa.sin_addr.s_addr=htonl(INADDR_ANY);
		if(ip&&*ip){ if(cu_esp_inet_pton(AF_INET,ip,&sa.sin_addr)!=1)return -1; }
		return bind(s,(const struct sockaddr*)&sa,(socklen_t)sizeof(sa));
	}
}

static void cu_evt_set_peer(cu_async_evt_t *e, const struct sockaddr *sa)
{
	if(!e||!sa)return;
	if(sa->sa_family==AF_INET){const struct sockaddr_in*s=(const struct sockaddr_in*)sa;e->peer_valid=1u;e->peer_ipv4_be=s->sin_addr.s_addr;e->peer_port=ntohs(s->sin_port);cu_esp_inet_ntop(AF_INET,&s->sin_addr,e->peer_ip,sizeof(e->peer_ip));}
	else if(sa->sa_family==AF_INET6){const struct sockaddr_in6*s=(const struct sockaddr_in6*)sa;e->peer_valid=1u;e->peer_port=ntohs(s->sin6_port);cu_esp_inet_ntop(AF_INET6,&s->sin6_addr,e->peer_ip,sizeof(e->peer_ip));}
}

/* ------------------------------------------------------------------------- */
/* Worker job handlers                                                        */
/* ------------------------------------------------------------------------- */

static void cu_worker_do_connect(const cu_async_job_t *j)
{
	cu_async_evt_t e; memset(&e,0,sizeof(e));e.kind=CU_EVT_CONNECT_DONE;e.sock=j->sock;e.type=j->type;e.new_sock=ESP_INVALID_SOCKET;e.ready_ms=cu_esp_now_ms()+50u+(uint32)(rand()%200u);
	int family=(j->type&ESP_PROTO_IPV6)?AF_INET6:AF_INET;
	int stype=(j->type&ESP_PROTO_UDP)?SOCK_DGRAM:SOCK_STREAM;
	int proto=(j->type&ESP_PROTO_UDP)?IPPROTO_UDP:IPPROTO_TCP;
	struct addrinfo hints,*res=NULL,*it=NULL;char portbuf[16];memset(&hints,0,sizeof(hints));hints.ai_family=family;hints.ai_socktype=stype;hints.ai_protocol=proto;snprintf(portbuf,sizeof(portbuf),"%u",(unsigned)j->port);
	if(j->host[0] && j->port){if(getaddrinfo(j->host,portbuf,&hints,&res)!=0||!res){e.err=-2;cu_push_evt(&e);return;}}
	if(!res && (j->type&ESP_PROTO_UDP)){
		ESP_SOCKET so=socket(family,stype,proto);if(so==ESP_INVALID_SOCKET||so==-1){e.err=-1;cu_push_evt(&e);return;}
		if(j->local_port||j->local_ip[0]){if(cu_bind_local_addr(so,family,j->local_ip,(uint16)j->local_port)!=0){
#if defined(WIN32)||defined(_WIN32)||defined(__CYGWIN__)||defined(__MINGW32__)
			closesocket(so);
#else
			close(so);
#endif
			e.err=-3;cu_push_evt(&e);return;}}
		cu_sock_set_nonblocking(so);e.new_sock=so;e.err=0;cu_push_evt(&e);return;
	}
	for(it=res;it;it=it->ai_next){
		ESP_SOCKET so=socket(it->ai_family,it->ai_socktype,it->ai_protocol);if(so==ESP_INVALID_SOCKET||so==-1)continue;
		if(j->local_port||j->local_ip[0]){if(cu_bind_local_addr(so,it->ai_family,j->local_ip,(uint16)j->local_port)!=0){
#if defined(WIN32)||defined(_WIN32)||defined(__CYGWIN__)||defined(__MINGW32__)
			closesocket(so);
#else
			close(so);
#endif
			continue;}}
		cu_evt_set_peer(&e,it->ai_addr);
		if((j->type&ESP_PROTO_UDP)&&j->udp_mode!=0u){cu_sock_set_nonblocking(so);e.new_sock=so;e.err=0;break;}
		if(cu_sock_connect_timeout(so,it->ai_addr,(socklen_t)it->ai_addrlen,(sint32)(j->timeout_ms32 ? j->timeout_ms32 : (uint32)((j->timeout_ms > 0) ? j->timeout_ms : 5000)))==0){cu_sock_set_nonblocking(so);e.new_sock=so;e.err=0;break;}
#if defined(WIN32)||defined(_WIN32)||defined(__CYGWIN__)||defined(__MINGW32__)
		closesocket(so);
#else
		close(so);
#endif
	}
	if(res) freeaddrinfo(res);
	if(e.new_sock==ESP_INVALID_SOCKET && e.err==0) e.err=-1;
	cu_push_evt(&e);
}

static void cu_worker_do_ping(const cu_async_job_t *j)
{
	cu_async_evt_t e;
	memset(&e, 0, sizeof(e));

	e.kind = CU_EVT_PING_DONE;
	e.err = 0;
	e.rtt_ms = cu_ping_best(j->host, j->timeout_ms);
	if(e.rtt_ms < 0)
		e.err = -1;

	e.ready_ms = cu_esp_now_ms() + (50u + (uint32)(rand() % 200u));

	cu_push_evt(&e);
}

static void cu_worker_do_dns(const cu_async_job_t *j)
{
	cu_async_evt_t e;memset(&e,0,sizeof(e));e.kind=CU_EVT_DNS_DONE;e.err=-1;e.ready_ms=cu_esp_now_ms()+20u+(uint32)(rand()%80u);
	struct addrinfo hints,*res=NULL,*it=NULL;memset(&hints,0,sizeof(hints));hints.ai_family=(j->ip_network==2)?AF_INET:(j->ip_network==3)?AF_INET6:AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
	if(getaddrinfo(j->host,NULL,&hints,&res)==0&&res){
		/* ip_network=1 prefers IPv4. */
		if(j->ip_network==0||j->ip_network==1){for(it=res;it;it=it->ai_next)if(it->ai_family==AF_INET)break;}
		if(!it)it=res;
		if(it->ai_family==AF_INET){struct sockaddr_in*sa=(struct sockaddr_in*)it->ai_addr;e.ipv4_be=sa->sin_addr.s_addr;cu_esp_inet_ntop(AF_INET,&sa->sin_addr,e.ip_text,sizeof(e.ip_text));e.err=0;}
		else if(it->ai_family==AF_INET6){struct sockaddr_in6*sa=(struct sockaddr_in6*)it->ai_addr;cu_esp_inet_ntop(AF_INET6,&sa->sin6_addr,e.ip_text,sizeof(e.ip_text));e.err=0;}
		freeaddrinfo(res);
	}cu_push_evt(&e);
}

static void cu_worker_do_udp_send_resolve(const cu_async_job_t *j)
{
	cu_async_evt_t e;memset(&e,0,sizeof(e));e.kind=CU_EVT_UDP_SEND_RESOLVE_DONE;e.sock=j->sock;e.err=-1;e.ready_ms=cu_esp_now_ms()+20u+(uint32)(rand()%80u);
	struct addrinfo hints,*res=NULL;char portbuf[16];memset(&hints,0,sizeof(hints));hints.ai_family=(j->ip_network==3)?AF_INET6:AF_INET;hints.ai_socktype=SOCK_DGRAM;hints.ai_protocol=IPPROTO_UDP;snprintf(portbuf,sizeof(portbuf),"%u",(unsigned)j->port);
	if(getaddrinfo(j->host,portbuf,&hints,&res)==0&&res){cu_evt_set_peer(&e,res->ai_addr);e.err=0;freeaddrinfo(res);}cu_push_evt(&e);
}

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)

static DWORD WINAPI cu_async_thread_main(LPVOID p)
{
	(void)p;

	while(esp_state.async_run){
		WaitForSingleObject(esp_state.async_event, INFINITE);
		if(!esp_state.async_run)
			break;

		for(;;){
			cu_async_job_t j;
			if(!cu_pop_job(&j))
				break;

			if(j.kind == CU_JOB_CONNECT)
				cu_worker_do_connect(&j);
			else if(j.kind == CU_JOB_PING)
				cu_worker_do_ping(&j);
			else if(j.kind == CU_JOB_DNS)
				cu_worker_do_dns(&j);
			else if(j.kind == CU_JOB_UDP_SEND_RESOLVE)
				cu_worker_do_udp_send_resolve(&j);
		}

		ResetEvent(esp_state.async_event);
	}

	return 0;
}

#else

static void *cu_async_thread_main(void *p)
{
	(void)p;

	while(esp_state.async_run){
		cu_async_job_t j;

		pthread_mutex_lock(&esp_state.async_mtx);
		while(esp_state.async_run && cu_q_empty(esp_state.async_job_r, esp_state.async_job_w))
			pthread_cond_wait(&esp_state.async_cv, &esp_state.async_mtx);
		pthread_mutex_unlock(&esp_state.async_mtx);

		if(!esp_state.async_run)
			break;

		while(cu_pop_job(&j)){
			if(j.kind == CU_JOB_CONNECT)
				cu_worker_do_connect(&j);
			else if(j.kind == CU_JOB_PING)
				cu_worker_do_ping(&j);
			else if(j.kind == CU_JOB_DNS)
				cu_worker_do_dns(&j);
			else if(j.kind == CU_JOB_UDP_SEND_RESOLVE)
				cu_worker_do_udp_send_resolve(&j);
		}
	}

	return NULL;
}

#endif

static void cu_async_start_once(void)
{
	if(esp_state.async_run)
		return;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(!esp_state.async_sync_init){
		InitializeCriticalSection(&esp_state.async_cs);
		esp_state.async_event = CreateEvent(NULL, TRUE, FALSE, NULL);
		esp_state.async_sync_init = 1u;
	}
#else
	if(!esp_state.async_sync_init){
		pthread_mutex_init(&esp_state.async_mtx, NULL);
		pthread_cond_init(&esp_state.async_cv, NULL);
		esp_state.async_sync_init = 1u;
	}
#endif

	esp_state.async_run = 1;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	esp_state.async_thread = CreateThread(NULL, 0, cu_async_thread_main, NULL, 0, NULL);
#else
	pthread_create(&esp_state.async_thread, NULL, cu_async_thread_main, NULL);
#endif
}

static void cu_async_stop(void)
{
	if(!esp_state.async_run)
		return;

	esp_state.async_run = 0;
	cu_async_wake();

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(esp_state.async_event)
		SetEvent(esp_state.async_event);

	if(esp_state.async_thread){
		WaitForSingleObject(esp_state.async_thread, INFINITE);
		CloseHandle(esp_state.async_thread);
		esp_state.async_thread = NULL;
	}
#else
	pthread_join(esp_state.async_thread, NULL);
#endif

	esp_state.async_job_r = esp_state.async_job_w = 0u;
	esp_state.async_evt_r = esp_state.async_evt_w = 0u;
}

#endif /* !__EMSCRIPTEN__ */

/* ------------------------------------------------------------------------- */
/* Link state + TLS                                                           */
/* ------------------------------------------------------------------------- */

/* Link/TLS runtime state now lives in esp_state. */

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
static unsigned int cu_tls_psk_client_cb(SSL *ssl, const char *hint, char *identity, unsigned int max_identity_len, unsigned char *psk, unsigned int max_psk_len)
{
	uint32 sock = 0u;
	const char *id;
	size_t id_len;
	size_t key_len;
	(void)hint;

	if(!ssl || !identity || !psk)
		return 0u;

	sock = (uint32)((size_t)SSL_get_app_data(ssl));
	if(sock == 0u)
		return 0u;
	sock--;

	if(sock >= ESP_MAX_LINKS)
		return 0u;

	/* ESP-AT's <hint> is the TLS PSK identity; <psk> is the actual
	 * pre-shared secret.  Earlier CUzeBox code used the two backwards. */
	id = (const char *)esp_state.ssl_psk_key[sock];
	key_len = (size_t)esp_state.ssl_psk_bin_len[sock];
	if(!id || !id[0] || key_len == 0u)
		return 0u;

	id_len = strlen(id);
	if(id_len + 1u > max_identity_len || key_len > max_psk_len)
		return 0u;

	memcpy(identity, id, id_len);
	identity[id_len] = '\0';
	memcpy(psk, esp_state.ssl_psk_bin[sock], key_len);
	return (unsigned int)key_len;
}

static const char *cu_tls_slot_path(sint8 paths[ESP_SSL_SLOT_COUNT][ESP_SSL_PATH_MAX], uint32 slot)
{
	if(slot >= ESP_SSL_SLOT_COUNT)
		return NULL;
	if(paths[slot][0] == 0)
		return NULL;
	return (const char *)paths[slot];
}

static void cu_tls_ctx_free(uint32 sock)
{
	if(sock >= ESP_MAX_LINKS)
		return;
	if(esp_state.tls_ssl[sock]){
		SSL_free(esp_state.tls_ssl[sock]);
		esp_state.tls_ssl[sock] = NULL;
	}
	if(esp_state.tls_ctx[sock]){
		SSL_CTX_free(esp_state.tls_ctx[sock]);
		esp_state.tls_ctx[sock] = NULL;
	}
}

static uint8 cu_tls_uses_virtual(uint32 sock)
{
	if(sock >= ESP_MAX_LINKS)
		return 0u;
	if((esp_state.link_type[sock] & ESP_PROTO_SSL) == 0u &&
	   (esp_state.proto[sock] & ESP_PROTO_SSL) == 0u &&
	   (esp_state.protocol[sock] & ESP_PROTO_SSL) == 0u &&
	   esp_state.link_is_ssl[sock] == 0u)
		return 0u;
	return cu_esp_lan_virtual_link_present(&esp_state, sock);
}

static void cu_tls_virtual_update_eof(uint32 sock)
{
	BIO *rbio;
	if(sock >= ESP_MAX_LINKS || esp_state.tls_ssl[sock] == NULL)
		return;
	if(!cu_tls_uses_virtual(sock) || !cu_esp_lan_virtual_link_present(&esp_state, sock))
		return;
	rbio = SSL_get_rbio(esp_state.tls_ssl[sock]);
	if(rbio == NULL)
		return;
	if(esp_state.lan.vlinks[sock].closed_remote && esp_state.lan.vlinks[sock].rx_len == 0u)
		BIO_set_mem_eof_return(rbio, 0);
	else
		BIO_set_mem_eof_return(rbio, -1);
}

static void cu_tls_virtual_flush_out(uint32 sock)
{
	BIO *wbio;
	char obuf[CU_ESP_LAN_DATA_MAX];
	int rd;
	if(sock >= ESP_MAX_LINKS || esp_state.tls_ssl[sock] == NULL)
		return;
	if(!cu_tls_uses_virtual(sock) || !cu_esp_lan_virtual_link_open(&esp_state, sock))
		return;
	wbio = SSL_get_wbio(esp_state.tls_ssl[sock]);
	if(wbio == NULL)
		return;
	for(;;){
		rd = BIO_read(wbio, obuf, (int)sizeof(obuf));
		if(rd <= 0)
			break;
		if(cu_esp_lan_virtual_send(&esp_state, sock, (const uint8 *)obuf, (uint16)rd) < 0)
			break;
	}
}

static void cu_tls_virtual_feed_in(uint32 sock)
{
	BIO *rbio;
	sint32 rd;
	uint8 ibuf[CU_ESP_LAN_DATA_MAX];
	if(sock >= ESP_MAX_LINKS || esp_state.tls_ssl[sock] == NULL)
		return;
	if(!cu_tls_uses_virtual(sock))
		return;
	rbio = SSL_get_rbio(esp_state.tls_ssl[sock]);
	if(rbio == NULL)
		return;
	cu_tls_virtual_update_eof(sock);
	for(;;){
		rd = cu_esp_lan_virtual_recv(&esp_state, sock, ibuf, (uint16)sizeof(ibuf));
		if(rd <= 0)
			break;
		(void)BIO_write(rbio, ibuf, (int)rd);
	}
	cu_tls_virtual_update_eof(sock);
}

static sint32 cu_tls_ctx_init(uint32 sock)
{
	const char *ca_path;
	const char *cert_path;
	const char *key_path;
	uint32 ca_slot;
	uint32 pki_slot;

	if(sock >= ESP_MAX_LINKS)
		return -1;

	if(esp_state.tls_ctx[sock])
		return 0;

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	esp_state.tls_ctx[sock] = SSL_CTX_new(TLS_client_method());
	if(!esp_state.tls_ctx[sock])
		return -1;

	SSL_CTX_set_verify(esp_state.tls_ctx[sock], SSL_VERIFY_NONE, NULL);
	SSL_CTX_set_psk_client_callback(esp_state.tls_ctx[sock], cu_tls_psk_client_cb);

	ca_slot = (uint32)esp_state.ssl_ca_num[sock];
	pki_slot = (uint32)esp_state.ssl_pki_num[sock];
	ca_path = cu_tls_slot_path(esp_state.ssl_ca_path, ca_slot);
	cert_path = cu_tls_slot_path(esp_state.ssl_pki_cert_path, pki_slot);
	key_path = cu_tls_slot_path(esp_state.ssl_pki_key_path, pki_slot);

	if(ca_slot != 0u && ca_path == NULL){
		cu_tls_ctx_free(sock);
		return -11;
	}
	if(ca_path != NULL){
		if(SSL_CTX_load_verify_locations(esp_state.tls_ctx[sock], ca_path, NULL) != 1){
			cu_tls_ctx_free(sock);
			return -12;
		}
	}else{
		SSL_CTX_set_default_verify_paths(esp_state.tls_ctx[sock]);
	}

	if((cert_path != NULL) || (key_path != NULL) || (pki_slot != 0u)){
		if(cert_path == NULL || key_path == NULL){
			cu_tls_ctx_free(sock);
			return -13;
		}
		if(SSL_CTX_use_certificate_chain_file(esp_state.tls_ctx[sock], cert_path) != 1){
			cu_tls_ctx_free(sock);
			return -14;
		}
		if(SSL_CTX_use_PrivateKey_file(esp_state.tls_ctx[sock], key_path, SSL_FILETYPE_PEM) != 1){
			cu_tls_ctx_free(sock);
			return -15;
		}
		if(SSL_CTX_check_private_key(esp_state.tls_ctx[sock]) != 1){
			cu_tls_ctx_free(sock);
			return -16;
		}
	}

	return 0;
}

static const char *cu_tls_cipher_name(uint16 id)
{
	switch(id){case 0x002F:return "AES128-SHA";case 0x0035:return "AES256-SHA";case 0x003C:return "AES128-SHA256";case 0x003D:return "AES256-SHA256";case 0x009C:return "AES128-GCM-SHA256";case 0x009D:return "AES256-GCM-SHA384";case 0xC013:return "ECDHE-RSA-AES128-SHA";case 0xC014:return "ECDHE-RSA-AES256-SHA";case 0xC027:return "ECDHE-RSA-AES128-SHA256";case 0xC028:return "ECDHE-RSA-AES256-SHA384";case 0xC02F:return "ECDHE-RSA-AES128-GCM-SHA256";case 0xC030:return "ECDHE-RSA-AES256-GCM-SHA384";default:return NULL;}
}

static sint32 cu_tls_step(uint32 sock)
{
	sint32 trc = cu_tls_ctx_init(sock);
	if(trc != 0)
		return trc;

	if(!esp_state.tls_ssl[sock]){
		const char *sni = NULL;
		const char *host_name = NULL;

		esp_state.tls_ssl[sock] = SSL_new(esp_state.tls_ctx[sock]);
		if(!esp_state.tls_ssl[sock])
			return -2;

		SSL_set_connect_state(esp_state.tls_ssl[sock]);
		if(cu_tls_uses_virtual(sock)){
			BIO *rbio = BIO_new(BIO_s_mem());
			BIO *wbio = BIO_new(BIO_s_mem());
			if(rbio == NULL || wbio == NULL){
				if(rbio) BIO_free(rbio);
				if(wbio) BIO_free(wbio);
				cu_tls_ctx_free(sock);
				return -2;
			}
			BIO_set_mem_eof_return(rbio, -1);
			BIO_set_mem_eof_return(wbio, -1);
			SSL_set_bio(esp_state.tls_ssl[sock], rbio, wbio);
			cu_tls_virtual_update_eof(sock);
		}else{
			SSL_set_fd(esp_state.tls_ssl[sock], (int)esp_state.link_pending_sock[sock]);
		}
		SSL_set_app_data(esp_state.tls_ssl[sock], (void *)(size_t)(sock + 1u));

		if(esp_state.ssl_sni[sock][0])
			sni = (const char *)esp_state.ssl_sni[sock];
		else if(esp_state.link_host[sock][0])
			sni = (const char *)esp_state.link_host[sock];

		if(esp_state.ssl_common_name[sock][0])
			host_name = (const char *)esp_state.ssl_common_name[sock];
		else if(sni)
			host_name = sni;
		else if(esp_state.link_host[sock][0])
			host_name = (const char *)esp_state.link_host[sock];

		if(sni)
			SSL_set_tlsext_host_name(esp_state.tls_ssl[sock], sni);

		if(esp_state.ssl_psk_key[sock][0]){
			SSL_set_cipher_list(esp_state.tls_ssl[sock], "PSK");
#if defined(TLS1_2_VERSION)
			SSL_set_max_proto_version(esp_state.tls_ssl[sock], TLS1_2_VERSION);
#endif
		}else if(esp_state.ssl_cipher_count[sock]){
			char list[512];size_t pos=0;list[0]=0;for(uint8 ci=0;ci<esp_state.ssl_cipher_count[sock]&&ci<12u;ci++){const char*n=cu_tls_cipher_name(esp_state.ssl_cipher[sock][ci]);if(!n)continue;size_t l=strlen(n);if(pos&&pos+1<sizeof(list))list[pos++]=':';if(pos+l>=sizeof(list))break;memcpy(list+pos,n,l);pos+=l;list[pos]=0;}if(pos&&SSL_set_cipher_list(esp_state.tls_ssl[sock],list)!=1)return -18;
		}

		if(esp_state.ssl_auth_mode[sock] != 0u){
			SSL_set_verify(esp_state.tls_ssl[sock], SSL_VERIFY_PEER, NULL);
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
			if(host_name)
				SSL_set1_host(esp_state.tls_ssl[sock], host_name);
#endif
		}else{
			SSL_set_verify(esp_state.tls_ssl[sock], SSL_VERIFY_NONE, NULL);
		}

		if(esp_state.ssl_alpn_count[sock] != 0u){
			unsigned char alpn[5u*(1u+32u)];unsigned int pos=0u;const sint8 *ap[5]={esp_state.ssl_alpn0[sock],esp_state.ssl_alpn1[sock],esp_state.ssl_alpn2[sock],esp_state.ssl_alpn3[sock],esp_state.ssl_alpn4[sock]};
			for(uint8 ai=0;ai<esp_state.ssl_alpn_count[sock]&&ai<5u;ai++){if(!ap[ai][0])continue;size_t n=strlen((const char*)ap[ai]);if(n>32u)n=32u;if(pos+1u+n>sizeof(alpn))break;alpn[pos++]=(unsigned char)n;memcpy(&alpn[pos],ap[ai],n);pos+=(unsigned)n;}
			if(pos)SSL_set_alpn_protos(esp_state.tls_ssl[sock],alpn,pos);
		}
	}

	{
		int rc;
		if(cu_tls_uses_virtual(sock))
			cu_tls_virtual_feed_in(sock);
		rc = SSL_connect(esp_state.tls_ssl[sock]);
		if(cu_tls_uses_virtual(sock))
			cu_tls_virtual_flush_out(sock);
		if(rc == 1)
			return 0;

		{
			int e = SSL_get_error(esp_state.tls_ssl[sock], rc);
			if(e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE){
				if(cu_tls_uses_virtual(sock) && cu_esp_lan_virtual_link_present(&esp_state, sock) &&
				   esp_state.lan.vlinks[sock].closed_remote && esp_state.lan.vlinks[sock].rx_len == 0u)
					return -4;
				return 1;
			}
			if(cu_tls_uses_virtual(sock))
				cu_tls_virtual_flush_out(sock);
		}
		return -3;
	}
}
#endif

/* ------------------------------------------------------------------------- */
/* CIPSERVER (listen/accept)                                                  */
/* ------------------------------------------------------------------------- */

/* CIPSERVER runtime state now lives in esp_state. */

static void cu_srv_close(void)
{
	if(esp_state.listen_socket != ESP_INVALID_SOCKET && esp_state.listen_socket != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.listen_socket);
#else
		close(esp_state.listen_socket);
#endif
	}

	esp_state.listen_socket = ESP_INVALID_SOCKET;
	esp_state.listen_port = 0u;
	esp_state.server_enabled = 0u;
}

static void cu_srv_accept_tick(void)
{
	uint32 accept_budget = cu_esp_server_accept_budget();

	if(!esp_state.server_enabled)
		return;

	while(accept_budget-- != 0u){
		struct sockaddr_in peer;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		int plen = (int)sizeof(peer);
#else
		socklen_t plen = (socklen_t)sizeof(peer);
#endif

		ESP_SOCKET cs = accept(esp_state.listen_socket, (struct sockaddr *)&peer, &plen);
		if(cs == ESP_INVALID_SOCKET || cs == -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			int e = WSAGetLastError();
			if(e == WSAEWOULDBLOCK || e == WSAEINPROGRESS)
				break;
#else
			if(errno == EWOULDBLOCK || errno == EAGAIN)
				break;
#endif
			break;
		}

		/* enforce server_max_conn */
		{
			uint32 maxc = esp_state.server_max_conn;
			uint32 used = 0u;
			if(maxc == 0u || maxc > ESP_MAX_LINKS)
				maxc = ESP_MAX_LINKS;

			for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
				if(esp_state.link_is_server[i] &&
				   esp_state.socks[i] != ESP_INVALID_SOCKET &&
				   esp_state.link_state[i] == CU_ESP_LS_OPEN)
					used++;
			}

			if(used >= maxc){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
				closesocket(cs);
#else
				close(cs);
#endif
				continue;
			}
		}

		/* Find a free link slot */
		uint32 slot = ESP_MAX_LINKS;
		for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
			if(esp_state.socks[i] == ESP_INVALID_SOCKET && esp_state.link_state[i] == CU_ESP_LS_IDLE){
				slot = i;
				break;
			}
		}

		if(slot >= ESP_MAX_LINKS){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			closesocket(cs);
#else
			close(cs);
#endif
			continue;
		}

		cu_sock_set_nonblocking(cs);

		esp_state.socks[slot] = cs;
		esp_state.proto[slot] = ESP_PROTO_TCP;
		esp_state.protocol[slot] = ESP_PROTO_TCP;
		esp_state.sock_info[slot] = peer;
		esp_state.link_is_server[slot] = 1u;

		esp_state.link_state[slot] = CU_ESP_LS_OPEN;
		esp_state.link_notice_ok[slot] = 1u;
	}
}

/* ------------------------------------------------------------------------- */
/* Public network API                                                         */
/* ------------------------------------------------------------------------- */

sint32 cu_esp_init_sockets(void)
{
	if(!esp_state.emulation_model &&
	   esp_state.serial_route != CU_ESP_SERIAL_TCP_SERIAL &&
	   esp_state.serial_route != CU_ESP_SERIAL_LOOPBACK &&
	   !esp_state.tcp_serial_enabled)
		return 0;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(!esp_state.winsock_enabled){
		WSADATA wsadata;
		if(WSAStartup(MAKEWORD(2, 2), &wsadata)){
			print_error("ESP ERROR: Failed for Winsock 2.2 reverting to 1.1\n");
			if(WSAStartup(MAKEWORD(1, 1), &wsadata)){
				print_error("ESP ERROR: Failed on WSAStartup(): %d\n", (int)cu_esp_get_last_error());
				esp_state.winsock_enabled = 0u;
				return -1;
			}
		}
		esp_state.winsock_enabled = 1u;
	}
#endif

#ifndef __EMSCRIPTEN__
	cu_async_start_once();
#endif

	esp_state.ping_sock[0] = -1;
	esp_state.ping_sock[1] = -1;
	esp_state.ping_sock[2] = -1;

	cu_ping_shutdown();

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	esp_state.ping_icmp_h = IcmpCreateFile();
	if(esp_state.ping_icmp_h && esp_state.ping_icmp_h != INVALID_HANDLE_VALUE){
		esp_state.ping_mode = CU_PING_ICMP_WIN;
		esp_state.ping_sock[0] = (ESP_SOCKET)1; /* sentinel */
	}else{
		esp_state.ping_icmp_h = NULL;
		esp_state.ping_mode = CU_PING_TCP_ONLY;
		esp_state.ping_sock[2] = (ESP_SOCKET)1; /* sentinel */
	}
#else
	esp_state.ping_icmp_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if(esp_state.ping_icmp_sock != ESP_INVALID_SOCKET && esp_state.ping_icmp_sock != -1){
		esp_state.ping_mode = CU_PING_ICMP_RAW;
		esp_state.ping_sock[0] = esp_state.ping_icmp_sock;
	}else{
		esp_state.ping_icmp_sock = ESP_INVALID_SOCKET;
		esp_state.ping_mode = CU_PING_TCP_ONLY;
		esp_state.ping_sock[2] = (ESP_SOCKET)1; /* sentinel */
	}
#endif

	/* UDP "ping" socket (used by ping_udp helper) */
	esp_state.ping_sock[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(esp_state.ping_sock[1] != ESP_INVALID_SOCKET && esp_state.ping_sock[1] != -1)
		cu_sock_set_nonblocking(esp_state.ping_sock[1]);

	return 0;
}

static void cu_esp_close_aux_files(void)
{
	if(esp_state.uart_logging_file != NULL){
		fclose(esp_state.uart_logging_file);
		esp_state.uart_logging_file = NULL;
	}
	if(esp_state.uart_playback_file != NULL){
		fclose(esp_state.uart_playback_file);
		esp_state.uart_playback_file = NULL;
	}
	esp_state.uart_logging_started = 0u;
	esp_state.uart_playback_started = 0u;
}

void cu_esp_runtime_shutdown(void)
{
	cu_esp_close_aux_files();
	cu_esp_host_serial_end();
	cu_esp_tcp_serial_end();
	cu_esp_serial_midi_end();
	cu_esp_net_cleanup();
}

void cu_esp_net_cleanup(void)
{
	/* close all regular sockets */
	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++)
		cu_esp_close_socket(i);

	cu_srv_close();

	/* close ping sockets */
	if(esp_state.ping_sock[1] != ESP_INVALID_SOCKET && esp_state.ping_sock[1] != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.ping_sock[1]);
#else
		close(esp_state.ping_sock[1]);
#endif
		esp_state.ping_sock[1] = -1;
	}

	cu_ping_shutdown();

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++)
		cu_tls_ctx_free(i);
#endif

#ifndef __EMSCRIPTEN__
	cu_async_stop();
#endif

	cu_esp_lan_shutdown(&esp_state);

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(esp_state.winsock_enabled){
		WSACleanup();
		esp_state.winsock_enabled = 0u;
	}
#endif
}

void cu_esp_reset_network(void)
{
	if(!esp_state.emulation_model)
		return;

	cu_esp_init_sockets();

	cu_srv_close();

	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
		esp_state.socks[i] = ESP_INVALID_SOCKET;
		esp_state.proto[i] = 0u;
		esp_state.protocol[i] = 0u;
		esp_state.link_is_server[i] = 0u;
		memset(&esp_state.sock_info[i], 0, sizeof(esp_state.sock_info[i]));

		esp_state.link_state[i] = CU_ESP_LS_IDLE;
		esp_state.link_err[i] = 0;
		esp_state.link_notice_ok[i] = 0u;
		esp_state.link_notice_err[i] = 0u;
		esp_state.link_ready_ms[i] = 0u;
		esp_state.link_pending_sock[i] = ESP_INVALID_SOCKET;
		esp_state.link_type[i] = 0u;
		esp_state.link_port[i] = 0u;
		esp_state.udp_local_port[i] = 0u;
		esp_state.udp_mode[i] = 0u;
		esp_state.udp_mode1_latched[i] = 0u;
		esp_state.udp_send_override[i] = 0u;
		memset(&esp_state.udp_send_info[i], 0, sizeof(esp_state.udp_send_info[i]));
		memset(esp_state.link_host[i], 0, sizeof(esp_state.link_host[i]));
	}

	esp_state.ping_pending = 0u;
	esp_state.ping_ready = 0u;
	esp_state.ping_rtt = -1;
	esp_state.ping_err = 0;
	esp_state.ping_ready_ms = 0u;

	esp_state.cwlap_sort_rssi = 1u;
	esp_state.cwlap_print_mask = 0x07FFu;
	esp_state.cwlap_rssi_filter = -100;
	esp_state.cwlap_authmask = 0xFFFFu;

	esp_state.dns_pending = 0u;
	esp_state.dns_ready = 0u;
	esp_state.dns_err = 0;
	esp_state.dns_ready_ms = 0u;
	esp_state.dns_ipv4_be = 0u;
	memset(esp_state.dns_host, 0, sizeof(esp_state.dns_host));

	esp_state.udp_send_resolve_pending = 0u;
	esp_state.udp_send_resolve_ready = 0u;
	esp_state.udp_send_resolve_sock = 0u;
	esp_state.send_prep_pending = 0u;
	esp_state.udp_send_resolve_err = 0;
	esp_state.udp_send_resolve_ready_ms = 0u;
	esp_state.send_prep_len = 0u;
	memset(&esp_state.udp_send_resolve_info, 0, sizeof(esp_state.udp_send_resolve_info));

	srand((unsigned int)cu_esp_get_time_seconds());
	esp_state.ip_delay_timer = ESP_AT_IP_DELAY + ((rand() % 500) * ESP_AT_MS_DELAY);

	cu_esp_lan_init(&esp_state);
	esp_state.translink_boot_pending=esp_state.translink_enabled?1u:0u;esp_state.translink_retry_ms=0u;
}

auint cu_esp_net_last_error(void)
{
	return (auint)cu_esp_get_last_error();
}

#include "cu_esp_mqtt.inc"

static auint cu_esp_runtime_needs_net_tick(void)
{
	uint32 i;
	if ((esp_state.serial_route != CU_ESP_SERIAL_DISCONNECTED) ||
	    (esp_state.emulation_model != 0u) ||
	    esp_state.lan.enable ||
	    esp_state.server_enabled ||
	    esp_state.tcp_serial_enabled ||
	    esp_state.wifi_join_pending ||
	    esp_state.dns_pending ||
	    esp_state.ping_pending ||
	    esp_state.send_prep_pending ||
	    esp_state.udp_send_resolve_pending){
		return 1u;
	}
	for (i = 0u; i < ESP_MAX_LINKS; ++i){
		if ((esp_state.socks[i] != ESP_INVALID_SOCKET) ||
		    (esp_state.link_pending_sock[i] != ESP_INVALID_SOCKET) ||
		    (esp_state.link_state[i] != CU_ESP_LS_IDLE) ||
		    esp_state.link_wait_ok[i]){
			return 1u;
		}
	}
	return 0u;
}

void cu_esp_net_tick(void)
{
	if(cu_mqtt_runtime_active())
		cu_esp_mqtt_tick();
	if (!cu_esp_runtime_needs_net_tick()) return;
#ifndef __EMSCRIPTEN__
	cu_esp_async_drain_events();
#endif

	{
		uint32 now = cu_esp_now_ms();

		/* SAVETRANSLINK: after station association, reopen link 0 and enter
		 * transparent mode.  USB/host scheduling never participates in socket timing. */
		if(esp_state.translink_enabled && esp_state.translink_boot_pending){
			if(esp_state.translink_boot_pending==2u && esp_state.link_state[0]==CU_ESP_LS_OPEN){esp_state.cip_mode=1u;esp_state.user_input_mode=ESP_USER_MODE_UNVARNISHED;esp_state.unvarnished_bytes=0u;esp_state.unvarnished_end_cycle=0u;esp_state.translink_boot_pending=0u;}
			else if(esp_state.translink_boot_pending==2u && esp_state.link_state[0]==CU_ESP_LS_ERROR){cu_esp_close_socket(0);esp_state.translink_boot_pending=1u;esp_state.translink_retry_ms=now+1000u;}
			else if(esp_state.translink_boot_pending==1u && (esp_state.state&ESP_AP_CONNECTED) && now>=esp_state.translink_retry_ms && esp_state.link_state[0]==CU_ESP_LS_IDLE){uint32 t=ESP_PROTO_TCP;if(esp_state.translink_kind==CU_ESP_TRANSLINK_UDP)t=ESP_PROTO_UDP;else if(esp_state.translink_kind==CU_ESP_TRANSLINK_SSL)t=ESP_PROTO_SSL|ESP_PROTO_TCP;else if(esp_state.translink_kind==CU_ESP_TRANSLINK_TCP6)t=ESP_PROTO_TCP|ESP_PROTO_IPV6;else if(esp_state.translink_kind==CU_ESP_TRANSLINK_UDP6)t=ESP_PROTO_UDP|ESP_PROTO_IPV6;else if(esp_state.translink_kind==CU_ESP_TRANSLINK_SSL6)t=ESP_PROTO_SSL|ESP_PROTO_TCP|ESP_PROTO_IPV6;esp_state.tcp_keepalive[0]=esp_state.translink_keepalive;if(cu_esp_net_connect_ex((sint8*)esp_state.translink_host,0,esp_state.translink_port,t,esp_state.translink_local_port,esp_state.translink_udp_mode)==0)esp_state.translink_boot_pending=2u;else esp_state.translink_retry_ms=now+1000u;}
		}

		/* virtual LAN overlay service */
		if(esp_state.lan.enable)
			cu_esp_lan_tick(&esp_state, now);

		/* CIPSERVER accept */
		cu_srv_accept_tick();

		/* apply connection delay / TLS / open transitions */
		for(uint32 s = 0u; s < ESP_MAX_LINKS; s++){
			if(esp_state.link_state[s] == CU_ESP_LS_DELAY){
				if(now < esp_state.link_ready_ms[s])
					continue;

				if(esp_state.link_pending_sock[s] == ESP_INVALID_SOCKET){
					esp_state.link_state[s] = CU_ESP_LS_ERROR;
					esp_state.link_err[s] = -11;
					esp_state.link_notice_err[s] = 1u;
					continue;
				}

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
				if(esp_state.link_type[s] & ESP_PROTO_SSL){
					esp_state.link_state[s] = CU_ESP_LS_TLS;
					continue;
				}
#endif

				esp_state.socks[s] = esp_state.link_pending_sock[s];
				esp_state.proto[s] = esp_state.link_type[s];
				esp_state.protocol[s] = esp_state.link_type[s];
				if(esp_state.link_type[s] & ESP_PROTO_UDP)
					esp_state.udp_local_port[s] = cu_udp_local_port_of(esp_state.socks[s]);

				esp_state.link_pending_sock[s] = ESP_INVALID_SOCKET;
				cu_esp_net_reapply_tcp_options(s);
				esp_state.link_state[s] = CU_ESP_LS_OPEN;
				esp_state.link_notice_ok[s] = 1u;
			}

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
			if(esp_state.link_state[s] == CU_ESP_LS_TLS){
				sint32 tr = cu_tls_step(s);
				if(tr == 0){
					esp_state.socks[s] = esp_state.link_pending_sock[s];
					esp_state.proto[s] = esp_state.link_type[s];
					esp_state.protocol[s] = esp_state.link_type[s];
					if(esp_state.link_type[s] & ESP_PROTO_UDP)
						esp_state.udp_local_port[s] = cu_udp_local_port_of(esp_state.socks[s]);

					esp_state.link_pending_sock[s] = ESP_INVALID_SOCKET;
					cu_esp_net_reapply_tcp_options(s);
					esp_state.link_state[s] = CU_ESP_LS_OPEN;
					esp_state.link_notice_ok[s] = 1u;
				}else if(tr < 0){
					esp_state.link_state[s] = CU_ESP_LS_ERROR;
					esp_state.link_err[s] = tr;
					esp_state.link_notice_err[s] = 1u;
				}
			}
#endif
		}
	}
}

uint8 cu_esp_ssl_link_busy(uint32 except_sock)
{
	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
		if(i == except_sock)
			continue;

		if((esp_state.link_type[i] & ESP_PROTO_SSL) != 0u)
			return 1u;
		if((esp_state.proto[i] & ESP_PROTO_SSL) != 0u)
			return 1u;
		if((esp_state.protocol[i] & ESP_PROTO_SSL) != 0u)
			return 1u;
		if(esp_state.link_is_ssl[i] != 0u)
			return 1u;
#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
		if(esp_state.tls_ssl[i] != NULL)
			return 1u;
#endif
		if(esp_state.link_pending_sock[i] != ESP_INVALID_SOCKET && esp_state.link_pending_sock[i] != -1){
			if(esp_state.link_state[i] == CU_ESP_LS_DNS || esp_state.link_state[i] == CU_ESP_LS_DELAY || esp_state.link_state[i] == CU_ESP_LS_TLS)
				if((esp_state.link_type[i] & ESP_PROTO_SSL) != 0u || esp_state.link_is_ssl[i] != 0u)
					return 1u;
		}
	}

	return 0u;
}

sint32 cu_esp_net_connect_ex(sint8 *hostname, uint32 sock, uint32 port, uint32 type, uint32 local_port, uint32 udp_mode)
{
	if(sock >= ESP_MAX_LINKS)
		return -1;

	if(((type & ESP_PROTO_UDP) == 0u) && (hostname == NULL || hostname[0] == '\0'))
		return -1;

	if((type & ESP_PROTO_UDP) == 0u && port == 0u)
		return -1;

	if((type & ESP_PROTO_SSL) != 0u && cu_esp_ssl_link_busy(sock))
		return -1;

	cu_esp_close_socket(sock);

	if(esp_state.lan.joined && esp_state.lan.joined_ap_addr.sin_family == AF_INET &&
	   ((type & ESP_PROTO_TCP) != 0u || (type & ESP_PROTO_SSL) != 0u || (type & ESP_PROTO_UDP) != 0u) &&
	   hostname != NULL && hostname[0] != '\0')
		return (cu_esp_lan_virtual_open(&esp_state, sock, (const char *)hostname, port, type, local_port, udp_mode) == 0) ? 0 : -1;

	esp_state.link_state[sock] = CU_ESP_LS_DNS;
	esp_state.link_err[sock] = 0;
	esp_state.link_notice_ok[sock] = 0u;
	esp_state.link_notice_err[sock] = 0u;
	esp_state.link_ready_ms[sock] = 0u;
	esp_state.link_pending_sock[sock] = ESP_INVALID_SOCKET;
	esp_state.link_type[sock] = type;
	esp_state.link_port[sock] = port;
	esp_state.link_is_server[sock] = ((type & ESP_PROTO_UDP) != 0u && local_port != 0u && (udp_mode != 0u || hostname == NULL || hostname[0] == '\0' || port == 0u)) ? 1u : 0u;
	esp_state.udp_local_port[sock] = (uint16)local_port;
	esp_state.udp_mode[sock] = (uint8)udp_mode;
	esp_state.udp_mode1_latched[sock] = 0u;
	esp_state.udp_send_override[sock] = 0u;
	memset(&esp_state.udp_send_info[sock], 0, sizeof(esp_state.udp_send_info[sock]));
	esp_state.udp_send_ipv6_valid[sock]=0u;esp_state.udp_send_ipv6_port[sock]=0u;memset(esp_state.udp_send_ipv6[sock],0,16);

	memset(esp_state.link_host[sock], 0, sizeof(esp_state.link_host[sock]));
	if(hostname)
		strncpy(esp_state.link_host[sock], (const char *)hostname, sizeof(esp_state.link_host[sock]) - 1u);

#ifndef __EMSCRIPTEN__
	{
		cu_async_job_t j;
		memset(&j, 0, sizeof(j));
		j.kind = CU_JOB_CONNECT;
		j.sock = (uint8)sock;
		j.port = port;
		j.local_port = (uint16)local_port;
		j.type = type;
		j.udp_mode = (uint8)udp_mode;
		j.timeout_ms = 5000;
		j.timeout_ms32 = esp_state.link_timeout_ms[sock] ? esp_state.link_timeout_ms[sock] : 5000u;
		j.ip_network = (type & ESP_PROTO_IPV6) ? 3u : 2u;
		strncpy(j.local_ip, (const char *)esp_state.link_local_ip[sock], sizeof(j.local_ip) - 1u);
		if(hostname)
			strncpy(j.host, (const char *)hostname, sizeof(j.host) - 1u);

		if(!cu_push_job(&j)){
			esp_state.link_state[sock] = CU_ESP_LS_ERROR;
			esp_state.link_err[sock] = -10;
			esp_state.link_notice_err[sock] = 1u;
			return -1;
		}
		return 0;
	}
#else
	return -1;
#endif
}

sint32 cu_esp_net_connect(sint8 *hostname, uint32 sock, uint32 port, uint32 type)
{
	return cu_esp_net_connect_ex(hostname, sock, port, type, 0u, 0u);
}

/* optional helper used by AT+CIPSERVER handling elsewhere */
sint32 cu_esp_listen(uint32 port)
{
	if(port == 0u){
		cu_srv_close();
		return 0;
	}

	cu_srv_close();

	ESP_SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s == ESP_INVALID_SOCKET || s == -1)
		return -1;

	{
		int one = 1;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, (int)sizeof(one));
#else
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one));
#endif
	}

	{
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = htonl(INADDR_ANY);
		sa.sin_port = htons((uint16)port);

		if(bind(s, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) != 0){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			closesocket(s);
#else
			close(s);
#endif
			return -1;
		}
	}

	if(listen(s, (int)((esp_state.server_max_conn == 0u || esp_state.server_max_conn > ESP_MAX_LINKS) ? ESP_MAX_LINKS : esp_state.server_max_conn)) != 0){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(s);
#else
		close(s);
#endif
		return -1;
	}

	cu_sock_set_nonblocking(s);
	esp_state.listen_socket = s;
	esp_state.listen_port = port;
	esp_state.server_enabled = 1u;

	return 0;
}

sint32 cu_esp_create_socket(uint32 s, uint32 proto, uint32 port)
{
	(void)port;

	if(s >= ESP_MAX_LINKS)
		return -1;

	{ int family=(proto & ESP_PROTO_IPV6)?AF_INET6:AF_INET;
	if((proto & ESP_PROTO_TCP) || (proto & ESP_PROTO_SSL))
		esp_state.socks[s] = socket(family, SOCK_STREAM, IPPROTO_TCP);
	else
		esp_state.socks[s] = socket(family, SOCK_DGRAM, IPPROTO_UDP); }

	if(esp_state.socks[s] != ESP_INVALID_SOCKET && esp_state.socks[s] != -1)
		cu_sock_set_nonblocking(esp_state.socks[s]);

	esp_state.proto[s] = proto;
	esp_state.protocol[s] = proto;

	return (esp_state.socks[s] == ESP_INVALID_SOCKET || esp_state.socks[s] == -1) ? -1 : 0;
}

void cu_esp_close_socket(uint32 sock)
{
	if(sock == ESP_ALL_CONNECTIONS){
		for(uint32 i = 0u; i < ESP_MAX_LINKS; i++)
			cu_esp_close_socket(i);
		return;
	}

	if(sock >= ESP_MAX_LINKS)
		return;

	if(cu_esp_lan_virtual_link_present(&esp_state, sock)){
#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
		if(esp_state.tls_ssl[sock] != NULL && cu_tls_uses_virtual(sock) && !esp_state.lan.vlinks[sock].closed_remote){
			(void)SSL_shutdown(esp_state.tls_ssl[sock]);
			cu_tls_virtual_flush_out(sock);
		}
#endif
		cu_esp_lan_virtual_close(&esp_state, sock, (uint8)(esp_state.lan.vlinks[sock].closed_remote ? 0u : 1u));
	}

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	cu_tls_ctx_free(sock);
#endif

	if(esp_state.socks[sock] != ESP_INVALID_SOCKET && esp_state.socks[sock] != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.socks[sock]);
#else
		close(esp_state.socks[sock]);
#endif
	}

	esp_state.socks[sock] = ESP_INVALID_SOCKET;
	esp_state.proto[sock] = 0u;
	esp_state.protocol[sock] = 0u;
	esp_state.link_is_server[sock] = 0u;
	esp_state.link_is_ssl[sock] = 0u;
	memset(&esp_state.sock_info[sock], 0, sizeof(esp_state.sock_info[sock]));
	esp_state.link_peer_ipv6_valid[sock]=0u; esp_state.link_peer_ipv6_port[sock]=0u; memset(esp_state.link_peer_ipv6[sock],0,16);

	if(esp_state.link_pending_sock[sock] != ESP_INVALID_SOCKET && esp_state.link_pending_sock[sock] != -1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(esp_state.link_pending_sock[sock]);
#else
		close(esp_state.link_pending_sock[sock]);
#endif
		esp_state.link_pending_sock[sock] = ESP_INVALID_SOCKET;
	}

	esp_state.link_state[sock] = CU_ESP_LS_IDLE;
	esp_state.link_err[sock] = 0;
	esp_state.link_notice_ok[sock] = 0u;
	esp_state.link_notice_err[sock] = 0u;
	esp_state.link_ready_ms[sock] = 0u;
	esp_state.link_type[sock] = 0u;
	esp_state.link_port[sock] = 0u;	esp_state.udp_local_port[sock] = 0u;
	esp_state.udp_mode[sock] = 0u;
	esp_state.udp_mode1_latched[sock] = 0u;
	esp_state.udp_send_override[sock] = 0u;
	memset(&esp_state.udp_send_info[sock], 0, sizeof(esp_state.udp_send_info[sock]));
	esp_state.udp_send_ipv6_valid[sock]=0u;esp_state.udp_send_ipv6_port[sock]=0u;memset(esp_state.udp_send_ipv6[sock],0,16);
	memset(esp_state.link_host[sock], 0, sizeof(esp_state.link_host[sock]));
}

sint32 cu_esp_ping_async_start(const char *host, sint16 timeout_ms)
{
	if(!host || !*host)
		return -1;

	if(esp_state.ping_pending)
		return -1;

	esp_state.ping_pending = 1u;
	esp_state.ping_ready = 0u;
	esp_state.ping_rtt = -1;
	esp_state.ping_err = 0;
	esp_state.ping_ready_ms = 0u;

#ifndef __EMSCRIPTEN__
	{
		cu_async_job_t j;
		memset(&j, 0, sizeof(j));
		j.kind = CU_JOB_PING;
		j.timeout_ms = timeout_ms;
		strncpy(j.host, host, sizeof(j.host) - 1u);

		if(!cu_push_job(&j)){
			esp_state.ping_pending = 0u;
			return -1;
		}
		return 0;
	}
#else
	esp_state.ping_pending = 0u;
	return -1;
#endif
}

sint32 cu_esp_ping_async_poll(sint16 *rtt_ms)
{
	if(!esp_state.ping_ready)
		return 0;

	if(esp_state.ping_ready_ms && cu_esp_now_ms() < esp_state.ping_ready_ms)
		return 0;

	esp_state.ping_ready = 0u;
	esp_state.ping_ready_ms = 0u;

	if(esp_state.ping_err != 0)
		return -1;

	if(rtt_ms)
		*rtt_ms = esp_state.ping_rtt;

	return 1;
}

sint32 cu_esp_dns_async_start(const char *host)
{
	return cu_esp_dns_async_start_ex(host, 1u);
}

sint32 cu_esp_dns_async_start_ex(const char *host, uint8 ip_network)
{
	if(!host || !*host || esp_state.dns_pending || ip_network > 3u) return -1;
	esp_state.dns_pending=1u;esp_state.dns_ready=0u;esp_state.dns_err=0;esp_state.dns_ready_ms=0u;esp_state.dns_ipv4_be=0u;esp_state.dns_result_text[0]=0;
	memset(esp_state.dns_host,0,sizeof(esp_state.dns_host));strncpy((char*)esp_state.dns_host,host,sizeof(esp_state.dns_host)-1u);
#ifndef __EMSCRIPTEN__
	{cu_async_job_t j;memset(&j,0,sizeof(j));j.kind=CU_JOB_DNS;j.ip_network=ip_network;strncpy(j.host,host,sizeof(j.host)-1u);if(!cu_push_job(&j)){esp_state.dns_pending=0u;esp_state.dns_err=-1;return -1;}return 0;}
#else
	esp_state.dns_pending=0u;return -1;
#endif
}

sint32 cu_esp_dns_async_poll(uint32 *ipv4_be)
{
	if(!esp_state.dns_ready)
		return 0;

	if(esp_state.dns_ready_ms && cu_esp_now_ms() < esp_state.dns_ready_ms)
		return 0;

	esp_state.dns_ready = 0u;
	esp_state.dns_ready_ms = 0u;

	if(esp_state.dns_err != 0)
		return -1;

	if(ipv4_be)
		*ipv4_be = esp_state.dns_ipv4_be;

	return 1;
}

sint32 cu_esp_dns_async_poll_text(char *out, auint out_sz)
{
	if(!esp_state.dns_ready) return 0;
	if(esp_state.dns_ready_ms && cu_esp_now_ms() < esp_state.dns_ready_ms) return 0;
	esp_state.dns_ready=0u;esp_state.dns_ready_ms=0u;
	if(esp_state.dns_err!=0) return -1;
	if(out && out_sz){snprintf(out,(size_t)out_sz,"%s",esp_state.dns_result_text[0]?esp_state.dns_result_text:"0.0.0.0");}
	return 1;
}


sint32 cu_esp_udp_send_resolve_async_start(uint32 sock, const char *host, uint16 port)
{
	if(sock >= ESP_MAX_LINKS || !host || !*host || port == 0u)
		return -1;

	if(esp_state.udp_send_resolve_pending || esp_state.udp_send_resolve_ready || esp_state.send_prep_pending)
		return -1;

	if(!(esp_state.protocol[sock] & ESP_PROTO_UDP))
		return -1;

	esp_state.udp_send_resolve_pending = 1u;
	esp_state.udp_send_resolve_ready = 0u;
	esp_state.udp_send_resolve_sock = (uint8)sock;
	esp_state.udp_send_resolve_err = 0;
	esp_state.udp_send_resolve_ready_ms = 0u;
	memset(&esp_state.udp_send_resolve_info, 0, sizeof(esp_state.udp_send_resolve_info));

#ifndef __EMSCRIPTEN__
	{
		cu_async_job_t j;
		memset(&j, 0, sizeof(j));
		j.kind = CU_JOB_UDP_SEND_RESOLVE;
		j.sock = (uint8)sock;
		j.port = (uint32)port;
		j.ip_network = (esp_state.protocol[sock] & ESP_PROTO_IPV6) ? 3u : 2u;
		strncpy(j.host, host, sizeof(j.host) - 1u);

		if(!cu_push_job(&j)){
			esp_state.udp_send_resolve_pending = 0u;
			esp_state.udp_send_resolve_err = -1;
			return -1;
		}
		return 0;
	}
#else
	esp_state.udp_send_resolve_pending = 0u;
	esp_state.udp_send_resolve_err = -1;
	return -1;
#endif
}

sint32 cu_esp_get_last_error(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	return WSAGetLastError();
#else
	return errno;
#endif
}

sint32 cu_esp_net_send(uint32 sock, sint8 *buf, sint32 len, sint32 flags)
{
	if(sock >= ESP_MAX_LINKS)
		return -1;

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	if(esp_state.tls_ssl[sock]){
		int rc;
		if(cu_tls_uses_virtual(sock))
			cu_tls_virtual_feed_in(sock);
		rc = SSL_write(esp_state.tls_ssl[sock], (const void *)buf, (int)len);
		if(cu_tls_uses_virtual(sock))
			cu_tls_virtual_flush_out(sock);
		if(rc <= 0){
			int se = SSL_get_error(esp_state.tls_ssl[sock], rc);
			if(se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE)
				return -1;
			if(cu_tls_uses_virtual(sock)){
				esp_state.link_state[sock] = CU_ESP_LS_ERROR;
				esp_state.link_err[sock] = -41;
				esp_state.link_notice_err[sock] = 1u;
			}
			return -1;
		}
		return (sint32)rc;
	}
#endif

	if(cu_esp_lan_virtual_link_open(&esp_state, sock))
		return cu_esp_lan_virtual_send(&esp_state, sock, (const uint8 *)buf, (uint16)len);

	if(esp_state.socks[sock] == ESP_INVALID_SOCKET || esp_state.socks[sock] == -1)
		return -1;

	if(esp_state.protocol[sock] & ESP_PROTO_UDP){
		if(esp_state.protocol[sock] & ESP_PROTO_IPV6){
			struct sockaddr_in6 d6; memset(&d6,0,sizeof(d6)); d6.sin6_family=AF_INET6;
			if(esp_state.udp_send_override[sock] && esp_state.udp_send_ipv6_valid[sock]){memcpy(&d6.sin6_addr,esp_state.udp_send_ipv6[sock],16);d6.sin6_port=htons(esp_state.udp_send_ipv6_port[sock]);}
			else if(esp_state.link_peer_ipv6_valid[sock]){memcpy(&d6.sin6_addr,esp_state.link_peer_ipv6[sock],16);d6.sin6_port=htons(esp_state.link_peer_ipv6_port[sock]);}
			else return (sint32)send(esp_state.socks[sock],(const char*)buf,(int)len,flags);
			return (sint32)sendto(esp_state.socks[sock],(const char*)buf,(int)len,flags,(const struct sockaddr*)&d6,(socklen_t)sizeof(d6));
		}else{
			const struct sockaddr_in *dst=NULL;if(esp_state.udp_send_override[sock])dst=&esp_state.udp_send_info[sock];else if(esp_state.sock_info[sock].sin_family==AF_INET&&esp_state.sock_info[sock].sin_port!=0)dst=&esp_state.sock_info[sock];
			if(dst)return (sint32)sendto(esp_state.socks[sock],(const char*)buf,(int)len,flags,(const struct sockaddr*)dst,(socklen_t)sizeof(*dst));
			return (sint32)send(esp_state.socks[sock],(const char*)buf,(int)len,flags);
		}
	}

	return (sint32)send(esp_state.socks[sock], (const char *)buf, (int)len, flags);
}

uint32 cu_esp_net_rx_ready_mask(void)
{
	uint32 ready = 0u;
	uint32 socket_mask = 0u;
#ifndef __EMSCRIPTEN__
	fd_set rfds;
	struct timeval tv;
	int have_socket = 0;
#if !defined(WIN32) && !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MINGW32__)
	int maxfd = -1;
#endif

	FD_ZERO(&rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
#endif

	for(uint32 sock = 0u; sock < ESP_MAX_LINKS; ++sock){
#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
		/* SSL may already have decrypted bytes buffered even if the underlying
		 * socket (or virtual transport) is not currently readable. */
		if(esp_state.tls_ssl[sock] && SSL_pending(esp_state.tls_ssl[sock]) > 0){
			ready |= (1u << sock);
			continue;
		}
#endif

		/* The virtual-LAN path is an in-memory ring, so checking it is cheap and
		 * must also surface a remote close even when no payload remains. */
		if(cu_esp_lan_virtual_link_present(&esp_state, sock)){
			if(esp_state.lan.vlinks[sock].rx_len != 0u || esp_state.lan.vlinks[sock].closed_remote)
				ready |= (1u << sock);
			continue;
		}

		if(esp_state.socks[sock] == ESP_INVALID_SOCKET || esp_state.socks[sock] == -1)
			continue;

#ifdef __EMSCRIPTEN__
		/* Preserve the previous nonblocking-recv behavior on the browser build;
		 * desktop builds use one readiness syscall for all links below. */
		ready |= (1u << sock);
#else
		socket_mask |= (1u << sock);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		FD_SET((SOCKET)esp_state.socks[sock], &rfds);
#else
		FD_SET((int)esp_state.socks[sock], &rfds);
		if((int)esp_state.socks[sock] > maxfd)
			maxfd = (int)esp_state.socks[sock];
#endif
		have_socket = 1;
#endif
	}

#ifndef __EMSCRIPTEN__
	if(have_socket){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		int rc = select(0, &rfds, NULL, NULL, &tv);
#else
		int rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
#endif
		if(rc < 0){
			/* Let the normal recv/error path diagnose unusual select failures. */
			ready |= socket_mask;
		}else if(rc > 0){
			for(uint32 sock = 0u; sock < ESP_MAX_LINKS; ++sock){
				if((socket_mask & (1u << sock)) == 0u)
					continue;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
				if(FD_ISSET((SOCKET)esp_state.socks[sock], &rfds))
#else
				if(FD_ISSET((int)esp_state.socks[sock], &rfds))
#endif
					ready |= (1u << sock);
			}
		}
	}
#endif

	return ready;
}

sint32 cu_esp_net_recv(uint32 sock, sint8 *buf, sint32 len, sint32 flags)
{
	(void)flags;

	if(sock >= ESP_MAX_LINKS)
		return -1;

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	if(esp_state.tls_ssl[sock]){
		int rc;
		if(cu_tls_uses_virtual(sock))
			cu_tls_virtual_feed_in(sock);
		rc = SSL_read(esp_state.tls_ssl[sock], (void *)buf, (int)len);
		if(cu_tls_uses_virtual(sock))
			cu_tls_virtual_flush_out(sock);
		if(rc <= 0){
			int se = SSL_get_error(esp_state.tls_ssl[sock], rc);
			if(se == SSL_ERROR_ZERO_RETURN)
				return 0;
			if(se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE){
				if(cu_tls_uses_virtual(sock) && cu_esp_lan_virtual_link_present(&esp_state, sock) &&
				   esp_state.lan.vlinks[sock].closed_remote && esp_state.lan.vlinks[sock].rx_len == 0u &&
				   SSL_pending(esp_state.tls_ssl[sock]) == 0)
					return 0;
				return -1;
			}
			if(cu_tls_uses_virtual(sock) && se == SSL_ERROR_SYSCALL &&
			   cu_esp_lan_virtual_link_present(&esp_state, sock) &&
			   esp_state.lan.vlinks[sock].closed_remote && esp_state.lan.vlinks[sock].rx_len == 0u &&
			   SSL_pending(esp_state.tls_ssl[sock]) == 0)
				return 0;
			if(cu_tls_uses_virtual(sock)){
				esp_state.link_state[sock] = CU_ESP_LS_ERROR;
				esp_state.link_err[sock] = -42;
				esp_state.link_notice_err[sock] = 1u;
			}
		}
		return (sint32)rc;
	}
#endif

	if(cu_esp_lan_virtual_link_present(&esp_state, sock))
		return cu_esp_lan_virtual_recv(&esp_state, sock, (uint8 *)buf, (uint16)len);

	if(esp_state.socks[sock] == ESP_INVALID_SOCKET || esp_state.socks[sock] == -1)
		return -1;

	if(esp_state.protocol[sock] & ESP_PROTO_UDP){
		struct sockaddr_storage peer;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		int plen=(int)sizeof(peer);
#else
		socklen_t plen=(socklen_t)sizeof(peer);
#endif
		int rc;memset(&peer,0,sizeof(peer));rc=(int)recvfrom(esp_state.socks[sock],(char*)buf,(int)len,0,(struct sockaddr*)&peer,&plen);
		if(rc>0 && peer.ss_family==AF_INET6){struct sockaddr_in6*p6=(struct sockaddr_in6*)&peer;uint8 update=(esp_state.udp_mode[sock]==2u)||(!esp_state.link_peer_ipv6_valid[sock])||(esp_state.udp_mode[sock]==1u&&!esp_state.udp_mode1_latched[sock]);if(update){esp_state.link_peer_ipv6_valid[sock]=1u;memcpy(esp_state.link_peer_ipv6[sock],&p6->sin6_addr,16);esp_state.link_peer_ipv6_port[sock]=(uint16)ntohs(p6->sin6_port);if(esp_state.udp_mode[sock]==1u)esp_state.udp_mode1_latched[sock]=1u;}}
		else if(rc>0 && peer.ss_family==AF_INET){struct sockaddr_in*p4=(struct sockaddr_in*)&peer;uint8 update=(esp_state.udp_mode[sock]==2u)||(esp_state.sock_info[sock].sin_family!=AF_INET)||(esp_state.udp_mode[sock]==1u&&!esp_state.udp_mode1_latched[sock]);if(update){esp_state.link_peer_ipv6_valid[sock]=0u;esp_state.sock_info[sock]=*p4;if(esp_state.udp_mode[sock]==1u)esp_state.udp_mode1_latched[sock]=1u;}}
		return (sint32)rc;
	}

	return (sint32)recv(esp_state.socks[sock], (char *)buf, (int)len, 0);
}

auint cu_esp_net_link_state(uint32 sock)
{
	if(sock >= ESP_MAX_LINKS)
		return CU_ESP_LS_ERROR;
	return (auint)esp_state.link_state[sock];
}

auint cu_esp_net_link_take_connected(uint32 sock)
{
	if(sock >= ESP_MAX_LINKS)
		return 0u;

	if(esp_state.link_notice_ok[sock]){
		esp_state.link_notice_ok[sock] = 0u;
		return 1u;
	}
	return 0u;
}

auint cu_esp_net_link_take_error(uint32 sock, sint32 *err_out)
{
	if(sock >= ESP_MAX_LINKS)
		return 0u;

	if(esp_state.link_notice_err[sock]){
		esp_state.link_notice_err[sock] = 0u;
		if(err_out)
			*err_out = esp_state.link_err[sock];
		return 1u;
	}
	return 0u;
}

/* ------------------------------------------------------------------------- */
/* Ping helpers (exported)                                                    */
/* ------------------------------------------------------------------------- */

uint16 cu_esp_checksum_oc(void *b, sint32 len)
{
	sint16 *buf = (sint16 *)b;
	uint32 sum = 0u;
	uint16 result;

	while(len > 1){
		sum += (uint16)*buf++;
		len -= 2;
	}
	if(len == 1)
		sum += *(uint8 *)buf;

	sum = (sum >> 16) + (sum & 0xFFFFu);
	sum += (sum >> 16);

	result = (uint16)(~sum);
	return result;
}

sint16 ping_udp(const char *host, sint16 timeout_ms)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int rc;
	uint32 start_ms, end_ms;

	if(!host || !*host)
		return -1;

	if(timeout_ms <= 0)
		timeout_ms = 5000;

	/* create on demand if needed */
	if(esp_state.ping_sock[1] == -1 || esp_state.ping_sock[1] == ESP_INVALID_SOCKET){
		esp_state.ping_sock[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if(esp_state.ping_sock[1] == -1 || esp_state.ping_sock[1] == ESP_INVALID_SOCKET)
			return -1;
		cu_sock_set_nonblocking(esp_state.ping_sock[1]);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	rc = getaddrinfo(host, "33434", &hints, &res);
	if(rc != 0 || res == NULL)
		return -1;

	start_ms = cu_esp_now_ms();

	{
		const char payload[] = "CUzeBox UDP ping";
		int sent = (int)sendto(esp_state.ping_sock[1],
			(const char *)payload, (int)sizeof(payload), 0,
			res->ai_addr, (int)res->ai_addrlen);

		freeaddrinfo(res);

		if(sent < 0){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			int err = WSAGetLastError();
			if(err == ESP_WOULD_BLOCK || err == ESP_EAGAIN)
				return -1;
#else
			if(errno == ESP_WOULD_BLOCK || errno == ESP_EAGAIN)
				return -1;
#endif
			return -1;
		}
	}

	end_ms = cu_esp_now_ms();

	if(end_ms <= start_ms)
		return 0;

	{
		uint32 delta = end_ms - start_ms;
		if(delta > 0x7FFFu)
			delta = 0x7FFFu;
		return (sint16)delta;
	}
}

static sint16 ping_tcp_port(const char *host, const char *portstr, sint16 timeout_ms)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *p;
	uint32 start_ms, end_ms;

	if(!host || !*host)
		return -1;

	if(timeout_ms <= 0)
		timeout_ms = 5000;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if(getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
		return -1;

	start_ms = cu_esp_now_ms();

	for(p = res; p; p = p->ai_next){
		ESP_SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(s == ESP_INVALID_SOCKET || s == -1)
			continue;

		if(cu_sock_connect_timeout(s, (const struct sockaddr *)p->ai_addr, (socklen_t)p->ai_addrlen, timeout_ms) == 0){
			end_ms = cu_esp_now_ms();
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			closesocket(s);
#else
			close(s);
#endif
			freeaddrinfo(res);

			if(end_ms <= start_ms)
				return 0;

			{
				uint32 delta = end_ms - start_ms;
				if(delta > 0x7FFFu)
					delta = 0x7FFFu;
				return (sint16)delta;
			}
		}

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		closesocket(s);
#else
		close(s);
#endif
	}

	freeaddrinfo(res);
	return -1;
}

sint16 ping_tcp(const char *host, sint16 timeout_ms)
{
	sint16 r;

	r = ping_tcp_port(host, "443", timeout_ms);
	if(r >= 0)
		return r;

	return ping_tcp_port(host, "80", timeout_ms);
}

sint16 ping_icmp_raw(const char *host, sint16 timeout_ms)
{
	return cu_ping_icmp_impl(host, timeout_ms);
}

/* internal ICMP implementation */
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)

static sint16 cu_ping_icmp_impl(const char *host, sint16 timeout_ms)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	IPAddr ip;
	DWORD rc;
	uint8 reply_buf[sizeof(ICMP_ECHO_REPLY) + 64];
	const char payload[] = "CUzeBox ICMP ping";

	if(!host || !*host)
		return -1;
	if(!esp_state.ping_icmp_h || esp_state.ping_icmp_h == INVALID_HANDLE_VALUE)
		return -1;

	if(timeout_ms <= 0)
		timeout_ms = 5000;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(host, NULL, &hints, &res) != 0 || !res)
		return -1;

	ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(res);

	memset(reply_buf, 0, sizeof(reply_buf));

	rc = IcmpSendEcho(
		esp_state.ping_icmp_h,
		ip,
		(void *)payload,
		(uint16)sizeof(payload),
		NULL,
		reply_buf,
		(DWORD)sizeof(reply_buf),
		(DWORD)timeout_ms
	);

	if(rc == 0)
		return -1;

	{
		ICMP_ECHO_REPLY *rep = (ICMP_ECHO_REPLY *)reply_buf;
		uint32 rtt = (uint32)rep->RoundTripTime;
		if(rtt > 0x7FFFu)
			rtt = 0x7FFFu;
		return (sint16)rtt;
	}
}

#else /* POSIX */

static sint16 cu_ping_icmp_impl(const char *host, sint16 timeout_ms)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct sockaddr_in dst;
	uint8 pkt[sizeof(struct icmphdr) + 32];
	uint8 rx[1500];
	uint32 start_ms, end_ms;
	fd_set rfds;
	struct timeval tv;
	int rc;
	uint16 id;
	struct icmphdr *ih;

	if(!host || !*host)
		return -1;
	if(esp_state.ping_icmp_sock == ESP_INVALID_SOCKET || esp_state.ping_icmp_sock == -1)
		return -1;

	if(timeout_ms <= 0)
		timeout_ms = 5000;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_RAW;

	if(getaddrinfo(host, NULL, &hints, &res) != 0 || !res)
		return -1;

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);

	memset(pkt, 0, sizeof(pkt));
	ih = (struct icmphdr *)pkt;
	ih->type = ICMP_ECHO;
	ih->code = 0;
	id = (uint16)getpid();
	ih->un.echo.id = htons(id);
	ih->un.echo.sequence = htons(esp_state.ping_seq++);
	for(uint32 i = 0u; i < 32u; i++)
		pkt[sizeof(struct icmphdr) + i] = (uint8)(i * 7u + 3u);

	ih->checksum = 0u;
	ih->checksum = cu_esp_checksum_oc(pkt, (sint32)sizeof(pkt));

	start_ms = cu_esp_now_ms();

	rc = (int)sendto(esp_state.ping_icmp_sock, pkt, (int)sizeof(pkt), 0,
		(struct sockaddr *)&dst, (socklen_t)sizeof(dst));
	if(rc < 0)
		return -1;

	FD_ZERO(&rfds);
	FD_SET(esp_state.ping_icmp_sock, &rfds);
	tv.tv_sec = (long)(timeout_ms / 1000);
	tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

	rc = select((int)(esp_state.ping_icmp_sock + 1), &rfds, NULL, NULL, &tv);
	if(rc <= 0)
		return -1;

	{
		struct sockaddr_in src;
		socklen_t sl = (socklen_t)sizeof(src);
		int n = (int)recvfrom(esp_state.ping_icmp_sock, rx, (int)sizeof(rx), 0,
			(struct sockaddr *)&src, &sl);
		if(n <= 0)
			return -1;

		if(n < (int)(sizeof(struct iphdr) + sizeof(struct icmphdr)))
			return -1;

		{
			struct iphdr *ip = (struct iphdr *)rx;
			uint32 ihl = (uint32)(ip->ihl * 4u);
			if(n < (int)(ihl + sizeof(struct icmphdr)))
				return -1;

			ih = (struct icmphdr *)(rx + ihl);
			if(ih->type != ICMP_ECHOREPLY)
				return -1;
			if(ntohs(ih->un.echo.id) != id)
				return -1;
		}
	}

	end_ms = cu_esp_now_ms();

	if(end_ms <= start_ms)
		return 0;

	{
		uint32 delta = end_ms - start_ms;
		if(delta > 0x7FFFu)
			delta = 0x7FFFu;
		return (sint16)delta;
	}
}

#endif

/* ------------------------------------------------------------------------- */
/* Host serial passthrough                                                    */
/* ------------------------------------------------------------------------- */

#if !defined(WIN32) && !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MINGW32__)
static auint cu_esp_host_serial_baud_to_termios(auint baud, speed_t *out)
{
	if(!out)
		return 0u;

	switch(baud){
		case 2400:	*out = B2400; return 1u;
		case 4800:	*out = B4800; return 1u;
		case 9600:	*out = B9600; return 1u;
		case 19200:	*out = B19200; return 1u;
		case 38400:	*out = B38400; return 1u;
#ifdef B57600
		case 57600:	*out = B57600; return 1u;
#endif
#ifdef B115200
		case 115200:	*out = B115200; return 1u;
#endif
#ifdef B230400
		case 230400:	*out = B230400; return 1u;
#endif
#ifdef B460800
		case 460800:	*out = B460800; return 1u;
#endif
#ifdef B500000
		case 500000:	*out = B500000; return 1u;
#endif
#ifdef B576000
		case 576000:	*out = B576000; return 1u;
#endif
#ifdef B921600
		case 921600:	*out = B921600; return 1u;
#endif
#ifdef B1000000
		case 1000000:	*out = B1000000; return 1u;
#endif
#ifdef B1152000
		case 1152000:	*out = B1152000; return 1u;
#endif
#ifdef B1500000
		case 1500000:	*out = B1500000; return 1u;
#endif
#ifdef B2000000
		case 2000000:	*out = B2000000; return 1u;
#endif
#ifdef B2500000
		case 2500000:	*out = B2500000; return 1u;
#endif
		default:
			break;
	}

	return 0u;
}
#endif

sint32 cu_esp_host_serial_start(void)
{
	auint original_baud = esp_state.baud_rate;
	auint baud = original_baud;

	if(esp_state.host_serial_enabled)
		cu_esp_host_serial_end();

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)

	esp_state.host_serial_port = CreateFileA(
		(LPCSTR)esp_state.host_serial_device_name,
		GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if(esp_state.host_serial_port == INVALID_HANDLE_VALUE){
		print_error("ESP ERROR: Host Serial CreateFileA() failed: %u\n",
			(unsigned int)GetLastError());
		esp_state.host_serial_enabled = 0u;
		return ESP_SERIAL_OPEN_ERROR;
	}

	{
		BOOL success;
		COMMTIMEOUTS timeouts;
		DCB state;

		FlushFileBuffers(esp_state.host_serial_port);

		memset(&timeouts, 0, sizeof(timeouts));
		timeouts.ReadIntervalTimeout = MAXDWORD;
		timeouts.ReadTotalTimeoutConstant = 0;
		timeouts.ReadTotalTimeoutMultiplier = 0;
		timeouts.WriteTotalTimeoutConstant = 0;
		timeouts.WriteTotalTimeoutMultiplier = 0;

		success = SetCommTimeouts(esp_state.host_serial_port, &timeouts);
		if(!success){
			print_error("ESP ERROR: Host Serial SetCommTimeouts(): %lu\n",
				(DWORD)GetLastError());
			CloseHandle(esp_state.host_serial_port);
			esp_state.host_serial_port = INVALID_HANDLE_VALUE;
			esp_state.host_serial_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}

		memset(&state, 0, sizeof(state));
		state.DCBlength = sizeof(DCB);
		state.BaudRate = baud;
		state.ByteSize = 8;
		state.Parity = NOPARITY;
		state.StopBits = ONESTOPBIT;
		state.fBinary = TRUE;

		success = SetCommState(esp_state.host_serial_port, &state);
		if(!success){
			print_error("ESP ERROR: Host Serial SetCommState() for [%s] failed: %lu\n",
				esp_state.host_serial_device_name, (DWORD)GetLastError());
			CloseHandle(esp_state.host_serial_port);
			esp_state.host_serial_port = INVALID_HANDLE_VALUE;
			esp_state.host_serial_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
	}

#else /* POSIX */

	{
		speed_t tb;
		struct termios port_settings;
		int fd;

		if(!cu_esp_host_serial_baud_to_termios(baud, &tb)){
			print_error("ESP ERROR: Host Serial baud conversion failed, [%d] is not supported\n",
				(int)original_baud);
			esp_state.host_serial_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}

		fd = open((char *)esp_state.host_serial_device_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
		if(fd < 0){
			print_error("ESP ERROR: Host Serial open() for [%s] failed: %d\n",
				esp_state.host_serial_device_name, (int)cu_esp_get_last_error());
			esp_state.host_serial_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}

		memset(&port_settings, 0, sizeof(port_settings));
		if(tcgetattr(fd, &port_settings) != 0){
			print_error("ESP ERROR: Host Serial tcgetattr() failed: %d\n", (int)cu_esp_get_last_error());
			close(fd);
			esp_state.host_serial_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
		cfmakeraw(&port_settings);
#else
		port_settings.c_iflag = IGNPAR;
		port_settings.c_oflag = 0;
		port_settings.c_lflag = 0;
#endif
		port_settings.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
		port_settings.c_cflag |= (CLOCAL | CREAD | CS8);
		port_settings.c_cc[VMIN] = 0;
		port_settings.c_cc[VTIME] = 0;

		cfsetispeed(&port_settings, tb);
		cfsetospeed(&port_settings, tb);

		if(tcsetattr(fd, TCSANOW, &port_settings) != 0){
			print_error("ESP ERROR: Host Serial tcsetattr() failed: %d\n", (int)cu_esp_get_last_error());
			close(fd);
			esp_state.host_serial_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}

		esp_state.host_serial_port = fd;
	}

#endif

	esp_state.host_serial_enabled = 1u;
	print_message("ESP Host Serial initialized: [%s] @ %d\n",
		esp_state.host_serial_device_name, (int)original_baud);

	return 0;
}

void cu_esp_host_serial_end(void)
{
	if(!esp_state.host_serial_enabled)
		return;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(esp_state.host_serial_port && esp_state.host_serial_port != INVALID_HANDLE_VALUE){
		CloseHandle(esp_state.host_serial_port);
		esp_state.host_serial_port = INVALID_HANDLE_VALUE;
	}
#else
	if(esp_state.host_serial_port >= 0){
		close(esp_state.host_serial_port);
		esp_state.host_serial_port = -1;
	}
#endif

	esp_state.host_serial_enabled = 0u;
}

void cu_esp_host_serial_write(uint8 c)
{
	if(!esp_state.host_serial_enabled)
		return;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		DWORD written = 0;
		BOOL success = WriteFile(esp_state.host_serial_port, &c, 1, &written, NULL);
		if(!success || written != 1u){
			print_error("ESP ERROR: failed to write host serial byte\n");
		}
	}
#else
	{
		ssize_t r = write(esp_state.host_serial_port, &c, 1);
		if(r != 1){
			print_error("ESP ERROR: failed to write host serial byte\n");
		}
	}
#endif
}

uint8 cu_esp_host_serial_read(void)
{
	uint8 c = 0;

	if(!esp_state.host_serial_enabled)
		return 0;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		DWORD received = 0;
		BOOL success = ReadFile(esp_state.host_serial_port, &c, 1, &received, NULL);
		if(!success || received != 1u)
			return 0;
	}
#else
	{
		ssize_t received = read(esp_state.host_serial_port, &c, 1);
		if(received != 1)
			return 0;
	}
#endif

	return c;
}

auint cu_esp_host_serial_rx_bytes_ready(void)
{
	if(!esp_state.host_serial_enabled)
		return 0u;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		COMSTAT comstat;
		DWORD dwError = 0;
		ClearCommError(esp_state.host_serial_port, &dwError, &comstat);
		return (comstat.cbInQue > 0u) ? 1u : 0u;
	}
#else
	{
		int nbytes = 0;
		if(ioctl(esp_state.host_serial_port, FIONREAD, &nbytes) != 0)
			return 0u;
		return (nbytes > 0) ? 1u : 0u;
	}
#endif
}

sint32 cu_esp_tcp_serial_start(void)
{
	cu_esp_tcp_serial_end();
	memset(&cu_tcp_diag, 0, sizeof(cu_tcp_diag));
	cu_esp_tcp_serial_reset_buffers();
	esp_state.tcp_serial_sock = ESP_INVALID_SOCKET;
	esp_state.tcp_serial_listen_sock = ESP_INVALID_SOCKET;
	esp_state.tcp_serial_enabled = 1u;
	esp_state.tcp_serial_connect_pending = 0u;
	esp_state.tcp_serial_retry_at_ms = 0u;
	esp_state.tcp_serial_last_error = 0;
	esp_state.tcp_serial_state = CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
	cu_tcp_diag.initialized = 1u;
	cu_tcp_diag.last_observed_state = CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
	cu_tcp_diag.pub.mode = esp_state.tcp_serial_mode;
	cu_tcp_diag.pub.state = CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
	cu_tcp_diag.pub.enabled = 1u;
	cu_tcp_diag.pub.current_ms = cu_esp_now_ms();
	if(esp_state.tcp_serial_retry_interval_ms == 0u)
		esp_state.tcp_serial_retry_interval_ms = 1000u;
	if(cu_esp_init_sockets() != 0){
		esp_state.tcp_serial_enabled = 0u;
		esp_state.tcp_serial_last_error = -1;
		return ESP_SERIAL_OPEN_ERROR;
	}
	if(!cu_esp_tcp_serial_host_valid()){
		esp_state.tcp_serial_last_error = -2;
		return ESP_SERIAL_OPEN_ERROR;
	}
	/* Open the diagnostics file only after socket/ping initialization. Some
	 * headless environments have fd 0 available, and network initialization
	 * may close a stale zero-valued ping descriptor. */
	cu_tcp_diag_open_log();
	cu_tcp_auto_listen = 0u; /* AUTO always tries to join an existing peer first */
	cu_tcp_auto_dwell_until_ms = 0u;
	cu_link_lan_server = 0u;
#ifndef __EMSCRIPTEN__
	cu_imp_reset();
	if(cu_esp_tcp_serial_is_internet()){
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_CONNECTING, 0, "internet_start");
		(void)cu_lk_start(cu_link_relay_host, cu_link_relay_port, cu_link_room, cu_esp_now_ms());
		return 0;
	}
	if(cu_esp_tcp_serial_is_lan()){
		if(!cu_lan_start(cu_link_lan_port, esp_state.tcp_serial_port, cu_link_rom_id)){
			esp_state.tcp_serial_last_error = -cu_esp_get_last_error();
			cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, esp_state.tcp_serial_last_error, "lan_discovery_bind_failed");
			return ESP_SERIAL_OPEN_ERROR;
		}
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_SEARCHING, 0, "lan_searching");
		return 0;
	}
#endif
	if(cu_esp_tcp_serial_effective_mode() == CU_ESP_TCP_SERIAL_MODE_SERVER)
		cu_esp_tcp_serial_kick_listen();
	else
		cu_esp_tcp_serial_kick_connect();
	return 0;
}

void cu_esp_tcp_serial_end(void)
{
	boole had_diag = (cu_tcp_diag.log_file != NULL) ? TRUE : FALSE;
	esp_state.tcp_serial_enabled = 0u;
	esp_state.tcp_serial_connect_pending = 0u;
	esp_state.tcp_serial_retry_at_ms = 0u;
	if(had_diag)
		cu_tcp_diag_note_state(CU_ESP_TCP_SERIAL_STATE_DISCONNECTED, esp_state.tcp_serial_last_error, "tcp_serial_end");
	else
		esp_state.tcp_serial_state = CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
	cu_esp_tcp_serial_close_socket_only();
	cu_esp_tcp_serial_close_listener_only();
#ifndef __EMSCRIPTEN__
	cu_lk_stop();
	cu_lan_stop();
	cu_imp_reset();
#endif
	cu_esp_tcp_serial_reset_buffers();
	cu_tcp_diag_close_log("tcp_serial_end");
}

void cu_esp_tcp_serial_write(uint8 c)
{
	if(!esp_state.tcp_serial_enabled)
		return;
	/* AVR UDR writes only enqueue at the far side of the emulated UART.
	 * Host socket servicing is performed by cu_esp_endpoint_host_tick(), not
	 * from the timing-critical UART access path. */
	if(esp_state.tcp_serial_tx_count >= (uint32)sizeof(esp_state.tcp_serial_tx_buf)){
		esp_state.tcp_serial_tx_drops++;
		cu_tcp_diag_log_line("AVR_TX_DROP", "backend_queue_full", (sint32)c, TRUE);
		return;
	}
	esp_state.tcp_serial_tx_buf[esp_state.tcp_serial_tx_tail] = c;
	esp_state.tcp_serial_tx_tail = (esp_state.tcp_serial_tx_tail + 1u) % (uint32)sizeof(esp_state.tcp_serial_tx_buf);
	esp_state.tcp_serial_tx_count++;
	cu_tcp_diag.pub.avr_to_backend_bytes++;
	cu_tcp_diag.pub.last_avr_tx_ms = cu_tcp_diag.pub.current_ms;
	if(esp_state.tcp_serial_tx_count > cu_tcp_diag.pub.tx_queue_high_water)
		cu_tcp_diag.pub.tx_queue_high_water = esp_state.tcp_serial_tx_count;
}

uint8 cu_esp_tcp_serial_read(void)
{
	uint8 c = 0u;
	if(!esp_state.tcp_serial_enabled)
		return 0u;
	if(esp_state.tcp_serial_rx_count == 0u)
		return 0u;
	c = esp_state.tcp_serial_rx_buf[esp_state.tcp_serial_rx_head];
	esp_state.tcp_serial_rx_head = (esp_state.tcp_serial_rx_head + 1u) % (uint32)sizeof(esp_state.tcp_serial_rx_buf);
	esp_state.tcp_serial_rx_count--;
	cu_tcp_diag.pub.backend_to_avr_bytes++;
	cu_tcp_diag.pub.last_avr_rx_ms = cu_tcp_diag.pub.current_ms;
	return c;
}

auint cu_esp_tcp_serial_rx_bytes_ready(void)
{
	/* Pure endpoint queue query. The AVR UART model decides when RXC/UDR
	 * expose a queued byte according to UBRR/U2X/frame settings. */
	return (esp_state.tcp_serial_rx_count != 0u) ? 1u : 0u;
}

void cu_esp_endpoint_host_tick(void)
{
	/* Host transport servicing is independent of the emulated UART. This
	 * function only moves bytes between the OS socket and the endpoint rings.
	 * cu_uart.c remains the sole authority for when a byte becomes visible to
	 * the ATmega644 through RXC/UDR and for when the transmitter is ready.
	 * The ESP8266 module route is deliberately not touched here; its existing
	 * timer/network path remains unchanged.
	 */
	if(esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL &&
	   esp_state.tcp_serial_enabled)
		cu_esp_tcp_serial_service();
}

static void cu_esp_config_normalize(cu_state_esp_t *st, boole verbose);

void cu_esp_apply_serial_route_live(void)
{
	cu_esp_host_serial_end();
	cu_esp_tcp_serial_end();
	cu_esp_serial_midi_end();
	cu_esp_net_cleanup();
	cu_esp_reset_uart();

	if(esp_state.serial_route == CU_ESP_SERIAL_ESP_MODULE){
		if(!esp_state.reset_pin)
			return;
		cu_esp_reset_network();
	}else if(esp_state.serial_route == CU_ESP_SERIAL_HOST_SERIAL){
		(void)cu_esp_host_serial_start();
	}else if(esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL){
		(void)cu_esp_tcp_serial_start();
	}else if((esp_state.serial_route == CU_ESP_SERIAL_HOST_MIDI) ||
	         (esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI)){
		(void)cu_esp_serial_midi_start(esp_state.serial_route);
	}
}

auint cu_esp_get_serial_route(void)
{
	return esp_state.serial_route;
}

void cu_esp_set_serial_route(auint route)
{
	esp_state.serial_route = route;
	cu_esp_config_normalize(&esp_state, FALSE);
	cu_esp_apply_serial_route_live();
}

auint cu_esp_get_serial_esp_model(void)
{
	return esp_state.serial_esp_model;
}

void cu_esp_set_serial_esp_model(auint model)
{
	esp_state.serial_esp_model = model;
	cu_esp_config_normalize(&esp_state, FALSE);
	cu_esp_apply_serial_route_live();
}

auint cu_esp_get_at_firmware_profile(void)
{
	return esp_state.at_firmware_profile;
}

void cu_esp_set_at_firmware_profile(auint profile)
{
	if(profile > CU_ESP_AT_FW_ESPAT_230)
		profile = CU_ESP_AT_FW_ESPAT_230;
	esp_state.at_firmware_profile = (uint8)profile;
}

boole cu_esp_get_softap_enabled(void)
{
	return (esp_state.soft_ap_enabled != 0u) ? TRUE : FALSE;
}

void cu_esp_set_softap_enabled(boole enable)
{
	esp_state.soft_ap_enabled = enable ? 1u : 0u;
	cu_esp_config_normalize(&esp_state, FALSE);
	cu_esp_lan_on_softap_change(&esp_state);
}

char const* cu_esp_get_host_serial_device_name(void)
{
	return (char const*)esp_state.host_serial_device_name;
}

void cu_esp_set_host_serial_device_name(char const* name)
{
	cu_esp_s8cpy(esp_state.host_serial_device_name, sizeof(esp_state.host_serial_device_name),
		(name != NULL) ? name : "");
}

char const* cu_esp_get_host_midi_port_name(void)
{
	return (char const*)esp_state.host_midi_port_name;
}

void cu_esp_set_host_midi_port_name(char const* name)
{
	cu_esp_s8cpy(esp_state.host_midi_port_name, sizeof(esp_state.host_midi_port_name),
		(name != NULL) ? name : "");
}

auint cu_esp_get_virtual_midi_mode(void)
{
	return esp_state.virtual_midi_mode;
}

void cu_esp_set_virtual_midi_mode(auint mode)
{
	esp_state.virtual_midi_mode = mode;
	cu_esp_config_normalize(&esp_state, FALSE);
	cu_esp_apply_serial_route_live();
}

char const* cu_esp_get_virtual_midi_port_name(void)
{
	return (char const*)esp_state.virtual_midi_port_name;
}

void cu_esp_set_virtual_midi_port_name(char const* name)
{
	cu_esp_s8cpy(esp_state.virtual_midi_port_name, sizeof(esp_state.virtual_midi_port_name),
		(name != NULL) ? name : "");
}

auint cu_esp_get_uart_profile(void)
{
	return (auint)esp_state.uart_profile;
}

void cu_esp_set_uart_profile(auint profile)
{
	esp_state.uart_profile = (uint8)profile;
	cu_esp_config_normalize(&esp_state, FALSE);
	cu_esp_reset_uart();
}

auint cu_esp_get_tcp_serial_state(void)
{
	return esp_state.tcp_serial_state;
}

sint32 cu_esp_get_tcp_serial_last_error(void)
{
	return esp_state.tcp_serial_last_error;
}

char const* cu_esp_get_tcp_serial_host(void)
{
	return (char const*)esp_state.tcp_serial_host;
}

void cu_esp_set_tcp_serial_host(char const* host)
{
	cu_esp_s8cpy(esp_state.tcp_serial_host, sizeof(esp_state.tcp_serial_host),
		(host != NULL) ? host : "");
}

auint cu_esp_get_tcp_serial_port(void)
{
	return (auint)esp_state.tcp_serial_port;
}

void cu_esp_set_tcp_serial_port(auint port)
{
	esp_state.tcp_serial_port = (uint32)port;
}

boole cu_esp_get_tcp_serial_auto_reconnect(void)
{
	return esp_state.tcp_serial_auto_reconnect ? TRUE : FALSE;
}

void cu_esp_set_tcp_serial_auto_reconnect(boole enable)
{
	esp_state.tcp_serial_auto_reconnect = enable ? 1u : 0u;
}

auint cu_esp_get_tcp_serial_role(void)
{
	return (auint)cu_esp_tcp_serial_effective_mode();
}

void cu_esp_link_set_rom_id(uint32 rom_id)
{
	cu_link_rom_id = rom_id;
#ifndef __EMSCRIPTEN__
	cu_lan.rom = rom_id; /* a ROM change while searching is picked up live */
#endif
}

char const* cu_esp_link_get_room(void){ return cu_link_room; }

void cu_esp_link_set_room(char const* room)
{
#ifndef __EMSCRIPTEN__
	cu_lk_normalize_room(cu_link_room, sizeof(cu_link_room), room);
#else
	snprintf(cu_link_room, sizeof(cu_link_room), "%s", room ? room : "");
#endif
}

char const* cu_esp_link_get_relay_host(void){ return cu_link_relay_host; }

void cu_esp_link_set_relay_host(char const* host)
{
	uint32 o = 0u;
	/* Host names / IP literals only (also keeps the JSON status safe). */
	while(host != NULL && *host != 0 && o + 1u < sizeof(cu_link_relay_host)){
		char c = *host++;
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':')
			cu_link_relay_host[o++] = c;
	}
	cu_link_relay_host[o] = 0;
	if(o == 0u)
		snprintf(cu_link_relay_host, sizeof(cu_link_relay_host), "uzenet.us");
}

auint cu_esp_link_get_relay_port(void){ return cu_link_relay_port; }

void cu_esp_link_set_relay_port(auint port)
{
	cu_link_relay_port = (port != 0u && port <= 65535u) ? (uint32)port : 43810u;
}

void cu_esp_link_get_impair(cu_esp_link_impair_t* out)
{
	if(out == NULL)
		return;
#ifndef __EMSCRIPTEN__
	out->enabled = cu_imp_cfg.enabled;
	out->latency_ms = cu_imp_cfg.latency_ms;
	out->jitter_ms = cu_imp_cfg.jitter_ms;
	out->stall_every_ms = cu_imp_cfg.stall_every_ms;
	out->stall_ms = cu_imp_cfg.stall_ms;
	out->noise_ppm = cu_imp_cfg.noise_ppm;
	out->drop_ppm = cu_imp_cfg.drop_ppm;
#else
	memset(out, 0, sizeof(*out));
#endif
}

void cu_esp_link_set_impair(cu_esp_link_impair_t const* in)
{
#ifndef __EMSCRIPTEN__
	if(in == NULL)
		return;
	cu_imp_cfg.enabled = in->enabled ? 1u : 0u;
	cu_imp_cfg.latency_ms = (in->latency_ms > 10000u) ? 10000u : in->latency_ms;
	cu_imp_cfg.jitter_ms = (in->jitter_ms > 5000u) ? 5000u : in->jitter_ms;
	cu_imp_cfg.stall_every_ms = (in->stall_every_ms > 600000u) ? 600000u : in->stall_every_ms;
	cu_imp_cfg.stall_ms = (in->stall_ms > 30000u) ? 30000u : in->stall_ms;
	cu_imp_cfg.noise_ppm = (in->noise_ppm > 1000000u) ? 1000000u : in->noise_ppm;
	cu_imp_cfg.drop_ppm = (in->drop_ppm > 1000000u) ? 1000000u : in->drop_ppm;
	cu_imp.stall_armed = 0u;
#else
	(void)in;
#endif
}

char const* cu_esp_link_get_notice(void)
{
#ifndef __EMSCRIPTEN__
	if(cu_esp_tcp_serial_is_internet())
		return cu_lk.notice;
#endif
	return "";
}

auint cu_esp_link_lan_peers(cu_esp_link_lan_peer_t* out, auint max)
{
	auint n = 0u;
#ifndef __EMSCRIPTEN__
	uint32 i, now = cu_esp_now_ms();
	for(i = 0u; i < CU_LAN_MAX_PEERS && n < max; i++){
		cu_lan_peer_t *p = &cu_lan.peers[i];
		struct in_addr a;
		if(!p->used || CU_LK_TIME_GE(now, p->last_seen + CU_LAN_PEER_TTL_MS))
			continue;
		out[n].id = p->id;
		out[n].same_rom = (p->rom == cu_lan.rom) ? 1u : 0u;
		out[n].searching = (p->flags & CU_LAN_F_SEARCHING) ? 1u : 0u;
		out[n].wants_us = (p->want == cu_lan.my_id) ? 1u : 0u;
		memcpy(out[n].name, p->name, sizeof(out[n].name));
		out[n].name[sizeof(out[n].name) - 1u] = 0;
		a.s_addr = p->ip_be;
		snprintf(out[n].ip, sizeof(out[n].ip), "%s", inet_ntoa(a));
		n++;
	}
#else
	(void)out; (void)max;
#endif
	return n;
}

void cu_esp_link_lan_choose(uint32 id)
{
#ifndef __EMSCRIPTEN__
	cu_lan.want = id;
	if(cu_lan.paired_id != 0u && cu_lan.paired_id != id && !cu_lan.linked)
		cu_lan_unpair();
#else
	(void)id;
#endif
}

/* JSON object (no surrounding braces' key) describing the link transport. */
auint cu_esp_link_status_json(char* buf, auint cap)
{
	int n;
#ifndef __EMSCRIPTEN__
	uint32 i, now = cu_esp_now_ms(), peers = 0u;
	char note[80];
	uint32 o = 0u;
	for(i = 0u; cu_lk.notice[i] != 0 && o + 1u < sizeof(note); i++){
		char c = cu_lk.notice[i];
		if(c == '"' || c == '\\' || (unsigned char)c < 32u) c = ' ';
		note[o++] = c;
	}
	note[o] = 0;
	for(i = 0u; i < CU_LAN_MAX_PEERS; i++)
		if(cu_lan.peers[i].used && !CU_LK_TIME_GE(now, cu_lan.peers[i].last_seen + CU_LAN_PEER_TTL_MS))
			peers++;
	n = snprintf(buf, cap,
		"{\"room\":\"%s\",\"relay_host\":\"%s\",\"relay_port\":%u,"
		"\"internet\":{\"phase\":%u,\"connected\":%u,\"owner\":%u,\"direct\":%u,\"srtt_ms\":%u,\"rto_ms\":%u,"
		"\"pkts_tx\":%u,\"pkts_rx\":%u,\"retransmits\":%u,\"relay_tx\":%u,\"direct_tx\":%u,\"unacked\":%u,\"notice\":\"%s\"},"
		"\"lan\":{\"active\":%u,\"id\":%u,\"rom\":%u,\"peers\":%u,\"paired\":%u,\"server\":%u,\"linked\":%u,\"beacons_rx\":%u},"
		"\"impair\":{\"enabled\":%u,\"latency_ms\":%u,\"jitter_ms\":%u,\"stall_every_ms\":%u,\"stall_ms\":%u,\"noise_ppm\":%u,\"drop_ppm\":%u,"
		"\"queued\":%u,\"corrupted\":%u,\"dropped\":%u,\"stalls\":%u,\"overflow\":%u}}",
		cu_link_room, cu_link_relay_host, (unsigned)cu_link_relay_port,
		(unsigned)cu_lk.phase, (unsigned)cu_lk.connected, (unsigned)cu_lk.owner, (unsigned)cu_lk_direct_usable(now),
		(unsigned)cu_lk.srtt, (unsigned)cu_lk.rto, (unsigned)cu_lk.pkts_tx, (unsigned)cu_lk.pkts_rx,
		(unsigned)cu_lk.retransmits, (unsigned)cu_lk.relay_tx, (unsigned)cu_lk.direct_tx, (unsigned)cu_lk.tx_count, note,
		(unsigned)cu_lan.active, (unsigned)cu_lan.my_id, (unsigned)cu_lan.rom, (unsigned)peers, (unsigned)cu_lan.paired_id,
		(unsigned)cu_lan.paired_server, (unsigned)cu_lan.linked, (unsigned)cu_lan.beacons_rx,
		(unsigned)cu_imp_cfg.enabled, (unsigned)cu_imp_cfg.latency_ms, (unsigned)cu_imp_cfg.jitter_ms,
		(unsigned)cu_imp_cfg.stall_every_ms, (unsigned)cu_imp_cfg.stall_ms, (unsigned)cu_imp_cfg.noise_ppm,
		(unsigned)cu_imp_cfg.drop_ppm, (unsigned)cu_imp.count, (unsigned)cu_imp.corrupted, (unsigned)cu_imp.dropped,
		(unsigned)cu_imp.stalls, (unsigned)cu_imp.overflow);
	/* Append the LAN peer list inside the "lan" object would complicate the
	 * format string; add it as a sibling array instead: ...,"lan_peers":[...]} */
	if(n > 0 && (auint)n + 2u < cap){
		cu_esp_link_lan_peer_t pl[CU_LAN_MAX_PEERS];
		auint np = cu_esp_link_lan_peers(pl, CU_LAN_MAX_PEERS), k;
		int m;
		n--; /* drop the final '}' */
		m = snprintf(buf + n, cap - (auint)n, ",\"lan_peers\":[");
		if(m > 0) n += m;
		for(k = 0u; k < np && (auint)n + 160u < cap; k++){
			char nm[16];
			uint32 c;
			for(c = 0u; c + 1u < sizeof(nm) && pl[k].name[c] != 0; c++)
				nm[c] = (pl[k].name[c] == '"' || pl[k].name[c] == '\\' || (unsigned char)pl[k].name[c] < 32u) ? ' ' : pl[k].name[c];
			nm[c] = 0;
			m = snprintf(buf + n, cap - (auint)n, "%s{\"id\":%u,\"name\":\"%s\",\"ip\":\"%s\",\"same_rom\":%u,\"searching\":%u,\"wants_us\":%u}",
				k ? "," : "", (unsigned)pl[k].id, nm, pl[k].ip, (unsigned)pl[k].same_rom, (unsigned)pl[k].searching, (unsigned)pl[k].wants_us);
			if(m > 0) n += m;
		}
		if((auint)n + 3u < cap){ buf[n++] = ']'; buf[n++] = '}'; buf[n] = 0; }
	}
#else
	n = snprintf(buf, cap, "{}");
#endif
	if(n < 0)
		n = 0;
	return ((auint)n < cap) ? (auint)n : (cap ? cap - 1u : 0u);
}

auint cu_esp_get_tcp_serial_mode(void)
{
	return (auint)esp_state.tcp_serial_mode;
}

void cu_esp_set_tcp_serial_mode(auint mode)
{
	esp_state.tcp_serial_mode = (mode <= CU_ESP_TCP_SERIAL_MODE_LAN) ? mode : CU_ESP_TCP_SERIAL_MODE_CLIENT;
}

/* ------------------------------------------------------------------------- */
/* Factory defaults                                                           */
/* ------------------------------------------------------------------------- */

static const char cu_esp_cfg_path[] = "config.cfg";
static const char cu_esp_legacy_cfg_path[] = "esp.cfg";
static boole cu_esp_config_loaded = FALSE;

/* ------------------------------------------------------------------------- */
/* Factory defaults                                                           */
/* ------------------------------------------------------------------------- */

static void cu_esp_factory_defaults_state(cu_state_esp_t *st)
{
		char macs[32];
	
		memset(st, 0, sizeof(*st));
	
		st->uart_baud_bits_module_default = ESP_FACTORY_DEFAULT_BAUD_BITS;
		st->uart_baud_bits_module = ESP_FACTORY_DEFAULT_BAUD_BITS;
		st->baud_rate = ESP_FACTORY_BAUD_RATE;
	
		st->uart_logging = 0u;
		st->uart_playback = 0u;
		st->emulation_model = 3;	/* ESP32-ETH01 */
		st->sleep_mode = 2;
		st->vdd33 = 3300;
	
		st->rf_power = 82; /* arbitrary reasonable default */
		st->server_max_conn = ESP_MAX_LINKS;
		st->ssl_rx_buf_size = 4096;
		st->cipcheckseq = 0u;
		st->cipbuf_recv_mode = 0u;
		st->dns_enable = 0u;
		st->mdns_enable = 0u;
		st->mdns_port = 0u;
		st->sysmsg_flags = 0u;
		st->adc = 1024u;
		st->at_firmware_profile = CU_ESP_AT_FW_ESPAT_230;
		st->sysstore_mode = 1u;
		st->syslog_enabled = 0u;
		st->ipv6_enabled = 0u;
		st->cip_reconn_interval = 1u;
		st->wifi_reconn_repeat = 0u;
		st->wifi_ap_proto = 3u;
		st->wifi_sta_proto = 3u;
		st->wifi_autoconn = 1u;
		cu_esp_s8cpy(st->station_hostname, sizeof(st->station_hostname), "cuzebox");
		st->mqtt_scheme = 1u;
		st->mqtt_keepalive = 120u;
		cu_esp_s8cpy(st->mqtt_path, sizeof(st->mqtt_path), "/mqtt");
		memset(st->sysflash_data, 0xFF, sizeof(st->sysflash_data));
		for(uint32 i=0;i<ESP_MAX_LINKS;i++){ st->tcp_linger[i] = -1; st->tcp_nodelay[i]=0u; st->tcp_sndtimeo[i]=0u; }
	
		st->wifi_channel = ESP_DEFAULT_CHANNEL;
		st->wifi_mode = ESP_WIFI_MODE_STATION;
		st->wifi_enc = 3;
		st->wifi_rssi = -42;
		st->wifi_pci_en = 1;
		st->wifi_reconn_interval = ESP_DEFAULT_RECONN_INTERVAL;
		st->wifi_listen_interval = ESP_DEFAULT_LISTEN_INTERVAL;
		st->wifi_scan_mode = ESP_DEFAULT_SCAN_MODE;
		st->wifi_jap_timeout = ESP_DEFAULT_JAP_TIMEOUT;
		st->wifi_pmf = ESP_DEFAULT_PMF;
	
		st->country_policy = ESP_DEFAULT_COUNTRY_POLICY;
		cu_esp_s8cpy(st->country_code, sizeof(st->country_code), ESP_DEFAULT_COUNTRY_CODE);
		st->country_start_ch = ESP_DEFAULT_COUNTRY_START_CH;
		st->country_count = ESP_DEFAULT_COUNTRY_COUNT;
	
		cu_esp_s8cpy(st->soft_ap_name, sizeof(st->soft_ap_name), "CUzeBox SoftAP");
		cu_esp_s8cpy(st->soft_ap_pass, sizeof(st->soft_ap_pass), "s0m3p4ssw0rd");
		cu_esp_s8cpy(st->soft_ap_mac, sizeof(st->soft_ap_mac), "ec:44:4a:67:cb:d8");
		cu_esp_s8cpy(st->soft_ap_ip, sizeof(st->soft_ap_ip), "10.0.0.1");
		cu_esp_s8cpy(st->soft_ap_gateway, sizeof(st->soft_ap_gateway), "10.0.0.1");
		cu_esp_s8cpy(st->soft_ap_netmask, sizeof(st->soft_ap_netmask), "255.255.255.0");
		st->soft_ap_channel = ESP_DEFAULT_CHANNEL;
		st->soft_ap_encryption = 3u;
		st->soft_ap_enabled = 1u;
		st->soft_ap_max_conn = 4u;
		st->soft_ap_hidden = 0u;
		st->soft_ap_dhcp_lease_min = 5u;
	
		cu_esp_s8cpy(st->wifi_name, sizeof(st->wifi_name), "uzenet");
		cu_esp_s8cpy(st->wifi_pass, sizeof(st->wifi_pass), "s0m3p4ssw0rd");
		cu_esp_s8cpy(st->wifi_mac, sizeof(st->wifi_mac), "60:22:32:e5:f3:85");
		cu_esp_s8cpy(st->wifi_ip, sizeof(st->wifi_ip), "10.0.0.2");
	
		cu_esp_s8cpy(st->ethernet_mac, sizeof(st->ethernet_mac), "60:22:32:e5:f3:85");
		cu_esp_s8cpy(st->ethernet_ip, sizeof(st->ethernet_ip), "10.0.0.2");
		cu_esp_s8cpy(st->bluetooth_mac, sizeof(st->bluetooth_mac), "60:22:32:e5:f3:85");
		cu_esp_s8cpy(st->bluetooth_ip, sizeof(st->bluetooth_ip), "10.0.0.2");
	
		/* station aliases */
		cu_esp_s8cpy(st->station_mac, sizeof(st->station_mac), (const char *)st->wifi_mac);
		cu_esp_s8cpy(st->station_ip, sizeof(st->station_ip), (const char *)st->wifi_ip);
		cu_esp_s8cpy(st->station_gateway, sizeof(st->station_gateway), "10.0.0.1");
		cu_esp_s8cpy(st->station_netmask, sizeof(st->station_netmask), "255.255.255.0");
	
		cu_esp_s8cpy(st->uart_logging_fname, sizeof(st->uart_logging_fname), "uart-debug.dat");
		cu_esp_s8cpy(st->uart_playback_fname, sizeof(st->uart_playback_fname), "uart-debug.dat");
		st->serial_route = CU_ESP_SERIAL_ESP_MODULE;
		st->serial_esp_model = 1u;
		st->uart_profile = CU_UART_PROFILE_FAST;
		st->virtual_midi_mode = CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL;
	
	#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		cu_esp_s8cpy(st->host_serial_device_name, sizeof(st->host_serial_device_name), "COM3");
		st->host_serial_port = INVALID_HANDLE_VALUE;
		cu_esp_s8cpy(st->host_midi_port_name, sizeof(st->host_midi_port_name), "");
		cu_esp_s8cpy(st->virtual_midi_port_name, sizeof(st->virtual_midi_port_name), "CUzeBox MIDI");
	#else
		cu_esp_s8cpy(st->host_serial_device_name, sizeof(st->host_serial_device_name), "/dev/ttyUSB0");
		st->host_serial_port = -1;
		cu_esp_s8cpy(st->host_midi_port_name, sizeof(st->host_midi_port_name), "CUzeBox MIDI");
		cu_esp_s8cpy(st->virtual_midi_port_name, sizeof(st->virtual_midi_port_name), "CUzeBox MIDI");
	#endif
		st->host_serial_enabled = 0u;
		st->host_midi_enabled = 0u;
		st->virtual_midi_enabled = 0u;
		cu_esp_s8cpy(st->tcp_serial_host, sizeof(st->tcp_serial_host), "127.0.0.1");
		st->tcp_serial_port = 12001u;
		st->tcp_serial_auto_reconnect = 1u;
		st->tcp_serial_mode = CU_ESP_TCP_SERIAL_MODE_AUTO;
		st->tcp_serial_retry_interval_ms = 1000u;
		st->tcp_serial_retry_at_ms = 0u;
		st->tcp_serial_last_error = 0;
		st->tcp_serial_connect_pending = 0u;
		st->tcp_serial_state = CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
		st->tcp_serial_sock = ESP_INVALID_SOCKET;
		st->tcp_serial_listen_sock = ESP_INVALID_SOCKET;
		st->tcp_serial_enabled = 0u;
		cu_esp_tcp_serial_reset_buffers();
	
		/* SNTP */
		st->sntp_enabled = 1u;
		st->sntp_timezone = ESP_DEFAULT_TIMEZONE;
		cu_esp_s8cpy(st->sntp_server[0], sizeof(st->sntp_server[0]), default_sntp_server0);
		cu_esp_s8cpy(st->sntp_server[1], sizeof(st->sntp_server[1]), default_sntp_server1);
		cu_esp_s8cpy(st->sntp_server[2], sizeof(st->sntp_server[2]), default_sntp_server2);
	
		/* enable autoconnect by default */
		st->state |= ESP_AUTOCONNECT;
	
		/* Virtual LAN overlay defaults */
		st->lan.enable = 1u;
		st->lan.adv_enable = 0u;
		st->lan.joined = 0u;
		st->lan.dhcp_enable_ap = 1u;
		st->lan.dhcp_enable_sta = 1u;
		st->lan.mcast_sock = ESP_INVALID_SOCKET;
		st->lan.ucast_sock = ESP_INVALID_SOCKET;
		st->lan.ap_ip_be = cu_esp_ipv4_to_be((const char *)st->soft_ap_ip, htonl(0x0A000001u)); /* 10.0.0.1 */
		st->lan.netmask_be = htonl(0xFFFFFF00u);
		st->lan.pool_start_be = htonl(0x0A000064u); /* 10.0.0.100 */
		st->lan.pool_end_be = htonl(0x0A00006Fu); /* 10.0.0.111 */
		st->lan.dns_ip_be = st->lan.ap_ip_be;
	
		if(!cu_esp_parse_mac6((const char *)st->soft_ap_mac, st->lan.local_mac)){
			st->lan.local_mac[0] = 0x02;
			st->lan.local_mac[1] = 0x43;
			st->lan.local_mac[2] = 0x55;
			st->lan.local_mac[3] = 0x5A;
			st->lan.local_mac[4] = 0x45;
			st->lan.local_mac[5] = 0x01;
		}
		/* force locally administered bit */
		st->lan.local_mac[0] |= 0x02u;
		st->lan.local_mac[0] &= (uint8)~0x01u;
	
		cu_esp_format_mac6(st->lan.local_mac, macs, sizeof(macs));
		(void)macs;}

void cu_esp_reset_factory(void)
{
	cu_esp_factory_defaults_state(&esp_state);
}

/* ------------------------------------------------------------------------- */
/* Config helpers                                                             */
/* ------------------------------------------------------------------------- */

static boole cu_esp_apply_cfg_kv(cu_state_esp_t *st, const char *key, const char *val)
{
	uint8 tmp_mac6[6];

	if(!st || !key || !val){
		return FALSE;
	}

	if(strcmp(key, "EspEmulationModel") == 0){
			st->emulation_model = (auint)atoi(val);
		}else if(strcmp(key, "SerialRoute") == 0){
			st->serial_route = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SerialEspModel") == 0){
			st->serial_esp_model = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "UartProfile") == 0){
			st->uart_profile = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtFirmwareProfile") == 0){
			st->at_firmware_profile = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtSysStore") == 0){
			st->sysstore_mode = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtIpv6") == 0){
			st->ipv6_enabled = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtStationHostname") == 0){
			cu_esp_s8cpy(st->station_hostname, sizeof(st->station_hostname), val);
		}else if(strcmp(key, "EspAtTranslinkEnabled") == 0){
			st->translink_enabled = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtTranslinkKind") == 0){
			st->translink_kind = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtTranslinkHost") == 0){
			cu_esp_s8cpy(st->translink_host, sizeof(st->translink_host), val);
		}else if(strcmp(key, "EspAtTranslinkPort") == 0){
			st->translink_port = (uint16)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtTranslinkLocalPort") == 0){
			st->translink_local_port = (uint16)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtTranslinkKeepAlive") == 0){
			st->translink_keepalive = (uint16)strtoul(val, NULL, 10);
		}else if(strcmp(key, "EspAtTranslinkUdpMode") == 0){
			st->translink_udp_mode = (uint8)strtoul(val, NULL, 10);
	
		}else if(strcmp(key, "SoftApName") == 0){
			cu_esp_s8cpy(st->soft_ap_name, sizeof(st->soft_ap_name), val);
		}else if(strcmp(key, "SoftApPass") == 0){
			cu_esp_s8cpy(st->soft_ap_pass, sizeof(st->soft_ap_pass), val);
		}else if(strcmp(key, "SoftApMac") == 0){
			cu_esp_s8cpy(st->soft_ap_mac, sizeof(st->soft_ap_mac), val);
		}else if(strcmp(key, "SoftApIp") == 0){
			cu_esp_s8cpy(st->soft_ap_ip, sizeof(st->soft_ap_ip), val);
		}else if(strcmp(key, "SoftApGateway") == 0){
			cu_esp_s8cpy(st->soft_ap_gateway, sizeof(st->soft_ap_gateway), val);
		}else if(strcmp(key, "SoftApNetmask") == 0){
			cu_esp_s8cpy(st->soft_ap_netmask, sizeof(st->soft_ap_netmask), val);
		}else if(strcmp(key, "SoftApChannel") == 0){
			st->soft_ap_channel = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SoftApEncryption") == 0){
			st->soft_ap_encryption = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SoftApMaxConn") == 0){
			st->soft_ap_max_conn = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SoftApHidden") == 0){
			st->soft_ap_hidden = (uint8)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SoftApDhcpLeaseMin") == 0){
			st->soft_ap_dhcp_lease_min = (uint16)strtoul(val, NULL, 10);
	
		}else if(strcmp(key, "WifiName") == 0){
			cu_esp_s8cpy(st->wifi_name, sizeof(st->wifi_name), val);
		}else if(strcmp(key, "WifiPass") == 0){
			cu_esp_s8cpy(st->wifi_pass, sizeof(st->wifi_pass), val);
		}else if(strcmp(key, "WifiMac") == 0){
			cu_esp_s8cpy(st->wifi_mac, sizeof(st->wifi_mac), val);
		}else if(strcmp(key, "WifiIp") == 0){
			cu_esp_s8cpy(st->wifi_ip, sizeof(st->wifi_ip), val);
		}else if(strcmp(key, "WifiChannel") == 0){
			st->wifi_channel = (uint8)atoi(val);
		}else if(strcmp(key, "WifiMode") == 0){
			st->wifi_mode = (uint8)atoi(val);
		}else if(strcmp(key, "WifiEnc") == 0){
			st->wifi_enc = (uint8)atoi(val);
		}else if(strcmp(key, "WifiRssi") == 0){
			st->wifi_rssi = (sint8)atoi(val);
		}else if(strcmp(key, "WifiPciEn") == 0){
			st->wifi_pci_en = (uint8)atoi(val);
		}else if(strcmp(key, "WifiReconnInterval") == 0){
			st->wifi_reconn_interval = (uint16)atoi(val);
		}else if(strcmp(key, "WifiListenInterval") == 0){
			st->wifi_listen_interval = (uint8)atoi(val);
		}else if(strcmp(key, "WifiScanMode") == 0){
			st->wifi_scan_mode = (uint8)atoi(val);
		}else if(strcmp(key, "WifiJapTimeout") == 0){
			st->wifi_jap_timeout = (uint16)atoi(val);
		}else if(strcmp(key, "WifiPmf") == 0){
			st->wifi_pmf = (uint8)atoi(val);
		}else if(strcmp(key, "AutoConnect") == 0){
			if((auint)atoi(val))
			st->state |= ESP_AUTOCONNECT;
			else
			st->state &= ~ESP_AUTOCONNECT;
	
		}else if(strcmp(key, "StationMac") == 0){
			cu_esp_s8cpy(st->station_mac, sizeof(st->station_mac), val);
		}else if(strcmp(key, "StationIp") == 0){
			cu_esp_s8cpy(st->station_ip, sizeof(st->station_ip), val);
		}else if(strcmp(key, "StationGateway") == 0){
			cu_esp_s8cpy(st->station_gateway, sizeof(st->station_gateway), val);
		}else if(strcmp(key, "StationNetmask") == 0){
			cu_esp_s8cpy(st->station_netmask, sizeof(st->station_netmask), val);
	
		}else if(strcmp(key, "CountryPolicy") == 0){
			st->country_policy = (uint8)atoi(val);
		}else if(strcmp(key, "CountryCode") == 0){
			cu_esp_s8cpy(st->country_code, sizeof(st->country_code), val);
		}else if(strcmp(key, "CountryStartCh") == 0){
			st->country_start_ch = (uint8)atoi(val);
		}else if(strcmp(key, "CountryCount") == 0){
			st->country_count = (uint8)atoi(val);
	
		}else if(strcmp(key, "EthernetMac") == 0){
			cu_esp_s8cpy(st->ethernet_mac, sizeof(st->ethernet_mac), val);
		}else if(strcmp(key, "EthernetIp") == 0){
			cu_esp_s8cpy(st->ethernet_ip, sizeof(st->ethernet_ip), val);
		}else if(strcmp(key, "BluetoothMac") == 0){
			cu_esp_s8cpy(st->bluetooth_mac, sizeof(st->bluetooth_mac), val);
		}else if(strcmp(key, "BluetoothIp") == 0){
			cu_esp_s8cpy(st->bluetooth_ip, sizeof(st->bluetooth_ip), val);
	
		}else if(strcmp(key, "UzenetPass") == 0){
			cu_esp_s8cpy(st->uzenet_pass, sizeof(st->uzenet_pass), val);
	
		}else if(strcmp(key, "Baud") == 0){
			st->baud_rate = (auint)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SleepMode") == 0){
			st->sleep_mode = (sint32)strtol(val, NULL, 10);
		}else if(strcmp(key, "Vdd33") == 0){
			st->vdd33 = (uint16)strtoul(val, NULL, 10);
	
		}else if(strcmp(key, "SntpEnabled") == 0){
			st->sntp_enabled = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "SntpTimeZone") == 0 || strcmp(key, "SntpTimezone") == 0){
			st->sntp_timezone = (sint32)strtol(val, NULL, 10);
		}else if(strcmp(key, "SntpServer0") == 0){
			cu_esp_s8cpy(st->sntp_server[0], sizeof(st->sntp_server[0]), val);
		}else if(strcmp(key, "SntpServer1") == 0){
			cu_esp_s8cpy(st->sntp_server[1], sizeof(st->sntp_server[1]), val);
		}else if(strcmp(key, "SntpServer2") == 0){
			cu_esp_s8cpy(st->sntp_server[2], sizeof(st->sntp_server[2]), val);
	
		}else if(strcmp(key, "DnsEnable") == 0){
			st->dns_enable = (uint8)atoi(val);
		}else if(strcmp(key, "DnsServer0") == 0){
			cu_esp_s8cpy(st->dns_server[0], sizeof(st->dns_server[0]), val);
		}else if(strcmp(key, "DnsServer1") == 0){
			cu_esp_s8cpy(st->dns_server[1], sizeof(st->dns_server[1]), val);
		}else if(strcmp(key, "DnsServer2") == 0){
			cu_esp_s8cpy(st->dns_server[2], sizeof(st->dns_server[2]), val);
	
		}else if(strcmp(key, "MdnsEnable") == 0){
			st->mdns_enable = (uint8)atoi(val);
		}else if(strcmp(key, "MdnsPort") == 0){
			st->mdns_port = (uint16)strtoul(val, NULL, 10);
		}else if(strcmp(key, "MdnsHost") == 0){
			cu_esp_s8cpy(st->mdns_host, sizeof(st->mdns_host), val);
		}else if(strcmp(key, "MdnsService") == 0){
			cu_esp_s8cpy(st->mdns_service, sizeof(st->mdns_service), val);
	
		}else if(strcmp(key, "SslRxBufSize") == 0){
			st->ssl_rx_buf_size = (uint16)strtoul(val, NULL, 10);
		}else if(strncmp(key, "SslAuthMode", 11) == 0){
			int idx = atoi(key + 11);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			st->ssl_auth_mode[idx] = (uint8)atoi(val);
		}else if(strncmp(key, "SslPkiNum", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			st->ssl_pki_num[idx] = (uint8)atoi(val);
		}else if(strncmp(key, "SslCaNum", 8) == 0){
			int idx = atoi(key + 8);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			st->ssl_ca_num[idx] = (uint8)atoi(val);
		}else if(strncmp(key, "SslKeepAlive", 12) == 0){
			int idx = atoi(key + 12);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			st->ssl_keep_alive[idx] = (uint16)strtoul(val, NULL, 10);
		}else if(strncmp(key, "SslSni", 6) == 0){
			int idx = atoi(key + 6);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_sni[idx], sizeof(st->ssl_sni[idx]), val);
		}else if(strncmp(key, "SslCommonName", 13) == 0){
			int idx = atoi(key + 13);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_common_name[idx], sizeof(st->ssl_common_name[idx]), val);
		}else if(strncmp(key, "SslAlpnCount", 12) == 0){
			int idx = atoi(key + 12);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			st->ssl_alpn_count[idx] = (uint8)atoi(val);
		}else if(strncmp(key, "SslAlpn0_", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_alpn0[idx], sizeof(st->ssl_alpn0[idx]), val);
		}else if(strncmp(key, "SslAlpn1_", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_alpn1[idx], sizeof(st->ssl_alpn1[idx]), val);
		}else if(strncmp(key, "SslAlpn2_", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_alpn2[idx], sizeof(st->ssl_alpn2[idx]), val);
		}else if(strncmp(key, "SslAlpn3_", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_alpn3[idx], sizeof(st->ssl_alpn3[idx]), val);
		}else if(strncmp(key, "SslAlpn4_", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_alpn4[idx], sizeof(st->ssl_alpn4[idx]), val);
		}else if(strncmp(key, "SslPskId", 8) == 0){
			int idx = atoi(key + 8);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS){
			cu_esp_s8cpy(st->ssl_psk_id[idx], sizeof(st->ssl_psk_id[idx]), val);
				size_t n = strlen(val); if(n > sizeof(st->ssl_psk_bin[idx])) n = sizeof(st->ssl_psk_bin[idx]);
				memset(st->ssl_psk_bin[idx], 0, sizeof(st->ssl_psk_bin[idx]));
				memcpy(st->ssl_psk_bin[idx], val, n); st->ssl_psk_bin_len[idx] = (uint8)n;
			}
		}else if(strncmp(key, "SslPskKey", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_MAX_LINKS)
			cu_esp_s8cpy(st->ssl_psk_key[idx], sizeof(st->ssl_psk_key[idx]), val);
		}else if(strncmp(key, "SslCaPath", 9) == 0){
			int idx = atoi(key + 9);
			if(idx >= 0 && idx < (int)ESP_SSL_SLOT_COUNT)
			cu_esp_s8cpy(st->ssl_ca_path[idx], sizeof(st->ssl_ca_path[idx]), val);
		}else if(strncmp(key, "SslPkiCertPath", 14) == 0){
			int idx = atoi(key + 14);
			if(idx >= 0 && idx < (int)ESP_SSL_SLOT_COUNT)
			cu_esp_s8cpy(st->ssl_pki_cert_path[idx], sizeof(st->ssl_pki_cert_path[idx]), val);
		}else if(strncmp(key, "SslPkiKeyPath", 13) == 0){
			int idx = atoi(key + 13);
			if(idx >= 0 && idx < (int)ESP_SSL_SLOT_COUNT)
			cu_esp_s8cpy(st->ssl_pki_key_path[idx], sizeof(st->ssl_pki_key_path[idx]), val);
		}else if(strcmp(key, "ServerMaxConn") == 0){
			st->server_max_conn = (uint8)atoi(val);
		}else if(strcmp(key, "CipCheckSeq") == 0){
			st->cipcheckseq = (uint8)atoi(val);
		}else if(strcmp(key, "CipBufRecvMode") == 0){
			st->cipbuf_recv_mode = (uint8)atoi(val);
		}else if(strcmp(key, "CipDInfo") == 0){
			if((auint)atoi(val))
			st->state |= ESP_CIPDINFO;
			else
			st->state &= ~ESP_CIPDINFO;
		}else if(strcmp(key, "SysMsgFlags") == 0){
			st->sysmsg_flags = (uint8)atoi(val);
	
		}else if(strcmp(key, "LanEnable") == 0){
			st->lan.enable = (uint8)atoi(val);
		}else if(strcmp(key, "LanDhcpAp") == 0){
			st->lan.dhcp_enable_ap = (uint8)atoi(val);
		}else if(strcmp(key, "LanDhcpSta") == 0){
			st->lan.dhcp_enable_sta = (uint8)atoi(val);
		}else if(strcmp(key, "LanLocalMac") == 0){
			if(cu_esp_parse_mac6(val, tmp_mac6))
			memcpy(st->lan.local_mac, tmp_mac6, 6u);
		}else if(strcmp(key, "LanDhcpStartIp") == 0){
			st->lan.pool_start_be = cu_esp_ipv4_to_be(val, st->lan.pool_start_be);
		}else if(strcmp(key, "LanDhcpEndIp") == 0){
			st->lan.pool_end_be = cu_esp_ipv4_to_be(val, st->lan.pool_end_be);
	
		}else if(strcmp(key, "UartLogging") == 0){
			st->uart_logging = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "UartLoggingFile") == 0){
			cu_esp_s8cpy(st->uart_logging_fname, sizeof(st->uart_logging_fname), val);
		}else if(strcmp(key, "UartPlayback") == 0){
			st->uart_playback = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "UartPlaybackFile") == 0){
			cu_esp_s8cpy(st->uart_playback_fname, sizeof(st->uart_playback_fname), val);
		}else if(strcmp(key, "HostSerialBypass") == 0){
			st->host_serial_bypass = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "HostSerialDevice") == 0){
			cu_esp_s8cpy(st->host_serial_device_name, sizeof(st->host_serial_device_name), val);
		}else if(strcmp(key, "HostMidiBypass") == 0){
			st->host_midi_bypass = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "HostMidiPort") == 0){
			cu_esp_s8cpy(st->host_midi_port_name, sizeof(st->host_midi_port_name), val);
		}else if(strcmp(key, "VirtualMidiMode") == 0){
			st->virtual_midi_mode = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "VirtualMidiPort") == 0){
			cu_esp_s8cpy(st->virtual_midi_port_name, sizeof(st->virtual_midi_port_name), val);
		}else if(strcmp(key, "TcpSerialHost") == 0){
			cu_esp_s8cpy(st->tcp_serial_host, sizeof(st->tcp_serial_host), val);
		}else if(strcmp(key, "TcpSerialPort") == 0){
			st->tcp_serial_port = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "TcpSerialAutoReconnect") == 0){
			st->tcp_serial_auto_reconnect = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "TcpSerialMode") == 0){
			st->tcp_serial_mode = (uint32)strtoul(val, NULL, 10);
		}else if(strcmp(key, "LinkRoom") == 0){
			cu_esp_link_set_room(val);
		}else if(strcmp(key, "LinkRelayHost") == 0){
			cu_esp_link_set_relay_host(val);
		}else if(strcmp(key, "LinkRelayPort") == 0){
			cu_esp_link_set_relay_port((auint)strtoul(val, NULL, 10));
		}else if(strncmp(key, "LinkImpair", 10) == 0){
			cu_esp_link_impair_t im;
			uint32 v = (uint32)strtoul(val, NULL, 10);
			cu_esp_link_get_impair(&im);
			if(strcmp(key, "LinkImpairEnable") == 0) im.enabled = v;
			else if(strcmp(key, "LinkImpairLatencyMs") == 0) im.latency_ms = v;
			else if(strcmp(key, "LinkImpairJitterMs") == 0) im.jitter_ms = v;
			else if(strcmp(key, "LinkImpairStallEveryMs") == 0) im.stall_every_ms = v;
			else if(strcmp(key, "LinkImpairStallMs") == 0) im.stall_ms = v;
			else if(strcmp(key, "LinkImpairNoisePpm") == 0) im.noise_ppm = v;
			else if(strcmp(key, "LinkImpairDropPpm") == 0) im.drop_ppm = v;
			cu_esp_link_set_impair(&im);
		}
	else
		return FALSE;

	return TRUE;
}

static boole cu_esp_config_line_is_esp(const char *line)
{
	cu_state_esp_t tmp;
	char key[128];
	char val[384];

	if(!line)
		return FALSE;
	if(!cu_esp_cfg_parse_kv(line, key, sizeof(key), val, sizeof(val)))
		return FALSE;
	memset(&tmp, 0, sizeof(tmp));
	return cu_esp_apply_cfg_kv(&tmp, key, val);
}

static void cu_esp_config_normalize(cu_state_esp_t *st, boole verbose)
{
	uint8 tmp_mac6[6];

	if(!st){
		return;
	}

	/* normalize/clamp */
	if(st->serial_esp_model < 1u || st->serial_esp_model > 3u){
			if(st->emulation_model >= 1u && st->emulation_model <= 3u)
				st->serial_esp_model = st->emulation_model;
			else
				st->serial_esp_model = 1u;
		}
		if(st->uart_profile > CU_UART_PROFILE_DEBUG)
			st->uart_profile = CU_UART_PROFILE_FAST;
		if(st->serial_route > CU_ESP_SERIAL_LOOPBACK){
			if(st->host_midi_bypass)
				st->serial_route = CU_ESP_SERIAL_HOST_MIDI;
			else if(st->host_serial_bypass)
				st->serial_route = CU_ESP_SERIAL_HOST_SERIAL;
			else if(st->emulation_model != 0u)
				st->serial_route = CU_ESP_SERIAL_ESP_MODULE;
			else
				st->serial_route = CU_ESP_SERIAL_DISCONNECTED;
		}
		switch(st->serial_route){
			case CU_ESP_SERIAL_ESP_MODULE:
				st->emulation_model = st->serial_esp_model;
				st->host_serial_bypass = 0u;
				st->host_midi_bypass = 0u;
				break;
			case CU_ESP_SERIAL_HOST_SERIAL:
				st->emulation_model = 0u;
				st->host_serial_bypass = 1u;
				st->host_midi_bypass = 0u;
				break;
			case CU_ESP_SERIAL_HOST_MIDI:
				st->emulation_model = 0u;
				st->host_serial_bypass = 0u;
				st->host_midi_bypass = 1u;
				break;
			case CU_ESP_SERIAL_VIRTUAL_MIDI:
				st->emulation_model = 0u;
				st->host_serial_bypass = 0u;
				st->host_midi_bypass = 0u;
				break;
			case CU_ESP_SERIAL_TCP_SERIAL:
			case CU_ESP_SERIAL_LOOPBACK:
				st->emulation_model = 0u;
				st->host_serial_bypass = 0u;
				st->host_midi_bypass = 0u;
				break;
			default:
				st->serial_route = CU_ESP_SERIAL_DISCONNECTED;
				st->emulation_model = 0u;
				st->host_serial_bypass = 0u;
				st->host_midi_bypass = 0u;
				break;
		}
		if((st->virtual_midi_mode < CU_ESP_VIRTUAL_MIDI_INSTRUMENT) ||
		   (st->virtual_midi_mode > CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL))
			st->virtual_midi_mode = CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL;
		if(st->virtual_midi_port_name[0] == '\0')
			cu_esp_s8cpy(st->virtual_midi_port_name, sizeof(st->virtual_midi_port_name), "CUzeBox MIDI");
		if(st->tcp_serial_host[0] == '\0')
			cu_esp_s8cpy(st->tcp_serial_host, sizeof(st->tcp_serial_host), "127.0.0.1");
		if(st->tcp_serial_mode > CU_ESP_TCP_SERIAL_MODE_LAN)
			st->tcp_serial_mode = CU_ESP_TCP_SERIAL_MODE_CLIENT;
		if(st->tcp_serial_port == 0u)
			st->tcp_serial_port = 12001u;
		if(st->tcp_serial_auto_reconnect > 1u)
			st->tcp_serial_auto_reconnect = 1u;
		if(st->tcp_serial_retry_interval_ms == 0u)
			st->tcp_serial_retry_interval_ms = 1000u;
		if(st->server_max_conn == 0u || st->server_max_conn > ESP_MAX_LINKS)
			st->server_max_conn = ESP_MAX_LINKS;
		if(st->at_firmware_profile > CU_ESP_AT_FW_ESPAT_230) st->at_firmware_profile = CU_ESP_AT_FW_ESPAT_230;
		if(st->sysstore_mode > 1u) st->sysstore_mode = 1u;
		if(st->ipv6_enabled > 1u) st->ipv6_enabled = 0u;
		if(st->wifi_ap_proto != 1u && st->wifi_ap_proto != 3u && st->wifi_ap_proto != 7u) st->wifi_ap_proto = 3u;
		if(st->wifi_sta_proto != 1u && st->wifi_sta_proto != 3u && st->wifi_sta_proto != 7u) st->wifi_sta_proto = 3u;
		if(st->station_hostname[0] == '\0') cu_esp_s8cpy(st->station_hostname, sizeof(st->station_hostname), "cuzebox");
	
		if(st->country_code[0] == '\0')
			cu_esp_s8cpy(st->country_code, sizeof(st->country_code), ESP_DEFAULT_COUNTRY_CODE);
	
		if(st->country_start_ch == 0u)
			st->country_start_ch = ESP_DEFAULT_COUNTRY_START_CH;
		if(st->country_count == 0u)
			st->country_count = ESP_DEFAULT_COUNTRY_COUNT;
	
		if(st->sntp_server[0][0] == '\0')
			cu_esp_s8cpy(st->sntp_server[0], sizeof(st->sntp_server[0]), default_sntp_server0);
		if(st->sntp_server[1][0] == '\0')
			cu_esp_s8cpy(st->sntp_server[1], sizeof(st->sntp_server[1]), default_sntp_server1);
		if(st->sntp_server[2][0] == '\0')
			cu_esp_s8cpy(st->sntp_server[2], sizeof(st->sntp_server[2]), default_sntp_server2);
	
		/* station aliases fallback */
		if(st->station_mac[0] == '\0')
			cu_esp_s8cpy(st->station_mac, sizeof(st->station_mac), (const char *)st->wifi_mac);
		if(st->station_ip[0] == '\0')
			cu_esp_s8cpy(st->station_ip, sizeof(st->station_ip), (const char *)st->wifi_ip);
		if(st->station_gateway[0] == '\0')
			cu_esp_s8cpy(st->station_gateway, sizeof(st->station_gateway), "10.0.0.1");
		if(st->station_netmask[0] == '\0')
			cu_esp_s8cpy(st->station_netmask, sizeof(st->station_netmask), "255.255.255.0");
		if(st->soft_ap_enabled > 1u)
			st->soft_ap_enabled = 1u;
		if(st->soft_ap_gateway[0] == '\0')
			cu_esp_s8cpy(st->soft_ap_gateway, sizeof(st->soft_ap_gateway), (const char *)st->soft_ap_ip);
		if(st->soft_ap_netmask[0] == '\0')
			cu_esp_s8cpy(st->soft_ap_netmask, sizeof(st->soft_ap_netmask), "255.255.255.0");
	
		/* update LAN IPv4 defaults from SoftAP IP after config load */
		st->lan.ap_ip_be = cu_esp_ipv4_to_be((const char *)st->soft_ap_ip, st->lan.ap_ip_be);
		st->lan.netmask_be = htonl(0xFFFFFF00u);
		st->lan.dns_ip_be = st->lan.ap_ip_be;
		if(!cu_esp_parse_mac6((const char *)st->soft_ap_mac, tmp_mac6)){
			/* keep existing lan.local_mac from factory/config */
		}else{
			/* if no explicit LanLocalMac was set, factory already matches softap; leave as-is */
			(void)tmp_mac6;
		}
	
		/* don't allow logging + playback file at the same time (except host serial logging mode) */
		if(st->uart_logging && st->uart_logging < 3u && st->uart_playback){
			st->uart_logging = 0u;
			if(verbose){
				print_message("***UART Logging disabled due to UART file playback[%s]\n", st->uart_logging_fname);
			}
		}
}

static boole cu_esp_load_config_state_from_path(cu_state_esp_t *st, const char *path, boole verbose, boole *found_any)
{
	FILE *f;
	char buf[512];
	char key[128];
	char val[384];

	if(found_any)
		*found_any = FALSE;

	if(!st || !path)
		return FALSE;

	f = fopen(path, "r");
	if(f == NULL)
		return FALSE;

	while(fgets(buf, sizeof(buf), f)){
		cu_esp_trim(buf);
		if(!cu_esp_cfg_parse_kv(buf, key, sizeof(key), val, sizeof(val)))
			continue;
		if(cu_esp_apply_cfg_kv(st, key, val)){
			if(found_any)
				*found_any = TRUE;
		}
	}

	fclose(f);
	cu_esp_config_normalize(st, verbose);
	return TRUE;
}

static boole cu_esp_write_config_stream(FILE *f, cu_state_esp_t const *st, boole with_comments)
{
	char lan_mac[32];

	if(f == NULL || st == NULL)
		return FALSE;

	cu_esp_format_mac6(st->lan.local_mac, lan_mac, sizeof(lan_mac));

	if(with_comments){
		fprintf(f, "# -----------------------------------------------------------------------------\n");
		fprintf(f, "# ESP8266 / ESP32 emulation\n");
		fprintf(f, "# -----------------------------------------------------------------------------\n");
		fprintf(f, "# This replaces the older standalone esp.cfg file.\n");
		fprintf(f, "# String values stay quoted for compatibility with the ESP config loader.\n");
		fprintf(f, "# SerialRoute: 0=disconnected, 1=ESP module, 2=host serial, 3=host MIDI, 4=virtual MIDI device, 5=TCP serial, 6=loopback\n");
		fprintf(f, "# SerialEspModel: 1=ESP8266, 2=ESP32, 3=ESP32-ETH01\n");
		fprintf(f, "# UartProfile: 0=FAST, 1=BALANCED, 2=DEBUG\n");
		fprintf(f, "# EspEmulationModel: 0=disabled, 1=ESP8266, 2=ESP32, 3=ESP32-ETH01\n\n");
	}

	fprintf(f, "SerialRoute=\"%u\"\n", (unsigned int)st->serial_route);
	fprintf(f, "SerialEspModel=\"%u\"\n", (unsigned int)st->serial_esp_model);
	fprintf(f, "UartProfile=\"%u\"\n", (unsigned int)st->uart_profile);
	fprintf(f, "EspEmulationModel=\"%d\"\n", (int)st->emulation_model);
	fprintf(f, "EspAtFirmwareProfile=\"%u\"\n", (unsigned int)st->at_firmware_profile);
	fprintf(f, "EspAtSysStore=\"%u\"\n", (unsigned int)st->sysstore_mode);
	fprintf(f, "EspAtIpv6=\"%u\"\n", (unsigned int)st->ipv6_enabled);
	fprintf(f, "EspAtStationHostname=\"%s\"\n", st->station_hostname);
	fprintf(f, "\n# SoftAP\n");
	fprintf(f, "SoftApName=\"%s\"\n", st->soft_ap_name);
	fprintf(f, "SoftApPass=\"%s\"\n", st->soft_ap_pass);
	fprintf(f, "SoftApMac=\"%s\"\n", st->soft_ap_mac);
	fprintf(f, "SoftApIp=\"%s\"\n", st->soft_ap_ip);
	fprintf(f, "SoftApGateway=\"%s\"\n", st->soft_ap_gateway);
	fprintf(f, "SoftApNetmask=\"%s\"\n", st->soft_ap_netmask);
	fprintf(f, "SoftApChannel=\"%u\"\n", (unsigned int)st->soft_ap_channel);
	fprintf(f, "SoftApEncryption=\"%u\"\n", (unsigned int)st->soft_ap_encryption);
	fprintf(f, "SoftApMaxConn=\"%u\"\n", (unsigned int)st->soft_ap_max_conn);
	fprintf(f, "SoftApHidden=\"%u\"\n", (unsigned int)st->soft_ap_hidden);
	fprintf(f, "SoftApDhcpLeaseMin=\"%u\"\n", (unsigned int)st->soft_ap_dhcp_lease_min);

	fprintf(f, "\n# Station / WiFi\n");
	fprintf(f, "WifiName=\"%s\"\n", st->wifi_name);
	fprintf(f, "WifiPass=\"%s\"\n", st->wifi_pass);
	fprintf(f, "WifiMac=\"%s\"\n", st->wifi_mac);
	fprintf(f, "WifiIp=\"%s\"\n", st->wifi_ip);
	fprintf(f, "WifiChannel=\"%d\"\n", (int)st->wifi_channel);
	fprintf(f, "WifiMode=\"%d\"\n", (int)st->wifi_mode);
	fprintf(f, "WifiEnc=\"%d\"\n", (int)st->wifi_enc);
	fprintf(f, "WifiRssi=\"%d\"\n", (int)st->wifi_rssi);
	fprintf(f, "WifiPciEn=\"%d\"\n", (int)st->wifi_pci_en);
	fprintf(f, "WifiReconnInterval=\"%d\"\n", (int)st->wifi_reconn_interval);
	fprintf(f, "WifiListenInterval=\"%d\"\n", (int)st->wifi_listen_interval);
	fprintf(f, "WifiScanMode=\"%d\"\n", (int)st->wifi_scan_mode);
	fprintf(f, "WifiJapTimeout=\"%d\"\n", (int)st->wifi_jap_timeout);
	fprintf(f, "WifiPmf=\"%d\"\n", (int)st->wifi_pmf);
	fprintf(f, "AutoConnect=\"%u\"\n", (unsigned int)((st->state & ESP_AUTOCONNECT) ? 1u : 0u));
	fprintf(f, "StationMac=\"%s\"\n", st->station_mac);
	fprintf(f, "StationIp=\"%s\"\n", st->station_ip);
	fprintf(f, "StationGateway=\"%s\"\n", st->station_gateway);
	fprintf(f, "StationNetmask=\"%s\"\n", st->station_netmask);

	fprintf(f, "\n# Country\n");
	fprintf(f, "CountryPolicy=\"%u\"\n", (unsigned int)st->country_policy);
	fprintf(f, "CountryCode=\"%s\"\n", st->country_code);
	fprintf(f, "CountryStartCh=\"%u\"\n", (unsigned int)st->country_start_ch);
	fprintf(f, "CountryCount=\"%u\"\n", (unsigned int)st->country_count);

	fprintf(f, "\n# Other interfaces\n");
	fprintf(f, "EthernetMac=\"%s\"\n", st->ethernet_mac);
	fprintf(f, "EthernetIp=\"%s\"\n", st->ethernet_ip);
	fprintf(f, "BluetoothMac=\"%s\"\n", st->bluetooth_mac);
	fprintf(f, "BluetoothIp=\"%s\"\n", st->bluetooth_ip);
	fprintf(f, "UzenetPass=\"%s\"\n", st->uzenet_pass);
	fprintf(f, "Baud=\"%d\"\n", (int)st->baud_rate);
	fprintf(f, "SleepMode=\"%d\"\n", (int)st->sleep_mode);
	fprintf(f, "Vdd33=\"%u\"\n", (unsigned int)st->vdd33);
	fprintf(f, "EspAtTranslinkEnabled=\"%u\"\n", (unsigned int)st->translink_enabled);
	fprintf(f, "EspAtTranslinkKind=\"%u\"\n", (unsigned int)st->translink_kind);
	fprintf(f, "EspAtTranslinkHost=\"%s\"\n", st->translink_host);
	fprintf(f, "EspAtTranslinkPort=\"%u\"\n", (unsigned int)st->translink_port);
	fprintf(f, "EspAtTranslinkLocalPort=\"%u\"\n", (unsigned int)st->translink_local_port);
	fprintf(f, "EspAtTranslinkKeepAlive=\"%u\"\n", (unsigned int)st->translink_keepalive);
	fprintf(f, "EspAtTranslinkUdpMode=\"%u\"\n", (unsigned int)st->translink_udp_mode);

	fprintf(f, "\n# SNTP\n");
	fprintf(f, "SntpEnabled=\"%u\"\n", (unsigned int)st->sntp_enabled);
	fprintf(f, "SntpTimeZone=\"%d\"\n", (int)st->sntp_timezone);
	fprintf(f, "SntpServer0=\"%s\"\n", st->sntp_server[0]);
	fprintf(f, "SntpServer1=\"%s\"\n", st->sntp_server[1]);
	fprintf(f, "SntpServer2=\"%s\"\n", st->sntp_server[2]);

	fprintf(f, "\n# DNS / mDNS / SSL / TCP\n");
	fprintf(f, "DnsEnable=\"%u\"\n", (unsigned int)st->dns_enable);
	fprintf(f, "DnsServer0=\"%s\"\n", st->dns_server[0]);
	fprintf(f, "DnsServer1=\"%s\"\n", st->dns_server[1]);
	fprintf(f, "DnsServer2=\"%s\"\n", st->dns_server[2]);
	fprintf(f, "MdnsEnable=\"%u\"\n", (unsigned int)st->mdns_enable);
	fprintf(f, "MdnsPort=\"%u\"\n", (unsigned int)st->mdns_port);
	fprintf(f, "MdnsHost=\"%s\"\n", st->mdns_host);
	fprintf(f, "MdnsService=\"%s\"\n", st->mdns_service);
	fprintf(f, "SslRxBufSize=\"%u\"\n", (unsigned int)st->ssl_rx_buf_size);
	for(uint32 i = 0u; i < ESP_MAX_LINKS; i++){
		fprintf(f, "SslAuthMode%u=\"%u\"\n", (unsigned int)i, (unsigned int)st->ssl_auth_mode[i]);
		fprintf(f, "SslPkiNum%u=\"%u\"\n", (unsigned int)i, (unsigned int)st->ssl_pki_num[i]);
		fprintf(f, "SslCaNum%u=\"%u\"\n", (unsigned int)i, (unsigned int)st->ssl_ca_num[i]);
		fprintf(f, "SslKeepAlive%u=\"%u\"\n", (unsigned int)i, (unsigned int)st->ssl_keep_alive[i]);
		fprintf(f, "SslSni%u=\"%s\"\n", (unsigned int)i, st->ssl_sni[i]);
		fprintf(f, "SslCommonName%u=\"%s\"\n", (unsigned int)i, st->ssl_common_name[i]);
		fprintf(f, "SslAlpnCount%u=\"%u\"\n", (unsigned int)i, (unsigned int)st->ssl_alpn_count[i]);
		fprintf(f, "SslAlpn0_%u=\"%s\"\n", (unsigned int)i, st->ssl_alpn0[i]);
		fprintf(f, "SslAlpn1_%u=\"%s\"\n", (unsigned int)i, st->ssl_alpn1[i]);
		fprintf(f, "SslAlpn2_%u=\"%s\"\n", (unsigned int)i, st->ssl_alpn2[i]);
		fprintf(f, "SslAlpn3_%u=\"%s\"\n", (unsigned int)i, st->ssl_alpn3[i]);
		fprintf(f, "SslAlpn4_%u=\"%s\"\n", (unsigned int)i, st->ssl_alpn4[i]);
		fprintf(f, "SslPskId%u=\"%s\"\n", (unsigned int)i, st->ssl_psk_id[i]);
		fprintf(f, "SslPskKey%u=\"%s\"\n", (unsigned int)i, st->ssl_psk_key[i]);
	}
	for(uint32 i = 0u; i < ESP_SSL_SLOT_COUNT; i++){
		fprintf(f, "SslCaPath%u=\"%s\"\n", (unsigned int)i, st->ssl_ca_path[i]);
		fprintf(f, "SslPkiCertPath%u=\"%s\"\n", (unsigned int)i, st->ssl_pki_cert_path[i]);
		fprintf(f, "SslPkiKeyPath%u=\"%s\"\n", (unsigned int)i, st->ssl_pki_key_path[i]);
	}
	fprintf(f, "ServerMaxConn=\"%u\"\n", (unsigned int)st->server_max_conn);
	fprintf(f, "CipCheckSeq=\"%u\"\n", (unsigned int)st->cipcheckseq);
	fprintf(f, "CipBufRecvMode=\"%u\"\n", (unsigned int)st->cipbuf_recv_mode);
	fprintf(f, "CipDInfo=\"%u\"\n", (unsigned int)((st->state & ESP_CIPDINFO) ? 1u : 0u));
	fprintf(f, "SysMsgFlags=\"%u\"\n", (unsigned int)st->sysmsg_flags);

	fprintf(f, "\n# Virtual LAN overlay\n");
	fprintf(f, "LanEnable=\"%u\"\n", (unsigned int)st->lan.enable);
	fprintf(f, "LanDhcpAp=\"%u\"\n", (unsigned int)st->lan.dhcp_enable_ap);
	fprintf(f, "LanDhcpSta=\"%u\"\n", (unsigned int)st->lan.dhcp_enable_sta);
	fprintf(f, "LanLocalMac=\"%s\"\n", lan_mac);
	{
		char lan_pool_start[24];
		char lan_pool_end[24];
		cu_esp_ipv4_be_to_str(st->lan.pool_start_be, lan_pool_start, sizeof(lan_pool_start));
		cu_esp_ipv4_be_to_str(st->lan.pool_end_be, lan_pool_end, sizeof(lan_pool_end));
		fprintf(f, "LanDhcpStartIp=\"%s\"\n", lan_pool_start);
		fprintf(f, "LanDhcpEndIp=\"%s\"\n", lan_pool_end);
	}

	fprintf(f, "\n# Uart logging debug (0=off, 1=text file, 2=binary file, 3=host serial)\n");
	fprintf(f, "UartLogging=\"%u\"\n", (unsigned int)st->uart_logging);
	fprintf(f, "UartLoggingFile=\"%s\"\n", st->uart_logging_fname);

	fprintf(f, "\n# Uart playback (0=off, 1=on)\n");
	fprintf(f, "UartPlayback=\"%u\"\n", (unsigned int)st->uart_playback);
	fprintf(f, "UartPlaybackFile=\"%s\"\n", st->uart_playback_fname);

	fprintf(f, "\n# Allows Tx/Rx UART data between Uzebox and a host serial device\n");
	fprintf(f, "HostSerialBypass=\"%u\"\n", (unsigned int)st->host_serial_bypass);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	fprintf(f, "# Windows example: COM3\n");
#else
	fprintf(f, "# Linux example: /dev/ttyUSB0\n");
#endif
	fprintf(f, "HostSerialDevice=\"%s\"\n", st->host_serial_device_name);
	fprintf(f, "\n# Host MIDI connection to existing endpoints\n");
	fprintf(f, "HostMidiBypass=\"%u\"\n", (unsigned int)st->host_midi_bypass);
	fprintf(f, "HostMidiPort=\"%s\"\n", st->host_midi_port_name);
	fprintf(f, "\n# Virtual MIDI device exported by CUzeBox\n");
	fprintf(f, "# VirtualMidiMode: 1=instrument (receive), 2=controller (send), 3=bidirectional\n");
	fprintf(f, "VirtualMidiMode=\"%u\"\n", (unsigned int)st->virtual_midi_mode);
	fprintf(f, "VirtualMidiPort=\"%s\"\n", st->virtual_midi_port_name);
	fprintf(f, "\n# TCP serial bridge\n");
	fprintf(f, "# TcpSerialMode: 0=client, 1=server, 2=auto (connect if a peer is listening, otherwise listen),\n");
	fprintf(f, "#                3=internet (meet in LinkRoom on the uzenet relay), 4=lan (find another CUzeBox on the LAN)\n");
	fprintf(f, "TcpSerialHost=\"%s\"\n", st->tcp_serial_host);
	fprintf(f, "TcpSerialPort=\"%u\"\n", (unsigned int)st->tcp_serial_port);
	fprintf(f, "TcpSerialAutoReconnect=\"%u\"\n", (unsigned int)st->tcp_serial_auto_reconnect);
	fprintf(f, "TcpSerialMode=\"%u\"\n", (unsigned int)st->tcp_serial_mode);
	{
		cu_esp_link_impair_t im;
		cu_esp_link_get_impair(&im);
		fprintf(f, "# Internet link: both players use the same room code (A-Z, 0-9, up to 8)\n");
		fprintf(f, "LinkRoom=\"%s\"\n", cu_esp_link_get_room());
		fprintf(f, "LinkRelayHost=\"%s\"\n", cu_esp_link_get_relay_host());
		fprintf(f, "LinkRelayPort=\"%u\"\n", (unsigned int)cu_esp_link_get_relay_port());
		fprintf(f, "# Bad-connection simulator, applied to bytes received from the link\n");
		fprintf(f, "LinkImpairEnable=\"%u\"\n", (unsigned int)im.enabled);
		fprintf(f, "LinkImpairLatencyMs=\"%u\"\n", (unsigned int)im.latency_ms);
		fprintf(f, "LinkImpairJitterMs=\"%u\"\n", (unsigned int)im.jitter_ms);
		fprintf(f, "LinkImpairStallEveryMs=\"%u\"\n", (unsigned int)im.stall_every_ms);
		fprintf(f, "LinkImpairStallMs=\"%u\"\n", (unsigned int)im.stall_ms);
		fprintf(f, "LinkImpairNoisePpm=\"%u\"\n", (unsigned int)im.noise_ppm);
		fprintf(f, "LinkImpairDropPpm=\"%u\"\n", (unsigned int)im.drop_ppm);
	}
	fprintf(f, "\n");

	return TRUE;
}

boole cu_esp_config_live_ready(void)
{
	return cu_esp_config_loaded;
}

boole cu_esp_append_live_config(FILE *f, boole with_comments)
{
	return cu_esp_write_config_stream(f, &esp_state, with_comments);
}

boole cu_esp_append_default_config(FILE *f, boole with_comments)
{
	cu_state_esp_t tmp;
	cu_esp_factory_defaults_state(&tmp);
	cu_esp_config_normalize(&tmp, FALSE);
	return cu_esp_write_config_stream(f, &tmp, with_comments);
}

boole cu_esp_append_config_from_path(FILE *f, const char *path, boole with_comments)
{
	cu_state_esp_t tmp;
	boole found_any = FALSE;

	cu_esp_factory_defaults_state(&tmp);
	if(!cu_esp_load_config_state_from_path(&tmp, path, FALSE, &found_any) || !found_any)
		return FALSE;

	return cu_esp_write_config_stream(f, &tmp, with_comments);
}

auint cu_esp_reload_config_runtime(void)
{
	cu_esp_runtime_shutdown();
	return cu_esp_load_config();
}

void cu_esp_save_config(void)
{
	FILE *fin = NULL;
	FILE *fout = NULL;
	char tmp_path[512];
	char line[512];
	boole copied_old = FALSE;

	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cu_esp_cfg_path);
	fout = fopen(tmp_path, "w");
	if(fout == NULL){
		print_error("ESP ERROR: Can't open %s to save settings\n", cu_esp_cfg_path);
		return;
	}

	fin = fopen(cu_esp_cfg_path, "r");
	if(fin != NULL){
		while(fgets(line, sizeof(line), fin) != NULL){
			if(strstr(line, "# ESP8266 / ESP32 emulation") != NULL)
				break;
			if(cu_esp_config_line_is_esp(line))
				break;
			fputs(line, fout);
			copied_old = TRUE;
		}
		fclose(fin);
	}

	if(copied_old)
		fputc('\n', fout);

	if(!cu_esp_write_config_stream(fout, &esp_state, TRUE) || ferror(fout)){
		fclose(fout);
		remove(tmp_path);
		print_error("ESP ERROR: Failed to serialize ESP settings to %s\n", cu_esp_cfg_path);
		return;
	}

	if(fclose(fout) != 0){
		remove(tmp_path);
		print_error("ESP ERROR: Failed to flush %s\n", cu_esp_cfg_path);
		return;
	}
	if(configfile_replace(tmp_path, cu_esp_cfg_path) != 0){
		remove(tmp_path);
		print_error("ESP ERROR: Failed to replace %s\n", cu_esp_cfg_path);
		return;
	}

	print_message("ESP: Saved Config %s\n", cu_esp_cfg_path);
	esp_state.flash_dirty = 0u;
}

auint cu_esp_load_config(void)
{
	auint ret = 0u;
	boole loaded = FALSE;
	boole found_any = FALSE;

	esp_state.flash_dirty = 0u;
	cu_esp_factory_defaults_state(&esp_state);

	loaded = cu_esp_load_config_state_from_path(&esp_state, cu_esp_cfg_path, TRUE, &found_any);
	if((!loaded) || (!found_any)){
		boole legacy_found = FALSE;
		cu_esp_factory_defaults_state(&esp_state);
		if(cu_esp_load_config_state_from_path(&esp_state, cu_esp_legacy_cfg_path, TRUE, &legacy_found) && legacy_found){
			loaded = TRUE;
			found_any = TRUE;
			print_message("ESP: Loaded legacy %s settings; next save will merge them into %s\n", cu_esp_legacy_cfg_path, cu_esp_cfg_path);
		}
	}

	if(!found_any){
		print_message("\nESP: No ESP settings found in %s, using factory defaults\n", cu_esp_cfg_path);
		cu_esp_save_config();
	}

	cu_esp_config_loaded = TRUE;

	print_message("ESP: Loaded CFG File:\n");
	{
		char emu_mod[32];
		char route_name[32];
		char lan_mac[32];

		if(esp_state.serial_esp_model == 1u)
			sprintf(emu_mod, "ESP8266");
		else if(esp_state.serial_esp_model == 2u)
			sprintf(emu_mod, "ESP32");
		else
			sprintf(emu_mod, "ESP32-ETH01");

		if(esp_state.serial_route == CU_ESP_SERIAL_ESP_MODULE)
			sprintf(route_name, "ESP module");
		else if(esp_state.serial_route == CU_ESP_SERIAL_HOST_SERIAL)
			sprintf(route_name, "host serial");
		else if(esp_state.serial_route == CU_ESP_SERIAL_HOST_MIDI)
			sprintf(route_name, "host MIDI");
		else if(esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI)
			sprintf(route_name, "virtual MIDI");
		else if(esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL)
			sprintf(route_name, "TCP serial");
		else if(esp_state.serial_route == CU_ESP_SERIAL_LOOPBACK)
			sprintf(route_name, "loopback");
		else
			sprintf(route_name, "disconnected");

		cu_esp_format_mac6(esp_state.lan.local_mac, lan_mac, sizeof(lan_mac));

		print_message("	SerialRoute[%d=%s]\n", (int)esp_state.serial_route, route_name);
		if(esp_state.serial_route == CU_ESP_SERIAL_ESP_MODULE){
			print_message("	SerialEspModel[%d=%s]\n", (int)esp_state.serial_esp_model, emu_mod);
			print_message("	UartProfile[%u]\n", (unsigned)esp_state.uart_profile);
			print_message("	EspEmulationModel[%d]\n", (int)esp_state.emulation_model);
		}else if((esp_state.serial_route == CU_ESP_SERIAL_HOST_SERIAL) ||
		         (esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL) ||
		         (esp_state.serial_route == CU_ESP_SERIAL_LOOPBACK)){
			print_message("	RawUartLink[1]\n");
			print_message("	UartProfile[%u]\n", (unsigned)esp_state.uart_profile);
			print_message("	EspSettingsIgnored[1]\n");
		}else{
			print_message("	UartProfile[%u]\n", (unsigned)esp_state.uart_profile);
		}
		print_message("	WifiName[%s]\n", esp_state.wifi_name);
		print_message("	WifiPass[%s]\n", esp_state.wifi_pass);
		print_message("	WifiMac[%s]\n", esp_state.wifi_mac);
		print_message("	WifiIp[%s]\n", esp_state.wifi_ip);
		print_message("	SoftApName[%s]\n", esp_state.soft_ap_name);
		print_message("	SoftApPass[%s]\n", esp_state.soft_ap_pass);
		print_message("	SoftApMac[%s]\n", esp_state.soft_ap_mac);
		print_message("	SoftApIp[%s]\n", esp_state.soft_ap_ip);
		print_message("	StationMac[%s]\n", esp_state.station_mac);
		print_message("	StationIp[%s]\n", esp_state.station_ip);
		print_message("	Country[%s %u..%u]\n", esp_state.country_code,
			(unsigned int)esp_state.country_start_ch,
			(unsigned int)(esp_state.country_start_ch + esp_state.country_count - 1u));
		print_message("	Baud[%d]\n", (int)esp_state.baud_rate);
		print_message("	SntpEnabled[%d]\n", (int)esp_state.sntp_enabled);
		print_message("	SntpTimeZone[%d]\n", (int)esp_state.sntp_timezone);
		print_message("	DnsEnable[%d]\n", (int)esp_state.dns_enable);
		print_message("	ServerMaxConn[%d]\n", (int)esp_state.server_max_conn);
		print_message("	LanEnable[%d]\n", (int)esp_state.lan.enable);
		print_message("	LanLocalMac[%s]\n", lan_mac);
		print_message("	UartLogging[%d]\n", (int)esp_state.uart_logging);
		print_message("	UartLoggingFile[%s]\n", esp_state.uart_logging_fname);
		print_message("	UartPlayback[%d]\n", (int)esp_state.uart_playback);
		print_message("	UartPlaybackFile[%s]\n", esp_state.uart_playback_fname);
		print_message("	HostSerialBypass[%d]\n", (int)esp_state.host_serial_bypass);
		print_message("	HostSerialDevice[%s]\n", esp_state.host_serial_device_name);
		print_message("	HostMidiBypass[%d]\n", (int)esp_state.host_midi_bypass);
		print_message("	HostMidiPort[%s]\n", esp_state.host_midi_port_name);
		print_message("	VirtualMidiMode[%u]\n", (unsigned)esp_state.virtual_midi_mode);
		print_message("	VirtualMidiPort[%s]\n", esp_state.virtual_midi_port_name);
		print_message("	TcpSerialHost[%s]\n", esp_state.tcp_serial_host);
		print_message("	TcpSerialPort[%u]\n", (unsigned)esp_state.tcp_serial_port);
		print_message("	TcpSerialAutoReconnect[%u]\n", (unsigned)esp_state.tcp_serial_auto_reconnect);
		print_message("	TcpSerialMode[%u]\n", (unsigned)esp_state.tcp_serial_mode);
	}


	/* Host serial / MIDI bypass */
	if(esp_state.serial_route == CU_ESP_SERIAL_HOST_SERIAL){
		if(cu_esp_host_serial_start() == 0){
			print_message("ESP Opened Host Serial Device [%s] for bypass\n", esp_state.host_serial_device_name);
		}
	}
	if(esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL)
		(void)cu_esp_tcp_serial_start();
	if((esp_state.serial_route == CU_ESP_SERIAL_HOST_MIDI) ||
	   (esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI))
		(void)cu_esp_serial_midi_start(esp_state.serial_route);

	/* Open logging or playback once per process reset */
	if(!esp_state.uart_logging_started && esp_state.uart_logging && esp_state.uart_logging < 3u){
		esp_state.uart_logging_started = 1u;

		if(esp_state.uart_logging == 1u)
			esp_state.uart_logging_file = fopen((char *)esp_state.uart_logging_fname, "w");
		else if(esp_state.uart_logging == 2u)
			esp_state.uart_logging_file = fopen((char *)esp_state.uart_logging_fname, "wb");

		if(esp_state.uart_logging < 3u && esp_state.uart_logging_file == NULL){
			print_error("ESP ERROR: failed to open [%s] for UART logging\n", esp_state.uart_logging_fname);
			esp_state.uart_logging = 0u;
		}else if(esp_state.uart_logging_file != NULL){
			print_message("ESP UART logging started [%d][%s]\n",
				(int)esp_state.uart_logging, esp_state.uart_logging_fname);
		}
	}else if(!esp_state.uart_playback_started && esp_state.uart_playback){
		esp_state.uart_playback_started = 1u;
		esp_state.uart_playback_file = fopen((char *)esp_state.uart_playback_fname, "rb");
		if(esp_state.uart_playback_file == NULL){
			print_error("ESP ERROR: failed to open [%s] for UART playback\n", esp_state.uart_playback_fname);
			esp_state.uart_playback = 0u;
		}else{
			print_message("ESP UART playback started [%s]\n", esp_state.uart_playback_fname);
		}
	}

	if(!esp_state.uart_logging_started && esp_state.uart_logging == 3u)
		cu_esp_host_serial_start();

	return ret;
}

#endif /* ENABLE_ESP */
