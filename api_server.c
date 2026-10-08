#include "api_server.h"

#ifdef ENABLE_API_SERVER

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include "mainui.h"
#include "configcfg.h"
#include "savestate.h"
#include "cu_ctr.h"
#include "cu_avr.h"
#include "audio.h"
#include "cu_uart.h"
#ifdef ENABLE_DEBUGGER
#include "debug_dwarf.h"
#include "debug_timing.h"
#include "debug_sd_fs_history.h"
#include "debug_sd_timing_analysis.h"
#include "cu_vfat.h"
#endif
#ifdef ENABLE_ESP
#include "cu_esp.h"
#endif
#ifdef ENABLE_NETPLAY
#include "netplay.h"
#endif

extern Uint32 SDL_GetTicks(void);

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET api_socket_t;
#define API_INVALID_SOCKET INVALID_SOCKET
#define API_CLOSESOCK closesocket
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int api_socket_t;
#define API_INVALID_SOCKET (-1)
#define API_CLOSESOCK close
#endif

#define API_RX_CAP 8192U
#define API_TX_CAP 65536U
#define API_INPUT_PORTS 2U
#define API_INPUT_QUEUE_CAP 64U
#define API_MAX_READ_LEN 256U
#define API_SCREENSHOT_READ_MAX 4096U
#define API_VIDEO_LINE_CYCLES 1820U
#define API_VIDEO_HSYNC_CYCLES 136U
#define API_VIDEO_ACTIVE_BEGIN 299U
#define API_VIDEO_ACTIVE_CYCLES 1440U
#define API_PROFILE_TOP_MAX 32U
#define API_PROFILE_SCAN_WORDS 32768U
#define API_DISASM_MAX 64U
#define API_STACK_MAX 128U
#define API_CALLSTACK_MAX 32U
#define API_MEMSNAP_SLOTS 4U
#define API_MEMSNAP_MAX 1024U
#define API_MEMSNAP_DIFF_MAX 128U
#define API_MEMSCAN_SLOTS 4U
#define API_MEMSCAN_MAX 4096U
#define API_MEMSCAN_MASK_BYTES ((API_MEMSCAN_MAX + 7U) / 8U)
#define API_MEMSCAN_RESULTS_MAX 128U
#define API_RETRY_MS 1000U
#define API_MAX_CLIENTS 16U
#define API_PORT_TRIES 10U
#define API_NO_CLIENT 0xFFFFFFFFU

#define API_SUB_FRAME  0x01U
#define API_SUB_CPU    0x02U
#define API_SUB_SERIAL 0x04U
#define API_SUB_ESP    0x08U
#define API_SUB_BREAK  0x10U
#define API_SUB_WATCH  0x20U
#define API_SUB_ALL    0x3FU

#define API_WAIT_NONE 0U
#define API_WAIT_FRAME 1U
#define API_WAIT_MEM 2U

#define API_WAIT_OP_EQ 0U
#define API_WAIT_OP_NE 1U
#define API_WAIT_OP_LT 2U
#define API_WAIT_OP_LE 3U
#define API_WAIT_OP_GT 4U
#define API_WAIT_OP_GE 5U
#define API_WAIT_OP_AND 6U

#define API_MEMSCAN_OP_EQ 0U
#define API_MEMSCAN_OP_NE 1U
#define API_MEMSCAN_OP_LT 2U
#define API_MEMSCAN_OP_LE 3U
#define API_MEMSCAN_OP_GT 4U
#define API_MEMSCAN_OP_GE 5U
#define API_MEMSCAN_OP_CHANGED 6U
#define API_MEMSCAN_OP_UNCHANGED 7U
#define API_MEMSCAN_OP_INCREASED 8U
#define API_MEMSCAN_OP_DECREASED 9U

/*
** The API server is deliberately frame-oriented rather than instruction-
** oriented. This keeps the off-path cheap: when the feature is compiled in but
** disabled at runtime, the main loop pays only a tiny top-level branch. When
** enabled, all work is contained to the once-per-loop socket tick plus explicit
** frame begin/end hooks for input injection and bounded run control.
*/

typedef struct{
	auint buttons;
	auint frames;
} api_input_step_t;

typedef struct{
	boole active;
	auint buttons;
	auint frames_remaining;
	api_input_step_t queue[API_INPUT_QUEUE_CAP];
	auint queue_pos;
	auint queue_len;
} api_input_override_t;

typedef struct{
	boole active;
	auint type;
	uint32 start_frame;
	uint32 timeout_frames;
	uint32 frame_target;
	auint region;
	auint addr;
	auint op;
	auint value;
} api_wait_state_t;

typedef struct{
	boole valid;
	auint region;
	auint addr;
	auint len;
	uint32 frame;
	uint8 data[API_MEMSNAP_MAX];
} api_memsnap_t;

typedef struct{
	boole valid;
	auint region;
	auint addr;
	auint len;
	auint remaining;
	uint32 frame;
	uint8 previous[API_MEMSCAN_MAX];
	uint8 candidates[API_MEMSCAN_MASK_BYTES];
} api_memscan_t;

typedef struct{
	api_socket_t sock;
	char rx_buf[API_RX_CAP];
	auint rx_len;
	char tx_buf[API_TX_CAP];
	auint tx_len;
	auint tx_off;
	api_wait_state_t wait;
	auint subscriptions;
	uint8* screenshot_data;
	auint screenshot_size;
	auint screenshot_width;
	auint screenshot_height;
} api_client_t;

typedef struct{
	boole enabled;
	boole wsa_started;
	boole initialized;
	auint port;
	auint bound_port;
	uint32 retry_at_ms;
	api_socket_t listen_sock;
	api_client_t clients[API_MAX_CLIENTS];
	auint active_client;
	uint32 run_frames_remaining;
	api_input_override_t input[API_INPUT_PORTS];
	api_memsnap_t memsnap[API_MEMSNAP_SLOTS];
	api_memscan_t memscan[API_MEMSCAN_SLOTS];
} api_server_state_t;

static api_server_state_t api_state;

static void api_screenshot_buffer_clear(api_client_t* client)
{
	if (client == NULL){ return; }
	if (client->screenshot_data != NULL){ free(client->screenshot_data); }
	client->screenshot_data = NULL;
	client->screenshot_size = 0U;
	client->screenshot_width = 0U;
	client->screenshot_height = 0U;
}

static void api_wait_state_clear(api_wait_state_t* wait)
{
	if (wait == NULL){ return; }
	memset(wait, 0, sizeof(*wait));
	wait->type = API_WAIT_NONE;
	wait->op = API_WAIT_OP_EQ;
}

static void api_state_init_once(void)
{
	auint i;
	if (api_state.initialized){ return; }
	memset(&api_state, 0, sizeof(api_state));
	api_state.initialized = TRUE;
	api_state.port = API_SERVER_DEFAULT_PORT;
	api_state.listen_sock = API_INVALID_SOCKET;
	api_state.active_client = API_NO_CLIENT;
	for (i = 0U; i < API_MAX_CLIENTS; ++i){
		api_state.clients[i].sock = API_INVALID_SOCKET;
		api_wait_state_clear(&api_state.clients[i].wait);
	}
}

static auint api_client_count(void)
{
	auint i;
	auint count = 0U;
	for (i = 0U; i < API_MAX_CLIENTS; ++i){
		if (api_state.clients[i].sock != API_INVALID_SOCKET){ count++; }
	}
	return count;
}

static api_client_t* api_active_client(void)
{
	if (api_state.active_client >= API_MAX_CLIENTS){ return NULL; }
	if (api_state.clients[api_state.active_client].sock == API_INVALID_SOCKET){ return NULL; }
	return &api_state.clients[api_state.active_client];
}

static void api_wait_clear_all(void)
{
	auint i;
	for (i = 0U; i < API_MAX_CLIENTS; ++i){
		api_wait_state_clear(&api_state.clients[i].wait);
	}
}

static void api_copy_str(char* dst, auint cap, char const* src)
{
	auint i = 0U;
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ dst[0] = 0; return; }
	while ((i + 1U) < cap && src[i] != 0){
		dst[i] = src[i];
		i++;
	}
	dst[i] = 0;
}


static void api_close_client(auint slot)
{
	api_client_t* client;
	boole had_client;
	if (slot >= API_MAX_CLIENTS){ return; }
	client = &api_state.clients[slot];
	had_client = (client->sock != API_INVALID_SOCKET);
	api_screenshot_buffer_clear(client);
	if (client->sock != API_INVALID_SOCKET){
		API_CLOSESOCK(client->sock);
		client->sock = API_INVALID_SOCKET;
	}
	client->rx_len = 0U;
	client->tx_len = 0U;
	client->tx_off = 0U;
	api_wait_state_clear(&client->wait);
	if (api_state.active_client == slot){ api_state.active_client = API_NO_CLIENT; }
	if (had_client){
		mainui_system_message("API CLIENT DISCONNECTED (%u ACTIVE)", (unsigned)api_client_count());
	}
}

static void api_close_all_clients(void)
{
	auint i;
	for (i = 0U; i < API_MAX_CLIENTS; ++i){ api_close_client(i); }
	api_state.active_client = API_NO_CLIENT;
}

static auint api_active_port(void)
{
	return ((api_state.listen_sock != API_INVALID_SOCKET) && (api_state.bound_port != 0U)) ? api_state.bound_port : api_state.port;
}

static void api_close_listener(void)
{
	if (api_state.listen_sock != API_INVALID_SOCKET){
		API_CLOSESOCK(api_state.listen_sock);
		api_state.listen_sock = API_INVALID_SOCKET;
	}
}

static void api_input_player_clear(auint player)
{
	if (player >= API_INPUT_PORTS){ return; }
	api_state.input[player].active = FALSE;
	api_state.input[player].buttons = 0U;
	api_state.input[player].frames_remaining = 0U;
	api_state.input[player].queue_pos = 0U;
	api_state.input[player].queue_len = 0U;
		cu_ctr_setsnes(player, 0U);
}

static void api_input_clear_all(void)
{
	auint i;
	for (i = 0U; i < API_INPUT_PORTS; ++i){
		api_input_player_clear(i);
	}
}

static void api_trim(char* s)
{
	char* e;
	if (s == NULL){ return; }
	while ((*s != 0) && isspace((unsigned char)*s)){
		memmove(s, s + 1, strlen(s));
	}
	e = s + strlen(s);
	while ((e > s) && isspace((unsigned char)e[-1])){
		--e;
	}
	*e = 0;
}

static char* api_next_token(char** psrc)
{
	char* s;
	char* t;
	if ((psrc == NULL) || (*psrc == NULL)){ return NULL; }
	s = *psrc;
	while ((*s != 0) && isspace((unsigned char)*s)){ ++s; }
	if (*s == 0){ *psrc = s; return NULL; }
	t = s;
	while ((*t != 0) && !isspace((unsigned char)*t)){ ++t; }
	if (*t != 0){ *t = 0; ++t; }
	*psrc = t;
	return s;
}

static boole api_socket_set_nonblock(api_socket_t s)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	u_long mode = 1UL;
	return (ioctlsocket(s, FIONBIO, &mode) == 0) ? TRUE : FALSE;
#else
	sint32 fl = fcntl(s, F_GETFL, 0);
	if (fl < 0){ return FALSE; }
	return (fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0) ? TRUE : FALSE;
#endif
}

static boole api_would_block(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	sint32 e = WSAGetLastError();
	return (e == WSAEWOULDBLOCK) ? TRUE : FALSE;
#else
	return ((errno == EAGAIN) || (errno == EWOULDBLOCK)) ? TRUE : FALSE;
#endif
}

static void api_json_escape_append(char* dst, auint cap, auint* pos, char const* src)
{
	char c;
	while ((src != NULL) && ((c = *src++) != 0)){
		if ((*pos + 2U) >= cap){ return; }
		switch (c){
			case '\\': case '"':
				dst[(*pos)++] = '\\';
				dst[(*pos)++] = c;
				break;
			case '\n':
				dst[(*pos)++] = '\\';
				dst[(*pos)++] = 'n';
				break;
			case '\r':
				dst[(*pos)++] = '\\';
				dst[(*pos)++] = 'r';
				break;
			case '\t':
				dst[(*pos)++] = '\\';
				dst[(*pos)++] = 't';
				break;
			default:
				if ((unsigned char)c < 0x20U){
					char tmp[8];
					snprintf(tmp, sizeof(tmp), "\\u%04X", (unsigned)((unsigned char)c));
					if ((*pos + strlen(tmp)) >= cap){ return; }
					memcpy(dst + *pos, tmp, strlen(tmp));
					*pos += (auint)strlen(tmp);
				}else{
					dst[(*pos)++] = c;
				}
				break;
		}
	}
	dst[*pos] = 0;
}

static void api_queue_json_to(api_client_t* client, char const* json)
{
	size_t len;
	auint slot;
	if ((json == NULL) || (client == NULL) || (client->sock == API_INVALID_SOCKET)){ return; }
	len = strlen(json);
	if ((client->tx_len + (auint)len + 1U) >= API_TX_CAP){
		slot = (auint)(client - api_state.clients);
		api_close_client(slot);
		return;
	}
	memcpy(client->tx_buf + client->tx_len, json, len);
	client->tx_len += (auint)len;
	client->tx_buf[client->tx_len++] = '\n';
}

static void api_queue_json(char const* json)
{
	api_queue_json_to(api_active_client(), json);
}

static void api_reply_error(char const* message);

static void api_broadcast_event(auint mask, char const* json)
{
	auint i;
	for (i = 0U; i < API_MAX_CLIENTS; ++i){
		api_client_t* c = &api_state.clients[i];
		if ((c->sock != API_INVALID_SOCKET) && ((c->subscriptions & mask) != 0U)){
			api_queue_json_to(c, json);
		}
	}
}

static int api_stricmp(char const* a, char const* b){ while(*a && *b){int ca=tolower((unsigned char)*a++),cb=tolower((unsigned char)*b++);if(ca!=cb)return ca-cb;}return (unsigned char)*a-(unsigned char)*b;}
static auint api_subscription_mask(char const* name)
{
	if (name == NULL){ return 0U; }
	if (api_stricmp(name, "ALL") == 0){ return API_SUB_ALL; }
	if (api_stricmp(name, "FRAME") == 0){ return API_SUB_FRAME; }
	if (api_stricmp(name, "CPU") == 0){ return API_SUB_CPU; }
	if (api_stricmp(name, "SERIAL") == 0){ return API_SUB_SERIAL; }
	if (api_stricmp(name, "ESP") == 0){ return API_SUB_ESP; }
	if (api_stricmp(name, "BREAK") == 0){ return API_SUB_BREAK; }
	if (api_stricmp(name, "WATCH") == 0){ return API_SUB_WATCH; }
	return 0U;
}

static void api_handle_subscribe(char* rest, boole add)
{
	api_client_t* c = api_active_client();
	char* tok;
	auint changed = 0U;
	if (c == NULL){ return; }
	while ((tok = api_next_token(&rest)) != NULL){
		auint m = api_subscription_mask(tok);
		if (m == 0U){ api_reply_error("Unknown subscription"); return; }
		if (add){ c->subscriptions |= m; } else { c->subscriptions &= ~m; }
		changed |= m;
	}
	if (changed == 0U){ api_reply_error("Subscription name required"); return; }
	{ char b[160]; snprintf(b,sizeof(b),"{\"ok\":1,\"subscriptions\":%u}",(unsigned)c->subscriptions); api_queue_json(b); }
}

static void api_wait_cancel_all(char const* reason)
{
	auint i;
	char buf[512];
	for (i = 0U; i < API_MAX_CLIENTS; ++i){
		api_client_t* client = &api_state.clients[i];
		auint pos = 0U;
		if ((client->sock == API_INVALID_SOCKET) || (!client->wait.active)){ continue; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":0,\"wait_cancelled\":1,\"error\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, (reason != NULL) ? reason : "WAIT cancelled");
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
		api_queue_json_to(client, buf);
		api_wait_state_clear(&client->wait);
	}
}

static void api_reply_ok_simple(char const* extra)
{
	char buf[512];
	if ((extra != NULL) && (extra[0] != 0)){
		snprintf(buf, sizeof(buf), "{\"ok\":1,%s}", extra);
	}else{
		snprintf(buf, sizeof(buf), "{\"ok\":1}");
	}
	api_queue_json(buf);
}

static void api_reply_error(char const* message)
{
	char buf[512];
	auint pos = 0U;
	char const* prefix = "\"ok\":0,\"error\":\"";
	buf[pos++] = '{';
	memcpy(buf + pos, prefix, strlen(prefix)); pos += (auint)strlen(prefix);
	api_json_escape_append(buf, sizeof(buf), &pos, (message == NULL) ? "error" : message);
	if ((pos + 3U) < sizeof(buf)){
		buf[pos++] = '"';
		buf[pos++] = '}';
		buf[pos] = 0;
	}
	api_queue_json(buf);
}

static auint api_parse_region(char const* s)
{
	if (s == NULL){ return 0xFFFFFFFFU; }
	if ((strcmp(s, "SRAM") == 0) || (strcmp(s, "sram") == 0)){ return MAINUI_DBG_MEM_SRAM; }
	if ((strcmp(s, "SPIRAM") == 0) || (strcmp(s, "spiram") == 0)){ return MAINUI_DBG_MEM_SPIRAM; }
	if ((strcmp(s, "EEPROM") == 0) || (strcmp(s, "eeprom") == 0)){ return MAINUI_DBG_MEM_EEPROM; }
	if ((strcmp(s, "FLASH") == 0) || (strcmp(s, "flash") == 0)){ return MAINUI_DBG_MEM_FLASH; }
	if ((strcmp(s, "IO") == 0) || (strcmp(s, "io") == 0)){ return 0xFFFFFFFEU; }
	return 0xFFFFFFFFU;
}

static char const* api_region_name(auint region)
{
	if (region == MAINUI_DBG_MEM_SRAM){ return "SRAM"; }
	if (region == MAINUI_DBG_MEM_SPIRAM){ return "SPIRAM"; }
	if (region == MAINUI_DBG_MEM_EEPROM){ return "EEPROM"; }
	if (region == MAINUI_DBG_MEM_FLASH){ return "FLASH"; }
	if (region == 0xFFFFFFFEU){ return "IO"; }
	return "UNKNOWN";
}

static auint api_region_size(auint region)
{
	return (region == 0xFFFFFFFEU) ? 0x100U : mainui_debug_get_mem_region_size(region);
}

static auint api_region_read(auint region, auint addr)
{
	return (region == 0xFFFFFFFEU) ? mainui_debug_get_io_byte(addr) : mainui_debug_get_mem_region_byte(region, addr);
}

static void api_region_write(auint region, auint addr, auint value)
{
	if (region == 0xFFFFFFFEU){
		mainui_debug_set_io_byte(addr, value);
	}else{
		mainui_debug_set_mem_region_byte(region, addr, value);
	}
}

static auint api_parse_wait_op(char const* s)
{
	if (s == NULL){ return 0xFFFFFFFFU; }
	if (strcmp(s, "==") == 0){ return API_WAIT_OP_EQ; }
	if (strcmp(s, "!=") == 0){ return API_WAIT_OP_NE; }
	if (strcmp(s, "<") == 0){ return API_WAIT_OP_LT; }
	if (strcmp(s, "<=") == 0){ return API_WAIT_OP_LE; }
	if (strcmp(s, ">") == 0){ return API_WAIT_OP_GT; }
	if (strcmp(s, ">=") == 0){ return API_WAIT_OP_GE; }
	if (strcmp(s, "&") == 0){ return API_WAIT_OP_AND; }
	return 0xFFFFFFFFU;
}

static boole api_wait_compare(auint op, auint lhs, auint rhs)
{
	switch (op){
		case API_WAIT_OP_EQ: return (lhs == rhs) ? TRUE : FALSE;
		case API_WAIT_OP_NE: return (lhs != rhs) ? TRUE : FALSE;
		case API_WAIT_OP_LT: return (lhs < rhs) ? TRUE : FALSE;
		case API_WAIT_OP_LE: return (lhs <= rhs) ? TRUE : FALSE;
		case API_WAIT_OP_GT: return (lhs > rhs) ? TRUE : FALSE;
		case API_WAIT_OP_GE: return (lhs >= rhs) ? TRUE : FALSE;
		case API_WAIT_OP_AND: return ((lhs & rhs) != 0U) ? TRUE : FALSE;
		default: return FALSE;
	}
}


static boole api_memscan_candidate_get(api_memscan_t const* scan, auint index)
{
	return ((scan->candidates[index >> 3U] & (uint8)(1U << (index & 7U))) != 0U) ? TRUE : FALSE;
}

static void api_memscan_candidate_set(api_memscan_t* scan, auint index, boole enabled)
{
	uint8 mask = (uint8)(1U << (index & 7U));
	if (enabled){
		scan->candidates[index >> 3U] |= mask;
	}else{
		scan->candidates[index >> 3U] &= (uint8)(~mask);
	}
}

static auint api_parse_memscan_op(char const* s)
{
	if (s == NULL){ return 0xFFFFFFFFU; }
	if ((strcmp(s, "EQ") == 0) || (strcmp(s, "eq") == 0) || (strcmp(s, "==") == 0)){ return API_MEMSCAN_OP_EQ; }
	if ((strcmp(s, "NE") == 0) || (strcmp(s, "ne") == 0) || (strcmp(s, "!=") == 0)){ return API_MEMSCAN_OP_NE; }
	if ((strcmp(s, "LT") == 0) || (strcmp(s, "lt") == 0) || (strcmp(s, "<") == 0)){ return API_MEMSCAN_OP_LT; }
	if ((strcmp(s, "LE") == 0) || (strcmp(s, "le") == 0) || (strcmp(s, "<=") == 0)){ return API_MEMSCAN_OP_LE; }
	if ((strcmp(s, "GT") == 0) || (strcmp(s, "gt") == 0) || (strcmp(s, ">") == 0)){ return API_MEMSCAN_OP_GT; }
	if ((strcmp(s, "GE") == 0) || (strcmp(s, "ge") == 0) || (strcmp(s, ">=") == 0)){ return API_MEMSCAN_OP_GE; }
	if ((strcmp(s, "CHANGED") == 0) || (strcmp(s, "changed") == 0) || (strcmp(s, "CHG") == 0) || (strcmp(s, "chg") == 0)){ return API_MEMSCAN_OP_CHANGED; }
	if ((strcmp(s, "UNCHANGED") == 0) || (strcmp(s, "unchanged") == 0) || (strcmp(s, "SAME") == 0) || (strcmp(s, "same") == 0)){ return API_MEMSCAN_OP_UNCHANGED; }
	if ((strcmp(s, "INCREASED") == 0) || (strcmp(s, "increased") == 0) || (strcmp(s, "INC") == 0) || (strcmp(s, "inc") == 0)){ return API_MEMSCAN_OP_INCREASED; }
	if ((strcmp(s, "DECREASED") == 0) || (strcmp(s, "decreased") == 0) || (strcmp(s, "DEC") == 0) || (strcmp(s, "dec") == 0)){ return API_MEMSCAN_OP_DECREASED; }
	return 0xFFFFFFFFU;
}

static char const* api_memscan_op_name(auint op)
{
	switch (op){
		case API_MEMSCAN_OP_EQ: return "EQ";
		case API_MEMSCAN_OP_NE: return "NE";
		case API_MEMSCAN_OP_LT: return "LT";
		case API_MEMSCAN_OP_LE: return "LE";
		case API_MEMSCAN_OP_GT: return "GT";
		case API_MEMSCAN_OP_GE: return "GE";
		case API_MEMSCAN_OP_CHANGED: return "CHANGED";
		case API_MEMSCAN_OP_UNCHANGED: return "UNCHANGED";
		case API_MEMSCAN_OP_INCREASED: return "INCREASED";
		case API_MEMSCAN_OP_DECREASED: return "DECREASED";
		default: return "UNKNOWN";
	}
}

static boole api_memscan_op_needs_value(auint op)
{
	return (op <= API_MEMSCAN_OP_GE) ? TRUE : FALSE;
}

static boole api_memscan_compare(auint op, auint current, auint previous, auint value)
{
	switch (op){
		case API_MEMSCAN_OP_EQ: return (current == value) ? TRUE : FALSE;
		case API_MEMSCAN_OP_NE: return (current != value) ? TRUE : FALSE;
		case API_MEMSCAN_OP_LT: return (current < value) ? TRUE : FALSE;
		case API_MEMSCAN_OP_LE: return (current <= value) ? TRUE : FALSE;
		case API_MEMSCAN_OP_GT: return (current > value) ? TRUE : FALSE;
		case API_MEMSCAN_OP_GE: return (current >= value) ? TRUE : FALSE;
		case API_MEMSCAN_OP_CHANGED: return (current != previous) ? TRUE : FALSE;
		case API_MEMSCAN_OP_UNCHANGED: return (current == previous) ? TRUE : FALSE;
		case API_MEMSCAN_OP_INCREASED: return (current > previous) ? TRUE : FALSE;
		case API_MEMSCAN_OP_DECREASED: return (current < previous) ? TRUE : FALSE;
		default: return FALSE;
	}
}

static boole api_parse_word_addr_or_pc(char** prest, auint* out_addr, char const* opname)
{
	char* tok;
	if ((prest == NULL) || (*prest == NULL)){
		char msg[96];
		snprintf(msg, sizeof(msg), "%s needs address or PC", opname);
		api_reply_error(msg);
		return FALSE;
	}
	tok = api_next_token(prest);
	if (tok == NULL){
		char msg[96];
		snprintf(msg, sizeof(msg), "%s needs address or PC", opname);
		api_reply_error(msg);
		return FALSE;
	}
	if ((strcmp(tok, "PC") == 0) || (strcmp(tok, "pc") == 0) || (strcmp(tok, ".") == 0)){
		mainui_debug_cpu_t cpu;
		mainui_debug_get_cpu(&cpu);
		if (out_addr != NULL){ *out_addr = cpu.pc & 0x7FFFU; }
		return TRUE;
	}
	if (out_addr != NULL){ *out_addr = ((auint)strtoul(tok, NULL, 0)) & 0x7FFFU; }
	return TRUE;
}

static void api_handle_disasm(char* rest)
{
	char* tok;
	auint addr;
	auint count = 16U;
	auint emitted = 0U;
	auint cur;
	char buf[8192];
	auint pos = 0U;

	if (!api_parse_word_addr_or_pc(&rest, &addr, "DISASM")){ return; }
	tok = api_next_token(&rest);
	if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); }
	if (count == 0U){ count = 16U; }
	if (count > API_DISASM_MAX){ count = API_DISASM_MAX; }

	cur = addr & 0x7FFFU;
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"addr\":%u,\"count\":%u,\"instructions\":[",
		(unsigned)addr,
		(unsigned)count);
	while ((emitted < count) && ((sizeof(buf) - pos) > 256U)){
		char text[384];
		auint words = 1U;
		auint word0;
		auint word1;
		text[0] = 0;
		if (!mainui_debug_get_disasm(cur, text, sizeof(text), &words)){
			api_reply_error("Disassembler unavailable");
			return;
		}
		if (words == 0U){ words = 1U; }
		if (words > 2U){ words = 2U; }
		word0 = mainui_debug_get_prog_word(cur);
		word1 = mainui_debug_get_prog_word((cur + 1U) & 0x7FFFU);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s{\"addr\":%u,\"words\":%u,\"word0\":%u,\"word1\":%u,\"text\":\"",
			(emitted == 0U) ? "" : ",",
			(unsigned)cur,
			(unsigned)words,
			(unsigned)word0,
			(unsigned)word1);
		api_json_escape_append(buf, sizeof(buf), &pos, text);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
		cur = (cur + words) & 0x7FFFU;
		emitted++;
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static char const* api_callsite_kind(auint return_addr, auint* out_call_addr)
{
	auint prev1;
	auint prev2;
	if (return_addr == 0U){ return NULL; }
	prev1 = mainui_debug_get_prog_word((return_addr - 1U) & 0x7FFFU);
	if ((prev1 & 0xF000U) == 0xD000U){
		if (out_call_addr != NULL){ *out_call_addr = (return_addr - 1U) & 0x7FFFU; }
		return "RCALL";
	}
	if (prev1 == 0x9509U){
		if (out_call_addr != NULL){ *out_call_addr = (return_addr - 1U) & 0x7FFFU; }
		return "ICALL";
	}
	if (return_addr >= 2U){
		prev2 = mainui_debug_get_prog_word((return_addr - 2U) & 0x7FFFU);
		if ((prev2 & 0xFE0EU) == 0x940EU){
			if (out_call_addr != NULL){ *out_call_addr = (return_addr - 2U) & 0x7FFFU; }
			return "CALL";
		}
	}
	return NULL;
}

static void api_handle_stack(char* rest)
{
	mainui_debug_cpu_t cpu;
	char* tok;
	auint count = 32U;
	auint i;
	char buf[8192];
	auint pos = 0U;
	mainui_debug_get_cpu(&cpu);
	tok = api_next_token(&rest);
	if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); }
	if (count == 0U){ count = 32U; }
	if (count > API_STACK_MAX){ count = API_STACK_MAX; }
	if (cpu.sp >= 0x10FFU){ count = 0U; }
	else if (count > (0x10FFU - cpu.sp)){ count = 0x10FFU - cpu.sp; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"sp\":%u,\"count\":%u,\"bytes\":[",
		(unsigned)cpu.sp, (unsigned)count);
	for (i = 0U; i < count; ++i){
		auint addr = cpu.sp + 1U + i;
		auint value = mainui_debug_get_sram_byte(addr);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s{\"addr\":%u,\"value\":%u}",
			(i == 0U) ? "" : ",", (unsigned)addr, (unsigned)value);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_callstack(char* rest)
{
	mainui_debug_cpu_t cpu;
	char* tok;
	auint count = 16U;
	auint slot;
	auint emitted = 0U;
	char buf[8192];
	auint pos = 0U;
	mainui_debug_get_cpu(&cpu);
	tok = api_next_token(&rest);
	if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); }
	if (count == 0U){ count = 16U; }
	if (count > API_CALLSTACK_MAX){ count = API_CALLSTACK_MAX; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"sp\":%u,\"max_frames\":%u,\"frames\":[",
		(unsigned)cpu.sp, (unsigned)count);
	for (slot = 0U; (emitted < count) && ((cpu.sp + 2U + (slot * 2U)) <= 0x10FFU); ++slot){
		auint stack_addr = cpu.sp + 1U + (slot * 2U);
		auint high = mainui_debug_get_sram_byte(stack_addr);
		auint low = mainui_debug_get_sram_byte(stack_addr + 1U);
		auint ret = ((high << 8) | low) & 0x7FFFU;
		auint call_addr = 0U;
		char const* kind = api_callsite_kind(ret, &call_addr);
		if (kind != NULL){
			char text[384];
			auint words = 1U;
			text[0] = 0;
			(void)mainui_debug_get_disasm(call_addr, text, sizeof(text), &words);
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
				"%s{\"depth\":%u,\"stack_addr\":%u,\"return_addr\":%u,\"call_addr\":%u,\"kind\":\"%s\",\"text\":\"",
				(emitted == 0U) ? "" : ",", (unsigned)emitted,
				(unsigned)stack_addr, (unsigned)ret, (unsigned)call_addr, kind);
			api_json_escape_append(buf, sizeof(buf), &pos, text);
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
			emitted++;
		}
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"],\"count\":%u,\"heuristic\":1}", (unsigned)emitted);
	api_queue_json(buf);
}

static void api_handle_run_status(void)
{
	auint run_target = 0U;
	auint break_addr = 0U;
	boole run_target_active = mainui_debug_get_run_target(&run_target);
	boole break_hit = mainui_debug_get_last_break_hit(&break_addr);
	char buf[1024];
	auint pos = 0U;
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"paused\":%u,\"frame\":%u,\"run_target_active\":%u,\"run_target\":%u,\"break_hit\":%u,\"break_addr\":%u,\"status\":\"",
		(unsigned)(mainui_debug_is_paused() ? 1U : 0U),
		(unsigned)mainui_get_frame_counter(),
		(unsigned)(run_target_active ? 1U : 0U),
		(unsigned)run_target,
		(unsigned)(break_hit ? 1U : 0U),
		(unsigned)break_addr);
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_run_control_status());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	api_queue_json(buf);
}

static void api_handle_run_to(char* rest)
{
	auint addr;
	char extra[160];
	if (!api_parse_word_addr_or_pc(&rest, &addr, "RUN_TO")){ return; }
	if (!mainui_debug_run_to_word(addr)){ api_reply_error("RUN_TO unavailable"); return; }
	snprintf(extra, sizeof(extra), "\"run_to\":1,\"addr\":%u", (unsigned)addr);
	api_reply_ok_simple(extra);
}

static void api_handle_step_into(void)
{
	mainui_debug_step_instruction();
	api_reply_ok_simple("\"step_into\":1");
}

static void api_handle_step_over(void)
{
	if (!mainui_debug_step_over()){ api_reply_error("STEP_OVER unavailable"); return; }
	api_reply_ok_simple("\"step_over\":1");
}

static void api_handle_step_out(void)
{
	if (!mainui_debug_step_out()){ api_reply_error("STEP_OUT unavailable"); return; }
	api_reply_ok_simple("\"step_out\":1");
}


#ifdef ENABLE_DEBUGGER
static auint api_parse_watch_region(char const* s)
{
	auint region = api_parse_region(s);
	if (region == MAINUI_DBG_MEM_SRAM){ return CU_AVR_WATCH_REGION_SRAM; }
	if (region == 0xFFFFFFFEU){ return CU_AVR_WATCH_REGION_IO; }
	return 0xFFFFFFFFU;
}

static char const* api_watch_region_name(auint region)
{
	if (region == CU_AVR_WATCH_REGION_SRAM){ return "SRAM"; }
	if (region == CU_AVR_WATCH_REGION_IO){ return "IO"; }
	return "UNKNOWN";
}

static auint api_watch_region_size(auint region)
{
	if (region == CU_AVR_WATCH_REGION_SRAM){ return 0x1000U; }
	if (region == CU_AVR_WATCH_REGION_IO){ return 0x100U; }
	return 0U;
}

static auint api_parse_watch_flags(char const* s)
{
	if (s == NULL){ return 0xFFFFFFFFU; }
	if ((strcmp(s, "R") == 0) || (strcmp(s, "r") == 0) || (strcmp(s, "READ") == 0) || (strcmp(s, "read") == 0)){
		return CU_AVR_WATCH_READ;
	}
	if ((strcmp(s, "W") == 0) || (strcmp(s, "w") == 0) || (strcmp(s, "WRITE") == 0) || (strcmp(s, "write") == 0)){
		return CU_AVR_WATCH_WRITE;
	}
	if ((strcmp(s, "RW") == 0) || (strcmp(s, "rw") == 0) || (strcmp(s, "WR") == 0) || (strcmp(s, "wr") == 0) ||
	    (strcmp(s, "READWRITE") == 0) || (strcmp(s, "readwrite") == 0)){
		return CU_AVR_WATCH_READ | CU_AVR_WATCH_WRITE;
	}
	return 0xFFFFFFFFU;
}

static char const* api_watch_flags_name(auint flags)
{
	flags &= (CU_AVR_WATCH_READ | CU_AVR_WATCH_WRITE);
	if (flags == (CU_AVR_WATCH_READ | CU_AVR_WATCH_WRITE)){ return "RW"; }
	if (flags == CU_AVR_WATCH_READ){ return "R"; }
	if (flags == CU_AVR_WATCH_WRITE){ return "W"; }
	return "";
}
#endif

static boole api_wait_mem_current(api_wait_state_t const* wait, auint* out_value)
{
	auint size;
	auint cur;
	if ((wait == NULL) || (!wait->active) || (wait->type != API_WAIT_MEM)){ return FALSE; }
	size = api_region_size(wait->region);
	if (wait->addr >= size){ return FALSE; }
	cur = api_region_read(wait->region, wait->addr) & 0xFFFFU;
	if (out_value != NULL){ *out_value = cur; }
	return api_wait_compare(wait->op, cur, wait->value);
}

static void api_wait_reply_frame(api_client_t* client, boole timeout)
{
	char buf[256];
	api_wait_state_t* wait;
	if (client == NULL){ return; }
	wait = &client->wait;
	snprintf(buf, sizeof(buf),
		"{\"ok\":1,\"wait\":\"frame\",\"frame\":%u,\"target\":%u,\"timeout\":%u}",
		(unsigned)mainui_get_frame_counter(),
		(unsigned)wait->frame_target,
		(unsigned)(timeout ? 1U : 0U));
	api_queue_json_to(client, buf);
	api_wait_state_clear(wait);
}

static void api_wait_reply_mem(api_client_t* client, boole timeout, auint current)
{
	char buf[320];
	api_wait_state_t* wait;
	if (client == NULL){ return; }
	wait = &client->wait;
	snprintf(buf, sizeof(buf),
		"{\"ok\":1,\"wait\":\"mem\",\"region\":\"%s\",\"addr\":%u,\"value\":%u,\"expect\":%u,\"frame\":%u,\"timeout\":%u}",
		api_region_name(wait->region),
		(unsigned)wait->addr,
		(unsigned)current,
		(unsigned)wait->value,
		(unsigned)mainui_get_frame_counter(),
		(unsigned)(timeout ? 1U : 0U));
	api_queue_json_to(client, buf);
	api_wait_state_clear(wait);
}

static void api_wait_check_now(void)
{
	uint32 frame_now = mainui_get_frame_counter();
	auint i;
	for (i = 0U; i < API_MAX_CLIENTS; ++i){
		api_client_t* client = &api_state.clients[i];
		api_wait_state_t* wait = &client->wait;
		auint current = 0U;
		if ((client->sock == API_INVALID_SOCKET) || (!wait->active)){ continue; }
		if (wait->type == API_WAIT_FRAME){
			if (frame_now >= wait->frame_target){
				api_wait_reply_frame(client, FALSE);
				continue;
			}
			if ((wait->timeout_frames != 0U) && ((frame_now - wait->start_frame) >= wait->timeout_frames)){
				api_wait_reply_frame(client, TRUE);
				continue;
			}
		}else if (wait->type == API_WAIT_MEM){
			if (api_wait_mem_current(wait, &current)){
				api_wait_reply_mem(client, FALSE, current);
				continue;
			}
			if ((wait->timeout_frames != 0U) && ((frame_now - wait->start_frame) >= wait->timeout_frames)){
				current = api_region_read(wait->region, wait->addr) & 0xFFFFU;
				api_wait_reply_mem(client, TRUE, current);
				continue;
			}
		}
	}
}

static void api_input_prime_slot(auint player)
{
	api_input_override_t* slot;
	if (player >= API_INPUT_PORTS){ return; }
	slot = &api_state.input[player];
	if (slot->active){ return; }
	while (slot->queue_pos < slot->queue_len){
		auint idx = slot->queue_pos++;
		if (slot->queue[idx].frames == 0U){ continue; }
		slot->active = TRUE;
		slot->buttons = slot->queue[idx].buttons;
		slot->frames_remaining = slot->queue[idx].frames;
		break;
	}
	if (slot->queue_pos >= slot->queue_len){
		slot->queue_pos = 0U;
		slot->queue_len = 0U;
	}
}

static void api_handle_get_rom_info(void)
{
	char buf[2048];
	auint pos = 0U;
	char const* path = mainui_get_loaded_rom_path();
	char const* name = mainui_get_current_rom_name();
	char const* author = mainui_get_current_rom_author();
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"name\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, name);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"path\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, (path != NULL) ? path : "");
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"author\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, author);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"\",\"year\":%u,\"is_uze\":%u,\"crc32\":%u,\"frame\":%u}",
		(unsigned)mainui_get_current_rom_year(),
		(unsigned)(mainui_get_current_rom_is_uze() ? 1U : 0U),
		(unsigned)mainui_get_current_rom_crc32(),
		(unsigned)mainui_get_frame_counter());
	api_queue_json(buf);
}

static void api_handle_get_state(void)
{
	char buf[512];
	snprintf(buf, sizeof(buf),
		"{\"ok\":1,\"connected\":1,\"paused\":%u,\"frame\":%u,\"run_frames_remaining\":%u,\"api_enabled\":%u,\"api_port\":%u,\"api_clients\":%u,\"api_max_clients\":%u}",
		(unsigned)(mainui_debug_is_paused() ? 1U : 0U),
		(unsigned)mainui_get_frame_counter(),
		(unsigned)api_state.run_frames_remaining,
		(unsigned)(api_state.enabled ? 1U : 0U),
		(unsigned)api_active_port(),
		(unsigned)api_client_count(),
		(unsigned)API_MAX_CLIENTS);
	api_queue_json(buf);
}

static void api_handle_get_frame(void)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "{\"ok\":1,\"frame\":%u}", (unsigned)mainui_get_frame_counter());
	api_queue_json(buf);
}

static void api_handle_read_regs(void)
{
	char buf[4096];
	auint pos = 0U;
	auint i;
	mainui_debug_cpu_t cpu;
	mainui_debug_get_cpu(&cpu);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"pc\":%u,\"cycle\":%u,\"sp\":%u,\"row_pulse\":%u,\"sreg\":%u,\"regs\":[",
		(unsigned)cpu.pc,
		(unsigned)cpu.cycle,
		(unsigned)cpu.sp,
		(unsigned)cpu.row_pulse,
		(unsigned)cpu.sreg);
	for (i = 0U; i < 32U; ++i){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%u", (i == 0U) ? "" : ",", (unsigned)cpu.regs[i]);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}


#ifdef ENABLE_DEBUGGER
static void api_handle_profile(char* rest)
{
	char* tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("PROFILE needs ON, OFF, or CLEAR"); return; }
	if ((strcmp(tok, "ON") == 0) || (strcmp(tok, "on") == 0)){
		cu_avr_profiler_enable(TRUE);
		api_reply_ok_simple("\"profile_enabled\":1");
	}else if ((strcmp(tok, "OFF") == 0) || (strcmp(tok, "off") == 0)){
		cu_avr_profiler_enable(FALSE);
		api_reply_ok_simple("\"profile_enabled\":0");
	}else if ((strcmp(tok, "CLEAR") == 0) || (strcmp(tok, "clear") == 0) || (strcmp(tok, "RESET") == 0) || (strcmp(tok, "reset") == 0)){
		cu_avr_profiler_reset();
		api_reply_ok_simple("\"profile_cleared\":1");
	}else{
		api_reply_error("PROFILE command must be ON, OFF, or CLEAR");
	}
}

static void api_handle_read_profile(char* rest)
{
	char* tok;
	auint addr;
	auint len = 1U;
	auint i;
	char buf[4096];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("READ_PROFILE needs address and optional length"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("READ_PROFILE needs address"); return; }
	addr = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest);
	if (tok != NULL){ len = (auint)strtoul(tok, NULL, 0); }
	if (len > 128U){ len = 128U; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"profile_enabled\":%u,\"addr\":%u,\"samples\":[",
		(unsigned)(cu_avr_profiler_enabled() ? 1U : 0U),
		(unsigned)addr);
	for (i = 0U; i < len; ++i){
		uint32 hits = 0U;
		uint64_t cycles = 0U;
		cu_avr_profiler_get(addr + i, &hits, &cycles);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s{\"pc\":%u,\"hits\":%lu,\"cycles\":%" PRIu64 "}",
			(i == 0U) ? "" : ",",
			(unsigned)(addr + i),
			(unsigned long)hits,
			(uint64_t)cycles);
		if ((sizeof(buf) - pos) < 96U){ break; }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_profile_top(char* rest)
{
	char* tok;
	auint count = 16U;
	auint by_cycles = TRUE;
	auint pcs[API_PROFILE_TOP_MAX];
	uint32 hits[API_PROFILE_TOP_MAX];
	uint64_t cycles[API_PROFILE_TOP_MAX];
	auint used = 0U;
	auint pc;
	auint i;
	char buf[4096];
	auint pos = 0U;

	for (i = 0U; i < API_PROFILE_TOP_MAX; ++i){
		pcs[i] = 0U;
		hits[i] = 0U;
		cycles[i] = 0U;
	}

	tok = api_next_token(&rest);
	if (tok != NULL){
		count = (auint)strtoul(tok, NULL, 0);
		if (count == 0U){ count = 16U; }
	}
	if (count > API_PROFILE_TOP_MAX){ count = API_PROFILE_TOP_MAX; }

	tok = api_next_token(&rest);
	if (tok != NULL){
		if ((strcmp(tok, "HITS") == 0) || (strcmp(tok, "hits") == 0)){
			by_cycles = FALSE;
		}else if ((strcmp(tok, "CYCLES") == 0) || (strcmp(tok, "cycles") == 0)){
			by_cycles = TRUE;
		}else{
			api_reply_error("PROFILE_TOP sort must be CYCLES or HITS");
			return;
		}
	}

	for (pc = 0U; pc < API_PROFILE_SCAN_WORDS; ++pc){
		uint32 h = 0U;
		uint64_t c = 0U;
		uint64_t score;
		cu_avr_profiler_get(pc, &h, &c);
		if (h == 0U){ continue; }
		score = by_cycles ? c : (uint64_t)h;
		if ((used < count) || (score > (by_cycles ? cycles[used - 1U] : (uint64_t)hits[used - 1U]))){
			auint posi = used;
			if (posi >= count){ posi = count - 1U; }
			while (posi > 0U){
				uint64_t prev_score = by_cycles ? cycles[posi - 1U] : (uint64_t)hits[posi - 1U];
				if (score <= prev_score){ break; }
				pcs[posi] = pcs[posi - 1U];
				hits[posi] = hits[posi - 1U];
				cycles[posi] = cycles[posi - 1U];
				posi --;
			}
			pcs[posi] = pc;
			hits[posi] = h;
			cycles[posi] = c;
			if (used < count){ used ++; }
		}
	}

	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"profile_enabled\":%u,\"sort\":\"%s\",\"entries\":[",
		(unsigned)(cu_avr_profiler_enabled() ? 1U : 0U),
		by_cycles ? "cycles" : "hits");
	for (i = 0U; i < used; ++i){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s{\"pc\":%u,\"hits\":%lu,\"cycles\":%" PRIu64 "}",
			(i == 0U) ? "" : ",",
			(unsigned)pcs[i],
			(unsigned long)hits[i],
			(uint64_t)cycles[i]);
		if ((sizeof(buf) - pos) < 96U){ break; }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}



static void api_handle_break_list(char* rest)
{
	char* tok;
	auint start = 0U;
	auint limit = 64U;
	auint addr = 0U;
	auint emitted = 0U;
	char buf[4096];
	auint pos = 0U;

	tok = api_next_token(&rest);
	if (tok != NULL){ start = (auint)strtoul(tok, NULL, 0); }
	tok = api_next_token(&rest);
	if (tok != NULL){ limit = (auint)strtoul(tok, NULL, 0); }
	if (limit == 0U){ limit = 64U; }
	if (limit > 128U){ limit = 128U; }

	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"start\":%u,\"limit\":%u,\"breakpoints\":[",
		(unsigned)(start & 0x7FFFU),
		(unsigned)limit);
	addr = start & 0x7FFFU;
	while ((addr < 32768U) && (emitted < limit)){
		auint found = 0U;
		if (!cu_avr_breakpoint_next(addr, &found)){ break; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s%u",
			(emitted == 0U) ? "" : ",",
			(unsigned)found);
		emitted++;
		addr = found + 1U;
		if ((sizeof(buf) - pos) < 32U){ break; }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_break_temp(char* rest)
{
	char* tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("BREAK TEMP needs SET, CLEAR, or LIST"); return; }
	if ((strcmp(tok, "SET") == 0) || (strcmp(tok, "set") == 0)){
		char* atok = api_next_token(&rest);
		auint addr;
		char buf[160];
		if (atok == NULL){ api_reply_error("BREAK TEMP SET needs address"); return; }
		addr = (auint)strtoul(atok, NULL, 0) & 0x7FFFU;
		cu_avr_temp_break_set(addr, TRUE);
		snprintf(buf, sizeof(buf), "\"temp_breakpoint_set\":1,\"addr\":%u", (unsigned)addr);
		api_reply_ok_simple(buf);
		return;
	}
	if ((strcmp(tok, "CLEAR") == 0) || (strcmp(tok, "clear") == 0)){
		cu_avr_temp_break_clear();
		api_reply_ok_simple("\"temp_breakpoint_cleared\":1");
		return;
	}
	if ((strcmp(tok, "LIST") == 0) || (strcmp(tok, "list") == 0)){
		auint addr = 0U;
		boole active = cu_avr_temp_break_get(&addr);
		char buf[160];
		snprintf(buf, sizeof(buf), "\"temp_breakpoint_active\":%u,\"addr\":%u", (unsigned)(active ? 1U : 0U), (unsigned)addr);
		api_reply_ok_simple(buf);
		return;
	}
	api_reply_error("BREAK TEMP command must be SET, CLEAR, or LIST");
}

static void api_handle_break(char* rest)
{
	char* tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("BREAK needs SET, CLEAR, LIST, NEXT, TEMP, STEP, or STEP_CLEAR"); return; }
	if ((strcmp(tok, "SET") == 0) || (strcmp(tok, "set") == 0)){
		char* atok = api_next_token(&rest);
		auint addr;
		char buf[160];
		if (atok == NULL){ api_reply_error("BREAK SET needs address"); return; }
		addr = (auint)strtoul(atok, NULL, 0) & 0x7FFFU;
		cu_avr_breakpoint_set(addr, TRUE);
		snprintf(buf, sizeof(buf), "\"breakpoint_set\":1,\"addr\":%u", (unsigned)addr);
		api_reply_ok_simple(buf);
		return;
	}
	if ((strcmp(tok, "CLEAR") == 0) || (strcmp(tok, "clear") == 0)){
		char* atok = api_next_token(&rest);
		if (atok == NULL){ api_reply_error("BREAK CLEAR needs address or ALL"); return; }
		if ((strcmp(atok, "ALL") == 0) || (strcmp(atok, "all") == 0)){
			cu_avr_breakpoints_clear();
			api_reply_ok_simple("\"breakpoints_cleared\":1");
			return;
		}else{
			auint addr = (auint)strtoul(atok, NULL, 0) & 0x7FFFU;
			cu_avr_breakpoint_set(addr, FALSE);
			api_reply_ok_simple("\"breakpoint_cleared\":1");
			return;
		}
	}
	if ((strcmp(tok, "LIST") == 0) || (strcmp(tok, "list") == 0)){
		api_handle_break_list(rest);
		return;
	}
	if ((strcmp(tok, "NEXT") == 0) || (strcmp(tok, "next") == 0)){
		char* atok = api_next_token(&rest);
		auint start = (atok != NULL) ? (auint)strtoul(atok, NULL, 0) : 0U;
		auint addr = 0U;
		boole found = cu_avr_breakpoint_next(start, &addr);
		char buf[160];
		snprintf(buf, sizeof(buf), "\"found\":%u,\"addr\":%u", (unsigned)(found ? 1U : 0U), (unsigned)addr);
		api_reply_ok_simple(buf);
		return;
	}
	if ((strcmp(tok, "TEMP") == 0) || (strcmp(tok, "temp") == 0)){
		api_handle_break_temp(rest);
		return;
	}
	if ((strcmp(tok, "STEP") == 0) || (strcmp(tok, "step") == 0)){
		char* ctok = api_next_token(&rest);
		auint count = (ctok != NULL) ? (auint)strtoul(ctok, NULL, 0) : 1U;
		char buf[160];
		if (count == 0U){ count = 1U; }
		cu_avr_debug_step_instructions(count);
		mainui_debug_set_paused(FALSE);
		snprintf(buf, sizeof(buf), "\"instruction_step\":%u", (unsigned)count);
		api_reply_ok_simple(buf);
		return;
	}
	if ((strcmp(tok, "STEP_CLEAR") == 0) || (strcmp(tok, "step_clear") == 0)){
		cu_avr_debug_step_clear();
		api_reply_ok_simple("\"instruction_step_cleared\":1");
		return;
	}
	api_reply_error("BREAK command must be SET, CLEAR, LIST, NEXT, TEMP, STEP, or STEP_CLEAR");
}

static void api_reply_watch_hit(boole clear)
{
	auint slot = 0U;
	auint region = 0U;
	auint flags = 0U;
	auint addr = 0U;
	auint value = 0U;
	auint pc = 0U;
	boole hit = cu_avr_watchpoint_get_last(clear, &slot, &region, &flags, &addr, &value, &pc);
	char buf[384];
	snprintf(buf, sizeof(buf),
		"{\"ok\":1,\"hit\":%u,\"slot\":%u,\"region\":\"%s\",\"mode\":\"%s\",\"addr\":%u,\"value\":%u,\"pc\":%u,\"cleared\":%u}",
		(unsigned)(hit ? 1U : 0U),
		(unsigned)slot,
		api_watch_region_name(region),
		api_watch_flags_name(flags),
		(unsigned)addr,
		(unsigned)value,
		(unsigned)pc,
		(unsigned)(clear ? 1U : 0U));
	api_queue_json(buf);
}

static void api_handle_watch_list(void)
{
	char buf[2048];
	auint pos = 0U;
	auint slot;
	auint emitted = 0U;
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"watchpoints\":[");
	for (slot = 0U; slot < CU_AVR_WATCH_SLOTS; ++slot){
		boole enable = FALSE;
		auint region = 0U;
		auint flags = 0U;
		auint start_addr = 0U;
		auint end_addr = 0U;
		(void)cu_avr_watchpoint_get(slot, &enable, &region, &flags, &start_addr, &end_addr);
		if (!enable){ continue; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s{\"slot\":%u,\"region\":\"%s\",\"mode\":\"%s\",\"start\":%u,\"end\":%u}",
			(emitted == 0U) ? "" : ",",
			(unsigned)slot,
			api_watch_region_name(region),
			api_watch_flags_name(flags),
			(unsigned)start_addr,
			(unsigned)end_addr);
		emitted++;
		if ((sizeof(buf) - pos) < 160U){ break; }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_watch(char* rest)
{
	char* tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WATCH needs SET, CLEAR, LIST, or HIT"); return; }
	if ((strcmp(tok, "LIST") == 0) || (strcmp(tok, "list") == 0)){
		api_handle_watch_list();
		return;
	}
	if ((strcmp(tok, "HIT") == 0) || (strcmp(tok, "hit") == 0)){
		char* ctok = api_next_token(&rest);
		boole clear = FALSE;
		if ((ctok != NULL) && ((strcmp(ctok, "CLEAR") == 0) || (strcmp(ctok, "clear") == 0))){ clear = TRUE; }
		api_reply_watch_hit(clear);
		return;
	}
	if ((strcmp(tok, "CLEAR") == 0) || (strcmp(tok, "clear") == 0)){
		char* stok = api_next_token(&rest);
		if (stok == NULL){ api_reply_error("WATCH CLEAR needs slot or ALL"); return; }
		if ((strcmp(stok, "ALL") == 0) || (strcmp(stok, "all") == 0)){
			cu_avr_watchpoints_clear();
			api_reply_ok_simple("\"watchpoints_cleared\":1");
			return;
		}else{
			auint slot = (auint)strtoul(stok, NULL, 0);
			if (slot >= CU_AVR_WATCH_SLOTS){ api_reply_error("Watch slot out of range"); return; }
			cu_avr_watchpoint_set(slot, FALSE, CU_AVR_WATCH_REGION_SRAM, 0U, 0U, 0U);
			api_reply_ok_simple("\"watchpoint_cleared\":1");
			return;
		}
	}
	if ((strcmp(tok, "SET") == 0) || (strcmp(tok, "set") == 0)){
		char* stok = api_next_token(&rest);
		char* rtok = api_next_token(&rest);
		char* ftok = api_next_token(&rest);
		char* atok = api_next_token(&rest);
		char* etok;
		auint slot;
		auint region;
		auint flags;
		auint start_addr;
		auint end_addr;
		auint size;
		char buf[384];
		if ((stok == NULL) || (rtok == NULL) || (ftok == NULL) || (atok == NULL)){
			api_reply_error("WATCH SET needs slot region mode start [end]");
			return;
		}
		slot = (auint)strtoul(stok, NULL, 0);
		if (slot >= CU_AVR_WATCH_SLOTS){ api_reply_error("Watch slot out of range"); return; }
		region = api_parse_watch_region(rtok);
		if (region == 0xFFFFFFFFU){ api_reply_error("Watch region must be SRAM or IO"); return; }
		flags = api_parse_watch_flags(ftok);
		if (flags == 0xFFFFFFFFU){ api_reply_error("Watch mode must be R, W, or RW"); return; }
		start_addr = (auint)strtoul(atok, NULL, 0);
		etok = api_next_token(&rest);
		end_addr = (etok != NULL) ? (auint)strtoul(etok, NULL, 0) : start_addr;
		size = api_watch_region_size(region);
		if ((start_addr >= size) || (end_addr >= size)){ api_reply_error("Watch address out of range"); return; }
		cu_avr_watchpoint_set(slot, TRUE, region, flags, start_addr, end_addr);
		snprintf(buf, sizeof(buf),
			"\"watchpoint_set\":1,\"slot\":%u,\"region\":\"%s\",\"mode\":\"%s\",\"start\":%u,\"end\":%u",
			(unsigned)slot,
			api_watch_region_name(region),
			api_watch_flags_name(flags),
			(unsigned)start_addr,
			(unsigned)end_addr);
		api_reply_ok_simple(buf);
		return;
	}
	api_reply_error("WATCH command must be SET, CLEAR, LIST, or HIT");
}

static void api_handle_memory_trace(char* rest)
{
	char* op = (rest != NULL) ? api_next_token(&rest) : NULL;
	char buf[49152];
	auint pos = 0U;
	if ((op == NULL) || (api_stricmp(op, "STATUS") == 0)){
		snprintf(buf, sizeof(buf),
			"\"built\":%u,\"enabled\":%u,\"count\":%u,\"capacity\":%u,\"first_seq\":%u",
			(unsigned)cu_avr_memory_trace_built(),
			(unsigned)cu_avr_memory_trace_enabled(),
			(unsigned)cu_avr_memory_trace_count(),
			(unsigned)CU_AVR_MEMTRACE_CAP,
			(unsigned)cu_avr_memory_trace_first_seq());
		api_reply_ok_simple(buf);
		return;
	}
	if (!cu_avr_memory_trace_built()){ api_reply_error("Memory trace not built (ENABLE_MEMORY_TRACE=0)"); return; }
	if (api_stricmp(op, "ENABLE") == 0){ cu_avr_memory_trace_enable(TRUE); api_reply_ok_simple("\"enabled\":1"); return; }
	if (api_stricmp(op, "DISABLE") == 0){ cu_avr_memory_trace_enable(FALSE); api_reply_ok_simple("\"enabled\":0"); return; }
	if (api_stricmp(op, "CLEAR") == 0){ cu_avr_memory_trace_clear(); api_reply_ok_simple("\"cleared\":1"); return; }
	if (api_stricmp(op, "READ") == 0){
		uint32 seq = cu_avr_memory_trace_first_seq();
		auint count = 256U, i, emitted = 0U;
		char* tok = api_next_token(&rest);
		cu_avr_memtrace_event_t e;
		if (tok != NULL){ seq = (uint32)strtoul(tok, NULL, 0); }
		tok = api_next_token(&rest);
		if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); }
		if (count > 384U){ count = 384U; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"events\":[");
		for (i = 0U; i < count; ++i, ++seq){
			if (!cu_avr_memory_trace_get(seq, &e)){ continue; }
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
				"%s{\"seq\":%u,\"abs_cycle\":%u,\"row\":%u,\"cycle\":%u,\"pc\":%u,\"region\":\"%s\",\"mode\":\"%s\",\"addr\":%u,\"value\":%u}",
				(emitted != 0U) ? "," : "", (unsigned)e.seq, (unsigned)e.abs_cycle,
				(unsigned)e.row, (unsigned)e.beam_cycle, (unsigned)e.pc,
				api_watch_region_name(e.region), api_watch_flags_name(e.flags),
				(unsigned)e.addr, (unsigned)e.value);
			emitted++;
			if (pos > (sizeof(buf) - 512U)){ ++seq; break; }
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"],\"next_seq\":%u,\"count\":%u}", (unsigned)seq, (unsigned)emitted);
		api_queue_json(buf);
		return;
	}
	api_reply_error("MEM_TRACE expects STATUS|ENABLE|DISABLE|CLEAR|READ");
}

#endif

static void api_handle_read_mem(char* rest)
{
	char* tok;
	auint region;
	auint addr;
	auint len = 1U;
	auint size;
	auint i;
	char buf[4096];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("READ_MEM needs region and address"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("READ_MEM needs region"); return; }
	region = api_parse_region(tok);
	if (region == 0xFFFFFFFFU){ api_reply_error("Unknown region"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("READ_MEM needs address"); return; }
	addr = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest);
	if (tok != NULL){ len = (auint)strtoul(tok, NULL, 0); }
	if (len == 0U){ len = 1U; }
	if (len > API_MAX_READ_LEN){ len = API_MAX_READ_LEN; }
	size = api_region_size(region);
	if (addr >= size){ api_reply_error("Address out of range"); return; }
	if ((addr + len) > size){ len = size - addr; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"addr\":%u,\"len\":%u,\"values\":[",
		(unsigned)addr,
		(unsigned)len);
	for (i = 0U; i < len; ++i){
		auint v = api_region_read(region, addr + i);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%u", (i == 0U) ? "" : ",", (unsigned)(v & 0xFFU));
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}



static void api_handle_mem_size(char* rest)
{
	char* tok;
	auint region;
	auint size;
	char buf[160];
	if (rest == NULL){ api_reply_error("MEM_SIZE needs a region"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("MEM_SIZE needs a region"); return; }
	region = api_parse_region(tok);
	if (region == 0xFFFFFFFFU){ api_reply_error("Unknown region"); return; }
	size = api_region_size(region);
	snprintf(buf, sizeof(buf), "\"region\":\"%s\",\"size\":%u", api_region_name(region), (unsigned)size);
	api_reply_ok_simple(buf);
}

static void api_handle_mem_regions(void)
{
	char buf[384];
	snprintf(buf, sizeof(buf),
		"{\"ok\":1,\"regions\":{\"SRAM\":%u,\"IO\":%u,\"FLASH\":%u,\"EEPROM\":%u,\"SPIRAM\":%u}}",
		(unsigned)api_region_size(MAINUI_DBG_MEM_SRAM),
		(unsigned)api_region_size(0xFFFFFFFEU),
		(unsigned)api_region_size(MAINUI_DBG_MEM_FLASH),
		(unsigned)api_region_size(MAINUI_DBG_MEM_EEPROM),
		(unsigned)api_region_size(MAINUI_DBG_MEM_SPIRAM));
	api_queue_json(buf);
}

static void api_handle_write_mem(char* rest)
{
	char* tok;
	auint region;
	auint addr;
	auint size;
	auint count = 0U;
	char buf[256];
	if (rest == NULL){ api_reply_error("WRITE_MEM needs region address value [value...]"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WRITE_MEM needs region"); return; }
	region = api_parse_region(tok);
	if (region == 0xFFFFFFFFU){ api_reply_error("Unknown region"); return; }
	if (region == MAINUI_DBG_MEM_FLASH){ api_reply_error("Direct FLASH writes are not allowed; use bootloader/SPM emulation"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WRITE_MEM needs address"); return; }
	addr = (auint)strtoul(tok, NULL, 0);
	size = api_region_size(region);
	if (addr >= size){ api_reply_error("Address out of range"); return; }
	while ((tok = api_next_token(&rest)) != NULL){
		if ((addr + count) >= size){ break; }
		api_region_write(region, addr + count, (auint)strtoul(tok, NULL, 0));
		count++;
		if (count >= API_MAX_READ_LEN){ break; }
	}
	if (count == 0U){ api_reply_error("WRITE_MEM needs at least one value"); return; }
	snprintf(buf, sizeof(buf), "\"region\":\"%s\",\"addr\":%u,\"written\":%u", api_region_name(region), (unsigned)addr, (unsigned)count);
	api_reply_ok_simple(buf);
}

static void api_handle_write_reg(char* rest)
{
	char* tok;
	auint reg;
	auint value;
	char buf[128];
	if (rest == NULL){ api_reply_error("WRITE_REG needs register and value"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WRITE_REG needs register"); return; }
	if ((tok[0] == 'R') || (tok[0] == 'r')){ tok++; }
	reg = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WRITE_REG needs value"); return; }
	value = (auint)strtoul(tok, NULL, 0);
	if (reg >= 32U){ api_reply_error("Register must be R0..R31"); return; }
	if (!mainui_debug_is_paused()){ api_reply_error("Pause emulation before editing registers"); return; }
	mainui_debug_set_reg_byte(reg, value & 0xFFU);
	snprintf(buf, sizeof(buf), "\"reg\":%u,\"value\":%u", (unsigned)reg, (unsigned)(value & 0xFFU));
	api_reply_ok_simple(buf);
}

static void api_handle_debug_profile(char* rest)
{
	char* sub = api_next_token(&rest);
	char buf[2048];
	auint pos = 0U;
	if ((sub == NULL) || (strcmp(sub, "STATUS") == 0) || (strcmp(sub, "status") == 0)){
		(void)mainui_debug_profile_ensure_loaded();
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"dir\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_profile_dir());
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"status\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_profile_status());
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"symbols_file\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_symbols_file());
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"symbols_status\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_symbols_status());
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"symbol_count\":%u}", (unsigned)mainui_debug_get_symbol_count());
		api_queue_json(buf);
		return;
	}
	if ((strcmp(sub, "SAVE") == 0) || (strcmp(sub, "save") == 0)){
		if (!mainui_debug_profile_save_now()){ api_reply_error("Debugger profile save failed"); return; }
		api_reply_ok_simple("\"profile_saved\":1");
		return;
	}
	if ((strcmp(sub, "RELOAD") == 0) || (strcmp(sub, "reload") == 0)){
		if (!mainui_debug_profile_reload()){ api_reply_error("Debugger profile reload failed"); return; }
		api_reply_ok_simple("\"profile_reloaded\":1");
		return;
	}
	api_reply_error("DEBUG_PROFILE needs STATUS, SAVE, or RELOAD");
}

static void api_handle_memsnap(char* rest)
{
	char* sub;
	char* tok;
	auint slot;
	char buf[8192];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("MEMSNAP needs TAKE, DIFF, LIST, or CLEAR"); return; }
	sub = api_next_token(&rest);
	if (sub == NULL){ api_reply_error("MEMSNAP needs TAKE, DIFF, LIST, or CLEAR"); return; }
	if ((strcmp(sub, "LIST") == 0) || (strcmp(sub, "list") == 0)){
		auint i;
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"slots\":[");
		for (i = 0U; i < API_MEMSNAP_SLOTS; ++i){
			api_memsnap_t const* snap = &api_state.memsnap[i];
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
				"%s{\"slot\":%u,\"valid\":%u,\"region\":\"%s\",\"addr\":%u,\"len\":%u,\"frame\":%u}",
				(i == 0U) ? "" : ",", (unsigned)i, (unsigned)(snap->valid ? 1U : 0U),
				api_region_name(snap->region), (unsigned)snap->addr, (unsigned)snap->len, (unsigned)snap->frame);
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
		api_queue_json(buf);
		return;
	}
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("MEMSNAP needs slot"); return; }
	slot = (auint)strtoul(tok, NULL, 0);
	if (slot >= API_MEMSNAP_SLOTS){ api_reply_error("MEMSNAP slot out of range"); return; }
	if ((strcmp(sub, "CLEAR") == 0) || (strcmp(sub, "clear") == 0)){
		api_state.memsnap[slot].valid = FALSE;
		api_reply_ok_simple("\"memsnap_cleared\":1");
		return;
	}
	if ((strcmp(sub, "TAKE") == 0) || (strcmp(sub, "take") == 0)){
		auint region;
		auint addr;
		auint len;
		auint size;
		auint i;
		api_memsnap_t* snap = &api_state.memsnap[slot];
		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSNAP TAKE needs region address length"); return; }
		region = api_parse_region(tok);
		if (region == 0xFFFFFFFFU){ api_reply_error("Unknown region"); return; }
		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSNAP TAKE needs address"); return; }
		addr = (auint)strtoul(tok, NULL, 0);
		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSNAP TAKE needs length"); return; }
		len = (auint)strtoul(tok, NULL, 0);
		if (len == 0U){ api_reply_error("MEMSNAP length must be nonzero"); return; }
		if (len > API_MEMSNAP_MAX){ len = API_MEMSNAP_MAX; }
		size = api_region_size(region);
		if (addr >= size){ api_reply_error("Address out of range"); return; }
		if ((addr + len) > size){ len = size - addr; }
		for (i = 0U; i < len; ++i){ snap->data[i] = (uint8)(api_region_read(region, addr + i) & 0xFFU); }
		snap->valid = TRUE;
		snap->region = region;
		snap->addr = addr;
		snap->len = len;
		snap->frame = mainui_get_frame_counter();
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"{\"ok\":1,\"memsnap_taken\":1,\"slot\":%u,\"region\":\"%s\",\"addr\":%u,\"len\":%u,\"frame\":%u}",
			(unsigned)slot, api_region_name(region), (unsigned)addr, (unsigned)len, (unsigned)snap->frame);
		api_queue_json(buf);
		return;
	}
	if ((strcmp(sub, "DIFF") == 0) || (strcmp(sub, "diff") == 0)){
		api_memsnap_t const* snap = &api_state.memsnap[slot];
		auint max_changes = API_MEMSNAP_DIFF_MAX;
		auint i;
		auint changed = 0U;
		auint emitted = 0U;
		if (!snap->valid){ api_reply_error("MEMSNAP slot is empty"); return; }
		tok = api_next_token(&rest);
		if (tok != NULL){ max_changes = (auint)strtoul(tok, NULL, 0); }
		if (max_changes == 0U){ max_changes = API_MEMSNAP_DIFF_MAX; }
		if (max_changes > API_MEMSNAP_DIFF_MAX){ max_changes = API_MEMSNAP_DIFF_MAX; }
		for (i = 0U; i < snap->len; ++i){
			auint current = api_region_read(snap->region, snap->addr + i) & 0xFFU;
			if (current != snap->data[i]){ changed++; }
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"{\"ok\":1,\"slot\":%u,\"region\":\"%s\",\"addr\":%u,\"len\":%u,\"snapshot_frame\":%u,\"current_frame\":%u,\"changed\":%u,\"changes\":[",
			(unsigned)slot, api_region_name(snap->region), (unsigned)snap->addr, (unsigned)snap->len,
			(unsigned)snap->frame, (unsigned)mainui_get_frame_counter(), (unsigned)changed);
		for (i = 0U; (i < snap->len) && (emitted < max_changes); ++i){
			auint current = api_region_read(snap->region, snap->addr + i) & 0xFFU;
			if (current != snap->data[i]){
				pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
					"%s{\"addr\":%u,\"before\":%u,\"after\":%u}",
					(emitted == 0U) ? "" : ",", (unsigned)(snap->addr + i),
					(unsigned)snap->data[i], (unsigned)current);
				emitted++;
			}
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "],\"returned\":%u,\"truncated\":%u}",
			(unsigned)emitted, (unsigned)((changed > emitted) ? 1U : 0U));
		api_queue_json(buf);
		return;
	}
	api_reply_error("MEMSNAP command must be TAKE, DIFF, LIST, or CLEAR");
}


static void api_handle_memscan(char* rest)
{
	char* sub;
	char* tok;
	auint slot;
	char buf[16384];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("MEMSCAN needs START, REFINE, RESULTS, STATUS, or CLEAR"); return; }
	sub = api_next_token(&rest);
	if (sub == NULL){ api_reply_error("MEMSCAN needs START, REFINE, RESULTS, STATUS, or CLEAR"); return; }

	if ((strcmp(sub, "STATUS") == 0) || (strcmp(sub, "status") == 0) ||
	    (strcmp(sub, "LIST") == 0) || (strcmp(sub, "list") == 0)){
		auint i;
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"slots\":[");
		for (i = 0U; i < API_MEMSCAN_SLOTS; ++i){
			api_memscan_t const* scan = &api_state.memscan[i];
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
				"%s{\"slot\":%u,\"valid\":%u,\"region\":\"%s\",\"addr\":%u,\"len\":%u,\"remaining\":%u,\"frame\":%u}",
				(i == 0U) ? "" : ",", (unsigned)i, (unsigned)(scan->valid ? 1U : 0U),
				api_region_name(scan->region), (unsigned)scan->addr, (unsigned)scan->len,
				(unsigned)scan->remaining, (unsigned)scan->frame);
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
		api_queue_json(buf);
		return;
	}

	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("MEMSCAN needs slot"); return; }
	if ((strcmp(sub, "CLEAR") == 0) || (strcmp(sub, "clear") == 0)){
		if ((strcmp(tok, "ALL") == 0) || (strcmp(tok, "all") == 0)){
			auint i;
			for (i = 0U; i < API_MEMSCAN_SLOTS; ++i){ api_state.memscan[i].valid = FALSE; }
			api_reply_ok_simple("\"memscan_cleared\":\"all\"");
			return;
		}
	}
	slot = (auint)strtoul(tok, NULL, 0);
	if (slot >= API_MEMSCAN_SLOTS){ api_reply_error("MEMSCAN slot out of range"); return; }

	if ((strcmp(sub, "CLEAR") == 0) || (strcmp(sub, "clear") == 0)){
		api_state.memscan[slot].valid = FALSE;
		api_reply_ok_simple("\"memscan_cleared\":1");
		return;
	}

	if ((strcmp(sub, "START") == 0) || (strcmp(sub, "start") == 0)){
		api_memscan_t* scan = &api_state.memscan[slot];
		auint region;
		auint addr;
		auint len;
		auint size;
		auint i;
		auint op = API_MEMSCAN_OP_EQ;
		auint value = 0U;
		boole filter = FALSE;

		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSCAN START needs region address length [ANY|value|op value]"); return; }
		region = api_parse_region(tok);
		if (region == 0xFFFFFFFFU){ api_reply_error("Unknown region"); return; }
		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSCAN START needs address"); return; }
		addr = (auint)strtoul(tok, NULL, 0);
		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSCAN START needs length"); return; }
		len = (auint)strtoul(tok, NULL, 0);
		if (len == 0U){ api_reply_error("MEMSCAN length must be nonzero"); return; }
		if (len > API_MEMSCAN_MAX){ len = API_MEMSCAN_MAX; }
		size = api_region_size(region);
		if (addr >= size){ api_reply_error("Address out of range"); return; }
		if (len > (size - addr)){ len = size - addr; }

		tok = api_next_token(&rest);
		if ((tok != NULL) && (strcmp(tok, "ANY") != 0) && (strcmp(tok, "any") != 0)){
			auint parsed_op = api_parse_memscan_op(tok);
			filter = TRUE;
			if (parsed_op != 0xFFFFFFFFU){
				char* value_tok;
				if (!api_memscan_op_needs_value(parsed_op)){ api_reply_error("MEMSCAN START only accepts relational operators"); return; }
				op = parsed_op;
				value_tok = api_next_token(&rest);
				if (value_tok == NULL){ api_reply_error("MEMSCAN START operator needs value"); return; }
				value = (auint)strtoul(value_tok, NULL, 0) & 0xFFU;
			}else{
				op = API_MEMSCAN_OP_EQ;
				value = (auint)strtoul(tok, NULL, 0) & 0xFFU;
			}
		}

		memset(scan->candidates, 0, sizeof(scan->candidates));
		scan->remaining = 0U;
		for (i = 0U; i < len; ++i){
			auint current = api_region_read(region, addr + i) & 0xFFU;
			boole candidate = filter ? api_memscan_compare(op, current, current, value) : TRUE;
			scan->previous[i] = (uint8)current;
			api_memscan_candidate_set(scan, i, candidate);
			if (candidate){ scan->remaining++; }
		}
		scan->valid = TRUE;
		scan->region = region;
		scan->addr = addr;
		scan->len = len;
		scan->frame = mainui_get_frame_counter();
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"{\"ok\":1,\"memscan_started\":1,\"slot\":%u,\"region\":\"%s\",\"addr\":%u,\"len\":%u,\"remaining\":%u,\"frame\":%u,\"initial\":\"%s\"",
			(unsigned)slot, api_region_name(region), (unsigned)addr, (unsigned)len,
			(unsigned)scan->remaining, (unsigned)scan->frame, filter ? api_memscan_op_name(op) : "ANY");
		if (filter){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ",\"value\":%u", (unsigned)value); }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "}");
		api_queue_json(buf);
		return;
	}

	if ((strcmp(sub, "REFINE") == 0) || (strcmp(sub, "refine") == 0)){
		api_memscan_t* scan = &api_state.memscan[slot];
		auint op;
		auint value = 0U;
		auint before;
		auint i;
		if (!scan->valid){ api_reply_error("MEMSCAN slot is empty"); return; }
		tok = api_next_token(&rest);
		if (tok == NULL){ api_reply_error("MEMSCAN REFINE needs operator"); return; }
		op = api_parse_memscan_op(tok);
		if (op == 0xFFFFFFFFU){ api_reply_error("Unknown MEMSCAN operator"); return; }
		if (api_memscan_op_needs_value(op)){
			tok = api_next_token(&rest);
			if (tok == NULL){ api_reply_error("MEMSCAN relational operator needs value"); return; }
			value = (auint)strtoul(tok, NULL, 0) & 0xFFU;
		}
		before = scan->remaining;
		for (i = 0U; i < scan->len; ++i){
			auint current = api_region_read(scan->region, scan->addr + i) & 0xFFU;
			if (api_memscan_candidate_get(scan, i) &&
			    !api_memscan_compare(op, current, scan->previous[i], value)){
				api_memscan_candidate_set(scan, i, FALSE);
				scan->remaining--;
			}
			scan->previous[i] = (uint8)current;
		}
		scan->frame = mainui_get_frame_counter();
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"{\"ok\":1,\"memscan_refined\":1,\"slot\":%u,\"operator\":\"%s\",\"before\":%u,\"remaining\":%u,\"removed\":%u,\"frame\":%u",
			(unsigned)slot, api_memscan_op_name(op), (unsigned)before, (unsigned)scan->remaining,
			(unsigned)(before - scan->remaining), (unsigned)scan->frame);
		if (api_memscan_op_needs_value(op)){
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ",\"value\":%u", (unsigned)value);
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "}");
		api_queue_json(buf);
		return;
	}

	if ((strcmp(sub, "RESULTS") == 0) || (strcmp(sub, "results") == 0)){
		api_memscan_t const* scan = &api_state.memscan[slot];
		auint offset = 0U;
		auint count = 32U;
		auint ordinal = 0U;
		auint emitted = 0U;
		auint i;
		if (!scan->valid){ api_reply_error("MEMSCAN slot is empty"); return; }
		tok = api_next_token(&rest);
		if (tok != NULL){ offset = (auint)strtoul(tok, NULL, 0); }
		tok = api_next_token(&rest);
		if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); }
		if (count == 0U){ count = 32U; }
		if (count > API_MEMSCAN_RESULTS_MAX){ count = API_MEMSCAN_RESULTS_MAX; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"{\"ok\":1,\"slot\":%u,\"region\":\"%s\",\"addr\":%u,\"len\":%u,\"remaining\":%u,\"frame\":%u,\"offset\":%u,\"results\":[",
			(unsigned)slot, api_region_name(scan->region), (unsigned)scan->addr, (unsigned)scan->len,
			(unsigned)scan->remaining, (unsigned)scan->frame, (unsigned)offset);
		for (i = 0U; (i < scan->len) && (emitted < count); ++i){
			auint current;
			if (!api_memscan_candidate_get(scan, i)){ continue; }
			if (ordinal++ < offset){ continue; }
			current = api_region_read(scan->region, scan->addr + i) & 0xFFU;
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
				"%s{\"addr\":%u,\"last\":%u,\"current\":%u}",
				(emitted == 0U) ? "" : ",", (unsigned)(scan->addr + i),
				(unsigned)scan->previous[i], (unsigned)current);
			emitted++;
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"],\"returned\":%u,\"next_offset\":%u,\"truncated\":%u}",
			(unsigned)emitted, (unsigned)(offset + emitted),
			(unsigned)(((offset + emitted) < scan->remaining) ? 1U : 0U));
		api_queue_json(buf);
		return;
	}

	api_reply_error("MEMSCAN command must be START, REFINE, RESULTS, STATUS, or CLEAR");
}

