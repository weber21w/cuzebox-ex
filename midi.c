/*
 *  MIDI backends (host MIDI + virtual MIDI device)
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
 */

#include "cu_esp.h"
#include "midi.h"

#if !defined(ENABLE_ESP)

sint32 cu_esp_host_midi_start(void){ return ESP_SERIAL_OPEN_ERROR; }
void cu_esp_host_midi_end(void){}
void cu_esp_host_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_host_midi_read(void){ return 0u; }
auint cu_esp_host_midi_rx_bytes_ready(void){ return 0u; }
boole cu_esp_host_midi_supported(void){ return FALSE; }
void cu_esp_host_midi_refresh_ports(void){}
auint cu_esp_host_midi_get_port_count(void){ return 0u; }
char const* cu_esp_host_midi_get_port_name(auint idx){ (void)idx; return ""; }
char const* cu_esp_host_midi_get_port_label(auint idx){ (void)idx; return ""; }

sint32 cu_esp_virtual_midi_start(void){ return ESP_SERIAL_OPEN_ERROR; }
void cu_esp_virtual_midi_end(void){}
void cu_esp_virtual_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_virtual_midi_read(void){ return 0u; }
auint cu_esp_virtual_midi_rx_bytes_ready(void){ return 0u; }
boole cu_esp_virtual_midi_supported(void){ return FALSE; }

sint32 cu_esp_serial_midi_start(auint route){ (void)route; return ESP_SERIAL_OPEN_ERROR; }
void cu_esp_serial_midi_end(void){}
void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0u; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0u; }
boole cu_esp_serial_midi_supported(auint route){ (void)route; return FALSE; }

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifndef __EMSCRIPTEN__
	#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
		#include <windows.h>
		#include <mmsystem.h>
		#if defined(_MSC_VER)
			#pragma comment(lib, "winmm.lib")
		#endif
	#elif defined(__linux__) || defined(__APPLE__)
		#include <pthread.h>
		#if defined(__linux__)
			#include <alsa/asoundlib.h>
		#elif defined(__APPLE__)
			#include <CoreMIDI/CoreMIDI.h>
			#include <CoreFoundation/CoreFoundation.h>
		#endif
	#endif
#endif

extern cu_state_esp_t esp_state;

#define CU_ESP_MIDI_ROUTE_NONE 0u
#define CU_ESP_MIDI_ROUTE_HOST 1u
#define CU_ESP_MIDI_ROUTE_VIRTUAL 2u

#define CU_ESP_HOST_MIDI_RX_CAP 4096u
#define CU_ESP_HOST_MIDI_TX_CAP 4096u
#define CU_ESP_HOST_MIDI_WIN_SYSEX_CAP 1024u

#define CU_ESP_HOST_MIDI_ENUM_MAX 64u
#define CU_ESP_HOST_MIDI_ENUM_NAME_CAP 128u
#define CU_ESP_HOST_MIDI_ENUM_LABEL_CAP 160u

typedef struct {
	char	raw[CU_ESP_HOST_MIDI_ENUM_NAME_CAP];
	char	label[CU_ESP_HOST_MIDI_ENUM_LABEL_CAP];
	uint8	has_in;
	uint8	has_out;
} cu_host_midi_enum_entry_t;

typedef struct {
	uint8	route_kind;
	uint8	rx_buf[CU_ESP_HOST_MIDI_RX_CAP];
	auint	rx_in;
	auint	rx_out;
	uint8	tx_msg[CU_ESP_HOST_MIDI_TX_CAP];
	auint	tx_len;
	uint8	tx_status;
	uint8	tx_expected;
	uint8	tx_data_count;
	uint8	tx_running_status;
	uint8	tx_in_sysex;
	uint8	lock_ready;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	CRITICAL_SECTION	rx_cs;
	HMIDIOUT	out_handle;
	HMIDIIN	in_handle;
	MIDIHDR	out_sysex_hdr;
	uint8	out_sysex_buf[CU_ESP_HOST_MIDI_TX_CAP];
	MIDIHDR	in_sysex_hdr[2];
	uint8	in_sysex_buf[2][CU_ESP_HOST_MIDI_WIN_SYSEX_CAP];
	char	port_in_name[CU_ESP_HOST_MIDI_ENUM_NAME_CAP];
	char	port_out_name[CU_ESP_HOST_MIDI_ENUM_NAME_CAP];
	char	virtual_pub_name[CU_ESP_HOST_MIDI_ENUM_NAME_CAP];
	char	virtual_dev_name[CU_ESP_HOST_MIDI_ENUM_NAME_CAP];
	char	wms_midi_exe[MAX_PATH];
	char	wms_association_id[64];
	char	wms_unique_id[64];
	uint8	wms_loopback_owned;
#elif defined(__linux__) || defined(__APPLE__)
	pthread_mutex_t	rx_mtx;
	#if defined(__linux__)
		snd_seq_t	*seq;
		snd_midi_event_t	*encoder;
		snd_midi_event_t	*decoder;
		sint32	port_in;
		sint32	port_out;
	#elif defined(__APPLE__)
		MIDIClientRef	client;
		MIDIEndpointRef	source;
		MIDIEndpointRef	destination;
	#endif
#endif
} cu_midi_runtime_t;

static cu_midi_runtime_t cu_host_midi;
static cu_midi_runtime_t cu_virtual_midi;
static cu_host_midi_enum_entry_t cu_host_midi_enum[CU_ESP_HOST_MIDI_ENUM_MAX];
static auint cu_host_midi_enum_count = 0u;
static auint cu_serial_midi_active = CU_ESP_SERIAL_DISCONNECTED;

static void cu_midi_s8cpy(sint8 *dst, size_t dsz, const char *src)
{
	if((dst == NULL) || (dsz == 0u))
		return;

	if(src == NULL){
		dst[0] = '\0';
		return;
	}

	strncpy((char *)dst, src, dsz - 1u);
	dst[dsz - 1u] = '\0';
}

static boole cu_esp_strieq(char const *a, char const *b)
{
	if(a == NULL || b == NULL)
		return FALSE;

	while(*a && *b){
		if(tolower((unsigned char)(*a)) != tolower((unsigned char)(*b)))
			return FALSE;
		a++;
		b++;
	}

	return ((*a == '\0') && (*b == '\0')) ? TRUE : FALSE;
}

static boole cu_esp_stricontains(char const *hay, char const *needle)
{
	char const *h;
	size_t nlen;
	size_t i;

	if(hay == NULL || needle == NULL)
		return FALSE;
	if(needle[0] == '\0')
		return TRUE;

	nlen = strlen(needle);
	for(h = hay; *h; ++h){
		for(i = 0u; i < nlen; ++i){
			if(h[i] == '\0')
				return FALSE;
			if(tolower((unsigned char)h[i]) != tolower((unsigned char)needle[i]))
				break;
		}
		if(i == nlen)
			return TRUE;
	}

	return FALSE;
}

static char const* cu_virtual_midi_mode_base_name(void)
{
	if(((char const *)esp_state.virtual_midi_port_name)[0] != '\0')
		return (char const *)esp_state.virtual_midi_port_name;
	return "CUzeBox MIDI";
}

static char const* cu_virtual_midi_mode_name(auint mode)
{
	switch(mode){
		case CU_ESP_VIRTUAL_MIDI_INSTRUMENT: return "instrument";
		case CU_ESP_VIRTUAL_MIDI_CONTROLLER: return "controller";
		default: return "bidirectional";
	}
}

static boole cu_esp_host_midi_backend_supported(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__linux__) || defined(__APPLE__)
	return TRUE;
#else
	return FALSE;
#endif
}

static boole cu_esp_virtual_midi_backend_supported(void)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__linux__) || defined(__APPLE__)
	return TRUE;
#else
	return FALSE;
#endif
}

