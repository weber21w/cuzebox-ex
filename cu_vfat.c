/*
 *  Virtual FAT16 filesystem
 *
 *  Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Nested-directory extension (C) 2026 CUzeBox contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include "cu_vfat.h"
#include "filesys.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Disk geometry. One cluster is 64 512-byte sectors (32768 bytes). */
#define VFAT_DATA_SIZE       0xFFEEU
#define VFAT_DATA_END        (VFAT_DATA_SIZE + 2U)
#define VFAT_ROOT_BASE       0x040200U
#define VFAT_ROOT_SECTOR     (VFAT_ROOT_BASE >> 9)
#define VFAT_DATA_SECTOR     (CU_VFAT_SYS_SIZE >> 9)
#define VFAT_SECTORS_CLUSTER 64U
#define VFAT_CLUSTER_BYTES   0x8000U
#define VFAT_NODE_NONE       0xFFFFFFFFU
#define VFAT_PATH_MAX        1024U
#define VFAT_IMPORT_DEPTH    64U

/* Sparse sectors are the backing store for FAT subdirectories and for data
** sectors written before a directory entry links their cluster. Ordinary
** regular-file data remains streamed directly to host files. */
typedef struct{
 auint sector;
 uint8 data[512U];
} vfat_sparse_sector_t;

typedef struct{
 auint entry_sector;   /* Physical sector containing the 32-byte entry */
 auint entry_index;    /* 0..15 within entry_sector */
 auint parent_cluster; /* 0 means FAT16 root directory */
 auint start_cluster;
 uint8 attr;
 char* path;           /* Exact host-relative path mapping */
} vfat_node_t;

typedef struct{
 char name[CU_VFAT_HNAME_SIZE];
 auint size;
 boole isdir;
} vfat_import_ent_t;

static cu_state_vfat_t vfat_state;
static auint vfat_last_node;
static boole vfat_broken;
static auint vfat_lnode;
static uint8 vfat_root_shadow[CU_VFAT_ROOT_SIZE * 32U];
static vfat_sparse_sector_t* vfat_sparse;
static auint vfat_sparse_count;
static auint vfat_sparse_cap;
static vfat_node_t* vfat_nodes;
static auint vfat_node_count;
static auint vfat_node_cap;
static auint vfat_build_cpos;

static const uint8 vfat_data_mbr[] = {
 0x00U, 0x00U, 0x00U,
 'C','U','Z','E','B','O','X',' ',
 0x00U,0x02U, 0x40U, 0x01U,0x00U, 0x02U,
 (CU_VFAT_ROOT_SIZE & 0xFFU), (CU_VFAT_ROOT_SIZE >> 8),
 0x00U,0x00U, 0xF8U, 0x00U,0x01U,
 0x00U,0x00U, 0x00U,0x00U,
 0x00U,0x00U,0x00U,0x00U,
 0xA1U,0xFDU,0x3FU,0x00U,
 0x81U,0x00U,0x29U, 0xEFU,0xBEU,0xADU,0xDEU,
 'U','Z','E','B','O','X','G','A','M','E','S',
 'F','A','T','1','6',' ',' ',' '
};
static const uint8 vfat_data_fat[] = {0xFFU,0xF8U,0xFFU,0xFFU};

static char* vfat_strdup(char const* s)
{
 size_t n;
 char* p;
 if (s == NULL){ return NULL; }
 n = strlen(s) + 1U;
 p = (char*)malloc(n);
 if (p != NULL){ memcpy(p, s, n); }
 return p;
}

static void vfat_runtime_clear(void)
{
 auint i;
 filesys_flush(FILESYS_CH_SD);
 for (i = 0U; i < vfat_node_count; ++i){ free(vfat_nodes[i].path); }
 free(vfat_nodes); vfat_nodes = NULL; vfat_node_count = 0U; vfat_node_cap = 0U;
 free(vfat_sparse); vfat_sparse = NULL; vfat_sparse_count = 0U; vfat_sparse_cap = 0U;
 vfat_last_node = VFAT_NODE_NONE;
 vfat_lnode = VFAT_NODE_NONE;
 vfat_broken = FALSE;
}

static auint vfat_fat_get(auint cl)
{
 if (cl >= 0x10000U){ return 0xFFFFU; }
 return ((auint)vfat_state.sys[0x000200U + (cl << 1)]) |
        ((auint)vfat_state.sys[0x000201U + (cl << 1)] << 8);
}

static void vfat_fat_set(auint cl, auint val)
{
 if (cl >= 0x10000U){ return; }
 vfat_state.sys[0x000200U + (cl << 1)] = val & 0xFFU;
 vfat_state.sys[0x000201U + (cl << 1)] = (val >> 8) & 0xFFU;
 vfat_state.sys[0x020200U + (cl << 1)] = val & 0xFFU;
 vfat_state.sys[0x020201U + (cl << 1)] = (val >> 8) & 0xFFU;
}

static auint vfat_alloc_chain(auint count)
{
 auint first;
 auint i;
 if (count == 0U){ count = 1U; }
 if ((vfat_build_cpos < 2U) || (count > (VFAT_DATA_END - vfat_build_cpos))){ return 0U; }
 first = vfat_build_cpos;
 for (i = 0U; i < count; ++i){
  auint cl = vfat_build_cpos++;
  vfat_fat_set(cl, (i + 1U < count) ? (cl + 1U) : 0xFFFFU);
 }
 return first;
}

static boole vfat_chain_contains(auint start, auint target, auint* chain_index)
{
 auint cl = start;
 auint idx = 0U;
 auint maxch = VFAT_DATA_SIZE;
 while ((cl >= 2U) && (cl < VFAT_DATA_END) && (maxch != 0U)){
  if (cl == target){ if (chain_index != NULL){ *chain_index = idx; } return TRUE; }
  cl = vfat_fat_get(cl);
  idx++;
  maxch--;
 }
 if (maxch == 0U){ vfat_broken = TRUE; }
 return FALSE;
}

static auint vfat_chain_nth(auint start, auint nth)
{
 auint cl = start;
 auint maxch = VFAT_DATA_SIZE;
 while ((nth != 0U) && (cl >= 2U) && (cl < VFAT_DATA_END) && (maxch != 0U)){
  cl = vfat_fat_get(cl); nth--; maxch--;
 }
 if (maxch == 0U){ vfat_broken = TRUE; return 0U; }
 return ((cl >= 2U) && (cl < VFAT_DATA_END)) ? cl : 0U;
}