static void api_handle_set_input(char* rest)
{
	char* tok;
	auint player;
	auint buttons;
	auint frames;
	if (rest == NULL){ api_reply_error("SET_INPUT needs player mask frames"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("SET_INPUT needs player"); return; }
	player = (auint)strtoul(tok, NULL, 0);
	if (player >= API_INPUT_PORTS){ api_reply_error("Player out of range"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("SET_INPUT needs mask"); return; }
	buttons = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("SET_INPUT needs frame count"); return; }
	frames = (auint)strtoul(tok, NULL, 0);
	api_input_player_clear(player);
	api_state.input[player].active = (frames != 0U) ? TRUE : FALSE;
	api_state.input[player].buttons = buttons;
	api_state.input[player].frames_remaining = frames;
	if (frames == 0U){ cu_ctr_setsnes(player, 0U); }
	api_reply_ok_simple(NULL);
}

static void api_handle_queue_input(char* rest)
{
	char* tok;
	auint player;
	api_input_override_t* slot;
	if (rest == NULL){ api_reply_error("QUEUE_INPUT needs player and mask/frame pairs"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("QUEUE_INPUT needs player"); return; }
	player = (auint)strtoul(tok, NULL, 0);
	if (player >= API_INPUT_PORTS){ api_reply_error("Player out of range"); return; }
	slot = &api_state.input[player];
	while ((tok = api_next_token(&rest)) != NULL){
		auint buttons;
		auint frames;
		char* tframes;
		if (slot->queue_len >= API_INPUT_QUEUE_CAP){ api_reply_error("QUEUE_INPUT full"); return; }
		buttons = (auint)strtoul(tok, NULL, 0);
		tframes = api_next_token(&rest);
		if (tframes == NULL){ api_reply_error("QUEUE_INPUT needs mask/frame pairs"); return; }
		frames = (auint)strtoul(tframes, NULL, 0);
		slot->queue[slot->queue_len].buttons = buttons;
		slot->queue[slot->queue_len].frames = frames;
		slot->queue_len++;
	}
	{
		char extra[96];
		snprintf(extra, sizeof(extra), "\"player\":%u,\"queued\":%u", (unsigned)player, (unsigned)(slot->queue_len - slot->queue_pos));
		api_reply_ok_simple(extra);
	}
}

static void api_handle_clear_input(char* rest)
{
	char* tok;
	auint player;
	if (rest == NULL){ api_reply_error("CLEAR_INPUT needs player"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("CLEAR_INPUT needs player"); return; }
	player = (auint)strtoul(tok, NULL, 0);
	if (player >= API_INPUT_PORTS){ api_reply_error("Player out of range"); return; }
	api_input_player_clear(player);
	api_reply_ok_simple(NULL);
}

static void api_handle_run_frames(char* rest)
{
	char* tok;
	auint frames;
	if (rest == NULL){ api_reply_error("RUN_FRAMES needs count"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("RUN_FRAMES needs count"); return; }
	frames = (auint)strtoul(tok, NULL, 0);
	api_state.run_frames_remaining = (uint32)frames;
	if (frames != 0U){
		mainui_debug_set_paused(FALSE);
	}
	{
		char extra[64];
		snprintf(extra, sizeof(extra), "\"run_frames_remaining\":%u", (unsigned)api_state.run_frames_remaining);
		api_reply_ok_simple(extra);
	}
}

static void api_handle_wait_frame(char* rest)
{
	char* tok;
	uint32 target;
	uint32 timeout = 0U;
	api_client_t* client = api_active_client();
	api_wait_state_t* wait;
	if (client == NULL){ return; }
	wait = &client->wait;
	if (wait->active){ api_reply_error("Another WAIT command is already pending on this client"); return; }
	if (rest == NULL){ api_reply_error("WAIT_FRAME needs target frame"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WAIT_FRAME needs target frame"); return; }
	target = (uint32)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest);
	if (tok != NULL){ timeout = (uint32)strtoul(tok, NULL, 0); }
	if (mainui_get_frame_counter() >= target){
		char buf[128];
		snprintf(buf, sizeof(buf), "{\"ok\":1,\"wait\":\"frame\",\"frame\":%u,\"target\":%u,\"timeout\":0}",
			(unsigned)mainui_get_frame_counter(), (unsigned)target);
		api_queue_json(buf);
		return;
	}
	wait->active = TRUE;
	wait->type = API_WAIT_FRAME;
	wait->start_frame = mainui_get_frame_counter();
	wait->timeout_frames = timeout;
	wait->frame_target = target;
}

static void api_handle_wait_mem(char* rest)
{
	char* tok;
	auint region;
	auint addr;
	auint op;
	auint value;
	uint32 timeout = 0U;
	auint current;
	api_client_t* client = api_active_client();
	api_wait_state_t* wait;
	if (client == NULL){ return; }
	wait = &client->wait;
	if (wait->active){ api_reply_error("Another WAIT command is already pending on this client"); return; }
	if (rest == NULL){ api_reply_error("WAIT_MEM needs region addr op value [timeout]"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WAIT_MEM needs region"); return; }
	region = api_parse_region(tok);
	if (region == 0xFFFFFFFFU){ api_reply_error("Unknown region"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WAIT_MEM needs address"); return; }
	addr = (auint)strtoul(tok, NULL, 0);
	if (addr >= api_region_size(region)){ api_reply_error("Address out of range"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WAIT_MEM needs operator"); return; }
	op = api_parse_wait_op(tok);
	if (op == 0xFFFFFFFFU){ api_reply_error("Unknown WAIT_MEM operator"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WAIT_MEM needs value"); return; }
	value = (auint)strtoul(tok, NULL, 0) & 0xFFFFU;
	tok = api_next_token(&rest);
	if (tok != NULL){ timeout = (uint32)strtoul(tok, NULL, 0); }
	wait->active = TRUE;
	wait->type = API_WAIT_MEM;
	wait->start_frame = mainui_get_frame_counter();
	wait->timeout_frames = timeout;
	wait->region = region;
	wait->addr = addr;
	wait->op = op;
	wait->value = value;
	if (api_wait_mem_current(wait, &current)){
		api_wait_reply_mem(client, FALSE, current);
	}
}

static void api_handle_read_symbol(char* rest)
{
	char* tok;
	auint kind;
	auint addr;
	char buf[512];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("READ_SYMBOL needs a name"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("READ_SYMBOL needs a name"); return; }
	if (!mainui_debug_find_symbol(tok, &kind, &addr)){
		api_reply_error("Symbol not found");
		return;
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"name\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, tok);
	if (kind == MAINUI_DBG_SYMBOL_PROG){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"kind\":\"prog\",\"addr\":%u,\"value\":%u}", (unsigned)addr, (unsigned)mainui_debug_get_prog_word(addr));
	}else{
		auint region = (addr < 0x100U) ? 0xFFFFFFFEU : MAINUI_DBG_MEM_SRAM;
		auint value = api_region_read(region, addr) & 0xFFU;
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"kind\":\"data\",\"region\":\"%s\",\"addr\":%u,\"value\":%u}", api_region_name(region), (unsigned)addr, (unsigned)value);
	}
	api_queue_json(buf);
}

static void api_handle_write_symbol(char* rest)
{
	char* tok;
	char symname[128];
	auint kind;
	auint addr;
	auint value;
	auint region;
	if (rest == NULL){ api_reply_error("WRITE_SYMBOL needs name and value"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("WRITE_SYMBOL needs name"); return; }
	api_copy_str(symname, sizeof(symname), tok);
	if (!mainui_debug_find_symbol(tok, &kind, &addr)){
		api_reply_error("Symbol not found");
		return;
	}
	if (kind != MAINUI_DBG_SYMBOL_DATA){ api_reply_error("WRITE_SYMBOL only supports data/io symbols"); return; }
	{
		char* tval = api_next_token(&rest);
		if (tval == NULL){ api_reply_error("WRITE_SYMBOL needs value"); return; }
		value = (auint)strtoul(tval, NULL, 0) & 0xFFU;
	}
	region = (addr < 0x100U) ? 0xFFFFFFFEU : MAINUI_DBG_MEM_SRAM;
	api_region_write(region, addr, value);
	{
		char extra[256];
		snprintf(extra, sizeof(extra), "\"name\":\"%s\",\"region\":\"%s\",\"addr\":%u,\"value\":%u", symname, api_region_name(region), (unsigned)addr, (unsigned)value);
		api_reply_ok_simple(extra);
	}
}

static void api_handle_screenshot(char* rest)
{
	char pathbuf[APPCFG_PATH_MAX + 96U];
	char buf[512];
	auint pos = 0U;
	if (rest != NULL){ api_trim(rest); }
	if ((rest == NULL) || (rest[0] == 0)){
		if (!mainui_save_screenshot_auto(pathbuf, (auint)sizeof(pathbuf))){ api_reply_error("Failed to save screenshot"); return; }
	}else{
		if (!mainui_save_screenshot_file(rest)){ api_reply_error("Failed to save screenshot"); return; }
		api_copy_str(pathbuf, sizeof(pathbuf), rest);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"path\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, pathbuf);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	api_queue_json(buf);
}

static void api_handle_screenshot_capture(void)
{
	uint8* data = NULL;
	auint size = 0U, width = 0U, height = 0U;
	char buf[256];
	api_client_t* client = api_active_client();
	if (client == NULL){ api_reply_error("No active API client"); return; }
	api_screenshot_buffer_clear(client);
	if (!mainui_capture_screenshot_bmp(&data, &size, &width, &height) || data == NULL || size == 0U){
		if (data != NULL){ free(data); }
		api_reply_error("Failed to capture screenshot in memory");
		return;
	}
	client->screenshot_data = data;
	client->screenshot_size = size;
	client->screenshot_width = width;
	client->screenshot_height = height;
	snprintf(buf, sizeof(buf), "\"size\":%u,\"width\":%u,\"height\":%u,\"format\":\"bmp24\"",
		(unsigned)size, (unsigned)width, (unsigned)height);
	api_reply_ok_simple(buf);
}

static void api_handle_screenshot_read(char* rest)
{
	char* tok;
	auint off, len = API_SCREENSHOT_READ_MAX, i, pos = 0U;
	char buf[24576];
	api_client_t* client = api_active_client();
	if (client == NULL || client->screenshot_data == NULL || client->screenshot_size == 0U){ api_reply_error("No in-memory screenshot"); return; }
	if (rest == NULL){ api_reply_error("SCREENSHOT_READ needs offset [length]"); return; }
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("SCREENSHOT_READ needs offset"); return; }
	off = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest); if (tok != NULL){ len = (auint)strtoul(tok, NULL, 0); }
	if (len == 0U){ len = 1U; }
	if (len > API_SCREENSHOT_READ_MAX){ len = API_SCREENSHOT_READ_MAX; }
	if (off >= client->screenshot_size){ api_reply_error("Screenshot offset out of range"); return; }
	if (len > (client->screenshot_size - off)){ len = client->screenshot_size - off; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"offset\":%u,\"len\":%u,\"values\":[", (unsigned)off, (unsigned)len);
	for (i = 0U; i < len; ++i){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%u", (i == 0U) ? "" : ",", (unsigned)client->screenshot_data[off + i]); }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}


#ifdef ENABLE_DEBUGGER
static char const* api_timing_event_name(auint type)
{
 switch(type){
  case CU_TIMING_EVT_PORTC: return "PORTC";
  case CU_TIMING_EVT_SYNC: return "SYNC";
  case CU_TIMING_EVT_SPI: return "SPI";
  case CU_TIMING_EVT_UART_TX: return "UART_TX";
  case CU_TIMING_EVT_UART_RX: return "UART_RX";
  case CU_TIMING_EVT_RAM_CS: return "RAM_CS";
  case CU_TIMING_EVT_SD_CS: return "SD_CS";
  case CU_TIMING_EVT_IRQ_ENTER: return "IRQ_ENTER";
  case CU_TIMING_EVT_IRQ_EXIT: return "IRQ_EXIT";
  case CU_TIMING_EVT_T1_COMPA: return "T1_COMPA";
  case CU_TIMING_EVT_T1_COMPB: return "T1_COMPB";
  case CU_TIMING_EVT_T1_OVF: return "T1_OVF";
  case CU_TIMING_EVT_CTR_LATCH: return "CTR_LATCH";
  case CU_TIMING_EVT_CTR_CLOCK: return "CTR_CLOCK";
  case CU_TIMING_EVT_CTR_DATA0: return "CTR_DATA0";
  case CU_TIMING_EVT_CTR_DATA1: return "CTR_DATA1";
  default: return "UNKNOWN";
 }
}

static char const* api_timing_irq_name(auint vector)
{
 auint v=vector;
 if(v>=0x7800U)v-=0x7800U;
 switch(v){
  case 0x0010U:return "WDT";
  case 0x001AU:return "TIMER1_COMPA";
  case 0x001CU:return "TIMER1_COMPB";
  case 0x001EU:return "TIMER1_OVF";
  case 0x0026U:return "SPI_STC";
  case 0x002CU:return "USART0_TX";
  default:return "IRQ";
 }
}

static char const* api_raster_mode_name(auint mode)
{
 if(mode==CU_TIMING_RASTER_SYNC_RISE)return "SYNC_RISE";
 if(mode==CU_TIMING_RASTER_SYNC_FALL)return "SYNC_FALL";
 return "CYCLE";
}

static void api_handle_raster_break(char* rest)
{
 char* op; cu_timing_raster_t r; char buf[448];
 if(rest==NULL){ op=NULL; } else op=api_next_token(&rest);
 if(op==NULL || api_stricmp(op,"STATUS")==0){
  cu_debug_timing_raster_get(&r,FALSE);
  snprintf(buf,sizeof(buf),"\"enabled\":%u,\"mode\":\"%s\",\"any_row\":%u,\"row\":%u,\"cycle\":%u,\"hit\":%u,\"hit_row\":%u,\"hit_cycle\":%u,\"hit_pc\":%u",
   (unsigned)r.enabled,api_raster_mode_name(r.mode),(unsigned)r.any_row,(unsigned)r.row,(unsigned)r.cycle,(unsigned)r.hit,(unsigned)r.hit_row,(unsigned)r.hit_cycle,(unsigned)r.hit_pc);
  api_reply_ok_simple(buf); return;
 }
 if(api_stricmp(op,"CLEAR")==0){ cu_debug_timing_raster_set(FALSE,FALSE,0U,0U); api_reply_ok_simple("\"cleared\":1"); return; }
 if(api_stricmp(op,"HIT_CLEAR")==0){ cu_debug_timing_raster_get(&r,TRUE); api_reply_ok_simple("\"hit_cleared\":1"); return; }
 if(api_stricmp(op,"SET")==0){
  char* row=api_next_token(&rest); char* cyc=api_next_token(&rest); boole any=FALSE; auint rv=0U,cv;
  if(!row||!cyc){ api_reply_error("RASTER_BREAK SET needs <row|ANY> <cycle>"); return; }
  if(api_stricmp(row,"ANY")==0) any=TRUE; else rv=(auint)strtoul(row,NULL,0);
  cv=(auint)strtoul(cyc,NULL,0); if((!any && rv>=CU_TIMING_LINES) || cv>2031U){ api_reply_error("Raster row/cycle out of range"); return; }
  cu_debug_timing_raster_set(TRUE,any,rv,cv); mainui_debug_set_paused(FALSE);
  snprintf(buf,sizeof(buf),"\"armed\":1,\"mode\":\"CYCLE\",\"any_row\":%u,\"row\":%u,\"cycle\":%u",(unsigned)any,(unsigned)rv,(unsigned)cv); api_reply_ok_simple(buf); return;
 }
 if(api_stricmp(op,"EVENT")==0){
  char* ev=api_next_token(&rest); char* row=api_next_token(&rest); boole any=TRUE; auint rv=0U,mode;
  if(!ev){ api_reply_error("RASTER_BREAK EVENT needs <RISE|FALL> [row|ANY]"); return; }
  if(api_stricmp(ev,"RISE")==0 || api_stricmp(ev,"SYNC_RISE")==0)mode=CU_TIMING_RASTER_SYNC_RISE;
  else if(api_stricmp(ev,"FALL")==0 || api_stricmp(ev,"SYNC_FALL")==0 || api_stricmp(ev,"LINE_START")==0)mode=CU_TIMING_RASTER_SYNC_FALL;
  else{ api_reply_error("Raster event must be RISE or FALL"); return; }
  if(row && api_stricmp(row,"ANY")!=0){ any=FALSE; rv=(auint)strtoul(row,NULL,0); if(rv>=CU_TIMING_LINES){ api_reply_error("Raster row out of range"); return; } }
  cu_debug_timing_raster_set_event(TRUE,any,rv,mode); mainui_debug_set_paused(FALSE);
  snprintf(buf,sizeof(buf),"\"armed\":1,\"mode\":\"%s\",\"any_row\":%u,\"row\":%u",api_raster_mode_name(mode),(unsigned)any,(unsigned)rv); api_reply_ok_simple(buf); return;
 }
 api_reply_error("RASTER_BREAK expects STATUS|SET|EVENT|CLEAR|HIT_CLEAR");
}

static void api_sync_uart_logic_capture(void)
{
 uint32 mask=cu_debug_timing_trace_get_mask();
 boole on=cu_debug_timing_trace_enabled();
 cu_uart_debug_logic_capture_set((on && (mask & CU_TIMING_MASK_EVENT(CU_TIMING_EVT_UART_TX)))?TRUE:FALSE,
                                 (on && (mask & CU_TIMING_MASK_EVENT(CU_TIMING_EVT_UART_RX)))?TRUE:FALSE);
}

static void api_handle_logic_trace(char* rest)
{
 char* op; char buf[49152]; auint pos=0U;
 if(rest==NULL)op=NULL; else op=api_next_token(&rest);
 if(op==NULL || api_stricmp(op,"STATUS")==0){
  boole utx=FALSE,urx=FALSE; cu_uart_debug_logic_capture_get(&utx,&urx);
  snprintf(buf,sizeof(buf),"\"enabled\":%u,\"count\":%u,\"first_seq\":%u,\"mask\":%u,\"default_mask\":%u,\"cpu_hz\":%u,\"uart_tx_capture\":%u,\"uart_rx_capture\":%u",
   (unsigned)cu_debug_timing_trace_enabled(),(unsigned)cu_debug_timing_trace_count(),(unsigned)cu_debug_timing_trace_first_seq(),
   (unsigned)cu_debug_timing_trace_get_mask(),(unsigned)CU_TIMING_TRACE_MASK_DEFAULT,(unsigned)CU_TIMING_AVR_HZ,(unsigned)utx,(unsigned)urx); api_reply_ok_simple(buf); return;
 }
 if(api_stricmp(op,"ENABLE")==0){ cu_debug_timing_trace_enable(TRUE); api_sync_uart_logic_capture(); api_reply_ok_simple("\"enabled\":1"); return; }
 if(api_stricmp(op,"DISABLE")==0){ cu_debug_timing_trace_enable(FALSE); api_sync_uart_logic_capture(); api_reply_ok_simple("\"enabled\":0"); return; }
 if(api_stricmp(op,"CLEAR")==0){ cu_debug_timing_trace_clear(); api_reply_ok_simple("\"cleared\":1"); return; }
 if(api_stricmp(op,"MASK")==0){
  char* t=api_next_token(&rest); uint32 mask;
  if(t==NULL){ snprintf(buf,sizeof(buf),"\"mask\":%u,\"default_mask\":%u",(unsigned)cu_debug_timing_trace_get_mask(),(unsigned)CU_TIMING_TRACE_MASK_DEFAULT); api_reply_ok_simple(buf); return; }
  if(api_stricmp(t,"DEFAULT")==0)mask=CU_TIMING_TRACE_MASK_DEFAULT;
  else if(api_stricmp(t,"ALL")==0)mask=CU_TIMING_TRACE_MASK_ALL;
  else if(api_stricmp(t,"NONE")==0)mask=0U;
  else mask=(uint32)strtoul(t,NULL,0);
  cu_debug_timing_trace_set_mask(mask); api_sync_uart_logic_capture();
  snprintf(buf,sizeof(buf),"\"mask\":%u",(unsigned)cu_debug_timing_trace_get_mask()); api_reply_ok_simple(buf); return;
 }
 if(api_stricmp(op,"READ")==0){
  uint32 seq=cu_debug_timing_trace_first_seq(); auint count=256U,i,emitted=0U; char* t=api_next_token(&rest); cu_timing_event_t e;
  if(t){ seq=(uint32)strtoul(t,NULL,0); }
  t=api_next_token(&rest);
  if(t){ count=(auint)strtoul(t,NULL,0); }
  if(count>384U){ count=384U; }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"{\"ok\":1,\"events\":[");
  for(i=0U;i<count;i++,seq++){
   char const* name; if(!cu_debug_timing_trace_get(seq,&e))continue; name=api_timing_event_name(e.type);
   pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"%s{\"seq\":%u,\"abs_cycle\":%u,\"row\":%u,\"cycle\":%u,\"type\":\"%s\",\"value\":%u,\"value2\":%u,\"flags\":%u,\"aux\":%u,\"detail\":%u",
    emitted?",":"",(unsigned)e.seq,(unsigned)e.abs_cycle,(unsigned)e.row,(unsigned)e.beam_cycle,name,(unsigned)e.value,(unsigned)e.value2,(unsigned)e.flags,(unsigned)e.aux,(unsigned)e.detail);
   if(e.type==CU_TIMING_EVT_SPI){
    pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,",\"tx\":%u,\"rx\":%u,\"duration\":%u,\"cpol\":%u,\"cpha\":%u,\"dord\":%u",
     (unsigned)e.value,(unsigned)e.value2,(unsigned)e.aux,(unsigned)((e.flags&CU_TIMING_SPI_CPOL)!=0U),(unsigned)((e.flags&CU_TIMING_SPI_CPHA)!=0U),(unsigned)((e.flags&CU_TIMING_SPI_DORD)!=0U));
   }else if(e.type==CU_TIMING_EVT_UART_TX || e.type==CU_TIMING_EVT_UART_RX){
    auint data_bits=5U+(e.flags&CU_TIMING_UART_DATA_MASK);
    auint stop_bits=(e.flags&CU_TIMING_UART_STOP2)?2U:1U;
    auint parity=(e.flags&CU_TIMING_UART_PARITY_MASK)>>CU_TIMING_UART_PARITY_SHIFT;
    auint frame_bits=1U+data_bits+stop_bits+((parity!=0U)?1U:0U);
    pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,",\"wire_start_cycle\":%u,\"bit_cycles\":%u,\"frame_bits\":%u,\"frame_cycles\":%u,\"data_bits\":%u,\"stop_bits\":%u,\"parity\":%u,\"synchronous\":%u,\"double_speed\":%u,\"error_flags\":%u",
     (unsigned)e.detail,(unsigned)e.aux,(unsigned)frame_bits,(unsigned)(e.aux*frame_bits),(unsigned)data_bits,(unsigned)stop_bits,(unsigned)parity,(unsigned)((e.flags&CU_TIMING_UART_SYNC)!=0U),(unsigned)((e.flags&CU_TIMING_UART_U2X)!=0U),(unsigned)e.value2);
   }else if(e.type==CU_TIMING_EVT_IRQ_ENTER || e.type==CU_TIMING_EVT_IRQ_EXIT){
    pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,",\"vector\":%u,\"depth\":%u,\"irq\":\"%s\"",(unsigned)e.aux,(unsigned)e.value,api_timing_irq_name(e.aux));
   }else if(e.type==CU_TIMING_EVT_T1_COMPA || e.type==CU_TIMING_EVT_T1_COMPB || e.type==CU_TIMING_EVT_T1_OVF){
    pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,",\"timer_value\":%u",(unsigned)e.aux);
   }
   pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"}"); emitted++;
   if(pos>sizeof(buf)-768U){seq++;break;}
  }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"],\"next_seq\":%u,\"count\":%u}",(unsigned)seq,(unsigned)emitted); api_queue_json(buf); return;
 }
 api_reply_error("LOGIC_TRACE expects STATUS|ENABLE|DISABLE|CLEAR|MASK|READ");
}

static void api_handle_beam_history(char* rest)
{
 char* op; char buf[49152]; auint pos=0U;
 if(rest==NULL)op=NULL; else op=api_next_token(&rest);
 if(op==NULL || api_stricmp(op,"STATUS")==0){
  snprintf(buf,sizeof(buf),"\"built\":%u,\"enabled\":%u,\"count\":%u,\"capacity\":%u,\"first_seq\":%u",
   (unsigned)cu_debug_timing_beam_history_built(),(unsigned)cu_debug_timing_beam_history_enabled(),(unsigned)cu_debug_timing_beam_history_count(),(unsigned)CU_TIMING_BEAM_HISTORY_CAP,(unsigned)cu_debug_timing_beam_history_first_seq());
  api_reply_ok_simple(buf); return;
 }
 if(api_stricmp(op,"ENABLE")==0){
  if(!cu_debug_timing_beam_history_built()){ api_reply_error("Beam instruction history not built (ENABLE_BEAM_HISTORY=0)"); return; }
  cu_debug_timing_beam_history_enable(TRUE); api_reply_ok_simple("\"enabled\":1"); return;
 }
 if(api_stricmp(op,"DISABLE")==0){ cu_debug_timing_beam_history_enable(FALSE); api_reply_ok_simple("\"enabled\":0"); return; }
 if(api_stricmp(op,"CLEAR")==0){ cu_debug_timing_beam_history_clear(); api_reply_ok_simple("\"cleared\":1"); return; }
 if(api_stricmp(op,"READ")==0){
  uint32 seq=cu_debug_timing_beam_history_first_seq(); auint count=256U,i,emitted=0U; char* t=api_next_token(&rest); cu_timing_beam_instruction_t e;
  if(t){ seq=(uint32)strtoul(t,NULL,0); }
  t=api_next_token(&rest);
  if(t){ count=(auint)strtoul(t,NULL,0); }
  if(count>512U){ count=512U; }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"{\"ok\":1,\"instructions\":[");
  for(i=0U;i<count;i++,seq++){
   if(!cu_debug_timing_beam_history_get(seq,&e))continue;
   pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"%s{\"seq\":%u,\"abs_start\":%u,\"abs_end\":%u,\"start_row\":%u,\"start_cycle\":%u,\"end_row\":%u,\"end_cycle\":%u,\"pc\":%u,\"cycles\":%u}",
    emitted?",":"",(unsigned)e.seq,(unsigned)e.abs_start,(unsigned)e.abs_end,(unsigned)e.start_row,(unsigned)e.start_cycle,(unsigned)e.end_row,(unsigned)e.end_cycle,(unsigned)e.pc,(unsigned)e.cycles);
   emitted++;
   if(pos>sizeof(buf)-512U){seq++;break;}
  }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"],\"next_seq\":%u,\"count\":%u}",(unsigned)seq,(unsigned)emitted); api_queue_json(buf); return;
 }
 api_reply_error("BEAM_HISTORY expects STATUS|ENABLE|DISABLE|CLEAR|READ");
}

static void api_handle_scanline_profile(char* rest)
{
 char* op; char buf[32768]; auint pos=0U;
 if(rest==NULL)op=NULL; else op=api_next_token(&rest);
 if(op==NULL || api_stricmp(op,"STATUS")==0){ snprintf(buf,sizeof(buf),"\"enabled\":%u,\"frame\":%u",(unsigned)cu_debug_timing_scan_enabled(),(unsigned)cu_debug_timing_scan_frame()); api_reply_ok_simple(buf); return; }
 if(api_stricmp(op,"ENABLE")==0){ cu_debug_timing_scan_enable(TRUE); api_reply_ok_simple("\"enabled\":1"); return; }
 if(api_stricmp(op,"DISABLE")==0){ cu_debug_timing_scan_enable(FALSE); api_reply_ok_simple("\"enabled\":0"); return; }
 if(api_stricmp(op,"CLEAR")==0){ cu_debug_timing_scan_clear(); api_reply_ok_simple("\"cleared\":1"); return; }
 if(api_stricmp(op,"ROWS")==0){
  auint start=0U,count=CU_TIMING_LINES,i; char* t=api_next_token(&rest); cu_timing_line_stat_t st;
  if(t){ start=(auint)strtoul(t,NULL,0); }
  t=api_next_token(&rest);
  if(t){ count=(auint)strtoul(t,NULL,0); }
  if(start>=CU_TIMING_LINES){ start=CU_TIMING_LINES; }
  if(count>CU_TIMING_LINES-start){ count=CU_TIMING_LINES-start; }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"{\"ok\":1,\"frame\":%u,\"rows\":[",(unsigned)cu_debug_timing_scan_frame());
  for(i=0U;i<count;i++){ auint r=start+i; cu_debug_timing_scan_get(r,&st); pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"%s{\"row\":%u,\"valid\":%u,\"line_cycles\":%u,\"slack\":%d,\"overrun\":%u,\"instructions\":%u,\"instruction_cycles\":%u}",i?",":"",(unsigned)r,(unsigned)st.valid,(unsigned)st.line_cycles,(int)st.slack_cycles,(unsigned)st.overrun,(unsigned)st.instructions,(unsigned)st.instruction_cycles); if(pos>sizeof(buf)-512U)break; }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"]}"); api_queue_json(buf); return;
 }
 if(api_stricmp(op,"ROW")==0){
  auint row=0U,i; char* t=api_next_token(&rest); cu_timing_line_stat_t st; if(!t){api_reply_error("SCANLINE_PROFILE ROW needs row");return;} row=(auint)strtoul(t,NULL,0); if(!cu_debug_timing_scan_get(row,&st)){api_reply_error("row out of range");return;}
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"{\"ok\":1,\"row\":%u,\"valid\":%u,\"line_cycles\":%u,\"slack\":%d,\"overrun\":%u,\"instructions\":%u,\"instruction_cycles\":%u,\"top\":[",(unsigned)row,(unsigned)st.valid,(unsigned)st.line_cycles,(int)st.slack_cycles,(unsigned)st.overrun,(unsigned)st.instructions,(unsigned)st.instruction_cycles);
  for(i=0U;i<CU_TIMING_TOP_PC;i++){ if(!st.top_cycles[i])continue; pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"%s{\"pc\":%u,\"cycles\":%u}",(buf[pos-1]=='[')?"":",",(unsigned)st.top_pc[i],(unsigned)st.top_cycles[i]); }
  pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"]}"); api_queue_json(buf); return;
 }
 api_reply_error("SCANLINE_PROFILE expects STATUS|ENABLE|DISABLE|CLEAR|ROWS|ROW");
}

#endif /* ENABLE_DEBUGGER timing tools */