static void cu_midi_lock_init(cu_midi_runtime_t *rt)
{
	if((rt == NULL) || rt->lock_ready)
		return;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	InitializeCriticalSection(&rt->rx_cs);
	rt->out_handle = NULL;
	rt->in_handle = NULL;
	memset(&rt->out_sysex_hdr, 0, sizeof(rt->out_sysex_hdr));
	memset(&rt->in_sysex_hdr, 0, sizeof(rt->in_sysex_hdr));
	memset(rt->port_in_name, 0, sizeof(rt->port_in_name));
	memset(rt->port_out_name, 0, sizeof(rt->port_out_name));
	memset(rt->virtual_pub_name, 0, sizeof(rt->virtual_pub_name));
	memset(rt->virtual_dev_name, 0, sizeof(rt->virtual_dev_name));
	memset(rt->wms_midi_exe, 0, sizeof(rt->wms_midi_exe));
	memset(rt->wms_association_id, 0, sizeof(rt->wms_association_id));
	memset(rt->wms_unique_id, 0, sizeof(rt->wms_unique_id));
	rt->wms_loopback_owned = 0u;
#elif defined(__linux__) || defined(__APPLE__)
	pthread_mutex_init(&rt->rx_mtx, NULL);
	#if defined(__linux__)
		rt->seq = NULL;
		rt->encoder = NULL;
		rt->decoder = NULL;
		rt->port_in = -1;
		rt->port_out = -1;
	#elif defined(__APPLE__)
		rt->client = 0;
		rt->source = 0;
		rt->destination = 0;
	#endif
#endif
	rt->lock_ready = 1u;
}

static void cu_midi_lock(cu_midi_runtime_t *rt)
{
	if((rt == NULL) || !rt->lock_ready)
		return;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	EnterCriticalSection(&rt->rx_cs);
#elif defined(__linux__) || defined(__APPLE__)
	pthread_mutex_lock(&rt->rx_mtx);
#endif
}

static void cu_midi_unlock(cu_midi_runtime_t *rt)
{
	if((rt == NULL) || !rt->lock_ready)
		return;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	LeaveCriticalSection(&rt->rx_cs);
#elif defined(__linux__) || defined(__APPLE__)
	pthread_mutex_unlock(&rt->rx_mtx);
#endif
}

static void cu_midi_rx_clear(cu_midi_runtime_t *rt)
{
	if(rt == NULL)
		return;
	cu_midi_lock(rt);
	rt->rx_in = 0u;
	rt->rx_out = 0u;
	cu_midi_unlock(rt);
}

static void cu_midi_rx_push(cu_midi_runtime_t *rt, uint8 const *data, auint len)
{
	auint i;

	if((rt == NULL) || (data == NULL) || (len == 0u))
		return;

	cu_midi_lock(rt);
	for(i = 0u; i < len; ++i){
		auint next = (rt->rx_in + 1u) % CU_ESP_HOST_MIDI_RX_CAP;
		if(next == rt->rx_out)
			break;
		rt->rx_buf[rt->rx_in] = data[i];
		rt->rx_in = next;
	}
	cu_midi_unlock(rt);
}

static auint cu_midi_rx_ready_local(cu_midi_runtime_t *rt)
{
	auint ret;
	if(rt == NULL)
		return 0u;
	cu_midi_lock(rt);
	ret = (rt->rx_in != rt->rx_out) ? 1u : 0u;
	cu_midi_unlock(rt);
	return ret;
}

static uint8 cu_midi_rx_pop(cu_midi_runtime_t *rt)
{
	uint8 ret = 0u;

	if(rt == NULL)
		return 0u;

	cu_midi_lock(rt);
	if(rt->rx_in != rt->rx_out){
		ret = rt->rx_buf[rt->rx_out];
		rt->rx_out = (rt->rx_out + 1u) % CU_ESP_HOST_MIDI_RX_CAP;
	}
	cu_midi_unlock(rt);

	return ret;
}

static void cu_midi_tx_reset(cu_midi_runtime_t *rt)
{
	if(rt == NULL)
		return;
	rt->tx_len = 0u;
	rt->tx_status = 0u;
	rt->tx_expected = 0u;
	rt->tx_data_count = 0u;
	rt->tx_running_status = 0u;
	rt->tx_in_sysex = 0u;
}

static uint8 cu_midi_data_bytes(uint8 status)
{
	if(status < 0x80u)
		return 0u;

	if(status < 0xF0u){
		switch(status & 0xF0u){
			case 0xC0u:
			case 0xD0u:
				return 1u;
			default:
				return 2u;
		}
	}

	switch(status){
		case 0xF1u:
		case 0xF3u:
			return 1u;
		case 0xF2u:
			return 2u;
		case 0xF6u:
		case 0xF7u:
		case 0xF8u:
		case 0xF9u:
		case 0xFAu:
		case 0xFBu:
		case 0xFCu:
		case 0xFDu:
		case 0xFEu:
		case 0xFFu:
			return 0u;
		default:
			return 0u;
	}
}

static auint cu_midi_bytes_for_status(uint8 status)
{
	return (auint)cu_midi_data_bytes(status) + 1u;
}

static void cu_esp_host_midi_enum_clear(void)
{
	cu_host_midi_enum_count = 0u;
	memset(cu_host_midi_enum, 0, sizeof(cu_host_midi_enum));
}

static void cu_esp_host_midi_enum_refresh_label(auint idx)
{
	cu_host_midi_enum_entry_t *ent;
	char const *tag;

	if(idx >= cu_host_midi_enum_count)
		return;

	ent = &cu_host_midi_enum[idx];
	if(ent->raw[0] == '\0'){
		strncpy(ent->label, "DEFAULT / FIRST AVAILABLE", CU_ESP_HOST_MIDI_ENUM_LABEL_CAP - 1u);
		ent->label[CU_ESP_HOST_MIDI_ENUM_LABEL_CAP - 1u] = '\0';
		return;
	}

	if(ent->has_in && ent->has_out)
		tag = "[IN/OUT]";
	else if(ent->has_in)
		tag = "[IN]";
	else if(ent->has_out)
		tag = "[OUT]";
	else
		tag = "";

	if(tag[0] != '\0')
		snprintf(ent->label, CU_ESP_HOST_MIDI_ENUM_LABEL_CAP, "%s %s", ent->raw, tag);
	else
		strncpy(ent->label, ent->raw, CU_ESP_HOST_MIDI_ENUM_LABEL_CAP - 1u);
	ent->label[CU_ESP_HOST_MIDI_ENUM_LABEL_CAP - 1u] = '\0';
}

static sint32 cu_esp_host_midi_enum_find(char const *raw)
{
	auint i;

	for(i = 0u; i < cu_host_midi_enum_count; ++i){
		if(cu_esp_strieq(cu_host_midi_enum[i].raw, (raw != NULL) ? raw : ""))
			return (sint32)i;
	}

	return -1;
}

static void cu_esp_host_midi_enum_add(char const *raw, boole has_in, boole has_out)
{
	sint32 idx;
	cu_host_midi_enum_entry_t *ent;

	idx = cu_esp_host_midi_enum_find((raw != NULL) ? raw : "");
	if(idx < 0){
		if(cu_host_midi_enum_count >= CU_ESP_HOST_MIDI_ENUM_MAX)
			return;
		idx = (sint32)cu_host_midi_enum_count;
		cu_host_midi_enum_count++;
		ent = &cu_host_midi_enum[(auint)idx];
		memset(ent, 0, sizeof(*ent));
		cu_midi_s8cpy((sint8*)ent->raw, sizeof(ent->raw), (raw != NULL) ? raw : "");
	}else{
		ent = &cu_host_midi_enum[(auint)idx];
	}

	if(has_in)
		ent->has_in = 1u;
	if(has_out)
		ent->has_out = 1u;
	cu_esp_host_midi_enum_refresh_label((auint)idx);
}

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)

