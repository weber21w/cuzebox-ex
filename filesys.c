/*
 *  Filesystem interface layer
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
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "filesys.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <io.h>
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif



/* Size of name in the channel state structure */
#define CH_NSIZE     1280U
/* Size of path */
#define PATH_SIZE    1024U


/* Channel state structure */
typedef struct{
 char   name[CH_NSIZE]; /* The file name opened including base path. Used for re-opening */
 FILE*  fp;       /* Opened file if any */
 boole  rd;       /* If TRUE, file is open for reading, otherwise it is closed */
 boole  wr;       /* If TRUE, file is also open for writes (rd must be TRUE too) */
 auint  pos;      /* Position within the file (virtual) */
 uint64_t written; /* Bytes in this write burst; report once on close. */
 boole created;
}filesys_ch_t;


/* There are compiler problems about the uniform zero initializer ({0}) when
** they are used for more complex things. So the initializer below is a hack,
** it is meant to zero initialize everything. But it relies on FILESYS_CH_NO's
** size, so check here */
#if (FILESYS_CH_NO != 3U)
#error "Check filesys_ch's initializer! FILESYS_CH_NO changed!"
#endif


/* Base path of files */
static char filesys_path[PATH_SIZE] = {0U};

/* Channels */
static filesys_ch_t filesys_ch[FILESYS_CH_NO] = {
 { {0U}, NULL, FALSE, FALSE, 0U, 0U, FALSE},
 { {0U}, NULL, FALSE, FALSE, 0U, 0U, FALSE},
 { {0U}, NULL, FALSE, FALSE, 0U, 0U, FALSE},
};

/* File finder's opened directory */
static DIR* filesys_dir = NULL;
static char filesys_find_path[CH_NSIZE] = {0U};

/*
** Extra SD-card write permission. Keep this FALSE by default so emulated
** software can update bytes within existing files, but cannot create, resize,
** rename, or delete them unless the user explicitly opts in.
*/
static boole filesys_sd_allow_new_files = FALSE;

static void filesys_addpath(char* dest, const char* src, auint len);

/* A trusted guest may alter the SD directory, but it must not be able to
** escape it through a crafted name or a host-side symlink / reparse point.
** 4 GiB is the absolute host-side aggregate cap requested for writable SD
** content. The current virtual FAT16 geometry is smaller (~2 GiB), so this
** is primarily a safety backstop and future-proofing limit. */
#define FILESYS_SD_TOTAL_CAP (UINT64_C(4) * UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024))
#define FILESYS_SD_SCAN_DEPTH 64U

static boole filesys_sd_name_safe(char const* name)
{
 auint i = 0U;
 auint seg = 0U;
 if ((name == NULL) || (name[0] == 0)){ return FALSE; }
 /* Only canonical relative paths are accepted. Backslash is deliberately not
 ** treated as a separator so a state has the same meaning on POSIX/Windows. */
 if ((name[0] == '/') || (name[0] == '\\')){ return FALSE; }
 while (name[i] != 0){
  unsigned char c = (unsigned char)name[i];
  if ((c == '\\') || (c == ':') || (c < 0x20U)){ return FALSE; }
  if (c == '/'){
   if (seg == 0U){ return FALSE; }
   if ((seg == 1U) && (name[i - 1U] == '.')){ return FALSE; }
   if ((seg == 2U) && (name[i - 2U] == '.') && (name[i - 1U] == '.')){ return FALSE; }
   seg = 0U;
  }else{
   seg ++;
  }
  i ++;
 }
 if (seg == 0U){ return FALSE; }
 if ((seg == 1U) && (name[i - 1U] == '.')){ return FALSE; }
 if ((seg == 2U) && (name[i - 2U] == '.') && (name[i - 1U] == '.')){ return FALSE; }
 return TRUE;
}

static boole filesys_path_is_redirect(char const* path)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 DWORD attr = GetFileAttributesA(path);
 if (attr == INVALID_FILE_ATTRIBUTES){ return FALSE; }
 return ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) ? TRUE : FALSE;
