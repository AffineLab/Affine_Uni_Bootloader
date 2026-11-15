#include "usb.h"
#include "board_config.h"
#include "cdc_class.h"
#include "cdc_desc.h"

/* USB CDC ACM wire descriptors. Packet sizes match the SDK CDC endpoint ABI. */
/* Pad storage to a word boundary for the SDK FIFO writer, while advertising
 * the actual USB wire lengths below. */
static _Alignas(4) uint8_t g_device[20] = {
    18, 1, 0, 2, 2, 0, 0, 64,
    BOOT_AT32_USB_VID & 255, BOOT_AT32_USB_VID >> 8,
    BOOT_AT32_USB_PID & 255, BOOT_AT32_USB_PID >> 8,
    0, 1, 1, 2, 3, 1
};

#define CONFIG_BYTES(type, packet) \
    9, type, 67, 0, 2, 1, 0, 0x80, 50, \
    9, 4, 0, 0, 1, 2, 2, 1, 0, \
    5, 0x24, 0, 0x10, 1, \
    5, 0x24, 1, 0, 1, \
    4, 0x24, 2, 2, \
    5, 0x24, 6, 0, 1, \
    7, 5, USBD_CDC_INT_EPT, 3, USBD_CDC_CMD_MAXPACKET_SIZE, 0, 16, \
    9, 4, 1, 0, 2, 0x0A, 0, 0, 0, \
    7, 5, USBD_CDC_BULK_IN_EPT, 2, (packet) & 255, (packet) >> 8, 0, \
    7, 5, USBD_CDC_BULK_OUT_EPT, 2, (packet) & 255, (packet) >> 8, 0

static _Alignas(4) uint8_t g_fs[68] = {CONFIG_BYTES(2, USBD_FS_CDC_MAXPACKET_SIZE)};
static _Alignas(4) uint8_t g_hs[68] = {CONFIG_BYTES(2, USBD_HS_CDC_MAXPACKET_SIZE)};
static _Alignas(4) uint8_t g_other[68] = {CONFIG_BYTES(7, USBD_FS_CDC_MAXPACKET_SIZE)};
static _Alignas(4) uint8_t g_qualifier[12] = {10, 6, 0, 2, 2, 0, 0, 64, 1, 0};
static _Alignas(4) uint8_t g_language[] = {4, 3, 9, 4};
static _Alignas(4) uint8_t g_string[64];
static _Alignas(4) uint8_t g_serial[52];
static usbd_desc_t g_string_desc;

#define DESCRIPTOR(name, bytes, length) \
    static usbd_desc_t name##_desc = {length, bytes}; \
    static usbd_desc_t *name(void) { return &name##_desc; }
DESCRIPTOR(device, g_device, 18)
DESCRIPTOR(configuration_fs, g_fs, 67)
DESCRIPTOR(configuration_hs, g_hs, 67)
DESCRIPTOR(other_speed, g_other, 67)
DESCRIPTOR(qualifier, g_qualifier, 10)
DESCRIPTOR(language, g_language, 4)

static usbd_desc_t *string_descriptor(const char *text)
{
    uint32_t length = 2U;
    while (*text != '\0' && length + 2U <= sizeof(g_string))
    {
        g_string[length++] = (uint8_t)*text++;
        g_string[length++] = 0U;
    }
    g_string[0] = (uint8_t)length;
    g_string[1] = 3U;
    g_string_desc.length = (uint16_t)length;
    g_string_desc.descriptor = g_string;
    return &g_string_desc;
}

static usbd_desc_t *manufacturer(void) { return string_descriptor("Affine"); }
static usbd_desc_t *product(void) { return string_descriptor("Affine Uni Bootloader A405"); }
static usbd_desc_t *interface_string(void) { return string_descriptor("Bootloader CDC"); }

static usbd_desc_t *serial_number(void)
{
    static const uintptr_t addresses[] = {MCU_ID1, MCU_ID2, MCU_ID3};
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t word = 0U; word < 3U; ++word)
    {
        const uint32_t value = *(volatile uint32_t *)addresses[word];
        for (uint32_t nibble = 0U; nibble < 8U; ++nibble)
        {
            const uint32_t index = 2U + (word * 8U + nibble) * 2U;
            g_serial[index] = (uint8_t)hex[(value >> (28U - nibble * 4U)) & 15U];
            g_serial[index + 1U] = 0U;
        }
    }
    g_serial[0] = 50U;
    g_serial[1] = 3U;
    static usbd_desc_t descriptor = {50U, g_serial};
    return &descriptor;
}

usbd_desc_handler boot_usb_descriptors = {
    .get_device_descriptor = device,
    .get_device_qualifier = qualifier,
    .get_device_configuration = configuration_fs,
    .get_device_other_speed = other_speed,
    .get_device_lang_id = language,
    .get_device_manufacturer_string = manufacturer,
    .get_device_product_string = product,
    .get_device_serial_string = serial_number,
    .get_device_interface_string = interface_string,
    .get_device_config_string = interface_string,
    .get_hs_device_configuration = configuration_hs
};
