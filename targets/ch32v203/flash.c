#include "boot_platform.h"
#include <string.h>
#include "board_config.h"
#include "ch32v20x.h"
#include "usb_hw.h"
#include "flash_hw.h"
#include "targets/common/boot_target_common.h"

/*
 * A halfword is writable only after a successful, verified erase in this boot.
 * Reading E339 is insufficient: an application can also program that value.
 * One bit per mutable halfword costs 2560 bytes for this C8 layout. A failed
 * program consumes its bit too; a fresh erase is required before retrying.
 */
static uint8_t g_writable[(BOOT_CH32_FLASH_SIZE - BOOT_CH32_BOOT_SIZE) / 16U];

static uint32_t halfword_index(uint32_t address)
{
    return (address - g_target_config.app_base) / 2U;
}

const boot_target_config_t *boot_platform_target(void)
{
    return &g_target_config;
}

uint32_t boot_platform_flash_ecc_error_count(void)
{
    return 0U;
}

void boot_platform_flash_unlock(void)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR | FLASH_STATR_PGERR);
}

void boot_platform_flash_lock(void)
{
    FLASH_Lock();
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }
    if (address < g_target_config.app_base || !boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }
    const uint32_t end = address + length;
    address &= ~(g_target_config.erase_size - 1U);
    while (address < end)
    {
        const uint32_t bitmap_offset = halfword_index(address) / 8U;
        const uint32_t bitmap_length = g_target_config.erase_size / 16U;
        memset(g_writable + bitmap_offset, 0, bitmap_length);
        boot_platform_watchdog_kick();
        const uint32_t irq = boot_ch32_irq_save();
        const FLASH_Status status = FLASH_ErasePage(address);
        boot_ch32_irq_restore(irq);
        if (status != FLASH_COMPLETE)
        {
            return BOOT_ERR_FLASH;
        }
        for (uint32_t offset = 0U; offset < g_target_config.erase_size; offset += 2U)
        {
            if (*(volatile uint16_t *)(address + offset) != BOOT_CH32_ERASED_HALFWORD)
            {
                return BOOT_ERR_FLASH;
            }
        }
        /* FF is a RAM eligibility bitmask, not the flash's erased value. */
        memset(g_writable + bitmap_offset, 0xFF, bitmap_length);
        address += g_target_config.erase_size;
    }
    return BOOT_ERR_NONE;
}

static bool flash_has_error(void)
{
    /* SDK FLASH_GetStatus omits the PGERR bit present in ch32v20x.h. */
    return FLASH_GetStatus() != FLASH_COMPLETE ||
           (FLASH->STATR & (FLASH_STATR_PGERR | FLASH_FLAG_WRPRTERR)) != 0U;
}

static boot_error_t program_fast_page(uint32_t address, const uint8_t *data)
{
    _Alignas(4) uint32_t page[BOOT_CH32_FAST_PAGE_SIZE / sizeof(uint32_t)];
    memcpy(page, data, sizeof(page));

    /* Consume every halfword before the operation, including failure paths. */
    memset(g_writable + halfword_index(address) / 8U, 0, sizeof(page) / 16U);
    boot_platform_watchdog_kick();
    const uint32_t irq = boot_ch32_irq_save();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR | FLASH_STATR_PGERR);
    boot_ch32_flash_modekey_write(BOOT_CH32_FLASH_KEY1);
    boot_ch32_flash_modekey_write(BOOT_CH32_FLASH_KEY2);

    bool failed = (FLASH->CTLR & FLASH_CTLR_FAST_LOCK) != 0U;
    if (!failed)
    {
        FLASH_ProgramPage_Fast(address, page);
        failed = flash_has_error();
    }
    /* FLOCK only: do not set normal LOCK while the core owns the transaction. */
    FLASH_Lock_Fast();
    boot_ch32_irq_restore(irq);
    if (failed)
    {
        return BOOT_ERR_FLASH;
    }

    for (uint32_t offset = 0U; offset < sizeof(page); offset += sizeof(uint32_t))
    {
        if (*(volatile uint32_t *)(address + offset) != page[offset / sizeof(uint32_t)])
        {
            return BOOT_ERR_FLASH;
        }
    }
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }
    if (((address | length) & 1U) != 0U)
    {
        return BOOT_ERR_ALIGNMENT;
    }
    if (data == NULL || address < g_target_config.app_base ||
        !boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }
    /* Preflight the complete write before programming any halfword. */
    for (uint32_t offset = 0U; offset < length; offset += 2U)
    {
        const uint32_t index = halfword_index(address + offset);
        if ((g_writable[index / 8U] & (1U << (index % 8U))) == 0U ||
            *(volatile uint16_t *)(address + offset) != BOOT_CH32_ERASED_HALFWORD)
        {
            return BOOT_ERR_FLASH;
        }
    }
    uint32_t offset = 0U;
    while (offset < length)
    {
        const uint32_t current = address + offset;
        /* Metadata/state records retain standard programming, even if large. */
        if ((current % BOOT_CH32_FAST_PAGE_SIZE) == 0U &&
            (length - offset) >= BOOT_CH32_FAST_PAGE_SIZE &&
            current < g_target_config.metadata_base)
        {
            const boot_error_t error = program_fast_page(current, data + offset);
            if (error != BOOT_ERR_NONE)
            {
                return error;
            }
            offset += BOOT_CH32_FAST_PAGE_SIZE;
            continue;
        }

        const uint32_t index = halfword_index(current);
        uint16_t word;
        memcpy(&word, data + offset, sizeof(word));
        g_writable[index / 8U] &= (uint8_t)~(1U << (index % 8U));
        boot_platform_watchdog_kick();
        const uint32_t irq = boot_ch32_irq_save();
        const FLASH_Status status = FLASH_ProgramHalfWord(current, word);
        const bool failed = status != FLASH_COMPLETE || flash_has_error();
        boot_ch32_irq_restore(irq);
        if (failed || *(volatile uint16_t *)current != word)
        {
            return BOOT_ERR_FLASH;
        }
        offset += 2U;
    }
    return BOOT_ERR_NONE;
}
