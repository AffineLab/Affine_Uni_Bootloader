#ifndef AFFINE_TARGET_STM32H503_H
#define AFFINE_TARGET_STM32H503_H

#include "boot_image.h"

#define TARGET_ID_STM32H503  0x48353033UL

static const boot_target_config_t g_target_config = {
    .flash_base = 0x08000000UL,
    .flash_size = 128UL * 1024UL,
    .app_base = 0x08006000UL,
    .metadata_base = 0x0801C000UL,
    .metadata_size = 0x4000UL,
    .sram_base = 0x20000000UL,
    .sram_size = 32UL * 1024UL,
    .erase_size = 0x2000UL,
    .write_size = 16UL,
    .max_chunk_size = 256UL,
    .target_id = TARGET_ID_STM32H503
};

#endif
