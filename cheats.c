#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <direct.h>
#define CHEATS_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define CHEATS_MKDIR(path) mkdir(path, 0777)
#endif
#include "cheats.h"
#include "cu_avr.h"

#define CHEATS_FILE_VERSION 2U

static cheat_entry_t cheats_list[CHEATS_MAX];
static char          cheats_path[CHEAT_PATH_MAX];
static boole         cheats_dirty = FALSE;
static boole         cheats_master_enabled = TRUE;
static boole         cheats_autoloadsave = TRUE;

static void cheats_trim(char* s)
{
	char* end;
	while ((*s != 0) && isspace((unsigned char)*s)){
		memmove(s, s + 1, strlen(s));
	}
	end = s + strlen(s);
	while ((end > s) && isspace((unsigned char)end[-1])){
		end--;
	}
	*end = 0;
}

static void cheats_copy_str(char* dst, auint cap, char const* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

static boole cheats_mkdirs_for_file(char const* path)
{
	char tmp[CHEAT_PATH_MAX];
	char* slash1;
	char* slash2;
	char* slash;
	auint i;
	int rc;
	if ((path == NULL) || (path[0] == 0)){ return TRUE; }
	cheats_copy_str(tmp, sizeof(tmp), path);
	slash1 = strrchr(tmp, '/');
	slash2 = strrchr(tmp, '\\');
	slash = slash1;
	if ((slash2 != NULL) && ((slash == NULL) || (slash2 > slash))){ slash = slash2; }
	if (slash == NULL){ return TRUE; }
	*slash = 0;
	if (tmp[0] == 0){ return TRUE; }
	for (i = 1U; tmp[i] != 0; i++){
		if ((tmp[i] == '/') || (tmp[i] == '\\')){
			char hold = tmp[i];
			tmp[i] = 0;
			rc = CHEATS_MKDIR(tmp);
			if ((rc != 0) && (errno != EEXIST)){
				tmp[i] = hold;
				return FALSE;
			}
			tmp[i] = hold;
		}
	}
	rc = CHEATS_MKDIR(tmp);
	if ((rc != 0) && (errno != EEXIST)){ return FALSE; }
	return TRUE;
}

static auint cheats_clamp_width(auint width)
{
	if (width >= 4U){ return 4U; }
	if (width >= 2U){ return 2U; }
	return 1U;
}

static uint32 cheats_width_mask(auint width)
{
	switch (cheats_clamp_width(width)){
		case 4U: return 0xFFFFFFFFU;
		case 2U: return 0x0000FFFFU;
		default: return 0x000000FFU;
	}
}

static uint32 cheats_read_sram_value(cu_state_cpu_t const* ecpu, auint addr, auint width)
{
	uint32 value = 0U;
	auint  i;
	width = cheats_clamp_width(width);
	for (i = 0U; i < width; i++){
		value |= ((uint32)(ecpu->sram[(addr + i) & 0x0FFFU])) << (i * 8U);
	}
	return value & cheats_width_mask(width);
}

static void cheats_write_sram_value(cu_state_cpu_t* ecpu, auint addr, auint width, uint32 value)
{
	auint i;
	width = cheats_clamp_width(width);
	for (i = 0U; i < width; i++){
		ecpu->sram[(addr + i) & 0x0FFFU] = (uint8)((value >> (i * 8U)) & 0xFFU);
	}
}

void cheats_reset(void)
{
	memset(cheats_list, 0, sizeof(cheats_list));
	cheats_path[0] = 0;
	cheats_dirty = FALSE;
	cheats_master_enabled = TRUE;
	cheats_autoloadsave = TRUE;
}

void cheats_clear_all(void)
{
	memset(cheats_list, 0, sizeof(cheats_list));
	cheats_dirty = TRUE;
}


boole cheats_get_master_enabled(void)
{
	return cheats_master_enabled;
}

void cheats_set_master_enabled(boole enable)
{
	cheats_master_enabled = enable ? TRUE : FALSE;
}

boole cheats_get_autoloadsave(void)
{
	return cheats_autoloadsave;
}

void cheats_set_autoloadsave(boole enable)
{
	cheats_autoloadsave = enable ? TRUE : FALSE;
}

auint cheats_get_capacity(void)
{
	return CHEATS_MAX;
}

auint cheats_get_count(void)
{
	auint i;
	auint n = 0U;
	for (i = 0U; i < CHEATS_MAX; i++){
		if (cheats_list[i].used){ n++; }
	}
	return n;
}

boole cheats_get_entry(auint idx, cheat_entry_t* out)
{
	if ((idx >= CHEATS_MAX) || (out == NULL)){ return FALSE; }
	*out = cheats_list[idx];
	return TRUE;
}

boole cheats_set_entry(auint idx, cheat_entry_t const* in)
{
	cheat_entry_t tmp;
	uint32       mask;
	auint        width;
	if ((idx >= CHEATS_MAX) || (in == NULL)){ return FALSE; }
	tmp = *in;
	width = cheats_clamp_width(tmp.width_bytes);
	tmp.width_bytes = (uint8)width;
	if ((tmp.addr + width) > 0x1000U){
		tmp.addr = 0x1000U - width;
	}
	mask = cheats_width_mask(width);
	tmp.value &= mask;
	tmp.compare &= mask;
	tmp.desc[CHEAT_DESC_MAX - 1U] = 0;
	if ((tmp.used) && (tmp.desc[0] == 0)){
		snprintf(tmp.desc, CHEAT_DESC_MAX, "$%03X = $%02X", (unsigned)tmp.addr, (unsigned)tmp.value);
	}
	cheats_list[idx] = tmp;
	cheats_dirty = TRUE;
	return TRUE;
}

boole cheats_add_entry(auint* out_idx)
{
	auint i;
	cheat_entry_t ent;
	for (i = 0U; i < CHEATS_MAX; i++){
		if (!cheats_list[i].used){
			memset(&ent, 0, sizeof(ent));
			ent.used = TRUE;
			ent.enabled = TRUE;
			ent.compare_used = FALSE;
			ent.signed_value = FALSE;
			ent.width_bytes = 1U;
			ent.addr = 0U;
			ent.value = 0U;
			ent.compare = 0U;
			cheats_set_entry(i, &ent);
			if (out_idx != NULL){ *out_idx = i; }
			return TRUE;
		}
	}
	return FALSE;
}

void cheats_remove_entry(auint idx)
{
	if (idx >= CHEATS_MAX){ return; }
	memset(&cheats_list[idx], 0, sizeof(cheats_list[idx]));
	cheats_dirty = TRUE;
}

void cheats_apply(void)
{
	cu_state_cpu_t* ecpu;
	auint           i;
	ecpu = cu_avr_get_state();
	if ((ecpu == NULL) || (!cheats_master_enabled)){ return; }
	for (i = 0U; i < CHEATS_MAX; i++){
		if (!cheats_list[i].used){ continue; }
		if (!cheats_list[i].enabled){ continue; }
		{
			auint  width = cheats_clamp_width(cheats_list[i].width_bytes);
			uint32 cur = cheats_read_sram_value(ecpu, cheats_list[i].addr & 0x0FFFU, width);
			if (cheats_list[i].compare_used){
				if (cur != (cheats_list[i].compare & cheats_width_mask(width))){ continue; }
			}
			cheats_write_sram_value(ecpu, cheats_list[i].addr & 0x0FFFU, width, cheats_list[i].value);
		}
	}
}

void cheats_set_current_path(char const* path)
{
	cheats_copy_str(cheats_path, CHEAT_PATH_MAX, path);
}

char const* cheats_get_current_path(void)
{
	return cheats_path;
}

boole cheats_is_dirty(void)
{
	return cheats_dirty;
}

boole cheats_load_file(char const* path)
{
	FILE* f;
	char  buf[512];
	char* eq;
	char* key;
	char* val;
	auint idx;
	char* suffix;

	if (path == NULL){ return FALSE; }
	memset(cheats_list, 0, sizeof(cheats_list));
	cheats_set_current_path(path);
	f = fopen(path, "r");
	if (f == NULL){
		cheats_dirty = FALSE;
		return FALSE;
	}
	while (fgets(buf, sizeof(buf), f) != NULL){
		key = buf;
		while ((*key != 0) && isspace((unsigned char)*key)){ key++; }
		if ((*key == '#') || (*key == ';') || (*key == '\n') || (*key == 0)){ continue; }
		eq = strchr(key, '=');
		if (eq == NULL){ continue; }
		*eq = 0;
		val = eq + 1;
		cheats_trim(key);
		cheats_trim(val);
		if (strcmp(key, "Enabled") == 0){
			cheats_master_enabled = ((strcmp(val, "1") == 0) || (strcmp(val, "true") == 0) || (strcmp(val, "on") == 0));
			continue;
		}
		if (strcmp(key, "AutoLoadSave") == 0){
			cheats_autoloadsave = ((strcmp(val, "1") == 0) || (strcmp(val, "true") == 0) || (strcmp(val, "on") == 0));
			continue;
		}
		if (strncmp(key, "Cheat", 5) != 0){ continue; }
		idx = (auint)strtoul(key + 5, &suffix, 10);
		if ((suffix == NULL) || (idx >= CHEATS_MAX)){ continue; }
		if (strcmp(suffix, "Enabled") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].enabled = ((strcmp(val, "1") == 0) || (strcmp(val, "true") == 0) || (strcmp(val, "on") == 0));
		}else if (strcmp(suffix, "Addr") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].addr = (auint)strtoul(val, NULL, 0) & 0x0FFFU;
		}else if (strcmp(suffix, "Value") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].value = (auint)strtoul(val, NULL, 0) & 0x00FFU;
		}else if (strcmp(suffix, "CompareUsed") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].compare_used = ((strcmp(val, "1") == 0) || (strcmp(val, "true") == 0) || (strcmp(val, "on") == 0));
		}else if (strcmp(suffix, "Compare") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].compare = (uint32)strtoul(val, NULL, 0);
		}else if (strcmp(suffix, "Width") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].width_bytes = (uint8)cheats_clamp_width((auint)strtoul(val, NULL, 0));
		}else if (strcmp(suffix, "Signed") == 0){
			cheats_list[idx].used = TRUE;
			cheats_list[idx].signed_value = ((strcmp(val, "1") == 0) || (strcmp(val, "true") == 0) || (strcmp(val, "on") == 0));
		}else if (strcmp(suffix, "Desc") == 0){
			cheats_list[idx].used = TRUE;
			cheats_copy_str(cheats_list[idx].desc, CHEAT_DESC_MAX, val);
		}
	}
	fclose(f);
	for (idx = 0U; idx < CHEATS_MAX; idx++){
		if (cheats_list[idx].width_bytes == 0U){ cheats_list[idx].width_bytes = 1U; }
		cheats_list[idx].width_bytes = (uint8)cheats_clamp_width(cheats_list[idx].width_bytes);
		cheats_list[idx].value &= cheats_width_mask(cheats_list[idx].width_bytes);
		cheats_list[idx].compare &= cheats_width_mask(cheats_list[idx].width_bytes);
		if ((cheats_list[idx].used) && (cheats_list[idx].desc[0] == 0)){
			snprintf(cheats_list[idx].desc, CHEAT_DESC_MAX, "$%03X = $%0*X", (unsigned)cheats_list[idx].addr, (int)(cheats_list[idx].width_bytes * 2U), (unsigned)cheats_list[idx].value);
		}
	}
	cheats_dirty = FALSE;
	return TRUE;
}

