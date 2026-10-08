#include "cu_master.h"
#include "conout.h"
#include "cu_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define CU_MASTER_CLOSESOCK closesocket
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#define CU_MASTER_CLOSESOCK close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
typedef int SOCKET;
#endif

#define CU_MASTER_DEFAULT_PORT             31492U
#define CU_MASTER_DEFAULT_ANNOUNCE_FRAMES  180U
#define CU_MASTER_DEFAULT_QUERY_FRAMES     300U
#define CU_MASTER_DEFAULT_MAX_PLAYERS      2U
#define CU_MASTER_DEFAULT_CUR_PLAYERS      1U
#define CU_MASTER_QUERY_TOKEN              0x435a4d51UL
#define CU_MASTER_LISTING_TIMEOUT_FRAMES   1800U

typedef struct{
	boole used;
	auint last_seen_frame;
	uint32 instance_id;
	uint32 session_id;
	uint32 source_ip;
	uint16 source_port;
	auint cur_players;
	auint max_players;
	boole is_public;
	boole in_progress;
	char game[CU_MASTER_STR_LEN];
	char name[CU_MASTER_STR_LEN];
	char room[CU_MASTER_STR_LEN];
	char join_host[CU_MASTER_STR_LEN];
	char proto[CU_MASTER_STR_LEN];
	uint16 join_port;
} cu_master_listing_t;

typedef struct{
	boole enabled;
	boole query_enabled;
	SOCKET sock;
	struct sockaddr_in server_addr;
	boole server_valid;
	auint announce_interval;
	auint query_interval;
	auint next_announce_frame;
	auint next_query_frame;
	auint last_frame;
	uint32 session_id;
	uint32 instance_id;
	char game[CU_MASTER_STR_LEN];
	char name[CU_MASTER_STR_LEN];
	char room[CU_MASTER_STR_LEN];
	auint cur_players;
	auint max_players;
	auint join_port;
	char join_host[CU_MASTER_STR_LEN];
	cu_master_listing_t listings[CU_MASTER_MAX_LISTINGS];
} cu_master_state_t;

static cu_master_state_t cu_master_state;

static uint32 cu_master_rand32(void)
{
	uint32 a = (uint32)rand();
	uint32 b = (uint32)rand();
	return (a << 16) ^ b ^ (uint32)time(NULL);
}

static void cu_master_copy_env(char* dst, size_t dsz, char const* env_name, char const* defv)
{
	char const* src = getenv(env_name);
	if ((src == NULL) || (src[0] == 0)){
		src = defv;
	}
	if (dsz == 0U){
		return;
	}
	strncpy(dst, src, dsz - 1U);
	dst[dsz - 1U] = 0;
}

static auint cu_master_get_env_u32(char const* env_name, auint defv)
{
	char const* src = getenv(env_name);
	char* endp;
	unsigned long v;
	if ((src == NULL) || (src[0] == 0)){
		return defv;
	}
	v = strtoul(src, &endp, 10);
	if ((endp == src) || (*endp != 0)){
		return defv;
	}
	return (auint)v;
}

static boole cu_master_get_env_bool(char const* env_name)
{
	char const* src = getenv(env_name);
	if (src == NULL){
		return FALSE;
	}
	if ((strcmp(src, "1") == 0) ||
		(strcmp(src, "true") == 0) ||
		(strcmp(src, "TRUE") == 0) ||
		(strcmp(src, "yes") == 0) ||
		(strcmp(src, "on") == 0)){
		return TRUE;
	}
	return FALSE;
}

static void cu_master_set_nonblocking(SOCKET sock)
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

static boole cu_master_setup_server(char const* host, auint port)
{
	struct addrinfo hints;
	struct addrinfo* res = NULL;
	struct addrinfo* rp;
	char portbuf[16];
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);
	if (getaddrinfo(host, portbuf, &hints, &res) != 0){
		return FALSE;
	}
	for (rp = res; rp != NULL; rp = rp->ai_next){
		if ((rp->ai_family == AF_INET) && (rp->ai_addrlen >= sizeof(struct sockaddr_in))){
			memcpy(&(cu_master_state.server_addr), rp->ai_addr, sizeof(struct sockaddr_in));
			cu_master_state.server_valid = TRUE;
			freeaddrinfo(res);
			return TRUE;
		}
	}
	freeaddrinfo(res);
	return FALSE;
}

static void cu_master_send_line(char const* line)
{
	if ((!cu_master_state.enabled) || (!cu_master_state.server_valid)){
		return;
	}
	(void)sendto(cu_master_state.sock,
		line,
		(int)strlen(line),
		0,
		(struct sockaddr*)(&(cu_master_state.server_addr)),
		(sizeof(cu_master_state.server_addr)));
}

