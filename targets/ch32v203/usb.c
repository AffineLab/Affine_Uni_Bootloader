#include "usb.h"
#include "usb_hw.h"
#include "targets/common/boot_target_common.h"
#include <string.h>

static boot_target_usb_transport_t transport;
static uint8_t rx[64];
static volatile uint16_t rx_len;
static volatile bool rx_pending, reset_pending, packet_done;
static const uint8_t *tx_data;
static uint16_t tx_left;
static bool tx_busy, zlp;

void boot_ch32_usb_reset(void)
{
    rx_pending = false;
    reset_pending = true;
}

void boot_ch32_usb_tx_done(void)
{
    packet_done = true;
}

void boot_ch32_usb_rx(const uint8_t *data, uint16_t len)
{
    if (len <= sizeof(rx)&& !rx_pending)
    {
        memcpy(rx, data, len);
        rx_len = len;
        rx_pending = true;
    }
}

void boot_ch32_usb_init(void)
{
    boot_ch32_hw_init();
}

void boot_ch32_usb_deinit(void)
{
    boot_ch32_hw_deinit();
}

static void send_next(void)
{
    uint16_t len = tx_left > 64U ? 64U : tx_left;
    packet_done = false;
    if (boot_ch32_hw_send(tx_data, len))
    {
        tx_data += len;
        tx_left -= len;
        if (!len)
        {
            zlp = false;
        }
    }
    else
    {
        packet_done = true;
    }
}

static uint8_t busy(uint8_t ch)
{
    (void)ch;
    return tx_busy;
}

static uint8_t transmit(uint8_t ch, uint8_t *data, uint16_t len)
{
    (void)ch;
    if (tx_busy || !boot_ch32_hw_ready())
    {
        return 1;
    }
    tx_data = data;
    tx_left = len;
    tx_busy = true;
    zlp = (len % 64U) == 0U;
    send_next();
    return 0;
}

void boot_platform_transport_send(const uint8_t *data, size_t len)
{
    boot_target_usb_transport_send(&transport, data, len);
}

bool boot_platform_transport_get_diag(boot_diag_response_t *diag)
{
    return boot_target_usb_transport_get_diag(&transport, diag);
}

void boot_platform_transport_poll(void)
{
    uint32_t irq = boot_ch32_irq_save();
    if (reset_pending)
    {
        reset_pending = false;
        tx_busy = false;
        packet_done = false;
        memset(&transport, 0, sizeof(transport));
    }
    if (tx_busy && packet_done && boot_ch32_hw_ready())
    {
        packet_done = false;
        if (tx_left || zlp)
        {
            send_next();
        }
        else
        {
            tx_busy = false;
            boot_target_usb_transport_note_tx_complete(&transport, transport.diag.tx_last_len,
                boot_platform_millis());
        }
    }
    boot_target_usb_transport_poll(&transport, busy, transmit, 0, boot_platform_millis());
    boot_ch32_irq_restore(irq);
}

void boot_ch32_usb_receive(bootloader_t *boot)
{
    uint8_t packet[64];
    uint16_t len = 0;
    uint32_t irq = boot_ch32_irq_save();
    if (rx_pending && !reset_pending)
    {
        len = rx_len;
        memcpy(packet, rx, len);
        rx_pending = false;
        boot_ch32_hw_rx_arm();
        boot_target_usb_transport_note_rx(&transport, len, boot_platform_millis());
    }
    boot_ch32_irq_restore(irq);
    if (len)
    {
        bootloader_receive_bytes(boot, packet, len);
    }
}
