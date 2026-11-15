#include "usb.h"
#include <string.h>
#include "cdc_class.h"
#include "targets/common/boot_target_common.h"

otg_core_type g_boot_usb;
static boot_target_usb_transport_t g_transport;
/* The SDK FIFO writer reads rounded-up 32-bit words. Common queue entries
 * have a 16-bit length prefix, so their payloads need not be word-aligned. */
static _Alignas(4) uint8_t g_tx_frame[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
static _Alignas(4) uint8_t g_rx[USBD_HS_CDC_MAXPACKET_SIZE];
/* EP0 hardware receives a complete packet even for a seven-byte request. */
static _Alignas(4) uint8_t g_control[USB_MAX_EP0_SIZE] = {0x00, 0xC2, 0x01, 0, 0, 0, 8};
static uint32_t g_alt_setting;
static volatile uint32_t g_rx_length;
static volatile bool g_rx_ready;
static volatile bool g_tx_busy;
static bool g_tx_zlp;
static uint16_t g_tx_length;
static bool g_set_line_coding;

static uint16_t packet_size(void)
{
    return g_boot_usb.dev.speed == USB_HIGH_SPEED ? USBD_HS_CDC_MAXPACKET_SIZE : USBD_FS_CDC_MAXPACKET_SIZE;
}

static usb_sts_type class_init(void *context)
{
    usbd_core_type *dev = context;
    g_rx_ready = false;
    g_tx_busy = false;
    g_set_line_coding = false;
    g_alt_setting = 0U;
    usbd_ept_open(dev, USBD_CDC_BULK_IN_EPT, EPT_BULK_TYPE, packet_size());
    usbd_ept_open(dev, USBD_CDC_BULK_OUT_EPT, EPT_BULK_TYPE, packet_size());
    usbd_ept_open(dev, USBD_CDC_INT_EPT, EPT_INT_TYPE, USBD_CDC_CMD_MAXPACKET_SIZE);
    usbd_ept_recv(dev, USBD_CDC_BULK_OUT_EPT, g_rx, packet_size());
    return USB_OK;
}

static usb_sts_type class_clear(void *context)
{
    usbd_core_type *dev = context;
    g_rx_ready = false;
    g_tx_busy = false;
    g_set_line_coding = false;
    usbd_ept_close(dev, USBD_CDC_BULK_IN_EPT);
    usbd_ept_close(dev, USBD_CDC_BULK_OUT_EPT);
    usbd_ept_close(dev, USBD_CDC_INT_EPT);
    return USB_OK;
}

static usb_sts_type class_setup(void *context, usb_setup_type *setup)
{
    usbd_core_type *dev = context;
    g_set_line_coding = false;
    /* Bound control transfers before handing a buffer to the SDK. */
    if (setup->wIndex == 0U && setup->wValue == 0U && setup->wLength == 7U)
    {
        if (setup->bmRequestType == 0x21U && setup->bRequest == SET_LINE_CODING)
        {
            g_set_line_coding = true;
            usbd_ctrl_recv(dev, g_control, 7U);
            return USB_OK;
        }
        if (setup->bmRequestType == 0xA1U && setup->bRequest == GET_LINE_CODING)
        {
            usbd_ctrl_send(dev, g_control, 7U);
            return USB_OK;
        }
    }
    if (setup->bmRequestType == 0x21U && setup->wIndex == 0U && setup->wLength == 0U &&
        (setup->bRequest == 0x22U /* SET_CONTROL_LINE_STATE */ || setup->bRequest == 0x23U /* SEND_BREAK */))
    {
        return USB_OK;
    }
    if (setup->wIndex <= 1U && setup->wValue == 0U)
    {
        if (setup->bmRequestType == 0x81U && setup->bRequest == USB_STD_REQ_GET_INTERFACE && setup->wLength == 1U)
        {
            usbd_ctrl_send(dev, (uint8_t *)&g_alt_setting, 1U);
            return USB_OK;
        }
        if (setup->bmRequestType == 0x01U && setup->bRequest == USB_STD_REQ_SET_INTERFACE && setup->wLength == 0U)
        {
            return USB_OK;
        }
    }
    /* The SDK handles the endpoint halt bit and data toggle itself. */
    if (setup->bmRequestType == 0x02U && setup->bRequest == USB_STD_REQ_CLEAR_FEATURE &&
        setup->wValue == USB_FEATURE_EPT_HALT && setup->wLength == 0U &&
        (setup->wIndex == USBD_CDC_BULK_IN_EPT || setup->wIndex == USBD_CDC_BULK_OUT_EPT ||
         setup->wIndex == USBD_CDC_INT_EPT))
    {
        return USB_OK;
    }
    usbd_ctrl_unsupport(dev);
    return USB_FAIL;
}

static usb_sts_type control_received(void *context)
{
    usbd_core_type *dev = context;
    if (!g_set_line_coding || usbd_get_recv_len(dev, 0U) != 7U)
    {
        usbd_ctrl_unsupport(dev);
    }
    g_set_line_coding = false;
    return USB_OK;
}

static usb_sts_type data_sent(void *context, uint8_t endpoint)
{
    usbd_core_type *dev = context;
    if (endpoint == (USBD_CDC_BULK_IN_EPT & 0x7FU))
    {
        if (g_tx_zlp)
        {
            /* End a transfer whose length is a multiple of wMaxPacketSize. */
            g_tx_zlp = false;
            usbd_ept_send(dev, USBD_CDC_BULK_IN_EPT, g_rx, 0U);
        }
        else
        {
            boot_target_usb_transport_note_tx_complete(&g_transport, g_tx_length, boot_platform_millis());
            g_tx_busy = false;
        }
    }
    return USB_OK;
}

static usb_sts_type data_received(void *context, uint8_t endpoint)
{
    usbd_core_type *dev = context;
    if (endpoint == USBD_CDC_BULK_OUT_EPT)
    {
        g_rx_length = usbd_get_recv_len(dev, endpoint);
        boot_target_usb_transport_note_rx(&g_transport, g_rx_length, boot_platform_millis());
        g_rx_ready = true;
        /* Leave OUT NAKed until the main loop has copied this packet. */
    }
    return USB_OK;
}

static usb_sts_type class_event(void *context, usbd_event_type event)
{
    (void)context;
    if (event == USBD_RESET_EVENT || event == USBD_DISCONNECT_EVNET)
    {
        g_rx_ready = false;
        g_tx_busy = false;
        g_set_line_coding = false;
        memset(&g_transport, 0, sizeof(g_transport));
    }
    return USB_OK;
}

usbd_class_handler boot_usb_class = {
    .init_handler = class_init,
    .clear_handler = class_clear,
    .setup_handler = class_setup,
    .ept0_rx_handler = control_received,
    .in_handler = data_sent,
    .out_handler = data_received,
    .event_handler = class_event
};

static uint8_t tx_busy(uint8_t channel)
{
    (void)channel;
    return g_tx_busy || g_boot_usb.dev.conn_state != USB_CONN_STATE_CONFIGURED;
}

static uint8_t transmit(uint8_t channel, uint8_t *data, uint16_t length)
{
    (void)channel;
    if (tx_busy(0U))
    {
        return 1U;
    }
    g_tx_busy = true;
    g_tx_length = length;
    g_tx_zlp = length > 0U && length % packet_size() == 0U;
    memcpy(g_tx_frame, data, length);
    usbd_ept_send(&g_boot_usb.dev, USBD_CDC_BULK_IN_EPT, g_tx_frame, length);
    return 0U;
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    const uint32_t mask = boot_at32_irq_save();
    boot_target_usb_transport_send(&g_transport, data, length);
    boot_at32_irq_restore(mask);
}

void boot_platform_transport_poll(void)
{
    const uint32_t mask = boot_at32_irq_save();
    boot_target_usb_transport_poll(&g_transport, tx_busy, transmit, 0U, boot_platform_millis());
    boot_at32_irq_restore(mask);
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
    const uint32_t mask = boot_at32_irq_save();
    const bool result = boot_target_usb_transport_get_diag(&g_transport, out_diag);
    boot_at32_irq_restore(mask);
    return result;
}

void boot_at32_usb_receive(bootloader_t *bootloader)
{
    uint8_t packet[sizeof(g_rx)];
    uint32_t length = 0U;
    const uint32_t mask = boot_at32_irq_save();
    if (g_rx_ready)
    {
        length = g_rx_length;
        if (length > packet_size())
        {
            boot_target_usb_transport_note_rx_overflow(&g_transport);
            length = 0U;
        }
        memcpy(packet, g_rx, length);
        g_rx_ready = false;
        usbd_ept_recv(&g_boot_usb.dev, USBD_CDC_BULK_OUT_EPT, g_rx, packet_size());
    }
    boot_at32_irq_restore(mask);
    if (length != 0U)
    {
        bootloader_receive_bytes(bootloader, packet, length);
    }
}
