#include "usb.h"
#include <string.h>
#include "tusb.h"
#include "device/dcd.h"
#include "device/usbd_pvt.h"
#include "targets/common/boot_target_common.h"

static boot_target_usb_transport_t g_transport;
static bool g_tx_active;
static uint16_t g_tx_length;
/* Hook runs in IRQ context; all queue/FIFO work stays on core 0's main loop. */
static volatile bool g_reset_pending;

void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    (void)rhport; (void)in_isr;
    if (eventid == DCD_EVENT_BUS_RESET || eventid == DCD_EVENT_UNPLUGGED)
        g_reset_pending = true;
}

void boot_rp2350_usb_task(void)
{
    if (g_reset_pending)
    {
        g_reset_pending = false;
        memset(&g_transport, 0, sizeof(g_transport));
        g_tx_active = false;
    }
    tud_task();
}

void boot_rp2350_usb_init(void)
{
    tud_init(0);
}

static uint8_t tx_busy(uint8_t channel)
{
    (void)channel;
    if (!tud_ready() || g_reset_pending) return 1U;
    if (g_tx_active)
    {
        /* FIFO empty alone is insufficient: the final packet and its ZLP
         * must also have completed. TinyUSB owns segmentation and ZLPs. */
        if (tud_cdc_write_available() != CFG_TUD_CDC_TX_BUFSIZE ||
            usbd_edpt_busy(0, BOOT_RP2350_CDC_IN)) return 1U;
        boot_target_usb_transport_note_tx_complete(&g_transport, g_tx_length, boot_platform_millis());
        g_tx_active = false;
    }
    return 0U;
}

static uint8_t transmit(uint8_t channel, uint8_t *data, uint16_t length)
{
    if (tx_busy(channel) || tud_cdc_write_available() < length) return 1U;
    if (tud_cdc_write(data, length) != length) return 1U;
    g_tx_length = length;
    g_tx_active = true;
    (void)tud_cdc_write_flush();
    return 0U;
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    boot_target_usb_transport_send(&g_transport, data, length);
}

void boot_platform_transport_poll(void)
{
    boot_rp2350_usb_task();
    if (g_reset_pending) return;
    boot_target_usb_transport_poll(&g_transport, tx_busy, transmit, 0U, boot_platform_millis());
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out)
{
    return boot_target_usb_transport_get_diag(&g_transport, out);
}

void boot_rp2350_usb_receive(bootloader_t *ctx)
{
    uint8_t packet[64];
    boot_rp2350_usb_task();
    while (!g_reset_pending && tud_ready() && tud_cdc_available())
    {
        uint32_t length = tud_cdc_read(packet, sizeof(packet));
        if (length == 0U) break;
        boot_target_usb_transport_note_rx(&g_transport, length, boot_platform_millis());
        bootloader_receive_bytes(ctx, packet, length);
    }
}
