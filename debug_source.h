#ifndef DEBUG_SOURCE_H
#define DEBUG_SOURCE_H

#include <stdint.h>

typedef struct{
	uint32_t file_index;
	uint32_t line;
	uint32_t column;
	uint32_t word_addr;
	uint32_t end_word_addr;
} cu_debug_source_location_t;

void        cu_debug_source_reset(void);
int         cu_debug_source_load_elf(char const* path);
char const* cu_debug_source_status(void);
uint32_t    cu_debug_source_file_count(void);
uint32_t    cu_debug_source_row_count(void);
char const* cu_debug_source_file_path(uint32_t index);
int         cu_debug_source_lookup(uint32_t word_addr, cu_debug_source_location_t* out);
int         cu_debug_source_resolve_line(uint32_t file_index, uint32_t line, cu_debug_source_location_t* out);

#endif
