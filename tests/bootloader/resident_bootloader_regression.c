#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cu_avr.h"
#include "bootloader_builtin.h"

/* cu_avr_reset() dependencies irrelevant to reset-vector policy. */
void cu_ctr_reset(void){}
void cu_spi_reset(auint cycle){ (void)cycle; }
void cu_esp_reset_pin(uint8 state, auint cycle){ (void)state; (void)cycle; }
void cu_esp_uzebox_modify(auint port, auint val, auint cycle){ (void)port; (void)val; (void)cycle; }

static int hexbyte(char const *p)
{
    char b[3] = { p[0], p[1], 0 };
    return (int)strtol(b, NULL, 16);
}

static int load_hex_boot(char const *path, uint8 *boot)
{
    FILE *f;
    char line[1024];
    uint32_t base = 0;
    memset(boot, 0xFF, CU_BUILTIN_BOOTLOADER_SIZE);
    f = fopen(path, "r");
    if (f == NULL){ return 0; }
    while (fgets(line, sizeof(line), f) != NULL){
        int n, type, i;
        uint32_t addr;
        if (line[0] != ':'){ continue; }
        n = hexbyte(line + 1);
        addr = (uint32_t)((hexbyte(line + 3) << 8) | hexbyte(line + 5));
        type = hexbyte(line + 7);
        if (type == 0){
            for (i = 0; i < n; i++){
                uint32_t a = base + addr + (uint32_t)i;
                if ((a >= CU_BUILTIN_BOOTLOADER_ORIGIN) &&
                    (a < CU_BUILTIN_BOOTLOADER_ORIGIN + CU_BUILTIN_BOOTLOADER_SIZE)){
                    boot[a - CU_BUILTIN_BOOTLOADER_ORIGIN] = (uint8)hexbyte(line + 9 + (i * 2));
                }
            }
        }else if (type == 4){
            base = ((uint32_t)((hexbyte(line + 9) << 8) | hexbyte(line + 11))) << 16;
        }else if (type == 1){
            break;
        }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    cu_state_cpu_t *cpu = cu_avr_get_state();
    uint8 external[CU_BUILTIN_BOOTLOADER_SIZE];

    if ((argc != 2) || (!load_hex_boot(argv[1], external))){
        fprintf(stderr, "usage: %s Bootloader.hex\n", argv[0]);
        return 2;
    }
    if (memcmp(external, cu_builtin_bootloader, CU_BUILTIN_BOOTLOADER_SIZE) != 0){
        fprintf(stderr, "compiled-in bootloader differs from Bootloader.hex\n");
        return 3;
    }

    memset(cpu, 0, sizeof(*cpu));
    cpu->crom[0] = 0x0CU;
    cpu->crom[1] = 0x94U;
    memcpy(&(cpu->crom[CU_BUILTIN_BOOTLOADER_ORIGIN]),
           cu_builtin_bootloader, CU_BUILTIN_BOOTLOADER_SIZE);

    cu_avr_autofuse(TRUE);
    if ((cpu->fuse[1] & 1U) != 0U){
        fprintf(stderr, "BOOTRST disabled despite resident boot priority\n");
        return 4;
    }
    cu_avr_reset();
    if (cpu->pc != 0x7800U){
        fprintf(stderr, "resident reset PC=%04X, expected 7800\n", (unsigned)cpu->pc);
        return 5;
    }

    cu_avr_autofuse(FALSE);
    cu_avr_reset();
    if (cpu->pc != 0x0000U){
        fprintf(stderr, "game-priority reset PC=%04X, expected 0000\n", (unsigned)cpu->pc);
        return 6;
    }

    memset(&(cpu->crom[CU_BUILTIN_BOOTLOADER_ORIGIN]), 0xFF, CU_BUILTIN_BOOTLOADER_SIZE);
    cu_avr_autofuse(TRUE);
    if ((cpu->fuse[1] & 1U) == 0U){
        fprintf(stderr, "erased boot section falsely selected as resident bootloader\n");
        return 7;
    }

    puts("resident bootloader regression: PASS");
    return 0;
}
