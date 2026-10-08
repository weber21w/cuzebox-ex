#include "remote_roms.h"

#include "mainui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#if defined(ENABLE_REMOTE_ROMS_LIBCURL)
#include <curl/curl.h>
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <direct.h>
#define REMOTE_MKDIR(path) _mkdir(path)
#define REMOTE_POPEN(cmd, mode) _popen((cmd), (mode))
#define REMOTE_PCLOSE(fp) _pclose((fp))
#else
#include <sys/stat.h>
#include <sys/types.h>
#define REMOTE_MKDIR(path) mkdir(path, 0777)
#define REMOTE_POPEN(cmd, mode) popen((cmd), (mode))
#define REMOTE_PCLOSE(fp) pclose((fp))
#endif

typedef struct{
	char*	data;
	size_t	size;
	size_t	cap;
} remote_mem_t;

typedef struct{
	remote_roms_game_t	games[REMOTE_ROMS_MAX_GAMES];
	auint			count;
	char			status[256];
	char			last_rom_path[REMOTE_ROMS_PATH_CAP];
	char			host[REMOTE_ROMS_STR_CAP];
} remote_roms_state_t;

static remote_roms_state_t remote_state;

static void remote_copy_str(char* dst, auint cap, char const* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

#if !defined(ENABLE_REMOTE_ROMS)

void remote_roms_init(void)
{
	memset(&remote_state, 0, sizeof(remote_state));
	remote_copy_str(remote_state.host, sizeof(remote_state.host), "uzenet.us");
	remote_copy_str(remote_state.status, sizeof(remote_state.status), "Remote ROMs disabled in this build");
}

void remote_roms_shutdown(void)
{
}

void remote_roms_set_host(char const* host)
{
	(void)host;
}

char const* remote_roms_get_host(void)
{
	return remote_state.host;
}

boole remote_roms_refresh_list(void)
{
	remote_copy_str(remote_state.status, sizeof(remote_state.status), "Remote ROMs disabled in this build");
	return FALSE;
}

auint remote_roms_get_count(void)
{
	return 0u;
}

boole remote_roms_get_entry(auint idx, remote_roms_game_t* out)
{
	(void)idx;
	(void)out;
	return FALSE;
}

boole remote_roms_download_game(auint idx, boole run_after)
{
	(void)idx;
	(void)run_after;
	remote_copy_str(remote_state.status, sizeof(remote_state.status), "Remote ROMs disabled in this build");
	return FALSE;
}

boole remote_roms_download_all(void)
{
	remote_copy_str(remote_state.status, sizeof(remote_state.status), "Remote ROMs disabled in this build");
	return FALSE;
}

char const* remote_roms_get_status(void)
{
	return remote_state.status;
}

char const* remote_roms_get_last_rom_path(void)
{
	return remote_state.last_rom_path;
}

#else

static void remote_make_base_url(char* dst, auint cap)
{
	char host[REMOTE_ROMS_STR_CAP];
	size_t len;
	if ((dst == NULL) || (cap == 0U)){ return; }
	remote_copy_str(host, sizeof(host), remote_state.host);
	if (host[0] == 0){ remote_copy_str(host, sizeof(host), "uzenet.us"); }
	len = strlen(host);
	while ((len > 0U) && ((host[len - 1U] == '/') || (host[len - 1U] == '\\'))){
		host[len - 1U] = 0;
		len--;
	}
	if (strstr(host, "://") != NULL){
		remote_copy_str(dst, cap, host);
	}else{
		snprintf(dst, cap, "https://%s", host);
		dst[cap - 1U] = 0;
	}
}

static void remote_make_service_url(char* dst, auint cap, char const* rel)
{
	char base[256];
	remote_make_base_url(base, sizeof(base));
	if ((rel == NULL) || (rel[0] == 0)){
		remote_copy_str(dst, cap, base);
		return;
	}
	if (rel[0] == '/'){
		snprintf(dst, cap, "%s%s", base, rel);
	}else{
		snprintf(dst, cap, "%s/%s", base, rel);
	}
	dst[cap - 1U] = 0;
}

static void remote_set_status(char const* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(remote_state.status, sizeof(remote_state.status), fmt, ap);
	va_end(ap);
	remote_state.status[sizeof(remote_state.status) - 1U] = 0;
	print_message("remote: %s\n", remote_state.status);
}

static void remote_mem_free(remote_mem_t* mem)
{
	if (mem == NULL){ return; }
	if (mem->data != NULL){ free(mem->data); }
	mem->data = NULL;
	mem->size = 0U;
	mem->cap = 0U;
}

#if defined(ENABLE_REMOTE_ROMS_LIBCURL)

static boole remote_mem_reserve(remote_mem_t* mem, size_t need)
{
	char* tmp;
	size_t cap;

	if(mem == NULL)
		return FALSE;
	if(need <= mem->cap)
		return TRUE;
	cap = (mem->cap != 0u) ? mem->cap : 1024u;
	while(cap < need){
		size_t next = cap * 2u;
		if(next <= cap){
			cap = need;
			break;
		}
		cap = next;
	}
	tmp = (char*)realloc(mem->data, cap);
	if(tmp == NULL)
		return FALSE;
	mem->data = tmp;
	mem->cap = cap;
	return TRUE;
}

static size_t remote_curl_write_mem(void* ptr, size_t size, size_t nmemb, void* userdata)
{
	remote_mem_t* mem = (remote_mem_t*)userdata;
	size_t got = size * nmemb;

	if((mem == NULL) || (got == 0u))
		return got;
	if(!remote_mem_reserve(mem, mem->size + got + 1u))
		return 0u;
	memcpy(mem->data + mem->size, ptr, got);
	mem->size += got;
	mem->data[mem->size] = 0;
	return got;
}

static size_t remote_curl_write_file(void* ptr, size_t size, size_t nmemb, void* userdata)
{
	FILE* f = (FILE*)userdata;
	if(f == NULL)
		return 0u;
	return fwrite(ptr, size, nmemb, f);
}

static boole remote_http_request(remote_mem_t* out, char const* url, char const* post_fields, char const* out_path)
{
	CURL* curl;
	CURLcode rc;
	FILE* f = NULL;
	boole ok = FALSE;
	char errbuf[CURL_ERROR_SIZE];

	if(url == NULL)
		return FALSE;
	if((out == NULL) && (out_path == NULL))
		return FALSE;

	if(out != NULL){
		out->data = NULL;
		out->size = 0u;
		out->cap = 0u;
	}

	curl = curl_easy_init();
	if(curl == NULL){
		remote_set_status("Unable to initialize libcurl");
		return FALSE;
	}

	errbuf[0] = 0;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "CUzeBox-RemoteRoms/1.0");
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	if(post_fields != NULL){
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
	}

	if(out_path != NULL){
		f = fopen(out_path, "wb");
		if(f == NULL){
			remote_set_status("Unable to open output file: %s", out_path);
			curl_easy_cleanup(curl);
			return FALSE;
		}
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, remote_curl_write_file);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	}else{
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, remote_curl_write_mem);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
	}

	rc = curl_easy_perform(curl);
	if(rc == CURLE_OK){
		ok = TRUE;
	}else if(errbuf[0] != 0){
		remote_set_status("HTTP failed: %s", errbuf);
	}else{
		remote_set_status("HTTP failed: %s", curl_easy_strerror(rc));
	}

	if(f != NULL){
		fclose(f);
		if(!ok)
			remove(out_path);
	}
	curl_easy_cleanup(curl);
	if(!ok && (out != NULL))
		remote_mem_free(out);
	return ok;
}

