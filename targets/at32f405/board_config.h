#ifndef AFFINE_TARGET_AT32F405_H
#define AFFINE_TARGET_AT32F405_H

#include "boot_image.h"
#include "boot_target_ids.h"

#define BOOT_AT32_REQUEST_ADDRESS 0x20019700UL
#define BOOT_AT32_REQUEST_SIGNATURE 0x424F4F54UL
/* Proposed reference-board ID and PID: confirm before releasing hardware. */
#define BOOT_AT32_USB_VID 0xAFF1U
#define BOOT_AT32_USB_PID 0x52B3U

static const boot_target_config_t g_target_config = {
    .flash_base = 0x08000000UL,
    .flash_size = 256UL * 1024UL,
    .app_base = 0x08006000UL,
    .app_size = 228UL * 1024UL,
    .metadata_base = 0x0803F000UL,
    .metadata_size = 0x1000UL,
    .sram_base = 0x20000000UL,
    .sram_size = 102UL * 1024UL,
    .erase_size = 0x800UL,
    .write_size = 4UL,
    .max_chunk_size = 256UL,
    .target_id = BOOT_TARGET_ID_AT32F405,
    .board_id = BOOT_BOARD_ID_A405_REF,
    .board_revision = 1UL,
    .flash_layout_id = BOOT_LAYOUT_ID_A405_A
};

#endif
