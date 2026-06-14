#include "boot_platform.h"

#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_hal.h"
#include "stm32g4xx_hal_flash.h"
#include "stm32g4xx_hal_flash_ex.h"
#include "usbd_cdc_acm_if.h"

#include "targets/common/boot_target_common.h"
#include "targets/stm32g431/board_config.h"

#define G431_BOOT_REQUEST_SIGNATURE   0x424F4F54UL
#define G431_BOOT_REQUEST_ADDRESS     (0x20000000UL + 0x7F00UL)

static boot_target_usb_transport_t g_usb_transport;

static bool boot_g431_flash_uses_dual_bank(void)
{
#if defined(FLASH_OPTR_DBANK)
    return (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) != 0U);
#else
    return false;
#endif
}

static uint32_t boot_g431_flash_page_size(void)
{
#if defined(FLASH_OPTR_DBANK) && defined(FLASH_PAGE_SIZE_128_BITS)
    if (!boot_g431_flash_uses_dual_bank())
    {
        return FLASH_PAGE_SIZE_128_BITS;
    }
#endif

    return FLASH_PAGE_SIZE;
}

static uint32_t boot_g431_flash_bank_size(void)
{
    return boot_g431_flash_uses_dual_bank() ? (g_target_config.flash_size / 2U) : g_target_config.flash_size;
}

static uint32_t boot_g431_flash_bank_for_offset(uint32_t offset)
{
#if defined(FLASH_BANK_2)
    return (boot_g431_flash_uses_dual_bank() && (offset >= boot_g431_flash_bank_size())) ? FLASH_BANK_2 : FLASH_BANK_1;
#else
    (void)offset;
    return FLASH_BANK_1;
#endif
}

static uint32_t boot_g431_flash_page_for_offset(uint32_t offset)
{
    const uint32_t bank_size = boot_g431_flash_bank_size();

    if (boot_g431_flash_uses_dual_bank() && (offset >= bank_size))
    {
        offset -= bank_size;
    }

    return offset / boot_g431_flash_page_size();
}

const boot_target_config_t *boot_platform_target(void)
{
    return &g_target_config;
}

uint32_t boot_platform_millis(void)
{
    return HAL_GetTick();
}

bool boot_platform_force_bootloader(void)
{
    volatile uint32_t *request = (volatile uint32_t *)G431_BOOT_REQUEST_ADDRESS;

    if (*request == G431_BOOT_REQUEST_SIGNATURE)
    {
        *request = 0U;
        __DSB();
        __ISB();
        return true;
    }

    return false;
}

void boot_stm32g431_usb_diag_note_rx(uint32_t length)
{
    boot_target_usb_transport_note_rx(&g_usb_transport, length, HAL_GetTick());
}

void boot_stm32g431_usb_diag_note_rx_overflow(void)
{
    boot_target_usb_transport_note_rx_overflow(&g_usb_transport);
}

void boot_stm32g431_usb_diag_note_tx_complete(uint32_t length)
{
    boot_target_usb_transport_note_tx_complete(&g_usb_transport, length, HAL_GetTick());
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    boot_target_usb_transport_send(&g_usb_transport, data, length);
}

void boot_platform_transport_poll(void)
{
    boot_target_usb_transport_poll(&g_usb_transport, CDC_IsBusy, CDC_Transmit, USBD_OK, HAL_GetTick());
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
    return boot_target_usb_transport_get_diag(&g_usb_transport, out_diag);
}

void boot_platform_watchdog_kick(void)
{
#if defined(IWDG)
    IWDG->KR = 0xAAAAU;
#endif
}

void boot_platform_flash_unlock(void)
{
    HAL_FLASH_Unlock();
}

void boot_platform_flash_lock(void)
{
    HAL_FLASH_Lock();
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    uint32_t page_size;
    uint32_t start_offset;
    uint32_t end_offset;
    uint32_t page_offset;

    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }

    if (!boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }

    page_size = boot_g431_flash_page_size();
    start_offset = address - g_target_config.flash_base;
    end_offset = start_offset + length;
    page_offset = (start_offset / page_size) * page_size;
    while (page_offset < end_offset)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t page_error = 0U;

        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.Banks = boot_g431_flash_bank_for_offset(page_offset);
        erase.Page = boot_g431_flash_page_for_offset(page_offset);
        erase.NbPages = 1U;

        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }

        page_offset += page_size;
    }

    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;
    uint64_t word = 0U;

    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }

    if (!boot_target_flash_write_length_is_aligned(&g_target_config, length))
    {
        return BOOT_ERR_ALIGNMENT;
    }

    if (!boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }

    if (!boot_target_flash_write_address_is_aligned(&g_target_config, address))
    {
        return BOOT_ERR_ALIGNMENT;
    }

    while (offset < length)
    {
        memcpy(&word, &data[offset], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + offset, word) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }
        offset += sizeof(word);
    }

    return BOOT_ERR_NONE;
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    void (*app_reset_handler)(void);
    uint32_t app_stack = *(volatile uint32_t *)app_base;
    uint32_t app_reset = *(volatile uint32_t *)(app_base + 4U);

    __disable_irq();

    HAL_DeInit();
    HAL_RCC_DeInit();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (uint32_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = app_base;
    __DSB();
    __ISB();

    __set_MSP(app_stack);
    app_reset_handler = (void (*)(void))app_reset;
    app_reset_handler();

    for (;;)
    {
    }
}

void boot_platform_reboot(void)
{
    NVIC_SystemReset();
}
