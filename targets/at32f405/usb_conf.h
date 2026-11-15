#ifndef AFFINE_AT32_USB_CONF_H
#define AFFINE_AT32_USB_CONF_H

#include "at32f402_405.h"
#include <stddef.h>
#define USE_OTG_DEVICE_MODE
/* Device-only bootloader: no VBUS or ID GPIO is required. */
#define USB_VBUS_IGNORE
#define USB_EPT_MAX_NUM 8

#if AFFINE_AT32_USB_HS
#define USB_OTG_HS
#define BOOT_USB_ID USB_OTG2_ID
#define BOOT_USB_SPEED USB_HIGH_SPEED_CORE_ID
#define BOOT_USB_CLOCK CRM_OTGHS_PERIPH_CLOCK
#define BOOT_USB_RESET CRM_OTGHS_PERIPH_RESET
#define BOOT_USB_IRQ OTGHS_IRQn
#define BOOT_USB_IRQ_HANDLER OTGHS_IRQHandler
#else
#define USB_OTG_FS
#define BOOT_USB_ID USB_OTG1_ID
#define BOOT_USB_SPEED USB_FULL_SPEED_CORE_ID
#define BOOT_USB_CLOCK CRM_OTGFS1_PERIPH_CLOCK
#define BOOT_USB_RESET CRM_OTGFS1_PERIPH_RESET
#define BOOT_USB_IRQ OTGFS1_IRQn
#define BOOT_USB_IRQ_HANDLER OTGFS1_IRQHandler
#endif

/* FIFO allocation in 32-bit words, from the SDK virtual_comport example.
 * FS: 1280 bytes total. HS: 4096 bytes available. No DMA. */
#define USBD_RX_SIZE 128
#define USBD_EP0_TX_SIZE 24
#define USBD_EP1_TX_SIZE 20
#define USBD_EP2_TX_SIZE 20
#define USBD_EP3_TX_SIZE 20
#define USBD_EP4_TX_SIZE 20
#define USBD_EP5_TX_SIZE 20
#define USBD_EP6_TX_SIZE 20
#define USBD_EP7_TX_SIZE 20
#define USBD2_RX_SIZE 256
#define USBD2_EP0_TX_SIZE 64
#define USBD2_EP1_TX_SIZE 256
#define USBD2_EP2_TX_SIZE 20
#define USBD2_EP3_TX_SIZE 20
#define USBD2_EP4_TX_SIZE 20
#define USBD2_EP5_TX_SIZE 20
#define USBD2_EP6_TX_SIZE 20
#define USBD2_EP7_TX_SIZE 20

void usb_delay_ms(uint32_t ms);
void usb_delay_us(uint32_t us);

#endif