static void CALLBACK cu_esp_host_midi_in_callback(HMIDIIN hmi, UINT msg,
	DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
	cu_midi_runtime_t *rt = (instance != 0u) ? (cu_midi_runtime_t *)instance : &cu_host_midi;
	(void)hmi;
	(void)param2;

	if((rt == &cu_host_midi) && !esp_state.host_midi_enabled)
		return;
	if((rt == &cu_virtual_midi) && !esp_state.virtual_midi_enabled)
		return;

	if(msg == MIM_DATA || msg == MIM_MOREDATA){
		DWORD dw = (DWORD)param1;
		uint8 bytes[3];
		auint len;
		bytes[0] = (uint8)(dw & 0xFFu);
		bytes[1] = (uint8)((dw >> 8) & 0xFFu);
		bytes[2] = (uint8)((dw >> 16) & 0xFFu);
		len = cu_midi_bytes_for_status(bytes[0]);
		if(len > 3u)
			len = 3u;
		cu_midi_rx_push(rt, bytes, len);
	}else if(msg == MIM_LONGDATA){
		MIDIHDR *hdr = (MIDIHDR *)param1;
		if((hdr != NULL) && (hdr->dwBytesRecorded != 0u)){
			cu_midi_rx_push(rt, (uint8 const *)hdr->lpData, (auint)hdr->dwBytesRecorded);
			hdr->dwBytesRecorded = 0u;
			(void)midiInAddBuffer(hmi, hdr, sizeof(MIDIHDR));
		}
	}
}

static UINT cu_esp_host_midi_find_out_port(char const *name, boole *found)
{
	UINT count = midiOutGetNumDevs();
	UINT i;
	MIDIOUTCAPSA caps;

	*found = FALSE;
	if(count == 0u)
		return MIDI_MAPPER;
	if(name == NULL || name[0] == '\0'){
		*found = TRUE;
		return MIDI_MAPPER;
	}

	for(i = 0u; i < count; ++i){
		if(midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
			if(cu_esp_strieq(caps.szPname, name)){
				*found = TRUE;
				return i;
			}
		}
	}
	for(i = 0u; i < count; ++i){
		if(midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
			if(cu_esp_stricontains(caps.szPname, name)){
				*found = TRUE;
				return i;
			}
		}
	}
	return MIDI_MAPPER;
}

static UINT cu_esp_host_midi_find_in_port(char const *name, boole *found)
{
	UINT count = midiInGetNumDevs();
	UINT i;
	MIDIINCAPSA caps;

	*found = FALSE;
	if(count == 0u)
		return (UINT)-1;
	if(name == NULL || name[0] == '\0'){
		*found = TRUE;
		return 0u;
	}

	for(i = 0u; i < count; ++i){
		if(midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
			if(cu_esp_strieq(caps.szPname, name)){
				*found = TRUE;
				return i;
			}
		}
	}
	for(i = 0u; i < count; ++i){
		if(midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
			if(cu_esp_stricontains(caps.szPname, name)){
				*found = TRUE;
				return i;
			}
		}
	}
	return (UINT)-1;
}


#endif

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)

static uint32 cu_midi_hash32(char const *s)
{
	uint32 h = 2166136261u;
	unsigned char c;
	if(s == NULL)
		return h;
	while(*s != '\0'){
		c = (unsigned char)(*s++);
		h ^= (uint32)c;
		h *= 16777619u;
	}
	return h;
}