#else
 struct stat st;
 if (lstat(path, &st) < 0){ return FALSE; }
 return S_ISLNK(st.st_mode) ? TRUE : FALSE;
#endif
}

static void filesys_base_dir(char* dest, auint len);

/* Build an SD path and reject any symlink/reparse-point component below the
** user-selected SD root. The root itself may intentionally be a symlink; it
** is the trust boundary selected by the user. */
static boole filesys_sd_makepath(char* dest, char const* name)
{
 char cur[CH_NSIZE];
 char base[CH_NSIZE];
 struct stat st;
 auint bi;
 auint ni = 0U;
 auint ci;
 if (!filesys_sd_name_safe(name)){ return FALSE; }
 filesys_addpath(dest, name, CH_NSIZE);
 filesys_base_dir(base, CH_NSIZE);
 strncpy(cur, base, sizeof(cur) - 1U);
 cur[sizeof(cur) - 1U] = 0;
 ci = (auint)strlen(cur);
 while (name[ni] != 0){
  auint ss = ni;
  while ((name[ni] != 0) && (name[ni] != '/')){ ni++; }
  if ((ci != 0U) && (cur[ci - 1U] != '/') && (cur[ci - 1U] != '\\')){
   if (ci + 1U >= sizeof(cur)){ return FALSE; }
   cur[ci++] = '/';
  }
  bi = ss;
  while (bi < ni){
   if (ci + 1U >= sizeof(cur)){ return FALSE; }
   cur[ci++] = name[bi++];
  }
  cur[ci] = 0;
  if (filesys_path_is_redirect(cur)){ return FALSE; }
  if (name[ni] == '/'){
   /* Existing intermediate components must really be directories. Missing
   ** ones are left to the operation itself to reject/create as appropriate. */
   if ((stat(cur, &st) == 0) && (!S_ISDIR(st.st_mode))){ return FALSE; }
   ni++;
  }
 }
 return TRUE;
}

static uint64_t filesys_tree_bytes(char const* path, char const* exclude, auint depth)
{
 DIR* dir;
 struct dirent const* ds;
 struct stat st;
 char child[CH_NSIZE];
 size_t plen;
 size_t cpos;
 size_t nlen;
 uint64_t total = 0U;
 uint64_t part;

 if (depth > FILESYS_SD_SCAN_DEPTH){ return FILESYS_SD_TOTAL_CAP + 1U; }
 dir = opendir(path);
 if (dir == NULL){ return 0U; }
 plen = strlen(path);
 while ((ds = readdir(dir)) != NULL){
  if ((strcmp(ds->d_name, ".") == 0) || (strcmp(ds->d_name, "..") == 0)){ continue; }
  nlen = strlen(ds->d_name);
  if ((plen + nlen + 2U) > sizeof(child)){ total = FILESYS_SD_TOTAL_CAP + 1U; break; }
  memcpy(child, path, plen);
  cpos = plen;
  if ((cpos != 0U) && (path[cpos - 1U] != '/') && (path[cpos - 1U] != '\\')){
   child[cpos++] = '/';
  }
  memcpy(&(child[cpos]), ds->d_name, nlen + 1U);
  if ((exclude != NULL) && (strcmp(child, exclude) == 0)){ continue; }
  if (filesys_path_is_redirect(child)){ continue; }
  if (stat(child, &st) < 0){ continue; }
  if (S_ISDIR(st.st_mode)){
   part = filesys_tree_bytes(child, exclude, depth + 1U);
  }else if (st.st_size >= 0){
   part = (uint64_t)st.st_size;
  }else{
   part = 0U;
  }
  if ((part > FILESYS_SD_TOTAL_CAP) || (total > (FILESYS_SD_TOTAL_CAP - part))){
   total = FILESYS_SD_TOTAL_CAP + 1U;
   break;
  }
  total += part;
 }
 closedir(dir);
 return total;
}

