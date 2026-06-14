#ifndef AFFINE_TARGET_STM32F072_H
#define AFFINE_TARGET_STM32F072_H

#include "boot_image.h"

#define TARGET_ID_STM32F072  0x46303732UL

static const boot_target_config_t g_target_config = {
    .flash_base = 0x08000000UL,
    .flash_size = 128UL * 1024UL,
    .app_base = 0x08004000UL,
    .metadata_base = 0x0801F800UL,
    .metadata_size = 0x0800UL,
    .sram_base = 0x20000000UL,
    .sram_size = 16UL * 1024UL,
    .erase_size = 0x0800UL,
    .write_size = 2UL,
    .max_chunk_size = 256UL,
    .target_id = TARGET_ID_STM32F072
};

#endif
