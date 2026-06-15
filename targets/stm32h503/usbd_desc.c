#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"
#include "usbd_ctlreq.h"

#define USBD_VID                      0xAFF1U
#define USBD_PID                      0x52B1U
#define USBD_LANGID_STRING            1033U
#define USBD_MANUFACTURER_STRING      "AffineLab"
#define USBD_PRODUCT_STRING           "Affine Uni Bootloader"
#define USBD_CONFIGURATION_STRING     "Bootloader CDC"
#define USBD_INTERFACE_STRING         "CDC Interface"

static void Get_SerialNum(void);
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);

uint8_t *USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef USBD_Desc = {
    USBD_DeviceDescriptor,
    USBD_LangIDStrDescriptor,
    USBD_ManufacturerStrDescriptor,
    USBD_ProductStrDescriptor,
    USBD_SerialStrDescriptor,
    USBD_ConfigStrDescriptor,
    USBD_InterfaceStrDescriptor
};

__ALIGN_BEGIN static uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12U,
    USB_DESC_TYPE_DEVICE,
    0x00U,
    0x02U,
    0xEFU,
    0x02U,
    0x01U,
    USB_MAX_EP0_SIZE,
    LOBYTE(USBD_VID),
    HIBYTE(USBD_VID),
    LOBYTE(USBD_PID),
    HIBYTE(USBD_PID),
    0x00U,
    0x01U,
    USBD_IDX_MFC_STR,
    USBD_IDX_PRODUCT_STR,
    USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION
};

__ALIGN_BEGIN static uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC,
    USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING),
    HIBYTE(USBD_LANGID_STRING)
};

__ALIGN_BEGIN static uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN static uint8_t USBD_StringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
    USB_SIZ_STRING_SERIAL,
    USB_DESC_TYPE_STRING
};
__ALIGN_BEGIN uint8_t USBD_ContainerIDDesc[24] __ALIGN_END = {
    0x18U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x01U,
    0x06U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U
};

uint8_t *USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_DeviceDesc);
    return USBD_DeviceDesc;
}

uint8_t *USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_LangIDDesc);
    return USBD_LangIDDesc;
}

uint8_t *USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

uint8_t *USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

uint8_t *USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = USB_SIZ_STRING_SERIAL;
    Get_SerialNum();
    return USBD_StringSerial;
}

uint8_t *USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

uint8_t *USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static void Get_SerialNum(void)
{
    uint32_t deviceserial0 = *(uint32_t *)DEVICE_ID1;
    uint32_t deviceserial1 = *(uint32_t *)DEVICE_ID2;
    uint32_t deviceserial2 = *(uint32_t *)DEVICE_ID3;

    deviceserial0 += deviceserial2;

    if (deviceserial0 != 0U)
    {
        IntToUnicode(deviceserial0, &USBD_StringSerial[2], 8U);
        IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4U);
    }
}

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint8_t idx = 0U; idx < len; idx++)
    {
        pbuf[2U * idx] = (value >> 28U) < 0xAU ? (value >> 28U) + '0' : (value >> 28U) + 'A' - 10U;
        value <<= 4U;
        pbuf[(2U * idx) + 1U] = 0U;
    }
}