static void filesys_base_dir(char* dest, auint len)
{
 auint i = 0U;
 if (len == 0U){ return; }
 while ((i < (len - 1U)) && (filesys_path[i] != 0)){
  dest[i] = filesys_path[i];
  i ++;
 }
 if (i == 0U){ dest[i++] = '.'; }
 while ((i > 1U) && ((dest[i - 1U] == '/') || (dest[i - 1U] == '\\'))){ i --; }
 dest[i] = 0;
}

static boole filesys_sd_size_allowed(char const* fullpath, uint64_t newsize)
{
 char base[PATH_SIZE];
 uint64_t other;
 if (newsize > FILESYS_SD_TOTAL_CAP){ return FALSE; }
 filesys_base_dir(base, PATH_SIZE);
 other = filesys_tree_bytes(base, fullpath, 0U);
 if (other > FILESYS_SD_TOTAL_CAP){ return FALSE; }
 return (newsize <= (FILESYS_SD_TOTAL_CAP - other)) ? TRUE : FALSE;
}


/*
** Returns the current size of a channel's host file, or 0xFFFFFFFFU if it
** does not exist, is a directory, or cannot be represented by auint.
*/
static auint filesys_channel_size(auint ch)
{
 struct stat st;

 if (stat(&(filesys_ch[ch].name[0]), &st) < 0){ return 0xFFFFFFFFU; }
 if (S_ISDIR(st.st_mode)){ return 0xFFFFFFFFU; }
 if (st.st_size < 0){ return 0xFFFFFFFFU; }
 if (((st.st_size >> 16) >> 16) != 0){ return 0xFFFFFFFFU; }

 return (auint)(st.st_size);
}



/*
** Combines the set path with the passed filename.
*/
static void filesys_addpath(char* dest, const char* src, auint len)
{
 auint i = 0U;
 auint j = 0U;

 if (len == 0){ return; } /* No sense to call with len set zero */

 while ( (i < (len - 1U)) &&
         (filesys_path[i] != 0) ){
  dest[i] = filesys_path[i];
  i ++;
 }

 while ( (i < (len - 1U)) &&
         (src[j] != 0) ){
  dest[i] = src[j];
  i ++;
  j ++;
 }

 dest[i] = 0;

 return;
}



/*
** Sets path string. All filesystem operations will be carried out on the
** set path. If there is a file name on the end of the path, it is removed
** from it (bare paths must terminate with a '/'). If name is non-null, the
** filename part of the path (if any) is loaded into it (up to len bytes).
*/
void  filesys_setpath(char const* path, char* name, auint len)
{
 auint i = 0U;
 auint j = 0U;

 /* Copy along with receiving string length (into 'i') */

 while ( (i < (PATH_SIZE - 1U)) &&
         (path[i] != 0) ){
  filesys_path[i] = path[i];
  i ++;
 }
 filesys_path[i] = 0;

 /* Walk back to last directory seperator (if any) */

 while (i != 0U){
  i --;
  if ( (filesys_path[i] ==  '/') ||
       (filesys_path[i] == '\\') ){
   i ++;
   break;
  }
 }
 filesys_path[i] = 0; /* Trim everything after last path separator */

 /* If returning the name was requested, do it */

 if ( (name != NULL) &&
      (len != 0U) ){
  while ( (j < (len - 1U)) &&
          (path[i] != 0) ){
   name[j] = path[i];
   i ++;
   j ++;
  }
  name[j] = 0;
 }

 return;
}



