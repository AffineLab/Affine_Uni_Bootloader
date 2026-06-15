#include "boot_platform.h"

#include <string.h>

#include "stm32f0xx.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_flash.h"
#include "stm32f0xx_hal_flash_ex.h"
#include "usbd_cdc_acm_if.h"

#include "targets/common/boot_target_common.h"
#include "targets/stm32f072/board_config.h"

#define F072_BOOT_REQUEST_SIGNATURE   0x424F4F54UL
#define F072_BOOT_REQUEST_ADDRESS     (0x20000000UL + 0x3F00UL)
#define F072_VECTOR_TABLE_WORDS       48U

static boot_target_usb_transport_t g_usb_transport;

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
    volatile uint32_t *request = (volatile uint32_t *)F072_BOOT_REQUEST_ADDRESS;

    if (*request == F072_BOOT_REQUEST_SIGNATURE)
    {
        *request = 0U;
        __DSB();
        __ISB();
        return true;
    }

    return false;
}

void boot_stm32f072_usb_diag_note_rx(uint32_t length)
{
    boot_target_usb_transport_note_rx(&g_usb_transport, length, HAL_GetTick());
}

void boot_stm32f072_usb_diag_note_rx_overflow(void)
{
    boot_target_usb_transport_note_rx_overflow(&g_usb_transport);
}

void boot_stm32f072_usb_diag_note_tx_complete(uint32_t length)
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
    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }

    if (!boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }

    const uint32_t erase_start = address - ((address - g_target_config.flash_base) % g_target_config.erase_size);
    const uint32_t erase_end = address + length;
    for (uint32_t page_address = erase_start; page_address < erase_end; page_address += g_target_config.erase_size)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t page_error = 0U;

        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = page_address;
        erase.NbPages = 1U;

        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }
    }

    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

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
        uint16_t halfword;

        memcpy(&halfword, &data[offset], sizeof(halfword));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + offset, halfword) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }
        offset += sizeof(halfword);
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

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    for (uint32_t i = 0U; i < F072_VECTOR_TABLE_WORDS; ++i)
    {
        ((volatile uint32_t *)g_target_config.sram_base)[i] = *(volatile uint32_t *)(app_base + (i * sizeof(uint32_t)));
    }
    __HAL_SYSCFG_REMAPMEMORY_SRAM();
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
