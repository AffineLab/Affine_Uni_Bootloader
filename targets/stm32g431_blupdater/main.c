/*
 * G431 bootloader self-update app.
 *
 * Runs from the application slot (0x08006000), launched by the existing
 * bootloader. It carries a new bootloader image embedded in flash and, on
 * boot, reprograms the bootloader region (0x08000000) with it. The image is
 * generated at build time by tools/embed_bl_image.py from a stamped, guarded
 * bootloader build.
 *
 * Power loss: the new image starts with the boot guard
 * (targets/stm32g431/boot_guard_stm32g431.c), which checks the rest of the
 * bootloader at every start and hands over to the ST system DFU loader when it
 * is incomplete. So the first page is written first, then the others:
 *  - lost while the first page is erased or written (some tens of ms): there is
 *    no valid reset vector, recovery needs BOOT0 or SWD. Single-bank G431 flash
 *    leaves no way around this window.
 *  - lost at any later point: the new guard finds the rest incomplete and starts
 *    the ST DFU loader, from where the bootloader can be written again.
 *
 * Other notes (STM32G431):
 *  - The new image is staged into RAM first, so no flash read happens while the
 *    bootloader region (same bank) is being erased/programmed.
 *  - Interrupts are disabled across the erase/program window.
 *  - An embedded image without a stamped guard is refused: without the guard a
 *    power loss anywhere in the rewrite would leave the board unbootable.
 *  - Idempotent: if the bootloader region already matches the embedded image we
 *    do not reprogram; only a fresh, verified reflash requests bootloader entry.
 *  - On any failure the app simply halts; the device is recoverable over the ST
 *    system DFU bootloader.
 */

#include "stm32g4xx_hal.h"

#include <string.h>

#include "v3_bl_image.h"

#define BL_BASE 0x08000000UL
#define BL_GUARD_MAGIC 0x44524147UL /* "GARD", see boot_guard_stm32g431.c */

/* Matches targets/stm32g431/boot_platform_stm32g431.c — asking the (new)
 * bootloader to stay in bootloader mode after reset instead of booting the app,
 * so the result is verifiable over USB. */
#define G431_BOOT_REQUEST_SIGNATURE 0x424F4F54UL
#define G431_BOOT_REQUEST_ADDRESS   (0x20000000UL + 0x7F00UL)

static volatile uint32_t g_flash_ecc_errors;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* A read of a double-word left half-written by an earlier power loss raises
 * a flash ECC double error: count it and carry on, the compare then fails. */
void NMI_Handler(void)
{
    if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_ECCD))
    {
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ECCD);
        g_flash_ecc_errors++;
        return;
    }

    for (;;)
    {
    }
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

/* Same check as boot_guard() runs on the board, applied to the embedded copy. */
static int embedded_image_is_guarded(void)
{
    uint32_t record[3];
    uint32_t sum = 0U;
    uint32_t start;

    memcpy(record, &g_v3_bl_image[V3_BL_GUARD_RECORD_OFFSET], sizeof(record));
    if ((record[0] != BL_GUARD_MAGIC) || (record[1] == 0U) || ((record[1] & 3U) != 0U) ||
        (record[1] > V3_BL_STAMPED_SIZE))
    {
        return 0;
    }

    /* The body runs to the end of the stamped image, before any padding. */
    start = V3_BL_STAMPED_SIZE - record[1];
    for (uint32_t offset = start; offset < V3_BL_STAMPED_SIZE; offset += 4U)
    {
        uint32_t word;

        memcpy(&word, &g_v3_bl_image[offset], sizeof(word));
        sum = ((sum << 5) | (sum >> 27)) ^ word;
    }
    return sum == record[2];
}

static int bootloader_region_matches(void)
{
    const uint32_t ecc_errors = g_flash_ecc_errors;
    const int same = memcmp((const void *)BL_BASE, g_v3_bl_image, V3_BL_IMAGE_SIZE) == 0;

    return same && (g_flash_ecc_errors == ecc_errors);
}

/* Erases one page of the bootloader region and programs its part of the image. */
static int reflash_page(uint32_t page, const uint8_t *src, uint32_t page_size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    const uint32_t start = page * page_size;
    const uint32_t end = ((start + page_size) < V3_BL_IMAGE_SIZE) ? (start + page_size) : V3_BL_IMAGE_SIZE;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1; /* bootloader region is at the start of bank 1 */
    erase.Page = page;
    erase.NbPages = 1U;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        return -1;
    }

    for (uint32_t offset = start; offset < end; offset += 8U)
    {
        uint64_t doubleword;

        memcpy(&doubleword, src + offset, 8U);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, BL_BASE + offset, doubleword) != HAL_OK)
        {
            return -2;
        }
    }

    return (memcmp((const void *)(BL_BASE + start), src + start, end - start) == 0) ? 0 : -3;
}

static int reflash_bootloader(void)
{
    const uint32_t page_size = flash_page_size();
    const uint32_t pages = (V3_BL_IMAGE_SIZE + page_size - 1U) / page_size;
    const uint8_t *src;
    int result = 0;

    /* Stage in RAM before touching flash (same-bank read-while-write stall). */
    memcpy(g_ram_image, g_v3_bl_image, V3_BL_IMAGE_SIZE);
    src = (const uint8_t *)g_ram_image;

    __disable_irq();
    HAL_FLASH_Unlock();

    /* Page 0 (vectors and guard) first: from then on the guard covers the rest. */
    for (uint32_t page = 0U; (page < pages) && (result == 0); ++page)
    {
        result = reflash_page(page, src, page_size);
    }

    HAL_FLASH_Lock();
    __enable_irq();
    return result;
}

static void request_bootloader_and_reset(void)
{
    *(volatile uint32_t *)G431_BOOT_REQUEST_ADDRESS = G431_BOOT_REQUEST_SIGNATURE;
    NVIC_SystemReset();
}

int main(void)
{
    HAL_Init();

    if (embedded_image_is_guarded() && !bootloader_region_matches())
    {
        if ((reflash_bootloader() == 0) && bootloader_region_matches())
        {
            request_bootloader_and_reset();
        }
        /* fall through to halt on failure -> recover over ST DFU */
    }

    /* Already up to date, a refused image, or a failed reflash: do nothing further. */
    for (;;)
    {
    }
}