/*
** Opens a file. Position is at the beginning. Returns TRUE on success.
** Initially the file internally is opened for reading (so write protected
** files may also be opened), adding write access is only attempted when
** trying to write the file. If there is already a file open, it is closed
** first.
*/
boole filesys_open(auint ch, char const* name)
{
 /* Clean up previously open file if any */

 filesys_flush(ch);

 /* Open new file (or attempt it) */

 if (ch == FILESYS_CH_SD){
  if (!filesys_sd_makepath(&(filesys_ch[ch].name[0]), name)){
   filesys_ch[ch].name[0] = 0;
   filesys_ch[ch].pos = 0U;
   return FALSE;
  }
 }else{
  filesys_addpath(&(filesys_ch[ch].name[0]), name, CH_NSIZE);
 }
 filesys_ch[ch].fp = fopen(&(filesys_ch[ch].name[0]), "rb");
 if (filesys_ch[ch].fp != NULL){
  filesys_ch[ch].rd = TRUE;
  filesys_ch[ch].wr = FALSE;
 }
 filesys_ch[ch].pos = 0U;

 return filesys_ch[ch].rd;
}



/*
** Read bytes from a file. The reading increases the internal position.
** Returns the number of bytes read, which may be zero if no file is open
** on the channel or it can not be read (such as because the position is at
** or beyond the end). This automatically reopens the last opened file if
** necessary, seeking to the last set position.
*/
auint filesys_read(auint ch, uint8* dest, auint len)
{
 auint rb;

 /* Try to open the file if necessary */

 if (!filesys_ch[ch].rd){
  if ((ch == FILESYS_CH_SD) &&
      ((filesys_ch[ch].name[0] == 0) || filesys_path_is_redirect(&(filesys_ch[ch].name[0])))){
   return 0U;
  }
  filesys_ch[ch].fp = fopen(&(filesys_ch[ch].name[0]), "rb");
  if (filesys_ch[ch].fp != NULL){
   filesys_ch[ch].rd = TRUE;
   filesys_ch[ch].wr = FALSE;
   if (filesys_ch[ch].pos != 0U){
    (void)(fseek(filesys_ch[ch].fp, filesys_ch[ch].pos, SEEK_SET));
   }
  }
 }

 /* Read data */

 if (filesys_ch[ch].rd){
  rb = fread(dest, 1U, len, filesys_ch[ch].fp);
  filesys_ch[ch].pos += rb;
  return rb;
 }

 return 0U;
}

#ifdef ENABLE_DEBUGGER
boole filesys_debug_sd_read(char const* name, auint pos, uint8* dest, auint len)
{
 char path[CH_NSIZE];
 FILE* fp;
 size_t got;
 if ((name == NULL) || (dest == NULL)){ return FALSE; }
 if (len == 0U){ return TRUE; }
 memset(dest, 0, len);
 if (!filesys_sd_makepath(&(path[0]), name)){ return FALSE; }
 fp = fopen(&(path[0]), "rb");
 if (fp == NULL){ return FALSE; }
 if ((pos != 0U) && (fseek(fp, (long)pos, SEEK_SET) != 0)){
  fclose(fp);
  return FALSE;
 }
 got = fread(dest, 1U, len, fp);
 (void)got;
 fclose(fp);
 return TRUE;
}
#endif



