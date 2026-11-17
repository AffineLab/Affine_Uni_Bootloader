/* Real adapter/descriptors + TinyUSB headers, with host-only I/O fakes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/dcd.h"
#include "device/usbd_pvt.h"
#include "targets/rp2350/usb.h"
#include "boot_platform.h"
static bool mounted = true, suspended, endpoint_busy;
static uint32_t space = CFG_TUD_CDC_TX_BUFSIZE, writes, tasks;
static uint8_t rx[1024], sent[1024], received[1024];
static uint32_t rx_length, rx_offset, received_length, sent_length;
uint32_t boot_platform_millis(void) { return 123U; }
bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return suspended; }
bool tud_rhport_init(uint8_t port, const tusb_rhport_init_t *init)
{ assert(port == 0 && init->role == TUSB_ROLE_DEVICE && init->speed == TUSB_SPEED_FULL); return true; }
void tud_task_ext(uint32_t timeout, bool isr) { (void)timeout; assert(!isr); ++tasks; }
bool usbd_edpt_busy(uint8_t port, uint8_t ep)
{ assert(port == 0 && ep == 0x81); return endpoint_busy; }
uint32_t tud_cdc_n_write_available(uint8_t itf) { assert(itf == 0); return space; }
uint32_t tud_cdc_n_write(uint8_t itf, const void *data, uint32_t length)
{
    assert(itf == 0 && length <= space && length <= sizeof(sent));
    memcpy(sent, data, length); sent_length = length; space -= length; ++writes; return length;
}
uint32_t tud_cdc_n_write_flush(uint8_t itf) { assert(itf == 0); endpoint_busy = true; return sent_length; }
uint32_t tud_cdc_n_available(uint8_t itf) { assert(itf == 0); return rx_length - rx_offset; }
uint32_t tud_cdc_n_read(uint8_t itf, void *out, uint32_t size)
{
    assert(itf == 0);
    uint32_t take = rx_length - rx_offset;
    if (take > size) take = size;
    memcpy(out, rx + rx_offset, take); rx_offset += take; return take;
}
void bootloader_receive_bytes(bootloader_t *ctx, const uint8_t *data, size_t length)
{
    (void)ctx; assert(length <= 64 && received_length + length <= sizeof(received));
    memcpy(received + received_length, data, length); received_length += length;
}
static boot_diag_response_t diag(void)
{ boot_diag_response_t out; assert(boot_platform_transport_get_diag(&out)); return out; }
static void reset(uint32_t event)
{
    tud_event_hook_cb(0, event, true);
    rx_length = rx_offset = 0; space = CFG_TUD_CDC_TX_BUFSIZE; endpoint_busy = false;
    boot_rp2350_usb_task();
}
int main(void)
{
    boot_rp2350_usb_init();
    const tusb_desc_device_t *dev = (const void *)tud_descriptor_device_cb();
    assert(dev->idVendor == 0xAFF1 && dev->idProduct == 0x52B4 && dev->bMaxPacketSize0 == 64);
    const uint8_t *config = tud_descriptor_configuration_cb(0);
    unsigned total = config[2] | (unsigned)config[3] << 8, bulk = 0, notify = 0;
    assert(total == TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN && config[4] == 2);
    for (unsigned i = 0; i < total; i += config[i])
    {
        assert(config[i] && i + config[i] <= total);
        if (config[i+1] == TUSB_DESC_ENDPOINT)
        {
            if (config[i+3] == TUSB_XFER_BULK)
            { assert(config[i+4] == 64 && config[i+5] == 0); ++bulk; }
            else { assert(config[i+2] == 0x82); ++notify; }
        }
    }
    assert(bulk == 2 && notify == 1 && tud_descriptor_configuration_cb(1) == NULL);
    assert(tud_descriptor_string_cb(0, 0)[1] == 0x0409);
    assert(tud_descriptor_string_cb(2, 0)[0] >> 8 == TUSB_DESC_STRING);
    assert(tud_descriptor_string_cb(255, 0) == NULL);
    uint8_t frame[532];
    for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
    memcpy(rx, frame, sizeof(frame)); rx_length = sizeof(frame);
    boot_rp2350_usb_receive(NULL);
    assert(received_length == sizeof(frame) && memcmp(received, frame, sizeof(frame)) == 0);
    assert(diag().rx_byte_count == sizeof(frame) && diag().rx_callback_count == 9);
    space = 10;
    boot_platform_transport_send(frame, sizeof(frame)); boot_platform_transport_poll(); assert(writes == 0);
    space = CFG_TUD_CDC_TX_BUFSIZE; boot_platform_transport_poll();
    assert(writes == 1 && sent_length == sizeof(frame) && memcmp(sent, frame, sizeof(frame)) == 0);
    boot_platform_transport_send(frame, 64);
    /* FIFO empty while the final packet/ZLP is active must not retire a frame. */
    space = CFG_TUD_CDC_TX_BUFSIZE; boot_platform_transport_poll();
    assert(writes == 1 && diag().tx_complete_count == 0);
    endpoint_busy = false; boot_platform_transport_poll();
    assert(writes == 2 && sent_length == 64 && diag().tx_complete_count == 1);
    space = CFG_TUD_CDC_TX_BUFSIZE; boot_platform_transport_poll(); assert(diag().tx_complete_count == 1);
    endpoint_busy = false; boot_platform_transport_poll(); assert(diag().tx_complete_count == 2);
    suspended = true;
    boot_platform_transport_send(frame, 20); boot_platform_transport_poll(); assert(writes == 2);
    suspended = false; mounted = false; boot_platform_transport_poll(); assert(writes == 2);
    mounted = true; boot_platform_transport_poll(); assert(writes == 3);
    reset(DCD_EVENT_BUS_RESET); assert(diag().tx_enqueue_count == 0 && diag().tx_complete_count == 0);
    for (unsigned i = 0; i < 9; ++i) boot_platform_transport_send(frame, 20);
    assert(diag().tx_enqueue_count == 8 && diag().tx_queue_drop_count == 1);
    reset(DCD_EVENT_UNPLUGGED);
    boot_platform_transport_send(frame, sizeof(frame) + 1);
    assert(diag().tx_queue_drop_count == 1 && tasks > 0);
    puts("ALL PASS: RP2350 CDC descriptors, 532-byte frames, backpressure, ZLP lifetime, reset, suspend, queue bounds");
    return 0;
}