static void cu_master_send_announce(char const* kind, auint frame)
{
	char buf[512];
	snprintf(buf,
		sizeof(buf),
		"CZMS/1 %s instance=%08X session=%08X game=%s name=%s room=%s cur=%u max=%u public=%u in_progress=%u join_port=%u join_host=%s frame=%u proto=rollback1\n",
		kind,
		(unsigned)cu_master_state.instance_id,
		(unsigned)cu_master_state.session_id,
		cu_master_state.game,
		cu_master_state.name,
		cu_master_state.room,
		(unsigned)cu_master_state.cur_players,
		(unsigned)cu_master_state.max_players,
		1U,
		(frame != 0U) ? 1U : 0U,
		(unsigned)cu_master_state.join_port,
		(cu_master_state.join_host[0] != 0) ? cu_master_state.join_host : "",
		(unsigned)frame);
	cu_master_send_line(buf);
}

static void cu_master_send_query(void)
{
	char buf[256];
	snprintf(buf,
		sizeof(buf),
		"CZMS/1 QUERY instance=%08X session=%08X token=%08X limit=%u\n",
		(unsigned)cu_master_state.instance_id,
		(unsigned)cu_master_state.session_id,
		(unsigned)CU_MASTER_QUERY_TOKEN,
		(unsigned)CU_MASTER_MAX_LISTINGS);
	cu_master_send_line(buf);
}

static char const* cu_master_get_token_value(char const* src, char const* key, char* dst, size_t dsz)
{
	char pattern[64];
	char const* pos;
	size_t i = 0U;
	if (dsz == 0U){
		return NULL;
	}
	snprintf(pattern, sizeof(pattern), "%s=", key);
	pos = strstr(src, pattern);
	if (pos == NULL){
		dst[0] = 0;
		return NULL;
	}
	pos += strlen(pattern);
	while ((pos[i] != 0) && (pos[i] != ' ') && (pos[i] != '\r') && (pos[i] != '\n') && (i < (dsz - 1U))){
		dst[i] = pos[i];
		i++;
	}
	dst[i] = 0;
	return pos;
}

static uint32 cu_master_parse_hex(char const* src)
{
	return (uint32)strtoul(src, NULL, 16);
}

static auint cu_master_parse_dec(char const* src, auint defv)
{
	char* endp;
	unsigned long v = strtoul(src, &endp, 10);
	if ((endp == src) || ((*endp != 0) && (*endp != '\r') && (*endp != '\n'))){
		return defv;
	}
	return (auint)v;
}

static cu_master_listing_t* cu_master_alloc_listing(uint32 instance_id)
{
	auint i;
	auint oldest = 0U;
	auint oldest_age = 0U;
	for (i = 0U; i < CU_MASTER_MAX_LISTINGS; i++){
		if ((!cu_master_state.listings[i].used) || (cu_master_state.listings[i].instance_id == instance_id)){
			return &(cu_master_state.listings[i]);
		}
		if ((i == 0U) || (WRAP32(cu_master_state.last_frame - cu_master_state.listings[i].last_seen_frame) > oldest_age)){
			oldest = i;
			oldest_age = WRAP32(cu_master_state.last_frame - cu_master_state.listings[i].last_seen_frame);
		}
	}
	return &(cu_master_state.listings[oldest]);
}

