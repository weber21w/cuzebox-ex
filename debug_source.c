#include "debug_source.h"
#include "debug_dwarf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define CU_DBG_SOURCE_PATH_MAX 1024U
#define CU_DBG_SOURCE_STATUS_MAX 256U
#define CU_DBG_SOURCE_UNIT_FILES_MAX 1024U
#define CU_DBG_SOURCE_UNIT_DIRS_MAX 256U

typedef struct{
	uint32_t word_addr;
	uint32_t end_word_addr;
	uint32_t file_index;
	uint32_t line;
	uint32_t column;
} cu_debug_source_row_t;

static char** g_files = NULL;
static uint32_t g_file_count = 0U;
static uint32_t g_file_cap = 0U;
static cu_debug_source_row_t* g_rows = NULL;
static uint32_t g_row_count = 0U;
static uint32_t g_row_cap = 0U;
static char g_status[CU_DBG_SOURCE_STATUS_MAX] = "No DWARF source information loaded.";
static char g_elf_dir[CU_DBG_SOURCE_PATH_MAX] = ".";

static uint16_t rd16(uint8_t const* p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(uint8_t const* p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void set_status(char const* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_status, sizeof(g_status), fmt, ap);
	va_end(ap);
}

static char* dupstr(char const* s)
{
	size_t n;
	char* p;
	if (s == NULL){ return NULL; }
	n = strlen(s) + 1U;
	p = (char*)malloc(n);
	if (p != NULL){ memcpy(p, s, n); }
	return p;
}

static int is_abs_path(char const* p)
{
	if ((p == NULL) || (p[0] == 0)){ return 0; }
	if ((p[0] == '/') || (p[0] == '\\')){ return 1; }
	if (isalpha((unsigned char)p[0]) && (p[1] == ':')){ return 1; }
	return 0;
}

static void path_dirname(char* out, size_t cap, char const* path)
{
	char const* slash = NULL;
	char const* p;
	size_t n;
	if ((out == NULL) || (cap == 0U)){ return; }
	if ((path == NULL) || (path[0] == 0)){ snprintf(out, cap, "."); return; }
	for (p = path; *p != 0; ++p){
		if ((*p == '/') || (*p == '\\')){ slash = p; }
	}
	if (slash == NULL){ snprintf(out, cap, "."); return; }
	n = (size_t)(slash - path);
	if (n == 0U){ n = 1U; }
	if (n >= cap){ n = cap - 1U; }
	memcpy(out, path, n);
	out[n] = 0;
}

static void path_join(char* out, size_t cap, char const* a, char const* b)
{
	if ((out == NULL) || (cap == 0U)){ return; }
	if ((b != NULL) && is_abs_path(b)){
		snprintf(out, cap, "%s", b);
		return;
	}
	if ((a == NULL) || (a[0] == 0) || (strcmp(a, ".") == 0)){
		snprintf(out, cap, "%s", (b != NULL) ? b : "");
		return;
	}
	if ((b == NULL) || (b[0] == 0)){
		snprintf(out, cap, "%s", a);
		return;
	}
	snprintf(out, cap, "%s/%s", a, b);
}


static int file_exists(char const* path)
{
	FILE* f;
	if ((path == NULL) || (path[0] == 0)){ return 0; }
	f = fopen(path, "rb");
	if (f == NULL){ return 0; }
	fclose(f);
	return 1;
}

static char const* path_basename_any(char const* path)
{
	char const* base = path;
	char const* p;
	if (path == NULL){ return ""; }
	for (p = path; *p != 0; ++p){ if ((*p == '/') || (*p == '\\')){ base = p + 1; } }
	return base;
}

static void source_path_resolve(char* out, size_t cap, char const* candidate)
{
	char alt[CU_DBG_SOURCE_PATH_MAX];
	char alt2[CU_DBG_SOURCE_PATH_MAX];
	char const* base;
	size_t i;
	if ((out == NULL) || (cap == 0U)){ return; }
	snprintf(out, cap, "%s", (candidate != NULL) ? candidate : "");
	if (file_exists(out)){ return; }

	/* WSL path in DWARF, native Windows CUzeBox.  Trying this form is harmless
	** on other hosts because it is only selected if the translated file exists. */
	if ((candidate != NULL) && (strncmp(candidate, "/mnt/", 5U) == 0) && isalpha((unsigned char)candidate[5]) && (candidate[6] == '/')){
		snprintf(alt, sizeof(alt), "%c:/%s", (char)toupper((unsigned char)candidate[5]), candidate + 7);
		if (file_exists(alt)){ snprintf(out, cap, "%s", alt); return; }
	}

	/* Native Windows path embedded by a Windows toolchain, CUzeBox running in
	** WSL. */
	if ((candidate != NULL) && isalpha((unsigned char)candidate[0]) && (candidate[1] == ':') && ((candidate[2] == '\\') || (candidate[2] == '/'))){
		snprintf(alt, sizeof(alt), "/mnt/%c/%s", (char)tolower((unsigned char)candidate[0]), candidate + 3);
		for (i = 0U; alt[i] != 0; ++i){ if (alt[i] == '\\'){ alt[i] = '/'; } }
		if (file_exists(alt)){ snprintf(out, cap, "%s", alt); return; }
	}

	/* A relocated build tree often still has the source immediately above the
	** ELF's default/ directory even when the absolute compile path is stale. */
	base = path_basename_any(candidate);
	if ((base != NULL) && (base[0] != 0)){
		path_join(alt, sizeof(alt), g_elf_dir, base);
		if (file_exists(alt)){ snprintf(out, cap, "%s", alt); return; }
		path_join(alt2, sizeof(alt2), g_elf_dir, "..");
		path_join(alt, sizeof(alt), alt2, base);
		if (file_exists(alt)){ snprintf(out, cap, "%s", alt); return; }
	}
}

static uint32_t add_file(char const* path)
{
	uint32_t i;
	char** next;
	char* copy;
	if ((path == NULL) || (path[0] == 0)){ return UINT32_MAX; }
	for (i = 0U; i < g_file_count; ++i){
		if (strcmp(g_files[i], path) == 0){ return i; }
	}
	if (g_file_count == g_file_cap){
		uint32_t next_cap = (g_file_cap == 0U) ? 64U : (g_file_cap * 2U);
		next = (char**)realloc(g_files, (size_t)next_cap * sizeof(*g_files));
		if (next == NULL){ return UINT32_MAX; }
		g_files = next;
		g_file_cap = next_cap;
	}
	copy = dupstr(path);
	if (copy == NULL){ return UINT32_MAX; }
	g_files[g_file_count] = copy;
	return g_file_count++;
}

static int add_row(uint32_t byte_addr, uint32_t file_index, uint32_t line, uint32_t column,
                   uint32_t* last_row_index)
{
	cu_debug_source_row_t* next;
	cu_debug_source_row_t* row;
	uint32_t word_addr = (byte_addr >> 1) & 0x7FFFU;
	if ((file_index == UINT32_MAX) || (file_index >= g_file_count) || (line == 0U)){ return 1; }
	if (g_row_count == g_row_cap){
		uint32_t next_cap = (g_row_cap == 0U) ? 4096U : (g_row_cap * 2U);
		next = (cu_debug_source_row_t*)realloc(g_rows, (size_t)next_cap * sizeof(*g_rows));
		if (next == NULL){ return 0; }
		g_rows = next;
		g_row_cap = next_cap;
	}
	if ((last_row_index != NULL) && (*last_row_index != UINT32_MAX) && (*last_row_index < g_row_count)){
		cu_debug_source_row_t* prev = &g_rows[*last_row_index];
		if (word_addr > prev->word_addr){ prev->end_word_addr = word_addr; }
		else if (prev->end_word_addr <= prev->word_addr){ prev->end_word_addr = prev->word_addr + 1U; }
	}
	row = &g_rows[g_row_count];
	row->word_addr = word_addr;
	row->end_word_addr = word_addr + 1U;
	row->file_index = file_index;
	row->line = line;
	row->column = column;
	if (last_row_index != NULL){ *last_row_index = g_row_count; }
	g_row_count++;
	return 1;
}

static int finish_sequence(uint32_t byte_addr, uint32_t* last_row_index)
{
	uint32_t end_word = (byte_addr >> 1) & 0x7FFFU;
	if ((last_row_index != NULL) && (*last_row_index != UINT32_MAX) && (*last_row_index < g_row_count)){
		cu_debug_source_row_t* prev = &g_rows[*last_row_index];
		if (end_word > prev->word_addr){ prev->end_word_addr = end_word; }
		else{ prev->end_word_addr = prev->word_addr + 1U; }
		*last_row_index = UINT32_MAX;
	}
	return 1;
}

static int read_uleb(uint8_t const** pp, uint8_t const* end, uint32_t* out)
{
	uint32_t value = 0U;
	unsigned shift = 0U;
	uint8_t const* p = *pp;
	while (p < end && shift < 35U){
		uint8_t b = *p++;
		value |= ((uint32_t)(b & 0x7FU)) << shift;
		if ((b & 0x80U) == 0U){
			*pp = p;
			if (out != NULL){ *out = value; }
			return 1;
		}
		shift += 7U;
	}
	return 0;
}

static int read_sleb(uint8_t const** pp, uint8_t const* end, int32_t* out)
{
	int32_t value = 0;
	unsigned shift = 0U;
	uint8_t b = 0U;
	uint8_t const* p = *pp;
	while (p < end && shift < 35U){
		b = *p++;
		value |= ((int32_t)(b & 0x7FU)) << shift;
		shift += 7U;
		if ((b & 0x80U) == 0U){ break; }
	}
	if ((b & 0x80U) != 0U){ return 0; }
	if ((shift < 32U) && ((b & 0x40U) != 0U)){ value |= -((int32_t)1 << shift); }
	*pp = p;
	if (out != NULL){ *out = value; }
	return 1;
}

static int read_cstring(uint8_t const** pp, uint8_t const* end, char const** out)
{
	uint8_t const* p = *pp;
	uint8_t const* q = p;
	while ((q < end) && (*q != 0U)){ ++q; }
	if (q >= end){ return 0; }
	if (out != NULL){ *out = (char const*)p; }
	*pp = q + 1U;
	return 1;
}

typedef struct{
	char const* dirs[CU_DBG_SOURCE_UNIT_DIRS_MAX];
	uint32_t dir_count;
	uint32_t files[CU_DBG_SOURCE_UNIT_FILES_MAX];
	uint32_t file_count;
} unit_paths_t;

static int unit_add_file(unit_paths_t* up, char const* name, uint32_t dir_index)
{
	char tmp[CU_DBG_SOURCE_PATH_MAX];
	char tmp2[CU_DBG_SOURCE_PATH_MAX];
	char const* dir = NULL;
	uint32_t global_index;
	if ((up == NULL) || (name == NULL) || (name[0] == 0)){ return 0; }
	if (up->file_count >= CU_DBG_SOURCE_UNIT_FILES_MAX){ return 0; }
	if (dir_index > 0U && dir_index <= up->dir_count){ dir = up->dirs[dir_index - 1U]; }
	if ((dir != NULL) && (dir[0] != 0)){
		if (is_abs_path(dir)){
			path_join(tmp, sizeof(tmp), dir, name);
		}else{
			path_join(tmp2, sizeof(tmp2), g_elf_dir, dir);
			path_join(tmp, sizeof(tmp), tmp2, name);
		}
	}else{
		path_join(tmp, sizeof(tmp), g_elf_dir, name);
	}
	{
		char resolved[CU_DBG_SOURCE_PATH_MAX];
		source_path_resolve(resolved, sizeof(resolved), tmp);
		global_index = add_file(resolved);
	}
	if (global_index == UINT32_MAX){ return 0; }
	up->files[up->file_count++] = global_index;
	return 1;
}

static int parse_line_unit(uint8_t const* unit_start, uint8_t const* unit_end, uint16_t version)
{
	uint8_t const* p = unit_start;
	uint32_t header_length;
	uint8_t const* header_end;
	uint8_t min_inst_len;
	uint8_t max_ops = 1U;
	uint8_t default_is_stmt;
	int8_t line_base;
	uint8_t line_range;
	uint8_t opcode_base;
	uint8_t std_lengths[256];
	unit_paths_t up;
	uint32_t address = 0U;
	uint32_t file = 1U;
	int32_t line = 1;
	uint32_t column = 0U;
	uint8_t is_stmt;
	uint32_t last_row_index = UINT32_MAX;
	unsigned i;
	(void)max_ops;
	memset(&up, 0, sizeof(up));
	memset(std_lengths, 0, sizeof(std_lengths));
	if ((size_t)(unit_end - p) < 4U){ return 0; }
	header_length = rd32(p); p += 4U;
	if ((size_t)(unit_end - p) < (size_t)header_length){ return 0; }
	header_end = p + header_length;
	if (p >= header_end){ return 0; }
	min_inst_len = *p++;
	if (version >= 4U){ if (p >= header_end){ return 0; } max_ops = *p++; }
	if ((size_t)(header_end - p) < 4U){ return 0; }
	default_is_stmt = *p++;
	line_base = (int8_t)*p++;
	line_range = *p++;
	opcode_base = *p++;
	if ((line_range == 0U) || (opcode_base == 0U)){ return 0; }
	if ((size_t)(header_end - p) < (size_t)(opcode_base - 1U)){ return 0; }
	for (i = 1U; i < opcode_base; ++i){ std_lengths[i] = *p++; }
	while (p < header_end){
		char const* dir;
		if (!read_cstring(&p, header_end, &dir)){ return 0; }
		if (dir[0] == 0){ break; }
		if (up.dir_count < CU_DBG_SOURCE_UNIT_DIRS_MAX){ up.dirs[up.dir_count++] = dir; }
	}
	while (p < header_end){
		char const* name;
		uint32_t dir_index = 0U, modtime = 0U, length = 0U;
		if (!read_cstring(&p, header_end, &name)){ return 0; }
		if (name[0] == 0){ break; }
		if (!read_uleb(&p, header_end, &dir_index) || !read_uleb(&p, header_end, &modtime) || !read_uleb(&p, header_end, &length)){ return 0; }
		(void)modtime; (void)length;
		if (!unit_add_file(&up, name, dir_index)){ return 0; }
	}
	p = header_end;
	is_stmt = default_is_stmt;
	while (p < unit_end){
		uint8_t opcode = *p++;
		if (opcode == 0U){
			uint32_t ext_len;
			uint8_t const* ext_end;
			uint8_t sub;
			if (!read_uleb(&p, unit_end, &ext_len)){ return 0; }
			if ((ext_len == 0U) || ((size_t)(unit_end - p) < ext_len)){ return 0; }
			ext_end = p + ext_len;
			sub = *p++;
			if (sub == 1U){
				finish_sequence(address, &last_row_index);
				address = 0U; file = 1U; line = 1; column = 0U; is_stmt = default_is_stmt;
			}else if (sub == 2U){
				uint32_t v = 0U;
				unsigned bytes = (unsigned)(ext_end - p);
				unsigned j;
				if (bytes > 4U){ bytes = 4U; }
				for (j = 0U; j < bytes; ++j){ v |= ((uint32_t)p[j]) << (8U * j); }
				address = v;
			}else if (sub == 3U){
				char const* name;
				uint32_t dir_index = 0U, modtime = 0U, length = 0U;
				if (read_cstring(&p, ext_end, &name) && name[0] != 0 && read_uleb(&p, ext_end, &dir_index) && read_uleb(&p, ext_end, &modtime) && read_uleb(&p, ext_end, &length)){
					(void)unit_add_file(&up, name, dir_index);
				}
			}
			p = ext_end;
			continue;
		}
		if (opcode < opcode_base){
			switch (opcode){
				case 1U:
					if ((file > 0U) && (file <= up.file_count) && (line > 0)){
						if (!add_row(address, up.files[file - 1U], (uint32_t)line, column, &last_row_index)){ return 0; }
					}
					break;
				case 2U: {
					uint32_t v; if (!read_uleb(&p, unit_end, &v)){ return 0; } address += v * (uint32_t)min_inst_len; break;
				}
				case 3U: {
					int32_t v; if (!read_sleb(&p, unit_end, &v)){ return 0; } line += v; break;
				}
				case 4U: if (!read_uleb(&p, unit_end, &file)){ return 0; } break;
				case 5U: if (!read_uleb(&p, unit_end, &column)){ return 0; } break;
				case 6U: is_stmt = (uint8_t)!is_stmt; break;
				case 7U: break;
				case 8U: address += ((255U - opcode_base) / line_range) * (uint32_t)min_inst_len; break;
				case 9U:
					if ((size_t)(unit_end - p) < 2U){ return 0; }
					address += rd16(p); p += 2U; break;
				default: {
					uint8_t argc = std_lengths[opcode];
					uint8_t k;
					for (k = 0U; k < argc; ++k){ uint32_t ignored; if (!read_uleb(&p, unit_end, &ignored)){ return 0; } }
					break;
				}
			}
		}else{
			uint32_t adjusted = (uint32_t)(opcode - opcode_base);
			address += (adjusted / line_range) * (uint32_t)min_inst_len;
			line += (int32_t)line_base + (int32_t)(adjusted % line_range);
			if ((file > 0U) && (file <= up.file_count) && (line > 0)){
				if (!add_row(address, up.files[file - 1U], (uint32_t)line, column, &last_row_index)){ return 0; }
			}
		}
	}
	finish_sequence(address + 2U, &last_row_index);
	return 1;
}

static int row_compare(void const* a, void const* b)
{
	cu_debug_source_row_t const* ra = (cu_debug_source_row_t const*)a;
	cu_debug_source_row_t const* rb = (cu_debug_source_row_t const*)b;
	if (ra->word_addr < rb->word_addr){ return -1; }
	if (ra->word_addr > rb->word_addr){ return 1; }
	if (ra->end_word_addr < rb->end_word_addr){ return -1; }
	if (ra->end_word_addr > rb->end_word_addr){ return 1; }
	return 0;
}

void cu_debug_source_reset(void)
{
	uint32_t i;
	for (i = 0U; i < g_file_count; ++i){ free(g_files[i]); }
	free(g_files); g_files = NULL; g_file_count = 0U; g_file_cap = 0U;
	free(g_rows); g_rows = NULL; g_row_count = 0U; g_row_cap = 0U;
	set_status("No DWARF source information loaded.");
	cu_debug_dwarf_reset();
}

int cu_debug_source_load_elf(char const* path)
{
	FILE* f;
	long flen;
	uint8_t* data = NULL;
	size_t size;
	uint32_t shoff;
	uint16_t shentsize, shnum, shstrndx;
	uint16_t i;
	uint8_t const* debug_line = NULL;
	uint32_t debug_line_size = 0U;
	uint32_t units = 0U;
	uint32_t bad_units = 0U;
	cu_debug_source_reset();
	if ((path == NULL) || (path[0] == 0)){ set_status("No ELF file selected."); return 0; }
	/* Variable/type information is parsed independently so a partially supported
	** line table never prevents DWARF data inspection. */
	(void)cu_debug_dwarf_load_elf(path);
	path_dirname(g_elf_dir, sizeof(g_elf_dir), path);
	f = fopen(path, "rb");
	if (f == NULL){ set_status("DWARF: unable to open %s", path); return 0; }
	if (fseek(f, 0L, SEEK_END) != 0 || (flen = ftell(f)) < 0L || fseek(f, 0L, SEEK_SET) != 0){ fclose(f); set_status("DWARF: unable to read %s", path); return 0; }
	size = (size_t)flen;
	data = (uint8_t*)malloc(size ? size : 1U);
	if (data == NULL){ fclose(f); set_status("DWARF: out of memory"); return 0; }
	if (size != fread(data, 1U, size, f)){ fclose(f); free(data); set_status("DWARF: short read"); return 0; }
	fclose(f);
	if ((size < 52U) || data[0] != 0x7FU || data[1] != 'E' || data[2] != 'L' || data[3] != 'F' || data[4] != 1U || data[5] != 1U){ free(data); set_status("DWARF: unsupported ELF format"); return 0; }
	shoff = rd32(data + 32U); shentsize = rd16(data + 46U); shnum = rd16(data + 48U); shstrndx = rd16(data + 50U);
	if ((shentsize < 40U) || (shnum == 0U) || (shstrndx >= shnum) || ((size_t)shoff + (size_t)shentsize * shnum > size)){ free(data); set_status("DWARF: corrupt ELF section table"); return 0; }
	{
		uint8_t const* shstr = data + shoff + (size_t)shstrndx * shentsize;
		uint32_t shstroff = rd32(shstr + 16U), shstrsize = rd32(shstr + 20U);
		if ((size_t)shstroff + shstrsize > size){ free(data); set_status("DWARF: corrupt section names"); return 0; }
		for (i = 0U; i < shnum; ++i){
			uint8_t const* sh = data + shoff + (size_t)i * shentsize;
			uint32_t nameoff = rd32(sh + 0U), off = rd32(sh + 16U), secsize = rd32(sh + 20U);
			char const* name;
			if (nameoff >= shstrsize || (size_t)off + secsize > size){ continue; }
			name = (char const*)(data + shstroff + nameoff);
			if (strcmp(name, ".debug_line") == 0){ debug_line = data + off; debug_line_size = secsize; break; }
		}
	}
	if ((debug_line == NULL) || (debug_line_size == 0U)){ free(data); set_status("ELF has no .debug_line section."); return 0; }
	{
		uint8_t const* p = debug_line;
		uint8_t const* end = debug_line + debug_line_size;
		while ((size_t)(end - p) >= 10U){
			uint32_t unit_len = rd32(p); uint8_t const* unit_end; uint16_t version;
			p += 4U;
			if (unit_len == 0U){ continue; }
			if (unit_len == 0xFFFFFFFFU || (size_t)(end - p) < unit_len){ bad_units++; break; }
			unit_end = p + unit_len;
			if ((size_t)(unit_end - p) < 2U){ bad_units++; p = unit_end; continue; }
			version = rd16(p); p += 2U;
			if (version < 2U || version > 4U){ bad_units++; p = unit_end; continue; }
			if (parse_line_unit(p, unit_end, version)){ units++; } else{ bad_units++; }
			p = unit_end;
		}
	}
	free(data);
	if (g_row_count != 0U){ qsort(g_rows, g_row_count, sizeof(*g_rows), row_compare); }
	if (g_row_count == 0U){ set_status("DWARF line table parsed but produced no AVR source rows%s", bad_units ? " (some units unsupported)" : ""); return 0; }
	set_status("Loaded %u DWARF source rows from %u file%s (%u unit%s%s)",
		(unsigned)g_row_count, (unsigned)g_file_count, g_file_count == 1U ? "" : "s",
		(unsigned)units, units == 1U ? "" : "s", bad_units ? ", some units ignored" : "");
	return 1;
}

char const* cu_debug_source_status(void){ return g_status; }
uint32_t cu_debug_source_file_count(void){ return g_file_count; }
uint32_t cu_debug_source_row_count(void){ return g_row_count; }
char const* cu_debug_source_file_path(uint32_t index){ return (index < g_file_count) ? g_files[index] : NULL; }

int cu_debug_source_lookup(uint32_t word_addr, cu_debug_source_location_t* out)
{
	uint32_t lo = 0U, hi = g_row_count;
	uint32_t i;
	if (g_row_count == 0U){ return 0; }
	word_addr &= 0x7FFFU;
	while (lo < hi){ uint32_t mid = lo + ((hi - lo) >> 1); if (g_rows[mid].word_addr <= word_addr){ lo = mid + 1U; } else{ hi = mid; } }
	if (lo == 0U){ return 0; }
	i = lo - 1U;
	for (;;){
		cu_debug_source_row_t const* row = &g_rows[i];
		if (row->word_addr <= word_addr && word_addr < row->end_word_addr){
			if (out != NULL){ out->file_index = row->file_index; out->line = row->line; out->column = row->column; out->word_addr = row->word_addr; out->end_word_addr = row->end_word_addr; }
			return 1;
		}
		if (i == 0U || g_rows[i - 1U].word_addr < word_addr){ break; }
		--i;
	}
	return 0;
}

int cu_debug_source_resolve_line(uint32_t file_index, uint32_t line, cu_debug_source_location_t* out)
{
	uint32_t i;
	uint32_t best = UINT32_MAX;
	uint32_t best_delta = UINT32_MAX;
	if (file_index >= g_file_count || line == 0U){ return 0; }
	for (i = 0U; i < g_row_count; ++i){
		uint32_t delta;
		if (g_rows[i].file_index != file_index){ continue; }
		if (g_rows[i].line >= line){ delta = g_rows[i].line - line; }
		else{ delta = (line - g_rows[i].line) + 0x10000000U; }
		if (delta < best_delta){ best_delta = delta; best = i; if (delta == 0U){ break; } }
	}
	if (best == UINT32_MAX){ return 0; }
	if (out != NULL){ out->file_index = g_rows[best].file_index; out->line = g_rows[best].line; out->column = g_rows[best].column; out->word_addr = g_rows[best].word_addr; out->end_word_addr = g_rows[best].end_word_addr; }
	return 1;
}
