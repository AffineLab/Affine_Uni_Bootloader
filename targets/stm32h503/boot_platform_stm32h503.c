#include "boot_platform.h"

#include <string.h>

#include "stm32h5xx.h"
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_flash.h"
#include "stm32h5xx_hal_flash_ex.h"
#include "usbd_cdc_acm_if.h"

#include "targets/common/boot_target_common.h"
#include "targets/stm32h503/board_config.h"

#define H503_BOOT_REQUEST_SIGNATURE   0x424F4F54UL
#define H503_BOOT_REQUEST_ADDRESS     (0x20000000UL + 0x7F00UL)
#define H503_FLASH_BANK_SIZE          (64UL * 1024UL)

static boot_target_usb_transport_t g_usb_transport;

static uint32_t h503_flash_bank(uint32_t address)
{
    const uint32_t offset = address - g_target_config.flash_base;
    return (offset < H503_FLASH_BANK_SIZE) ? FLASH_BANK_1 : FLASH_BANK_2;
}

static uint32_t h503_flash_sector_in_bank(uint32_t address)
{
    const uint32_t offset = address - g_target_config.flash_base;
    return (offset % H503_FLASH_BANK_SIZE) / g_target_config.erase_size;
}

static uint32_t h503_flash_bank_end(uint32_t address)
{
    const uint32_t offset = address - g_target_config.flash_base;
    const uint32_t bank_base_offset = offset - (offset % H503_FLASH_BANK_SIZE);
    return g_target_config.flash_base + bank_base_offset + H503_FLASH_BANK_SIZE;
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
    volatile uint32_t *request = (volatile uint32_t *)H503_BOOT_REQUEST_ADDRESS;

    if (*request == H503_BOOT_REQUEST_SIGNATURE)
    {
        *request = 0U;
        __DSB();
        __ISB();
        return true;
    }

    return false;
}

void boot_stm32h503_usb_diag_note_rx(uint32_t length)
{
    boot_target_usb_transport_note_rx(&g_usb_transport, length, HAL_GetTick());
}

void boot_stm32h503_usb_diag_note_rx_overflow(void)
{
    boot_target_usb_transport_note_rx_overflow(&g_usb_transport);
}

void boot_stm32h503_usb_diag_note_tx_complete(uint32_t length)
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

    while (length > 0U)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_error = 0U;
        const uint32_t bank_end = h503_flash_bank_end(address);
        const uint32_t chunk_len = ((address + length) > bank_end) ? (bank_end - address) : length;
        const uint32_t start_sector = h503_flash_sector_in_bank(address);
        const uint32_t end_sector = h503_flash_sector_in_bank(address + chunk_len - 1U);

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = h503_flash_bank(address);
        erase.Sector = start_sector;
        erase.NbSectors = (end_sector - start_sector) + 1U;

        if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }

        address += chunk_len;
        length -= chunk_len;
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
        uint32_t quadword[4];

        memcpy(quadword, &data[offset], sizeof(quadword));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, address + offset, (uint32_t)quadword) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }
        offset += sizeof(quadword);
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
