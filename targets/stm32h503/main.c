#include "main.h"

#include <string.h>

#include "bootloader.h"
#include "usb.h"
#include "usb_device.h"

#define BOOT_USB_RX_FIFO_SIZE  1024U
#define BOOT_USB_DRAIN_CHUNK    64U
#define BOOT_AUTO_BOOT_DELAY_MS 1000U

static bootloader_t g_bootloader;
static volatile uint16_t g_usb_rx_head;
static volatile uint16_t g_usb_rx_tail;
static uint8_t g_usb_rx_fifo[BOOT_USB_RX_FIFO_SIZE];

static void boot_fifo_push_isr(uint8_t value)
{
    const uint16_t next = (uint16_t)((g_usb_rx_head + 1U) % BOOT_USB_RX_FIFO_SIZE);

    if (next == g_usb_rx_tail)
    {
        boot_stm32h503_usb_diag_note_rx_overflow();
        return;
    }

    g_usb_rx_fifo[g_usb_rx_head] = value;
    g_usb_rx_head = next;
}

void boot_stm32h503_usb_receive_isr(const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0U; i < length; ++i)
    {
        boot_fifo_push_isr(data[i]);
    }
}

void boot_stm32h503_poll(void)
{
    uint8_t chunk[BOOT_USB_DRAIN_CHUNK];
    uint32_t count = 0U;

    while ((g_usb_rx_tail != g_usb_rx_head) && (count < BOOT_USB_DRAIN_CHUNK))
    {
        chunk[count++] = g_usb_rx_fifo[g_usb_rx_tail];
        g_usb_rx_tail = (uint16_t)((g_usb_rx_tail + 1U) % BOOT_USB_RX_FIFO_SIZE);
    }

    if (count > 0U)
    {
        bootloader_receive_bytes(&g_bootloader, chunk, count);
    }

    bootloader_poll(&g_bootloader);
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    osc.HSI48State = RCC_HSI48_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    SystemCoreClock = HSI_VALUE;
    __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
    }
}

int main(void)
{
    uint32_t boot_wait_start_ms;

    memset((void *)g_usb_rx_fifo, 0, sizeof(g_usb_rx_fifo));
    g_usb_rx_head = 0U;
    g_usb_rx_tail = 0U;

    bootloader_init(&g_bootloader);

    HAL_Init();
    SystemClock_Config();
    MX_USB_PCD_Init();
    MX_USB_DEVICE_Init();

    boot_wait_start_ms = HAL_GetTick();
    while (((HAL_GetTick() - boot_wait_start_ms) < BOOT_AUTO_BOOT_DELAY_MS) && (g_bootloader.frame_ok_count == 0U))
    {
        boot_stm32h503_poll();
    }

    if (g_bootloader.frame_ok_count == 0U)
    {
        bootloader_try_boot_app(&g_bootloader);
    }

    for (;;)
    {
        boot_stm32h503_poll();
    }
}