static void api_handle_video_beam(char* rest)
{
	cu_row_t const* row = cu_avr_get_row();
	uint8 const* beam_pixels = cu_avr_get_video_beam_pixels();
	auint raw_cycle = cu_avr_get_video_beam_cycle();
	auint row_pulse = cu_avr_get_video_beam_pulse();
	boole capture_built = cu_avr_video_beam_capture_built();
	boole capture_on = cu_avr_video_beam_capture_enabled();
	auint valid = capture_on ? ((raw_cycle > API_VIDEO_LINE_CYCLES) ? API_VIDEO_LINE_CYCLES : raw_cycle) : 0U;
	auint start = 0U, count = API_VIDEO_LINE_CYCLES, i, pos = 0U;
	char* tok = NULL;
	char buf[16384];
	if (rest != NULL){ tok = api_next_token(&rest); }
	if (tok != NULL){
		if (api_stricmp(tok, "STATUS") == 0){
			char sbuf[256];
			snprintf(sbuf, sizeof(sbuf), "\"built\":%u,\"enabled\":%u,\"cycle\":%u,\"row_pulse\":%u",
				(unsigned)capture_built, (unsigned)capture_on, (unsigned)raw_cycle, (unsigned)row_pulse);
			api_reply_ok_simple(sbuf); return;
		}
		if (api_stricmp(tok, "ENABLE") == 0){
			if (!capture_built){ api_reply_error("Video beam capture not built (ENABLE_DEBUGGER=0 or ENABLE_BEAM_CAPTURE=0)"); return; }
			cu_avr_video_beam_capture_enable(TRUE); api_reply_ok_simple("\"enabled\":1"); return;
		}
		if (api_stricmp(tok, "DISABLE") == 0){ cu_avr_video_beam_capture_enable(FALSE); api_reply_ok_simple("\"enabled\":0"); return; }
		if (api_stricmp(tok, "READ") == 0){ tok = api_next_token(&rest); }
		if (tok != NULL){ start = (auint)strtoul(tok, NULL, 0); tok = api_next_token(&rest); if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); } }
	}
	if (row == NULL || beam_pixels == NULL){ api_reply_error("Video state unavailable"); return; }
	if (start > API_VIDEO_LINE_CYCLES){ start = API_VIDEO_LINE_CYCLES; }
	if (count > (API_VIDEO_LINE_CYCLES - start)){ count = API_VIDEO_LINE_CYCLES - start; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"built\":%u,\"enabled\":%u,\"abs_cycle\":%u,\"line_start_abs\":%u,\"cycle\":%u,\"valid_cycles\":%u,\"line_cycles\":%u,\"hsync_cycles\":%u,\"active_begin\":%u,\"active_cycles\":%u,\"active_end\":%u,\"pulse_counter\":%u,\"row_pulse\":%u,\"last_completed_pulse\":%u,\"start\":%u,\"count\":%u,\"pixels\":[",
		(unsigned)capture_built, (unsigned)capture_on, (unsigned)cu_avr_getcycle(), (unsigned)((auint)(cu_avr_getcycle() - raw_cycle)), (unsigned)raw_cycle, (unsigned)valid, (unsigned)API_VIDEO_LINE_CYCLES,
		(unsigned)API_VIDEO_HSYNC_CYCLES, (unsigned)API_VIDEO_ACTIVE_BEGIN,
		(unsigned)API_VIDEO_ACTIVE_CYCLES, (unsigned)(API_VIDEO_ACTIVE_BEGIN + API_VIDEO_ACTIVE_CYCLES),
		(unsigned)cu_avr_get_video_pulse(), (unsigned)row_pulse, (unsigned)row->pno, (unsigned)start, (unsigned)count);
	if (capture_on){
		for (i = 0U; i < count; ++i){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%u", (i == 0U) ? "" : ",", (unsigned)beam_pixels[start + i]); }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}


static void api_handle_load_rom(char* rest)
{
	char* tok;
	boole run_after = FALSE;
	if (rest == NULL){ api_reply_error("LOAD_ROM needs WAIT/RUN and a path"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("LOAD_ROM needs WAIT/RUN and a path"); return; }
	if ((strcmp(tok, "RUN") == 0) || (strcmp(tok, "run") == 0)){
		run_after = TRUE;
	}else if ((strcmp(tok, "WAIT") == 0) || (strcmp(tok, "wait") == 0)){
		run_after = FALSE;
	}else{
		api_reply_error("LOAD_ROM mode must be WAIT or RUN");
		return;
	}
	api_trim(rest);
	if ((rest == NULL) || (rest[0] == 0)){
		api_reply_error("LOAD_ROM needs a path");
		return;
	}
	if (!mainui_load_rom_file(rest)){
		api_reply_error("Failed to load ROM");
		return;
	}
	api_state.run_frames_remaining = 0U;
	api_wait_cancel_all("WAIT cancelled: ROM changed");
	api_input_clear_all();
	mainui_debug_set_paused(run_after ? FALSE : TRUE);
	api_reply_ok_simple("\"loaded\":1");
}


static void api_handle_source_status(void)
{
	char buf[1536];
	auint pos = 0U;
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"symbols_file\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_symbols_file());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"symbol_count\":%u,\"symbol_status\":\"", (unsigned)mainui_debug_get_symbol_count());
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_symbols_status());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"source_files\":%u,\"source_rows\":%u,\"source_status\":\"",
		(unsigned)mainui_debug_get_source_file_count(), (unsigned)mainui_debug_get_source_row_count());
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_source_status());
#ifdef ENABLE_DEBUGGER
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"dwarf_types\":%u,\"dwarf_variables\":%u,\"dwarf_globals\":%u,\"dwarf_status\":\"",
		(unsigned)cu_debug_dwarf_type_count(), (unsigned)cu_debug_dwarf_variable_count(), (unsigned)cu_debug_dwarf_global_count());
	api_json_escape_append(buf, sizeof(buf), &pos, cu_debug_dwarf_status());
#endif
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	api_queue_json(buf);
}

static boole api_sibling_elf_path(char* out, auint cap)
{
	char const* rom = mainui_get_loaded_rom_path();
	char* dot = NULL;
	char* p;
	if ((out == NULL) || (cap < 6U) || (rom == NULL) || (rom[0] == 0)){ return FALSE; }
	api_copy_str(out, cap, rom);
	for (p = out; *p != 0; ++p){
		if ((*p == '/') || (*p == '\\')){ dot = NULL; }
		else if (*p == '.'){ dot = p; }
	}
	if (dot != NULL){ *dot = 0; }
	if ((strlen(out) + 5U) >= cap){ return FALSE; }
	strcat(out, ".elf");
	return TRUE;
}

static void api_handle_load_symbols(char* rest)
{
	char buf[1536];
	char pathbuf[APPCFG_PATH_MAX + 16U];
	char const* path;
	auint pos = 0U;
	api_trim(rest);
	if ((rest == NULL) || (rest[0] == 0)){ api_reply_error("LOAD_SYMBOLS needs AUTO or an ELF/MAP/symbol path"); return; }
	if ((strcmp(rest, "AUTO") == 0) || (strcmp(rest, "auto") == 0)){
		if (!api_sibling_elf_path(pathbuf, (auint)sizeof(pathbuf))){ api_reply_error("No loaded ROM path available for automatic ELF lookup"); return; }
		path = pathbuf;
	}else{
		path = rest;
	}
	if (!mainui_debug_load_symbols_file(path)){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":0,\"error\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_symbols_status());
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"source_status\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, mainui_debug_get_source_status());
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"attempted_path\":\"");
		api_json_escape_append(buf, sizeof(buf), &pos, path);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
		api_queue_json(buf);
		return;
	}
	api_handle_source_status();
}

static void api_handle_clear_symbols(void)
{
	mainui_debug_clear_symbols();
	api_handle_source_status();
}

static void api_handle_source_list(char* rest)
{
	char* tok;
	auint start = 0U;
	auint count = 32U;
	auint total = mainui_debug_get_source_file_count();
	auint i;
	auint emitted = 0U;
	char buf[32768];
	auint pos = 0U;
	tok = api_next_token(&rest); if (tok != NULL){ start = (auint)strtoul(tok, NULL, 0); }
	tok = api_next_token(&rest); if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); }
	if (count == 0U){ count = 32U; } if (count > 32U){ count = 32U; } if (start > total){ start = total; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"start\":%u,\"count\":%u,\"total\":%u,\"files\":[",
		(unsigned)start, (unsigned)count, (unsigned)total);
	for (i = start; (i < total) && (i < (start + count)) && ((sizeof(buf) - pos) > 1024U); ++i){
		char const* path = mainui_debug_get_source_file(i);
		FILE* f = (path != NULL) ? fopen(path, "r") : NULL;
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s{\"index\":%u,\"available\":%u,\"path\":\"",
			emitted == 0U ? "" : ",", (unsigned)i, (unsigned)(f != NULL ? 1U : 0U));
		if (f != NULL){ fclose(f); }
		api_json_escape_append(buf, sizeof(buf), &pos, (path != NULL) ? path : "");
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
		emitted++;
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_source_at(char* rest)
{
	auint addr;
	mainui_debug_source_t loc;
	char const* path;
	char buf[2048];
	auint pos = 0U;
	if (!api_parse_word_addr_or_pc(&rest, &addr, "SOURCE_AT")){ return; }
	if (!mainui_debug_source_lookup(addr, &loc)){
		char extra[96]; snprintf(extra, sizeof(extra), "\"mapped\":0,\"addr\":%u", (unsigned)addr); api_reply_ok_simple(extra); return;
	}
	path = mainui_debug_get_source_file(loc.file_index);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"mapped\":1,\"addr\":%u,\"row_addr\":%u,\"end_addr\":%u,\"file\":%u,\"line\":%u,\"column\":%u,\"path\":\"",
		(unsigned)addr, (unsigned)loc.word_addr, (unsigned)loc.end_word_addr, (unsigned)loc.file_index, (unsigned)loc.line, (unsigned)loc.column);
	api_json_escape_append(buf, sizeof(buf), &pos, (path != NULL) ? path : "");
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	api_queue_json(buf);
}

static void api_handle_source_read(char* rest)
{
	char* tok;
	auint file_index, start_line, count = 40U, line_no = 0U, emitted = 0U;
	char const* path;
	FILE* f;
	char linebuf[2048];
	char buf[49152];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("SOURCE_READ needs file-index start-line [count]"); return; }
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("SOURCE_READ needs file index"); return; } file_index = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("SOURCE_READ needs start line"); return; } start_line = (auint)strtoul(tok, NULL, 0); if (start_line == 0U){ start_line = 1U; }
	tok = api_next_token(&rest); if (tok != NULL){ count = (auint)strtoul(tok, NULL, 0); } if (count == 0U){ count = 40U; } if (count > 128U){ count = 128U; }
	path = mainui_debug_get_source_file(file_index); if (path == NULL){ api_reply_error("Source file index out of range"); return; }
	f = fopen(path, "r"); if (f == NULL){ api_reply_error("Source file is not available at the DWARF path"); return; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"file\":%u,\"start\":%u,\"path\":\"", (unsigned)file_index, (unsigned)start_line);
	api_json_escape_append(buf, sizeof(buf), &pos, path); pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"lines\":[");
	while ((fgets(linebuf, sizeof(linebuf), f) != NULL) && (emitted < count) && ((sizeof(buf) - pos) > 4096U)){
		size_t n; line_no++; if (line_no < start_line){ continue; }
		n = strlen(linebuf); while ((n != 0U) && ((linebuf[n - 1U] == '\n') || (linebuf[n - 1U] == '\r'))){ linebuf[--n] = 0; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s{\"line\":%u,\"text\":\"", emitted == 0U ? "" : ",", (unsigned)line_no);
		api_json_escape_append(buf, sizeof(buf), &pos, linebuf); pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}"); emitted++;
	}
	fclose(f); pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "],\"count\":%u}", (unsigned)emitted); api_queue_json(buf);
}

static void api_handle_source_break(char* rest)
{
	char* tok;
	auint file_index, line;
	boole enable = TRUE;
	mainui_debug_source_t loc;
	char buf[256];
	if (rest == NULL){ api_reply_error("SOURCE_BREAK needs file-index line [SET|CLEAR]"); return; }
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("SOURCE_BREAK needs file index"); return; } file_index = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("SOURCE_BREAK needs line"); return; } line = (auint)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest);
	if (tok != NULL){
		if ((strcmp(tok, "CLEAR") == 0) || (strcmp(tok, "clear") == 0) || (strcmp(tok, "OFF") == 0) || (strcmp(tok, "off") == 0)){ enable = FALSE; }
		else if (!((strcmp(tok, "SET") == 0) || (strcmp(tok, "set") == 0) || (strcmp(tok, "ON") == 0) || (strcmp(tok, "on") == 0))){ api_reply_error("SOURCE_BREAK mode must be SET or CLEAR"); return; }
	}
	if (!mainui_debug_source_resolve_line(file_index, line, &loc)){ api_reply_error("No executable source row for that file/line"); return; }
	mainui_debug_set_breakpoint(loc.word_addr, enable);
	snprintf(buf, sizeof(buf), "{\"ok\":1,\"set\":%u,\"file\":%u,\"requested_line\":%u,\"line\":%u,\"addr\":%u}",
		(unsigned)(enable ? 1U : 0U), (unsigned)file_index, (unsigned)line, (unsigned)loc.line, (unsigned)loc.word_addr);
	api_queue_json(buf);
}


#ifdef ENABLE_DEBUGGER
static uint8_t api_dwarf_read_data(void* user, uint32_t addr)
{
	(void)user;
	if ((addr & 0xFFFFU) < 0x100U){ return (uint8_t)mainui_debug_get_io_byte((auint)addr); }
	return (uint8_t)mainui_debug_get_sram_byte((auint)addr);
}

static char const* api_dwarf_scope_name(uint32_t scope)
{
	if (scope == CU_DBG_DWARF_SCOPE_PARAM){ return "param"; }
	if (scope == CU_DBG_DWARF_SCOPE_LOCAL){ return "local"; }
	return "global";
}

static char const* api_dwarf_location_name(uint32_t kind)
{
	switch (kind){
		case CU_DBG_DWARF_LOC_ADDRESS: return "address";
		case CU_DBG_DWARF_LOC_REGISTER: return "register";
		case CU_DBG_DWARF_LOC_VALUE: return "value";
		case CU_DBG_DWARF_LOC_OPTIMIZED: return "optimized";
		case CU_DBG_DWARF_LOC_UNSUPPORTED: return "unsupported";
		default: return "none";
	}
}

static uint64_t api_dwarf_value_u(cu_debug_dwarf_value_t const* v)
{
	uint64_t u = 0U;
	uint32_t i;
	uint32_t n = (v->value_size > 8U) ? 8U : v->value_size;
	for (i = 0U; i < n; ++i){ u |= ((uint64_t)v->value[i]) << (8U * i); }
	return u;
}

static int64_t api_dwarf_value_s(cu_debug_dwarf_value_t const* v)
{
	uint64_t u = api_dwarf_value_u(v);
	uint32_t bits = ((v->value_size > 8U) ? 8U : v->value_size) * 8U;
	if ((bits != 0U) && (bits < 64U) && ((u >> (bits - 1U)) & 1U)){ u |= (~0ULL) << bits; }
	return (int64_t)u;
}

static uint32_t api_dwarf_unwrap_type(uint32_t type_id, cu_debug_dwarf_type_info_t* out)
{
	uint32_t guard = 0U;
	cu_debug_dwarf_type_info_t t;
	while ((type_id != 0xFFFFFFFFU) && cu_debug_dwarf_type_info(type_id, &t) && (guard++ < 16U)){
		if ((t.kind != CU_DBG_DWARF_TYPE_TYPEDEF) && (t.kind != CU_DBG_DWARF_TYPE_CONST) && (t.kind != CU_DBG_DWARF_TYPE_VOLATILE)){
			if (out != NULL){ *out = t; }
			return type_id;
		}
		type_id = t.target_type;
	}
	if (out != NULL){ memset(out, 0, sizeof(*out)); out->id = 0xFFFFFFFFU; }
	return 0xFFFFFFFFU;
}

static void api_dwarf_append_value(char* buf, auint cap, auint* pos, cu_debug_dwarf_value_t const* v)
{
	uint32_t i;
	uint64_t u = api_dwarf_value_u(v);
	*pos += (auint)snprintf(buf + *pos, cap - *pos,
		"\"location\":\"%s\",\"address\":%u,\"reg\":%u,\"byte_size\":%u,\"value_size\":%u,\"value_u\":%" PRIu64 ",\"bytes\":[",
		api_dwarf_location_name(v->location_kind), (unsigned)v->address, (unsigned)v->reg,
		(unsigned)v->byte_size, (unsigned)v->value_size, (uint64_t)u);
	for (i = 0U; i < v->value_size && i < CU_DBG_DWARF_VALUE_MAX && (cap - *pos) > 32U; ++i){
		*pos += (auint)snprintf(buf + *pos, cap - *pos, "%s%u", i ? "," : "", (unsigned)v->value[i]);
	}
	*pos += (auint)snprintf(buf + *pos, cap - *pos, "],\"message\":\"");
	api_json_escape_append(buf, cap, pos, v->message);
	*pos += (auint)snprintf(buf + *pos, cap - *pos, "\"");
}

static boole api_dwarf_append_variable(char* buf, auint cap, auint* pos, uint32_t id, uint32_t pc, boole with_members)
{
	cu_debug_dwarf_variable_info_t vi;
	cu_debug_dwarf_value_t vv;
	cu_debug_dwarf_type_info_t ti;
	mainui_debug_cpu_t cpu;
	char type_name[256];
	uint32_t real_type;
	uint32_t mi;
	if (!cu_debug_dwarf_variable_info(id, &vi)){ return FALSE; }
	mainui_debug_get_cpu(&cpu);
	memset(&vv, 0, sizeof(vv));
	(void)cu_debug_dwarf_eval_variable(id, pc, cpu.regs, cpu.sp, api_dwarf_read_data, NULL, &vv);
	if (!cu_debug_dwarf_type_format(vi.type_id, type_name, sizeof(type_name))){ api_copy_str(type_name, sizeof(type_name), "?"); }
	real_type = api_dwarf_unwrap_type(vi.type_id, &ti);
	*pos += (auint)snprintf(buf + *pos, cap - *pos,
		"{\"id\":%u,\"scope\":\"%s\",\"name\":\"", (unsigned)id, api_dwarf_scope_name(vi.scope));
	api_json_escape_append(buf, cap, pos, vi.name);
	*pos += (auint)snprintf(buf + *pos, cap - *pos, "\",\"type_id\":%u,\"type\":\"", (unsigned)vi.type_id);
	api_json_escape_append(buf, cap, pos, type_name);
	*pos += (auint)snprintf(buf + *pos, cap - *pos,
		"\",\"function\":\"");
	api_json_escape_append(buf, cap, pos, vi.function_name);
	*pos += (auint)snprintf(buf + *pos, cap - *pos,
		"\",\"line\":%u,\"low_pc\":%u,\"high_pc\":%u,",
		(unsigned)vi.line, (unsigned)vi.low_word_addr, (unsigned)vi.high_word_addr);
	api_dwarf_append_value(buf, cap, pos, &vv);
	if (real_type != 0xFFFFFFFFU){
		*pos += (auint)snprintf(buf + *pos, cap - *pos, ",\"type_kind\":%u,\"encoding\":%u,\"value_s\":%" PRId64,
			(unsigned)ti.kind, (unsigned)ti.encoding, (int64_t)api_dwarf_value_s(&vv));
		if ((ti.kind == CU_DBG_DWARF_TYPE_ENUM) && (vv.value_size != 0U)){
			uint32_t ei; int64_t sv = api_dwarf_value_s(&vv);
			for (ei = 0U; ei < ti.member_count; ++ei){
				cu_debug_dwarf_member_info_t em;
				if (cu_debug_dwarf_member_info(real_type, ei, &em) && em.is_enumerator && em.const_value == sv){
					*pos += (auint)snprintf(buf + *pos, cap - *pos, ",\"enum_name\":\"");
					api_json_escape_append(buf, cap, pos, em.name);
					*pos += (auint)snprintf(buf + *pos, cap - *pos, "\"");
					break;
				}
			}
		}
	}
	if (with_members && (real_type != 0xFFFFFFFFU) &&
	    ((ti.kind == CU_DBG_DWARF_TYPE_STRUCT) || (ti.kind == CU_DBG_DWARF_TYPE_UNION)) && ti.member_count != 0U){
		*pos += (auint)snprintf(buf + *pos, cap - *pos, ",\"members\":[");
		for (mi = 0U; mi < ti.member_count && mi < 64U && (cap - *pos) > 1024U; ++mi){
			cu_debug_dwarf_member_info_t m;
			cu_debug_dwarf_value_t mv;
			char mt[192];
			uint32_t mtid = 0xFFFFFFFFU;
			if (!cu_debug_dwarf_member_info(real_type, mi, &m)){ continue; }
			memset(&mv, 0, sizeof(mv));
			(void)cu_debug_dwarf_eval_member(id, mi, pc, cpu.regs, cpu.sp, api_dwarf_read_data, NULL, &mv, &mtid);
			(void)cu_debug_dwarf_type_format(m.type_id, mt, sizeof(mt));
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "%s{\"index\":%u,\"name\":\"", mi ? "," : "", (unsigned)mi);
			api_json_escape_append(buf, cap, pos, m.name);
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "\",\"type_id\":%u,\"type\":\"", (unsigned)m.type_id);
			api_json_escape_append(buf, cap, pos, mt);
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "\",\"offset\":%u,", (unsigned)m.byte_offset);
			api_dwarf_append_value(buf, cap, pos, &mv);
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "}");
		}
		*pos += (auint)snprintf(buf + *pos, cap - *pos, "]");
	}
	if (with_members && (real_type != 0xFFFFFFFFU) && (ti.kind == CU_DBG_DWARF_TYPE_ARRAY) && ti.element_count != 0U){
		uint32_t ei;
		*pos += (auint)snprintf(buf + *pos, cap - *pos, ",\"elements\":[");
		for (ei = 0U; ei < ti.element_count && ei < 64U && (cap - *pos) > 768U; ++ei){
			cu_debug_dwarf_value_t ev;
			uint32_t etid = 0xFFFFFFFFU;
			char et[192];
			memset(&ev, 0, sizeof(ev));
			if (!cu_debug_dwarf_eval_element(id, ei, pc, cpu.regs, cpu.sp, api_dwarf_read_data, NULL, &ev, &etid)){ continue; }
			(void)cu_debug_dwarf_type_format(etid, et, sizeof(et));
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "%s{\"index\":%u,\"type_id\":%u,\"type\":\"", ei ? "," : "", (unsigned)ei, (unsigned)etid);
			api_json_escape_append(buf, cap, pos, et);
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "\",");
			api_dwarf_append_value(buf, cap, pos, &ev);
			*pos += (auint)snprintf(buf + *pos, cap - *pos, "}");
		}
		*pos += (auint)snprintf(buf + *pos, cap - *pos, "]");
	}
	*pos += (auint)snprintf(buf + *pos, cap - *pos, "}");
	return TRUE;
}

static uint32_t api_dwarf_parse_pc(char** rest)
{
	char* tok = api_next_token(rest);
	mainui_debug_cpu_t cpu;
	if ((tok == NULL) || (strcmp(tok, "PC") == 0) || (strcmp(tok, "pc") == 0)){
		mainui_debug_get_cpu(&cpu);
		return (uint32_t)cpu.pc;
	}
	return (uint32_t)strtoul(tok, NULL, 0) & 0x7FFFU;
}

static void api_handle_dwarf_status(void)
{
	char buf[1024];
	auint pos = 0U;
	mainui_debug_cpu_t cpu;
	mainui_debug_get_cpu(&cpu);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"types\":%u,\"variables\":%u,\"globals\":%u,\"locals\":%u,\"pc\":%u,\"status\":\"",
		(unsigned)cu_debug_dwarf_type_count(), (unsigned)cu_debug_dwarf_variable_count(),
		(unsigned)cu_debug_dwarf_global_count(), (unsigned)cu_debug_dwarf_local_count((uint32_t)cpu.pc), (unsigned)cpu.pc);
	api_json_escape_append(buf, sizeof(buf), &pos, cu_debug_dwarf_status());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	api_queue_json(buf);
}

static void api_handle_dwarf_vars(char* rest, boole locals)
{
	char* tok;
	uint32_t pc;
	uint32_t offset = 0U, count = 32U, total, ord, id;
	char buf[32768];
	auint pos = 0U;
	if (locals){ pc = api_dwarf_parse_pc(&rest); }
	else{ mainui_debug_cpu_t cpu; mainui_debug_get_cpu(&cpu); pc = (uint32_t)cpu.pc; }
	tok = api_next_token(&rest); if (tok != NULL){ offset = (uint32_t)strtoul(tok, NULL, 0); }
	tok = api_next_token(&rest); if (tok != NULL){ count = (uint32_t)strtoul(tok, NULL, 0); }
	if (count == 0U){ count = 32U; } if (count > 32U){ count = 32U; }
	total = locals ? cu_debug_dwarf_local_count(pc) : cu_debug_dwarf_global_count();
	if (offset > total){ offset = total; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"scope\":\"%s\",\"pc\":%u,\"offset\":%u,\"total\":%u,\"variables\":[",
		locals ? "locals" : "globals", (unsigned)pc, (unsigned)offset, (unsigned)total);
	for (ord = offset; ord < total && ord < offset + count && (sizeof(buf) - pos) > 2048U; ++ord){
		boole ok = locals ? cu_debug_dwarf_local_at(pc, ord, &id) : cu_debug_dwarf_global_at(ord, &id);
		if (!ok){ continue; }
		if (ord != offset){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
		(void)api_dwarf_append_variable(buf, sizeof(buf), &pos, id, pc, FALSE);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_dwarf_value(char* rest)
{
	char* tok;
	uint32_t id, pc;
	char buf[32768];
	auint pos = 0U;
	if (rest == NULL){ api_reply_error("DWARF_VALUE needs variable-id [PC|address]"); return; }
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("DWARF_VALUE needs variable-id"); return; }
	id = (uint32_t)strtoul(tok, NULL, 0);
	pc = api_dwarf_parse_pc(&rest);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"variable\":");
	if (!api_dwarf_append_variable(buf, sizeof(buf), &pos, id, pc, TRUE)){ api_reply_error("Unknown DWARF variable id"); return; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "}");
	api_queue_json(buf);
}

static void api_handle_dwarf_write(char* rest)
{
	char* tok;
	uint32_t id, pc, i, n;
	uint64_t value;
	mainui_debug_cpu_t cpu;
	cu_debug_dwarf_value_t vv;
	if (rest == NULL){ api_reply_error("DWARF_WRITE needs variable-id value [PC|address]"); return; }
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("DWARF_WRITE needs variable-id"); return; }
	id = (uint32_t)strtoul(tok, NULL, 0);
	tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("DWARF_WRITE needs value"); return; }
	value = (uint64_t)strtoull(tok, NULL, 0);
	pc = api_dwarf_parse_pc(&rest);
	if (!mainui_debug_is_paused()){ api_reply_error("Pause emulation before editing DWARF variables"); return; }
	mainui_debug_get_cpu(&cpu);
	memset(&vv, 0, sizeof(vv));
	if (!cu_debug_dwarf_eval_variable(id, pc, cpu.regs, cpu.sp, api_dwarf_read_data, NULL, &vv)){ api_reply_error("Unknown DWARF variable id"); return; }
	n = vv.byte_size;
	if ((n == 0U) || (n > 8U)){ api_reply_error("DWARF_WRITE supports scalar values up to 8 bytes"); return; }
	if (vv.location_kind == CU_DBG_DWARF_LOC_REGISTER){
		if ((vv.reg + n) > 32U){ api_reply_error("DWARF register value spans unsupported registers"); return; }
		for (i = 0U; i < n; ++i){ mainui_debug_set_reg_byte((auint)(vv.reg + i), (auint)((value >> (8U * i)) & 0xFFU)); }
	}else if (vv.location_kind == CU_DBG_DWARF_LOC_ADDRESS){
		for (i = 0U; i < n; ++i){
			uint32_t a = vv.address + i;
			if (a < 0x100U){ mainui_debug_set_io_byte((auint)a, (auint)((value >> (8U * i)) & 0xFFU)); }
			else{ mainui_debug_set_sram_byte((auint)a, (auint)((value >> (8U * i)) & 0xFFU)); }
		}
	}else{
		api_reply_error("DWARF variable is not currently writable (optimized/value/unsupported location)"); return;
	}
	{
		char extra[160];
		snprintf(extra, sizeof(extra), "\"variable\":%u,\"written\":%u,\"value\":%" PRIu64, (unsigned)id, (unsigned)n, (uint64_t)value);
		api_reply_ok_simple(extra);
	}
}

static void api_handle_dwarf_type(char* rest)
{
	char* tok;
	uint32_t id, i;
	cu_debug_dwarf_type_info_t ti;
	char name[256];
	char buf[32768];
	auint pos = 0U;
	if (rest == NULL || (tok = api_next_token(&rest)) == NULL){ api_reply_error("DWARF_TYPE needs type-id"); return; }
	id = (uint32_t)strtoul(tok, NULL, 0);
	if (!cu_debug_dwarf_type_info(id, &ti)){ api_reply_error("Unknown DWARF type id"); return; }
	(void)cu_debug_dwarf_type_format(id, name, sizeof(name));
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"id\":%u,\"kind\":%u,\"name\":\"", (unsigned)id, (unsigned)ti.kind);
	api_json_escape_append(buf, sizeof(buf), &pos, name);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"\",\"byte_size\":%u,\"target_type\":%u,\"element_count\":%u,\"encoding\":%u,\"members\":[",
		(unsigned)ti.byte_size, (unsigned)ti.target_type, (unsigned)ti.element_count, (unsigned)ti.encoding);
	for (i = 0U; i < ti.member_count && i < 128U && (sizeof(buf) - pos) > 512U; ++i){
		cu_debug_dwarf_member_info_t m;
		char mt[192];
		if (!cu_debug_dwarf_member_info(id, i, &m)){ continue; }
		(void)cu_debug_dwarf_type_format(m.type_id, mt, sizeof(mt));
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s{\"index\":%u,\"name\":\"", i ? "," : "", (unsigned)i);
		api_json_escape_append(buf, sizeof(buf), &pos, m.name);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"type_id\":%u,\"type\":\"", (unsigned)m.type_id);
		api_json_escape_append(buf, sizeof(buf), &pos, mt);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"\",\"offset\":%u,\"bit_offset\":%d,\"bit_size\":%u,\"enumerator\":%u,\"const_value\":%" PRId64 "}",
			(unsigned)m.byte_offset, (int)m.bit_offset, (unsigned)m.bit_size, (unsigned)m.is_enumerator, (int64_t)m.const_value);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}
#else
static void api_handle_dwarf_status(void){ api_reply_error("DWARF debugger support is not compiled"); }
static void api_handle_dwarf_vars(char* rest, boole locals){ (void)rest; (void)locals; api_reply_error("DWARF debugger support is not compiled"); }
static void api_handle_dwarf_value(char* rest){ (void)rest; api_reply_error("DWARF debugger support is not compiled"); }
static void api_handle_dwarf_write(char* rest){ (void)rest; api_reply_error("DWARF debugger support is not compiled"); }
static void api_handle_dwarf_type(char* rest){ (void)rest; api_reply_error("DWARF debugger support is not compiled"); }
#endif


#ifdef ENABLE_ESP
static boole api_token_bool(char const* tok, boole* out)
{
	if ((tok == NULL) || (out == NULL)){ return FALSE; }
	if ((strcmp(tok, "1") == 0) || (strcmp(tok, "ON") == 0) || (strcmp(tok, "on") == 0) ||
	    (strcmp(tok, "TRUE") == 0) || (strcmp(tok, "true") == 0) || (strcmp(tok, "YES") == 0) || (strcmp(tok, "yes") == 0)){
		*out = TRUE; return TRUE;
	}
	if ((strcmp(tok, "0") == 0) || (strcmp(tok, "OFF") == 0) || (strcmp(tok, "off") == 0) ||
	    (strcmp(tok, "FALSE") == 0) || (strcmp(tok, "false") == 0) || (strcmp(tok, "NO") == 0) || (strcmp(tok, "no") == 0)){
		*out = FALSE; return TRUE;
	}
	return FALSE;
}


static void api_handle_serial_status(void)
{
	char buf[8192];
	char uart[512];
	auint pos = 0U;
	auint atx = 0U, arx = 0U, btx = 0U, brx = 0U, cfg = 0U;
	cu_esp_tcp_serial_diag_t diag;
	cu_esp_serial_trace_get_uart_status(uart, (auint)sizeof(uart));
	cu_esp_serial_trace_get_counts(&atx, &arx, &btx, &brx, &cfg);
	cu_esp_tcp_serial_diag_get(&diag);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"route\":%u,\"esp_model\":%u,\"at_firmware\":%u,\"uart_profile\":%u,\"host_serial\":\"",
		(unsigned)mainui_get_serial_route(), (unsigned)mainui_get_serial_esp_model(),
		(unsigned)mainui_get_esp_at_firmware_profile(), (unsigned)mainui_get_uart_profile());
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_get_host_serial_device_name());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"host_midi\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_get_host_midi_port_name());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"virtual_midi_mode\":%u,\"virtual_midi_port\":\"", (unsigned)mainui_get_virtual_midi_mode());
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_get_virtual_midi_port_name());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"esp_softap_mode\":%u,\"tcp_host\":\"", (unsigned)mainui_get_esp_softap_mode());
	api_json_escape_append(buf, sizeof(buf), &pos, mainui_get_tcp_serial_host());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"\",\"tcp_port\":%u,\"tcp_mode\":%u,\"tcp_role\":%u,\"tcp_auto_reconnect\":%u,\"tcp_state\":%u,\"tcp_last_error\":%d,"
		"\"trace_enabled\":%u,\"trace_full\":%u,\"trace_events\":%u,\"trace_lines\":%u,\"trace_capacity\":%u,\"trace_dropped\":%u,"
		"\"avr_tx\":%u,\"avr_rx\":%u,\"backend_tx\":%u,\"backend_rx\":%u,\"config_events\":%u,\"uart_status\":\"",
		(unsigned)mainui_get_tcp_serial_port(), (unsigned)mainui_get_tcp_serial_mode(), (unsigned)cu_esp_get_tcp_serial_role(),
		(unsigned)(mainui_get_tcp_serial_auto_reconnect() ? 1U : 0U), (unsigned)mainui_get_tcp_serial_state(),
		(int)mainui_get_tcp_serial_last_error(), (unsigned)(cu_esp_serial_trace_get_enabled() ? 1U : 0U),
		(unsigned)(cu_esp_serial_trace_is_full() ? 1U : 0U), (unsigned)cu_esp_serial_trace_get_event_count(),
		(unsigned)cu_esp_serial_trace_get_line_count(), (unsigned)cu_esp_serial_trace_get_capacity(),
		(unsigned)cu_esp_serial_trace_get_drop_count(), (unsigned)atx, (unsigned)arx, (unsigned)btx, (unsigned)brx, (unsigned)cfg);
	api_json_escape_append(buf, sizeof(buf), &pos, uart);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"\",\"tcp_diag\":{\"avr_to_backend\":%u,\"backend_to_avr\":%u,\"socket_tx\":%u,\"socket_rx\":%u,"
		"\"tx_queue\":%u,\"tx_high_water\":%u,\"rx_queue\":%u,\"rx_high_water\":%u,\"rx_pending\":%u,"
		"\"send_calls\":%u,\"recv_calls\":%u,\"send_would_block\":%u,\"recv_would_block\":%u,"
		"\"send_errors\":%u,\"recv_errors\":%u,\"max_service_gap_ms\":%u,\"gap_over_50ms\":%u,\"gap_over_250ms\":%u,\"log_path\":\"",
		(unsigned)diag.avr_to_backend_bytes, (unsigned)diag.backend_to_avr_bytes,
		(unsigned)diag.socket_tx_bytes, (unsigned)diag.socket_rx_bytes,
		(unsigned)diag.tx_queue_bytes, (unsigned)diag.tx_queue_high_water,
		(unsigned)diag.rx_queue_bytes, (unsigned)diag.rx_queue_high_water,
		(unsigned)diag.socket_rx_pending_bytes, (unsigned)diag.send_calls, (unsigned)diag.recv_calls,
		(unsigned)diag.send_would_block, (unsigned)diag.recv_would_block,
		(unsigned)diag.send_errors, (unsigned)diag.recv_errors,
		(unsigned)diag.max_service_gap_ms, (unsigned)diag.service_gap_over_50ms, (unsigned)diag.service_gap_over_250ms);
	api_json_escape_append(buf, sizeof(buf), &pos, cu_esp_tcp_serial_diag_get_log_path());
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"},\"link\":");
	if (pos < sizeof(buf)){ pos += cu_esp_link_status_json(buf + pos, (auint)(sizeof(buf) - pos)); }
	if (pos + 2U < sizeof(buf)){ buf[pos++] = '}'; buf[pos] = 0; }
	api_queue_json(buf);
}

