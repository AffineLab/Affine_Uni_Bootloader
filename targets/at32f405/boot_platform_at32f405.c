#include "boot_platform.h"
#include <string.h>
#include "at32f402_405.h"
#include "board_config.h"
#include "usb.h"
#include "targets/common/boot_jump.h"
#include "targets/common/boot_target_common.h"

const boot_target_config_t *boot_platform_target(void)
{
    return &g_target_config;
}

bool boot_platform_force_bootloader(void)
{
    volatile uint32_t *request = (volatile uint32_t *)BOOT_AT32_REQUEST_ADDRESS;
    if (*request != BOOT_AT32_REQUEST_SIGNATURE)
    {
        return false;
    }
    *request = 0U;
    __DSB();
    __ISB();
    return true;
}

void boot_platform_watchdog_kick(void)
{
    WDT->cmd = WDT_CMD_RELOAD;
}

uint32_t boot_platform_flash_ecc_error_count(void)
{
    /* SDK FLASH status has operation/protection flags, no flash ECC. */
    return 0U;
}

void boot_platform_flash_unlock(void)
{
    flash_unlock();
    flash_flag_clear(FLASH_PRGMERR_FLAG | FLASH_EPPERR_FLAG | FLASH_ODF_FLAG);
}

void boot_platform_flash_lock(void)
{
    flash_lock();
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }
    if (!boot_target_flash_range_is_valid(&g_target_config, address, length) ||
        address < g_target_config.app_base)
    {
        return BOOT_ERR_RANGE;
    }
    const uint32_t end = address + length;
    address -= (address - g_target_config.flash_base) % g_target_config.erase_size;
    while (address < end)
    {
        boot_platform_watchdog_kick();
        if (flash_sector_erase(address) != FLASH_OPERATE_DONE)
        {
            return BOOT_ERR_FLASH;
        }
        address += g_target_config.erase_size;
    }
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }
    if (!boot_target_flash_write_length_is_aligned(&g_target_config, length) ||
        !boot_target_flash_write_address_is_aligned(&g_target_config, address))
    {
        return BOOT_ERR_ALIGNMENT;
    }
    if (!boot_target_flash_range_is_valid(&g_target_config, address, length) ||
        address < g_target_config.app_base)
    {
        return BOOT_ERR_RANGE;
    }
    for (uint32_t offset = 0U; offset < length; offset += sizeof(uint32_t))
    {
        uint32_t word;
        memcpy(&word, data + offset, sizeof(word));
        boot_platform_watchdog_kick();
        if (flash_word_program(address + offset, word) != FLASH_OPERATE_DONE)
        {
            return BOOT_ERR_FLASH;
        }
    }
    return BOOT_ERR_NONE;
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    const uint32_t app_stack = *(volatile uint32_t *)app_base;
    const uint32_t app_reset = *(volatile uint32_t *)(app_base + 4U);
    __disable_irq();
    boot_at32_usb_deinit();
    crm_reset();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (uint32_t i = 0U; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    SCB->VTOR = app_base;
    __DSB();
    __ISB();
    boot_jump_with_stack(app_stack, app_reset);
}

void boot_platform_reboot(void)
{
    NVIC_SystemReset();
}
