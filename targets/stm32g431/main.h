#ifndef AFFINE_BOOT_STM32G431_MAIN_H
#define AFFINE_BOOT_STM32G431_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

void Error_Handler(void);
void SystemClock_Config(void);
void MX_USB_PCD_Init(void);
void MX_USB_DEVICE_Init(void);
void MX_NVIC_Init(void);

void boot_stm32g431_usb_receive_isr(const uint8_t *data, uint32_t length);
void boot_stm32g431_usb_diag_note_rx(uint32_t length);
void boot_stm32g431_usb_diag_note_rx_overflow(void);
void boot_stm32g431_usb_diag_note_tx_complete(uint32_t length);
void boot_stm32g431_poll(void);

#ifdef __cplusplus
}
#endif

#endif
