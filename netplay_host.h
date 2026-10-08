#ifndef NETPLAY_HOST_H
#define NETPLAY_HOST_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

boole main_get_loaded_rom_info(uint32 *crc32, uint32 *size, char *name, auint name_cap, boole *sendable);
boole main_get_loaded_rom_image(uint8 **data_out, uint32 *size_out, uint32 *crc32_out, char *name, auint name_cap);
boole main_load_rom_from_memory(uint8 const *data, uint32 size, char const *name);

#ifdef __cplusplus
}
#endif

#endif
