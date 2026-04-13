#ifndef AFFINE_TARGET_STM32G431_H
#define AFFINE_TARGET_STM32G431_H

#include "boot_image.h"

#define TARGET_ID_STM32G431  0x47343331UL

static const boot_target_config_t g_target_config = {
    .flash_base = 0x08000000UL,
    .flash_size = 128UL * 1024UL,
    .app_base = 0x08004000UL,
    .metadata_base = 0x0801F000UL,
    .metadata_size = 0x0800UL,
    .sram_base = 0x20000000UL,
    .sram_size = 32UL * 1024UL,
    .erase_size = 0x0800UL,
    .write_size = 8UL,
    .max_chunk_size = 256UL,
    .target_id = TARGET_ID_STM32G431,
    .target_name = "STM32G431"
};

#endif