static void cu_master_absorb_listing(char const* msg, struct sockaddr_in const* srcaddr)
{
	char tok[CU_MASTER_STR_LEN];
	cu_master_listing_t* ent;
	uint32 instance_id;
	if ((strstr(msg, "CZMS/1 HOST ") == NULL) &&
		(strstr(msg, "CZMS/1 REPLY ") == NULL) &&
		(strstr(msg, "CZMS/1 LISTING ") == NULL) &&
		(strstr(msg, "CZMS/1 ANNOUNCE ") == NULL)){
		return;
	}
	if (cu_master_get_token_value(msg, "instance", tok, sizeof(tok)) == NULL){
		return;
	}
	instance_id = cu_master_parse_hex(tok);
	ent = cu_master_alloc_listing(instance_id);
	memset(ent, 0, sizeof(*ent));
	ent->used = TRUE;
	ent->instance_id = instance_id;
	ent->last_seen_frame = cu_master_state.last_frame;
	ent->source_ip = ntohl(srcaddr->sin_addr.s_addr);
	ent->source_port = ntohs(srcaddr->sin_port);
	if (cu_master_get_token_value(msg, "session", tok, sizeof(tok)) != NULL){
		ent->session_id = cu_master_parse_hex(tok);
	}
	if (cu_master_get_token_value(msg, "cur", tok, sizeof(tok)) != NULL){
		ent->cur_players = cu_master_parse_dec(tok, CU_MASTER_DEFAULT_CUR_PLAYERS);
	}
	if (cu_master_get_token_value(msg, "max", tok, sizeof(tok)) != NULL){
		ent->max_players = cu_master_parse_dec(tok, CU_MASTER_DEFAULT_MAX_PLAYERS);
	}
	if (cu_master_get_token_value(msg, "public", tok, sizeof(tok)) != NULL){
		ent->is_public = (cu_master_parse_dec(tok, 1U) != 0U);
	}
	if (cu_master_get_token_value(msg, "in_progress", tok, sizeof(tok)) != NULL){
		ent->in_progress = (cu_master_parse_dec(tok, 0U) != 0U);
	}
	if (cu_master_get_token_value(msg, "join_port", tok, sizeof(tok)) != NULL){
		ent->join_port = (uint16)cu_master_parse_dec(tok, 0U);
	}
	if (cu_master_get_token_value(msg, "game", ent->game, sizeof(ent->game)) == NULL){
		strcpy(ent->game, "CUzeBox");
	}
	if (cu_master_get_token_value(msg, "name", ent->name, sizeof(ent->name)) == NULL){
		strcpy(ent->name, "host");
	}
	if (cu_master_get_token_value(msg, "room", ent->room, sizeof(ent->room)) == NULL){
		strcpy(ent->room, "public");
	}
	if (cu_master_get_token_value(msg, "join_host", ent->join_host, sizeof(ent->join_host)) == NULL){
		ent->join_host[0] = 0;
	}
	if (cu_master_get_token_value(msg, "proto", ent->proto, sizeof(ent->proto)) == NULL){
		strcpy(ent->proto, "rollback1");
	}
}

static void cu_master_poll_recv(void)
{
	char buf[1024];
	struct sockaddr_in srcaddr;
	socklen_t srclen = sizeof(srcaddr);
	int rv;
	while (cu_master_state.enabled){
		rv = recvfrom(cu_master_state.sock, buf, (int)(sizeof(buf) - 1U), 0, (struct sockaddr*)(&srcaddr), &srclen);
		if (rv <= 0){
			break;
		}
		buf[rv] = 0;
		cu_master_absorb_listing(buf, &srcaddr);
		srclen = sizeof(srcaddr);
	}
}

static void cu_master_prune(void)
{
	auint i;
	for (i = 0U; i < CU_MASTER_MAX_LISTINGS; i++){
		if (cu_master_state.listings[i].used){
			if (WRAP32(cu_master_state.last_frame - cu_master_state.listings[i].last_seen_frame) > CU_MASTER_LISTING_TIMEOUT_FRAMES){
				memset(&(cu_master_state.listings[i]), 0, sizeof(cu_master_state.listings[i]));
			}
		}
	}
}

void cu_master_init(void)
{
	char const* host;
	auint port;
	memset(&cu_master_state, 0, sizeof(cu_master_state));
	host = getenv("CUZEBOX_MASTER_HOST");
	if ((host == NULL) || (host[0] == 0)){
		return;
	}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0){
			return;
		}
	}
