#include "usb.h"
#include "usbd_int.h"

uint32_t boot_at32_irq_save(void)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

void boot_at32_irq_restore(uint32_t mask)
{
    __set_PRIMASK(mask);
}

void BOOT_USB_IRQ_HANDLER(void)
{
    usbd_irq_handler(&g_boot_usb);
}

void boot_at32_usb_init(void)
{
    /* Matches the examples: dedicated USB data pins; VBUS/ID are ignored. */
    crm_periph_clock_enable(AFFINE_AT32_USB_HS ? CRM_GPIOB_PERIPH_CLOCK : CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(BOOT_USB_CLOCK, TRUE);
    (void)usbd_init(&g_boot_usb, BOOT_USB_SPEED, BOOT_USB_ID, &boot_usb_class, &boot_usb_descriptors);
    NVIC_SetPriority(BOOT_USB_IRQ, 0U);
    NVIC_EnableIRQ(BOOT_USB_IRQ);
}

void boot_at32_usb_deinit(void)
{
    usb_interrupt_disable(g_boot_usb.usb_reg);
    usbd_disconnect(&g_boot_usb.dev);
    NVIC_DisableIRQ(BOOT_USB_IRQ);
    /* The SDK's FS power workaround also touches the unused HS controller. */
    crm_periph_reset(CRM_OTGFS1_PERIPH_RESET, TRUE);
    crm_periph_reset(CRM_OTGFS1_PERIPH_RESET, FALSE);
    crm_periph_clock_enable(CRM_OTGFS1_PERIPH_CLOCK, FALSE);
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET, TRUE);
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET, FALSE);
    crm_periph_clock_enable(CRM_OTGHS_PERIPH_CLOCK, FALSE);
    crm_periph_reset(CRM_OTGHSPHY_PERIPH_RESET, TRUE);
    crm_periph_reset(CRM_OTGHSPHY_PERIPH_RESET, FALSE);
    crm_periph_clock_enable(CRM_OTGHSPHY_PERIPH_CLOCK, FALSE);
    acc_calibration_mode_enable(ACC_CAL_HICKTRIM, FALSE);
    crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, FALSE);
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
}
