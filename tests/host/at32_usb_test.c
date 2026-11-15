/* Actual target CDC adapter + SDK headers, with host-only USB I/O fakes.
 * No SDK code or device registers are executed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "boot_platform.h"
#include "targets/at32f405/usb.h"
#include "cdc_class.h"

static uint8_t *rx_buffer;
static uint16_t rx_capacity;
static uint32_t rx_count, receive_calls, send_calls, control_sends, control_receives, stalls;
static uint16_t last_send_length, opened_packet;
static uint8_t received[1024];
static size_t received_length;
static uint8_t sent[1024];

uint32_t boot_at32_irq_save(void) { return 0U; }
void boot_at32_irq_restore(uint32_t mask) { (void)mask; }
uint32_t boot_platform_millis(void) { return 123U; }
void bootloader_receive_bytes(bootloader_t *bootloader, const uint8_t *data, size_t length)
{
    (void)bootloader;
    assert(received_length + length <= sizeof(received));
    memcpy(received + received_length, data, length);
    received_length += length;
}
void usbd_ept_open(usbd_core_type *dev, uint8_t ep, uint8_t type, uint16_t packet)
{
    (void)dev; (void)type;
    if (ep == USBD_CDC_BULK_OUT_EPT) opened_packet = packet;
}
void usbd_ept_close(usbd_core_type *dev, uint8_t ep) { (void)dev; (void)ep; }
void usbd_ept_recv(usbd_core_type *dev, uint8_t ep, uint8_t *buffer, uint16_t length)
{
    (void)dev;
    assert(ep == USBD_CDC_BULK_OUT_EPT);
    rx_buffer = buffer;
    rx_capacity = length;
    ++receive_calls;
}
void usbd_ept_send(usbd_core_type *dev, uint8_t ep, uint8_t *buffer, uint16_t length)
{
    (void)dev;
    assert(ep == USBD_CDC_BULK_IN_EPT && length <= sizeof(sent));
    assert((uintptr_t)buffer % 4U == 0U);
    memcpy(sent, buffer, length);
    last_send_length = length;
    ++send_calls;
}
uint32_t usbd_get_recv_len(usbd_core_type *dev, uint8_t ep) { (void)dev; (void)ep; return rx_count; }
void usbd_ctrl_recv(usbd_core_type *dev, uint8_t *buffer, uint16_t length)
{
    (void)dev; (void)buffer;
    assert(length == 7U);
    ++control_receives;
}
void usbd_ctrl_send(usbd_core_type *dev, uint8_t *buffer, uint16_t length)
{
    (void)dev; (void)buffer;
    assert(length == 7U || length == 1U);
    ++control_sends;
}
void usbd_ctrl_unsupport(usbd_core_type *dev) { (void)dev; ++stalls; }

static void configure(uint8_t speed)
{
    boot_usb_class.event_handler(&g_boot_usb.dev, USBD_RESET_EVENT);
    g_boot_usb.dev.speed = speed;
    g_boot_usb.dev.conn_state = USB_CONN_STATE_CONFIGURED;
    g_boot_usb.dev.class_handler = &boot_usb_class;
    boot_usb_class.init_handler(&g_boot_usb.dev);
}

static void descriptor_test(usbd_desc_t *desc, uint16_t packet, uint8_t type)
{
    assert(desc->length == 67U && desc->descriptor[1] == type);
    unsigned bulk_count = 0U;
    for (unsigned i = 0U; i < desc->length; i += desc->descriptor[i])
    {
        assert(desc->descriptor[i] != 0U && i + desc->descriptor[i] <= desc->length);
        if (desc->descriptor[i + 1U] == 5U && desc->descriptor[i + 3U] == 2U)
        {
            assert((desc->descriptor[i + 4U] | desc->descriptor[i + 5U] << 8) == packet);
            ++bulk_count;
        }
    }
    assert(bulk_count == 2U);
}

static void test_speed(uint8_t speed, uint16_t packet)
{
    configure(speed);
    assert(rx_capacity == packet && opened_packet == packet);
    uint8_t frame[532];
    for (unsigned i = 0U; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
    received_length = 0U;
    /* Full-size frames span both a USB packet and a core parser call. */
    for (unsigned offset = 0U; offset < sizeof(frame);)
    {
        const unsigned length = sizeof(frame) - offset < packet ? sizeof(frame) - offset : packet;
        memcpy(rx_buffer, frame + offset, length);
        rx_count = length;
        const uint32_t before = receive_calls;
        boot_usb_class.out_handler(&g_boot_usb.dev, USBD_CDC_BULK_OUT_EPT);
        assert(receive_calls == before); /* OUT remains NAKed until consumed. */
        boot_at32_usb_receive(NULL);
        assert(receive_calls == before + 1U);
        offset += length;
    }
    assert(received_length == sizeof(frame) && memcmp(frame, received, sizeof(frame)) == 0);

    send_calls = 0U;
    boot_platform_transport_send(frame, sizeof(frame));
    boot_platform_transport_poll();
    assert(send_calls == 1U && last_send_length == sizeof(frame) && memcmp(frame, sent, sizeof(frame)) == 0);
    boot_platform_transport_poll();
    assert(send_calls == 1U);
    boot_usb_class.in_handler(&g_boot_usb.dev, USBD_CDC_BULK_IN_EPT & 0x7FU);
    boot_platform_transport_poll();
    boot_diag_response_t diag;
    assert(boot_platform_transport_get_diag(&diag) && diag.tx_complete_count == 1U);

    /* A full-packet response needs its terminating ZLP before queue retirement. */
    boot_platform_transport_send(frame, packet);
    boot_platform_transport_poll();
    assert(send_calls == 2U && last_send_length == packet);
    boot_usb_class.in_handler(&g_boot_usb.dev, 1U);
    assert(send_calls == 3U && last_send_length == 0U);
    boot_platform_transport_poll();
    assert(boot_platform_transport_get_diag(&diag) && diag.tx_complete_count == 1U);
    boot_usb_class.in_handler(&g_boot_usb.dev, 1U);
    boot_platform_transport_poll();
    assert(boot_platform_transport_get_diag(&diag) && diag.tx_complete_count == 2U);

    /* Suspension does not submit a transfer; reset discards queued replies/RX. */
    g_boot_usb.dev.conn_state = USB_CONN_STATE_SUSPENDED;
    boot_platform_transport_send(frame, 20U);
    boot_platform_transport_poll();
    assert(send_calls == 3U);
    g_boot_usb.dev.conn_state = USB_CONN_STATE_CONFIGURED;
    boot_platform_transport_poll();
    assert(send_calls == 4U);
    rx_count = 1U;
    boot_usb_class.out_handler(&g_boot_usb.dev, 1U);
    boot_usb_class.event_handler(&g_boot_usb.dev, USBD_RESET_EVENT);
    boot_at32_usb_receive(NULL);
    assert(received_length == sizeof(frame));
    assert(boot_platform_transport_get_diag(&diag) && diag.tx_enqueue_count == 0U);

    configure(speed);
    rx_count = packet + 1U;
    boot_usb_class.out_handler(&g_boot_usb.dev, 1U);
    boot_at32_usb_receive(NULL);
    assert(boot_platform_transport_get_diag(&diag) && diag.rx_fifo_overflow_count == 1U);
}