/*
** Writes bytes to a file. The writing increases the internal position.
** Returns the number of bytes written, which may be zero if writing is not
** possible for any reason. This automatically reopens the file for writing.
*/
auint filesys_write(auint ch, uint8 const* src, auint len)
{
 auint wb;
 auint fsize;
 asint tr;
 uint8 dum[512];
 boole restrict_sd_growth;

 restrict_sd_growth = ((ch == FILESYS_CH_SD) && (!filesys_sd_allow_new_files));

 /* Try to reopen the file for writing if necessary */

 if (!filesys_ch[ch].wr){
  filesys_flush(ch);
  if ((ch == FILESYS_CH_SD) &&
      ((filesys_ch[ch].name[0] == 0) || filesys_path_is_redirect(&(filesys_ch[ch].name[0])))){
   return 0U;
  }
  filesys_ch[ch].fp = fopen(&(filesys_ch[ch].name[0]), "r+b");
  if ((filesys_ch[ch].fp == NULL) && (!restrict_sd_growth)){ /* Maybe has to create the file */
   if ((ch != FILESYS_CH_SD) || filesys_sd_size_allowed(&(filesys_ch[ch].name[0]), 0U)){
    filesys_ch[ch].fp = fopen(&(filesys_ch[ch].name[0]), "w+b");
    filesys_ch[ch].created = (filesys_ch[ch].fp != NULL);
   }
  }
  if (filesys_ch[ch].fp != NULL){
   filesys_ch[ch].rd = TRUE;
   filesys_ch[ch].wr = TRUE;
   if (filesys_ch[ch].pos != 0U){
    (void)(fseek(filesys_ch[ch].fp, filesys_ch[ch].pos, SEEK_SET));
   }
  }
 }

 if (!filesys_ch[ch].wr){ print_error("FILE OPEN FOR WRITE FAILED: %s\n", filesys_ch[ch].name); return 0U; }

 /*
 ** With the extra permission disabled, SD writes are restricted to the
 ** current extent of an existing host file. Clamp a sector write which
 ** straddles EOF and reject writes starting at or beyond EOF.
 */
 if (restrict_sd_growth){
  fsize = filesys_channel_size(ch);
  if ((fsize == 0xFFFFFFFFU) || (filesys_ch[ch].pos >= fsize)){ return 0U; }
  if (len > (fsize - filesys_ch[ch].pos)){
   len = fsize - filesys_ch[ch].pos;
  }
 }else if ((ch == FILESYS_CH_SD) && filesys_sd_allow_new_files){
  uint64_t endpos = (uint64_t)filesys_ch[ch].pos + (uint64_t)len;
  uint64_t cursize = 0U;
  struct stat st;
  if ((stat(&(filesys_ch[ch].name[0]), &st) == 0) && (st.st_size >= 0)){
   cursize = (uint64_t)st.st_size;
  }
  if ((endpos > cursize) &&
      (!filesys_sd_size_allowed(&(filesys_ch[ch].name[0]), endpos))){ return 0U; }
 }

 /* Check if padding the file with zeros is necessary */

 tr = ftell(filesys_ch[ch].fp);
 if (tr < 0){ return 0U; }
 if ((auint)(tr) < filesys_ch[ch].pos){ /* Can only happen if file is at EOF */
  wb = filesys_ch[ch].pos - (auint)(tr);
  memset(&(dum[0]), 0, 512U);
  while (wb >= 512U){
   (void)(fwrite(&(dum[0]), 1U, 512U, filesys_ch[ch].fp));
   wb -= 512U;
  }
  if (wb != 0U){
   (void)(fwrite(&(dum[0]), 1U,   wb, filesys_ch[ch].fp));
  }
 }

 /* Write data */

 if (filesys_ch[ch].wr){
  wb = fwrite(src, 1U, len, filesys_ch[ch].fp);
  filesys_ch[ch].written += wb;
  filesys_ch[ch].pos += wb;
  return wb;
 }

 return 0U;
}


/*
** Enables or disables trusted full file mutation for files reached through
** the emulated SD-card channel. Other emulator filesystem channels are not
** affected.
*/
void filesys_set_sd_allow_new_files(boole enable)
{
 boole next = enable ? TRUE : FALSE;
 if (filesys_sd_allow_new_files && (!next)){
  filesys_flush(FILESYS_CH_SD);
 }
 filesys_sd_allow_new_files = next;
}


boole filesys_get_sd_allow_new_files(void)
{
 return filesys_sd_allow_new_files;
}


