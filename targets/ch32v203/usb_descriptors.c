#include "board_config.h"

#if AFFINE_CH32_USBFS
#define DEVICE MyDevDescr
#define CONFIG MyCfgDescr
#define LANG MyLangDescr
#define VENDOR MyManuInfo
#define PRODUCT MyProdInfo
#define SERIAL MySerNumInfo
#else
#define DEVICE USBD_DeviceDescriptor
#define CONFIG USBD_ConfigDescriptor
#define LANG USBD_StringLangID
#define VENDOR USBD_StringVendor
#define PRODUCT USBD_StringProduct
#define SERIAL USBD_StringSerial
#endif
/* iSerialNumber=0: no fabricated unique device identity. */
const uint8_t DEVICE[] =
{
    18, 1, 0x10, 1, 2, 0, 0, 64, BOOT_CH32_USB_VID & 255, BOOT_CH32_USB_VID >> 8, BOOT_CH32_USB_PID & 255,
        BOOT_CH32_USB_PID >> 8, 0, 1, 1, 2, 0, 1
};

const uint8_t CONFIG[] = {
    9, 2, 67, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 1, 2, 2, 1, 0,
    5, 0x24, 0, 0x10, 1,
    5, 0x24, 1, 0, 1,
    4, 0x24, 2, 2,
    5, 0x24, 6, 0, 1,
    7, 5, 0x81, 3, 8, 0, 16,
    9, 4, 1, 0, 2, 0x0A, 0, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0,
    7, 5, 0x83, 2, 64, 0, 0
};

const uint8_t LANG[] =
{
    4, 3, 9, 4
};

const uint8_t VENDOR[] =
{
    14, 3, 65, 0, 102, 0, 102, 0, 105, 0, 110, 0, 101, 0
};

const uint8_t PRODUCT[] =
{
    18, 3, 85, 0, 110, 0, 105, 0, 32, 0, 66, 0, 111, 0, 111, 0, 116, 0
};

uint8_t SERIAL[] =
{
    2, 3
};
