#ifndef AFFINE_BOOT_STM32H503_USB_H
#define AFFINE_BOOT_STM32H503_USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

void MX_USB_PCD_Init(void);

#ifdef __cplusplus
}
#endif

#endif
