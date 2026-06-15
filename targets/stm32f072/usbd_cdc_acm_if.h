#ifndef AFFINE_BOOT_STM32F072_USBD_CDC_ACM_IF_H
#define AFFINE_BOOT_STM32F072_USBD_CDC_ACM_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_cdc_acm.h"

extern USBD_CDC_ACM_ItfTypeDef USBD_CDC_ACM_fops;

uint8_t CDC_Transmit(uint8_t ch, uint8_t *buf, uint16_t len);
uint8_t CDC_IsBusy(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif
