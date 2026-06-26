/*
 * G431 bootloader self-update app.
 *
 * Runs from the application slot (0x08006000), launched by the existing
 * bootloader. It carries a new bootloader image embedded in flash and, on
 * boot, reprograms the bootloader region (0x08000000) with it.
 *
 * Safety / single-bank notes (STM32G431):
 *  - The new image is staged into RAM first, so no flash read happens while the
 *    bootloader region (same bank) is being erased/programmed.
 *  - Interrupts are disabled across the erase/program window: the very first
 *    page being erased holds the vector table, so any IRQ during the window
 *    would fetch a vector from erased flash and fault.
 *  - Idempotent: if the bootloader region already matches the embedded image we
 *    do not reprogram; only a fresh, verified reflash requests bootloader entry.
 *  - On any failure the app simply halts; the device is recoverable over the ST
 *    system DFU bootloader.
 */

#include "stm32g4xx_hal.h"

#include <string.h>

#include "v3_bl_image.h"

#define BL_BASE 0x08000000UL

/* Matches targets/stm32g431/boot_platform_stm32g431.c — asking the (new)
 * bootloader to stay in bootloader mode after reset instead of booting the app,
 * so the result is verifiable over USB. */
#define G431_BOOT_REQUEST_SIGNATURE 0x424F4F54UL
#define G431_BOOT_REQUEST_ADDRESS   (0x20000000UL + 0x7F00UL)

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
    }
}

/* 8-byte aligned RAM staging buffer for the whole new bootloader image. */
static uint64_t g_ram_image[(V3_BL_IMAGE_SIZE + 7U) / 8U];

static uint32_t flash_page_size(void)
{
#if defined(FLASH_OPTR_DBANK) && defined(FLASH_PAGE_SIZE_128_BITS)
    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) == 0U)
    {
        return FLASH_PAGE_SIZE_128_BITS;
    }
#endif
    return FLASH_PAGE_SIZE;
}

static int reflash_bootloader(void)
{
    const uint32_t size = V3_BL_IMAGE_SIZE;
    const uint32_t page_size = flash_page_size();
    const uint8_t *src;

    /* Stage in RAM before touching flash (same-bank read-while-write stall). */
    memcpy(g_ram_image, g_v3_bl_image, size);
    src = (const uint8_t *)g_ram_image;

    __disable_irq();
    HAL_FLASH_Unlock();

    for (uint32_t offset = 0U; offset < size; offset += page_size)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t page_error = 0U;

        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.Banks = FLASH_BANK_1; /* bootloader region is at the start of bank 1 */
        erase.Page = offset / page_size;
        erase.NbPages = 1U;

        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
        {
            HAL_FLASH_Lock();
            __enable_irq();
            return -1;
        }
    }

    for (uint32_t offset = 0U; offset < size; offset += 8U)
    {
        uint64_t doubleword;

        memcpy(&doubleword, src + offset, 8U);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, BL_BASE + offset, doubleword) != HAL_OK)
        {
            HAL_FLASH_Lock();
            __enable_irq();
            return -2;
        }
    }

    HAL_FLASH_Lock();
    __enable_irq();
    return 0;
}

static void request_bootloader_and_reset(void)
{
    *(volatile uint32_t *)G431_BOOT_REQUEST_ADDRESS = G431_BOOT_REQUEST_SIGNATURE;
    NVIC_SystemReset();
}

int main(void)
{
    HAL_Init();

    if (memcmp((const void *)BL_BASE, g_v3_bl_image, V3_BL_IMAGE_SIZE) != 0)
    {
        if ((reflash_bootloader() == 0) &&
            (memcmp((const void *)BL_BASE, g_v3_bl_image, V3_BL_IMAGE_SIZE) == 0))
        {
            request_bootloader_and_reset();
        }
        /* fall through to halt on failure -> recover over ST DFU */
    }

    /* Already up to date, or a failed reflash: do nothing further. */
    for (;;)
    {
    }
}