static void api_handle_serial_set(char* rest)
{
	char* key = api_next_token(&rest);
	char* tok;
	char* value;
	boole b;
	auint v;
	if (key == NULL){ api_reply_error("SERIAL_SET needs a setting and value"); return; }
	api_trim(rest);
	value = (rest != NULL) ? rest : (char*)"";
	/* String-valued endpoints intentionally consume the entire remainder so
	** Windows device labels and MIDI port names may contain spaces. */
	if ((strcmp(key, "HOST_SERIAL") == 0) || (strcmp(key, "host_serial") == 0)){
		mainui_set_host_serial_device_name(value);
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if ((strcmp(key, "HOST_MIDI") == 0) || (strcmp(key, "host_midi") == 0)){
		mainui_set_host_midi_port_name(value);
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if ((strcmp(key, "VIRTUAL_MIDI_PORT") == 0) || (strcmp(key, "virtual_midi_port") == 0)){
		mainui_set_virtual_midi_port_name(value);
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if ((strcmp(key, "TCP_HOST") == 0) || (strcmp(key, "tcp_host") == 0)){
		mainui_set_tcp_serial_host(value);
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if ((api_stricmp(key, "LINK_ROOM") == 0)){
		cu_esp_link_set_room(value); mainui_touch_config();
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if ((api_stricmp(key, "RELAY_HOST") == 0)){
		cu_esp_link_set_relay_host(value); mainui_touch_config();
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if ((api_stricmp(key, "TCP_RESTART") == 0) || (api_stricmp(key, "LINK_RESTART") == 0)){
		/* Re-apply the TCP Serial endpoint with the current mode/room/host. */
		mainui_set_serial_route(CU_ESP_SERIAL_DISCONNECTED);
		mainui_set_serial_route(CU_ESP_SERIAL_TCP_SERIAL);
		api_reply_ok_simple("\"serial_updated\":1"); return;
	}
	if (value[0] == 0){ api_reply_error("SERIAL_SET needs a value"); return; }
	tok = api_next_token(&rest);
	if (tok == NULL){ api_reply_error("SERIAL_SET needs a value"); return; }
	if ((strcmp(key, "ROUTE") == 0) || (strcmp(key, "route") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if (v > CU_ESP_SERIAL_LOOPBACK){ api_reply_error("Serial route out of range"); return; }
		mainui_set_serial_route(v);
	}else if ((strcmp(key, "UART_PROFILE") == 0) || (strcmp(key, "uart_profile") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if (v > CU_UART_PROFILE_DEBUG){ api_reply_error("UART profile out of range"); return; }
		mainui_set_uart_profile(v);
	}else if ((strcmp(key, "ESP_MODEL") == 0) || (strcmp(key, "esp_model") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if ((v < 1U) || (v > 3U)){ api_reply_error("ESP model must be 1..3"); return; }
		mainui_set_serial_esp_model(v);
	}else if ((strcmp(key, "AT_FIRMWARE") == 0) || (strcmp(key, "at_firmware") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if (v > 1U){ api_reply_error("AT firmware profile out of range"); return; }
		mainui_set_esp_at_firmware_profile(v);
	}else if ((strcmp(key, "VIRTUAL_MIDI_MODE") == 0) || (strcmp(key, "virtual_midi_mode") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if ((v < CU_ESP_VIRTUAL_MIDI_INSTRUMENT) || (v > CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)){ api_reply_error("Virtual MIDI mode out of range"); return; }
		mainui_set_virtual_midi_mode(v);
	}else if ((strcmp(key, "SOFTAP_MODE") == 0) || (strcmp(key, "softap_mode") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if (v > MAINUI_ESP_SOFTAP_MODE_ENABLE){ api_reply_error("SoftAP mode out of range"); return; }
		mainui_set_esp_softap_mode(v);
	}else if ((strcmp(key, "TCP_PORT") == 0) || (strcmp(key, "tcp_port") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if ((v == 0U) || (v > 65535U)){ api_reply_error("TCP port out of range"); return; }
		mainui_set_tcp_serial_port(v);
	}else if ((strcmp(key, "TCP_MODE") == 0) || (strcmp(key, "tcp_mode") == 0)){
		v = (auint)strtoul(tok, NULL, 0); if (v > CU_ESP_TCP_SERIAL_MODE_LAN){ api_reply_error("TCP mode out of range (0..4)"); return; }
		mainui_set_tcp_serial_mode(v);
	}else if ((strcmp(key, "TCP_AUTORECONNECT") == 0) || (strcmp(key, "tcp_autoreconnect") == 0)){
		if (!api_token_bool(tok, &b)){ api_reply_error("TCP_AUTORECONNECT needs 0/1 or off/on"); return; }
		mainui_set_tcp_serial_auto_reconnect(b);
	}else if (api_stricmp(key, "RELAY_PORT") == 0){
		v = (auint)strtoul(tok, NULL, 0); if ((v == 0U) || (v > 65535U)){ api_reply_error("Relay port out of range"); return; }
		cu_esp_link_set_relay_port(v); mainui_touch_config();
	}else if (api_stricmp(key, "LAN_CHOOSE") == 0){
		cu_esp_link_lan_choose((uint32)strtoul(tok, NULL, 0));
	}else if ((api_stricmp(key, "IMPAIR") == 0) || (api_stricmp(key, "LATENCY_MS") == 0) || (api_stricmp(key, "JITTER_MS") == 0) ||
	          (api_stricmp(key, "STALL_EVERY_MS") == 0) || (api_stricmp(key, "STALL_MS") == 0) ||
	          (api_stricmp(key, "NOISE_PPM") == 0) || (api_stricmp(key, "DROP_PPM") == 0)){
		cu_esp_link_impair_t im;
		cu_esp_link_get_impair(&im);
		if (api_stricmp(key, "IMPAIR") == 0){
			if (!api_token_bool(tok, &b)){ api_reply_error("IMPAIR needs 0/1 or off/on"); return; }
			im.enabled = b ? 1U : 0U;
		}else{
			v = (auint)strtoul(tok, NULL, 0);
			if (api_stricmp(key, "LATENCY_MS") == 0) im.latency_ms = v;
			else if (api_stricmp(key, "JITTER_MS") == 0) im.jitter_ms = v;
			else if (api_stricmp(key, "STALL_EVERY_MS") == 0) im.stall_every_ms = v;
			else if (api_stricmp(key, "STALL_MS") == 0) im.stall_ms = v;
			else if (api_stricmp(key, "NOISE_PPM") == 0) im.noise_ppm = v;
			else im.drop_ppm = v;
		}
		cu_esp_link_set_impair(&im); mainui_touch_config();
	}else{
		api_reply_error("Unknown SERIAL_SET setting"); return;
	}
	api_reply_ok_simple("\"serial_updated\":1");
}

static void api_handle_serial_trace(char* rest)
{
	char* sub = api_next_token(&rest);
	char* tok;
	auint start, count, total, i, pos = 0U;
	char buf[32768];
	char line[512];
	if ((sub == NULL) || (strcmp(sub, "STATUS") == 0) || (strcmp(sub, "status") == 0)){ api_handle_serial_status(); return; }
	if ((strcmp(sub, "ON") == 0) || (strcmp(sub, "on") == 0)){ cu_esp_serial_trace_set_enabled(TRUE); api_reply_ok_simple("\"trace_enabled\":1"); return; }
	if ((strcmp(sub, "OFF") == 0) || (strcmp(sub, "off") == 0)){ cu_esp_serial_trace_set_enabled(FALSE); api_reply_ok_simple("\"trace_enabled\":0"); return; }
	if ((strcmp(sub, "CLEAR") == 0) || (strcmp(sub, "clear") == 0)){ cu_esp_serial_trace_clear(); api_reply_ok_simple("\"trace_cleared\":1"); return; }
	if ((strcmp(sub, "EXPORT") == 0) || (strcmp(sub, "export") == 0)){
		tok = api_next_token(&rest); if (tok == NULL){ api_reply_error("SERIAL_TRACE EXPORT needs COOKED or RAW"); return; }
		if ((strcmp(tok, "RAW") == 0) || (strcmp(tok, "raw") == 0)){
			if (!cu_esp_serial_trace_export("serial-trace-raw.txt", TRUE)){ api_reply_error("Serial raw trace export failed"); return; }
			api_reply_ok_simple("\"path\":\"serial-trace-raw.txt\""); return;
		}
		if (!cu_esp_serial_trace_export("serial-trace-cooked.txt", FALSE)){ api_reply_error("Serial cooked trace export failed"); return; }
		api_reply_ok_simple("\"path\":\"serial-trace-cooked.txt\""); return;
	}
	if ((strcmp(sub, "READ") == 0) || (strcmp(sub, "read") == 0)){
		total = cu_esp_serial_trace_get_line_count();
		tok = api_next_token(&rest); start = (tok != NULL) ? (auint)strtoul(tok, NULL, 0) : ((total > 200U) ? total - 200U : 0U);
		tok = api_next_token(&rest); count = (tok != NULL) ? (auint)strtoul(tok, NULL, 0) : 200U;
		if (count > 256U){ count = 256U; }
		if (start > total){ start = total; }
		if ((start + count) > total){ count = total - start; }
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"start\":%u,\"count\":%u,\"total\":%u,\"lines\":[", (unsigned)start, (unsigned)count, (unsigned)total);
		for (i = 0U; (i < count) && ((sizeof(buf) - pos) > 700U); ++i){
			cu_esp_serial_trace_format_line(start + i, line, (auint)sizeof(line));
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s\"", (i == 0U) ? "" : ",");
			api_json_escape_append(buf, sizeof(buf), &pos, line);
			pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"");
		}
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
		api_queue_json(buf); return;
	}
	api_reply_error("SERIAL_TRACE needs STATUS, ON, OFF, CLEAR, READ, or EXPORT");
}

static void api_handle_tcp_diag(char* rest)
{
	char* sub = api_next_token(&rest);
	if ((sub == NULL) || (strcmp(sub, "STATUS") == 0) || (strcmp(sub, "status") == 0)){ api_handle_serial_status(); return; }
	if ((strcmp(sub, "RESET") == 0) || (strcmp(sub, "reset") == 0)){ cu_esp_tcp_serial_diag_reset(); api_reply_ok_simple("\"tcp_diag_reset\":1"); return; }
	if ((strcmp(sub, "FLUSH") == 0) || (strcmp(sub, "flush") == 0)){ cu_esp_tcp_serial_diag_flush_log(); api_reply_ok_simple("\"tcp_diag_flushed\":1"); return; }
	api_reply_error("TCP_DIAG needs STATUS, RESET, or FLUSH");
}

static void api_handle_esp_status(void)
{
	cu_state_esp_t const* es = cu_esp_get_state();
	char buf[16384];
	auint pos = 0U, i;
	if (es == NULL){ api_reply_error("ESP state unavailable"); return; }
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"state\":%u,\"ready\":%u,\"flash_dirty\":%u,\"emulation_model\":%u,\"user_input_mode\":%u,"
		"\"baud_rate\":%u,\"uart_profile\":%u,\"uart_overrun\":%u,\"uart_rx_error_flags\":%u,"
		"\"active_socket\":%u,\"send_socket\":%u,\"cip_mode\":%u,\"cipbuf_mode\":%u,\"rx_packet_bytes\":%u,\"rx_await_bytes\":%u,\"rx_await_time\":%u,"
		"\"wifi_mode\":%u,\"wifi_ssid\":\"",
		(unsigned)es->state, (unsigned)es->ready, (unsigned)es->flash_dirty, (unsigned)es->emulation_model,
		(unsigned)es->user_input_mode, (unsigned)es->baud_rate, (unsigned)es->uart_profile,
		(unsigned)es->uart_overrun, (unsigned)es->uart_rx_error_flags, (unsigned)es->active_socket,
		(unsigned)es->send_to_socket, (unsigned)es->cip_mode, (unsigned)es->cipbuf_recv_mode,
		(unsigned)es->rx_packet_bytes, (unsigned)es->rx_await_bytes, (unsigned)es->rx_await_time,
		(unsigned)es->wifi_mode);
	api_json_escape_append(buf, sizeof(buf), &pos, (char const*)es->wifi_name);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"wifi_rssi\":%d,\"wifi_channel\":%u,\"wifi_join_pending\":%u,\"station_ip\":\"", (int)es->wifi_rssi, (unsigned)es->wifi_channel, (unsigned)es->wifi_join_pending);
	api_json_escape_append(buf, sizeof(buf), &pos, (char const*)es->station_ip);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"station_gateway\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, (char const*)es->station_gateway);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"station_netmask\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, (char const*)es->station_netmask);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"softap_ssid\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, (char const*)es->soft_ap_name);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"softap_ip\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, (char const*)es->soft_ap_ip);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"\",\"lan_enabled\":%u,\"lan_joined\":%u,\"dns_pending\":%u,\"ping_pending\":%u,\"udp_resolve_pending\":%u,\"server_enabled\":%u,\"server_port\":%u,\"links\":[",
		(unsigned)es->lan.enable, (unsigned)es->lan.joined, (unsigned)es->dns_pending, (unsigned)es->ping_pending,
		(unsigned)es->udp_send_resolve_pending, (unsigned)es->server_enabled, (unsigned)es->listen_port);
	for (i = 0U; i < (auint)ESP_MAX_LINKS; ++i){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
			"%s{\"id\":%u,\"open\":%u,\"state\":%u,\"type\":%u,\"port\":%u,\"error\":%d,\"ready_ms\":%u,\"host\":\"",
			(i == 0U) ? "" : ",", (unsigned)i, (unsigned)((es->socks[i] == ESP_INVALID_SOCKET) ? 0U : 1U),
			(unsigned)es->link_state[i], (unsigned)es->link_type[i], (unsigned)es->link_port[i], (int)es->link_err[i], (unsigned)es->link_ready_ms[i]);
		api_json_escape_append(buf, sizeof(buf), &pos, es->link_host[i]);
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}
#endif

#ifdef ENABLE_NETPLAY
static void api_handle_netplay_status(void)
{
	netplay_status_t st;
	mainui_netplay_runtime_t rt;
	char buf[8192];
	auint pos = 0U;
	netplay_get_status(&st);
	mainui_get_netplay_runtime(&rt);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"enabled\":%u,\"connected\":%u,\"relay_mode\":%u,\"relay_ready\":%u,\"relay_peer_present\":%u,\"direct_established\":%u,"
		"\"hello_acked\":%u,\"compat_ok\":%u,\"session_started\":%u,\"local_ready\":%u,\"peer_ready\":%u,"
		"\"ping_ms\":%u,\"ping_smoothed_ms\":%u,\"ping_jitter_ms\":%u,\"rx_dup\":%u,\"rx_ooo\":%u,\"rx_gap\":%u,"
		"\"local_port\":%u,\"peer_port\":%u,\"peer_host\":\"",
		(unsigned)st.enabled, (unsigned)st.connected, (unsigned)st.relay_mode, (unsigned)st.relay_room_ready,
		(unsigned)st.relay_peer_present, (unsigned)st.relay_direct_established, (unsigned)st.hello_acked,
		(unsigned)st.compat_ok, (unsigned)st.session_started, (unsigned)st.local_ready, (unsigned)st.peer_ready,
		(unsigned)st.ping_ms, (unsigned)st.ping_smoothed_ms, (unsigned)st.ping_jitter_ms,
		(unsigned)st.rx_dup_count, (unsigned)st.rx_ooo_count, (unsigned)st.rx_gap_count,
		(unsigned)st.local_port, (unsigned)st.peer_port);
	api_json_escape_append(buf, sizeof(buf), &pos, st.peer_host);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"network_interface\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, st.network_interface_label);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"local_address\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, st.active_local_addr);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"notice\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, st.notice);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"compat_reason\":\"");
	api_json_escape_append(buf, sizeof(buf), &pos, st.compat_reason);
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"\",\"runtime\":{\"active\":%u,\"predicted\":%u,\"stalled\":%u,\"resim_applied\":%u,\"frame\":%u,\"rollback_window\":%u,\"input_delay\":%u,\"resim_from\":%u,\"resim_to\":%u}}",
		(unsigned)rt.active, (unsigned)rt.predicted_any, (unsigned)rt.stalled, (unsigned)rt.resim_applied,
		(unsigned)rt.frame, (unsigned)rt.rollback_window, (unsigned)rt.input_delay,
		(unsigned)rt.resim_from_frame, (unsigned)rt.resim_to_frame);
	api_queue_json(buf);
}
#endif


static void api_handle_audio_scope_status(void)
{
	audio_scope_status_t st;
	char buf[512];
	audio_scope_get_status(&st);
	snprintf(buf, sizeof(buf),
		"{\"ok\":1,\"output_capture\":%u,\"source_rate\":%u,\"output_rate\":%u,\"output_format\":%u,"
		"\"output_channels\":%u,\"output_count\":%u,\"output_capacity\":%u,\"output_frames_total\":%" PRIu64 ","
		"\"rail_left_total\":%" PRIu64 ",\"rail_right_total\":%" PRIu64 ",\"peak_left\":%u,\"peak_right\":%u}",
		(unsigned)st.output_capture_enabled, (unsigned)st.source_rate, (unsigned)st.output_rate,
		(unsigned)st.output_format, (unsigned)st.output_channels, (unsigned)st.output_count,
		(unsigned)AUDIO_SCOPE_OUTPUT_CAP, (uint64_t)st.output_frames_total,
		(uint64_t)st.output_rail_left, (uint64_t)st.output_rail_right,
		(unsigned)st.output_peak_left, (unsigned)st.output_peak_right);
	api_queue_json(buf);
}

static void api_handle_audio_scope_source(char* rest)
{
	uint8 data[4096];
	char buf[24576];
	char* tok = api_next_token(&rest);
	auint want = tok ? (auint)strtoul(tok, NULL, 0) : 2048U;
	auint n;
	auint i;
	auint pos = 0U;
	auint lo = 255U, hi = 0U;
	auint rail_lo = 0U, rail_hi = 0U;
	auint run = 0U, run_max = 0U;
	uint64_t sum = 0ULL;
	uint64_t sum_sq = 0ULL;
	if (want == 0U){ want = 1U; }
	if (want > 4096U){ want = 4096U; }
	n = audio_scope_copy_source(data, want);
	for (i = 0U; i < n; i++){
		auint v = data[i];
		if (v < lo){ lo = v; }
		if (v > hi){ hi = v; }
		if (v == 0U){ rail_lo++; }
		if (v == 255U){ rail_hi++; }
		if ((v == 0U) || (v == 255U)){ run++; if (run > run_max){ run_max = run; } }else{ run = 0U; }
		sum += v;
		{ int64_t d = (int64_t)v - 128LL; sum_sq += (uint64_t)(d * d); }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"kind\":\"source\",\"rate\":%u,\"count\":%u,\"min\":%u,\"max\":%u,"
		"\"rail_low\":%u,\"rail_high\":%u,\"rail_run_max\":%u,\"mean_x1000\":%u,\"rms_centered_x1000\":%u,\"samples\":[",
		(unsigned)audio_getfreq(), (unsigned)n, (unsigned)((n != 0U) ? lo : 128U),
		(unsigned)((n != 0U) ? hi : 128U), (unsigned)rail_lo, (unsigned)rail_hi, (unsigned)run_max,
		(unsigned)((n != 0U) ? ((sum * 1000ULL) / n) : 128000ULL),
		(unsigned)((n != 0U) ? (sqrt((double)sum_sq / (double)n) * 1000.0) : 0.0));
	for (i = 0U; i < n && pos + 8U < sizeof(buf); i++){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%u", (i == 0U) ? "" : ",", (unsigned)data[i]);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_audio_scope_output(char* rest)
{
	sint16 left[1024];
	sint16 right[1024];
	char buf[32768];
	char* tok = api_next_token(&rest);
	auint want = tok ? (auint)strtoul(tok, NULL, 0) : 1024U;
	auint n;
	auint i;
	auint pos = 0U;
	uint64_t sumsq_l = 0ULL, sumsq_r = 0ULL;
	auint run_l = 0U, run_r = 0U, runmax_l = 0U, runmax_r = 0U;
	audio_scope_status_t st;
	if (want == 0U){ want = 1U; }
	if (want > 1024U){ want = 1024U; }
	audio_scope_get_status(&st);
	if (!st.output_capture_enabled){ api_reply_error("AUDIO_SCOPE output capture is not enabled"); return; }
	n = audio_scope_copy_output(left, right, want);
	for (i = 0U; i < n; i++){
		int64_t l = left[i], r = right[i];
		sumsq_l += (uint64_t)(l * l); sumsq_r += (uint64_t)(r * r);
		if ((left[i] <= -32768) || (left[i] >= 32767)){ run_l++; if (run_l > runmax_l){ runmax_l = run_l; } }else{ run_l = 0U; }
		if ((right[i] <= -32768) || (right[i] >= 32767)){ run_r++; if (run_r > runmax_r){ runmax_r = run_r; } }else{ run_r = 0U; }
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
		"{\"ok\":1,\"kind\":\"output\",\"rate\":%u,\"channels\":%u,\"count\":%u,"
		"\"rail_left_total\":%" PRIu64 ",\"rail_right_total\":%" PRIu64 ",\"peak_left\":%u,\"peak_right\":%u,"
		"\"rms_left_x1000\":%u,\"rms_right_x1000\":%u,\"rail_run_left_max\":%u,\"rail_run_right_max\":%u,\"left\":[",
		(unsigned)st.output_rate, (unsigned)st.output_channels, (unsigned)n,
		(uint64_t)st.output_rail_left, (uint64_t)st.output_rail_right,
		(unsigned)st.output_peak_left, (unsigned)st.output_peak_right,
		(unsigned)((n != 0U) ? (sqrt((double)sumsq_l / (double)n) * 1000.0) : 0.0),
		(unsigned)((n != 0U) ? (sqrt((double)sumsq_r / (double)n) * 1000.0) : 0.0),
		(unsigned)runmax_l, (unsigned)runmax_r);
	for (i = 0U; i < n && pos + 12U < sizeof(buf); i++){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%d", (i == 0U) ? "" : ",", (int)left[i]);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "],\"right\":[");
	for (i = 0U; i < n && pos + 12U < sizeof(buf); i++){
		pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%s%d", (i == 0U) ? "" : ",", (int)right[i]);
	}
	pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
	api_queue_json(buf);
}

static void api_handle_audio_scope(char* rest)
{
	char* op = api_next_token(&rest);
	if ((op == NULL) || (api_stricmp(op, "STATUS") == 0)){ api_handle_audio_scope_status(); return; }
	if (api_stricmp(op, "ENABLE") == 0){ audio_scope_output_enable(TRUE); api_handle_audio_scope_status(); return; }
	if (api_stricmp(op, "DISABLE") == 0){ audio_scope_output_enable(FALSE); api_handle_audio_scope_status(); return; }
	if (api_stricmp(op, "CLEAR") == 0){ audio_scope_clear(); api_handle_audio_scope_status(); return; }
	if (api_stricmp(op, "SOURCE") == 0){ api_handle_audio_scope_source(rest); return; }
	if (api_stricmp(op, "OUTPUT") == 0){ api_handle_audio_scope_output(rest); return; }
	api_reply_error("AUDIO_SCOPE needs STATUS|ENABLE|DISABLE|CLEAR|SOURCE [count]|OUTPUT [count]");
}


static char const* api_audio_break_name(auint mode)
{
 switch (mode){
  case CU_AVR_AUDIO_BREAK_RAIL: return "RAIL";
  case CU_AVR_AUDIO_BREAK_OUTSIDE: return "OUTSIDE";
  default: return "OFF";
 }
}

static void api_handle_audio_debug_status(void)
{
 cu_avr_audio_event_t hit;
 boole hv = cu_avr_audio_break_hit(FALSE, &hit);
 char buf[1024];
 snprintf(buf, sizeof(buf),
  "{\"ok\":1,\"built\":%u,\"enabled\":%u,\"count\":%u,\"capacity\":%u,\"first_seq\":%u,"
  "\"event_total\":%" PRIu64 ",\"rail_low_total\":%" PRIu64 ",\"rail_high_total\":%" PRIu64 ",\"peak_distance\":%u,"
  "\"break_mode\":\"%s\",\"break_low\":%u,\"break_high\":%u,\"hit\":%u,"
  "\"hit_seq\":%u,\"hit_cycle\":%u,\"hit_pc\":%u,\"hit_row\":%u,\"hit_beam_cycle\":%u,\"hit_value\":%u}",
  (unsigned)cu_avr_audio_trace_built(), (unsigned)cu_avr_audio_trace_enabled(),
  (unsigned)cu_avr_audio_trace_count(), (unsigned)CU_AVR_AUDIO_TRACE_CAP,
  (unsigned)cu_avr_audio_trace_first_seq(), (uint64_t)cu_avr_audio_event_total(),
  (uint64_t)cu_avr_audio_rail_low_total(), (uint64_t)cu_avr_audio_rail_high_total(),
  (unsigned)cu_avr_audio_peak_distance(), api_audio_break_name(cu_avr_audio_break_mode()),
  (unsigned)cu_avr_audio_break_low(), (unsigned)cu_avr_audio_break_high(), (unsigned)hv,
  (unsigned)(hv ? hit.seq : 0U), (unsigned)(hv ? hit.abs_cycle : 0U), (unsigned)(hv ? hit.pc : 0U),
  (unsigned)(hv ? hit.row : 0U), (unsigned)(hv ? hit.beam_cycle : 0U), (unsigned)(hv ? hit.value : 0U));
 api_queue_json(buf);
}

static void api_handle_audio_debug_read(char* rest)
{
 char* a = api_next_token(&rest);
 char* b = api_next_token(&rest);
 uint32 seq = a ? (uint32)strtoul(a, NULL, 0) : cu_avr_audio_trace_first_seq();
 auint want = b ? (auint)strtoul(b, NULL, 0) : 128U;
 char buf[32768];
 auint pos = 0U, n = 0U;
 cu_avr_audio_event_t e;
 if (want > 256U){ want = 256U; }
 pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"events\":[");
 while ((n < want) && cu_avr_audio_trace_get(seq, &e) && (pos + 220U < sizeof(buf))){
  pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
   "%s{\"seq\":%u,\"cycle\":%u,\"pc\":%u,\"row\":%u,\"beam_cycle\":%u,\"value\":%u,\"flags\":%u,\"rail_low\":%u,\"rail_high\":%u,\"break\":%u}",
   n ? "," : "", (unsigned)e.seq, (unsigned)e.abs_cycle, (unsigned)e.pc, (unsigned)e.row,
   (unsigned)e.beam_cycle, (unsigned)e.value, (unsigned)e.flags,
   (unsigned)((e.flags & CU_AVR_AUDIO_FLAG_RAIL_LOW) != 0U),
   (unsigned)((e.flags & CU_AVR_AUDIO_FLAG_RAIL_HIGH) != 0U),
   (unsigned)((e.flags & CU_AVR_AUDIO_FLAG_BREAK) != 0U));
  seq++; n++;
 }
 pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "] ,\"next_seq\":%u,\"count\":%u}", (unsigned)seq, (unsigned)n);
 api_queue_json(buf);
}

static void api_handle_audio_debug(char* rest)
{
 char* op = api_next_token(&rest);
 if ((op == NULL) || (api_stricmp(op, "STATUS") == 0)){ api_handle_audio_debug_status(); return; }
 if (!cu_avr_audio_trace_built()){ api_reply_error("AUDIO_DEBUG not built; set FLAG_AUDIO_TRACE=1"); return; }
 if (api_stricmp(op, "ENABLE") == 0){ cu_avr_audio_trace_enable(TRUE); api_handle_audio_debug_status(); return; }
 if (api_stricmp(op, "DISABLE") == 0){ cu_avr_audio_trace_enable(FALSE); api_handle_audio_debug_status(); return; }
 if (api_stricmp(op, "CLEAR") == 0){ cu_avr_audio_trace_clear(); api_handle_audio_debug_status(); return; }
 if (api_stricmp(op, "READ") == 0){ api_handle_audio_debug_read(rest); return; }
 if (api_stricmp(op, "HIT") == 0){
  char* x = api_next_token(&rest); if (x && api_stricmp(x, "CLEAR") == 0){ (void)cu_avr_audio_break_hit(TRUE, NULL); }
  api_handle_audio_debug_status(); return;
 }
 if (api_stricmp(op, "BREAK") == 0){
  char* mode = api_next_token(&rest);
  if ((mode == NULL) || (api_stricmp(mode, "OFF") == 0)){ cu_avr_audio_break_set(CU_AVR_AUDIO_BREAK_OFF,0U,255U); api_handle_audio_debug_status(); return; }
  if (api_stricmp(mode, "RAIL") == 0){ cu_avr_audio_break_set(CU_AVR_AUDIO_BREAK_RAIL,0U,255U); api_handle_audio_debug_status(); return; }
  if (api_stricmp(mode, "OUTSIDE") == 0){
   char* lo = api_next_token(&rest); char* hi = api_next_token(&rest);
   if ((lo == NULL) || (hi == NULL)){ api_reply_error("AUDIO_DEBUG BREAK OUTSIDE needs <low> <high>"); return; }
   cu_avr_audio_break_set(CU_AVR_AUDIO_BREAK_OUTSIDE,(auint)strtoul(lo,NULL,0),(auint)strtoul(hi,NULL,0)); api_handle_audio_debug_status(); return;
  }
 }
 api_reply_error("AUDIO_DEBUG needs STATUS|ENABLE|DISABLE|CLEAR|READ [SEQ] [COUNT]|BREAK OFF|RAIL|OUTSIDE <LOW> <HIGH>|HIT [CLEAR]");
}

static char const* api_sd_preset_name(auint preset)
{
    switch (preset){
        case CU_SPISD_PRESET_SLOW: return "SLOW";
        case CU_SPISD_PRESET_NORMAL: return "NORMAL";
        case CU_SPISD_PRESET_FAST: return "FAST";
        default: return "CUSTOM";
    }
}

static char const* api_sd_state_name(auint state)
{
    switch (state){
        case 0U: return "UNINITIALIZED";
        case 1U: return "NATIVE INIT PULSES";
        case 2U: return "NATIVE";
        case 3U: return "IDLE";
        case 4U: return "VERIFIED";
        case 5U: return "INITIALIZING";
        case 6U: return "AVAILABLE";
        case 7U: return "READ SINGLE";
        case 8U: return "READ MULTI";
        case 9U: return "WRITE SINGLE";
        case 10U: return "WRITE MULTI";
        default: return "UNKNOWN";
    }
}

static char const* api_sd_packet_state_name(auint state)
{
    switch (state){
        case 0U: return "IDLE";
        case 1U: return "READ WAIT/TOKEN";
        case 2U: return "READ DATA";
        case 3U: return "READ CRC";
        case 4U: return "WRITE1 WAIT TOKEN";
        case 5U: return "WRITE1 DATA";
        case 6U: return "WRITE1 CRC";
        case 7U: return "WRITEM WAIT TOKEN";
        case 8U: return "WRITEM DATA";
        case 9U: return "WRITEM CRC";
        case 10U: return "WRITE BUSY";
        default: return "UNKNOWN";
    }
}

static char const* api_sd_command_phase_name(auint cmd)
{
    if ((cmd & 0x400U) != 0U){ return "EXTRA RESPONSE"; }
    if ((cmd & 0x100U) != 0U){
        if ((cmd & 0x200U) != 0U){ return "RESPONSE WAIT"; }
        return "RECEIVING";
    }
    if ((cmd & 0x040U) != 0U){ return "WAIT APP COMMAND"; }
    return "IDLE";
}

static char const* api_sd_command_name(auint cmd, boole app)
{
    if (app){
        switch (cmd){
            case 23U: return "ACMD23 PRE-ERASE";
            case 41U: return "ACMD41 INIT";
            default: return "ACMD";
        }
    }
    switch (cmd){
        case 0U: return "CMD0 GO_IDLE";
        case 1U: return "CMD1 INIT";
        case 8U: return "CMD8 IF_COND";
        case 12U: return "CMD12 STOP";
        case 13U: return "CMD13 STATUS";
        case 16U: return "CMD16 BLOCKLEN";
        case 17U: return "CMD17 READ1";
        case 18U: return "CMD18 READM";
        case 24U: return "CMD24 WRITE1";
        case 25U: return "CMD25 WRITEM";
        case 55U: return "CMD55 APP";
        case 58U: return "CMD58 OCR";
        case 59U: return "CMD59 CRC";
        default: return "CMD";
    }
}

static void api_sd_r1_flags(char* out, auint cap, auint r1)
{
    auint pos = 0U;
    struct { auint bit; char const* name; } const f[] = {
        {0x01U,"IDLE"},{0x02U,"ERASE_RESET"},{0x04U,"ILLEGAL"},{0x08U,"CRC"},
        {0x10U,"ERASE_SEQ"},{0x20U,"ADDRESS"},{0x40U,"PARAM"}
    };
    auint i;
    if (cap == 0U){ return; }
    out[0] = 0;
    if ((r1 & 0x7FU) == 0U){ snprintf(out, cap, "OK"); return; }
    for (i = 0U; i < (sizeof(f) / sizeof(f[0])); ++i){
        if ((r1 & f[i].bit) != 0U){
            pos += (auint)snprintf(out + pos, (pos < cap) ? (cap - pos) : 0U, "%s%s", pos ? "|" : "", f[i].name);
            if (pos >= cap){ break; }
        }
    }
}

static void api_sd_model_json(char* buf, auint cap, auint* pos, cu_spisd_model_t const* m)
{
    *pos += (auint)snprintf(buf + *pos, cap - *pos,
        "{\"preset\":%u,\"preset_name\":\"%s\",\"init_ms\":%u,\"cmd_wait_bytes\":%u,\"read_wait_bytes\":%u,"
        "\"write_busy_ms\":%u,\"cs_high_ms\":%u,\"init_min_byte_cycles\":%u,\"init_max_byte_cycles\":%u}",
        (unsigned)m->preset, api_sd_preset_name(m->preset), (unsigned)m->init_ms,
        (unsigned)m->cmd_wait_bytes, (unsigned)m->read_wait_bytes, (unsigned)m->write_busy_ms,
        (unsigned)m->cs_high_ms, (unsigned)m->init_min_byte_cycles, (unsigned)m->init_max_byte_cycles);
}

static void api_sd_protocol_json(char* buf, auint cap, auint* pos, cu_state_spisd_t const* st, cu_spisd_model_t const* model, auint cycle)
{
    auint cmd = st->cmd;
    boole receiving = ((cmd & 0x100U) != 0U) ? TRUE : FALSE;
    boole response_wait = (receiving && ((cmd & 0x200U) != 0U)) ? TRUE : FALSE;
    boole extra_response = ((cmd & 0x400U) != 0U) ? TRUE : FALSE;
    auint cmd_index = cmd & 0x3FU;
    boole cmd_app = ((cmd & 0x040U) != 0U) ? TRUE : FALSE;
    auint arg_known = 0U;
    auint known_mask = 0U;
    auint next_index = 255U;
    auint crc_expected = 0U;
    boole crc_expected_valid = FALSE;
    char const* response_phase = "IDLE";
    char const* data_direction = "NONE";
    char const* data_phase = "IDLE";
    auint token = 0U;
    auint token_wait_done = 0U;
    auint token_wait_total = 0U;
    auint data_done = 0U;
    auint crc_done = 0U;
    auint busy_remaining = 0U;
    auint pct_x100 = 0U;

    if (receiving){
        if (response_wait){ arg_known = 4U; }
        else { arg_known = (st->evcnt < 4U) ? st->evcnt : 4U; }
        known_mask = 1U;
        if (arg_known != 0U){ known_mask |= ((1U << arg_known) - 1U) << 1U; }
        if (arg_known == 4U){
            crc_expected = ((st->cc7v << 1U) | 1U) & 0xFFU;
            crc_expected_valid = TRUE;
        }
        if (response_wait){ next_index = 6U; response_phase = "WAIT_R1"; }
        else { next_index = 1U + arg_known; }
    }else if (extra_response){
        response_phase = "EXTRA";
    }else if ((cmd & 0x040U) != 0U){
        response_phase = "WAIT_APP";
    }

    switch (st->pstat){
        case 1U:
            data_direction = "READ"; data_phase = "WAIT_TOKEN"; token = 0xFEU;
            token_wait_done = st->ppos; token_wait_total = model->read_wait_bytes;
            break;
        case 2U:
            data_direction = "READ"; data_phase = "DATA"; token = 0xFEU;
            data_done = st->ppos; pct_x100 = (st->ppos * 10000U) / 512U;
            break;
        case 3U:
            data_direction = "READ"; data_phase = "CRC"; token = 0xFEU;
            data_done = 512U; crc_done = st->ppos; pct_x100 = 10000U;
            break;
        case 4U:
            data_direction = "WRITE"; data_phase = "WAIT_TOKEN"; token = 0xFEU;
            token_wait_done = st->ppos;
            break;
        case 5U:
            data_direction = "WRITE"; data_phase = "DATA"; token = 0xFEU;
            data_done = st->ppos; pct_x100 = (st->ppos * 10000U) / 512U;
            break;
        case 6U:
            data_direction = "WRITE"; data_phase = "CRC"; token = 0xFEU;
            data_done = 512U; crc_done = st->ppos; pct_x100 = 10000U;
            break;
        case 7U:
            data_direction = "WRITE"; data_phase = "WAIT_TOKEN"; token = 0xFCU;
            token_wait_done = st->ppos;
            break;
        case 8U:
            data_direction = "WRITE"; data_phase = "DATA"; token = 0xFCU;
            data_done = st->ppos; pct_x100 = (st->ppos * 10000U) / 512U;
            break;
        case 9U:
            data_direction = "WRITE"; data_phase = "CRC"; token = 0xFCU;
            data_done = 512U; crc_done = st->ppos; pct_x100 = 10000U;
            break;
        case 10U:
            data_direction = "WRITE"; data_phase = "BUSY";
            busy_remaining = WRAP32(st->next - cycle);
            if (busy_remaining >= 0x80000000U){ busy_remaining = 0U; }
            break;
        default:
            break;
    }

    *pos += (auint)snprintf(buf + *pos, cap - *pos,
        "{\"command\":{\"active\":%u,\"phase\":\"%s\",\"cmd\":%u,\"app\":%u,\"name\":\"%s\"," 
        "\"arg\":%u,\"arg_known\":%u,\"known_mask\":%u,\"next_index\":%u,"
        "\"bytes\":[%u,%u,%u,%u,%u,%u],\"crc_expected\":%u,\"crc_expected_valid\":%u,\"crc_checked\":%u},"
        "\"response\":{\"phase\":\"%s\",\"r1\":%u,\"wait_done\":%u,\"wait_total\":%u,\"extra_done\":%u,\"extra_total\":4,"
        "\"extra_bytes\":[%u,%u,%u,%u],\"next_miso\":%u},"
        "\"data\":{\"active\":%u,\"direction\":\"%s\",\"phase\":\"%s\",\"token\":%u,\"sector\":%u,"
        "\"token_wait_done\":%u,\"token_wait_total\":%u,\"data_done\":%u,\"data_total\":512,\"crc_done\":%u,\"crc_total\":2,"
        "\"crc_calculated\":%u,\"crc_received\":%u,\"write_response_token\":%u,\"busy_remaining_cycles\":%u,\"progress_x100\":%u}},",
        (unsigned)receiving, api_sd_command_phase_name(cmd), (unsigned)cmd_index, (unsigned)cmd_app,
        api_sd_command_name(cmd_index, cmd_app), (unsigned)st->crarg, (unsigned)arg_known,
        (unsigned)known_mask, (unsigned)next_index,
        (unsigned)(0x40U | cmd_index), (unsigned)((st->crarg >> 24) & 0xFFU),
        (unsigned)((st->crarg >> 16) & 0xFFU), (unsigned)((st->crarg >> 8) & 0xFFU),
        (unsigned)(st->crarg & 0xFFU), (unsigned)crc_expected,
        (unsigned)crc_expected, (unsigned)crc_expected_valid, (unsigned)st->crc,
        response_phase, (unsigned)st->r1,
        (unsigned)(response_wait ? st->evcnt : 0U), (unsigned)(response_wait ? model->cmd_wait_bytes : 0U),
        (unsigned)(extra_response ? st->evcnt : 0U),
        (unsigned)((st->crarg >> 24) & 0xFFU), (unsigned)((st->crarg >> 16) & 0xFFU),
        (unsigned)((st->crarg >> 8) & 0xFFU), (unsigned)(st->crarg & 0xFFU), (unsigned)st->data,
        (unsigned)(st->pstat != 0U), data_direction, data_phase, (unsigned)token, (unsigned)st->paddr,
        (unsigned)token_wait_done, (unsigned)token_wait_total, (unsigned)data_done, (unsigned)crc_done,
        (unsigned)st->cc16v, (unsigned)st->cc16c,
        (unsigned)(((st->pstat == 10U) && ((st->data == 0x05U) || (st->data == 0x0BU))) ? st->data : 0xFFU),
        (unsigned)busy_remaining, (unsigned)pct_x100);
}

