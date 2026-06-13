#ifndef AFFINE_BOOT_STM32H503_USBD_CONF_H
#define AFFINE_BOOT_STM32H503_USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

#include "main.h"

#define USBD_MAX_NUM_INTERFACES           2U
#define USBD_MAX_NUM_CONFIGURATION        1U
#define USBD_MAX_STR_DESC_SIZ             64U
#define USBD_SUPPORT_USER_STRING_DESC     0U
#define USBD_DEBUG_LEVEL                  0U
#define USBD_LPM_ENABLED                  0U
#define USBD_SELF_POWERED                 1U

#define DEVICE_FS                         0
#define DEVICE_HS                         1

#define USBD_memset                       memset
#define USBD_memcpy                       memcpy
#define USBD_Delay                        HAL_Delay

#if (USBD_DEBUG_LEVEL > 0)
#define USBD_UsrLog(...)    printf(__VA_ARGS__); printf("\n")
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1)
#define USBD_ErrLog(...)    printf("ERROR: "); printf(__VA_ARGS__); printf("\n")
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2)
#define USBD_DbgLog(...)    printf("DEBUG : "); printf(__VA_ARGS__); printf("\n")
#else
#define USBD_DbgLog(...)
#endif

#ifdef __cplusplus
}
#endif

#endif
