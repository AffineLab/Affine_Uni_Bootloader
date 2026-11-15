#ifndef AFFINE_RP2350_BOARD_CONFIG_H
#define AFFINE_RP2350_BOARD_CONFIG_H
#include "boot_image.h"
#include "boot_target_ids.h"
#define BOOT_RP2350_REGION_SIZE 0x8000UL
#define BOOT_RP2350_APP_BASE 0x10008000UL
#define BOOT_RP2350_REQUEST_SCRATCH 0U
#define BOOT_RP2350_REQUEST_MAGIC 0x424F4F54UL
/* All RP2350 identities, including USB AFF1:52B4, are proposals. */
static const boot_target_config_t g_target_config = {
    .flash_base = 0x10000000UL, .flash_size = 2UL * 1024UL * 1024UL,
    .app_base = BOOT_RP2350_APP_BASE, .app_size = 0x1F6000UL,
    .metadata_base = 0x101FE000UL, .metadata_size = 0x2000UL,
    .sram_base = 0x20000000UL, .sram_size = 520UL * 1024UL,
    .erase_size = 4096UL, .write_size = 4UL, .max_chunk_size = 256UL,
    .target_id = BOOT_TARGET_ID_RP2350, .board_id = BOOT_BOARD_ID_R235_PICO2,
    .board_revision = 1UL, .flash_layout_id = BOOT_LAYOUT_ID_R235_A
};
#endif