static boole remote_http_get(remote_mem_t* out, char const* url)
{
	return remote_http_request(out, url, NULL, NULL);
}

static boole remote_http_post(remote_mem_t* out, char const* url, char const* form)
{
	return remote_http_request(out, url, form, NULL);
}

static boole remote_http_download(char const* url, char const* out_path)
{
	return remote_http_request(NULL, url, NULL, out_path);
}

#else

static boole remote_shell_quote(char* dst, auint cap, char const* src)
{
	auint di = 0U;
	auint si = 0U;
	if ((dst == NULL) || (cap < 3U) || (src == NULL)){ return FALSE; }
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	dst[di++] = '"';
	while (src[si] != 0){
		if (src[si] == '"'){
			return FALSE;
		}
		if ((di + 1U) >= cap){ return FALSE; }
		dst[di++] = src[si++];
	}
	if ((di + 1U) >= cap){ return FALSE; }
	dst[di++] = '"';
#else
	dst[di++] = 39;
	while (src[si] != 0){
		if (src[si] == 39){
			if ((di + 4U) >= cap){ return FALSE; }
			dst[di++] = 39;
			dst[di++] = '\\';
			dst[di++] = 39;
			dst[di++] = 39;
		}else{
			if ((di + 1U) >= cap){ return FALSE; }
			dst[di++] = src[si];
		}
		si++;
	}
	if ((di + 1U) >= cap){ return FALSE; }
	dst[di++] = 39;
#endif
	dst[di] = 0;
	return TRUE;
}