static auint vfat_sector_cluster(auint sector)
{
 if (sector < VFAT_DATA_SECTOR){ return 0U; }
 return ((sector - VFAT_DATA_SECTOR) / VFAT_SECTORS_CLUSTER) + 2U;
}

static auint vfat_cluster_sector(auint cluster, auint sec_in_cluster)
{
 return VFAT_DATA_SECTOR + ((cluster - 2U) * VFAT_SECTORS_CLUSTER) + sec_in_cluster;
}

static asint vfat_sparse_find(auint sector)
{
 auint lo = 0U, hi = vfat_sparse_count;
 while (lo < hi){
  auint m = lo + ((hi - lo) >> 1);
  if (vfat_sparse[m].sector < sector){ lo = m + 1U; }else{ hi = m; }
 }
 if ((lo < vfat_sparse_count) && (vfat_sparse[lo].sector == sector)){ return (asint)lo; }
 return -((asint)lo) - 1;
}

static vfat_sparse_sector_t* vfat_sparse_get(auint sector, boole create)
{
 asint fi = vfat_sparse_find(sector);
 auint ins;
 if (fi >= 0){ return &vfat_sparse[(auint)fi]; }
 if (!create){ return NULL; }
 ins = (auint)(-fi - 1);
 if (vfat_sparse_count == vfat_sparse_cap){
  auint nc = (vfat_sparse_cap == 0U) ? 16U : (vfat_sparse_cap * 2U);
  vfat_sparse_sector_t* np = (vfat_sparse_sector_t*)realloc(vfat_sparse, (size_t)nc * sizeof(*np));
  if (np == NULL){ return NULL; }
  vfat_sparse = np; vfat_sparse_cap = nc;
 }
 if (ins < vfat_sparse_count){
  memmove(&vfat_sparse[ins + 1U], &vfat_sparse[ins], (size_t)(vfat_sparse_count - ins) * sizeof(*vfat_sparse));
 }
 vfat_sparse_count++;
 vfat_sparse[ins].sector = sector;
 memset(vfat_sparse[ins].data, 0, sizeof(vfat_sparse[ins].data));
 return &vfat_sparse[ins];
}

static void vfat_sparse_remove_index(auint idx)
{
 if (idx >= vfat_sparse_count){ return; }
 if (idx + 1U < vfat_sparse_count){
  memmove(&vfat_sparse[idx], &vfat_sparse[idx + 1U], (size_t)(vfat_sparse_count - idx - 1U) * sizeof(*vfat_sparse));
 }
 vfat_sparse_count--;
}

static boole vfat_nodes_reserve(void)
{
 if (vfat_node_count == vfat_node_cap){
  auint nc = (vfat_node_cap == 0U) ? 32U : (vfat_node_cap * 2U);
  vfat_node_t* np = (vfat_node_t*)realloc(vfat_nodes, (size_t)nc * sizeof(*np));
  if (np == NULL){ return FALSE; }
  vfat_nodes = np; vfat_node_cap = nc;
 }
 return TRUE;
}

static auint vfat_node_add(auint esec, auint eidx, auint parent, auint start, uint8 attr, char const* path)
{
 char* cp;
 if (!vfat_nodes_reserve()){ return VFAT_NODE_NONE; }
 cp = vfat_strdup(path);
 if (cp == NULL){ return VFAT_NODE_NONE; }
 vfat_nodes[vfat_node_count].entry_sector = esec;
 vfat_nodes[vfat_node_count].entry_index = eidx;
 vfat_nodes[vfat_node_count].parent_cluster = parent;
 vfat_nodes[vfat_node_count].start_cluster = start;
 vfat_nodes[vfat_node_count].attr = attr;
 vfat_nodes[vfat_node_count].path = cp;
 return vfat_node_count++;
}

static void vfat_node_remove(auint idx)
{
 if (idx >= vfat_node_count){ return; }
 if (vfat_lnode == idx){ filesys_flush(FILESYS_CH_SD); vfat_lnode = VFAT_NODE_NONE; }
 if (vfat_last_node == idx){ vfat_last_node = VFAT_NODE_NONE; }
 free(vfat_nodes[idx].path);
 if (idx + 1U < vfat_node_count){
  memmove(&vfat_nodes[idx], &vfat_nodes[idx + 1U], (size_t)(vfat_node_count - idx - 1U) * sizeof(*vfat_nodes));
 }
 vfat_node_count--;
 /* Moving the array invalidates cached indices. */
 vfat_last_node = VFAT_NODE_NONE;
 vfat_lnode = VFAT_NODE_NONE;
}

static auint vfat_node_by_entry(auint sector, auint index)
{
 auint i;
 for (i = 0U; i < vfat_node_count; ++i){
  if ((vfat_nodes[i].entry_sector == sector) && (vfat_nodes[i].entry_index == index)){ return i; }
 }
 return VFAT_NODE_NONE;
}

static boole vfat_node_isdir(vfat_node_t const* n){ return ((n->attr & 0x10U) != 0U) ? TRUE : FALSE; }

static auint vfat_node_owner(auint sector, auint* fpos)
{
 auint cl = vfat_sector_cluster(sector);
 auint ci;
 auint i;
 auint off = (sector - VFAT_DATA_SECTOR) % VFAT_SECTORS_CLUSTER;
 if (vfat_broken || (cl < 2U)){ return VFAT_NODE_NONE; }
 if ((vfat_last_node < vfat_node_count) &&
     vfat_chain_contains(vfat_nodes[vfat_last_node].start_cluster, cl, &ci)){
  if (fpos != NULL){ *fpos = (ci * VFAT_CLUSTER_BYTES) + (off * 512U); }
  return vfat_last_node;
 }
 for (i = 0U; i < vfat_node_count; ++i){
  if (vfat_chain_contains(vfat_nodes[i].start_cluster, cl, &ci)){
   vfat_last_node = i;
   if (fpos != NULL){ *fpos = (ci * VFAT_CLUSTER_BYTES) + (off * 512U); }
   return i;
  }
  if (vfat_broken){ return VFAT_NODE_NONE; }
 }
 return VFAT_NODE_NONE;
}

