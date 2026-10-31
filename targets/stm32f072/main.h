#ifndef AFFINE_BOOT_STM32F072_MAIN_H
#define AFFINE_BOOT_STM32F072_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "boot_protocol.h"
#include "stm32f0xx_hal.h"

void Error_Handler(void);
void SystemClock_Config(void);

/* Receive channels: one per transport built in, each with its own FIFO. */
#define BOOT_RX_CHANNEL_USB 0U
#if defined(AFFINE_BOOT_TRANSPORT_DUAL)
#define BOOT_RX_CHANNEL_UART 1U
#else
#define BOOT_RX_CHANNEL_UART 0U
#endif

/* Bytes the transport received, from its interrupt (main.c). */
void boot_stm32f072_receive_isr(uint32_t channel, const uint8_t *data, uint32_t length);
void boot_stm32f072_poll(void);
/* AFFINE_BOOT_TRANSPORT=dual: the channel in use, BOOT_RX_ARBITER_NONE before any frame. */
uint8_t boot_stm32f072_locked_channel(void);

/*
 * The transport, chosen at build time (AFFINE_BOOT_TRANSPORT): transport.c
 * implements boot_platform_transport_send/poll/get_diag on top of
 * transport_usb.c (USB CDC), transport_uart.c (USART), or both.
 */
void boot_stm32f072_transport_init(void);
void boot_stm32f072_transport_note_rx_overflow(uint32_t channel);

void boot_stm32f072_usb_transport_init(void);
void boot_stm32f072_usb_transport_note_rx_overflow(void);
void boot_stm32f072_usb_transport_send(const uint8_t *data, size_t length);
void boot_stm32f072_usb_transport_poll(void);
bool boot_stm32f072_usb_transport_get_diag(boot_diag_response_t *out_diag);

void boot_stm32f072_uart_transport_init(void);
void boot_stm32f072_uart_transport_note_rx_overflow(void);
void boot_stm32f072_uart_transport_send(const uint8_t *data, size_t length);
bool boot_stm32f072_uart_transport_get_diag(boot_diag_response_t *out_diag);

/* USB only: called by usbd_cdc_acm_if.c. */
void boot_stm32f072_usb_diag_note_rx(uint32_t length);
void boot_stm32f072_usb_diag_note_tx_complete(uint32_t length);
void MX_USB_PCD_Init(void);
void MX_USB_DEVICE_Init(void);

#ifdef __cplusplus
}
#endif

#endif