static void api_handle_sd_status(void)
{
    cu_spisd_model_t m, slow, normal, fast;
    cu_state_spisd_t* st = cu_spisd_get_state();
    cu_state_cpu_t* cpu = cu_avr_get_state();
    auint cycle = cu_avr_getcycle();
    auint cmd_index = st->cmd & 0x3FU;
    boole cmd_app = ((st->cmd & 0x040U) != 0U) ? TRUE : FALSE;
    auint cs_age = WRAP32(cycle - st->enac);
    auint byte_age = WRAP32(cycle - st->recvc);
    boole deadline_active = ((st->state == 5U) || (st->pstat == 10U)) ? TRUE : FALSE;
    auint deadline_in = 0U;
    boole deadline_expired = FALSE;
    auint spi_remaining = 0U;
    auint spi_duration = 0U;
    auint spi_elapsed = 0U;
    char r1flags[96];
    char buf[8192]; auint pos = 0U;
    if (deadline_active){
        deadline_in = WRAP32(st->next - cycle);
        if (deadline_in >= 0x80000000U){ deadline_expired = TRUE; deadline_in = 0U; }
    }
    if (cpu->spi_tran){
        spi_duration = 16U << (((cpu->iors[CU_IO_SPCR] & 0x3U) << 1) | ((cpu->iors[CU_IO_SPSR] & 0x1U) ^ 1U));
        spi_remaining = WRAP32(cpu->spi_end - cycle);
        if (spi_remaining >= 0x80000000U){ spi_remaining = 0U; }
        spi_elapsed = (spi_remaining < spi_duration) ? (spi_duration - spi_remaining) : 0U;
    }
    api_sd_r1_flags(r1flags, (auint)sizeof(r1flags), st->r1);
    mainui_get_sd_timing_model(&m);
    cu_spisd_model_preset(CU_SPISD_PRESET_SLOW, &slow);
    cu_spisd_model_preset(CU_SPISD_PRESET_NORMAL, &normal);
    cu_spisd_model_preset(CU_SPISD_PRESET_FAST, &fast);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"cpu_cycle\":%u,\"model\":", (unsigned)cycle);
    api_sd_model_json(buf, sizeof(buf), &pos, &m);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
        ",\"allow_new_files\":%u,\"card\":{\"enabled\":%u,\"crc\":%u,\"state\":%u,\"state_name\":\"%s\","
        "\"packet_state\":%u,\"packet_state_name\":\"%s\",\"sector\":%u,\"packet_pos\":%u,"
        "\"command\":%u,\"command_index\":%u,\"command_app\":%u,\"command_name\":\"%s\",\"command_phase\":\"%s\","
        "\"command_arg\":%u,\"event_count\":%u,\"r1\":%u,\"r1_flags\":\"%s\",\"data_out\":%u,"
        "\"cs_change_cycle\":%u,\"cs_age_cycles\":%u,\"last_byte_cycle\":%u,\"last_byte_age_cycles\":%u,"
        "\"next_event_cycle\":%u,\"deadline_active\":%u,\"deadline_in_cycles\":%u,\"deadline_expired\":%u},"
        "\"spi\":{\"active\":%u,\"end_cycle\":%u,\"remaining_cycles\":%u,\"duration_cycles\":%u,\"elapsed_cycles\":%u,\"tx\":%u,\"rx\":%u,\"spdr\":%u,\"spcr\":%u,\"spsr\":%u},",
        (unsigned)mainui_get_sd_allow_new_files(), (unsigned)st->ena, (unsigned)st->crc,
        (unsigned)st->state, api_sd_state_name(st->state), (unsigned)st->pstat, api_sd_packet_state_name(st->pstat),
        (unsigned)st->paddr, (unsigned)st->ppos, (unsigned)st->cmd, (unsigned)cmd_index, (unsigned)cmd_app,
        api_sd_command_name(cmd_index, cmd_app), api_sd_command_phase_name(st->cmd), (unsigned)st->crarg,
        (unsigned)st->evcnt, (unsigned)st->r1, r1flags, (unsigned)st->data,
        (unsigned)st->enac, (unsigned)cs_age, (unsigned)st->recvc, (unsigned)byte_age,
        (unsigned)st->next, (unsigned)deadline_active, (unsigned)deadline_in, (unsigned)deadline_expired,
        (unsigned)cpu->spi_tran, (unsigned)cpu->spi_end, (unsigned)spi_remaining, (unsigned)spi_duration, (unsigned)spi_elapsed,
        (unsigned)cpu->spi_tx, (unsigned)cpu->spi_rx, (unsigned)cpu->iors[CU_IO_SPDR],
        (unsigned)cpu->iors[CU_IO_SPCR], (unsigned)cpu->iors[CU_IO_SPSR]);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"protocol\":");
    api_sd_protocol_json(buf, sizeof(buf), &pos, st, &m, cycle);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"presets\":{\"slow\":");
    api_sd_model_json(buf, sizeof(buf), &pos, &slow);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ",\"normal\":");
    api_sd_model_json(buf, sizeof(buf), &pos, &normal);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ",\"fast\":");
    api_sd_model_json(buf, sizeof(buf), &pos, &fast);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "}}");
    api_queue_json(buf);
}

#ifdef ENABLE_DEBUGGER
static char const* api_vfat_role_name(auint role)
{
    switch (role){
        case CU_VFAT_DEBUG_ROLE_BOOT: return "BOOT";
        case CU_VFAT_DEBUG_ROLE_FAT1: return "FAT1";
        case CU_VFAT_DEBUG_ROLE_FAT2: return "FAT2";
        case CU_VFAT_DEBUG_ROLE_ROOT: return "ROOT";
        case CU_VFAT_DEBUG_ROLE_FILE: return "FILE";
        case CU_VFAT_DEBUG_ROLE_DIRECTORY: return "DIRECTORY";
        case CU_VFAT_DEBUG_ROLE_SPARSE: return "SPARSE";
        case CU_VFAT_DEBUG_ROLE_UNALLOCATED: return "UNALLOCATED";
        default: return "UNKNOWN";
    }
}

static auint api_sd_le16(uint8 const* p){ return (auint)p[0] | ((auint)p[1] << 8); }
static auint api_sd_le32(uint8 const* p){ return (auint)p[0] | ((auint)p[1] << 8) | ((auint)p[2] << 16) | ((auint)p[3] << 24); }

#endif

static void api_handle_sd_payload(char* rest)
{
#ifdef ENABLE_DEBUGGER
    cu_state_spisd_t* st = cu_spisd_get_state();
    cu_vfat_debug_sector_t snap;
    char* tok;
    auint sector = st->paddr;
    auint start = 0U;
    auint count = 128U;
    auint cursor = st->ppos;
    auint last = (cursor != 0U) ? (cursor - 1U) : 0U;
    auint data_done = 0U;
    auint crc_done = 0U;
    char const* direction = "NONE";
    auint i, diff = 0U, pos = 0U;
    char buf[12288];
    boole compare = FALSE;
    tok = api_next_token(&rest);
    if (tok != NULL){
        if (api_stricmp(tok, "CURRENT") == 0){
            tok = api_next_token(&rest);
        }else if (api_stricmp(tok, "SECTOR") == 0){
            tok = api_next_token(&rest);
            if (tok == NULL){ api_reply_error("SD_PAYLOAD SECTOR needs <sector> [start] [count] [COMPARE]"); return; }
            sector = (auint)strtoul(tok, NULL, 0);
            tok = api_next_token(&rest);
        }else{
            api_reply_error("SD_PAYLOAD expects CURRENT [start] [count] [COMPARE] or SECTOR <n> [start] [count] [COMPARE]");
            return;
        }
        if ((tok != NULL) && (api_stricmp(tok, "COMPARE") != 0)){
            start = (auint)strtoul(tok, NULL, 0);
            tok = api_next_token(&rest);
            if ((tok != NULL) && (api_stricmp(tok, "COMPARE") != 0)){
                count = (auint)strtoul(tok, NULL, 0);
                tok = api_next_token(&rest);
            }
        }
        if ((tok != NULL) && (api_stricmp(tok, "COMPARE") == 0)){ compare = TRUE; }
    }
    if (start > 511U){ start = 511U; }
    if (count == 0U){ count = 1U; }
    if (count > 128U){ count = 128U; }
    if (start + count > 512U){ count = 512U - start; }
    if ((st->pstat == 1U) || (st->pstat == 2U) || (st->pstat == 3U)){ direction = "READ"; }
    else if ((st->pstat >= 4U) && (st->pstat <= 10U)){ direction = "WRITE"; }
    if ((st->pstat == 2U) || (st->pstat == 5U) || (st->pstat == 8U)){ data_done = st->ppos; }
    else if ((st->pstat == 3U) || (st->pstat == 6U) || (st->pstat == 9U) || (st->pstat == 10U)){ data_done = 512U; }
    if ((st->pstat == 3U) || (st->pstat == 6U) || (st->pstat == 9U)){ crc_done = st->ppos; }
    else if (st->pstat == 10U){ crc_done = 2U; }
    if (!cu_vfat_debug_sector_snapshot(sector, compare, &snap)){ api_reply_error("SD payload snapshot failed"); return; }
    if (sector == st->paddr){
        boole buffered = FALSE;
        if ((st->pstat == 2U) && (st->ppos != 0U)){ buffered = TRUE; }
        else if (st->pstat == 3U){ buffered = TRUE; }
        else if (((st->pstat == 5U) || (st->pstat == 8U)) && (st->ppos != 0U)){ buffered = TRUE; }
        else if ((st->pstat == 6U) || (st->pstat == 9U) || (st->pstat == 10U)){ buffered = TRUE; }
        if (buffered){ memcpy(snap.stream, cu_vfat_get_state()->rwbuf, 512U); snap.stream_valid = TRUE; }
    }
    if (snap.stream_valid && snap.backing_valid){
        for (i = start; i < start + count; ++i){ if (snap.stream[i] != snap.backing[i]){ diff++; } }
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
        "{\"ok\":1,\"built\":1,\"sector\":%u,\"current_sector\":%u,\"start\":%u,\"count\":%u,"
        "\"packet_pos\":%u,\"cursor_offset\":%u,\"last_offset\":%u,\"packet_state\":%u,\"packet_state_name\":\"%s\","
        "\"direction\":\"%s\",\"data_done\":%u,\"crc_done\":%u,\"crc_total\":2,"
        "\"crc_calculated\":%u,\"crc_received\":%u,\"compare_requested\":%u,\"stream_valid\":%u,\"backing_valid\":%u,\"diff_count\":%u,"
        "\"file_offset\":%u,\"source\":\"",
        (unsigned)sector, (unsigned)st->paddr, (unsigned)start, (unsigned)count,
        (unsigned)st->ppos, (unsigned)((cursor < 512U) ? cursor : 511U), (unsigned)((last < 512U) ? last : 511U),
        (unsigned)st->pstat, api_sd_packet_state_name(st->pstat),
        direction, (unsigned)data_done, (unsigned)crc_done,
        (unsigned)st->cc16v, (unsigned)st->cc16c, (unsigned)compare,
        (unsigned)snap.stream_valid, (unsigned)snap.backing_valid, (unsigned)diff,
        (unsigned)snap.file_offset);
    api_json_escape_append(buf, sizeof(buf), &pos, snap.source);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
        "\",\"fs\":{\"role_id\":%u,\"role\":\"%s\",\"cluster\":%u,\"sector_in_cluster\":%u,"
        "\"fat_copy\":%u,\"fat_first_cluster\":%u,\"owner_valid\":%u,\"owner_is_dir\":%u,"
        "\"chain_index\":%u,\"start_cluster\":%u,\"next_cluster\":%u,\"entry_sector\":%u,"
        "\"entry_index\":%u,\"parent_cluster\":%u,\"attr\":%u,\"data_sector\":%u,"
        "\"root_sector\":%u,\"sectors_per_cluster\":64,\"bytes_per_sector\":512,\"boot\":{"
        "\"bytes_per_sector\":%u,\"sectors_per_cluster\":%u,\"reserved_sectors\":%u,\"fat_count\":%u,"
        "\"root_entries\":%u,\"total_sectors\":%u,\"media\":%u,\"sectors_per_fat\":%u,\"signature\":%u}},\"stream\":[",
        (unsigned)snap.role, api_vfat_role_name(snap.role), (unsigned)snap.cluster, (unsigned)snap.sector_in_cluster,
        (unsigned)snap.fat_copy, (unsigned)snap.fat_first_cluster, (unsigned)snap.owner_valid, (unsigned)snap.owner_is_dir,
        (unsigned)snap.chain_index, (unsigned)snap.start_cluster, (unsigned)snap.next_cluster, (unsigned)snap.entry_sector,
        (unsigned)snap.entry_index, (unsigned)snap.parent_cluster, (unsigned)snap.attr, (unsigned)(CU_VFAT_SYS_SIZE >> 9),
        (unsigned)(0x040200U >> 9),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? api_sd_le16(&snap.stream[11]) : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? snap.stream[13] : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? api_sd_le16(&snap.stream[14]) : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? snap.stream[16] : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? api_sd_le16(&snap.stream[17]) : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? (api_sd_le16(&snap.stream[19]) ? api_sd_le16(&snap.stream[19]) : api_sd_le32(&snap.stream[32])) : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? snap.stream[21] : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? api_sd_le16(&snap.stream[22]) : 0U),
        (unsigned)((snap.role == CU_VFAT_DEBUG_ROLE_BOOT) ? api_sd_le16(&snap.stream[510]) : 0U));
    for (i = 0U; i < count; ++i){
        if (i != 0U){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
        pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%u", (unsigned)snap.stream[start + i]);
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "],\"backing\":[");
    for (i = 0U; i < count; ++i){
        if (i != 0U){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
        pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "%u", (unsigned)snap.backing[start + i]);
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
    api_queue_json(buf);
#else
    (void)rest;
    api_queue_json("{\"ok\":1,\"built\":0,\"error\":\"SD payload inspector not built (ENABLE_DEBUGGER=0)\"}");
#endif
}

static boole api_sd_token_bool(char const* tok, boole* out)
{
    if ((tok == NULL) || (out == NULL)){ return FALSE; }
    if ((api_stricmp(tok, "ON") == 0) || (api_stricmp(tok, "TRUE") == 0) || (api_stricmp(tok, "YES") == 0) || (strcmp(tok, "1") == 0)){ *out = TRUE; return TRUE; }
    if ((api_stricmp(tok, "OFF") == 0) || (api_stricmp(tok, "FALSE") == 0) || (api_stricmp(tok, "NO") == 0) || (strcmp(tok, "0") == 0)){ *out = FALSE; return TRUE; }
    return FALSE;
}

static char const* api_sd_trace_kind_name(auint kind)
{
    switch (kind){
        case CU_SPISD_TRACE_KIND_COMMAND: return "COMMAND";
        case CU_SPISD_TRACE_KIND_BLOCK: return "BLOCK";
        case CU_SPISD_TRACE_KIND_INIT_FAIL: return "INIT_FAIL";
        case CU_SPISD_TRACE_KIND_DATA_CRC: return "DATA_CRC";
        case CU_SPISD_TRACE_KIND_LATENCY: return "LATENCY";
        case CU_SPISD_TRACE_KIND_ABORT: return "ABORT";
        case CU_SPISD_TRACE_KIND_DATA_TOKEN: return "DATA_TOKEN";
        case CU_SPISD_TRACE_KIND_DATA_END: return "DATA_END";
        case CU_SPISD_TRACE_KIND_CRC_END: return "CRC_END";
        case CU_SPISD_TRACE_KIND_BUSY_END: return "BUSY_END";
        case CU_SPISD_TRACE_KIND_FAULT: return "FAULT";
        default: return "UNKNOWN";
    }
}

static void api_sd_trace_event_json(char* buf, auint cap, auint* pos, cu_spisd_trace_event_t const* e)
{
    *pos += (auint)snprintf(buf + *pos, cap - *pos,
        "{\"seq\":%u,\"transaction_id\":%u,\"kind\":\"%s\",\"cycle\":%u,\"start_cycle\":%u,\"response_start_cycle\":%u,\"latency_cycles\":%u,"
        "\"pc\":%u,\"start_pc\":%u,\"response_start_pc\":%u,\"row\":%u,\"beam_cycle\":%u,\"start_row\":%u,\"start_beam_cycle\":%u,"
        "\"response_start_row\":%u,\"response_start_beam_cycle\":%u,\"cmd\":%u,\"app\":%u,\"command_name\":\"%s\",\"arg\":%u,\"sector\":%u,"
        "\"command_byte_cycle\":[%u,%u,%u,%u,%u,%u],\"command_byte_pc\":[%u,%u,%u,%u,%u,%u],"
        "\"command_byte_row\":[%u,%u,%u,%u,%u,%u],\"command_byte_beam_cycle\":[%u,%u,%u,%u,%u,%u],"
        "\"r1\":%u,\"state_before\":%u,\"state_before_name\":\"%s\",\"state_after\":%u,\"state_after_name\":\"%s\","
        "\"packet_state\":%u,\"packet_state_name\":\"%s\",\"command_crc\":%u,\"command_crc_expected\":%u,\"crc_calculated\":%u,\"crc_received\":%u,\"value\":%u,"
        "\"flags\":%u,\"break\":%u,\"crc_error\":%u,\"init_fail\":%u,\"latency_exceeded\":%u,\"fault_injected\":%u}",
        (unsigned)e->seq, (unsigned)e->transaction_id, api_sd_trace_kind_name(e->kind), (unsigned)e->cycle, (unsigned)e->start_cycle,
        (unsigned)e->response_start_cycle, (unsigned)e->latency_cycles, (unsigned)e->pc, (unsigned)e->start_pc, (unsigned)e->response_start_pc,
        (unsigned)e->row, (unsigned)e->beam_cycle, (unsigned)e->start_row, (unsigned)e->start_beam_cycle,
        (unsigned)e->response_start_row, (unsigned)e->response_start_beam_cycle, (unsigned)e->cmd, (unsigned)e->app,
        api_sd_command_name(e->cmd, e->app ? TRUE : FALSE), (unsigned)e->arg, (unsigned)e->sector,
        (unsigned)e->command_byte_cycle[0], (unsigned)e->command_byte_cycle[1], (unsigned)e->command_byte_cycle[2],
        (unsigned)e->command_byte_cycle[3], (unsigned)e->command_byte_cycle[4], (unsigned)e->command_byte_cycle[5],
        (unsigned)e->command_byte_pc[0], (unsigned)e->command_byte_pc[1], (unsigned)e->command_byte_pc[2],
        (unsigned)e->command_byte_pc[3], (unsigned)e->command_byte_pc[4], (unsigned)e->command_byte_pc[5],
        (unsigned)e->command_byte_row[0], (unsigned)e->command_byte_row[1], (unsigned)e->command_byte_row[2],
        (unsigned)e->command_byte_row[3], (unsigned)e->command_byte_row[4], (unsigned)e->command_byte_row[5],
        (unsigned)e->command_byte_beam_cycle[0], (unsigned)e->command_byte_beam_cycle[1], (unsigned)e->command_byte_beam_cycle[2],
        (unsigned)e->command_byte_beam_cycle[3], (unsigned)e->command_byte_beam_cycle[4], (unsigned)e->command_byte_beam_cycle[5],
        (unsigned)e->r1, (unsigned)e->state_before, api_sd_state_name(e->state_before),
        (unsigned)e->state_after, api_sd_state_name(e->state_after), (unsigned)e->packet_state,
        api_sd_packet_state_name(e->packet_state), (unsigned)e->command_crc, (unsigned)e->command_crc_expected, (unsigned)e->crc_calculated,
        (unsigned)e->crc_received, (unsigned)e->value, (unsigned)e->flags,
        (unsigned)(((e->flags & CU_SPISD_TRACE_FLAG_BREAK) != 0U) ? 1U : 0U),
        (unsigned)(((e->flags & CU_SPISD_TRACE_FLAG_CRC) != 0U) ? 1U : 0U),
        (unsigned)(((e->flags & CU_SPISD_TRACE_FLAG_INIT_FAIL) != 0U) ? 1U : 0U),
        (unsigned)(((e->flags & CU_SPISD_TRACE_FLAG_LATENCY) != 0U) ? 1U : 0U),
        (unsigned)(((e->flags & CU_SPISD_TRACE_FLAG_FAULT) != 0U) ? 1U : 0U));
}

static void api_handle_sd_trace_status(void)
{
    auint sector = 0U, latency = 0U;
    boole sector_on = cu_spisd_trace_break_sector_get(&sector);
    boole latency_on = cu_spisd_trace_break_latency_get(&latency);
    cu_spisd_trace_event_t hit;
    boole hit_valid = cu_spisd_trace_hit_get(FALSE, &hit);
    char buf[4096]; auint pos = 0U;
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
        "{\"ok\":1,\"built\":%u,\"enabled\":%u,\"active\":%u,\"count\":%u,\"capacity\":%u,\"first_seq\":%u,"
        "\"breaks\":{\"cmd_mask_lo\":%u,\"cmd_mask_hi\":%u,\"sector_on\":%u,\"sector\":%u,\"init_fail\":%u,\"crc\":%u,\"latency_on\":%u,\"latency_cycles\":%u,"
        "\"fs_role_mask\":%u,\"fs_access\":%u,\"fs_path\":\"",
        (unsigned)cu_spisd_trace_built(), (unsigned)cu_spisd_trace_enabled(), (unsigned)cu_spisd_trace_active(),
        (unsigned)cu_spisd_trace_count(), (unsigned)CU_SPISD_TRACE_CAP, (unsigned)cu_spisd_trace_first_seq(),
        (unsigned)cu_spisd_trace_break_command_mask_lo(), (unsigned)cu_spisd_trace_break_command_mask_hi(),
        (unsigned)sector_on, (unsigned)sector, (unsigned)cu_spisd_trace_break_init_fail_get(),
        (unsigned)cu_spisd_trace_break_crc_get(), (unsigned)latency_on, (unsigned)latency,
        (unsigned)cu_spisd_trace_break_fs_role_mask(), (unsigned)cu_spisd_trace_break_fs_access_get());
    api_json_escape_append(buf, sizeof(buf), &pos, cu_spisd_trace_break_fs_path_get());
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\"},\"hit\":");
    if (hit_valid){ api_sd_trace_event_json(buf, sizeof(buf), &pos, &hit); }
    else { pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "null"); }
#ifdef ENABLE_DEBUGGER
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ",\"hit_fs\":");
    if (hit_valid && ((hit.cmd == 17U) || (hit.cmd == 18U) || (hit.cmd == 24U) || (hit.cmd == 25U))){
        cu_vfat_debug_sector_t fs;
        if (cu_vfat_debug_sector_snapshot(hit.sector, FALSE, &fs)){
            pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"role\":%u,\"role_name\":\"%s\",\"source\":\"",
                (unsigned)fs.role, api_vfat_role_name(fs.role));
            api_json_escape_append(buf, sizeof(buf), &pos, fs.source);
            pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "\",\"cluster\":%u,\"file_offset\":%u}",
                (unsigned)fs.cluster, (unsigned)fs.file_offset);
        }else{ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "null"); }
    }else{ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "null"); }
#endif
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "}");
    api_queue_json(buf);
}

static void api_handle_sd_trace_read(char* rest)
{
    char* stok = api_next_token(&rest); char* ctok = api_next_token(&rest);
    uint32 seq = stok ? (uint32)strtoul(stok, NULL, 0) : cu_spisd_trace_first_seq();
    auint count = ctok ? (auint)strtoul(ctok, NULL, 0) : 32U;
    auint emitted = 0U; char buf[32768]; auint pos = 0U;
    if (!cu_spisd_trace_built()){ api_reply_error("SD protocol trace not built (FLAG_SD_TRACE=0)"); return; }
    if (count > 32U){ count = 32U; }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"events\":[");
    while (emitted < count){
        cu_spisd_trace_event_t e;
        if (!cu_spisd_trace_get(seq, &e)){ break; }
        if (emitted != 0U){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
        api_sd_trace_event_json(buf, sizeof(buf), &pos, &e);
        seq++; emitted++;
        if ((sizeof(buf) - pos) < 1024U){ break; }
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "],\"next_seq\":%u,\"count\":%u}", (unsigned)seq, (unsigned)emitted);
    api_queue_json(buf);
}


static char const* api_sd_fsop_access_name(auint access)
{
    return (access == CU_SD_FSOP_ACCESS_WRITE) ? "WRITE" : "READ";
}

static char const* api_sd_fsop_activity_name(cu_sd_fsop_t const* op)
{
    boole write = (op->access == CU_SD_FSOP_ACCESS_WRITE) ? TRUE : FALSE;
    switch (op->role){
        case CU_VFAT_DEBUG_ROLE_BOOT: return write ? "BOOT_UPDATE" : "BOOT_READ";
        case CU_VFAT_DEBUG_ROLE_FAT1:
        case CU_VFAT_DEBUG_ROLE_FAT2: return write ? "FAT_UPDATE" : "FAT_LOOKUP";
        case CU_VFAT_DEBUG_ROLE_ROOT:
        case CU_VFAT_DEBUG_ROLE_DIRECTORY: return write ? "DIR_UPDATE" : "DIR_LOOKUP";
        case CU_VFAT_DEBUG_ROLE_FILE: return write ? "FILE_WRITE" : "FILE_READ";
        case CU_VFAT_DEBUG_ROLE_SPARSE: return write ? "SPARSE_WRITE" : "SPARSE_READ";
        case CU_VFAT_DEBUG_ROLE_UNALLOCATED: return write ? "UNALLOCATED_WRITE" : "UNALLOCATED_READ";
        default: return write ? "SECTOR_WRITE" : "SECTOR_READ";
    }
}

static void api_sd_fsop_json(char* buf, auint cap, auint* pos, cu_sd_fsop_t const* op)
{
    uint32 duration = op->end_cycle - op->start_cycle;
    *pos += (auint)snprintf(buf + *pos, cap - *pos,
        "{\"activity\":\"%s\",\"access\":\"%s\",\"role\":%u,\"role_name\":\"%s\",\"source\":\"",
        api_sd_fsop_activity_name(op), api_sd_fsop_access_name(op->access), (unsigned)op->role,
        api_vfat_role_name(op->role));
    api_json_escape_append(buf, cap, pos, op->source);
    *pos += (auint)snprintf(buf + *pos, cap - *pos,
        "\",\"first_seq\":%u,\"last_seq\":%u,\"first_transaction_id\":%u,\"last_transaction_id\":%u,"
        "\"start_cycle\":%u,\"end_cycle\":%u,\"duration_cycles\":%u,"
        "\"start_pc\":%u,\"end_pc\":%u,\"start_row\":%u,\"start_beam_cycle\":%u,\"end_row\":%u,\"end_beam_cycle\":%u,"
        "\"first_sector\":%u,\"last_sector\":%u,\"sectors_started\":%u,\"sectors_completed\":%u,\"bytes_completed\":%u,"
        "\"file_offset_valid\":%u,\"file_offset_start\":%u,\"file_offset_end\":%u,"
        "\"first_cluster\":%u,\"last_cluster\":%u,\"next_cluster\":%u,\"first_chain_index\":%u,\"last_chain_index\":%u,"
        "\"cluster_transitions\":%u,\"physical_runs\":%u,\"r1\":%u,\"flags\":%u,"
        "\"break\":%u,\"crc_error\":%u,\"fault\":%u,\"abort\":%u,\"r1_error\":%u,\"incomplete\":%u}",
        (unsigned)op->first_seq, (unsigned)op->last_seq, (unsigned)op->first_transaction_id, (unsigned)op->last_transaction_id,
        (unsigned)op->start_cycle, (unsigned)op->end_cycle, (unsigned)duration,
        (unsigned)op->start_pc, (unsigned)op->end_pc, (unsigned)op->start_row, (unsigned)op->start_beam_cycle,
        (unsigned)op->end_row, (unsigned)op->end_beam_cycle, (unsigned)op->first_sector, (unsigned)op->last_sector,
        (unsigned)op->sectors_started, (unsigned)op->sectors_completed, (unsigned)op->bytes_completed,
        (unsigned)op->file_offset_valid, (unsigned)op->file_offset_start, (unsigned)op->file_offset_end,
        (unsigned)op->first_cluster, (unsigned)op->last_cluster, (unsigned)op->next_cluster,
        (unsigned)op->first_chain_index, (unsigned)op->last_chain_index, (unsigned)op->cluster_transitions,
        (unsigned)op->physical_runs, (unsigned)op->r1, (unsigned)op->flags,
        (unsigned)(((op->flags & CU_SD_FSOP_FLAG_BREAK) != 0U) ? 1U : 0U),
        (unsigned)(((op->flags & CU_SD_FSOP_FLAG_CRC_ERROR) != 0U) ? 1U : 0U),
        (unsigned)(((op->flags & CU_SD_FSOP_FLAG_FAULT) != 0U) ? 1U : 0U),
        (unsigned)(((op->flags & CU_SD_FSOP_FLAG_ABORT) != 0U) ? 1U : 0U),
        (unsigned)(((op->flags & CU_SD_FSOP_FLAG_R1_ERROR) != 0U) ? 1U : 0U),
        (unsigned)(((op->flags & CU_SD_FSOP_FLAG_INCOMPLETE) != 0U) ? 1U : 0U));
}