static void vfat_tofat(char const* src, uint8* dest)
{
 auint spos = 0U, dpos = 0U;
 memset(dest, ' ', 11U);
 while ((src[spos] != 0) && (dpos < 11U)){
  char c = src[spos];
  if ((c >= 'a') && (c <= 'z')){ dest[dpos] = (uint8)(c - 'a' + 'A'); }
  else if (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || c=='~' || c=='!' || c=='-' || c=='_'){ dest[dpos] = (uint8)c; }
  else if ((c == '.') && (dpos < 8U)){ dpos = 8U; spos++; continue; }
  else{ dest[dpos] = (uint8)'$'; }
  spos++; dpos++;
  if (dpos == 8U){
   while ((src[spos] != 0) && (src[spos] != '.')){ spos++; }
   if (src[spos] == '.'){ spos++; }
  }
 }
}

static void vfat_tohost(uint8 const* src, char* dest)
{
 auint endbase = 8U, endext = 11U, d = 0U, i;
 while ((endbase != 0U) && (src[endbase - 1U] == ' ')){ endbase--; }
 while ((endext > 8U) && (src[endext - 1U] == ' ')){ endext--; }
 for (i = 0U; i < endbase; ++i){
  uint8 c = src[i];
  if ((c >= 'A') && (c <= 'Z')){ c = (uint8)(c - 'A' + 'a'); }
  if ((c < 0x20U) || (c == '/') || (c == '\\') || (c == ':')){ c = '$'; }
  dest[d++] = (char)c;
 }
 if (endext > 8U){
  dest[d++] = '.';
  for (i = 8U; i < endext; ++i){
   uint8 c = src[i];
   if ((c >= 'A') && (c <= 'Z')){ c = (uint8)(c - 'A' + 'a'); }
   if ((c < 0x20U) || (c == '/') || (c == '\\') || (c == ':')){ c = '$'; }
   dest[d++] = (char)c;
  }
 }
 dest[d] = 0;
}

static auint vfat_entry_kind(uint8 const* e)
{
 uint8 f = e[0], a = e[11];
 if ((f == 0U) || (f == 0xE5U) || (f == 0x2EU) || (a == 0x0FU) || ((a & 0x08U) != 0U)){ return 0U; }
 return ((a & 0x10U) != 0U) ? 2U : 1U;
}
static auint vfat_entry_start(uint8 const* e){ return (auint)e[26] | ((auint)e[27] << 8); }
static auint vfat_entry_size(uint8 const* e){ return (auint)e[28] | ((auint)e[29] << 8) | ((auint)e[30] << 16) | ((auint)e[31] << 24); }

static boole vfat_path_join(char* out, auint cap, char const* parent, char const* leaf)
{
 size_t p = (parent == NULL) ? 0U : strlen(parent);
 size_t l = strlen(leaf);
 if ((p + l + ((p != 0U) ? 1U : 0U) + 1U) > cap){ return FALSE; }
 if (p != 0U){ memcpy(out, parent, p); out[p++] = '/'; }
 memcpy(&out[p], leaf, l + 1U);
 return TRUE;
}

static void vfat_root_hname_set(auint sector, auint index, char const* path)
{
 auint bytepos;
 auint id;
 if ((sector < VFAT_ROOT_SECTOR) || (sector >= VFAT_DATA_SECTOR)){ return; }
 bytepos = (sector << 9) + (index * 32U);
 if ((bytepos < VFAT_ROOT_BASE) || (bytepos >= CU_VFAT_SYS_SIZE)){ return; }
 id = (bytepos - VFAT_ROOT_BASE) >> 5;
 if (id >= CU_VFAT_ROOT_SIZE){ return; }
 if (path == NULL){ vfat_state.hnames[id][0] = 0; return; }
 strncpy(vfat_state.hnames[id], path, CU_VFAT_HNAME_SIZE - 1U);
 vfat_state.hnames[id][CU_VFAT_HNAME_SIZE - 1U] = 0;
}

static void vfat_node_rename_prefix(auint idx, char const* newpath)
{
 char old[VFAT_PATH_MAX];
 size_t olen, nlen;
 auint i;
 char temp[VFAT_PATH_MAX];
 if ((idx >= vfat_node_count) || (newpath == NULL)){ return; }
 strncpy(old, vfat_nodes[idx].path, sizeof(old)-1U); old[sizeof(old)-1U]=0;
 olen = strlen(old); nlen = strlen(newpath);
 free(vfat_nodes[idx].path); vfat_nodes[idx].path = vfat_strdup(newpath);
 if (vfat_nodes[idx].path == NULL){ vfat_nodes[idx].path = vfat_strdup(old); return; }
 if (!vfat_node_isdir(&vfat_nodes[idx])){ return; }
 for (i = 0U; i < vfat_node_count; ++i){
  size_t plen;
  if (i == idx){ continue; }
  plen = strlen(vfat_nodes[i].path);
  if ((plen > olen) && (memcmp(vfat_nodes[i].path, old, olen) == 0) && (vfat_nodes[i].path[olen] == '/')){
   size_t tail = plen - olen;
   if ((nlen + tail + 1U) <= sizeof(temp)){
    memcpy(temp, newpath, nlen);
    memcpy(&temp[nlen], &vfat_nodes[i].path[olen], tail + 1U);
    free(vfat_nodes[i].path); vfat_nodes[i].path = vfat_strdup(temp);
   }
  }
 }
}

static void vfat_remove_subtree_mappings(char const* prefix)
{
 size_t n = strlen(prefix);
 auint i = 0U;
 while (i < vfat_node_count){
  if ((strlen(vfat_nodes[i].path) > n) && (memcmp(vfat_nodes[i].path, prefix, n) == 0) && (vfat_nodes[i].path[n] == '/')){
   vfat_node_remove(i);
  }else{ i++; }
 }
}

static void vfat_write_file_sparse(auint ni)
{
 auint i = 0U;
 if ((ni >= vfat_node_count) || vfat_node_isdir(&vfat_nodes[ni]) || (!filesys_get_sd_allow_new_files())){ return; }
 while (i < vfat_sparse_count){
  auint ci;
  auint cl = vfat_sector_cluster(vfat_sparse[i].sector);
  if (vfat_chain_contains(vfat_nodes[ni].start_cluster, cl, &ci)){
   auint off = ((vfat_sparse[i].sector - VFAT_DATA_SECTOR) % VFAT_SECTORS_CLUSTER) * 512U;
   if (vfat_lnode != ni){ filesys_open(FILESYS_CH_SD, vfat_nodes[ni].path); vfat_lnode = ni; }
   filesys_setpos(FILESYS_CH_SD, ci * VFAT_CLUSTER_BYTES + off);
   if (filesys_write(FILESYS_CH_SD, vfat_sparse[i].data, 512U) == 512U){ vfat_sparse_remove_index(i); continue; }
  }
  i++;
 }
}

