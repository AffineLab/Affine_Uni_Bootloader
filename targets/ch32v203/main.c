#include "bootloader.h"
#include "boot_platform.h"
#include "ch32v20x.h"
#include "usb.h"

uint32_t SystemCoreClock = 48000000U;
static bootloader_t boot;
/* Poll a free-running 64-bit HCLK counter: no WCH-specific ISR ABI required. */
uint32_t boot_platform_millis(void)
{
    volatile uint32_t *counter = (volatile uint32_t *)&SysTick->CNT;
    uint32_t high, low;
    do
    {
        high = counter[1];
        low = counter[0];
    } while (high != counter[1]);
    return (uint32_t)((((uint64_t)high << 32) | low) / 48000U);
}

void Delay_Us(uint32_t us)
{
    while (us--)
    {
        uint32_t start = (uint32_t)SysTick->CNT;
        while ((uint32_t)SysTick->CNT - start < 48U)
        {
        }
    }
}

void Delay_Ms(uint32_t ms)
{
    while (ms--)
    {
        boot_platform_watchdog_kick();
        Delay_Us(1000);
    }
}

/* SDK startup calls this before main. D6 SetSysClockTo48_HSE sequence. */
void SystemInit(void)
{
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET)
    {
        boot_platform_watchdog_kick();
    }
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, AFFINE_CH32_PLL_MUL);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET)
    {
        boot_platform_watchdog_kick();
    }
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08U)
    {
        boot_platform_watchdog_kick();
    }
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = UINT64_MAX;
    SysTick->CTLR = 5U;
    /* STE + STCLK(HCLK), STIE remains clear; core_riscv.h */
}

int main(void)
{
    bootloader_init(&boot);
    boot_ch32_usb_init();
    uint32_t start = boot_platform_millis();
    while ((uint32_t)(boot_platform_millis() - start) < 1000U && !boot.frame_ok_count)
    {
        boot_ch32_usb_receive(&boot);
        bootloader_poll(&boot);
    }
    if (!boot.frame_ok_count)
    {
        bootloader_try_boot_app(&boot);
    }
    for (;;)
    {
        boot_ch32_usb_receive(&boot);
        bootloader_poll(&boot);
    }
}