/*
** Applies the logical size recorded by the emulated FAT directory to a host
** file. This is used only while the extra SD write permission is enabled.
*/
boole filesys_resize_sd(char const* name, auint size)
{
 char path[CH_NSIZE];
 struct stat st;
 FILE* fp;
 int rv;

 if (!filesys_sd_allow_new_files){ return FALSE; }
 if (!filesys_sd_makepath(&(path[0]), name)){ return FALSE; }

 /* Commit any buffered data before deciding whether the logical size already
 ** matches. Otherwise a buffered sector write could later grow the file after
 ** this function had incorrectly concluded no resize was needed. */
 filesys_flush(FILESYS_CH_SD);

 if (stat(&(path[0]), &st) == 0){
  if (S_ISDIR(st.st_mode)){ return FALSE; }
  if ((st.st_size >= 0) && (((st.st_size >> 16) >> 16) == 0) && ((auint)st.st_size == size)){
   return TRUE;
  }
  /* Shrinking never increases aggregate SD usage, so permit it even if the
  ** host directory was already over the configured cap before CUzeBox ran. */
  if ((st.st_size < 0) || ((uint64_t)size > (uint64_t)st.st_size)){
   if (!filesys_sd_size_allowed(&(path[0]), (uint64_t)size)){ return FALSE; }
  }
 }else{
  if (!filesys_sd_size_allowed(&(path[0]), (uint64_t)size)){ return FALSE; }
 }

 fp = fopen(&(path[0]), "r+b");
 if (fp == NULL){ fp = fopen(&(path[0]), "w+b"); }
 if (fp == NULL){ return FALSE; }

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 rv = _chsize_s(_fileno(fp), (__int64)size);
#else
 rv = ftruncate(fileno(fp), (off_t)size);
#endif
 if (fclose(fp) != 0){ rv = -1; }
 if (rv == 0){ print_info("SD FILE RESIZED/CREATED: %s (%u bytes)\n", name, (unsigned)size); }
 else{ print_error("SD FILE RESIZE FAILED: %s\n", name); }
 return (rv == 0) ? TRUE : FALSE;
}


boole filesys_delete_sd(char const* name)
{
 char path[CH_NSIZE];
 struct stat st;
 if (!filesys_sd_allow_new_files){ return FALSE; }
 if (!filesys_sd_makepath(&(path[0]), name)){ return FALSE; }
 filesys_flush(FILESYS_CH_SD);
 if (stat(path, &st) < 0){ return TRUE; } /* Already absent is an achieved delete. */
 if (S_ISDIR(st.st_mode)){ return FALSE; }
 if (remove(path) != 0){ print_error("SD FILE DELETE FAILED: %s\n", name); return FALSE; }
 print_info("SD FILE DELETED: %s\n", name);
 return TRUE;
}


boole filesys_rename_sd(char const* oldname, char const* newname)
{
 char oldpath[CH_NSIZE];
 char newpath[CH_NSIZE];
 struct stat st;
 if (!filesys_sd_allow_new_files){ return FALSE; }
 if (!filesys_sd_makepath(&(oldpath[0]), oldname)){ return FALSE; }
 if (!filesys_sd_makepath(&(newpath[0]), newname)){ return FALSE; }
 if (strcmp(oldpath, newpath) == 0){ return TRUE; }
 filesys_flush(FILESYS_CH_SD);
 if (stat(oldpath, &st) < 0){ return FALSE; }
 /* FAT rename does not replace an existing directory entry. Keep the host
 ** behavior deterministic across POSIX (where rename may replace) and Win32. */
 if (stat(newpath, &st) == 0){ return FALSE; }
 if (rename(oldpath, newpath) != 0){ print_error("SD FILE RENAME FAILED: %s\n", oldname); return FALSE; }
 print_info("SD FILE RENAMED: %s -> %s\n", oldname, newname);
 return TRUE;
}

boole filesys_mkdir_sd(char const* name)
{
 char path[CH_NSIZE];
 struct stat st;
 int rv;
 if (!filesys_sd_allow_new_files){ return FALSE; }
 if (!filesys_sd_makepath(path, name)){ return FALSE; }
 if (stat(path, &st) == 0){ return S_ISDIR(st.st_mode) ? TRUE : FALSE; }
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 rv = _mkdir(path);
#else
 rv = mkdir(path, 0777);
#endif
 if (rv == 0){ print_info("SD DIRECTORY CREATED: %s\n", name); }
 else{ print_error("SD DIRECTORY CREATE FAILED: %s\n", name); }
 return (rv == 0) ? TRUE : FALSE;
}