static void vfat_discover_directory(char const* path, auint start_cluster, auint depth);

static auint vfat_find_move_candidate(auint start, auint kind, auint esec, auint eidx)
{
 auint i;
 if (start < 2U){ return VFAT_NODE_NONE; }
 for (i = 0U; i < vfat_node_count; ++i){
  if ((vfat_nodes[i].entry_sector == esec) && (vfat_nodes[i].entry_index == eidx)){ continue; }
  if ((vfat_nodes[i].start_cluster == start) && ((vfat_node_isdir(&vfat_nodes[i]) ? 2U : 1U) == kind)){ return i; }
 }
 return VFAT_NODE_NONE;
}

static void vfat_sync_entry(char const* parent_path, auint parent_cluster, auint sector, auint index,
                            uint8 const* olde, uint8 const* newe)
{
 auint ok = vfat_entry_kind(olde), nk = vfat_entry_kind(newe);
 auint ni = vfat_node_by_entry(sector, index);
 auint ns = vfat_entry_start(newe);
 char leaf[16];
 char newpath[VFAT_PATH_MAX];
 boole allow = filesys_get_sd_allow_new_files();

 if (nk != 0U){
  vfat_tohost(newe, leaf);
  if (!vfat_path_join(newpath, sizeof(newpath), parent_path, leaf)){ nk = 0U; }
 }

 if ((ok != 0U) && (nk == 0U)){
  if (ni != VFAT_NODE_NONE){
   char doomed[VFAT_PATH_MAX];
   boole wasdir = vfat_node_isdir(&vfat_nodes[ni]);
   strncpy(doomed, vfat_nodes[ni].path, sizeof(doomed)-1U); doomed[sizeof(doomed)-1U]=0;
   if (allow){ if (wasdir){ (void)filesys_rmdir_sd(doomed); }else{ (void)filesys_delete_sd(doomed); } }
   vfat_root_hname_set(sector, index, NULL);
   vfat_node_remove(ni);
   if (wasdir){ vfat_remove_subtree_mappings(doomed); }
  }else{ vfat_root_hname_set(sector, index, NULL); }
  vfat_lnode = VFAT_NODE_NONE;
  return;
 }
 if (nk == 0U){ vfat_lnode = VFAT_NODE_NONE; return; }

 if (ni == VFAT_NODE_NONE){
  auint mv = vfat_find_move_candidate(ns, nk, sector, index);
  if (mv != VFAT_NODE_NONE){
   char oldpath[VFAT_PATH_MAX];
   strncpy(oldpath, vfat_nodes[mv].path, sizeof(oldpath)-1U); oldpath[sizeof(oldpath)-1U]=0;
   if ((!allow) || filesys_rename_sd(oldpath, newpath)){
    vfat_root_hname_set(vfat_nodes[mv].entry_sector, vfat_nodes[mv].entry_index, NULL);
    vfat_nodes[mv].entry_sector = sector; vfat_nodes[mv].entry_index = index;
    vfat_nodes[mv].parent_cluster = parent_cluster; vfat_nodes[mv].start_cluster = ns; vfat_nodes[mv].attr = newe[11];
    vfat_node_rename_prefix(mv, newpath);
    vfat_root_hname_set(sector, index, vfat_nodes[mv].path);
    ni = mv;
   }
  }
 }
 if (ni == VFAT_NODE_NONE){
  ni = vfat_node_add(sector, index, parent_cluster, ns, newe[11], newpath);
  if (ni == VFAT_NODE_NONE){ return; }
  vfat_root_hname_set(sector, index, newpath);
  if (allow && (nk == 2U)){ (void)filesys_mkdir_sd(newpath); }
  if (nk == 2U){ vfat_discover_directory(newpath, ns, 0U); }
 }else{
  boole wasdir = vfat_node_isdir(&vfat_nodes[ni]);
  if ((ok != nk) && (ok != 0U)){
   char oldpath[VFAT_PATH_MAX];
   strncpy(oldpath, vfat_nodes[ni].path, sizeof(oldpath)-1U); oldpath[sizeof(oldpath)-1U]=0;
   if (allow){ if (wasdir){ (void)filesys_rmdir_sd(oldpath); }else{ (void)filesys_delete_sd(oldpath); } }
   if (wasdir){ vfat_remove_subtree_mappings(oldpath); }
   vfat_node_rename_prefix(ni, newpath);
   if (allow && (nk == 2U)){ (void)filesys_mkdir_sd(newpath); }
  }else if ((ok != 0U) && (memcmp(olde, newe, 11U) != 0) && (strcmp(vfat_nodes[ni].path, newpath) != 0)){
   char oldpath[VFAT_PATH_MAX];
   strncpy(oldpath, vfat_nodes[ni].path, sizeof(oldpath)-1U); oldpath[sizeof(oldpath)-1U]=0;
   if ((!allow) || filesys_rename_sd(oldpath, newpath)){ vfat_node_rename_prefix(ni, newpath); }
  }
  vfat_nodes[ni].parent_cluster = parent_cluster;
  vfat_nodes[ni].start_cluster = ns;
  vfat_nodes[ni].attr = newe[11];
  vfat_root_hname_set(sector, index, vfat_nodes[ni].path);
 }

 vfat_last_node = VFAT_NODE_NONE;
 if (nk == 1U){
  vfat_write_file_sparse(ni);
  if (allow){ (void)filesys_resize_sd(vfat_nodes[ni].path, vfat_entry_size(newe)); }
 }
 /* Metadata helpers may flush the shared SD channel. */
 vfat_lnode = VFAT_NODE_NONE;
}

