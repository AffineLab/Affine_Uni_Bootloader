#ifndef AFFINE_CH32_USB_HW_H
#define AFFINE_CH32_USB_HW_H

#include <stdbool.h>
#include <stdint.h>

void boot_ch32_hw_init(void);
void boot_ch32_hw_detach(void);
void boot_ch32_hw_deinit(void);
bool boot_ch32_hw_ready(void);
bool boot_ch32_hw_send(const uint8_t *data, uint16_t len);
void boot_ch32_hw_rx_arm(void);
uint32_t boot_ch32_irq_save(void);
void boot_ch32_irq_restore(uint32_t state);

#endif
