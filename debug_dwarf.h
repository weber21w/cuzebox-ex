#ifndef DEBUG_DWARF_H
#define DEBUG_DWARF_H

#include <stdint.h>
#include <stddef.h>

#define CU_DBG_DWARF_SCOPE_GLOBAL 0U
#define CU_DBG_DWARF_SCOPE_LOCAL  1U
#define CU_DBG_DWARF_SCOPE_PARAM  2U

#define CU_DBG_DWARF_TYPE_UNKNOWN  0U
#define CU_DBG_DWARF_TYPE_BASE     1U
#define CU_DBG_DWARF_TYPE_POINTER  2U
#define CU_DBG_DWARF_TYPE_TYPEDEF  3U
#define CU_DBG_DWARF_TYPE_CONST    4U
#define CU_DBG_DWARF_TYPE_VOLATILE 5U
#define CU_DBG_DWARF_TYPE_ARRAY    6U
#define CU_DBG_DWARF_TYPE_STRUCT   7U
#define CU_DBG_DWARF_TYPE_UNION    8U
#define CU_DBG_DWARF_TYPE_ENUM     9U

#define CU_DBG_DWARF_LOC_NONE        0U
#define CU_DBG_DWARF_LOC_ADDRESS     1U
#define CU_DBG_DWARF_LOC_REGISTER    2U
#define CU_DBG_DWARF_LOC_VALUE       3U
#define CU_DBG_DWARF_LOC_OPTIMIZED   4U
#define CU_DBG_DWARF_LOC_UNSUPPORTED 5U

#define CU_DBG_DWARF_VALUE_MAX 32U

typedef struct{
    uint32_t id;
    uint32_t kind;
    uint32_t byte_size;
    uint32_t target_type;
    uint32_t element_count;
    uint32_t member_count;
    uint32_t encoding;
    char const* name;
} cu_debug_dwarf_type_info_t;

typedef struct{
    uint32_t index;
    uint32_t type_id;
    uint32_t byte_offset;
    int32_t bit_offset;
    uint32_t bit_size;
    uint32_t is_enumerator;
    int64_t const_value;
    char const* name;
} cu_debug_dwarf_member_info_t;

typedef struct{
    uint32_t id;
    uint32_t scope;
    uint32_t type_id;
    uint32_t file_index;
    uint32_t line;
    uint32_t low_word_addr;
    uint32_t high_word_addr;
    uint32_t function_id;
    char const* name;
    char const* function_name;
} cu_debug_dwarf_variable_info_t;

typedef struct{
    uint32_t status;
    uint32_t location_kind;
    uint32_t address;
    uint32_t reg;
    uint32_t byte_size;
    uint32_t value_size;
    uint8_t value[CU_DBG_DWARF_VALUE_MAX];
    char message[128];
} cu_debug_dwarf_value_t;

typedef uint8_t (*cu_debug_dwarf_read_data_fn)(void* user, uint32_t addr);

void        cu_debug_dwarf_reset(void);
int         cu_debug_dwarf_load_elf(char const* path);
char const* cu_debug_dwarf_status(void);
uint32_t    cu_debug_dwarf_type_count(void);
uint32_t    cu_debug_dwarf_variable_count(void);
uint32_t    cu_debug_dwarf_global_count(void);
uint32_t    cu_debug_dwarf_local_count(uint32_t pc_word);
int         cu_debug_dwarf_type_info(uint32_t type_id, cu_debug_dwarf_type_info_t* out);
int         cu_debug_dwarf_type_format(uint32_t type_id, char* out, size_t cap);
int         cu_debug_dwarf_member_info(uint32_t type_id, uint32_t member_index, cu_debug_dwarf_member_info_t* out);
int         cu_debug_dwarf_variable_info(uint32_t var_id, cu_debug_dwarf_variable_info_t* out);
int         cu_debug_dwarf_global_at(uint32_t ordinal, uint32_t* out_var_id);
int         cu_debug_dwarf_local_at(uint32_t pc_word, uint32_t ordinal, uint32_t* out_var_id);
int         cu_debug_dwarf_eval_variable(uint32_t var_id, uint32_t pc_word, uint8_t const regs[32], uint32_t sp,
                                         cu_debug_dwarf_read_data_fn read_data, void* user,
                                         cu_debug_dwarf_value_t* out);
int         cu_debug_dwarf_eval_member(uint32_t var_id, uint32_t member_index, uint32_t pc_word, uint8_t const regs[32], uint32_t sp,
                                       cu_debug_dwarf_read_data_fn read_data, void* user,
                                       cu_debug_dwarf_value_t* out, uint32_t* out_type_id);
int         cu_debug_dwarf_eval_element(uint32_t var_id, uint32_t element_index, uint32_t pc_word, uint8_t const regs[32], uint32_t sp,
                                        cu_debug_dwarf_read_data_fn read_data, void* user,
                                        cu_debug_dwarf_value_t* out, uint32_t* out_type_id);

#endif
