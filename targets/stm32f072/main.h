#ifndef AFFINE_BOOT_STM32F072_MAIN_H
#define AFFINE_BOOT_STM32F072_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"

void Error_Handler(void);
void SystemClock_Config(void);

/* Bytes the transport received, from its interrupt (main.c). */
void boot_stm32f072_receive_isr(const uint8_t *data, uint32_t length);
void boot_stm32f072_poll(void);

/*
 * The transport, chosen at build time (AFFINE_BOOT_TRANSPORT): transport_usb.c
 * (USB CDC) or transport_uart.c (USART). Each also implements
 * boot_platform_transport_send/poll/get_diag.
 */
void boot_stm32f072_transport_init(void);
void boot_stm32f072_transport_note_rx_overflow(void);

/* USB only: called by usbd_cdc_acm_if.c. */
void boot_stm32f072_usb_diag_note_rx(uint32_t length);
void boot_stm32f072_usb_diag_note_tx_complete(uint32_t length);
void MX_USB_PCD_Init(void);
void MX_USB_DEVICE_Init(void);

#ifdef __cplusplus
}
#endif

#endif