/* Discover already-written directory entries when a directory cluster was
** initialized before its parent entry was linked (the normal FAT mkdir order).
** This also handles a prebuilt nested subtree without requiring a full-disk RAM
** image. Only sparse sectors that actually exist are inspected. */
static void vfat_discover_directory(char const* path, auint start_cluster, auint depth)
{
 auint cl = start_cluster;
 auint maxch = VFAT_DATA_SIZE;
 uint8 zero[32U];
 if ((depth > VFAT_IMPORT_DEPTH) || (path == NULL) || (start_cluster < 2U)){ return; }
 memset(zero, 0, sizeof(zero));
 while ((cl >= 2U) && (cl < VFAT_DATA_END) && (maxch != 0U)){
  auint si;
  for (si = 0U; si < VFAT_SECTORS_CLUSTER; ++si){
   auint sector = vfat_cluster_sector(cl, si);
   vfat_sparse_sector_t* sp = vfat_sparse_get(sector, FALSE);
   auint ei;
   if (sp == NULL){ continue; }
   for (ei = 0U; ei < 16U; ++ei){
    uint8 const* e = &sp->data[ei * 32U];
    if ((vfat_entry_kind(e) != 0U) && (vfat_node_by_entry(sector, ei) == VFAT_NODE_NONE)){
     vfat_sync_entry(path, start_cluster, sector, ei, zero, e);
    }
   }
  }
  cl = vfat_fat_get(cl);
  maxch--;
 }
 if (maxch == 0U){ vfat_broken = TRUE; }
}

static void vfat_sync_dir_sector(char const* parent_path, auint parent_cluster, auint sector,
                                 uint8 const* oldsec, uint8 const* newsec)
{
 auint i;
 /* Process additions before deletions. FAT rename commonly creates a new
 ** directory entry which points at the same cluster, then deletes the old
 ** entry. If both changes land in one sector write this ordering lets the
 ** move detector transfer the host mapping instead of deleting the file. */
 for (i = 0U; i < 16U; ++i){
  uint8 const* oe = &oldsec[i * 32U];
  uint8 const* ne = &newsec[i * 32U];
  if ((memcmp(oe, ne, 32U) != 0) && (vfat_entry_kind(oe) == 0U) && (vfat_entry_kind(ne) != 0U)){
   vfat_sync_entry(parent_path, parent_cluster, sector, i, oe, ne);
  }
 }
 for (i = 0U; i < 16U; ++i){
  uint8 const* oe = &oldsec[i * 32U];
  uint8 const* ne = &newsec[i * 32U];
  if ((memcmp(oe, ne, 32U) != 0) && (vfat_entry_kind(oe) != 0U) && (vfat_entry_kind(ne) != 0U)){
   vfat_sync_entry(parent_path, parent_cluster, sector, i, oe, ne);
  }
 }
 for (i = 0U; i < 16U; ++i){
  uint8 const* oe = &oldsec[i * 32U];
  uint8 const* ne = &newsec[i * 32U];
  if ((memcmp(oe, ne, 32U) != 0) && (vfat_entry_kind(oe) != 0U) && (vfat_entry_kind(ne) == 0U)){
   vfat_sync_entry(parent_path, parent_cluster, sector, i, oe, ne);
  }
 }
}

static boole vfat_import_collect(char const* rel, vfat_import_ent_t** out, auint* count, auint limit)
{
 vfat_import_ent_t* a = NULL;
 auint n = 0U, cap = 0U;
 char name[CU_VFAT_HNAME_SIZE];
 boole isdir;
 auint sz;
 if (!filesys_find_dir_reset(rel)){ *out = NULL; *count = 0U; return FALSE; }
 while ((sz = filesys_find_next_info(name, sizeof(name), &isdir)) != 0xFFFFFFFFU){
  if (n >= limit){ continue; }
  if (n == cap){
   auint nc = (cap == 0U) ? 16U : cap * 2U;
   vfat_import_ent_t* np = (vfat_import_ent_t*)realloc(a, (size_t)nc * sizeof(*np));
   if (np == NULL){ free(a); filesys_find_end(); return FALSE; }
   a = np; cap = nc;
  }
  strncpy(a[n].name, name, CU_VFAT_HNAME_SIZE-1U); a[n].name[CU_VFAT_HNAME_SIZE-1U]=0;
  a[n].size = sz; a[n].isdir = isdir; n++;
 }
 *out = a; *count = n; return TRUE;
}

static void vfat_make_entry(uint8* e, char const* name, boole isdir, auint start, auint size)
{
 memset(e, 0, 32U); vfat_tofat(name, e); e[11] = isdir ? 0x10U : 0x20U;
 e[26] = start & 0xFFU; e[27] = (start >> 8) & 0xFFU;
 if (!isdir){ e[28]=size&0xFFU; e[29]=(size>>8)&0xFFU; e[30]=(size>>16)&0xFFU; e[31]=(size>>24)&0xFFU; }
}

static boole vfat_dir_entry_location(auint dir_start, auint entry_no, auint* sector, auint* index)
{
 auint per_cluster = VFAT_CLUSTER_BYTES / 32U;
 auint nth = entry_no / per_cluster;
 auint within = entry_no % per_cluster;
 auint cl = vfat_chain_nth(dir_start, nth);
 if (cl == 0U){ return FALSE; }
 *sector = vfat_cluster_sector(cl, (within * 32U) >> 9);
 *index = within & 15U;
 return TRUE;
}

static boole vfat_sparse_set_entry(auint dir_start, auint entry_no, uint8 const* ent, auint* outsec, auint* outidx)
{
 auint sec, idx;
 vfat_sparse_sector_t* sp;
 if (!vfat_dir_entry_location(dir_start, entry_no, &sec, &idx)){ return FALSE; }
 sp = vfat_sparse_get(sec, TRUE); if (sp == NULL){ return FALSE; }
 memcpy(&sp->data[idx * 32U], ent, 32U);
 if (outsec != NULL){ *outsec = sec; } if (outidx != NULL){ *outidx = idx; }
 return TRUE;
}

