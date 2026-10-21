#include "main.h"

#include <string.h>

#include "boot_platform.h"
#include "bootloader.h"

#define BOOT_RX_FIFO_SIZE      1024U
#define BOOT_RX_DRAIN_CHUNK      64U
#define BOOT_AUTO_BOOT_DELAY_MS 1000U
/* See boot_guard_stm32f072.c: the request word and value the guard looks for. */
#define BOOT_ROM_LOADER_REQUEST_ADDRESS (0x20000000UL + 0x3F00UL)
#define BOOT_ROM_LOADER_REQUEST         0x55444621UL
#define BOOT_ROM_LOADER_FALLBACK_MS     60000U

static bootloader_t g_bootloader;
/* Filled by the transport's interrupt, drained by the main loop. */
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;
static uint8_t g_rx_fifo[BOOT_RX_FIFO_SIZE];

static void boot_fifo_push_isr(uint8_t value)
{
    const uint16_t next = (uint16_t)((g_rx_head + 1U) % BOOT_RX_FIFO_SIZE);

    if (next == g_rx_tail)
    {
        boot_stm32f072_transport_note_rx_overflow();
        return;
    }

    g_rx_fifo[g_rx_head] = value;
    g_rx_head = next;
}

void boot_stm32f072_receive_isr(const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0U; i < length; ++i)
    {
        boot_fifo_push_isr(data[i]);
    }
}

void boot_stm32f072_poll(void)
{
    uint8_t chunk[BOOT_RX_DRAIN_CHUNK];
    uint32_t count = 0U;

    while ((g_rx_tail != g_rx_head) && (count < BOOT_RX_DRAIN_CHUNK))
    {
        chunk[count++] = g_rx_fifo[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1U) % BOOT_RX_FIFO_SIZE);
    }

    if (count > 0U)
    {
        bootloader_receive_bytes(&g_bootloader, chunk, count);
    }

    bootloader_poll(&g_bootloader);
}

#if defined(AFFINE_BOOT_TRANSPORT_UART)
/*
 * 48 MHz from HSI (8 MHz) through the PLL. Without USB there is nothing to
 * trim HSI48 against (CRS syncs on the USB SOF), so the factory-trimmed HSI
 * times the UART instead.
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLMUL = RCC_PLL_MUL6;
    osc.PLL.PREDIV = RCC_PREDIV_DIV1;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }
}
#else
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    RCC_PeriphCLKInitTypeDef periph_clk = {0};
    RCC_CRSInitTypeDef crs = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI48;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.HSI48State = RCC_HSI48_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }

    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    periph_clk.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_CRS_CLK_ENABLE();
    crs.Prescaler = RCC_CRS_SYNC_DIV1;
    crs.Source = RCC_CRS_SYNC_SOURCE_USB;
    crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
    crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&crs);
}
#endif

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

    memset((void *)g_rx_fifo, 0, sizeof(g_rx_fifo));
    g_rx_head = 0U;
    g_rx_tail = 0U;

    bootloader_init(&g_bootloader);

    HAL_Init();
    SystemClock_Config();
    boot_stm32f072_transport_init();

    boot_wait_start_ms = HAL_GetTick();
    while (((HAL_GetTick() - boot_wait_start_ms) < BOOT_AUTO_BOOT_DELAY_MS) && (g_bootloader.frame_ok_count == 0U))
    {
        boot_stm32f072_poll();
    }

    if (g_bootloader.frame_ok_count == 0U)
    {
        bootloader_try_boot_app(&g_bootloader);
    }

#if defined(AFFINE_BOARD_MONICA_LITE_R1) && defined(AFFINE_BOOT_F072_ROM_LOADER_ESCAPE)
    /*
     * Development builds for a board without a button: a bootloader that
     * nobody manages to talk to (no application yet, USB not coming up) hands
     * over to the MCU's ROM loader after a while, through the boot guard.
     */
    boot_wait_start_ms = HAL_GetTick();
    for (;;)
    {
        boot_stm32f072_poll();
        if ((g_bootloader.frame_ok_count == 0U) &&
            ((HAL_GetTick() - boot_wait_start_ms) >= BOOT_ROM_LOADER_FALLBACK_MS))
        {
            *(volatile uint32_t *)BOOT_ROM_LOADER_REQUEST_ADDRESS = BOOT_ROM_LOADER_REQUEST;
            boot_platform_reboot();
        }
    }
#else
    for (;;)
    {
        boot_stm32f072_poll();
    }
#endif
}
