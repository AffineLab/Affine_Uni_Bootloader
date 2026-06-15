#ifndef AFFINE_TARGET_STM32F401_H
#define AFFINE_TARGET_STM32F401_H

#include "boot_image.h"
#include "boot_target_ids.h"

static const boot_target_config_t g_target_config = {
    .flash_base = 0x08000000UL,
    .flash_size = 256UL * 1024UL,
    .app_base = 0x08008000UL,
    .metadata_base = 0x0803C000UL,
    .metadata_size = 0x4000UL,
    .sram_base = 0x20000000UL,
    .sram_size = 64UL * 1024UL,
    .erase_size = 0x4000UL,
    .write_size = 4UL,
    .max_chunk_size = 256UL,
    .target_id = BOOT_TARGET_ID_STM32F401,
    .board_id = BOOT_BOARD_ID_F401_REF,
    .board_revision = 1UL,
    .flash_layout_id = BOOT_LAYOUT_ID_F401_A
};

#endif