static boole remote_http_command(remote_mem_t* out, char const* cmd)
{
	FILE* fp;
	char  buf[512];
	size_t got;
	char* tmp;

	if ((out == NULL) || (cmd == NULL)){ return FALSE; }
	out->data = NULL;
	out->size = 0U;
	out->cap = 0U;
	fp = REMOTE_POPEN(cmd, "r");
	if (fp == NULL){
		remote_set_status("Unable to run curl command");
		return FALSE;
	}
	for (;;){
		got = fread(buf, 1U, sizeof(buf), fp);
		if (got == 0U){ break; }
		tmp = (char*)realloc(out->data, out->size + got + 1U);
		if (tmp == NULL){
			REMOTE_PCLOSE(fp);
			remote_mem_free(out);
			remote_set_status("Out of memory while reading remote data");
			return FALSE;
		}
		out->data = tmp;
		memcpy(out->data + out->size, buf, got);
		out->size += got;
		out->data[out->size] = 0;
	}
	if (REMOTE_PCLOSE(fp) != 0){
		remote_mem_free(out);
		return FALSE;
	}
	return TRUE;
}

static boole remote_http_get(remote_mem_t* out, char const* url)
{
	char qurl[2048];
	char cmd[2304];
	if (!remote_shell_quote(qurl, sizeof(qurl), url)){ return FALSE; }
	snprintf(cmd, sizeof(cmd), "curl -L --fail --silent --show-error %s 2>&1", qurl);
	return remote_http_command(out, cmd);
}

static boole remote_http_post(remote_mem_t* out, char const* url, char const* form)
{
	char qurl[2048];
	char qform[512];
	char cmd[3072];
	if (!remote_shell_quote(qurl, sizeof(qurl), url)){ return FALSE; }
	if (!remote_shell_quote(qform, sizeof(qform), form)){ return FALSE; }
	snprintf(cmd, sizeof(cmd), "curl -L --fail --silent --show-error -X POST -d %s %s 2>&1", qform, qurl);
	return remote_http_command(out, cmd);
}

static boole remote_http_download(char const* url, char const* out_path)
{
	char qurl[2048];
	char qout[2048];
	char cmd[4608];
	int  rc;
	if (!remote_shell_quote(qurl, sizeof(qurl), url)){ return FALSE; }
	if (!remote_shell_quote(qout, sizeof(qout), out_path)){ return FALSE; }
	snprintf(cmd, sizeof(cmd), "curl -L --fail --silent --show-error -o %s %s", qout, qurl);
	rc = system(cmd);
	return (rc == 0);
}

#endif

static void remote_sanitize_name(char* dst, auint cap, char const* src)
{
	auint di = 0U;
	auint si = 0U;
	boole prev_us = FALSE;
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	while ((src[si] != 0) && (di + 1U < cap)){
		unsigned char c = (unsigned char)src[si++];
		if (isalnum(c)){
			dst[di++] = (char)c;
			prev_us = FALSE;
		}else if ((c == '_') || (c == '-') || (c == '.')){
			dst[di++] = (char)c;
			prev_us = FALSE;
		}else{
			if (!prev_us){
				dst[di++] = '_';
				prev_us = TRUE;
			}
		}
	}
	while ((di > 0U) && (dst[di - 1U] == '_')){ di--; }
	dst[di] = 0;
	if (dst[0] == 0){ remote_copy_str(dst, cap, "unnamed"); }
}

static void remote_join_path(char* dst, auint cap, char const* a, char const* b)
{
	if ((a == NULL) || (a[0] == 0)){
		remote_copy_str(dst, cap, (b == NULL) ? "" : b);
		return;
	}
	if ((b == NULL) || (b[0] == 0)){
		remote_copy_str(dst, cap, a);
		return;
	}
	if ((a[strlen(a) - 1U] == '/') || (a[strlen(a) - 1U] == '\\')){
		snprintf(dst, cap, "%s%s", a, b);
	}else{
		snprintf(dst, cap, "%s/%s", a, b);
	}
	dst[cap - 1U] = 0;
}

