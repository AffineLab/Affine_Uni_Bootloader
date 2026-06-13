#ifndef AFFINE_BOOT_STM32H503_MAIN_H
#define AFFINE_BOOT_STM32H503_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h5xx_hal.h"

void SystemClock_Config(void);
void Error_Handler(void);
void boot_stm32h503_usb_receive_isr(const uint8_t *data, uint32_t length);
void boot_stm32h503_usb_diag_note_rx(uint32_t length);
void boot_stm32h503_usb_diag_note_rx_overflow(void);
void boot_stm32h503_usb_diag_note_tx_complete(uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
