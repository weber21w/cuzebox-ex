/* A "uzedisk" folder beside the ROM serves the SD card, writable; without one
** the card is the ROM's own folder, with the user's write setting unchanged. */
#include "filesys.h"
#include "cu_vfat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(char const* path, char const* text)
{
 FILE* f = fopen(path, "wb");
 assert(f);
 assert(fputs(text, f) >= 0);
 assert(fclose(f) == 0);
}

static int exists(char const* path)
{
 struct stat st;
 return stat(path, &st) == 0;
}

/* Is a file of that host name in the card's root directory? */
static int on_card(char const* host)
{
 cu_state_vfat_t* st = cu_vfat_get_state();
 unsigned slot;
 for (slot = 0; slot < CU_VFAT_ROOT_SIZE; ++slot){
  if (!strcmp(st->hnames[slot], host)){ return 1; }
 }
 return 0;
}

int main(void)
{
 char temp[] = "/tmp/cuzebox-uzedisk-XXXXXX";
 char* root = mkdtemp(temp);
 char rom[512], disk[512], path[1024];
 assert(root);
 snprintf(rom, sizeof(rom), "%s/GAME.UZE", root);
 snprintf(disk, sizeof(disk), "%s/uzedisk", root);
 put(rom, "rom");
 snprintf(path, sizeof(path), "%s/OUTSIDE.TXT", root);
 put(path, "outside");

 /* No uzedisk: the ROM's folder is the card, new files refused */
 filesys_set_sd_allow_new_files(0);
 filesys_setpath(rom, NULL, 0);
 cu_vfat_reset();
 assert(on_card("GAME.UZE"));
 assert(on_card("OUTSIDE.TXT"));
 assert(!filesys_get_sd_allow_new_files());

 /* A uzedisk folder: it is the card, and the guest may create files in it */
 assert(mkdir(disk, 0777) == 0);
 snprintf(path, sizeof(path), "%s/INSIDE.TXT", disk);
 put(path, "inside");
 filesys_setpath(rom, NULL, 0);
 cu_vfat_reset();
 assert(on_card("INSIDE.TXT"));
 assert(!on_card("GAME.UZE"));
 assert(!on_card("OUTSIDE.TXT"));
 assert(filesys_get_sd_allow_new_files());
 (void)filesys_open(FILESYS_CH_SD, "NEW.TXT"); /* Not there yet: the write creates it */
 assert(filesys_write(FILESYS_CH_SD, (uint8 const*)"new", 3U) == 3U);
 filesys_flushall();
 snprintf(path, sizeof(path), "%s/NEW.TXT", disk);
 assert(exists(path));
 snprintf(path, sizeof(path), "%s/NEW.TXT", root);
 assert(!exists(path));

 /* The folder gone: back to the ROM's folder and the user's setting */
 snprintf(path, sizeof(path), "%s/NEW.TXT", disk);
 assert(unlink(path) == 0);
 snprintf(path, sizeof(path), "%s/INSIDE.TXT", disk);
 assert(unlink(path) == 0);
 assert(rmdir(disk) == 0);
 filesys_setpath(rom, NULL, 0);
 cu_vfat_reset();
 assert(on_card("GAME.UZE"));
 assert(!filesys_get_sd_allow_new_files());

 filesys_flushall();
 assert(unlink(rom) == 0);
 snprintf(path, sizeof(path), "%s/OUTSIDE.TXT", root);
 assert(unlink(path) == 0);
 assert(rmdir(root) == 0);
 puts("uzedisk_regression: PASS");
 return 0;
}