static boole cu_midi_win_file_exists(char const *path)
{
	DWORD attr;
	if((path == NULL) || (path[0] == '\0'))
		return FALSE;
	attr = GetFileAttributesA(path);
	return (attr != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

static boole cu_midi_win_find_midi_exe(char *dst, size_t cap)
{
	char tmp[MAX_PATH];
	DWORD n;
	char const *pf;
	if((dst == NULL) || (cap == 0u))
		return FALSE;
	dst[0] = '\0';
	n = SearchPathA(NULL, "midi.exe", NULL, (DWORD)sizeof(tmp), tmp, NULL);
	if((n > 0u) && (n < sizeof(tmp)) && cu_midi_win_file_exists(tmp)){
		strncpy(dst, tmp, cap - 1u);
		dst[cap - 1u] = '\0';
		return TRUE;
	}
	pf = getenv("ProgramFiles");
	if((pf != NULL) && (pf[0] != '\0')){
		snprintf(tmp, sizeof(tmp), "%s\\Windows MIDI Services\\Tools\\midi.exe", pf);
		if(cu_midi_win_file_exists(tmp)){
			strncpy(dst, tmp, cap - 1u);
			dst[cap - 1u] = '\0';
			return TRUE;
		}
	}
	pf = getenv("ProgramFiles(x86)");
	if((pf != NULL) && (pf[0] != '\0')){
		snprintf(tmp, sizeof(tmp), "%s\\Windows MIDI Services\\Tools\\midi.exe", pf);
		if(cu_midi_win_file_exists(tmp)){
			strncpy(dst, tmp, cap - 1u);
			dst[cap - 1u] = '\0';
			return TRUE;
		}
	}
	return FALSE;
}

static sint32 cu_midi_win_run_hidden(char const *cmdline, DWORD wait_ms, DWORD *exit_code_out)
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char *cmdcpy;
	DWORD exit_code = 0xFFFFFFFFu;
	BOOL ok;
	if(cmdline == NULL)
		return -1;
	cmdcpy = _strdup(cmdline);
	if(cmdcpy == NULL)
		return -1;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	ok = CreateProcessA(NULL, cmdcpy, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	free(cmdcpy);
	if(!ok)
		return -1;
	if(WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_TIMEOUT){
		TerminateProcess(pi.hProcess, 1u);
		WaitForSingleObject(pi.hProcess, 1000u);
	}
	(void)GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if(exit_code_out != NULL)
		*exit_code_out = exit_code;
	return 0;
}

static void cu_midi_win_sanitize_unique_id(char *dst, size_t cap, char const *base)
{
	size_t di = 0u;
	char tmp[64];
	unsigned long pid = GetCurrentProcessId();
	if((dst == NULL) || (cap == 0u))
		return;
	if(base == NULL)
		base = "CUZEBOXMIDI";
	while((*base != '\0') && (di + 1u < cap) && (di < 20u)){
		unsigned char c = (unsigned char)(*base++);
		if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')){
			dst[di++] = (char)toupper(c);
		}
	}
	snprintf(tmp, sizeof(tmp), "%08lX", pid);
	for(size_t i = 0u; (tmp[i] != '\0') && (di + 1u < cap) && (di < 31u); ++i)
		dst[di++] = tmp[i];
	dst[di] = '\0';
}

static void cu_midi_win_make_assoc_id(char *dst, size_t cap, char const *seed)
{
	uint32 h1 = cu_midi_hash32(seed);
	uint32 h2 = (uint32)GetCurrentProcessId() ^ (uint32)GetTickCount();
	uint32 h3 = (uint32)time(NULL) ^ (h1 << 7) ^ (h2 >> 3);
	if((dst == NULL) || (cap == 0u))
		return;
	snprintf(dst, cap, "%08X-%04X-%04X-%04X-%08X%04X",
		(unsigned)h1,
		(unsigned)((h2 >> 16) & 0xFFFFu),
		(unsigned)(h2 & 0xFFFFu),
		(unsigned)((h3 >> 16) & 0xFFFFu),
		(unsigned)h3,
		(unsigned)(h1 & 0xFFFFu));
	dst[cap - 1u] = '\0';
}

static void cu_midi_win_make_virtual_names(cu_midi_runtime_t *rt)
{
	char const *base;
	if(rt == NULL)
		return;
	base = cu_virtual_midi_mode_base_name();
	strncpy(rt->virtual_pub_name, base, sizeof(rt->virtual_pub_name) - 1u);
	rt->virtual_pub_name[sizeof(rt->virtual_pub_name) - 1u] = '\0';
	snprintf(rt->virtual_dev_name, sizeof(rt->virtual_dev_name), "%s (CUzeBox)", base);
	rt->virtual_dev_name[sizeof(rt->virtual_dev_name) - 1u] = '\0';
}

static void cu_midi_win_build_virtual_target_names(cu_midi_runtime_t *rt, auint mode)
{
	if(rt == NULL)
		return;
	rt->port_in_name[0] = '\0';
	rt->port_out_name[0] = '\0';
	if((mode == CU_ESP_VIRTUAL_MIDI_INSTRUMENT) || (mode == CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)){
		strncpy(rt->port_in_name, rt->virtual_dev_name, sizeof(rt->port_in_name) - 1u);
		rt->port_in_name[sizeof(rt->port_in_name) - 1u] = '\0';
	}
	if((mode == CU_ESP_VIRTUAL_MIDI_CONTROLLER) || (mode == CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)){
		strncpy(rt->port_out_name, rt->virtual_dev_name, sizeof(rt->port_out_name) - 1u);
		rt->port_out_name[sizeof(rt->port_out_name) - 1u] = '\0';
	}
}

static boole cu_midi_win_create_basic_loopback(cu_midi_runtime_t *rt, auint mode)
{
	char cmd[768];
	DWORD exit_code = 0xFFFFFFFFu;
	if(rt == NULL)
		return FALSE;
	if(!cu_midi_win_find_midi_exe(rt->wms_midi_exe, sizeof(rt->wms_midi_exe)))
		return FALSE;
	cu_midi_win_make_virtual_names(rt);
	snprintf(cmd, sizeof(cmd),
		"\"%s\" loopback create --name \"%s\"",
		rt->wms_midi_exe,
		rt->virtual_pub_name);
	if(cu_midi_win_run_hidden(cmd, 15000u, &exit_code) != 0)
		return FALSE;
	if(exit_code != 0u)
		return FALSE;
	rt->port_in_name[0] = '\0';
	rt->port_out_name[0] = '\0';
	if((mode == CU_ESP_VIRTUAL_MIDI_INSTRUMENT) || (mode == CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)){
		strncpy(rt->port_in_name, rt->virtual_pub_name, sizeof(rt->port_in_name) - 1u);
		rt->port_in_name[sizeof(rt->port_in_name) - 1u] = '\0';
	}
	if((mode == CU_ESP_VIRTUAL_MIDI_CONTROLLER) || (mode == CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)){
		strncpy(rt->port_out_name, rt->virtual_pub_name, sizeof(rt->port_out_name) - 1u);
		rt->port_out_name[sizeof(rt->port_out_name) - 1u] = '\0';
	}
	print_message("ESP MIDI ENDPOINT: requested Windows MIDI 1.0 loopback [%s]\n", rt->virtual_pub_name);
	return TRUE;
}

static boole cu_midi_win_create_loopback(cu_midi_runtime_t *rt)
{
	char cmd[1024];
	DWORD exit_code = 0xFFFFFFFFu;
	char seed[512];
	if(rt == NULL)
		return FALSE;
	if(!cu_midi_win_find_midi_exe(rt->wms_midi_exe, sizeof(rt->wms_midi_exe))){
		print_message("ESP Virtual MIDI: Windows MIDI Services tool midi.exe was not found\n");
		return FALSE;
	}
	cu_midi_win_make_virtual_names(rt);
	snprintf(seed, sizeof(seed), "%s|%s|%lu|%u",
		rt->virtual_pub_name, rt->virtual_dev_name,
		(unsigned long)GetCurrentProcessId(), (unsigned)GetTickCount());
	cu_midi_win_make_assoc_id(rt->wms_association_id, sizeof(rt->wms_association_id), seed);
	cu_midi_win_sanitize_unique_id(rt->wms_unique_id, sizeof(rt->wms_unique_id), rt->virtual_pub_name);
	snprintf(cmd, sizeof(cmd),
		"\"%s\" loopback create --name-a \"%s\" --name-b \"%s\" --association-id %s --unique-identifier %s",
		rt->wms_midi_exe,
		rt->virtual_pub_name,
		rt->virtual_dev_name,
		rt->wms_association_id,
		rt->wms_unique_id);
	if(cu_midi_win_run_hidden(cmd, 15000u, &exit_code) != 0){
		print_error("ESP Virtual MIDI: failed to launch midi.exe\n");
		return FALSE;
	}
	if(exit_code != 0u){
		print_error("ESP Virtual MIDI: midi.exe loopback create failed (exit %lu)\n", (unsigned long)exit_code);
		return FALSE;
	}
	rt->wms_loopback_owned = 1u;
	print_message("ESP Virtual MIDI: created Windows loopback [%s] <-> [%s]\n",
		rt->virtual_pub_name, rt->virtual_dev_name);
	return TRUE;
}

static void cu_midi_win_remove_loopback(cu_midi_runtime_t *rt)
{
	char cmd[768];
	DWORD exit_code = 0xFFFFFFFFu;
	if((rt == NULL) || !rt->wms_loopback_owned)
		return;
	if(rt->wms_midi_exe[0] == '\0'){
		rt->wms_loopback_owned = 0u;
		return;
	}
	snprintf(cmd, sizeof(cmd),
		"\"%s\" loopback remove --association-id %s",
		rt->wms_midi_exe,
		rt->wms_association_id);
	if(cu_midi_win_run_hidden(cmd, 10000u, &exit_code) == 0){
		if(exit_code != 0u){
			print_message("ESP Virtual MIDI: midi.exe loopback remove exited %lu\n", (unsigned long)exit_code);
		}
	}
	rt->wms_loopback_owned = 0u;
}

static UINT cu_midi_win_find_out_exact(char const *name, boole *found)
{
	UINT count = midiOutGetNumDevs();
	UINT i;
	MIDIOUTCAPSA caps;
	*found = FALSE;
	if((name == NULL) || (name[0] == '\0'))
		return (UINT)-1;
	for(i = 0u; i < count; ++i){
		if(midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
			if(cu_esp_strieq(caps.szPname, name)){
				*found = TRUE;
				return i;
			}
		}
	}
	return (UINT)-1;
}

static UINT cu_midi_win_find_in_exact(char const *name, boole *found)
{
	UINT count = midiInGetNumDevs();
	UINT i;
	MIDIINCAPSA caps;
	*found = FALSE;
	if((name == NULL) || (name[0] == '\0'))
		return (UINT)-1;
	for(i = 0u; i < count; ++i){
		if(midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
			if(cu_esp_strieq(caps.szPname, name)){
				*found = TRUE;
				return i;
			}
		}
	}
	return (UINT)-1;
}

static sint32 cu_midi_runtime_open_winmm(cu_midi_runtime_t *rt,
	char const *out_name, char const *in_name,
	boole exact_names, char const *tag)
{
	MMRESULT mmr;
	UINT out_dev = (UINT)-1;
	UINT in_dev = (UINT)-1;
	boole out_found = FALSE;
	boole in_found = FALSE;
	auint i;
	if(rt == NULL)
		return ESP_SERIAL_OPEN_ERROR;
	if(out_name != NULL)
		strncpy(rt->port_out_name, out_name, sizeof(rt->port_out_name) - 1u);
	else
		rt->port_out_name[0] = '\0';
	rt->port_out_name[sizeof(rt->port_out_name) - 1u] = '\0';
	if(in_name != NULL)
		strncpy(rt->port_in_name, in_name, sizeof(rt->port_in_name) - 1u);
	else
		rt->port_in_name[0] = '\0';
	rt->port_in_name[sizeof(rt->port_in_name) - 1u] = '\0';

	if(rt->port_out_name[0] != '\0'){
		out_dev = exact_names ? cu_midi_win_find_out_exact(rt->port_out_name, &out_found)
			: cu_esp_host_midi_find_out_port(rt->port_out_name, &out_found);
		if(!out_found){
			print_message("%s: Windows MIDI output endpoint not found [%s]\n", tag, rt->port_out_name);
			return ESP_SERIAL_OPEN_ERROR;
		}
		mmr = midiOutOpen(&rt->out_handle, out_dev, 0u, 0u, CALLBACK_NULL);
		if(mmr != MMSYSERR_NOERROR){
			print_error("%s: failed to open Windows MIDI out [%s]\n", tag, rt->port_out_name);
			return ESP_SERIAL_OPEN_ERROR;
		}
	}
	if(rt->port_in_name[0] != '\0'){
		in_dev = exact_names ? cu_midi_win_find_in_exact(rt->port_in_name, &in_found)
			: cu_esp_host_midi_find_in_port(rt->port_in_name, &in_found);
		if(!in_found){
			if(rt->out_handle != NULL){
				(void)midiOutClose(rt->out_handle);
				rt->out_handle = NULL;
			}
			print_message("%s: Windows MIDI input endpoint not found [%s]\n", tag, rt->port_in_name);
			return ESP_SERIAL_OPEN_ERROR;
		}
		mmr = midiInOpen(&rt->in_handle, in_dev, (DWORD_PTR)cu_esp_host_midi_in_callback,
			(DWORD_PTR)rt, CALLBACK_FUNCTION);
		if(mmr != MMSYSERR_NOERROR){
			if(rt->out_handle != NULL){
				(void)midiOutClose(rt->out_handle);
				rt->out_handle = NULL;
			}
			print_error("%s: failed to open Windows MIDI in [%s]\n", tag, rt->port_in_name);
			return ESP_SERIAL_OPEN_ERROR;
		}
		for(i = 0u; i < 2u; ++i){
			memset(&rt->in_sysex_hdr[i], 0, sizeof(MIDIHDR));
			rt->in_sysex_hdr[i].lpData = (LPSTR)rt->in_sysex_buf[i];
			rt->in_sysex_hdr[i].dwBufferLength = (DWORD)sizeof(rt->in_sysex_buf[i]);
			(void)midiInPrepareHeader(rt->in_handle, &rt->in_sysex_hdr[i], sizeof(MIDIHDR));
			(void)midiInAddBuffer(rt->in_handle, &rt->in_sysex_hdr[i], sizeof(MIDIHDR));
		}
		(void)midiInStart(rt->in_handle);
	}
	return 0;
}

#endif

#if defined(__APPLE__)
static void cu_midi_read_callback(const MIDIPacketList *pktlist,
	void *readProcRefCon, void *srcConnRefCon)
{
	cu_midi_runtime_t *rt = (cu_midi_runtime_t *)readProcRefCon;
	MIDIPacket const *pkt;
	ItemCount i;
	(void)srcConnRefCon;

	if((rt == NULL) || (pktlist == NULL))
		return;

	pkt = &pktlist->packet[0];
	for(i = 0; i < pktlist->numPackets; ++i){
		if(pkt->length != 0)
			cu_midi_rx_push(rt, (uint8 const *)pkt->data, (auint)pkt->length);
		pkt = MIDIPacketNext(pkt);
	}
}
#endif

static boole cu_midi_send_platform(cu_midi_runtime_t *rt, uint8 const *data, auint len)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	MMRESULT mmr;
	if((rt == NULL) || (rt->out_handle == NULL) || (data == NULL) || (len == 0u))
		return FALSE;
	if((data[0] == 0xF0u) || (len > 3u)){
		DWORD wait_ms = 0u;
		if(len > CU_ESP_HOST_MIDI_TX_CAP)
			len = CU_ESP_HOST_MIDI_TX_CAP;
		memcpy(rt->out_sysex_buf, data, len);
		memset(&rt->out_sysex_hdr, 0, sizeof(rt->out_sysex_hdr));
		rt->out_sysex_hdr.lpData = (LPSTR)rt->out_sysex_buf;
		rt->out_sysex_hdr.dwBufferLength = (DWORD)len;
		mmr = midiOutPrepareHeader(rt->out_handle, &rt->out_sysex_hdr, sizeof(MIDIHDR));
		if(mmr != MMSYSERR_NOERROR)
			return FALSE;
		mmr = midiOutLongMsg(rt->out_handle, &rt->out_sysex_hdr, sizeof(MIDIHDR));
		if(mmr != MMSYSERR_NOERROR){
			(void)midiOutUnprepareHeader(rt->out_handle, &rt->out_sysex_hdr, sizeof(MIDIHDR));
			return FALSE;
		}
		while(((rt->out_sysex_hdr.dwFlags & MHDR_DONE) == 0u) && (wait_ms < 2000u)){
			Sleep(1u);
			wait_ms++;
		}
		(void)midiOutUnprepareHeader(rt->out_handle, &rt->out_sysex_hdr, sizeof(MIDIHDR));
		return TRUE;
	}else{
		DWORD msg = (DWORD)data[0];
		if(len > 1u)
			msg |= ((DWORD)data[1] << 8);
		if(len > 2u)
			msg |= ((DWORD)data[2] << 16);
		return (midiOutShortMsg(rt->out_handle, msg) == MMSYSERR_NOERROR) ? TRUE : FALSE;
	}
#elif defined(__linux__)
	auint used = 0u;
	snd_seq_event_t ev;
	long enc;
	if((rt == NULL) || (rt->seq == NULL) || (rt->encoder == NULL) || (rt->port_out < 0) || (data == NULL) || (len == 0u))
		return FALSE;
	snd_midi_event_reset_encode(rt->encoder);
	while(used < len){
		snd_seq_ev_clear(&ev);
		enc = snd_midi_event_encode(rt->encoder,
			(unsigned char const *)(data + used),
			(long)(len - used), &ev);
		if(enc <= 0)
			return FALSE;
		used += (auint)enc;
		snd_seq_ev_set_source(&ev, rt->port_out);
		snd_seq_ev_set_subs(&ev);
		snd_seq_ev_set_direct(&ev);
		if(snd_seq_event_output_direct(rt->seq, &ev) < 0)
			return FALSE;
	}
	return TRUE;
#elif defined(__APPLE__)
	uint8 pktbuf[sizeof(MIDIPacketList) + CU_ESP_HOST_MIDI_TX_CAP];
	MIDIPacketList *pktlist = (MIDIPacketList *)pktbuf;
	MIDIPacket *pkt;
	if((rt == NULL) || (rt->source == 0) || (data == NULL) || (len == 0u))
		return FALSE;
	if(len > CU_ESP_HOST_MIDI_TX_CAP)
		len = CU_ESP_HOST_MIDI_TX_CAP;
	pkt = MIDIPacketListInit(pktlist);
	pkt = MIDIPacketListAdd(pktlist, sizeof(pktbuf), pkt, 0, (UInt16)len, (Byte const *)data);
	if(pkt == NULL)
		return FALSE;
	return (MIDIReceived(rt->source, pktlist) == noErr) ? TRUE : FALSE;
#else
	(void)rt;
	(void)data;
	(void)len;
	return FALSE;
#endif
}

