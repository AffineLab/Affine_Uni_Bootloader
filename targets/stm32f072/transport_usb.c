/* USB CDC transport (AFFINE_BOOT_TRANSPORT=usb, the default, or dual). */
#include "boot_platform.h"

#include "main.h"
#include "usb.h"
#include "usbd_cdc_acm_if.h"

#include "targets/common/boot_target_common.h"

static boot_target_usb_transport_t g_usb_transport;

void boot_stm32f072_usb_transport_init(void)
{
    MX_USB_PCD_Init();
    HAL_NVIC_SetPriority(USB_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(USB_IRQn);
    MX_USB_DEVICE_Init();
}

void boot_stm32f072_usb_diag_note_rx(uint32_t length)
{
    boot_target_usb_transport_note_rx(&g_usb_transport, length, HAL_GetTick());
}

void boot_stm32f072_usb_transport_note_rx_overflow(void)
{
    boot_target_usb_transport_note_rx_overflow(&g_usb_transport);
}

void boot_stm32f072_usb_diag_note_tx_complete(uint32_t length)
{
    boot_target_usb_transport_note_tx_complete(&g_usb_transport, length, HAL_GetTick());
}

void boot_stm32f072_usb_transport_send(const uint8_t *data, size_t length)
{
    boot_target_usb_transport_send(&g_usb_transport, data, length);
}

void boot_stm32f072_usb_transport_poll(void)
{
    boot_target_usb_transport_poll(&g_usb_transport, CDC_IsBusy, CDC_Transmit, USBD_OK, HAL_GetTick());
}

bool boot_stm32f072_usb_transport_get_diag(boot_diag_response_t *out_diag)
{
    return boot_target_usb_transport_get_diag(&g_usb_transport, out_diag);
}
