#ifndef AFFINE_CH32_BOARD_CONFIG_H
#define AFFINE_CH32_BOARD_CONFIG_H

#include "boot_image.h"
#include "boot_target_ids.h"

/* Provisional C8 layout and board; owner confirmation required. */
#define BOOT_CH32_FLASH_BASE 0x08000000UL
#define BOOT_CH32_FLASH_SIZE (64UL * 1024UL)
#define BOOT_CH32_BOOT_SIZE (24UL * 1024UL)
#define BOOT_CH32_ERASE_SIZE 0x1000UL
#define BOOT_CH32_ERASED_HALFWORD 0xE339U
#define BOOT_CH32_REQUEST_ADDRESS 0x20004F00UL
#define BOOT_CH32_REQUEST_SIGNATURE 0x424F4F54UL
#define BOOT_CH32_USB_VID 0xAFF1U
#define BOOT_CH32_USB_PID 0x52B5U
static const boot_target_config_t g_target_config =
{
    .flash_base = BOOT_CH32_FLASH_BASE,.flash_size = BOOT_CH32_FLASH_SIZE,.app_base = BOOT_CH32_FLASH_BASE + BOOT_CH32_BOOT_SIZE,.app_size = 32UL * 1024UL,.metadata_base = 0x0800E000UL,.metadata_size = 2UL * BOOT_CH32_ERASE_SIZE,.sram_base = 0x20000000UL,.sram_size = 0x4F00UL,.erase_size = BOOT_CH32_ERASE_SIZE,.write_size = 2UL,.max_chunk_size = 256UL,.target_id = BOOT_TARGET_ID_CH32V203,.board_id = BOOT_BOARD_ID_C203_REF,.board_revision = 1UL,.flash_layout_id = BOOT_LAYOUT_ID_C203_A
};

#endif