static void test_control(void)
{
    static const uint16_t lengths[] = {0, 1, 6, 7, 8, 64, 65535};
    for (unsigned direction = 0U; direction < 2U; ++direction)
    {
        for (unsigned i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        {
            usb_setup_type setup = {.bmRequestType = direction ? 0xA1 : 0x21,
                .bRequest = direction ? GET_LINE_CODING : SET_LINE_CODING, .wLength = lengths[i]};
            control_sends = control_receives = stalls = 0U;
            const usb_sts_type result = boot_usb_class.setup_handler(&g_boot_usb.dev, &setup);
            assert((result == USB_OK) == (lengths[i] == 7U));
            assert(control_sends + control_receives == (lengths[i] == 7U ? 1U : 0U));
            assert(stalls == (lengths[i] == 7U ? 0U : 1U));
        }
    }
    usb_setup_type setup = {.bmRequestType = 0x21, .bRequest = 0x22};
    assert(boot_usb_class.setup_handler(&g_boot_usb.dev, &setup) == USB_OK);
    setup.wIndex = 1U;
    assert(boot_usb_class.setup_handler(&g_boot_usb.dev, &setup) != USB_OK);
}

int main(void)
{
    descriptor_test(boot_usb_descriptors.get_device_configuration(), 64U, 2U);
    descriptor_test(boot_usb_descriptors.get_hs_device_configuration(), 512U, 2U);
    descriptor_test(boot_usb_descriptors.get_device_other_speed(), 64U, 7U);
    const usbd_desc_t *device = boot_usb_descriptors.get_device_descriptor();
    assert(device->descriptor[8] == 0xF1 && device->descriptor[9] == 0xAF);
    assert(device->descriptor[10] == 0xB3 && device->descriptor[11] == 0x52);
    test_speed(USB_FULL_SPEED, 64U);
    test_speed(USB_HIGH_SPEED, 512U);
    test_control();
    puts("ALL PASS: AT32 CDC descriptors, frame buffering, ZLP, reset, suspend, control bounds");
    return 0;
}
