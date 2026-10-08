#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "mui_filedialog.h"
#include "mui_integration.h"

#define MUI_HASH_INITIAL 2166136261u

static mu_Container* mui_filedialog_find_container(mu_Context* ctx, char const* title)
{
	mu_Id id;
	mu_Id last_id;
	int idx;
	int saved_idx;
	if ((ctx == NULL) || (title == NULL)){ return NULL; }
	saved_idx = ctx->id_stack.idx;
	last_id = ctx->last_id;
	ctx->id_stack.idx = 0;
	id = mu_get_id(ctx, title, (int)strlen(title));
	ctx->id_stack.idx = saved_idx;
	ctx->last_id = last_id;
	if (id == 0U){ id = (mu_Id)MUI_HASH_INITIAL; }
	idx = mu_pool_get(ctx, ctx->container_pool, MU_CONTAINERPOOL_SIZE, id);
	if (idx < 0){ return NULL; }
	return &ctx->containers[idx];
}

static void mui_filedialog_copy(char* dst, auint cap, char const* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

static int mui_filedialog_casecmp(char const* a, char const* b)
{
	unsigned char ca;
	unsigned char cb;
	for (;;){
		ca = (unsigned char)*a;
		cb = (unsigned char)*b;
		ca = (unsigned char)tolower(ca);
		cb = (unsigned char)tolower(cb);
		if ((ca != cb) || (ca == 0U) || (cb == 0U)){
			return (int)ca - (int)cb;
		}
		a++;
		b++;
	}
}

static char const* mui_filedialog_basename(char const* path)
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

static void mui_filedialog_set_dir(mui_filedialog_t* dlg, char const* dir)
{
	if (dlg == NULL){ return; }
	if ((dir == NULL) || (dir[0] == 0)){
		mui_filedialog_copy(dlg->cwd, sizeof(dlg->cwd), ".");
	}else{
		mui_filedialog_copy(dlg->cwd, sizeof(dlg->cwd), dir);
	}
	dlg->dirty = TRUE;
	dlg->selected_path[0] = 0;
	dlg->list_offset = 0;
	dlg->last_click_path[0] = 0;
}

static void mui_filedialog_get_parent(char const* path, char* out, auint cap)
{
	size_t len;
	if ((out == NULL) || (cap == 0U)){ return; }
	if ((path == NULL) || (path[0] == 0) || (strcmp(path, ".") == 0)){
		mui_filedialog_copy(out, cap, ".");
		return;
	}
	mui_filedialog_copy(out, cap, path);
	len = strlen(out);
	while ((len > 0U) && ((out[len - 1U] == '/') || (out[len - 1U] == '\\'))){
		out[len - 1U] = 0;
		len--;
	}
	while (len > 0U){
		if ((out[len - 1U] == '/') || (out[len - 1U] == '\\')){ break; }
		len--;
	}
	if (len == 0U){
		mui_filedialog_copy(out, cap, ".");
	}else if ((len == 1U) && ((out[0] == '/') || (out[0] == '\\'))){
		out[1] = 0;
	}else{
		out[len - 1U] = 0;
	}
}


static void mui_filedialog_split_path(char const* path, char* out_dir, auint dir_cap, char* out_name, auint name_cap)
{
	char const* slash1;
	char const* slash2;
	char const* slash;
	auint len;
	if ((out_dir != NULL) && (dir_cap != 0U)){ out_dir[0] = 0; }
	if ((out_name != NULL) && (name_cap != 0U)){ out_name[0] = 0; }
	if ((path == NULL) || (path[0] == 0)){
		mui_filedialog_copy(out_dir, dir_cap, ".");
		return;
	}
	slash1 = strrchr(path, '/');
	slash2 = strrchr(path, '\\');
	slash = slash1;
	if ((slash2 != NULL) && ((slash == NULL) || (slash2 > slash))){ slash = slash2; }
	if (slash == NULL){
		mui_filedialog_copy(out_dir, dir_cap, ".");
		mui_filedialog_copy(out_name, name_cap, path);
		return;
	}
	len = (auint)(slash - path);
	if (len == 0U){
		mui_filedialog_copy(out_dir, dir_cap, "/");
	}else{
		if (len >= dir_cap){ len = dir_cap - 1U; }
		memcpy(out_dir, path, len);
		out_dir[len] = 0;
	}
	mui_filedialog_copy(out_name, name_cap, slash + 1);
}

static void mui_filedialog_join(char* out, auint cap, char const* a, char const* b)
{
	size_t len;
	if ((out == NULL) || (cap == 0U)){ return; }
	if ((a == NULL) || (a[0] == 0) || (strcmp(a, ".") == 0)){
		mui_filedialog_copy(out, cap, b);
		return;
	}
	mui_filedialog_copy(out, cap, a);
	len = strlen(out);
	if ((len != 0U) && (out[len - 1U] != '/') && (out[len - 1U] != '\\')){
		if (len < (cap - 1U)){
			out[len] = '/';
			out[len + 1U] = 0;
		}
	}
	strncat(out, (b != NULL) ? b : "", cap - strlen(out) - 1U);
}

static char const* mui_filedialog_effective_selection(mui_filedialog_t const* dlg)
{
	if (dlg == NULL){ return ""; }
	if (dlg->selected_path[0] != 0){ return dlg->selected_path; }
	if (dlg->last_click_path[0] != 0){ return dlg->last_click_path; }
	return "";
}

static boole mui_filedialog_is_dir(char const* dir, char const* name)
{
	char         path[MUI_FILEDIALOG_PATH_CAP];
	struct stat  st;
	mui_filedialog_join(path, sizeof(path), dir, name);
	if (stat(path, &st) != 0){ return FALSE; }
	return S_ISDIR(st.st_mode) ? TRUE : FALSE;
}

static boole mui_filedialog_match_exts(char const* name, char const* exts)
{
	char        extbuf[MUI_FILEDIALOG_FILTER_CAP];
	char*       tok;
	char const* ext;
	if ((exts == NULL) || (exts[0] == 0) || (strcmp(exts, "*") == 0)){ return TRUE; }
	ext = strrchr(name, '.');
	if (ext == NULL){ return FALSE; }
	mui_filedialog_copy(extbuf, sizeof(extbuf), exts);
	for (tok = strtok(extbuf, ";,"); tok != NULL; tok = strtok(NULL, ";,")){
		while ((*tok != 0) && isspace((unsigned char)*tok)){ tok++; }
		if (*tok == 0){ continue; }
		if (tok[0] != '.'){
			if (mui_filedialog_casecmp(ext, ".") == 0){ continue; }
			if (mui_filedialog_casecmp(ext + 1, tok) == 0){ return TRUE; }
		}else if (mui_filedialog_casecmp(ext, tok) == 0){
			return TRUE;
		}
	}
	return FALSE;
}

static int mui_filedialog_entrycmp(void const* a, void const* b)
{
	mui_filedialog_entry_t const* ea = (mui_filedialog_entry_t const*)a;
	mui_filedialog_entry_t const* eb = (mui_filedialog_entry_t const*)b;
	if (ea->is_dir != eb->is_dir){
		return ea->is_dir ? -1 : 1;
	}
	return mui_filedialog_casecmp(ea->name, eb->name);
}

static void mui_filedialog_reload(mui_filedialog_t* dlg)
{
	DIR*        dir;
	struct dirent* ent;
	if (dlg == NULL){ return; }
	free(dlg->entries);
	dlg->entries = NULL;
	dlg->entry_count = 0;
	dir = opendir((dlg->cwd[0] != 0) ? dlg->cwd : ".");
	if (dir == NULL){
		dlg->dirty = FALSE;
		return;
	}
	while ((ent = readdir(dir)) != NULL){
		mui_filedialog_entry_t* tmp;
		boole is_dir;
		if (strcmp(ent->d_name, ".") == 0){ continue; }
		is_dir = mui_filedialog_is_dir(dlg->cwd, ent->d_name);
		if (dlg->dirs_only){ if (!is_dir){ continue; } }
		else if ((!is_dir) && (!mui_filedialog_match_exts(ent->d_name, dlg->exts))){ continue; }
		tmp = (mui_filedialog_entry_t*)realloc(dlg->entries, (size_t)(dlg->entry_count + 1) * sizeof(mui_filedialog_entry_t));
		if (tmp == NULL){ break; }
		dlg->entries = tmp;
		mui_filedialog_copy(dlg->entries[dlg->entry_count].name, sizeof(dlg->entries[dlg->entry_count].name), ent->d_name);
		dlg->entries[dlg->entry_count].is_dir = is_dir;
		dlg->entry_count++;
	}
	closedir(dir);
	if (dlg->entries != NULL){
		qsort(dlg->entries, (size_t)dlg->entry_count, sizeof(mui_filedialog_entry_t), mui_filedialog_entrycmp);
	}
	if (dlg->list_offset < 0){ dlg->list_offset = 0; }
	if (dlg->list_offset > dlg->entry_count){ dlg->list_offset = dlg->entry_count; }
	dlg->dirty = FALSE;
}

void mui_filedialog_init(mui_filedialog_t* dlg)
{
	if (dlg == NULL){ return; }
	memset(dlg, 0, sizeof(*dlg));
	mui_filedialog_copy(dlg->cwd, sizeof(dlg->cwd), ".");
}

void mui_filedialog_destroy(mui_filedialog_t* dlg)
{
	if (dlg == NULL){ return; }
	free(dlg->entries);
	dlg->entries = NULL;
	dlg->entry_count = 0;
	dlg->list_offset = 0;
	dlg->open = FALSE;
	dlg->dirty = FALSE;
}

void mui_filedialog_open(mui_filedialog_t* dlg, char const* title, char const* start_dir, char const* exts)
{
	if (dlg == NULL){ return; }
	mui_filedialog_copy(dlg->title, sizeof(dlg->title), (title != NULL) ? title : "File Selector");
	mui_filedialog_copy(dlg->exts, sizeof(dlg->exts), (exts != NULL) ? exts : "*");
	mui_filedialog_set_dir(dlg, start_dir);
	dlg->selected_path[0] = 0;
	dlg->accepted = FALSE;
	dlg->accepted_path[0] = 0;
	dlg->dirs_only = FALSE;
	dlg->allow_current_dir = FALSE;
	dlg->save_mode = FALSE;
	dlg->input_name[0] = 0;
	dlg->list_offset = 0;
	dlg->last_click_path[0] = 0;
	dlg->open = TRUE;
}

void mui_filedialog_open_dir(mui_filedialog_t* dlg, char const* title, char const* start_dir)
{
	mui_filedialog_open(dlg, title, start_dir, "*");
	if (dlg == NULL){ return; }
	dlg->dirs_only = TRUE;
	dlg->allow_current_dir = TRUE;
	dlg->save_mode = FALSE;
	dlg->input_name[0] = 0;
}



void mui_filedialog_open_save(mui_filedialog_t* dlg, char const* title, char const* start_path, char const* exts)
{
	char start_dir[MUI_FILEDIALOG_PATH_CAP];
	char start_name[MUI_FILEDIALOG_PATH_CAP];
	if (dlg == NULL){ return; }
	mui_filedialog_split_path(start_path, start_dir, sizeof(start_dir), start_name, sizeof(start_name));
	mui_filedialog_copy(dlg->title, sizeof(dlg->title), (title != NULL) ? title : "Save File");
	mui_filedialog_copy(dlg->exts, sizeof(dlg->exts), (exts != NULL) ? exts : "*");
	mui_filedialog_set_dir(dlg, start_dir);
	mui_filedialog_copy(dlg->input_name, sizeof(dlg->input_name), start_name);
	dlg->selected_path[0] = 0;
	dlg->accepted = FALSE;
	dlg->accepted_path[0] = 0;
	dlg->dirs_only = FALSE;
	dlg->allow_current_dir = FALSE;
	dlg->save_mode = TRUE;
	dlg->list_offset = 0;
	dlg->last_click_path[0] = 0;
	dlg->open = TRUE;
}

void mui_filedialog_close(mui_filedialog_t* dlg)
{
	if (dlg == NULL){ return; }
	dlg->open = FALSE;
	dlg->selected_path[0] = 0;
	dlg->input_name[0] = 0;
	dlg->list_offset = 0;
	dlg->save_mode = FALSE;
	dlg->last_click_path[0] = 0;
}

boole mui_filedialog_is_open(mui_filedialog_t const* dlg)
{
	return (dlg != NULL) ? dlg->open : FALSE;
}

char const* mui_filedialog_get_title(mui_filedialog_t const* dlg)
{
	if ((dlg == NULL) || (dlg->title[0] == 0)){ return "File Selector"; }
	return dlg->title;
}

char const* mui_filedialog_get_cwd(mui_filedialog_t const* dlg)
{
	return (dlg != NULL) ? dlg->cwd : "";
}

char const* mui_filedialog_get_selected_path(mui_filedialog_t const* dlg)
{
	return (dlg != NULL) ? dlg->selected_path : "";
}

char const* mui_filedialog_get_accepted_path(mui_filedialog_t const* dlg)
{
	return (dlg != NULL) ? dlg->accepted_path : "";
}

boole mui_filedialog_consume_accept(mui_filedialog_t* dlg, char* out, auint cap)
{
	if ((dlg == NULL) || (!dlg->accepted) || (dlg->accepted_path[0] == 0)){
		if ((out != NULL) && (cap != 0U)){ out[0] = 0; }
		return FALSE;
	}
	if ((out != NULL) && (cap != 0U)){
		mui_filedialog_copy(out, cap, dlg->accepted_path);
	}
	dlg->accepted = FALSE;
	dlg->accepted_path[0] = 0;
	return TRUE;
}

int mui_filedialog_draw(mu_Context* ctx, mui_filedialog_t* dlg, mu_Rect rect)
{
	int  result = 0;
	int  action_close = 0;
	int  i;
	char label[MUI_FILEDIALOG_PATH_CAP + 8];
	char parent[MUI_FILEDIALOG_PATH_CAP];
	char pathbuf[MUI_FILEDIALOG_PATH_CAP];
	char acceptbuf[MUI_FILEDIALOG_PATH_CAP];
	mu_Container* cnt;
	if ((ctx == NULL) || (dlg == NULL) || (!dlg->open)){ return 0; }
	if (dlg->dirty){ mui_filedialog_reload(dlg); }
	cnt = mui_filedialog_find_container(ctx, mui_filedialog_get_title(dlg));
	if ((cnt != NULL) && !(cnt->open)){
		cnt->open = TRUE;
		mu_bring_to_front(ctx, cnt);
	}
	if (mu_begin_window_ex(ctx,
	                       mui_filedialog_get_title(dlg),
	                       rect,
	                       MU_OPT_NORESIZE | MU_OPT_NOSCROLL)){
		cnt = mui_filedialog_find_container(ctx, mui_filedialog_get_title(dlg));
		if (cnt != NULL){ mu_bring_to_front(ctx, cnt); }
		mu_layout_row(ctx, 4, (int[]){ 56, 64, 72, -1 }, 20);
		if (mu_button(ctx, "Up")){
			mui_filedialog_get_parent(dlg->cwd, parent, sizeof(parent));
			mui_filedialog_set_dir(dlg, parent);
		}
		if (mu_button(ctx, "Home")){
			mui_filedialog_set_dir(dlg, ".");
		}
		if (mu_button(ctx, "Refresh")){
			dlg->dirty = TRUE;
		}
		mu_label(ctx, dlg->cwd);
		{
			int visible_rows = ((rect.h - 182) / 20);
			int end_idx;
			if (visible_rows < 6){ visible_rows = 6; }
			if (dlg->list_offset < 0){ dlg->list_offset = 0; }
			if (dlg->list_offset > dlg->entry_count - visible_rows){
				dlg->list_offset = dlg->entry_count - visible_rows;
			}
			if (dlg->list_offset < 0){ dlg->list_offset = 0; }
			mu_layout_row(ctx, 3, (int[]){ 56, 56, -1 }, 22);
			if (mu_button(ctx, "UP")){
				dlg->list_offset -= visible_rows;
				if (dlg->list_offset < 0){ dlg->list_offset = 0; }
			}
			if (mu_button(ctx, "DOWN")){
				dlg->list_offset += visible_rows;
				if (dlg->list_offset > dlg->entry_count - visible_rows){ dlg->list_offset = dlg->entry_count - visible_rows; }
				if (dlg->list_offset < 0){ dlg->list_offset = 0; }
			}
			if (dlg->entry_count > 0){
				snprintf(label, sizeof(label), "%d-%d OF %d", dlg->list_offset + 1, mu_min(dlg->entry_count, dlg->list_offset + visible_rows), dlg->entry_count);
				mu_label(ctx, label);
			}else{
				mu_label(ctx, "0 FILES");
			}
			mu_layout_row(ctx, 1, (int[]){ -1 }, visible_rows * 20);
			mu_begin_panel(ctx, "##files");
			if (dlg->entry_count == 0){
				mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
				mu_label(ctx, "(empty)");
			}else{
				end_idx = dlg->list_offset + visible_rows;
				if (end_idx > dlg->entry_count){ end_idx = dlg->entry_count; }
				for (i = dlg->list_offset; i < end_idx; ++i){
					mui_filedialog_entry_t const* e = &(dlg->entries[i]);
					if (e->is_dir){ snprintf(label, sizeof(label), "[%s]", e->name); }
					else{ snprintf(label, sizeof(label), "%s", e->name); }
					mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
					if (mu_button_ex(ctx, label, 0, 0)){
						if (e->is_dir){
							mui_filedialog_join(pathbuf, sizeof(pathbuf), dlg->cwd, e->name);
							mui_filedialog_set_dir(dlg, pathbuf);
						}else if (!dlg->dirs_only){
							boole same_click;
							mui_filedialog_join(pathbuf, sizeof(pathbuf), dlg->cwd, e->name);
							same_click = (mui_filedialog_casecmp(dlg->selected_path, pathbuf) == 0) ? TRUE : FALSE;
							mui_filedialog_copy(dlg->selected_path, sizeof(dlg->selected_path), pathbuf);
							if (dlg->save_mode){
								mui_filedialog_copy(dlg->input_name, sizeof(dlg->input_name), e->name);
							}
							if ((!dlg->save_mode) && same_click && (mui_filedialog_casecmp(dlg->last_click_path, pathbuf) == 0)){
								mui_filedialog_copy(dlg->accepted_path, sizeof(dlg->accepted_path), pathbuf);
								dlg->accepted = TRUE;
								result = 1;
								action_close = 1;
								dlg->open = FALSE;
							}else{
								mui_filedialog_copy(dlg->last_click_path, sizeof(dlg->last_click_path), pathbuf);
							}
						}
					}
				}
			}
			mu_end_panel(ctx);
		}
		mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
		{
			char const* selected = mui_filedialog_effective_selection(dlg);
			if (selected[0] != 0){
				snprintf(label, sizeof(label), "SELECTED: %s", mui_filedialog_basename(selected));
				mu_label(ctx, label);
			}else if (dlg->dirs_only && dlg->allow_current_dir){
				mu_label(ctx, "SELECTED: CURRENT DIRECTORY");
			}else if (dlg->save_mode && (dlg->input_name[0] != 0)){
				snprintf(label, sizeof(label), "TARGET: %s", dlg->input_name);
				mu_label(ctx, label);
			}else{
				mu_label(ctx, "SELECTED: (NONE)");
			}
		}
		if (dlg->save_mode){
			mu_layout_row(ctx, 2, (int[]){ 88, -1 }, 24);
			mu_label(ctx, "FILE NAME");
			mui_textbox_with_vkbd(ctx, dlg->input_name, (int)sizeof(dlg->input_name), 0);
		}
		mu_layout_row(ctx, 3, (int[]){ 96, 80, -1 }, 24);
		if (mu_button(ctx, dlg->dirs_only ? "CHOOSE" : (dlg->save_mode ? "SAVE" : "LOAD"))){
			if (dlg->dirs_only && dlg->allow_current_dir && dlg->selected_path[0] == 0){
				mui_filedialog_copy(dlg->accepted_path, sizeof(dlg->accepted_path), dlg->cwd);
				dlg->accepted = TRUE;
				result = 1;
				action_close = 1;
				dlg->open = FALSE;
			}else if (dlg->save_mode){
				if (dlg->input_name[0] != 0){
					mui_filedialog_join(acceptbuf, sizeof(acceptbuf), dlg->cwd, dlg->input_name);
					mui_filedialog_copy(dlg->accepted_path, sizeof(dlg->accepted_path), acceptbuf);
					dlg->accepted = TRUE;
					result = 1;
					action_close = 1;
					dlg->open = FALSE;
				}
			}else{
				char const* selected = mui_filedialog_effective_selection(dlg);
				if (selected[0] != 0){
					mui_filedialog_copy(dlg->accepted_path, sizeof(dlg->accepted_path), selected);
					dlg->accepted = TRUE;
					result = 1;
					action_close = 1;
					dlg->open = FALSE;
				}
			}
		}
		if (mu_button(ctx, "CANCEL")){
			result = -1;
			action_close = 1;
			dlg->open = FALSE;
			dlg->selected_path[0] = 0;
			dlg->accepted = FALSE;
			dlg->accepted_path[0] = 0;
		}
		if (dlg->dirs_only){
			mu_label(ctx, "DIRECTORY PICKER");
		}else if (dlg->exts[0] != 0){
			snprintf(label, sizeof(label), "FILTER: %s", dlg->exts);
			mu_label(ctx, label);
		}else{
			mu_label(ctx, "FILTER: *");
		}
		mu_end_window(ctx);
	}
	cnt = mui_filedialog_find_container(ctx, mui_filedialog_get_title(dlg));
	if ((cnt != NULL) && !(cnt->open)){
		dlg->open = FALSE;
		if ((!action_close) && (result <= 0)){
			dlg->selected_path[0] = 0;
			dlg->accepted = FALSE;
			dlg->accepted_path[0] = 0;
		}
	}
	return result;
}
