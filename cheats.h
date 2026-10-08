#ifndef CHEATS_H
#define CHEATS_H

#include "types.h"

#define CHEATS_MAX      16U
#define CHEAT_DESC_MAX  40U
#define CHEAT_PATH_MAX  256U

typedef struct{
	boole used;
	boole enabled;
	boole compare_used;
	boole signed_value;
	uint8 width_bytes;
	auint addr;
	uint32 value;
	uint32 compare;
	char  desc[CHEAT_DESC_MAX];
} cheat_entry_t;

void  cheats_reset(void);
void  cheats_clear_all(void);
auint cheats_get_capacity(void);
auint cheats_get_count(void);
boole cheats_get_entry(auint idx, cheat_entry_t* out);
boole cheats_set_entry(auint idx, cheat_entry_t const* in);
boole cheats_add_entry(auint* out_idx);
void  cheats_remove_entry(auint idx);
void  cheats_apply(void);


boole cheats_get_master_enabled(void);
void  cheats_set_master_enabled(boole enable);
boole cheats_get_autoloadsave(void);
void  cheats_set_autoloadsave(boole enable);

void        cheats_set_current_path(char const* path);
char const* cheats_get_current_path(void);
boole       cheats_is_dirty(void);

boole cheats_load_file(char const* path);
boole cheats_save_file(char const* path);

#endif