static void cu_midi_poll(cu_midi_runtime_t *rt)
{
#if defined(__linux__)
	if((rt == NULL) || (rt->seq == NULL) || (rt->decoder == NULL) || (rt->port_in < 0))
		return;
	while(snd_seq_event_input_pending(rt->seq, 0) > 0){
		snd_seq_event_t *ev = NULL;
		unsigned char tmp[1024];
		long dec;
		if(snd_seq_event_input(rt->seq, &ev) < 0)
			break;
		if(ev == NULL)
			break;
		snd_midi_event_reset_decode(rt->decoder);
		dec = snd_midi_event_decode(rt->decoder, tmp, sizeof(tmp), ev);
		if(dec > 0)
			cu_midi_rx_push(rt, tmp, (auint)dec);
		snd_seq_free_event(ev);
	}
#else
	(void)rt;
#endif
}

static void cu_midi_tx_emit(cu_midi_runtime_t *rt, uint8 const *data, auint len)
{
	if((rt == NULL) || (data == NULL) || (len == 0u))
		return;
	if(!cu_midi_send_platform(rt, data, len))
		print_error("ESP MIDI: send failed\n");
}

static void cu_midi_feed_tx(cu_midi_runtime_t *rt, uint8 c)
{
	uint8 status;

	if(rt == NULL)
		return;

	if(c >= 0xF8u){
		cu_midi_tx_emit(rt, &c, 1u);
		return;
	}

	if(rt->tx_in_sysex){
		if(rt->tx_len < CU_ESP_HOST_MIDI_TX_CAP)
			rt->tx_msg[rt->tx_len++] = c;
		if(c == 0xF7u){
			cu_midi_tx_emit(rt, rt->tx_msg, rt->tx_len);
			rt->tx_in_sysex = 0u;
			rt->tx_len = 0u;
			rt->tx_status = 0u;
			rt->tx_expected = 0u;
			rt->tx_data_count = 0u;
		}
		return;
	}

	if((c & 0x80u) != 0u){
		if(c == 0xF0u){
			rt->tx_in_sysex = 1u;
			rt->tx_len = 0u;
			rt->tx_msg[rt->tx_len++] = c;
			rt->tx_status = 0u;
			rt->tx_expected = 0u;
			rt->tx_data_count = 0u;
			rt->tx_running_status = 0u;
			return;
		}

		status = c;
		rt->tx_status = status;
		rt->tx_expected = cu_midi_data_bytes(status);
		rt->tx_data_count = 0u;
		rt->tx_len = 1u;
		rt->tx_msg[0] = status;
		if(status < 0xF0u)
			rt->tx_running_status = status;
		else
			rt->tx_running_status = 0u;
		if(rt->tx_expected == 0u){
			cu_midi_tx_emit(rt, rt->tx_msg, rt->tx_len);
			rt->tx_status = 0u;
			rt->tx_len = 0u;
		}
		return;
	}

	if(rt->tx_status == 0u){
		if(rt->tx_running_status == 0u)
			return;
		rt->tx_status = rt->tx_running_status;
		rt->tx_expected = cu_midi_data_bytes(rt->tx_running_status);
		rt->tx_data_count = 0u;
		rt->tx_len = 1u;
		rt->tx_msg[0] = rt->tx_running_status;
	}

	if(rt->tx_len < CU_ESP_HOST_MIDI_TX_CAP)
		rt->tx_msg[rt->tx_len++] = c;
	rt->tx_data_count++;
	if(rt->tx_data_count >= rt->tx_expected){
		cu_midi_tx_emit(rt, rt->tx_msg, rt->tx_len);
		rt->tx_status = 0u;
		rt->tx_expected = 0u;
		rt->tx_data_count = 0u;
		rt->tx_len = 0u;
	}
}