boole filesys_rmdir_sd(char const* name)
{
 char path[CH_NSIZE];
 struct stat st;
 int rv;
 if (!filesys_sd_allow_new_files){ return FALSE; }
 if (!filesys_sd_makepath(path, name)){ return FALSE; }
 filesys_flush(FILESYS_CH_SD);
 if (stat(path, &st) < 0){ return TRUE; }
 if (!S_ISDIR(st.st_mode)){ return FALSE; }
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 rv = _rmdir(path);
#else
 rv = rmdir(path);
#endif
 if (rv == 0){ print_info("SD DIRECTORY REMOVED: %s\n", name); }
 else{ print_error("SD DIRECTORY REMOVE FAILED: %s\n", name); }
 return (rv == 0) ? TRUE : FALSE;
}



/*
** Sets file position. It may be set beyond the end of the actual file. This
** case a write access will attempt to pad the file with zeroes until the
** requested position.
*/
void  filesys_setpos(auint ch, auint pos)
{
 filesys_ch[ch].pos = pos;
 if (filesys_ch[ch].rd){
  (void)(fseek(filesys_ch[ch].fp, filesys_ch[ch].pos, SEEK_SET));
 }
}



/*
** Flushes a channel. It internally closes any opened file, safely flushing
** them as needed.
*/
void  filesys_flush(auint ch)
{
 if (filesys_ch[ch].rd){
  boole failed = (ferror(filesys_ch[ch].fp) != 0);
  if (fclose(filesys_ch[ch].fp) != 0){ failed = TRUE; }
  if (failed){ print_error("FILE WRITE/READ FAILED: %s\n", filesys_ch[ch].name); }
  else if (filesys_ch[ch].written != 0U || filesys_ch[ch].created){
   print_info("FILE %s: %s (%" PRIu64 " bytes written)\n",
              filesys_ch[ch].created ? "CREATED" : "WRITTEN",
              filesys_ch[ch].name, filesys_ch[ch].written);
  }
 }
 filesys_ch[ch].written = 0U;
 filesys_ch[ch].created = FALSE;
 filesys_ch[ch].rd  = FALSE;
 filesys_ch[ch].wr  = FALSE;
}



/*
** Flushes all channels. Should be used before exit.
*/
void  filesys_flushall(void)
{
 auint i;
 for (i = 0U; i < FILESYS_CH_NO; i ++){
  filesys_flush(i);
 }
}



/* Open a specific directory below the configured SD root for recursive
** virtual-FAT enumeration. An empty path selects the SD root itself. */
boole filesys_find_dir_reset(char const* relpath)
{
 char base[CH_NSIZE];
 struct stat st;
 if (filesys_dir != NULL){ closedir(filesys_dir); filesys_dir = NULL; }
 filesys_find_path[0] = 0;
 if ((relpath == NULL) || (relpath[0] == 0)){
  filesys_base_dir(base, CH_NSIZE);
 }else{
  if (!filesys_sd_makepath(base, relpath)){ return FALSE; }
 }
 if ((stat(base, &st) < 0) || (!S_ISDIR(st.st_mode))){ return FALSE; }
 filesys_dir = opendir(base);
 if (filesys_dir == NULL){ return FALSE; }
 strncpy(filesys_find_path, base, sizeof(filesys_find_path) - 1U);
 filesys_find_path[sizeof(filesys_find_path) - 1U] = 0;
 return TRUE;
}