static boole vfat_import_directory(char const* rel, auint parent_cluster, auint depth, auint* outstart)
{
 vfat_import_ent_t* list = NULL;
 auint count = 0U, i, used = 2U, clusters, start;
 uint8 ent[32U];
 if (depth > VFAT_IMPORT_DEPTH){ return FALSE; }
 if (!vfat_import_collect(rel, &list, &count, 0xFFF0U)){ return FALSE; }
 clusters = ((count + 2U) + 1023U) / 1024U; if (clusters == 0U){ clusters = 1U; }
 start = vfat_alloc_chain(clusters);
 if (start == 0U){ free(list); return FALSE; }
 memset(ent, 0, sizeof(ent)); memset(ent, ' ', 11U); ent[0]='.'; ent[11]=0x10U; ent[26]=start&0xFFU; ent[27]=(start>>8)&0xFFU;
 (void)vfat_sparse_set_entry(start, 0U, ent, NULL, NULL);
 memset(ent, 0, sizeof(ent)); memset(ent, ' ', 11U); ent[0]='.'; ent[1]='.'; ent[11]=0x10U; ent[26]=parent_cluster&0xFFU; ent[27]=(parent_cluster>>8)&0xFFU;
 (void)vfat_sparse_set_entry(start, 1U, ent, NULL, NULL);
 for (i = 0U; i < count; ++i){
  char child[VFAT_PATH_MAX];
  auint cstart, sec, idx;
  if (!vfat_path_join(child, sizeof(child), rel, list[i].name)){ continue; }
  if (list[i].isdir){
   if (!vfat_import_directory(child, start, depth + 1U, &cstart)){ continue; }
  }else{
   auint cc = (list[i].size + VFAT_CLUSTER_BYTES - 1U) / VFAT_CLUSTER_BYTES; if (cc == 0U){ cc = 1U; }
   cstart = vfat_alloc_chain(cc); if (cstart == 0U){ continue; }
  }
  vfat_make_entry(ent, list[i].name, list[i].isdir, cstart, list[i].size);
  if (!vfat_sparse_set_entry(start, used, ent, &sec, &idx)){ continue; }
  (void)vfat_node_add(sec, idx, start, cstart, ent[11], child);
  used++;
 }
 free(list); *outstart = start; return TRUE;
}

static void vfat_rebuild_root_nodes(void)
{
 auint i;
 for (i = 0U; i < CU_VFAT_ROOT_SIZE; ++i){
  uint8 const* e = &vfat_state.sys[VFAT_ROOT_BASE + i * 32U];
  auint k = vfat_entry_kind(e);
  if (k != 0U){
   char leaf[16];
   char const* path = vfat_state.hnames[i];
   if (path[0] == 0){ vfat_tohost(e, leaf); path = leaf; }
   (void)vfat_node_add(VFAT_ROOT_SECTOR + (i >> 4), i & 15U, 0U, vfat_entry_start(e), e[11], path);
  }
 }
}

void cu_vfat_reset(void)
{
 vfat_import_ent_t* list = NULL;
 auint count = 0U, i, slot = 0U;
 uint8 ent[32U];
 vfat_runtime_clear();
 memset(&vfat_state, 0, sizeof(vfat_state));
 memcpy(vfat_state.sys, vfat_data_mbr, sizeof(vfat_data_mbr));
 vfat_state.sys[0x1FEU]=0x55U; vfat_state.sys[0x1FFU]=0xAAU;
 memcpy(&vfat_state.sys[0x200U], vfat_data_fat, sizeof(vfat_data_fat));
 memcpy(&vfat_state.sys[0x20200U], &vfat_state.sys[0x200U], 0x20000U);
 vfat_build_cpos = 2U;
 if (vfat_import_collect("", &list, &count, CU_VFAT_ROOT_SIZE)){
  for (i = 0U; (i < count) && (slot < CU_VFAT_ROOT_SIZE); ++i){
   auint start;
   if (list[i].isdir){
    if (!vfat_import_directory(list[i].name, 0U, 1U, &start)){ continue; }
   }else{
    auint cc = (list[i].size + VFAT_CLUSTER_BYTES - 1U) / VFAT_CLUSTER_BYTES; if (cc == 0U){ cc = 1U; }
    start = vfat_alloc_chain(cc); if (start == 0U){ continue; }
   }
   vfat_make_entry(ent, list[i].name, list[i].isdir, start, list[i].size);
   memcpy(&vfat_state.sys[VFAT_ROOT_BASE + slot * 32U], ent, 32U);
   strncpy(vfat_state.hnames[slot], list[i].name, CU_VFAT_HNAME_SIZE-1U); vfat_state.hnames[slot][CU_VFAT_HNAME_SIZE-1U]=0;
   (void)vfat_node_add(VFAT_ROOT_SECTOR + (slot >> 4), slot & 15U, 0U, start, ent[11], list[i].name);
   slot++;
  }
 }
 free(list);
 memcpy(vfat_root_shadow, &vfat_state.sys[VFAT_ROOT_BASE], sizeof(vfat_root_shadow));
 vfat_state.isacc = FALSE;
 vfat_last_node = VFAT_NODE_NONE; vfat_lnode = VFAT_NODE_NONE; vfat_broken = FALSE;
}

static void vfat_buffer_data_sector(auint sector)
{
 auint fpos;
 auint ni = vfat_node_owner(sector, &fpos);
 vfat_sparse_sector_t* sp;
 memset(vfat_state.rwbuf, 0, sizeof(vfat_state.rwbuf));
 if (ni != VFAT_NODE_NONE){
  if (vfat_node_isdir(&vfat_nodes[ni])){
   sp = vfat_sparse_get(sector, FALSE); if (sp != NULL){ memcpy(vfat_state.rwbuf, sp->data, 512U); }
  }else{
   if (vfat_lnode != ni){ filesys_open(FILESYS_CH_SD, vfat_nodes[ni].path); vfat_lnode = ni; }
   filesys_setpos(FILESYS_CH_SD, fpos); (void)filesys_read(FILESYS_CH_SD, vfat_state.rwbuf, 512U);
  }
 }else{
  sp = vfat_sparse_get(sector, FALSE); if (sp != NULL){ memcpy(vfat_state.rwbuf, sp->data, 512U); }
 }
}

auint cu_vfat_read(auint pos)
{
 vfat_state.isacc = TRUE;
 if (pos < CU_VFAT_SYS_SIZE){ return vfat_state.sys[pos]; }
 if ((pos & 0x1FFU) == 0U){ vfat_buffer_data_sector(pos >> 9); }
 return vfat_state.rwbuf[pos & 0x1FFU];
}

