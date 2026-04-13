#include "boot_platform.h"

#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_hal.h"
#include "stm32g4xx_hal_flash.h"
#include "stm32g4xx_hal_flash_ex.h"
#include "usbd_cdc_acm_if.h"

#include "targets/stm32g431/board_config.h"

#define G431_BOOT_REQUEST_SIGNATURE   0x424F4F54UL
#define G431_BOOT_REQUEST_ADDRESS     (0x20000000UL + 0x7F00UL)
#define BOOT_USB_TX_QUEUE_DEPTH       8U

typedef struct
{
    uint16_t length;
    uint8_t data[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
} boot_usb_tx_packet_t;

static boot_usb_tx_packet_t g_tx_queue[BOOT_USB_TX_QUEUE_DEPTH];
static uint8_t g_tx_head;
static uint8_t g_tx_tail;
static uint8_t g_tx_count;
static bool g_tx_in_flight;
static boot_diag_response_t g_diag;

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
    g_diag.rx_callback_count++;
    g_diag.rx_byte_count += length;
    g_diag.rx_last_len = length;
    g_diag.last_rx_tick_ms = HAL_GetTick();
}

void boot_stm32g431_usb_diag_note_rx_overflow(void)
{
    g_diag.rx_fifo_overflow_count++;
}

void boot_stm32g431_usb_diag_note_tx_complete(uint32_t length)
{
    g_diag.tx_complete_count++;
    g_diag.tx_last_len = length;
    g_diag.last_tx_complete_tick_ms = HAL_GetTick();
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    boot_usb_tx_packet_t *packet;

    if ((length > sizeof(g_tx_queue[0].data)) || (g_tx_count >= BOOT_USB_TX_QUEUE_DEPTH))
    {
        g_diag.tx_queue_drop_count++;
        return;
    }

    packet = &g_tx_queue[g_tx_head];
    packet->length = (uint16_t)length;
    memcpy(packet->data, data, length);

    g_tx_head = (uint8_t)((g_tx_head + 1U) % BOOT_USB_TX_QUEUE_DEPTH);
    ++g_tx_count;
    g_diag.tx_enqueue_count++;
}

void boot_platform_transport_poll(void)
{
    if (g_tx_in_flight)
    {
        if (CDC_IsBusy(0U) != 0U)
        {
            return;
        }

        g_tx_tail = (uint8_t)((g_tx_tail + 1U) % BOOT_USB_TX_QUEUE_DEPTH);
        --g_tx_count;
        g_tx_in_flight = false;
    }

    if ((g_tx_count == 0U) || (CDC_IsBusy(0U) != 0U))
    {
        return;
    }

    if (CDC_Transmit(0U, g_tx_queue[g_tx_tail].data, g_tx_queue[g_tx_tail].length) == USBD_OK)
    {
        g_tx_in_flight = true;
        g_diag.tx_start_count++;
        g_diag.tx_last_len = g_tx_queue[g_tx_tail].length;
        g_diag.last_tx_start_tick_ms = HAL_GetTick();
    }
    else
    {
        g_diag.tx_busy_reject_count++;
    }
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
    if (out_diag == NULL)
    {
        return false;
    }

    *out_diag = g_diag;
    return true;
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
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    uint32_t start_page;
    uint32_t end_page;

    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }

    if (address < g_target_config.flash_base)
    {
        return BOOT_ERR_RANGE;
    }

    start_page = (address - g_target_config.flash_base) / FLASH_PAGE_SIZE;
    end_page = (address + length - 1U - g_target_config.flash_base) / FLASH_PAGE_SIZE;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = start_page;
    erase.NbPages = (end_page - start_page) + 1U;

    return (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK) ? BOOT_ERR_NONE : BOOT_ERR_FLASH;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;
    uint64_t word = 0U;

    if ((length % 8U) != 0U)
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

    for (uint32_t i = 0U; i < 8U; ++i)
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