/* Return regular files and directories from the currently opened finder.
** Directories report size 0 and set *isdir TRUE. Symlinks/reparse points are
** skipped entirely so recursive enumeration cannot leave the SD trust root. */
auint filesys_find_next_info(char* dest, auint len, boole* isdir)
{
 struct dirent const* ds;
 struct stat st;
 char child[CH_NSIZE];
 size_t plen;
 size_t nlen;
 if (isdir != NULL){ *isdir = FALSE; }
 if ((dest == NULL) || (len == 0U) || (filesys_dir == NULL)){
  if ((dest != NULL) && (len != 0U)){ dest[0] = 0; }
  return 0xFFFFFFFFU;
 }
 plen = strlen(filesys_find_path);
 while ((ds = readdir(filesys_dir)) != NULL){
  auint i;
  if ((strcmp(ds->d_name, ".") == 0) || (strcmp(ds->d_name, "..") == 0)){ continue; }
  if (!filesys_sd_name_safe(ds->d_name)){ continue; }
  nlen = strlen(ds->d_name);
  if ((nlen + 1U) > len){ continue; }
  if ((plen + nlen + 2U) > sizeof(child)){ continue; }
  memcpy(child, filesys_find_path, plen);
  i = (auint)plen;
  if ((i != 0U) && (child[i - 1U] != '/') && (child[i - 1U] != '\\')){ child[i++] = '/'; }
  memcpy(&(child[i]), ds->d_name, nlen + 1U);
  if (filesys_path_is_redirect(child)){ continue; }
  if (stat(child, &st) < 0){ continue; }
  memcpy(dest, ds->d_name, nlen + 1U);
  if (S_ISDIR(st.st_mode)){
   if (isdir != NULL){ *isdir = TRUE; }
   return 0U;
  }
  if (st.st_size < 0){ continue; }
  if (((st.st_size >> 16) >> 16) != 0){ continue; }
  return (auint)st.st_size;
 }
 closedir(filesys_dir);
 filesys_dir = NULL;
 dest[0] = 0;
 return 0xFFFFFFFFU;
}

/*
** Resets file finder to start a new search.
*/
void  filesys_find_reset(void)
{
 (void)filesys_find_dir_reset("");
}




/*
** Finds next file. Returns the found file's size and name in dest. The return
** is 0xFFFFFFFF on the end of the directory. It will skip any directory or
** file whose size is equal or larger than 0xFFFFFFFF bytes or whose name is
** too long to fit in dest (by len). The returned name is relative to the path
** (so it has no path component, only a bare file name).
*/
auint filesys_find_next(char* dest, auint len)
{
 auint siz;
 boole isdir;
 do{
  siz = filesys_find_next_info(dest, len, &isdir);
 }while ((siz != 0xFFFFFFFFU) && isdir);
 return siz;
}




/*
** Terminates a file find operation. It is not necessary to call it if using
** filesys_find_next() until it returns an empty string.
*/
void  filesys_find_end(void)
{
 if (filesys_dir != NULL){
  closedir(filesys_dir);
  filesys_dir = NULL;
 }
}



/*
** Returns the given file's size in bytes. If the file doesn't exist, its size
** is out of the 32 bit range or it is a directory, it returns 0xFFFFFFFF.
*/
auint filesys_getsize(char const* name)
{
 struct stat st;
 char        tstr[CH_NSIZE];

 filesys_addpath(&(tstr[0]), name, CH_NSIZE);

 if (stat(&(tstr[0]), &st) < 0){ return 0xFFFFFFFFU; }

 if (S_ISDIR(st.st_mode)){ return 0xFFFFFFFFU; }

 /*
 ** Note: Don't try to "fix" the code below to make it more meaningful.
 ** Despite that st.st_size represents the size of a file, it has a signed (!)
 ** type, so unexpected things might happen if you try to compare it to
 ** 0xFFFFFFFFU (and possibly cast things around). Assuming the size is always
 ** at least zero (as checked, so yes, the "true" path of that check is
 ** neither unreachable, although might be dead), this is a safe way to do
 ** this comparison.
 */

 if (st.st_size < 0){ return 0xFFFFFFFFU; }
 if (((st.st_size >> 16) >> 16) != 0){ return 0xFFFFFFFFU; }

 return (auint)(st.st_size);
}