static void api_handle_sd_fs_history(char* rest)
{
    char* ctok = api_next_token(&rest);
    auint want = ctok ? (auint)strtoul(ctok, NULL, 0) : 32U;
    auint total = 0U, count, i;
    cu_sd_fsop_t* ops;
    char buf[65536]; auint pos = 0U;
    if (want == 0U){ want = 1U; }
    if (want > 48U){ want = 48U; }
    if (!cu_sd_fs_history_built()){
        api_queue_json("{\"ok\":1,\"built\":0,\"mapping\":\"current_vfat\",\"total_operations\":0,\"count\":0,\"operations\":[]}");
        return;
    }
    ops = (cu_sd_fsop_t*)malloc(sizeof(cu_sd_fsop_t) * want);
    if (ops == NULL){ api_reply_error("SD filesystem history allocation failed"); return; }
    count = cu_sd_fs_history_build(ops, want, &total);
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
        "{\"ok\":1,\"built\":1,\"mapping\":\"current_vfat\",\"trace_enabled\":%u,\"trace_count\":%u,"
        "\"total_operations\":%u,\"count\":%u,\"truncated\":%u,\"operations\":[",
        (unsigned)cu_spisd_trace_enabled(), (unsigned)cu_spisd_trace_count(), (unsigned)total, (unsigned)count,
        (unsigned)((total > count) ? 1U : 0U));
    for (i = 0U; i < count; ++i){
        if (i != 0U){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
        api_sd_fsop_json(buf, sizeof(buf), &pos, &ops[i]);
        if ((sizeof(buf) - pos) < 1024U){ break; }
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
    free(ops);
    api_queue_json(buf);
}


static void api_handle_sd_timing_analysis(char* rest)
{
#ifdef ENABLE_DEBUGGER
    char* ctok = api_next_token(&rest);
    auint want = ctok ? (auint)strtoul(ctok, NULL, 0) : 24U;
    debug_sd_timing_sample_t* samples;
    cu_spisd_model_t const* m = cu_spisd_model_get();
    auint count, i, pos = 0U;
    char buf[65536];
    uint32 max_response = 0U, max_token = 0U, max_busy = 0U;
    uint64_t total_response = 0U, total_token = 0U, total_busy = 0U;
    auint n_response = 0U, n_token = 0U, n_busy = 0U, errors = 0U, incomplete = 0U;
    if (want == 0U){ want = 1U; }
    if (want > 48U){ want = 48U; }
    if (!cu_spisd_trace_built()){
        api_queue_json("{\"ok\":1,\"built\":0,\"source\":\"sd_trace\",\"count\":0,\"samples\":[]}");
        return;
    }
    samples = (debug_sd_timing_sample_t*)malloc(sizeof(*samples) * want);
    if (samples == NULL){ api_reply_error("SD timing analysis allocation failed"); return; }
    count = debug_sd_timing_collect(samples, want);
    for (i = 0U; i < count; ++i){
        debug_sd_timing_sample_t const* x = &samples[i];
        if (x->response_wait_cycles != 0U){ total_response += x->response_wait_cycles; n_response++; if (x->response_wait_cycles > max_response) max_response = x->response_wait_cycles; }
        if (x->token_wait_cycles != 0U){ total_token += x->token_wait_cycles; n_token++; if (x->token_wait_cycles > max_token) max_token = x->token_wait_cycles; }
        if (x->busy_cycles != 0U){ total_busy += x->busy_cycles; n_busy++; if (x->busy_cycles > max_busy) max_busy = x->busy_cycles; }
        if ((x->flags != 0U) || (x->first_block && (!x->command_crc_ok)) || x->busy_early){ errors++; }
        if (!x->complete){ incomplete++; }
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
        "{\"ok\":1,\"built\":1,\"source\":\"sd_trace\",\"trace_enabled\":%u,\"trace_count\":%u,"
        "\"model\":{\"preset\":%u,\"cmd_wait_bytes\":%u,\"read_wait_bytes\":%u,\"write_busy_ms\":%u,"
        "\"expected_response_slots\":%u,\"expected_read_token_slots\":%u,\"write_busy_min_cycles\":%u},"
        "\"summary\":{\"samples\":%u,\"errors\":%u,\"incomplete\":%u,"
        "\"response_avg_cycles\":%u,\"response_max_cycles\":%u,\"token_avg_cycles\":%u,\"token_max_cycles\":%u,"
        "\"busy_avg_cycles\":%u,\"busy_max_cycles\":%u},\"count\":%u,\"samples\":[",
        (unsigned)cu_spisd_trace_enabled(), (unsigned)cu_spisd_trace_count(), (unsigned)m->preset,
        (unsigned)m->cmd_wait_bytes, (unsigned)m->read_wait_bytes, (unsigned)m->write_busy_ms,
        (unsigned)(m->cmd_wait_bytes + 1U), (unsigned)(m->read_wait_bytes + 1U),
        (unsigned)(((uint64_t)m->write_busy_ms * 28634ULL > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32)((uint64_t)m->write_busy_ms * 28634ULL)),
        (unsigned)count, (unsigned)errors, (unsigned)incomplete,
        (unsigned)(n_response ? (uint32)(total_response / n_response) : 0U), (unsigned)max_response,
        (unsigned)(n_token ? (uint32)(total_token / n_token) : 0U), (unsigned)max_token,
        (unsigned)(n_busy ? (uint32)(total_busy / n_busy) : 0U), (unsigned)max_busy, (unsigned)count);
    for (i = 0U; i < count; ++i){
        debug_sd_timing_sample_t const* x = &samples[i];
        char const* dir = x->direction == 1U ? "READ" : (x->direction == 2U ? "WRITE" : "NONE");
        if (i != 0U){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
        pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
            "{\"transaction_id\":%u,\"first_seq\":%u,\"last_seq\":%u,\"cmd\":%u,\"command_name\":\"%s\",\"app\":%u,"
            "\"arg\":%u,\"sector\":%u,\"r1\":%u,\"first_block\":%u,\"direction\":\"%s\","
            "\"start_pc\":%u,\"start_row\":%u,\"start_beam_cycle\":%u,"
            "\"command_start_cycle\":%u,\"command_end_cycle\":%u,\"response_cycle\":%u,\"block_start_cycle\":%u,"
            "\"token_cycle\":%u,\"data_end_cycle\":%u,\"crc_end_cycle\":%u,\"busy_end_cycle\":%u,\"end_cycle\":%u,"
            "\"command_span_cycles\":%u,\"response_wait_cycles\":%u,\"token_wait_cycles\":%u,\"data_cycles\":%u,"
            "\"crc_cycles\":%u,\"busy_cycles\":%u,\"total_cycles\":%u,"
            "\"command_gap_min_cycles\":%u,\"command_gap_max_cycles\":%u,\"command_gap_avg_cycles\":%u,"
            "\"expected_busy_min_cycles\":%u,\"busy_early\":%u,\"command_crc\":%u,\"command_crc_expected\":%u,"
            "\"command_crc_ok\":%u,\"crc_calculated\":%u,\"crc_received\":%u,\"flags\":%u,\"complete\":%u}",
            (unsigned)x->transaction_id, (unsigned)x->first_seq, (unsigned)x->last_seq, (unsigned)x->command,
            api_sd_command_name(x->command, x->app ? TRUE : FALSE), (unsigned)x->app, (unsigned)x->arg, (unsigned)x->sector,
            (unsigned)x->r1, (unsigned)x->first_block, dir, (unsigned)x->start_pc, (unsigned)x->start_row, (unsigned)x->start_beam_cycle,
            (unsigned)x->command_start_cycle, (unsigned)x->command_end_cycle, (unsigned)x->response_cycle, (unsigned)x->block_start_cycle,
            (unsigned)x->token_cycle, (unsigned)x->data_end_cycle, (unsigned)x->crc_end_cycle, (unsigned)x->busy_end_cycle, (unsigned)x->end_cycle,
            (unsigned)x->command_span_cycles, (unsigned)x->response_wait_cycles, (unsigned)x->token_wait_cycles, (unsigned)x->data_cycles,
            (unsigned)x->crc_cycles, (unsigned)x->busy_cycles, (unsigned)x->total_cycles,
            (unsigned)x->command_gap_min_cycles, (unsigned)x->command_gap_max_cycles, (unsigned)x->command_gap_avg_cycles,
            (unsigned)x->expected_busy_min_cycles, (unsigned)x->busy_early, (unsigned)x->command_crc, (unsigned)x->command_crc_expected,
            (unsigned)x->command_crc_ok, (unsigned)x->crc_calculated, (unsigned)x->crc_received, (unsigned)x->flags, (unsigned)x->complete);
        if ((sizeof(buf) - pos) < 1400U){ break; }
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
    free(samples);
    api_queue_json(buf);
#else
    (void)rest;
    api_queue_json("{\"ok\":1,\"built\":0,\"source\":\"sd_trace\",\"count\":0,\"samples\":[]}");
#endif
}

static auint api_sd_fs_role_parse(char const* tok)
{
    if (tok == NULL){ return 0U; }
    if (api_stricmp(tok, "BOOT") == 0){ return CU_VFAT_DEBUG_ROLE_BOOT; }
    if (api_stricmp(tok, "FAT1") == 0){ return CU_VFAT_DEBUG_ROLE_FAT1; }
    if (api_stricmp(tok, "FAT2") == 0){ return CU_VFAT_DEBUG_ROLE_FAT2; }
    if (api_stricmp(tok, "ROOT") == 0){ return CU_VFAT_DEBUG_ROLE_ROOT; }
    if (api_stricmp(tok, "FILE") == 0){ return CU_VFAT_DEBUG_ROLE_FILE; }
    if ((api_stricmp(tok, "DIR") == 0) || (api_stricmp(tok, "DIRECTORY") == 0)){ return CU_VFAT_DEBUG_ROLE_DIRECTORY; }
    if (api_stricmp(tok, "SPARSE") == 0){ return CU_VFAT_DEBUG_ROLE_SPARSE; }
    if ((api_stricmp(tok, "UNALLOCATED") == 0) || (api_stricmp(tok, "FREE") == 0)){ return CU_VFAT_DEBUG_ROLE_UNALLOCATED; }
    return 0U;
}

static auint api_sd_fs_access_parse(char const* tok)
{
    if (tok == NULL){ return 0U; }
    if ((api_stricmp(tok, "R") == 0) || (api_stricmp(tok, "READ") == 0)){ return CU_SPISD_FS_ACCESS_READ; }
    if ((api_stricmp(tok, "W") == 0) || (api_stricmp(tok, "WRITE") == 0)){ return CU_SPISD_FS_ACCESS_WRITE; }
    if ((api_stricmp(tok, "RW") == 0) || (api_stricmp(tok, "WR") == 0) || (api_stricmp(tok, "BOTH") == 0)){
        return CU_SPISD_FS_ACCESS_READ | CU_SPISD_FS_ACCESS_WRITE;
    }
    return 0U;
}

static void api_handle_sd_trace_break(char* rest)
{
    char* kind = api_next_token(&rest);
    if (kind == NULL){ api_handle_sd_trace_status(); return; }
    if (api_stricmp(kind, "STATUS") == 0){ api_handle_sd_trace_status(); return; }
    if (!cu_spisd_trace_built()){ api_reply_error("SD protocol trace not built (FLAG_SD_TRACE=0)"); return; }
    if ((api_stricmp(kind, "CMD_CLEAR") == 0) || (api_stricmp(kind, "CMDS_CLEAR") == 0)){
        cu_spisd_trace_break_commands_clear(); api_handle_sd_trace_status(); return;
    }
    if ((api_stricmp(kind, "CMD") == 0) || (api_stricmp(kind, "COMMAND") == 0)){
        char* c = api_next_token(&rest); char* on = api_next_token(&rest); boole b; auint cmd;
        if ((c == NULL) || (on == NULL) || (!api_sd_token_bool(on, &b))){ api_reply_error("SD_TRACE BREAK CMD needs <0..63> <ON|OFF>"); return; }
        cmd = (auint)strtoul(c, NULL, 0); if (cmd > 63U){ api_reply_error("SD command must be 0..63"); return; }
        cu_spisd_trace_break_command_set(cmd, b); api_handle_sd_trace_status(); return;
    }
    if (api_stricmp(kind, "SECTOR") == 0){
        char* v = api_next_token(&rest);
        if (v == NULL){ api_reply_error("SD_TRACE BREAK SECTOR needs <sector|OFF>"); return; }
        if (api_stricmp(v, "OFF") == 0){ cu_spisd_trace_break_sector_set(FALSE, 0U); }
        else { cu_spisd_trace_break_sector_set(TRUE, (auint)strtoul(v, NULL, 0)); }
        api_handle_sd_trace_status(); return;
    }
    if ((api_stricmp(kind, "INIT") == 0) || (api_stricmp(kind, "INIT_FAIL") == 0)){
        char* v = api_next_token(&rest); boole b;
        if ((v == NULL) || (!api_sd_token_bool(v, &b))){ api_reply_error("SD_TRACE BREAK INIT needs ON|OFF"); return; }
        cu_spisd_trace_break_init_fail_set(b); api_handle_sd_trace_status(); return;
    }
    if (api_stricmp(kind, "CRC") == 0){
        char* v = api_next_token(&rest); boole b;
        if ((v == NULL) || (!api_sd_token_bool(v, &b))){ api_reply_error("SD_TRACE BREAK CRC needs ON|OFF"); return; }
        cu_spisd_trace_break_crc_set(b); api_handle_sd_trace_status(); return;
    }
    if ((api_stricmp(kind, "FS_ROLE") == 0) || (api_stricmp(kind, "ROLE") == 0)){
        char* rtok = api_next_token(&rest); char* otok = api_next_token(&rest); boole on; auint role;
        if ((rtok == NULL) || (otok == NULL) || (!api_sd_token_bool(otok, &on))){ api_reply_error("SD_TRACE BREAK FS_ROLE needs <role> <ON|OFF>"); return; }
        role = api_sd_fs_role_parse(rtok); if (role == 0U){ api_reply_error("Unknown VFAT role"); return; }
        cu_spisd_trace_break_fs_role_set(role, on); api_handle_sd_trace_status(); return;
    }
    if ((api_stricmp(kind, "FS_PATH") == 0) || (api_stricmp(kind, "PATH") == 0)){
        api_trim(rest);
        if ((rest == NULL) || (rest[0] == 0)){ api_reply_error("SD_TRACE BREAK FS_PATH needs <substring|OFF>"); return; }
        cu_spisd_trace_break_fs_path_set((api_stricmp(rest, "OFF") == 0) ? "" : rest); api_handle_sd_trace_status(); return;
    }
    if ((api_stricmp(kind, "FS_ACCESS") == 0) || (api_stricmp(kind, "ACCESS") == 0)){
        char* atok = api_next_token(&rest); auint a = api_sd_fs_access_parse(atok);
        if (a == 0U){ api_reply_error("SD_TRACE BREAK FS_ACCESS needs R|W|RW"); return; }
        cu_spisd_trace_break_fs_access_set(a); api_handle_sd_trace_status(); return;
    }
    if (api_stricmp(kind, "LATENCY") == 0){
        char* v = api_next_token(&rest);
        if (v == NULL){ api_reply_error("SD_TRACE BREAK LATENCY needs <cycles|OFF>"); return; }
        if (api_stricmp(v, "OFF") == 0){ cu_spisd_trace_break_latency_set(FALSE, 0U); }
        else { cu_spisd_trace_break_latency_set(TRUE, (auint)strtoul(v, NULL, 0)); }
        api_handle_sd_trace_status(); return;
    }
    api_reply_error("SD_TRACE BREAK expects CMD|CMD_CLEAR|SECTOR|INIT|CRC|LATENCY|FS_ROLE|FS_PATH|FS_ACCESS|STATUS");
}

static void api_handle_sd_trace(char* rest)
{
    char* tok = api_next_token(&rest);
    if ((tok == NULL) || (api_stricmp(tok, "STATUS") == 0)){ api_handle_sd_trace_status(); return; }
    if (api_stricmp(tok, "ENABLE") == 0){
        if (!cu_spisd_trace_built()){ api_reply_error("SD protocol trace not built (FLAG_SD_TRACE=0)"); return; }
        cu_spisd_trace_enable(TRUE); api_handle_sd_trace_status(); return;
    }
    if (api_stricmp(tok, "DISABLE") == 0){ cu_spisd_trace_enable(FALSE); api_handle_sd_trace_status(); return; }
    if (api_stricmp(tok, "CLEAR") == 0){ cu_spisd_trace_clear(); api_handle_sd_trace_status(); return; }
    if (api_stricmp(tok, "READ") == 0){ api_handle_sd_trace_read(rest); return; }
    if (api_stricmp(tok, "BREAK") == 0){ api_handle_sd_trace_break(rest); return; }
    if (api_stricmp(tok, "HIT") == 0){
        char* ctok = api_next_token(&rest); boole clear = (ctok && (api_stricmp(ctok, "CLEAR") == 0)) ? TRUE : FALSE;
        cu_spisd_trace_event_t hit; boole valid = cu_spisd_trace_hit_get(clear, &hit); char buf[1024]; auint pos = 0U;
        pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"hit\":");
        if (valid){ api_sd_trace_event_json(buf, sizeof(buf), &pos, &hit); } else { pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "null"); }
        pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ",\"cleared\":%u}", (unsigned)clear); api_queue_json(buf); return;
    }
    api_reply_error("SD_TRACE expects STATUS|ENABLE|DISABLE|CLEAR|READ|BREAK|HIT");
}

static char const* api_sd_fault_kind_name(auint kind)
{
    switch (kind){
        case CU_SPISD_FAULT_R1_DELAY: return "R1_DELAY";
        case CU_SPISD_FAULT_TOKEN_DELAY: return "TOKEN_DELAY";
        case CU_SPISD_FAULT_BUSY_EXTEND: return "BUSY_EXTEND";
        case CU_SPISD_FAULT_R1_BITS: return "R1_BITS";
        case CU_SPISD_FAULT_CMD_CRC: return "CMD_CRC";
        case CU_SPISD_FAULT_DATA_CRC: return "DATA_CRC";
        case CU_SPISD_FAULT_REJECT: return "REJECT";
        default: return "NONE";
    }
}

static auint api_sd_fault_kind_parse(char const* tok)
{
    if (tok == NULL){ return CU_SPISD_FAULT_NONE; }
    if ((api_stricmp(tok, "R1_DELAY") == 0) || (api_stricmp(tok, "RESPONSE_DELAY") == 0)){ return CU_SPISD_FAULT_R1_DELAY; }
    if ((api_stricmp(tok, "TOKEN_DELAY") == 0) || (api_stricmp(tok, "DATA_DELAY") == 0)){ return CU_SPISD_FAULT_TOKEN_DELAY; }
    if ((api_stricmp(tok, "BUSY") == 0) || (api_stricmp(tok, "BUSY_EXTEND") == 0)){ return CU_SPISD_FAULT_BUSY_EXTEND; }
    if ((api_stricmp(tok, "R1") == 0) || (api_stricmp(tok, "R1_BITS") == 0)){ return CU_SPISD_FAULT_R1_BITS; }
    if ((api_stricmp(tok, "CMD_CRC") == 0) || (api_stricmp(tok, "COMMAND_CRC") == 0)){ return CU_SPISD_FAULT_CMD_CRC; }
    if ((api_stricmp(tok, "DATA_CRC") == 0) || (api_stricmp(tok, "BLOCK_CRC") == 0)){ return CU_SPISD_FAULT_DATA_CRC; }
    if ((api_stricmp(tok, "REJECT") == 0) || (api_stricmp(tok, "FAIL") == 0)){ return CU_SPISD_FAULT_REJECT; }
    return CU_SPISD_FAULT_NONE;
}

static void api_handle_sd_fault_status(void)
{
    char buf[8192]; auint pos = 0U, i;
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":1,\"built\":%u,\"active\":%u,\"capacity\":%u,\"rules\":[",
        (unsigned)cu_spisd_fault_built(), (unsigned)cu_spisd_fault_active(), (unsigned)CU_SPISD_FAULT_CAP);
    for (i = 0U; i < CU_SPISD_FAULT_CAP; ++i){
        cu_spisd_fault_rule_t r;
        if (!cu_spisd_fault_get(i, &r)){ memset(&r, 0, sizeof(r)); }
        if (i != 0U){ pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, ","); }
        pos += (auint)snprintf(buf + pos, sizeof(buf) - pos,
            "{\"slot\":%u,\"enabled\":%u,\"persistent\":%u,\"kind\":%u,\"kind_name\":\"%s\",\"value\":%u,"
            "\"command_any\":%u,\"command\":%u,\"sector_any\":%u,\"sector\":%u,\"fired\":%u,\"last_cycle\":%u}",
            (unsigned)i, (unsigned)r.enabled, (unsigned)r.persistent, (unsigned)r.kind, api_sd_fault_kind_name(r.kind), (unsigned)r.value,
            (unsigned)r.command_any, (unsigned)r.command, (unsigned)r.sector_any, (unsigned)r.sector,
            (unsigned)r.fired, (unsigned)r.last_cycle);
    }
    pos += (auint)snprintf(buf + pos, sizeof(buf) - pos, "]}");
    api_queue_json(buf);
}

static void api_handle_sd_fault(char* rest)
{
    char* op = api_next_token(&rest);
    if ((op == NULL) || (api_stricmp(op, "STATUS") == 0)){ api_handle_sd_fault_status(); return; }
    if (!cu_spisd_fault_built()){ api_reply_error("SD fault injection not built (FLAG_SD_FAULT=0)"); return; }
    if (api_stricmp(op, "CLEAR") == 0){
        char* stok = api_next_token(&rest);
        if ((stok == NULL) || (api_stricmp(stok, "ALL") == 0)){ cu_spisd_fault_clear(CU_SPISD_FAULT_CAP); }
        else { auint slot = (auint)strtoul(stok, NULL, 0); if (slot >= CU_SPISD_FAULT_CAP){ api_reply_error("SD_FAULT slot out of range"); return; } cu_spisd_fault_clear(slot); }
        api_handle_sd_fault_status(); return;
    }
    if ((api_stricmp(op, "SET") == 0) || (api_stricmp(op, "ARM") == 0)){
        char* stok = api_next_token(&rest); char* ktok = api_next_token(&rest); char* vtok = api_next_token(&rest); char* mtok = api_next_token(&rest);
        cu_spisd_fault_rule_t r; auint slot, kind;
        if ((stok == NULL) || (ktok == NULL) || (vtok == NULL) || (mtok == NULL)){ api_reply_error("SD_FAULT SET <slot> <kind> <value> <ONCE|PERSIST> [CMD <n|ANY>] [SECTOR <n|ANY>]"); return; }
        slot = (auint)strtoul(stok, NULL, 0); kind = api_sd_fault_kind_parse(ktok);
        if ((slot >= CU_SPISD_FAULT_CAP) || (kind == CU_SPISD_FAULT_NONE)){ api_reply_error("Invalid SD fault slot/kind"); return; }
        memset(&r, 0, sizeof(r)); r.enabled = TRUE; r.kind = kind; r.value = (auint)strtoul(vtok, NULL, 0); r.command_any = TRUE; r.sector_any = TRUE;
        if ((api_stricmp(mtok, "PERSIST") == 0) || (api_stricmp(mtok, "PERSISTENT") == 0)){ r.persistent = TRUE; }
        else if (api_stricmp(mtok, "ONCE") == 0){ r.persistent = FALSE; }
        else { api_reply_error("SD fault mode must be ONCE or PERSIST"); return; }
        while (rest && (*rest != 0)){
            char* key = api_next_token(&rest); char* val;
            if (key == NULL){ break; }
            val = api_next_token(&rest); if (val == NULL){ api_reply_error("SD fault filter missing value"); return; }
            if (api_stricmp(key, "CMD") == 0){
                if (api_stricmp(val, "ANY") == 0){ r.command_any = TRUE; }
                else { auint c=(auint)strtoul(val,NULL,0); if(c>63U){api_reply_error("CMD filter must be 0..63 or ANY");return;} r.command_any=FALSE; r.command=c; }
            }else if (api_stricmp(key, "SECTOR") == 0){
                if (api_stricmp(val, "ANY") == 0){ r.sector_any = TRUE; }
                else { r.sector_any=FALSE; r.sector=(auint)strtoul(val,NULL,0); }
            }else{ api_reply_error("SD fault filters are CMD and SECTOR"); return; }
        }
        if ((kind == CU_SPISD_FAULT_CMD_CRC) || (kind == CU_SPISD_FAULT_DATA_CRC)){ if (r.value == 0U){ r.value = 1U; } }
        if ((kind == CU_SPISD_FAULT_REJECT) && (r.value == 0U)){ r.value = 0x02U; }
        if (!cu_spisd_fault_set(slot, &r)){ api_reply_error("Could not arm SD fault rule"); return; }
        api_handle_sd_fault_status(); return;
    }
    api_reply_error("SD_FAULT expects STATUS|SET|CLEAR");
}

#ifdef ENABLE_SD_REPLAY
static char const* api_sd_replay_mode_name(auint mode)
{
    switch (mode){
        case CU_SPISD_REPLAY_MODE_RECORD: return "RECORD";
        case CU_SPISD_REPLAY_MODE_REPLAY: return "REPLAY";
        case CU_SPISD_REPLAY_MODE_COMPLETE: return "COMPLETE";
        case CU_SPISD_REPLAY_MODE_ERROR: return "ERROR";
        default: return "OFF";
    }
}

static char const* api_sd_replay_error_name(auint err)
{
    switch (err){
        case CU_SPISD_REPLAY_ERR_EVENT_KIND: return "EVENT_KIND";
        case CU_SPISD_REPLAY_ERR_MOSI: return "MOSI";
        case CU_SPISD_REPLAY_ERR_RECV_CYCLE: return "RECV_CYCLE";
        case CU_SPISD_REPLAY_ERR_SEND_CYCLE: return "SEND_CYCLE";
        case CU_SPISD_REPLAY_ERR_CS_STATE: return "CS_STATE";
        case CU_SPISD_REPLAY_ERR_CS_CYCLE: return "CS_CYCLE";
        case CU_SPISD_REPLAY_ERR_RESET_CYCLE: return "RESET_CYCLE";
        case CU_SPISD_REPLAY_ERR_END: return "END_OF_RECORDING";
        case CU_SPISD_REPLAY_ERR_OVERFLOW: return "OVERFLOW";
        case CU_SPISD_REPLAY_ERR_ALLOC: return "ALLOC";
        case CU_SPISD_REPLAY_ERR_FILE: return "FILE";
        case CU_SPISD_REPLAY_ERR_FORMAT: return "FORMAT";
        default: return "NONE";
    }
}

static char const* api_sd_replay_event_name(auint kind)
{
    switch (kind){
        case CU_SPISD_REPLAY_EVENT_BYTE_START: return "BYTE_START";
        case CU_SPISD_REPLAY_EVENT_BYTE_END: return "BYTE_END";
        case CU_SPISD_REPLAY_EVENT_CS: return "CS";
        case CU_SPISD_REPLAY_EVENT_RESET: return "RESET";
        default: return "UNKNOWN";
    }
}

static void api_handle_sd_replay_status(void)
{
    cu_spisd_replay_status_t st; char buf[2048];
    cu_spisd_replay_status(&st);
    snprintf(buf, sizeof(buf),
        "{\"ok\":1,\"built\":%u,\"mode\":%u,\"mode_name\":\"%s\",\"recording\":%u,\"replaying\":%u,"
        "\"complete\":%u,\"count\":%u,\"capacity\":%u,\"cursor\":%u,\"strict_timing\":%u,\"tolerance_cycles\":%u,"
        "\"break_on_end\":%u,\"overflow\":%u,\"error\":%u,\"error_name\":\"%s\","
        "\"mismatch_index\":%u,\"mismatch_expected\":%u,\"mismatch_actual\":%u,\"origin_cycle\":%u,\"replay_origin_cycle\":%u}",
        (unsigned)st.built, (unsigned)st.mode, api_sd_replay_mode_name(st.mode),
        (unsigned)(st.mode==CU_SPISD_REPLAY_MODE_RECORD), (unsigned)(st.mode==CU_SPISD_REPLAY_MODE_REPLAY),
        (unsigned)(st.mode==CU_SPISD_REPLAY_MODE_COMPLETE), (unsigned)st.count, (unsigned)st.capacity, (unsigned)st.cursor,
        (unsigned)st.strict_timing, (unsigned)st.tolerance_cycles, (unsigned)st.break_on_end, (unsigned)st.overflow,
        (unsigned)st.error, api_sd_replay_error_name(st.error), (unsigned)st.mismatch_index,
        (unsigned)st.mismatch_expected, (unsigned)st.mismatch_actual, (unsigned)st.origin_cycle, (unsigned)st.replay_origin_cycle);
    api_queue_json(buf);
}

static void api_handle_sd_replay_read(char* rest)
{
    cu_spisd_replay_status_t st; char* tok; uint32 start=0U,count=64U,i,end; char buf[49152]; auint pos=0U;
    cu_spisd_replay_status(&st); tok=api_next_token(&rest);
    if(tok!=NULL){start=(uint32)strtoul(tok,NULL,0);tok=api_next_token(&rest);if(tok!=NULL)count=(uint32)strtoul(tok,NULL,0);}
    else if(st.count>count){start=st.count-count;}
    if(count==0U){ count=1U; }
    if(count>128U){ count=128U; }
    if(start>st.count){ start=st.count; }
    end=start+count;
    if(end>st.count){ end=st.count; }
    pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"{\"ok\":1,\"built\":%u,\"start\":%u,\"count\":%u,\"total\":%u,\"events\":[",(unsigned)st.built,(unsigned)start,(unsigned)(end-start),(unsigned)st.count);
    for(i=start;i<end;i++){
        cu_spisd_replay_event_t e;if(!cu_spisd_replay_get(i,&e))break;if(i!=start)pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,",");
        pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,
            "{\"seq\":%u,\"kind\":%u,\"kind_name\":\"%s\",\"recv_cycle_delta\":%u,\"send_cycle_delta\":%u,\"cycle_delta\":%u,"
            "\"mosi\":%u,\"miso\":%u,\"cs_enabled\":%u,\"state\":%u,\"state_name\":\"%s\",\"packet_state\":%u,\"packet_state_name\":\"%s\","
            "\"command\":%u,\"r1\":%u,\"sector\":%u,\"packet_pos\":%u}",
            (unsigned)e.seq,(unsigned)e.kind,api_sd_replay_event_name(e.kind),(unsigned)e.recv_cycle_delta,(unsigned)e.send_cycle_delta,(unsigned)e.cycle_delta,
            (unsigned)e.mosi,(unsigned)e.miso,(unsigned)e.cs_enabled,(unsigned)e.state,api_sd_state_name(e.state),(unsigned)e.packet_state,api_sd_packet_state_name(e.packet_state),
            (unsigned)e.command,(unsigned)e.r1,(unsigned)e.sector,(unsigned)e.packet_pos);
        if(pos+1024U>=sizeof(buf))break;
    }
    pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"]}");api_queue_json(buf);
}

static void api_handle_sd_replay(char* rest)
{
    char* op=api_next_token(&rest);cu_spisd_replay_status_t st;
    if((op==NULL)||(api_stricmp(op,"STATUS")==0)){api_handle_sd_replay_status();return;}
    if(!cu_spisd_replay_built()){api_reply_error("SD deterministic replay not built (FLAG_SD_REPLAY=0)");return;}
    if(api_stricmp(op,"RECORD")==0){if(!cu_spisd_replay_record_start(cu_avr_getcycle())){api_reply_error("Could not start SD recording");return;}api_handle_sd_replay_status();return;}
    if(api_stricmp(op,"STOP")==0){cu_spisd_replay_stop();api_handle_sd_replay_status();return;}
    if(api_stricmp(op,"CLEAR")==0){cu_spisd_replay_clear();api_handle_sd_replay_status();return;}
    if(api_stricmp(op,"REPLAY")==0){char*tt=api_next_token(&rest);auint tol=tt?(auint)strtoul(tt,NULL,0):0U;if(!cu_spisd_replay_start(cu_avr_getcycle(),tol)){api_reply_error("No SD recording available to replay");return;}api_handle_sd_replay_status();return;}
    if(api_stricmp(op,"BREAK_END")==0){char*bt=api_next_token(&rest);boole on;if((bt==NULL)||(!api_sd_token_bool(bt,&on))){api_reply_error("SD_REPLAY BREAK_END needs ON|OFF");return;}cu_spisd_replay_break_on_end_set(on);api_handle_sd_replay_status();return;}
    if(api_stricmp(op,"READ")==0){api_handle_sd_replay_read(rest);return;}
    if((api_stricmp(op,"SAVE")==0)||(api_stricmp(op,"LOAD")==0)){
        char*path=rest;boole ok;if(path!=NULL){api_trim(path);}if((path==NULL)||(path[0]==0)){api_reply_error("SD_REPLAY SAVE|LOAD needs a path");return;}
        ok=(api_stricmp(op,"SAVE")==0)?cu_spisd_replay_save(path):cu_spisd_replay_load(path);if(!ok){cu_spisd_replay_status(&st);api_reply_error(api_sd_replay_error_name(st.error));return;}api_handle_sd_replay_status();return;
    }
    api_reply_error("SD_REPLAY expects STATUS|RECORD|STOP|REPLAY [TOLERANCE]|CLEAR|READ [START] [COUNT]|SAVE <PATH>|LOAD <PATH>|BREAK_END ON|OFF");
}

#else
static void api_handle_sd_replay(char* rest)
{
    char* op=api_next_token(&rest);
    if((op==NULL)||(api_stricmp(op,"STATUS")==0)){api_queue_json("{\"ok\":1,\"built\":0,\"mode\":0,\"mode_name\":\"OFF\",\"recording\":0,\"replaying\":0,\"complete\":0,\"count\":0,\"capacity\":0,\"cursor\":0,\"strict_timing\":1,\"tolerance_cycles\":0,\"break_on_end\":0,\"overflow\":0,\"error\":0,\"error_name\":\"NONE\",\"mismatch_index\":0,\"mismatch_expected\":0,\"mismatch_actual\":0}");return;}
    api_reply_error("SD deterministic replay not built (FLAG_SD_REPLAY=0)");
}
#endif

static void api_handle_sd_preset(char* rest)
{
    char* tok = api_next_token(&rest); auint preset;
    if (tok == NULL){ api_reply_error("SD_PRESET needs SLOW, NORMAL, FAST, or CUSTOM"); return; }
    if (api_stricmp(tok, "SLOW") == 0){ preset = CU_SPISD_PRESET_SLOW; }
    else if (api_stricmp(tok, "NORMAL") == 0){ preset = CU_SPISD_PRESET_NORMAL; }
    else if (api_stricmp(tok, "FAST") == 0){ preset = CU_SPISD_PRESET_FAST; }
    else if (api_stricmp(tok, "CUSTOM") == 0){ preset = CU_SPISD_PRESET_CUSTOM; }
    else { api_reply_error("Unknown SD preset"); return; }
    mainui_set_sd_timing_preset(preset);
    api_handle_sd_status();
}

static void api_handle_sd_set(char* rest)
{
    char* key = api_next_token(&rest); char* valtok = api_next_token(&rest);
    cu_spisd_model_t m; auint v;
    if ((key == NULL) || (valtok == NULL)){ api_reply_error("SD_SET needs <setting> <value>"); return; }
    v = (auint)strtoul(valtok, NULL, 0);
    mainui_get_sd_timing_model(&m);
    if (api_stricmp(key, "init_ms") == 0){ if (v > 60000U) v = 60000U; m.init_ms = v; }
    else if (api_stricmp(key, "cmd_wait_bytes") == 0){ if (v > 255U) v = 255U; m.cmd_wait_bytes = v; }
    else if ((api_stricmp(key, "read_wait_bytes") == 0) || (api_stricmp(key, "intersector_wait_bytes") == 0)){ if (v > 65535U) v = 65535U; m.read_wait_bytes = v; }
    else if (api_stricmp(key, "write_busy_ms") == 0){ if (v > 60000U) v = 60000U; m.write_busy_ms = v; }
    else if (api_stricmp(key, "cs_high_ms") == 0){ if (v > 10000U) v = 10000U; m.cs_high_ms = v; }
    else if (api_stricmp(key, "init_min_byte_cycles") == 0){ if (v < 1U) v = 1U; m.init_min_byte_cycles = v; }
    else if (api_stricmp(key, "init_max_byte_cycles") == 0){ if (v < 1U) v = 1U; m.init_max_byte_cycles = v; }
    else { api_reply_error("Unknown SD timing setting"); return; }
    if (m.init_max_byte_cycles < m.init_min_byte_cycles){ m.init_max_byte_cycles = m.init_min_byte_cycles; }
    m.preset = CU_SPISD_PRESET_CUSTOM;
    mainui_set_sd_timing_custom(&m);
    api_handle_sd_status();
}

static void api_handle_emu_status(void)
{
    char buf[8192]; auint pos=0U; auint i; char recent[APPCFG_PATH_MAX+1U];
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"{\"ok\":1,\"rom_path\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_loaded_rom_path());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"rom_dir\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_rom_path());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"save_path\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_save_path());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"screenshot_path\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_screenshot_path());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"controllerdb_path\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_controllerdb_path());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"resident_bootloader_file\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_resident_bootloader_file());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"remote_roms_host\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_remote_roms_host());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"theme\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_gui_theme_name());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"theme_file\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_gui_theme_file());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,
      "\",\"display_gameonly\":%u,\"fullscreen\":%u,\"system_messages\":%u,\"log_verbosity\":%u,\"gui_gamepad\":%u,\"gui_gamepad_speed\":%u,\"gui_pause\":%u,\"virtual_keyboard\":%u,"
      "\"frame_limiter\":%u,\"frame_merge\":%u,\"render_path\":%u,\"filter_pre\":%u,\"filter_scale\":%u,\"filter_crt\":%u,"
      "\"input_kbuzem\":%u,\"player2_alloc\":%u,\"mouse\":%u,\"mouse_scale\":%u,"
      "\"audio_freqscale\":%u,\"audio_s16\":%u,\"audio_rate\":%u,\"audio_latency\":%u,\"audio_resampler\":%u,\"audio_dcblock\":%u,\"audio_lowpass\":%u,\"audio_lowpass_quality\":%u,\"audio_monitor\":%u,\"audio_monitor_width\":%u,\"audio_reverb\":%u,\"audio_volume\":%u,"
      "\"spiram_pages\":%u,\"sd_allow_new_files\":%u,\"resident_bootloader\":%u,\"fast_flash\":%u,\"recent_frozen\":%u,\"config_dirty\":%u,"
      "\"video_dump_active\":%u,\"video_dump_autoinc\":%u,\"video_dump_reset_first\":%u,\"video_dump_file\":\"",
      (unsigned)mainui_get_display_gameonly(),(unsigned)mainui_get_display_fullscreen(),(unsigned)mainui_get_system_messages(),(unsigned)mainui_get_log_verbosity(),
      (unsigned)mainui_get_gui_gamepad_enable(),(unsigned)mainui_get_gui_gamepad_speed_pct(),(unsigned)mainui_get_gui_pause_while_open(),(unsigned)mainui_get_gui_virtual_keyboard(),
      (unsigned)mainui_get_frame_rate_limiter(),(unsigned)mainui_get_frame_merge(),(unsigned)mainui_get_render_path(),(unsigned)mainui_get_filter_pre_mode(),(unsigned)mainui_get_filter_scale_mode(),(unsigned)mainui_get_filter_crt_mode(),
      (unsigned)mainui_get_input_kbuzem(),(unsigned)mainui_get_input_player2alloc(),(unsigned)mainui_get_mouse_enable(),(unsigned)mainui_get_mouse_scale(),
      (unsigned)mainui_get_audio_freqscale(),(unsigned)mainui_get_audio_s16(),(unsigned)mainui_get_audio_output_rate(),(unsigned)mainui_get_audio_latency(),(unsigned)mainui_get_audio_resampler(),(unsigned)mainui_get_audio_dcblock(),(unsigned)mainui_get_audio_lowpass(),(unsigned)mainui_get_audio_lowpass_quality(),(unsigned)mainui_get_audio_monitor_mode(),(unsigned)mainui_get_audio_monitor_width(),(unsigned)mainui_get_audio_reverb(),(unsigned)mainui_get_audio_master_volume(),
      (unsigned)mainui_get_spiram_pages_mode(),(unsigned)mainui_get_sd_allow_new_files(),(unsigned)mainui_get_resident_bootloader_enable(),(unsigned)mainui_get_fast_flash(),(unsigned)mainui_get_recent_roms_frozen(),(unsigned)mainui_get_config_dirty(),
      (unsigned)mainui_get_video_dump_active(),(unsigned)mainui_get_video_dump_autoinc(),(unsigned)mainui_get_video_dump_reset_first());
    api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_video_dump_file());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"video_dump_status\":\""); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_video_dump_status());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"input_capture_active\":%u,\"input_capture_file\":\"",(unsigned)mainui_get_input_capture_active()); api_json_escape_append(buf,sizeof(buf),&pos,mainui_get_input_capture_file());
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"\",\"save_slot\":%u,\"save_slots\":[",(unsigned)savestate_get_slot());
    for(i=0;i<SAVESTATE_SLOT_COUNT;i++){ if(i)pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,","); pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"%u",(unsigned)(savestate_slot_exists(i)?1U:0U)); }
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"],\"recent\":[");
    {boole first=TRUE; for(i=0;i<MAINUI_RECENT_ROMS;i++){ if(mainui_get_recent_rom_path(i,recent,sizeof(recent))){if(!first)pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,",");first=FALSE;pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"\"");api_json_escape_append(buf,sizeof(buf),&pos,recent);pos+=(auint)snprintf(buf+pos,sizeof(buf)-pos,"\"");}}}
    pos += (auint)snprintf(buf+pos,sizeof(buf)-pos,"]}"); api_queue_json(buf);
}

static void api_handle_emu_set(char* rest)
{
    char* key=api_next_token(&rest); char* v; unsigned long n;
    if(!key){api_reply_error("EMU_SET needs key and value");return;} api_trim(rest); if(!rest||!rest[0]){api_reply_error("EMU_SET needs value");return;} v=rest; n=strtoul(v,NULL,0);
#define KNUM(k,fn) if(strcmp(key,k)==0){fn((auint)n);api_reply_ok_simple(NULL);return;}
#define KBOOL(k,fn) if(strcmp(key,k)==0){fn(n?TRUE:FALSE);api_reply_ok_simple(NULL);return;}
#define KSTR(k,fn) if(strcmp(key,k)==0){fn(v);api_reply_ok_simple(NULL);return;}
    KBOOL("display_gameonly",mainui_set_display_gameonly) KBOOL("fullscreen",mainui_set_display_fullscreen) KBOOL("system_messages",mainui_set_system_messages)
    KNUM("log_verbosity",mainui_set_log_verbosity)
    KBOOL("gui_gamepad",mainui_set_gui_gamepad_enable) KNUM("gui_gamepad_speed",mainui_set_gui_gamepad_speed_pct) KBOOL("gui_pause",mainui_set_gui_pause_while_open) KBOOL("virtual_keyboard",mainui_set_gui_virtual_keyboard)
    KBOOL("frame_limiter",mainui_set_frame_rate_limiter) KBOOL("frame_merge",mainui_set_frame_merge) KNUM("render_path",mainui_set_render_path) KNUM("filter_pre",mainui_set_filter_pre_mode) KNUM("filter_scale",mainui_set_filter_scale_mode) KNUM("filter_crt",mainui_set_filter_crt_mode)
    KBOOL("input_kbuzem",mainui_set_input_kbuzem) KBOOL("player2_alloc",mainui_set_input_player2alloc) KBOOL("mouse",mainui_set_mouse_enable) KNUM("mouse_scale",mainui_set_mouse_scale)
    KBOOL("audio_freqscale",mainui_set_audio_freqscale) KBOOL("audio_s16",mainui_set_audio_s16) KNUM("audio_rate",mainui_set_audio_output_rate) KNUM("audio_latency",mainui_set_audio_latency) KNUM("audio_resampler",mainui_set_audio_resampler) KBOOL("audio_dcblock",mainui_set_audio_dcblock) KNUM("audio_lowpass",mainui_set_audio_lowpass) KNUM("audio_lowpass_quality",mainui_set_audio_lowpass_quality) KNUM("audio_monitor",mainui_set_audio_monitor_mode) KNUM("audio_monitor_width",mainui_set_audio_monitor_width) KNUM("audio_reverb",mainui_set_audio_reverb) KNUM("audio_volume",mainui_set_audio_master_volume)
    KNUM("spiram_pages",mainui_set_spiram_pages_mode) KBOOL("sd_allow_new_files",mainui_set_sd_allow_new_files) KBOOL("resident_bootloader",mainui_set_resident_bootloader_enable) KBOOL("fast_flash",mainui_set_fast_flash)
    KSTR("rom_dir",mainui_set_rom_path) KSTR("save_path",mainui_set_save_path) KSTR("screenshot_path",mainui_set_screenshot_path) KSTR("controllerdb_path",mainui_set_controllerdb_path) KSTR("resident_bootloader_file",mainui_set_resident_bootloader_file) KSTR("remote_roms_host",mainui_set_remote_roms_host)
    KSTR("theme_file",mainui_set_gui_theme_file) KSTR("video_dump_file",mainui_set_video_dump_file)
    KBOOL("video_dump_active",mainui_set_video_dump_active) KBOOL("video_dump_autoinc",mainui_set_video_dump_autoinc) KBOOL("video_dump_reset_first",mainui_set_video_dump_reset_first) KBOOL("input_capture_active",mainui_set_input_capture_active)
    KBOOL("recent_frozen",mainui_set_recent_roms_frozen)
#undef KNUM
#undef KBOOL
#undef KSTR
    api_reply_error("Unknown EMU_SET setting");
}