static boole remote_mkdirs(char const* path)
{
	char tmp[REMOTE_ROMS_PATH_CAP];
	auint i;
	auint len;
	int rc;

	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	remote_copy_str(tmp, sizeof(tmp), path);
	len = (auint)strlen(tmp);
	if ((len > 0U) && ((tmp[len - 1U] == '/') || (tmp[len - 1U] == '\\'))){
		tmp[len - 1U] = 0;
	}
	for (i = 1U; tmp[i] != 0; i++){
		if ((tmp[i] == '/') || (tmp[i] == '\\')){
			char hold = tmp[i];
			tmp[i] = 0;
			rc = REMOTE_MKDIR(tmp);
			if ((rc != 0) && (errno != EEXIST)){
				tmp[i] = hold;
				return FALSE;
			}
			tmp[i] = hold;
		}
	}
	rc = REMOTE_MKDIR(tmp);
	if ((rc != 0) && (errno != EEXIST)){ return FALSE; }
	return TRUE;
}

static boole remote_json_decode_string(char const* src, char* dst, auint cap, char const** out_end)
{
	auint di = 0U;
	auint si = 0U;
	if ((src == NULL) || (dst == NULL) || (cap == 0U)){ return FALSE; }
	if (src[0] != '"'){ return FALSE; }
	si++;
	while (src[si] != 0){
		char c = src[si++];
		if (c == '"'){
			dst[di] = 0;
			if (out_end != NULL){ *out_end = src + si; }
			return TRUE;
		}
		if (c == '\\'){
			char esc = src[si++];
			if (esc == 0){ return FALSE; }
			switch (esc){
				case '"': c = '"'; break;
				case '\\': c = '\\'; break;
				case '/': c = '/'; break;
				case 'b': c = '\b'; break;
				case 'f': c = '\f'; break;
				case 'n': c = '\n'; break;
				case 'r': c = '\r'; break;
				case 't': c = '\t'; break;
				case 'u':
					c = '?';
					if (src[si] != 0){ si++; }
					if (src[si] != 0){ si++; }
					if (src[si] != 0){ si++; }
					if (src[si] != 0){ si++; }
					break;
				default: break;
			}
		}
		if (di + 1U >= cap){ return FALSE; }
		dst[di++] = c;
	}
	return FALSE;
}

static boole remote_json_find_value(char const* src, char const* key, char* out, auint cap, char const** out_after)
{
	char needle[128];
	char const* p;
	snprintf(needle, sizeof(needle), "\"%s\":", key);
	p = strstr(src, needle);
	if (p == NULL){ return FALSE; }
	p += strlen(needle);
	while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')){ p++; }
	if (!remote_json_decode_string(p, out, cap, out_after)){ return FALSE; }
	return TRUE;
}

static char const* remote_basename(char const* path)
{
	char const* p;
	char const* base;
	if (path == NULL){ return ""; }
	base = path;
	for (p = path; *p != 0; ++p){
		if ((*p == '/') || (*p == '\\')){ base = p + 1; }
	}
	return base;
}

static boole remote_parse_gamelist(char const* json)
{
	char const* p;
	remote_roms_game_t* game;
	char idbuf[32];
	char const* endp;

	if (json == NULL){ return FALSE; }
	remote_state.count = 0U;
	p = json;
	while ((p = strstr(p, "\"id\":\"")) != NULL){
		if (remote_state.count >= REMOTE_ROMS_MAX_GAMES){ break; }
		game = &(remote_state.games[remote_state.count]);
		memset(game, 0, sizeof(*game));
		p += 6;
		if (!remote_json_decode_string(p, idbuf, sizeof(idbuf), &endp)){ break; }
		game->id = (auint)strtoul(idbuf, NULL, 10);
		if (!remote_json_find_value(endp, "title", game->title, sizeof(game->title), &endp)){ break; }
		if (!remote_json_find_value(endp, "status", game->status, sizeof(game->status), &endp)){ break; }
		if (!remote_json_find_value(endp, "authors", game->authors, sizeof(game->authors), &endp)){ break; }
		remote_state.count++;
		p = endp;
	}
	return (remote_state.count != 0U);
}

