#include "tusb.h"
/* Proposed Affine Pico 2 reference USB identity; confirm before release. */
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0xAFF1, .idProduct = 0x52B4,
    .bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2, .bNumConfigurations = 1
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 0, 0x82, 8, 0x01, 0x81, 64)
};
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&device; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    return index == 0U ? configuration : NULL;
}
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t descriptor[40];
    static const char *const strings[] = {"", "Affine", "Affine Uni Bootloader R235"};
    if (index == 0U) { descriptor[0] = 0x0304; descriptor[1] = 0x0409; return descriptor; }
    if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
    unsigned length = 0;
    while (strings[index][length] && length < 39U)
    { descriptor[1U + length] = (uint8_t)strings[index][length]; ++length; }
    descriptor[0] = (uint16_t)(0x0300U | (2U + 2U * length));
    return descriptor;
}