static void api_handle_savestate(char* rest)
{
    char* op=api_next_token(&rest); char* t; auint slot=savestate_get_slot();
    if(!op){api_reply_error("SAVESTATE needs STATUS|SLOT|SAVE|LOAD");return;}
    t=api_next_token(&rest); if(t)slot=(auint)strtoul(t,NULL,0);
    if(strcmp(op,"STATUS")==0||strcmp(op,"status")==0){api_handle_emu_status();return;}
    if(slot>=SAVESTATE_SLOT_COUNT){api_reply_error("Invalid save-state slot");return;}
    if(strcmp(op,"SLOT")==0||strcmp(op,"slot")==0){savestate_set_slot(slot);api_reply_ok_simple(NULL);return;}
    if(strcmp(op,"SAVE")==0||strcmp(op,"save")==0){savestate_set_slot(slot);if(!savestate_save_slot(slot)){api_reply_error("Save state failed");return;}api_reply_ok_simple("\"saved\":1");return;}
    if(strcmp(op,"LOAD")==0||strcmp(op,"load")==0){savestate_set_slot(slot);if(!savestate_load_slot(slot)){api_reply_error("Load state failed");return;}api_state.run_frames_remaining=0U;api_wait_cancel_all("WAIT cancelled: save state loaded");api_input_clear_all();api_reply_ok_simple("\"loaded\":1");return;}
    api_reply_error("Unknown SAVESTATE operation");
}

static void api_handle_recent_roms(char* rest)
{
    char* op=api_next_token(&rest); char* t;
    if(!op||strcmp(op,"LIST")==0||strcmp(op,"list")==0){api_handle_emu_status();return;}
    if(strcmp(op,"LOAD")==0||strcmp(op,"load")==0){t=api_next_token(&rest);if(!t){api_reply_error("RECENT_ROMS LOAD needs index");return;}if(!mainui_load_recent_rom((auint)strtoul(t,NULL,0))){api_reply_error("Recent ROM load failed");return;}api_reply_ok_simple("\"loaded\":1");return;}
    if(strcmp(op,"CLEAR")==0||strcmp(op,"clear")==0){mainui_clear_recent_roms();api_reply_ok_simple(NULL);return;}
    api_reply_error("Unknown RECENT_ROMS operation");
}

static void api_handle_config(char* rest)
{
    char* op=api_next_token(&rest); if(!op){api_reply_error("CONFIG needs SAVE|RELOAD|DEFAULTS");return;}
    if(strcmp(op,"SAVE")==0||strcmp(op,"save")==0){mainui_config_save_now();api_reply_ok_simple(NULL);return;}
    if(strcmp(op,"RELOAD")==0||strcmp(op,"reload")==0){mainui_config_reload_now();api_reply_ok_simple(NULL);return;}
    if(strcmp(op,"DEFAULTS")==0||strcmp(op,"defaults")==0){mainui_config_defaults_now();api_reply_ok_simple(NULL);return;}
    api_reply_error("Unknown CONFIG operation");
}

static void api_handle_help(void)
{
	api_queue_json("{\"ok\":1,\"commands\":[\"PING\",\"HELP\",\"SUBSCRIBE <FRAME|CPU|SERIAL|ESP|BREAK|WATCH|ALL>...\",\"UNSUBSCRIBE <...>\",\"GET_SUBSCRIPTIONS\",\"GET_ROM_INFO\",\"GET_STATE\",\"GET_FRAME\",\"READ_REGS\",\"WRITE_REG <R0..R31> <VALUE>\",\"GET_DEBUG_INFO\",\"STACK [COUNT]\",\"CALLSTACK [COUNT]\",\"MEMSNAP TAKE <SLOT> <REGION> <ADDR> <LEN>\",\"MEMSNAP DIFF <SLOT> [MAX]\",\"MEMSNAP LIST\",\"MEMSNAP CLEAR <SLOT>\",\"MEMSCAN START <SLOT> <REGION> <ADDR> <LEN> [ANY|VALUE|OP VALUE]\",\"MEMSCAN REFINE <SLOT> <OP> [VALUE]\",\"MEMSCAN RESULTS <SLOT> [OFFSET] [COUNT]\",\"MEMSCAN STATUS\",\"MEMSCAN CLEAR <SLOT|ALL>\",\"DISASM <ADDR|PC> [COUNT]\",\"RUN_STATUS\",\"RUN_TO <ADDR|PC>\",\"STEP_INTO\",\"STEP_OVER\",\"STEP_OUT\",\"PROFILE ON|OFF|CLEAR\",\"READ_PROFILE <ADDR> [LEN]\",\"PROFILE_TOP [COUNT] [CYCLES|HITS]\",\"DEBUG_PROFILE STATUS|SAVE|RELOAD\",\"WATCH SET <SLOT> <SRAM|IO> <R|W|RW> <START> [END]\",\"WATCH CLEAR <SLOT|ALL>\",\"WATCH LIST\",\"WATCH HIT [CLEAR]\",\"MEM_TRACE STATUS|ENABLE|DISABLE|CLEAR|READ [SEQ] [COUNT]\",\"BREAK SET <ADDR>\",\"BREAK CLEAR <ADDR|ALL>\",\"BREAK LIST [START] [COUNT]\",\"BREAK NEXT [START]\",\"BREAK TEMP SET|CLEAR|LIST\",\"BREAK STEP [COUNT]\",\"BREAK STEP_CLEAR\",\"MEM_SIZE <REGION>\",\"MEM_REGIONS\",\"READ_MEM <REGION> <ADDR> [LEN]\",\"WRITE_MEM <REGION> <ADDR> <VALUE> [VALUE...]\",\"READ_SYMBOL <NAME>\",\"WRITE_SYMBOL <NAME> <VALUE>\",\"SOURCE_STATUS\",\"SOURCE_LIST [START] [COUNT]\",\"SOURCE_AT <ADDR|PC>\",\"SOURCE_READ <FILE_INDEX> <START_LINE> [COUNT]\",\"SOURCE_BREAK <FILE_INDEX> <LINE> [SET|CLEAR]\",\"LOAD_SYMBOLS <AUTO|PATH>\",\"CLEAR_SYMBOLS\",\"DWARF_STATUS\",\"DWARF_LOCALS [PC] [OFFSET] [COUNT]\",\"DWARF_GLOBALS [OFFSET] [COUNT]\",\"DWARF_VALUE <ID> [PC]\",\"DWARF_WRITE <ID> <VALUE> [PC]\",\"DWARF_TYPE <TYPE_ID>\",\"SD_STATUS\",\"SD_PAYLOAD CURRENT [START] [COUNT] [COMPARE]|SECTOR <N> [START] [COUNT] [COMPARE]\",\"SD_TRACE STATUS|ENABLE|DISABLE|CLEAR|READ [SEQ] [COUNT]|BREAK ...|HIT [CLEAR]\",\"SD_FS_HISTORY [COUNT]\",\"SD_TIMING_ANALYSIS [COUNT]\",\"SD_FAULT STATUS|SET <slot> <kind> <value> <ONCE|PERSIST> [CMD ...] [SECTOR ...]|CLEAR <slot|ALL>\",\"SD_REPLAY STATUS|RECORD|STOP|REPLAY [TOLERANCE]|CLEAR|READ [START] [COUNT]|SAVE <PATH>|LOAD <PATH>|BREAK_END ON|OFF\",\"SD_PRESET <SLOW|NORMAL|FAST|CUSTOM>\",\"SD_SET <SETTING> <VALUE>\",\"SD_RESET\",\"AUDIO_SCOPE STATUS|ENABLE|DISABLE|CLEAR|SOURCE [COUNT]|OUTPUT [COUNT]\",\"AUDIO_DEBUG STATUS|ENABLE|DISABLE|CLEAR|READ [SEQ] [COUNT]|BREAK OFF|RAIL|OUTSIDE <LOW> <HIGH>|HIT [CLEAR]\",\"SERIAL_STATUS\",\"SERIAL_SET <SETTING> <VALUE>\",\"SERIAL_TRACE STATUS|ON|OFF|CLEAR|READ|EXPORT\",\"TCP_DIAG STATUS|RESET|FLUSH\",\"ESP_STATUS\",\"NETPLAY_STATUS\",\"WAIT_FRAME <TARGET> [TIMEOUT_FRAMES]\",\"WAIT_MEM <REGION> <ADDR> <OP> <VALUE> [TIMEOUT_FRAMES]\",\"SET_INPUT <PLAYER> <MASK> <FRAMES>\",\"QUEUE_INPUT <PLAYER> <MASK> <FRAMES> [MASK FRAMES]...\",\"CLEAR_INPUT <PLAYER>\",\"RUN_FRAMES <COUNT>\",\"LOAD_ROM WAIT|RUN <PATH>\",\"SCREENSHOT [PATH]\",\"SCREENSHOT_CAPTURE\",\"SCREENSHOT_READ <OFFSET> [LEN]\",\"SCREENSHOT_CLEAR\",\"VIDEO_BEAM STATUS|ENABLE|DISABLE|READ [START] [COUNT]\",\"BEAM_HISTORY STATUS|ENABLE|DISABLE|CLEAR|READ [SEQ] [COUNT]\",\"RASTER_BREAK STATUS|SET <ROW|ANY> <CYCLE>|EVENT <RISE|FALL> [ROW|ANY]|CLEAR|HIT_CLEAR\",\"LOGIC_TRACE STATUS|ENABLE|DISABLE|CLEAR|MASK [DEFAULT|ALL|NONE|VALUE]|READ [SEQ] [COUNT]\",\"SCANLINE_PROFILE STATUS|ENABLE|DISABLE|CLEAR|ROWS [START] [COUNT]|ROW <ROW>\",\"PAUSE\",\"RESUME\",\"STEP_FRAME\",\"RESET\",\"QUIT\"]}");
}

static void api_process_command(char* line)
{
	char* rest = line;
	char* cmd = api_next_token(&rest);
	if (cmd == NULL){ return; }
	if ((strcmp(cmd, "PING") == 0) || (strcmp(cmd, "ping") == 0)){
		api_reply_ok_simple("\"reply\":\"PONG\"");
	}else if ((strcmp(cmd, "HELP") == 0) || (strcmp(cmd, "help") == 0)){
		api_handle_help();
	}else if ((strcmp(cmd, "SUBSCRIBE") == 0) || (strcmp(cmd, "subscribe") == 0)){
		api_handle_subscribe(rest, TRUE);
	}else if ((strcmp(cmd, "UNSUBSCRIBE") == 0) || (strcmp(cmd, "unsubscribe") == 0)){
		api_handle_subscribe(rest, FALSE);
	}else if ((strcmp(cmd, "GET_SUBSCRIPTIONS") == 0) || (strcmp(cmd, "get_subscriptions") == 0)){
		api_client_t* c = api_active_client(); char b[128]; snprintf(b,sizeof(b),"{\"ok\":1,\"subscriptions\":%u}",(unsigned)(c?c->subscriptions:0U)); api_queue_json(b);
	}else if ((strcmp(cmd, "GET_ROM_INFO") == 0) || (strcmp(cmd, "get_rom_info") == 0)){
		api_handle_get_rom_info();
	}else if ((strcmp(cmd, "GET_STATE") == 0) || (strcmp(cmd, "get_state") == 0)){
		api_handle_get_state();
	}else if ((strcmp(cmd, "GET_FRAME") == 0) || (strcmp(cmd, "get_frame") == 0)){
		api_handle_get_frame();
	}else if ((strcmp(cmd, "WAIT_FRAME") == 0) || (strcmp(cmd, "wait_frame") == 0)){
		api_handle_wait_frame(rest);
	}else if ((strcmp(cmd, "READ_REGS") == 0) || (strcmp(cmd, "read_regs") == 0) || (strcmp(cmd, "GET_DEBUG_INFO") == 0) || (strcmp(cmd, "get_debug_info") == 0)){
		api_handle_read_regs();
	}else if ((strcmp(cmd, "WRITE_REG") == 0) || (strcmp(cmd, "write_reg") == 0)){
		api_handle_write_reg(rest);
	}else if ((strcmp(cmd, "DISASM") == 0) || (strcmp(cmd, "disasm") == 0)){
		api_handle_disasm(rest);
	}else if ((strcmp(cmd, "STACK") == 0) || (strcmp(cmd, "stack") == 0)){
		api_handle_stack(rest);
	}else if ((strcmp(cmd, "CALLSTACK") == 0) || (strcmp(cmd, "callstack") == 0)){
		api_handle_callstack(rest);
	}else if ((strcmp(cmd, "RUN_STATUS") == 0) || (strcmp(cmd, "run_status") == 0)){
		api_handle_run_status();
	}else if ((strcmp(cmd, "RUN_TO") == 0) || (strcmp(cmd, "run_to") == 0)){
		api_handle_run_to(rest);
	}else if ((strcmp(cmd, "STEP_INTO") == 0) || (strcmp(cmd, "step_into") == 0)){
		api_handle_step_into();
	}else if ((strcmp(cmd, "STEP_OVER") == 0) || (strcmp(cmd, "step_over") == 0)){
		api_handle_step_over();
	}else if ((strcmp(cmd, "STEP_OUT") == 0) || (strcmp(cmd, "step_out") == 0)){
		api_handle_step_out();
#ifdef ENABLE_DEBUGGER
	}else if ((strcmp(cmd, "DEBUG_PROFILE") == 0) || (strcmp(cmd, "debug_profile") == 0)){
		api_handle_debug_profile(rest);
	}else if ((strcmp(cmd, "PROFILE") == 0) || (strcmp(cmd, "profile") == 0)){
		api_handle_profile(rest);
	}else if ((strcmp(cmd, "READ_PROFILE") == 0) || (strcmp(cmd, "read_profile") == 0)){
		api_handle_read_profile(rest);
	}else if ((strcmp(cmd, "PROFILE_TOP") == 0) || (strcmp(cmd, "profile_top") == 0)){
		api_handle_profile_top(rest);
	}else if ((strcmp(cmd, "WATCH") == 0) || (strcmp(cmd, "watch") == 0)){
		api_handle_watch(rest);
	}else if ((strcmp(cmd, "MEM_TRACE") == 0) || (strcmp(cmd, "mem_trace") == 0)){
		api_handle_memory_trace(rest);
	}else if ((strcmp(cmd, "BREAK") == 0) || (strcmp(cmd, "break") == 0)){
		api_handle_break(rest);
#endif
	}else if ((strcmp(cmd, "MEMSNAP") == 0) || (strcmp(cmd, "memsnap") == 0)){
		api_handle_memsnap(rest);
	}else if ((strcmp(cmd, "MEMSCAN") == 0) || (strcmp(cmd, "memscan") == 0)){
		api_handle_memscan(rest);
	}else if ((strcmp(cmd, "MEM_SIZE") == 0) || (strcmp(cmd, "mem_size") == 0)){
		api_handle_mem_size(rest);
	}else if ((strcmp(cmd, "MEM_REGIONS") == 0) || (strcmp(cmd, "mem_regions") == 0)){
		api_handle_mem_regions();
	}else if ((strcmp(cmd, "READ_MEM") == 0) || (strcmp(cmd, "read_mem") == 0)){
		api_handle_read_mem(rest);
	}else if ((strcmp(cmd, "WRITE_MEM") == 0) || (strcmp(cmd, "write_mem") == 0)){
		api_handle_write_mem(rest);
	}else if ((strcmp(cmd, "WAIT_MEM") == 0) || (strcmp(cmd, "wait_mem") == 0)){
		api_handle_wait_mem(rest);
	}else if ((strcmp(cmd, "READ_SYMBOL") == 0) || (strcmp(cmd, "read_symbol") == 0)){
		api_handle_read_symbol(rest);
	}else if ((strcmp(cmd, "WRITE_SYMBOL") == 0) || (strcmp(cmd, "write_symbol") == 0)){
		api_handle_write_symbol(rest);
	}else if ((strcmp(cmd, "SOURCE_STATUS") == 0) || (strcmp(cmd, "source_status") == 0)){
		api_handle_source_status();
	}else if ((strcmp(cmd, "SOURCE_LIST") == 0) || (strcmp(cmd, "source_list") == 0)){
		api_handle_source_list(rest);
	}else if ((strcmp(cmd, "SOURCE_AT") == 0) || (strcmp(cmd, "source_at") == 0)){
		api_handle_source_at(rest);
	}else if ((strcmp(cmd, "SOURCE_READ") == 0) || (strcmp(cmd, "source_read") == 0)){
		api_handle_source_read(rest);
	}else if ((strcmp(cmd, "SOURCE_BREAK") == 0) || (strcmp(cmd, "source_break") == 0)){
		api_handle_source_break(rest);
	}else if ((strcmp(cmd, "LOAD_SYMBOLS") == 0) || (strcmp(cmd, "load_symbols") == 0)){
		api_handle_load_symbols(rest);
	}else if ((strcmp(cmd, "CLEAR_SYMBOLS") == 0) || (strcmp(cmd, "clear_symbols") == 0)){
		api_handle_clear_symbols();
	}else if ((strcmp(cmd, "DWARF_STATUS") == 0) || (strcmp(cmd, "dwarf_status") == 0)){
		api_handle_dwarf_status();
	}else if ((strcmp(cmd, "DWARF_LOCALS") == 0) || (strcmp(cmd, "dwarf_locals") == 0)){
		api_handle_dwarf_vars(rest, TRUE);
	}else if ((strcmp(cmd, "DWARF_GLOBALS") == 0) || (strcmp(cmd, "dwarf_globals") == 0)){
		api_handle_dwarf_vars(rest, FALSE);
	}else if ((strcmp(cmd, "DWARF_VALUE") == 0) || (strcmp(cmd, "dwarf_value") == 0)){
		api_handle_dwarf_value(rest);
	}else if ((strcmp(cmd, "DWARF_WRITE") == 0) || (strcmp(cmd, "dwarf_write") == 0)){
		api_handle_dwarf_write(rest);
	}else if ((strcmp(cmd, "DWARF_TYPE") == 0) || (strcmp(cmd, "dwarf_type") == 0)){
		api_handle_dwarf_type(rest);
	}else if ((strcmp(cmd, "SD_STATUS") == 0) || (strcmp(cmd, "sd_status") == 0)){
		api_handle_sd_status();
	}else if ((strcmp(cmd, "SD_PAYLOAD") == 0) || (strcmp(cmd, "sd_payload") == 0)){
		api_handle_sd_payload(rest);
	}else if ((strcmp(cmd, "SD_TRACE") == 0) || (strcmp(cmd, "sd_trace") == 0)){
		api_handle_sd_trace(rest);
	}else if ((strcmp(cmd, "SD_FS_HISTORY") == 0) || (strcmp(cmd, "sd_fs_history") == 0)){
		api_handle_sd_fs_history(rest);
	}else if ((strcmp(cmd, "SD_TIMING_ANALYSIS") == 0) || (strcmp(cmd, "sd_timing_analysis") == 0)){
		api_handle_sd_timing_analysis(rest);
	}else if ((strcmp(cmd, "SD_FAULT") == 0) || (strcmp(cmd, "sd_fault") == 0)){
		api_handle_sd_fault(rest);
	}else if ((strcmp(cmd, "SD_REPLAY") == 0) || (strcmp(cmd, "sd_replay") == 0)){
		api_handle_sd_replay(rest);
	}else if ((strcmp(cmd, "SD_PRESET") == 0) || (strcmp(cmd, "sd_preset") == 0)){
		api_handle_sd_preset(rest);
	}else if ((strcmp(cmd, "SD_SET") == 0) || (strcmp(cmd, "sd_set") == 0)){
		api_handle_sd_set(rest);
	}else if ((strcmp(cmd, "SD_RESET") == 0) || (strcmp(cmd, "sd_reset") == 0)){
		mainui_reset_sd_card();
		api_handle_sd_status();
	}else if ((strcmp(cmd, "AUDIO_SCOPE") == 0) || (strcmp(cmd, "audio_scope") == 0)){
		api_handle_audio_scope(rest);
	}else if ((strcmp(cmd, "AUDIO_DEBUG") == 0) || (strcmp(cmd, "audio_debug") == 0)){
		api_handle_audio_debug(rest);
#ifdef ENABLE_ESP
	}else if ((strcmp(cmd, "SERIAL_STATUS") == 0) || (strcmp(cmd, "serial_status") == 0)){
		api_handle_serial_status();
	}else if ((strcmp(cmd, "SERIAL_SET") == 0) || (strcmp(cmd, "serial_set") == 0)){
		api_handle_serial_set(rest);
	}else if ((strcmp(cmd, "SERIAL_TRACE") == 0) || (strcmp(cmd, "serial_trace") == 0)){
		api_handle_serial_trace(rest);
	}else if ((strcmp(cmd, "TCP_DIAG") == 0) || (strcmp(cmd, "tcp_diag") == 0)){
		api_handle_tcp_diag(rest);
	}else if ((strcmp(cmd, "ESP_STATUS") == 0) || (strcmp(cmd, "esp_status") == 0)){
		api_handle_esp_status();
#endif
#ifdef ENABLE_NETPLAY
	}else if ((strcmp(cmd, "NETPLAY_STATUS") == 0) || (strcmp(cmd, "netplay_status") == 0)){
		api_handle_netplay_status();
#endif
	}else if ((strcmp(cmd, "EMU_STATUS") == 0) || (strcmp(cmd, "emu_status") == 0)){
		api_handle_emu_status();
	}else if ((strcmp(cmd, "EMU_SET") == 0) || (strcmp(cmd, "emu_set") == 0)){
		api_handle_emu_set(rest);
	}else if ((strcmp(cmd, "SAVESTATE") == 0) || (strcmp(cmd, "savestate") == 0)){
		api_handle_savestate(rest);
	}else if ((strcmp(cmd, "RECENT_ROMS") == 0) || (strcmp(cmd, "recent_roms") == 0)){
		api_handle_recent_roms(rest);
	}else if ((strcmp(cmd, "CONFIG") == 0) || (strcmp(cmd, "config") == 0)){
		api_handle_config(rest);
	}else if ((strcmp(cmd, "SET_INPUT") == 0) || (strcmp(cmd, "set_input") == 0)){
		api_handle_set_input(rest);
	}else if ((strcmp(cmd, "QUEUE_INPUT") == 0) || (strcmp(cmd, "queue_input") == 0)){
		api_handle_queue_input(rest);
	}else if ((strcmp(cmd, "CLEAR_INPUT") == 0) || (strcmp(cmd, "clear_input") == 0)){
		api_handle_clear_input(rest);
	}else if ((strcmp(cmd, "RUN_FRAMES") == 0) || (strcmp(cmd, "run_frames") == 0)){
		api_handle_run_frames(rest);
	}else if ((strcmp(cmd, "LOAD_ROM") == 0) || (strcmp(cmd, "load_rom") == 0)){
		api_handle_load_rom(rest);
	}else if ((strcmp(cmd, "SCREENSHOT_CAPTURE") == 0) || (strcmp(cmd, "screenshot_capture") == 0)){
		api_handle_screenshot_capture();
	}else if ((strcmp(cmd, "SCREENSHOT_READ") == 0) || (strcmp(cmd, "screenshot_read") == 0)){
		api_handle_screenshot_read(rest);
	}else if ((strcmp(cmd, "SCREENSHOT_CLEAR") == 0) || (strcmp(cmd, "screenshot_clear") == 0)){
		api_screenshot_buffer_clear(api_active_client());
		api_reply_ok_simple(NULL);
#ifdef ENABLE_DEBUGGER
	}else if ((strcmp(cmd, "BEAM_HISTORY") == 0) || (strcmp(cmd, "beam_history") == 0)){
		api_handle_beam_history(rest);
	}else if ((strcmp(cmd, "RASTER_BREAK") == 0) || (strcmp(cmd, "raster_break") == 0)){
		api_handle_raster_break(rest);
	}else if ((strcmp(cmd, "LOGIC_TRACE") == 0) || (strcmp(cmd, "logic_trace") == 0)){
		api_handle_logic_trace(rest);
	}else if ((strcmp(cmd, "SCANLINE_PROFILE") == 0) || (strcmp(cmd, "scanline_profile") == 0)){
		api_handle_scanline_profile(rest);
#endif
	}else if ((strcmp(cmd, "VIDEO_BEAM") == 0) || (strcmp(cmd, "video_beam") == 0)){
		api_handle_video_beam(rest);
	}else if ((strcmp(cmd, "SCREENSHOT") == 0) || (strcmp(cmd, "screenshot") == 0)){
		api_handle_screenshot(rest);
	}else if ((strcmp(cmd, "PAUSE") == 0) || (strcmp(cmd, "pause") == 0)){
		api_state.run_frames_remaining = 0U;
		mainui_debug_set_paused(TRUE);
		api_reply_ok_simple(NULL);
	}else if ((strcmp(cmd, "RESUME") == 0) || (strcmp(cmd, "resume") == 0) || (strcmp(cmd, "RUN") == 0) || (strcmp(cmd, "run") == 0)){
		mainui_debug_set_paused(FALSE);
		api_reply_ok_simple(NULL);
	}else if ((strcmp(cmd, "STEP_FRAME") == 0) || (strcmp(cmd, "step_frame") == 0)){
		api_state.run_frames_remaining = 0U;
		mainui_debug_step_frame();
		api_reply_ok_simple(NULL);
	}else if ((strcmp(cmd, "RESET") == 0) || (strcmp(cmd, "reset") == 0)){
		api_state.run_frames_remaining = 0U;
		api_wait_cancel_all("WAIT cancelled: emulator reset");
		api_input_clear_all();
		mainui_reset_rom();
		api_reply_ok_simple(NULL);
	}else if ((strcmp(cmd, "QUIT") == 0) || (strcmp(cmd, "quit") == 0)){
		api_reply_ok_simple(NULL);
		mainui_request_quit();
	}else{
		api_reply_error("Unknown command");
	}
}

static void api_try_open_listener(void)
{
	api_socket_t s;
	struct sockaddr_in sa;
	int opt = 1;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	WSADATA wsadata;
#endif
	uint32 now_ms = SDL_GetTicks();
	if (!api_state.enabled){ return; }
	if (api_state.listen_sock != API_INVALID_SOCKET){ return; }
	if (now_ms < api_state.retry_at_ms){ return; }
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if (!api_state.wsa_started){
		if (WSAStartup(MAKEWORD(2,2), &wsadata) != 0){
			api_state.retry_at_ms = now_ms + API_RETRY_MS;
			return;
		}
		api_state.wsa_started = TRUE;
	}
#endif
	{
		/* Try the configured port first, then the next instance pair
		** (port+2, port+4, ...) so a second CUzeBox, e.g. the other half of a
		** local link-cable pair, gets its own API port instead of sharing or
		** stealing the first one. SO_REUSEADDR is POSIX-only: on Windows it
		** lets two processes bind the same port. */
		auint k;
		s = API_INVALID_SOCKET;
		for (k = 0U; k < API_PORT_TRIES; ++k){
			auint port = api_state.port + (2U * k);
			if (port > 65535U){ break; }
			s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (s == API_INVALID_SOCKET){ break; }
#if !(defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__))
			setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
			(void)opt;
#endif
			if (!api_socket_set_nonblock(s)){
				API_CLOSESOCK(s);
				s = API_INVALID_SOCKET;
				break;
			}
			memset(&sa, 0, sizeof(sa));
			sa.sin_family = AF_INET;
			sa.sin_port = htons((uint16)(port & 0xFFFFU));
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if ((bind(s, (struct sockaddr*)&sa, (socklen_t)sizeof(sa)) == 0) &&
			    (listen(s, (int)API_MAX_CLIENTS) == 0)){
				api_state.bound_port = port;
				break;
			}
			API_CLOSESOCK(s);
			s = API_INVALID_SOCKET;
		}
		if (s == API_INVALID_SOCKET){
			api_state.retry_at_ms = now_ms + API_RETRY_MS;
			return;
		}
	}
	api_state.listen_sock = s;
	api_state.retry_at_ms = 0U;
	print_message("API: listening on 127.0.0.1:%u\n", (unsigned)api_state.bound_port);
	if (api_state.bound_port != api_state.port){
		print_message("API: port %u was busy (another CUzeBox instance?), using %u\n", (unsigned)api_state.port, (unsigned)api_state.bound_port);
	}
	mainui_system_message("API SERVER LISTENING: 127.0.0.1:%u", (unsigned)api_state.bound_port);
}

static void api_accept_clients(void)
{
	auint accepted = 0U;
	if (api_state.listen_sock == API_INVALID_SOCKET){ return; }
	while (accepted < API_MAX_CLIENTS){
		struct sockaddr_in peer;
		socklen_t plen = (socklen_t)sizeof(peer);
		api_socket_t cs = accept(api_state.listen_sock, (struct sockaddr*)&peer, &plen);
		auint slot;
		if (cs == API_INVALID_SOCKET){ return; }
		if (!api_socket_set_nonblock(cs)){
			API_CLOSESOCK(cs);
			continue;
		}
		for (slot = 0U; slot < API_MAX_CLIENTS; ++slot){
			if (api_state.clients[slot].sock == API_INVALID_SOCKET){ break; }
		}
		if (slot >= API_MAX_CLIENTS){
			API_CLOSESOCK(cs);
			continue;
		}
		api_state.clients[slot].sock = cs;
		api_state.clients[slot].rx_len = 0U;
		api_state.clients[slot].tx_len = 0U;
		api_state.clients[slot].tx_off = 0U;
		api_wait_state_clear(&api_state.clients[slot].wait);
		api_state.clients[slot].subscriptions = 0U;
		{
			char hello[160];
			snprintf(hello, sizeof(hello), "{\"ok\":1,\"hello\":\"CUzeBox API\",\"port\":%u,\"client\":%u,\"max_clients\":%u}",
				(unsigned)api_active_port(), (unsigned)slot, (unsigned)API_MAX_CLIENTS);
			api_queue_json_to(&api_state.clients[slot], hello);
		}
		accepted++;
		mainui_system_message("API CLIENT CONNECTED (%u ACTIVE)", (unsigned)api_client_count());
	}
}

static void api_flush_tx_client(auint slot)
{
	api_client_t* client;
	sint32 wr;
	if (slot >= API_MAX_CLIENTS){ return; }
	client = &api_state.clients[slot];
	if ((client->sock == API_INVALID_SOCKET) || (client->tx_len == client->tx_off)){ return; }
	wr = (sint32)send(client->sock,
		client->tx_buf + client->tx_off,
		(int)(client->tx_len - client->tx_off),
		0);
	if (wr <= 0){
		if (!api_would_block()){ api_close_client(slot); }
		return;
	}
	client->tx_off += (auint)wr;
	if (client->tx_off >= client->tx_len){
		client->tx_off = 0U;
		client->tx_len = 0U;
	}
}

static void api_consume_rx_client(auint slot)
{
	api_client_t* client;
	char line[2048];
	auint i;
	auint start = 0U;
	auint budget = 8U;
	if (slot >= API_MAX_CLIENTS){ return; }
	client = &api_state.clients[slot];
	for (i = 0U; (i < client->rx_len) && (budget != 0U); ++i){
		if (client->rx_buf[i] == '\n'){
			auint len = i - start;
			if (len >= sizeof(line)){ len = sizeof(line) - 1U; }
			memcpy(line, client->rx_buf + start, len);
			line[len] = 0;
			api_trim(line);
			if (line[0] != 0){
				api_state.active_client = slot;
				api_process_command(line);
				api_state.active_client = API_NO_CLIENT;
				if (client->sock == API_INVALID_SOCKET){ return; }
			}
			start = i + 1U;
			budget--;
		}
	}
	if (start != 0U){
		memmove(client->rx_buf, client->rx_buf + start, client->rx_len - start);
		client->rx_len -= start;
	}
}

static void api_recv_rx_client(auint slot)
{
	api_client_t* client;
	sint32 rd;
	if (slot >= API_MAX_CLIENTS){ return; }
	client = &api_state.clients[slot];
	if (client->sock == API_INVALID_SOCKET){ return; }
	if (client->rx_len >= (API_RX_CAP - 1U)){
		api_close_client(slot);
		return;
	}
	rd = (sint32)recv(client->sock, client->rx_buf + client->rx_len, (int)(API_RX_CAP - 1U - client->rx_len), 0);
	if (rd == 0){
		api_close_client(slot);
		return;
	}
	if (rd < 0){
		if (!api_would_block()){ api_close_client(slot); }
		return;
	}
	client->rx_len += (auint)rd;
	client->rx_buf[client->rx_len] = 0;
	api_consume_rx_client(slot);
}

void api_server_configure(boole enable, auint port)
{
	api_state_init_once();
	if (port == 0U){ port = API_SERVER_DEFAULT_PORT; }
	if ((!enable) && api_state.enabled){
		api_state.enabled = FALSE;
		api_state.run_frames_remaining = 0U;
		api_wait_clear_all();
		api_close_all_clients();
		api_close_listener();
		api_input_clear_all();
		mainui_system_message("API SERVER DISABLED");
		return;
	}
	if ((api_state.enabled == enable) && (api_state.port == port)){ return; }
	api_state.enabled = enable ? TRUE : FALSE;
	api_state.port = port;
	api_state.run_frames_remaining = 0U;
	api_wait_clear_all();
	api_close_all_clients();
	api_close_listener();
	if (api_state.enabled){
		api_try_open_listener();
	}
}

boole api_server_is_enabled(void)
{
	api_state_init_once();
	return api_state.enabled;
}

auint api_server_get_port(void)
{
	api_state_init_once();
	return api_active_port();
}

void api_server_tick(void)
{
	api_state_init_once();
	if (!api_state.enabled){ return; }
	api_try_open_listener();
	api_accept_clients();
	{
		auint i;
		for (i = 0U; i < API_MAX_CLIENTS; ++i){
			api_flush_tx_client(i);
			api_recv_rx_client(i);
			api_flush_tx_client(i);
		}
	}
}

void api_server_frame_begin(void)
{
	api_state_init_once();
	auint i;
	if (!api_state.enabled){ return; }
	for (i = 0U; i < API_INPUT_PORTS; ++i){
		api_input_prime_slot(i);
		if (api_state.input[i].active && (api_state.input[i].frames_remaining != 0U)){
			cu_ctr_setsnes(i, api_state.input[i].buttons);
		}
	}
}

void api_server_frame_end(void)
{
	api_state_init_once();
	auint i;
	if (!api_state.enabled){ return; }
	for (i = 0U; i < API_INPUT_PORTS; ++i){
		if (api_state.input[i].active && (api_state.input[i].frames_remaining != 0U)){
			api_state.input[i].frames_remaining--;
			if (api_state.input[i].frames_remaining == 0U){
				api_state.input[i].active = FALSE;
				api_state.input[i].buttons = 0U;
				cu_ctr_setsnes(i, 0U);
			}
		}
	}
	api_wait_check_now();
	{
		static uint32 event_div = 0U;
		static boole last_paused = FALSE;
		static boole pause_init = FALSE;
		boole paused_now = mainui_debug_is_paused();
		char eb[256];
		event_div++;
		if ((event_div % 6U) == 0U){
			snprintf(eb,sizeof(eb),"{\"event\":\"FRAME\",\"frame\":%u,\"paused\":%u}",(unsigned)mainui_get_frame_counter(),paused_now?1U:0U);
			api_broadcast_event(API_SUB_FRAME | API_SUB_CPU, eb);
		}
		if ((!pause_init) || (paused_now != last_paused)){
			snprintf(eb,sizeof(eb),"{\"event\":\"CPU\",\"frame\":%u,\"paused\":%u}",(unsigned)mainui_get_frame_counter(),paused_now?1U:0U);
			api_broadcast_event(API_SUB_CPU | API_SUB_BREAK | API_SUB_WATCH, eb);
			last_paused = paused_now; pause_init = TRUE;
		}
#ifdef ENABLE_ESP
		if ((event_div % 30U) == 0U){
			api_broadcast_event(API_SUB_SERIAL, "{\"event\":\"SERIAL\"}");
			api_broadcast_event(API_SUB_ESP, "{\"event\":\"ESP\"}");
		}
#endif
	}
	if (mainui_debug_is_paused()){
		api_state.run_frames_remaining = 0U;
		return;
	}
	if (api_state.run_frames_remaining != 0U){
		api_state.run_frames_remaining--;
		if (api_state.run_frames_remaining == 0U){
			mainui_debug_set_paused(TRUE);
		}
	}
}

void api_server_notify_rom_loaded(void)
{
	api_state_init_once();
	api_state.run_frames_remaining = 0U;
	api_wait_cancel_all("WAIT cancelled: ROM changed");
	api_input_clear_all();
}

void api_server_shutdown(void)
{
	api_state_init_once();
	api_state.enabled = FALSE;
	api_state.run_frames_remaining = 0U;
	api_wait_clear_all();
	api_close_all_clients();
	api_close_listener();
	mainui_system_message("API SERVER DISABLED");
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if (api_state.wsa_started){
		WSACleanup();
		api_state.wsa_started = FALSE;
	}
#endif
}

#endif