void cu_esp_host_midi_refresh_ports(void)
{
	cu_esp_host_midi_enum_clear();
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		UINT i;
		UINT out_count = midiOutGetNumDevs();
		UINT in_count = midiInGetNumDevs();
		MIDIOUTCAPSA out_caps;
		MIDIINCAPSA in_caps;
		cu_esp_host_midi_enum_add("", TRUE, TRUE);
		for(i = 0u; i < out_count; ++i){
			if(midiOutGetDevCapsA(i, &out_caps, sizeof(out_caps)) == MMSYSERR_NOERROR)
				cu_esp_host_midi_enum_add(out_caps.szPname, FALSE, TRUE);
		}
		for(i = 0u; i < in_count; ++i){
			if(midiInGetDevCapsA(i, &in_caps, sizeof(in_caps)) == MMSYSERR_NOERROR)
				cu_esp_host_midi_enum_add(in_caps.szPname, TRUE, FALSE);
		}
	}
#endif
}

auint cu_esp_host_midi_get_port_count(void)
{
	return cu_host_midi_enum_count;
}

char const* cu_esp_host_midi_get_port_name(auint idx)
{
	if(idx >= cu_host_midi_enum_count)
		return "";
	return cu_host_midi_enum[idx].raw;
}

char const* cu_esp_host_midi_get_port_label(auint idx)
{
	if(idx >= cu_host_midi_enum_count)
		return "";
	return cu_host_midi_enum[idx].label;
}

static void cu_midi_runtime_end_host(cu_midi_runtime_t *rt)
{
	if(rt == NULL)
		return;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(rt->in_handle != NULL){
		auint i;
		(void)midiInStop(rt->in_handle);
		(void)midiInReset(rt->in_handle);
		for(i = 0u; i < 2u; ++i){
			if((rt->in_sysex_hdr[i].dwFlags & MHDR_PREPARED) != 0u)
				(void)midiInUnprepareHeader(rt->in_handle, &rt->in_sysex_hdr[i], sizeof(MIDIHDR));
		}
		(void)midiInClose(rt->in_handle);
		rt->in_handle = NULL;
	}
	if(rt->out_handle != NULL){
		(void)midiOutReset(rt->out_handle);
		(void)midiOutClose(rt->out_handle);
		rt->out_handle = NULL;
	}
#elif defined(__linux__)
	if(rt->encoder != NULL){
		snd_midi_event_free(rt->encoder);
		rt->encoder = NULL;
	}
	if(rt->decoder != NULL){
		snd_midi_event_free(rt->decoder);
		rt->decoder = NULL;
	}
	if((rt->seq != NULL) && (rt->port_in >= 0) && (rt->port_in == rt->port_out)){
		snd_seq_delete_simple_port(rt->seq, rt->port_in);
		rt->port_in = -1;
		rt->port_out = -1;
	}else{
		if((rt->seq != NULL) && (rt->port_in >= 0)){
			snd_seq_delete_simple_port(rt->seq, rt->port_in);
			rt->port_in = -1;
		}
		if((rt->seq != NULL) && (rt->port_out >= 0)){
			snd_seq_delete_simple_port(rt->seq, rt->port_out);
			rt->port_out = -1;
		}
	}
	if(rt->seq != NULL){
		snd_seq_close(rt->seq);
		rt->seq = NULL;
	}
#elif defined(__APPLE__)
	if(rt->source != 0){
		MIDIEndpointDispose(rt->source);
		rt->source = 0;
	}
	if(rt->destination != 0){
		MIDIEndpointDispose(rt->destination);
		rt->destination = 0;
	}
	if(rt->client != 0){
		MIDIClientDispose(rt->client);
		rt->client = 0;
	}
#endif
	cu_midi_rx_clear(rt);
	cu_midi_tx_reset(rt);
}

static void cu_midi_runtime_end_virtual(cu_midi_runtime_t *rt)
{
	cu_midi_runtime_end_host(rt);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	cu_midi_win_remove_loopback(rt);
#endif
}

