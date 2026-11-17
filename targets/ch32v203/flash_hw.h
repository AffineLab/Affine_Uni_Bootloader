#ifndef AFFINE_CH32_FLASH_HW_H
#define AFFINE_CH32_FLASH_HW_H

#include <stdint.h>

/* ch32v20x_flash.c: FLASH_Unlock_Fast / FLASH_ROM_WRITE key sequence. */
#define BOOT_CH32_FLASH_KEY1 0x45670123UL
#define BOOT_CH32_FLASH_KEY2 0xCDEF89ABUL
#define BOOT_CH32_FAST_PAGE_SIZE 256U

/* Narrow MODEKEYR register seam; normal KEYR ownership stays with the core. */
void boot_ch32_flash_modekey_write(uint32_t key);

#endif
