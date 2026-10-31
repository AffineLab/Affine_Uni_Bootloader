#ifndef AFFINE_TARGET_STM32H503_H
#define AFFINE_TARGET_STM32H503_H

#include "boot_image.h"
#include "boot_target_ids.h"

static const boot_target_config_t g_target_config = {
    .flash_base = 0x08000000UL,
    .flash_size = 128UL * 1024UL,
    .app_base = 0x08006000UL,
#if defined(AFFINE_BOOT_H503_KEYCHIP_LAYOUT)
    .app_size = 0x0000A000UL,
#else
    .app_size = 0x00016000UL,
#endif
    .metadata_base = 0x0801C000UL,
    .metadata_size = 0x4000UL,
    .sram_base = 0x20000000UL,
    .sram_size = 32UL * 1024UL,
    .erase_size = 0x2000UL,
    .write_size = 16UL,
    .max_chunk_size = 256UL,
    .target_id = BOOT_TARGET_ID_STM32H503,
    .board_id =
#if defined(AFFINE_BOOT_H503_KEYCHIP_LAYOUT)
        BOOT_BOARD_ID_KEYCHIP_H503,
#else
        BOOT_BOARD_ID_LP_H503,
#endif
    .board_revision = 1UL,
    .flash_layout_id =
#if defined(AFFINE_BOOT_H503_KEYCHIP_LAYOUT)
        BOOT_LAYOUT_ID_H503_KEYCHIP
#else
        BOOT_LAYOUT_ID_H503_A
#endif
};

#endif