sint32 cu_esp_host_midi_start(void)
{
	cu_midi_runtime_t *rt = &cu_host_midi;

	if(!cu_esp_host_midi_backend_supported()){
		esp_state.host_midi_enabled = 0u;
		return ESP_SERIAL_OPEN_ERROR;
	}

	if(esp_state.host_midi_enabled)
		cu_esp_host_midi_end();

	rt->route_kind = CU_ESP_MIDI_ROUTE_HOST;
	cu_midi_lock_init(rt);
	cu_midi_rx_clear(rt);
	cu_midi_tx_reset(rt);

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		if(cu_midi_runtime_open_winmm(rt,
			((char const *)esp_state.host_midi_port_name)[0] ? (char const *)esp_state.host_midi_port_name : NULL,
			((char const *)esp_state.host_midi_port_name)[0] ? (char const *)esp_state.host_midi_port_name : NULL,
			FALSE, "ESP Host MIDI") != 0){
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
	}
#elif defined(__linux__)
	{
		char const *base_name = ((char const *)esp_state.host_midi_port_name)[0] ?
			(char const *)esp_state.host_midi_port_name : "CUzeBox MIDI";
		if(snd_seq_open(&rt->seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0){
			print_error("ESP Host MIDI ALSA open failed\n");
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		snd_seq_nonblock(rt->seq, 1);
		snd_seq_set_client_name(rt->seq, base_name);
		rt->port_out = snd_seq_create_simple_port(rt->seq, base_name,
			SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE |
			SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
		rt->port_in = rt->port_out;
		if(rt->port_out < 0){
			snd_seq_close(rt->seq);
			rt->seq = NULL;
			print_error("ESP Host MIDI ALSA port create failed\n");
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		if((snd_midi_event_new(1024u, &rt->encoder) < 0) ||
		   (snd_midi_event_new(1024u, &rt->decoder) < 0)){
			if(rt->encoder != NULL)
				snd_midi_event_free(rt->encoder);
			if(rt->decoder != NULL)
				snd_midi_event_free(rt->decoder);
			snd_seq_delete_simple_port(rt->seq, rt->port_out);
			snd_seq_close(rt->seq);
			rt->seq = NULL;
			rt->encoder = NULL;
			rt->decoder = NULL;
			rt->port_in = -1;
			rt->port_out = -1;
			print_error("ESP Host MIDI ALSA codec create failed\n");
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		snd_midi_event_no_status(rt->encoder, 0);
		snd_midi_event_no_status(rt->decoder, 0);
	}
#elif defined(__APPLE__)
	{
		char base_name[256];
		char out_name[288];
		char in_name[288];
		CFStringRef cf_base;
		CFStringRef cf_out;
		CFStringRef cf_in;
		char const *wanted = ((char const *)esp_state.host_midi_port_name)[0] ?
			(char const *)esp_state.host_midi_port_name : "CUzeBox MIDI";
		strncpy(base_name, wanted, sizeof(base_name) - 1u);
		base_name[sizeof(base_name) - 1u] = '\0';
		snprintf(out_name, sizeof(out_name), "%s Out", base_name);
		snprintf(in_name, sizeof(in_name), "%s In", base_name);
		cf_base = CFStringCreateWithCString(NULL, base_name, kCFStringEncodingUTF8);
		cf_out = CFStringCreateWithCString(NULL, out_name, kCFStringEncodingUTF8);
		cf_in = CFStringCreateWithCString(NULL, in_name, kCFStringEncodingUTF8);
		if((cf_base == NULL) || (cf_out == NULL) || (cf_in == NULL)){
			if(cf_base != NULL) CFRelease(cf_base);
			if(cf_out != NULL) CFRelease(cf_out);
			if(cf_in != NULL) CFRelease(cf_in);
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		if(MIDIClientCreate(cf_base, NULL, NULL, &rt->client) != noErr){
			CFRelease(cf_base);
			CFRelease(cf_out);
			CFRelease(cf_in);
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		if(MIDISourceCreate(rt->client, cf_out, &rt->source) != noErr ||
		   MIDIDestinationCreate(rt->client, cf_in, cu_midi_read_callback, rt, &rt->destination) != noErr){
			if(rt->source != 0)
				MIDIEndpointDispose(rt->source);
			if(rt->destination != 0)
				MIDIEndpointDispose(rt->destination);
			MIDIClientDispose(rt->client);
			rt->client = 0;
			rt->source = 0;
			rt->destination = 0;
			CFRelease(cf_base);
			CFRelease(cf_out);
			CFRelease(cf_in);
			esp_state.host_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		CFRelease(cf_base);
		CFRelease(cf_out);
		CFRelease(cf_in);
	}
#else
	print_message("ESP Host MIDI backend missing on this target\n");
	esp_state.host_midi_enabled = 0u;
	return ESP_SERIAL_OPEN_ERROR;
#endif

	esp_state.host_midi_enabled = 1u;
	cu_serial_midi_active = CU_ESP_SERIAL_HOST_MIDI;
	{
		char const *active_name = ((char const *)esp_state.host_midi_port_name)[0] ?
			(char const *)esp_state.host_midi_port_name :
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			"default Windows MIDI endpoint";
#else
			"CUzeBox MIDI";
#endif
		print_message("ESP Host MIDI initialized [%s]\n", active_name);
	}
	return 0;
}

void cu_esp_host_midi_end(void)
{
	cu_midi_runtime_end_host(&cu_host_midi);
	esp_state.host_midi_enabled = 0u;
	if(cu_serial_midi_active == CU_ESP_SERIAL_HOST_MIDI)
		cu_serial_midi_active = CU_ESP_SERIAL_DISCONNECTED;
}

void cu_esp_host_midi_write(uint8 c)
{
	if(!esp_state.host_midi_enabled)
		return;
	cu_midi_feed_tx(&cu_host_midi, c);
}

uint8 cu_esp_host_midi_read(void)
{
	cu_midi_poll(&cu_host_midi);
	return cu_midi_rx_pop(&cu_host_midi);
}

auint cu_esp_host_midi_rx_bytes_ready(void)
{
	cu_midi_poll(&cu_host_midi);
	return cu_midi_rx_ready_local(&cu_host_midi);
}

boole cu_esp_host_midi_supported(void)
{
	return cu_esp_host_midi_backend_supported();
}

sint32 cu_esp_virtual_midi_start(void)
{
	cu_midi_runtime_t *rt = &cu_virtual_midi;
	auint mode = esp_state.virtual_midi_mode;

	if(!cu_esp_virtual_midi_backend_supported()){
		esp_state.virtual_midi_enabled = 0u;
		print_message("ESP MIDI ENDPOINT: unsupported on this build\n");
		return ESP_SERIAL_OPEN_ERROR;
	}

	if(esp_state.virtual_midi_enabled)
		cu_esp_virtual_midi_end();

	if((mode != CU_ESP_VIRTUAL_MIDI_INSTRUMENT) &&
	   (mode != CU_ESP_VIRTUAL_MIDI_CONTROLLER) &&
	   (mode != CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL))
		mode = CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL;

	rt->route_kind = CU_ESP_MIDI_ROUTE_VIRTUAL;
	cu_midi_lock_init(rt);
	cu_midi_rx_clear(rt);
	cu_midi_tx_reset(rt);

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	{
		DWORD start_tick = GetTickCount();
		boole need_in = ((mode == CU_ESP_VIRTUAL_MIDI_INSTRUMENT) || (mode == CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)) ? TRUE : FALSE;
		boole need_out = ((mode == CU_ESP_VIRTUAL_MIDI_CONTROLLER) || (mode == CU_ESP_VIRTUAL_MIDI_BIDIRECTIONAL)) ? TRUE : FALSE;
		boole created_any = FALSE;
		cu_midi_win_make_virtual_names(rt);
		cu_midi_win_build_virtual_target_names(rt, mode);
		if(cu_midi_runtime_open_winmm(rt,
			need_out ? rt->port_out_name : NULL,
			need_in ? rt->port_in_name : NULL,
			TRUE, "ESP MIDI ENDPOINT") != 0){
			if(cu_midi_win_create_basic_loopback(rt, mode)){
				created_any = TRUE;
				while((GetTickCount() - start_tick) < 3000u){
					Sleep(100u);
					if(cu_midi_runtime_open_winmm(rt,
						need_out ? rt->port_out_name : NULL,
						need_in ? rt->port_in_name : NULL,
						TRUE, "ESP MIDI ENDPOINT") == 0)
						break;
				}
			}
			if(((need_in && (rt->in_handle == NULL)) || (need_out && (rt->out_handle == NULL)))){
				cu_midi_runtime_end_host(rt);
				cu_midi_win_build_virtual_target_names(rt, mode);
				if(cu_midi_win_create_loopback(rt)){
					created_any = TRUE;
					while((GetTickCount() - start_tick) < 3000u){
						Sleep(100u);
						if(cu_midi_runtime_open_winmm(rt,
							need_out ? rt->port_out_name : NULL,
							need_in ? rt->port_in_name : NULL,
							TRUE, "ESP MIDI ENDPOINT") == 0)
							break;
					}
				}
			}
			if(((need_in && (rt->in_handle == NULL)) || (need_out && (rt->out_handle == NULL)))){
				if(created_any){
					print_message("ESP MIDI ENDPOINT: endpoint was requested but WinMM could not open it.\n");
					print_message("ESP MIDI ENDPOINT: for Anvil Studio, install Windows MIDI Services Basic MIDI 1.0 Loopback, or use HOST MIDI with loopMIDI.\n");
				}else{
					print_message("ESP MIDI ENDPOINT: Windows MIDI Services tools were not available.\n");
					print_message("ESP MIDI ENDPOINT: use HOST MIDI with an existing WinMM endpoint, or install Windows MIDI Services / loopMIDI.\n");
				}
				rt->wms_loopback_owned = 0u;
				cu_midi_runtime_end_host(rt);
				esp_state.virtual_midi_enabled = 0u;
				return ESP_SERIAL_OPEN_ERROR;
			}
		}
	}
#elif defined(__linux__)
	{
		char const *base_name = cu_virtual_midi_mode_base_name();
		char in_name[288];
		char out_name[288];
		unsigned int caps_in = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
		unsigned int caps_out = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
		if(snd_seq_open(&rt->seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0){
			print_error("ESP Virtual MIDI ALSA open failed\n");
			esp_state.virtual_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		snd_seq_nonblock(rt->seq, 1);
		snd_seq_set_client_name(rt->seq, base_name);
		if((snd_midi_event_new(1024u, &rt->encoder) < 0) ||
		   (snd_midi_event_new(1024u, &rt->decoder) < 0)){
			if(rt->encoder != NULL)
				snd_midi_event_free(rt->encoder);
			if(rt->decoder != NULL)
				snd_midi_event_free(rt->decoder);
			snd_seq_close(rt->seq);
			rt->seq = NULL;
			rt->encoder = NULL;
			rt->decoder = NULL;
			print_error("ESP Virtual MIDI ALSA codec create failed\n");
			esp_state.virtual_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		snd_midi_event_no_status(rt->encoder, 0);
		snd_midi_event_no_status(rt->decoder, 0);
		if(mode == CU_ESP_VIRTUAL_MIDI_INSTRUMENT){
			rt->port_in = snd_seq_create_simple_port(rt->seq, base_name, caps_in,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
			rt->port_out = -1;
		}else if(mode == CU_ESP_VIRTUAL_MIDI_CONTROLLER){
			rt->port_out = snd_seq_create_simple_port(rt->seq, base_name, caps_out,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
			rt->port_in = -1;
		}else{
			snprintf(in_name, sizeof(in_name), "%s In", base_name);
			snprintf(out_name, sizeof(out_name), "%s Out", base_name);
			rt->port_in = snd_seq_create_simple_port(rt->seq, in_name, caps_in,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
			rt->port_out = snd_seq_create_simple_port(rt->seq, out_name, caps_out,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
		}
		if(((mode != CU_ESP_VIRTUAL_MIDI_CONTROLLER) && (rt->port_in < 0)) ||
		   ((mode != CU_ESP_VIRTUAL_MIDI_INSTRUMENT) && (rt->port_out < 0))){
			cu_midi_runtime_end_virtual(rt);
			print_error("ESP Virtual MIDI ALSA port create failed\n");
			esp_state.virtual_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
	}
#elif defined(__APPLE__)
	{
		char const *base_name = cu_virtual_midi_mode_base_name();
		char in_name[288];
		char out_name[288];
		CFStringRef cf_base;
		CFStringRef cf_in = NULL;
		CFStringRef cf_out = NULL;
		cf_base = CFStringCreateWithCString(NULL, base_name, kCFStringEncodingUTF8);
		if(cf_base == NULL){
			esp_state.virtual_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		if(MIDIClientCreate(cf_base, NULL, NULL, &rt->client) != noErr){
			CFRelease(cf_base);
			esp_state.virtual_midi_enabled = 0u;
			return ESP_SERIAL_OPEN_ERROR;
		}
		if(mode == CU_ESP_VIRTUAL_MIDI_INSTRUMENT){
			cf_in = CFStringCreateWithCString(NULL, base_name, kCFStringEncodingUTF8);
			if((cf_in == NULL) || (MIDIDestinationCreate(rt->client, cf_in, cu_midi_read_callback, rt, &rt->destination) != noErr)){
				if(cf_in != NULL) CFRelease(cf_in);
				CFRelease(cf_base);
				cu_midi_runtime_end_virtual(rt);
				esp_state.virtual_midi_enabled = 0u;
				return ESP_SERIAL_OPEN_ERROR;
			}
		}else if(mode == CU_ESP_VIRTUAL_MIDI_CONTROLLER){
			cf_out = CFStringCreateWithCString(NULL, base_name, kCFStringEncodingUTF8);
			if((cf_out == NULL) || (MIDISourceCreate(rt->client, cf_out, &rt->source) != noErr)){
				if(cf_out != NULL) CFRelease(cf_out);
				CFRelease(cf_base);
				cu_midi_runtime_end_virtual(rt);
				esp_state.virtual_midi_enabled = 0u;
				return ESP_SERIAL_OPEN_ERROR;
			}
		}else{
			snprintf(in_name, sizeof(in_name), "%s In", base_name);
			snprintf(out_name, sizeof(out_name), "%s Out", base_name);
			cf_in = CFStringCreateWithCString(NULL, in_name, kCFStringEncodingUTF8);
			cf_out = CFStringCreateWithCString(NULL, out_name, kCFStringEncodingUTF8);
			if((cf_in == NULL) || (cf_out == NULL) ||
			   (MIDISourceCreate(rt->client, cf_out, &rt->source) != noErr) ||
			   (MIDIDestinationCreate(rt->client, cf_in, cu_midi_read_callback, rt, &rt->destination) != noErr)){
				if(cf_in != NULL) CFRelease(cf_in);
				if(cf_out != NULL) CFRelease(cf_out);
				CFRelease(cf_base);
				cu_midi_runtime_end_virtual(rt);
				esp_state.virtual_midi_enabled = 0u;
				return ESP_SERIAL_OPEN_ERROR;
			}
		}
		if(cf_in != NULL) CFRelease(cf_in);
		if(cf_out != NULL) CFRelease(cf_out);
		CFRelease(cf_base);
	}
#endif

	esp_state.virtual_midi_enabled = 1u;
	cu_serial_midi_active = CU_ESP_SERIAL_VIRTUAL_MIDI;
	print_message("ESP MIDI ENDPOINT initialized [%s, %s]\n",
		cu_virtual_midi_mode_base_name(), cu_virtual_midi_mode_name(mode));
	return 0;
}

void cu_esp_virtual_midi_end(void)
{
	cu_midi_runtime_end_virtual(&cu_virtual_midi);
	esp_state.virtual_midi_enabled = 0u;
	if(cu_serial_midi_active == CU_ESP_SERIAL_VIRTUAL_MIDI)
		cu_serial_midi_active = CU_ESP_SERIAL_DISCONNECTED;
}

void cu_esp_virtual_midi_write(uint8 c)
{
	if(!esp_state.virtual_midi_enabled)
		return;
	cu_midi_feed_tx(&cu_virtual_midi, c);
}

uint8 cu_esp_virtual_midi_read(void)
{
	cu_midi_poll(&cu_virtual_midi);
	return cu_midi_rx_pop(&cu_virtual_midi);
}

auint cu_esp_virtual_midi_rx_bytes_ready(void)
{
	cu_midi_poll(&cu_virtual_midi);
	return cu_midi_rx_ready_local(&cu_virtual_midi);
}

boole cu_esp_virtual_midi_supported(void)
{
	return cu_esp_virtual_midi_backend_supported();
}

sint32 cu_esp_serial_midi_start(auint route)
{
	if(route == CU_ESP_SERIAL_HOST_MIDI)
		return cu_esp_host_midi_start();
	if(route == CU_ESP_SERIAL_VIRTUAL_MIDI)
		return cu_esp_virtual_midi_start();
	return ESP_SERIAL_OPEN_ERROR;
}

void cu_esp_serial_midi_end(void)
{
	cu_esp_host_midi_end();
	cu_esp_virtual_midi_end();
	cu_serial_midi_active = CU_ESP_SERIAL_DISCONNECTED;
}

void cu_esp_serial_midi_write(uint8 c)
{
	if((cu_serial_midi_active == CU_ESP_SERIAL_VIRTUAL_MIDI) || (esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI))
		cu_esp_virtual_midi_write(c);
	else
		cu_esp_host_midi_write(c);
}

uint8 cu_esp_serial_midi_read(void)
{
	if((cu_serial_midi_active == CU_ESP_SERIAL_VIRTUAL_MIDI) || (esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI))
		return cu_esp_virtual_midi_read();
	return cu_esp_host_midi_read();
}

auint cu_esp_serial_midi_rx_bytes_ready(void)
{
	if((cu_serial_midi_active == CU_ESP_SERIAL_VIRTUAL_MIDI) || (esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI))
		return cu_esp_virtual_midi_rx_bytes_ready();
	return cu_esp_host_midi_rx_bytes_ready();
}

boole cu_esp_serial_midi_supported(auint route)
{
	if(route == CU_ESP_SERIAL_VIRTUAL_MIDI)
		return cu_esp_virtual_midi_supported();
	if(route == CU_ESP_SERIAL_HOST_MIDI)
		return cu_esp_host_midi_supported();
	return FALSE;
}

#endif /* ENABLE_ESP */
