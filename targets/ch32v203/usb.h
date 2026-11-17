#ifndef AFFINE_CH32_USB_H
#define AFFINE_CH32_USB_H

#include "bootloader.h"

void boot_ch32_usb_init(void);
void boot_ch32_usb_deinit(void);
void boot_ch32_usb_receive(bootloader_t *boot);
void boot_ch32_usb_rx(const uint8_t *data, uint16_t length);
void boot_ch32_usb_reset(void);
void boot_ch32_usb_tx_done(void);

#endif