void cu_vfat_write(auint pos, auint data)
{
 vfat_state.isacc = TRUE;
 if (pos < CU_VFAT_SYS_SIZE){
  vfat_state.sys[pos] = data; vfat_broken = FALSE; vfat_last_node = VFAT_NODE_NONE;
  if ((pos >= VFAT_ROOT_BASE) && ((pos & 0x1FFU) == 0x1FFU)){
   auint sec = pos >> 9;
   auint rootoff = (sec << 9) - VFAT_ROOT_BASE;
   uint8 oldsec[512U];
   memcpy(oldsec, &vfat_root_shadow[rootoff], 512U);
   vfat_sync_dir_sector("", 0U, sec, oldsec, &vfat_state.sys[sec << 9]);
   memcpy(&vfat_root_shadow[rootoff], &vfat_state.sys[sec << 9], 512U);
  }
  return;
 }
 if ((pos & 0x1FFU) == 0U){ vfat_buffer_data_sector(pos >> 9); }
 vfat_state.rwbuf[pos & 0x1FFU] = data;
 if ((pos & 0x1FFU) == 0x1FFU){
  auint sector = pos >> 9;
  auint fpos;
  auint ni = vfat_node_owner(sector, &fpos);
  if ((ni != VFAT_NODE_NONE) && vfat_node_isdir(&vfat_nodes[ni])){
   uint8 oldsec[512U];
   vfat_sparse_sector_t* sp = vfat_sparse_get(sector, FALSE);
   if (sp != NULL){ memcpy(oldsec, sp->data, 512U); }else{ memset(oldsec, 0, 512U); }
   vfat_sync_dir_sector(vfat_nodes[ni].path, vfat_nodes[ni].start_cluster, sector, oldsec, vfat_state.rwbuf);
   sp = vfat_sparse_get(sector, TRUE); if (sp != NULL){ memcpy(sp->data, vfat_state.rwbuf, 512U); }
  }else if (ni != VFAT_NODE_NONE){
   if (vfat_lnode != ni){ filesys_open(FILESYS_CH_SD, vfat_nodes[ni].path); vfat_lnode = ni; }
   filesys_setpos(FILESYS_CH_SD, fpos); (void)filesys_write(FILESYS_CH_SD, vfat_state.rwbuf, 512U);
  }else{
   vfat_sparse_sector_t* sp = vfat_sparse_get(sector, TRUE); if (sp != NULL){ memcpy(sp->data, vfat_state.rwbuf, 512U); }
  }
 }
}

cu_state_vfat_t* cu_vfat_get_state(void){ return &vfat_state; }

void cu_vfat_update(void)
{
 vfat_runtime_clear();
 vfat_rebuild_root_nodes();
 memcpy(vfat_root_shadow, &vfat_state.sys[VFAT_ROOT_BASE], sizeof(vfat_root_shadow));
 vfat_broken = FALSE; vfat_last_node = VFAT_NODE_NONE; vfat_lnode = VFAT_NODE_NONE;
}

boole cu_vfat_isaccessed(void){ return vfat_state.isacc; }

#ifdef ENABLE_DEBUGGER
boole cu_vfat_debug_sector_snapshot(auint sector, boole include_backing, cu_vfat_debug_sector_t* out)
{
 auint base;
 auint fpos = 0U;
 auint ni = VFAT_NODE_NONE;
 vfat_sparse_sector_t* sp = NULL;
 if (out == NULL){ return FALSE; }
 memset(out, 0, sizeof(*out));
 out->sector = sector;
 out->role = CU_VFAT_DEBUG_ROLE_UNKNOWN;
 out->next_cluster = 0xFFFFFFFFU;
 base = sector << 9;
 if (base < CU_VFAT_SYS_SIZE){
  auint left = CU_VFAT_SYS_SIZE - base;
  auint n = (left < 512U) ? left : 512U;
  memcpy(out->stream, &vfat_state.sys[base], n);
  out->stream_valid = TRUE;
  if (sector == 0U){
   out->role = CU_VFAT_DEBUG_ROLE_BOOT;
   strncpy(out->source, "VFAT FAT16 boot sector", sizeof(out->source) - 1U);
  }else if (sector <= 0x100U){
   out->role = CU_VFAT_DEBUG_ROLE_FAT1;
   out->fat_copy = 1U;
   out->fat_first_cluster = (sector - 1U) * 256U;
   strncpy(out->source, "VFAT FAT #1", sizeof(out->source) - 1U);
  }else if (sector <= 0x200U){
   out->role = CU_VFAT_DEBUG_ROLE_FAT2;
   out->fat_copy = 2U;
   out->fat_first_cluster = (sector - 0x101U) * 256U;
   strncpy(out->source, "VFAT FAT #2", sizeof(out->source) - 1U);
  }else{
   out->role = CU_VFAT_DEBUG_ROLE_ROOT;
   out->entry_sector = sector;
   out->entry_index = (sector - VFAT_ROOT_SECTOR) * 16U;
   strncpy(out->source, "VFAT root directory", sizeof(out->source) - 1U);
  }
  if (include_backing){ memcpy(out->backing, &vfat_state.sys[base], n); out->backing_valid = TRUE; }
  out->source[sizeof(out->source) - 1U] = 0;
  return TRUE;
 }
 out->cluster = vfat_sector_cluster(sector);
 out->sector_in_cluster = (sector - VFAT_DATA_SECTOR) % VFAT_SECTORS_CLUSTER;
 ni = vfat_node_owner(sector, &fpos);
 out->file_offset = fpos;
 sp = vfat_sparse_get(sector, FALSE);
 if (ni != VFAT_NODE_NONE){
  vfat_node_t const* node = &vfat_nodes[ni];
  out->owner_valid = TRUE;
  out->owner_is_dir = vfat_node_isdir(node);
  out->chain_index = fpos / VFAT_CLUSTER_BYTES;
  out->start_cluster = node->start_cluster;
  out->next_cluster = vfat_fat_get(out->cluster);
  out->entry_sector = node->entry_sector;
  out->entry_index = node->entry_index;
  out->parent_cluster = node->parent_cluster;
  out->attr = node->attr;
  out->role = out->owner_is_dir ? CU_VFAT_DEBUG_ROLE_DIRECTORY : CU_VFAT_DEBUG_ROLE_FILE;
  strncpy(out->source, node->path, sizeof(out->source) - 1U);
  if (out->owner_is_dir){
   if (sp != NULL){ memcpy(out->stream, sp->data, 512U); }
   out->stream_valid = TRUE;
  }
 }else if (sp != NULL){
  out->role = CU_VFAT_DEBUG_ROLE_SPARSE;
  memcpy(out->stream, sp->data, 512U);
  out->stream_valid = TRUE;
  strncpy(out->source, "VFAT sparse sector", sizeof(out->source) - 1U);
 }else{
  out->role = CU_VFAT_DEBUG_ROLE_UNALLOCATED;
  out->stream_valid = TRUE; /* Virtual reads return a zero-filled sector here. */
  strncpy(out->source, "VFAT unallocated sector", sizeof(out->source) - 1U);
 }
 if (include_backing && (ni != VFAT_NODE_NONE)){
  if (vfat_node_isdir(&vfat_nodes[ni])){
   if (sp != NULL){ memcpy(out->backing, sp->data, 512U); }
   out->backing_valid = TRUE;
  }else{
   out->backing_valid = filesys_debug_sd_read(vfat_nodes[ni].path, fpos, out->backing, 512U);
  }
 }else if (include_backing){
  if (sp != NULL){ memcpy(out->backing, sp->data, 512U); }
  out->backing_valid = TRUE;
 }
 out->source[sizeof(out->source) - 1U] = 0;
 return TRUE;
}
#endif

