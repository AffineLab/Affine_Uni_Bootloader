#ifndef AFFINE_AT32_USB_H
#define AFFINE_AT32_USB_H
#include "bootloader.h"
#include "usb_core.h"

extern otg_core_type g_boot_usb;
extern usbd_class_handler boot_usb_class;
uint32_t boot_at32_irq_save(void);
void boot_at32_irq_restore(uint32_t mask);
extern usbd_desc_handler boot_usb_descriptors;
void boot_at32_usb_init(void);
void boot_at32_usb_deinit(void);
void boot_at32_usb_receive(bootloader_t *bootloader);

#endif
