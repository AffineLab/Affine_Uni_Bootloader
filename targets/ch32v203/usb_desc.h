#ifndef AFFINE_CH32_USB_DESC_H
#define AFFINE_CH32_USB_DESC_H

#include "debug.h"

#define DEF_USBD_UEP0_SIZE 64
#define DEF_USBD_MAX_PACK_SIZE 64
#define DEF_USBD_ENDP1_SIZE 64
#define DEF_USBD_ENDP2_SIZE 64
#define DEF_USBD_ENDP3_SIZE 64
#define USBD_SIZE_DEVICE_DESC 18
#define USBD_SIZE_CONFIG_DESC 67
#define USBD_SIZE_STRING_LANGID 4
#define USBD_SIZE_STRING_VENDOR 14
#define USBD_SIZE_STRING_PRODUCT 18
#define USBD_SIZE_STRING_SERIAL 2
#define DEF_USBD_DEVICE_DESC_LEN 18
#define DEF_USBD_CONFIG_DESC_LEN 67
#define DEF_USBD_LANG_DESC_LEN 4
#define DEF_USBD_MANU_DESC_LEN 14
#define DEF_USBD_PROD_DESC_LEN 18
#define DEF_USBD_SN_DESC_LEN 2
extern const uint8_t USBD_DeviceDescriptor[], USBD_ConfigDescriptor[], USBD_StringLangID[],
    USBD_StringVendor[], USBD_StringProduct[];
extern uint8_t USBD_StringSerial[];
extern const uint8_t MyDevDescr[], MyCfgDescr[], MyLangDescr[], MyManuInfo[], MyProdInfo[], MySerNumInfo[];

#endif