/* Variable-length savestate extension. The fixed cu_state_vfat_t prefix is
** unchanged, so old save states remain loadable. */
#define VFAT_EXT_MAGIC 0x31584656UL /* VFX1 */
#define VFAT_EXT_VERSION 1U

static void vfat_put32(uint8* p, uint32 v){ p[0]=v&0xFFU; p[1]=(v>>8)&0xFFU; p[2]=(v>>16)&0xFFU; p[3]=(v>>24)&0xFFU; }
static uint32 vfat_get32(uint8 const* p){ return (uint32)p[0] | ((uint32)p[1]<<8) | ((uint32)p[2]<<16) | ((uint32)p[3]<<24); }

auint cu_vfat_state_size(void)
{
 uint64_t total = sizeof(vfat_state) + 16U + (uint64_t)vfat_sparse_count * 516U;
 auint i;
 for (i = 0U; i < vfat_node_count; ++i){ total += 24U + strlen(vfat_nodes[i].path); }
 if (total > 0xFFFFFFFFULL){ return 0U; }
 return (auint)total;
}

boole cu_vfat_state_save(uint8* dst, auint size)
{
 auint need = cu_vfat_state_size();
 auint i;
 uint8* p;
 if ((dst == NULL) || (need == 0U) || (size < need)){ return FALSE; }
 memcpy(dst, &vfat_state, sizeof(vfat_state)); p = dst + sizeof(vfat_state);
 vfat_put32(p, VFAT_EXT_MAGIC); p+=4; vfat_put32(p, VFAT_EXT_VERSION); p+=4;
 vfat_put32(p, vfat_sparse_count); p+=4; vfat_put32(p, vfat_node_count); p+=4;
 for (i=0U;i<vfat_sparse_count;++i){ vfat_put32(p,vfat_sparse[i].sector); p+=4; memcpy(p,vfat_sparse[i].data,512U); p+=512U; }
 for (i=0U;i<vfat_node_count;++i){
  auint l=(auint)strlen(vfat_nodes[i].path);
  vfat_put32(p,vfat_nodes[i].entry_sector); p+=4; vfat_put32(p,vfat_nodes[i].entry_index); p+=4;
  vfat_put32(p,vfat_nodes[i].parent_cluster); p+=4; vfat_put32(p,vfat_nodes[i].start_cluster); p+=4;
  vfat_put32(p,vfat_nodes[i].attr); p+=4; vfat_put32(p,l); p+=4; memcpy(p,vfat_nodes[i].path,l); p+=l;
 }
 return TRUE;
}

boole cu_vfat_state_load(uint8 const* src, auint size)
{
 uint8 const* p;
 uint8 const* end;
 auint sc, nc, i;
 if ((src == NULL) || (size < sizeof(vfat_state))){ return FALSE; }
 vfat_runtime_clear(); memcpy(&vfat_state, src, sizeof(vfat_state));
 p=src+sizeof(vfat_state); end=src+size;
 if (p==end){ vfat_rebuild_root_nodes(); memcpy(vfat_root_shadow,&vfat_state.sys[VFAT_ROOT_BASE],sizeof(vfat_root_shadow)); return TRUE; }
 if ((auint)(end-p)<16U){ goto fail; }
 if ((vfat_get32(p)!=VFAT_EXT_MAGIC) || (vfat_get32(p+4)!=VFAT_EXT_VERSION)){ goto fail; }
 sc=(auint)vfat_get32(p+8); nc=(auint)vfat_get32(p+12); p+=16;
 if ((uint64_t)sc*516ULL > (uint64_t)(end-p)){ goto fail; }
 for(i=0U;i<sc;++i){ vfat_sparse_sector_t* sp; auint sec=(auint)vfat_get32(p); p+=4; sp=vfat_sparse_get(sec,TRUE); if(sp==NULL)goto fail; memcpy(sp->data,p,512U); p+=512U; }
 for(i=0U;i<nc;++i){
  auint es,ei,pc,st,at,l; char tmp[VFAT_PATH_MAX];
  if ((auint)(end-p)<24U)goto fail;
  es=(auint)vfat_get32(p); ei=(auint)vfat_get32(p+4); pc=(auint)vfat_get32(p+8); st=(auint)vfat_get32(p+12); at=(auint)vfat_get32(p+16); l=(auint)vfat_get32(p+20); p+=24;
  if ((l == 0U) || (l >= sizeof(tmp)) || ((auint)(end - p) < l)){ goto fail; }
  memcpy(tmp, p, l); tmp[l] = 0; p += l;
  if(vfat_node_add(es,ei,pc,st,(uint8)at,tmp)==VFAT_NODE_NONE)goto fail;
 }
 if (p!=end)goto fail;
 memcpy(vfat_root_shadow,&vfat_state.sys[VFAT_ROOT_BASE],sizeof(vfat_root_shadow)); vfat_broken=FALSE; vfat_last_node=VFAT_NODE_NONE; vfat_lnode=VFAT_NODE_NONE; return TRUE;
fail:
 vfat_runtime_clear(); memset(&vfat_state,0,sizeof(vfat_state)); return FALSE;
}