#endif
	cu_master_state.sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (cu_master_state.sock == INVALID_SOCKET){
		return;
	}
	cu_master_set_nonblocking(cu_master_state.sock);
	port = cu_master_get_env_u32("CUZEBOX_MASTER_PORT", CU_MASTER_DEFAULT_PORT);
	if (!cu_master_setup_server(host, port)){
		CU_MASTER_CLOSESOCK(cu_master_state.sock);
		cu_master_state.sock = INVALID_SOCKET;
		return;
	}
	cu_master_state.enabled = TRUE;
	cu_master_state.query_enabled = cu_master_get_env_bool("CUZEBOX_MASTER_QUERY");
	cu_master_state.announce_interval = cu_master_get_env_u32("CUZEBOX_MASTER_ANNOUNCE_FRAMES", CU_MASTER_DEFAULT_ANNOUNCE_FRAMES);
	cu_master_state.query_interval    = cu_master_get_env_u32("CUZEBOX_MASTER_QUERY_FRAMES", CU_MASTER_DEFAULT_QUERY_FRAMES);
	cu_master_state.cur_players       = cu_master_get_env_u32("CUZEBOX_MASTER_CUR", CU_MASTER_DEFAULT_CUR_PLAYERS);
	cu_master_state.max_players       = cu_master_get_env_u32("CUZEBOX_MASTER_MAX", CU_MASTER_DEFAULT_MAX_PLAYERS);
	cu_master_state.join_port         = cu_master_get_env_u32("CUZEBOX_MASTER_JOIN_PORT", 0U);
	cu_master_copy_env(cu_master_state.game, sizeof(cu_master_state.game), "CUZEBOX_MASTER_GAME", "CUzeBox");
	cu_master_copy_env(cu_master_state.name, sizeof(cu_master_state.name), "CUZEBOX_MASTER_NAME", "host");
	cu_master_copy_env(cu_master_state.room, sizeof(cu_master_state.room), "CUZEBOX_MASTER_ROOM", "public");
	cu_master_copy_env(cu_master_state.join_host, sizeof(cu_master_state.join_host), "CUZEBOX_MASTER_JOIN_HOST", "");
	cu_master_state.session_id = cu_master_rand32();
	cu_master_state.instance_id = cu_master_rand32();
	cu_master_state.next_announce_frame = 0U;
	cu_master_state.next_query_frame = 0U;
	print_message("Master discovery enabled: %s:%u\n", host, (unsigned)port);
	cu_master_send_announce("ANNOUNCE", 0U);
	if (cu_master_state.query_enabled){
		cu_master_send_query();
	}
}

void cu_master_reset(void)
{
	if (!cu_master_state.enabled){
		return;
	}
	cu_master_state.session_id = cu_master_rand32();
	cu_master_state.next_announce_frame = 0U;
	cu_master_state.next_query_frame = 0U;
	memset(&(cu_master_state.listings[0]), 0, sizeof(cu_master_state.listings));
	cu_master_send_announce("RESET", 0U);
	if (cu_master_state.query_enabled){
		cu_master_send_query();
	}
}

void cu_master_quit(void)
{
	char buf[256];
	if (!cu_master_state.enabled){
		return;
	}
	snprintf(buf, sizeof(buf), "CZMS/1 BYE instance=%08X session=%08X\n",
		(unsigned)cu_master_state.instance_id,
		(unsigned)cu_master_state.session_id);
	cu_master_send_line(buf);
	CU_MASTER_CLOSESOCK(cu_master_state.sock);
	cu_master_state.sock = INVALID_SOCKET;
	cu_master_state.enabled = FALSE;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	WSACleanup();
#endif
}

void cu_master_update(auint frame)
{
	if (!cu_master_state.enabled){
		return;
	}
	cu_master_state.last_frame = frame;
	cu_master_poll_recv();
	cu_master_prune();
	if (frame >= cu_master_state.next_announce_frame){
		cu_master_send_announce("ANNOUNCE", frame);
		cu_master_state.next_announce_frame = frame + cu_master_state.announce_interval;
	}
	if (cu_master_state.query_enabled){
		if (frame >= cu_master_state.next_query_frame){
			cu_master_send_query();
			cu_master_state.next_query_frame = frame + cu_master_state.query_interval;
		}
	}
}

void cu_master_request_query(void)
{
	if (!cu_master_state.enabled){
		return;
	}
	cu_master_send_query();
}

void cu_master_print_browser(void)
{
	auint i;
	if (!cu_master_state.enabled){
		print_unf("Master discovery disabled\n");
		return;
	}
	print_unf("Master browser list:\n");
	for (i = 0U; i < CU_MASTER_MAX_LISTINGS; i++){
		cu_master_listing_t const* ent = &(cu_master_state.listings[i]);
		if (!ent->used){
			continue;
		}
		char hostbuf[32];
		struct in_addr ia;
		ia.s_addr = htonl(ent->source_ip);
		strncpy(hostbuf, inet_ntoa(ia), sizeof(hostbuf) - 1U);
		hostbuf[sizeof(hostbuf) - 1U] = 0;
		print_message("%02u: %s by %s room=%s %u/%u public=%u progress=%u join=%s:%u proto=%s instance=%08X session=%08X\n",
			(unsigned)i,
			ent->game,
			ent->name,
			ent->room,
			(unsigned)ent->cur_players,
			(unsigned)ent->max_players,
			(unsigned)ent->is_public,
			(unsigned)ent->in_progress,
			(ent->join_host[0] != 0) ? ent->join_host : hostbuf,
			(unsigned)ent->join_port,
			ent->proto,
			(unsigned)ent->instance_id,
			(unsigned)ent->session_id);
	}
}

boole cu_master_is_enabled(void)
{
	return cu_master_state.enabled;
}

boole cu_master_is_query_enabled(void)
{
	return cu_master_state.query_enabled;
}
