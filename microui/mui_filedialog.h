#ifndef MUI_FILEDIALOG_H
#define MUI_FILEDIALOG_H

#include "../types.h"
#include "microui.h"

#define MUI_FILEDIALOG_PATH_CAP   1024
#define MUI_FILEDIALOG_TITLE_CAP  64
#define MUI_FILEDIALOG_FILTER_CAP 128

typedef struct {
	char  name[MUI_FILEDIALOG_PATH_CAP];
	boole is_dir;
} mui_filedialog_entry_t;

typedef struct mui_filedialog_t {
	boole                   open;
	boole                   dirty;
	boole                   accepted;
	char                    title[MUI_FILEDIALOG_TITLE_CAP];
	char                    cwd[MUI_FILEDIALOG_PATH_CAP];
	char                    exts[MUI_FILEDIALOG_FILTER_CAP];
	char                    selected_path[MUI_FILEDIALOG_PATH_CAP];
	char                    accepted_path[MUI_FILEDIALOG_PATH_CAP];
	char                    input_name[MUI_FILEDIALOG_PATH_CAP];
	boole                   dirs_only;
	boole                   allow_current_dir;
	boole                   save_mode;
	mui_filedialog_entry_t* entries;
	int                     entry_count;
	int                     list_offset;
	char                    last_click_path[MUI_FILEDIALOG_PATH_CAP];
} mui_filedialog_t;

void  mui_filedialog_init(mui_filedialog_t* dlg);
void  mui_filedialog_destroy(mui_filedialog_t* dlg);
void  mui_filedialog_open(mui_filedialog_t* dlg, char const* title, char const* start_dir, char const* exts);
void  mui_filedialog_open_dir(mui_filedialog_t* dlg, char const* title, char const* start_dir);
void  mui_filedialog_open_save(mui_filedialog_t* dlg, char const* title, char const* start_path, char const* exts);
void  mui_filedialog_close(mui_filedialog_t* dlg);
boole mui_filedialog_is_open(mui_filedialog_t const* dlg);
char const* mui_filedialog_get_title(mui_filedialog_t const* dlg);
char const* mui_filedialog_get_cwd(mui_filedialog_t const* dlg);
char const* mui_filedialog_get_selected_path(mui_filedialog_t const* dlg);
char const* mui_filedialog_get_accepted_path(mui_filedialog_t const* dlg);
boole mui_filedialog_consume_accept(mui_filedialog_t* dlg, char* out, auint cap);
int   mui_filedialog_draw(mu_Context* ctx, mui_filedialog_t* dlg, mu_Rect rect);

#endif
