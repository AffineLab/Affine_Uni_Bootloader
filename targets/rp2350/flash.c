#include "boot_platform.h"
#include <string.h>
#include "board_config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "targets/common/boot_target_common.h"
#include "usb.h"

const boot_target_config_t *boot_platform_target(void) { return &g_target_config; }
void boot_platform_flash_unlock(void) {}
void boot_platform_flash_lock(void) {}
uint32_t boot_platform_flash_ecc_error_count(void) { return 0U; }

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    if (length == 0U) return BOOT_ERR_NONE;
    if (address < g_target_config.app_base ||
        !boot_target_flash_range_is_valid(&g_target_config, address, length)) return BOOT_ERR_RANGE;
    uint32_t end = address + length;
    address &= ~(FLASH_SECTOR_SIZE - 1U);
    for (; address < end; address += FLASH_SECTOR_SIZE)
    {
        boot_platform_watchdog_kick();
        uint32_t mask = save_and_disable_interrupts();
        flash_range_erase(address - g_target_config.flash_base, FLASH_SECTOR_SIZE);
        restore_interrupts(mask);
        /* Process USB events, never recursively feed commands into the core. */
        boot_rp2350_usb_task();
        boot_platform_watchdog_kick();
        const uint32_t *words = (const uint32_t *)(uintptr_t)address;
        for (uint32_t i = 0; i < FLASH_SECTOR_SIZE / sizeof(*words); ++i)
            if (words[i] != UINT32_MAX) return BOOT_ERR_FLASH;
    }
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (length == 0U) return BOOT_ERR_NONE;
    if (!boot_target_flash_write_address_is_aligned(&g_target_config, address) ||
        !boot_target_flash_write_length_is_aligned(&g_target_config, length)) return BOOT_ERR_ALIGNMENT;
    if (data == NULL || address < g_target_config.app_base ||
        !boot_target_flash_range_is_valid(&g_target_config, address, length)) return BOOT_ERR_RANGE;
    while (length)
    {
        uint8_t page[FLASH_PAGE_SIZE];
        uint32_t within = address % FLASH_PAGE_SIZE;
        uint32_t take = FLASH_PAGE_SIZE - within;
        if (take > length) take = length;
        memset(page, 0xFF, sizeof(page));
        memcpy(page + within, data, take);
        boot_platform_watchdog_kick();
        uint32_t mask = save_and_disable_interrupts();
        flash_range_program(address - within - g_target_config.flash_base, page, sizeof(page));
        restore_interrupts(mask);
        if (memcmp((const void *)(uintptr_t)address, data, take) != 0) return BOOT_ERR_FLASH;
        boot_rp2350_usb_task();
        boot_platform_watchdog_kick();
        address += take; data += take; length -= take;
    }
    return BOOT_ERR_NONE;
}
