#ifndef AFFINE_RP2350_USB_H
#define AFFINE_RP2350_USB_H
#include "bootloader.h"
#define BOOT_RP2350_CDC_IN 0x81U
void boot_rp2350_usb_init(void);
void boot_rp2350_usb_task(void);
void boot_rp2350_usb_receive(bootloader_t *ctx);
#endif