boole cheats_save_file(char const* path)
{
	FILE* f;
	auint i;
	char const* usepath;
	usepath = path;
	if ((usepath == NULL) || (usepath[0] == 0)){
		usepath = cheats_path;
	}
	if ((usepath == NULL) || (usepath[0] == 0)){ return FALSE; }
	if (!cheats_mkdirs_for_file(usepath)){ return FALSE; }
	f = fopen(usepath, "w");
	if (f == NULL){ return FALSE; }
	fprintf(f, "# CUzeBox cheats\n");
	fprintf(f, "Version=%u\n", (unsigned)CHEATS_FILE_VERSION);
	fprintf(f, "Enabled=%u\n", cheats_master_enabled ? 1U : 0U);
	fprintf(f, "AutoLoadSave=%u\n", cheats_autoloadsave ? 1U : 0U);
	for (i = 0U; i < CHEATS_MAX; i++){
		if (!cheats_list[i].used){ continue; }
		fprintf(f, "Cheat%uEnabled=%u\n", (unsigned)i, cheats_list[i].enabled ? 1U : 0U);
		fprintf(f, "Cheat%uAddr=0x%03X\n", (unsigned)i, (unsigned)(cheats_list[i].addr & 0x0FFFU));
		fprintf(f, "Cheat%uValue=0x%02X\n", (unsigned)i, (unsigned)(cheats_list[i].value & 0x00FFU));
		fprintf(f, "Cheat%uCompareUsed=%u\n", (unsigned)i, cheats_list[i].compare_used ? 1U : 0U);
		fprintf(f, "Cheat%uCompare=0x%02X\n", (unsigned)i, (unsigned)(cheats_list[i].compare & 0x00FFU));
		fprintf(f, "Cheat%uDesc=%s\n", (unsigned)i, cheats_list[i].desc);
	}
	fclose(f);
	cheats_set_current_path(usepath);
	cheats_dirty = FALSE;
	return TRUE;
}
