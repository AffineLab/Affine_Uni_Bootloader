#include "at32f402_405.h"
#include "bootloader.h"
#include "boot_platform.h"
#include "usb.h"

static bootloader_t g_bootloader;
static volatile uint32_t g_millis;

void SysTick_Handler(void)
{
    ++g_millis;
}

uint32_t boot_platform_millis(void)
{
    return g_millis;
}

/* The USB library calls these during initialization with interrupts disabled
 * as well as during normal operation. DWT does not depend on SysTick IRQs. */
void usb_delay_us(uint32_t us)
{
    while (us-- != 0U)
    {
        const uint32_t start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < system_core_clock / 1000000U)
        {
        }
    }
}

void usb_delay_ms(uint32_t ms)
{
    while (ms-- != 0U)
    {
        boot_platform_watchdog_kick();
        usb_delay_us(1000U);
    }
}

static void clock_init(void)
{
    crm_reset();
    flash_psr_set(FLASH_WAIT_CYCLE_6);
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
    pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);
#if AFFINE_AT32_USB_HS
    /* virtual_comport example: HEXT12 * 72 / 4 = 216 MHz;
     * PLLU = HEXT12 * 72 / 18 = 48 MHz. HS PHY uses HEXT12. */
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
    while (crm_hext_stable_wait() == ERROR)
    {
        boot_platform_watchdog_kick();
    }
    crm_pll_config(CRM_PLL_SOURCE_HEXT, 72U, 1U, CRM_PLL_FP_4);
#else
    /* acc/calibration example: HICK8 * 108 / 4 = 216 MHz. */
    crm_pll_config(CRM_PLL_SOURCE_HICK, 108U, 1U, CRM_PLL_FP_4);
#endif
    crm_pllu_div_set(CRM_PLL_FU_18);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
    while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
    {
        boot_platform_watchdog_kick();
    }
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb1_div_set(CRM_APB1_DIV_2);
    crm_apb2_div_set(CRM_APB2_DIV_1);
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
    {
    }
    crm_auto_step_mode_enable(FALSE);
    system_core_clock_update();

#if AFFINE_AT32_USB_HS
    crm_pllu_output_set(TRUE);
    while (crm_flag_get(CRM_PLLU_STABLE_FLAG) != SET)
    {
        boot_platform_watchdog_kick();
    }
    crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_PLLU);
    crm_usb_phy12_clock_select(CRM_USB_PHY12_CLOCK_HEXT_DIV_1);
    crm_periph_clock_enable(CRM_OTGHSPHY_PERIPH_CLOCK, TRUE);
#else
    /* SOF trimming thresholds from usb_device/virtual_comport. */
    crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);
    crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
    acc_write_c1(7980U);
    acc_write_c2(8000U);
    acc_write_c3(8020U);
    acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);
    /* SDK FAQ0148 workaround; PLL is already enabled. */
    reduce_power_consumption();
#endif
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    (void)SysTick_Config(system_core_clock / 1000U);
}

void __libc_init_array(void)
{
}

int main(void)
{
    clock_init();
    bootloader_init(&g_bootloader);
    boot_at32_usb_init();
    const uint32_t start = boot_platform_millis();
    while ((uint32_t)(boot_platform_millis() - start) < 1000U && g_bootloader.frame_ok_count == 0U)
    {
        boot_at32_usb_receive(&g_bootloader);
        bootloader_poll(&g_bootloader);
    }
    if (g_bootloader.frame_ok_count == 0U)
    {
        bootloader_try_boot_app(&g_bootloader);
    }
    for (;;)
    {
        boot_at32_usb_receive(&g_bootloader);
        bootloader_poll(&g_bootloader);
    }
}