static void remote_make_absolute_url(char* dst, auint cap, char const* src)
{
	if ((src == NULL) || (src[0] == 0)){
		remote_copy_str(dst, cap, "");
		return;
	}
	if ((strstr(src, "://") != NULL) || (strncmp(src, "data:", 5) == 0)){
		remote_copy_str(dst, cap, src);
		return;
	}
	if (src[0] == '/') {
		remote_make_service_url(dst, cap, src);
	} else {
		char rel[REMOTE_ROMS_PATH_CAP];
		snprintf(rel, sizeof(rel), "/UAM/APP_emu/%s", src);
		rel[sizeof(rel) - 1U] = 0;
		remote_make_service_url(dst, cap, rel);
	}
	dst[cap - 1U] = 0;
}

static boole remote_fetch_manifest_fallback(auint game_id, remote_mem_t* mem)
{
	char url[512];
	char rel[128];
	if (mem == NULL){ return FALSE; }
	snprintf(rel, sizeof(rel), "/UAM/APP_emu/emu.php?gameid=%u", (unsigned)game_id);
	rel[sizeof(rel) - 1U] = 0;
	remote_make_service_url(url, sizeof(url), rel);
	return remote_http_get(mem, url);
}

static boole remote_parse_manifest(char const* json, char urls[][REMOTE_ROMS_PATH_CAP], auint* url_count, char* gamefile, auint gamefile_cap)
{
	char const* p;
	char const* endp;
	char temp[REMOTE_ROMS_PATH_CAP];
	auint count = 0U;

	if ((json == NULL) || (urls == NULL) || (url_count == NULL) || (gamefile == NULL)){ return FALSE; }
	gamefile[0] = 0;
	if (!remote_json_find_value(json, "gamefile", gamefile, gamefile_cap, NULL)){
		return FALSE;
	}
	p = json;
	while ((p = strstr(p, "\"completefilepath\":")) != NULL){
		p += 19;
		if (count >= 64U){ break; }
		if (!remote_json_decode_string(p, temp, sizeof(temp), &endp)){ break; }
		remote_make_absolute_url(urls[count], REMOTE_ROMS_PATH_CAP, temp);
		count++;
		p = endp;
	}
	*url_count = count;
	return (count != 0U);
}

static boole remote_prepare_dest_dir(auint idx, char* out_dir, auint out_cap)
{
	char leaf[256];
	char safe_title[192];
	remote_roms_game_t const* game;
	if (idx >= remote_state.count){ return FALSE; }
	game = &(remote_state.games[idx]);
	remote_sanitize_name(safe_title, sizeof(safe_title), game->title);
	snprintf(leaf, sizeof(leaf), "%u_%s", (unsigned)game->id, safe_title);
	remote_join_path(out_dir, out_cap, mainui_get_rom_path(), leaf);
	return remote_mkdirs(out_dir);
}

void remote_roms_init(void)
{
	memset(&remote_state, 0, sizeof(remote_state));
	remote_copy_str(remote_state.host, sizeof(remote_state.host), "uzenet.us");
	#if defined(ENABLE_REMOTE_ROMS_LIBCURL)
	(void)curl_global_init(CURL_GLOBAL_DEFAULT);
	#endif
	remote_set_status("Remote ROMs idle");
}

void remote_roms_shutdown(void)
{
	#if defined(ENABLE_REMOTE_ROMS_LIBCURL)
	curl_global_cleanup();
	#endif
}

void remote_roms_set_host(char const* host)
{
	char cleaned[REMOTE_ROMS_STR_CAP];
	char* p;
	remote_copy_str(cleaned, sizeof(cleaned), (host == NULL) ? "" : host);
	for (p = cleaned; *p != 0; ++p){
		if ((*p == '\r') || (*p == '\n')){ *p = 0; break; }
	}
	if (cleaned[0] == 0){
		remote_copy_str(cleaned, sizeof(cleaned), "uzenet.us");
	}
	remote_copy_str(remote_state.host, sizeof(remote_state.host), cleaned);
}

char const* remote_roms_get_host(void)
{
	return remote_state.host;
}

boole remote_roms_refresh_list(void)
{
	char url[512];
	remote_mem_t mem;
	remote_make_service_url(url, sizeof(url), "/UAM/APP_emu/emu_p.php?o=emu_getBuiltInGamelist");
	mem.data = NULL;
	mem.size = 0U;
	mem.cap = 0U;
	if (!remote_http_get(&mem, url)){
		remote_set_status("Refresh failed: unable to fetch game list");
		return FALSE;
	}
	if (!remote_parse_gamelist(mem.data)){
		remote_mem_free(&mem);
		remote_set_status("Refresh failed: unable to parse game list");
		return FALSE;
	}
	remote_mem_free(&mem);
	remote_set_status("Loaded %u remote ROM entries", (unsigned)remote_state.count);
	return TRUE;
}

