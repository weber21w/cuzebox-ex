#ifndef REMOTE_ROMS_H
#define REMOTE_ROMS_H

#include "types.h"

#define REMOTE_ROMS_MAX_GAMES 384U
#define REMOTE_ROMS_STR_CAP   128U
#define REMOTE_ROMS_PATH_CAP  1024U

typedef struct{
	auint	id;
	char	title[REMOTE_ROMS_STR_CAP];
	char	authors[REMOTE_ROMS_STR_CAP];
	char	status[16];
} remote_roms_game_t;

void		remote_roms_init(void);
void		remote_roms_shutdown(void);
void		remote_roms_set_host(char const* host);
char const*	remote_roms_get_host(void);
boole		remote_roms_refresh_list(void);
auint		remote_roms_get_count(void);
boole		remote_roms_get_entry(auint idx, remote_roms_game_t* out);
boole		remote_roms_download_game(auint idx, boole run_after);
boole		remote_roms_download_all(void);
char const*	remote_roms_get_status(void);
char const*	remote_roms_get_last_rom_path(void);

#endif