auint remote_roms_get_count(void)
{
	return remote_state.count;
}

boole remote_roms_get_entry(auint idx, remote_roms_game_t* out)
{
	if ((idx >= remote_state.count) || (out == NULL)){ return FALSE; }
	*out = remote_state.games[idx];
	return TRUE;
}

static boole remote_roms_download_idx(auint idx, boole run_after)
{
	char url[512];
	remote_mem_t mem;
	remote_make_service_url(url, sizeof(url), "/UAM/APP_emu/emu_p.php?o=emu_returnJSON_byGameId");
	char form[64];
	char urls[64][REMOTE_ROMS_PATH_CAP];
	auint url_count = 0U;
	char gamefile[256];
	char out_dir[REMOTE_ROMS_PATH_CAP];
	char out_path[REMOTE_ROMS_PATH_CAP];
	char run_path[REMOTE_ROMS_PATH_CAP];
	auint i;
	remote_roms_game_t const* game;

	if (idx >= remote_state.count){
		remote_set_status("Download failed: invalid selection");
		return FALSE;
	}
	game = &(remote_state.games[idx]);
	if (!remote_prepare_dest_dir(idx, out_dir, sizeof(out_dir))){
		remote_set_status("Download failed: unable to create ROM directory");
		return FALSE;
	}
	snprintf(form, sizeof(form), "gameId=%u", (unsigned)game->id);
	mem.data = NULL;
	mem.size = 0U;
	mem.cap = 0U;
	if (!remote_http_post(&mem, url, form)){
		mem.data = NULL;
		mem.size = 0U;
		mem.cap = 0U;
		if (!remote_fetch_manifest_fallback(game->id, &mem)){
			remote_set_status("Download failed for %s: manifest request failed", game->title);
			return FALSE;
		}
	}
	if (!remote_parse_manifest(mem.data, urls, &url_count, gamefile, sizeof(gamefile))){
		remote_mem_free(&mem);
		mem.data = NULL;
		mem.size = 0U;
		if (!remote_fetch_manifest_fallback(game->id, &mem) ||
		    !remote_parse_manifest(mem.data, urls, &url_count, gamefile, sizeof(gamefile))){
			remote_mem_free(&mem);
			remote_set_status("Download failed for %s: manifest parse failed", game->title);
			return FALSE;
		}
	}
	remote_mem_free(&mem);
	for (i = 0U; i < url_count; i++){
		remote_join_path(out_path, sizeof(out_path), out_dir, remote_basename(urls[i]));
		if (!remote_http_download(urls[i], out_path)){
			remote_set_status("Download failed for %s: %s", game->title, remote_basename(urls[i]));
			return FALSE;
		}
	}
	remote_join_path(run_path, sizeof(run_path), out_dir, gamefile);
	remote_copy_str(remote_state.last_rom_path, sizeof(remote_state.last_rom_path), run_path);
	if (run_after){
		if (!mainui_load_rom_file(run_path)){
			remote_set_status("Downloaded %s but failed to run it", game->title);
			return FALSE;
		}
		remote_set_status("Downloaded and loaded %s", game->title);
	}else{
		remote_set_status("Downloaded %s", game->title);
	}
	return TRUE;
}

boole remote_roms_download_game(auint idx, boole run_after)
{
	return remote_roms_download_idx(idx, run_after);
}

boole remote_roms_download_all(void)
{
	auint i;
	auint ok = 0U;
	if (remote_state.count == 0U){
		if (!remote_roms_refresh_list()){
			return FALSE;
		}
	}
	for (i = 0U; i < remote_state.count; i++){
		if (remote_roms_download_idx(i, FALSE)){
			ok++;
		}
	}
	remote_set_status("Download all finished: %u / %u succeeded", (unsigned)ok, (unsigned)remote_state.count);
	return (ok != 0U);
}

char const* remote_roms_get_status(void)
{
	return remote_state.status;
}

char const* remote_roms_get_last_rom_path(void)
{
	return remote_state.last_rom_path;
}

#endif
