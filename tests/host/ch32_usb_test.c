#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "boot_platform.h"
#include "targets/ch32v203/usb.h"
#include "targets/ch32v203/usb_hw.h"

static bool ready = true, hardware_busy, fail_send;
static unsigned tx_packets, rx_arms, rx_bytes, init_count, deinit_count, mask;
static uint16_t lengths[32];
static uint8_t transmitted[2048], received[2048];
static unsigned tx_bytes;

uint32_t boot_platform_millis(void)
{
    return 123;
}

uint32_t boot_ch32_irq_save(void)
{
    unsigned old = mask;
    mask = 1;
    return old;
}

void boot_ch32_irq_restore(uint32_t old)
{
    assert(mask == 1);
    mask = old;
}

void boot_ch32_hw_init(void)
{
    init_count++;
}

void boot_ch32_hw_deinit(void)
{
    deinit_count++;
}

bool boot_ch32_hw_ready(void)
{
    return ready;
}

void boot_ch32_hw_rx_arm(void)
{
    rx_arms++;
}

bool boot_ch32_hw_send(const uint8_t *data, uint16_t len)
{
    assert(mask && !hardware_busy && len <= 64);
    if (fail_send)
    {
        return false;
    }
    assert(tx_packets < 32 && tx_bytes + len <= sizeof(transmitted));
    lengths[tx_packets++] = len;
    memcpy(transmitted + tx_bytes, data, len);
    tx_bytes += len;
    hardware_busy = true;
    return true;
}

void bootloader_receive_bytes(bootloader_t *boot, const uint8_t *data, size_t len)
{
    (void)boot;
    assert(!mask && rx_bytes + len <= sizeof(received));
    memcpy(received + rx_bytes, data, len);
    rx_bytes += len;
}

static void complete(void)
{
    assert(hardware_busy);
    hardware_busy = false;
    boot_ch32_usb_tx_done();
    boot_platform_transport_poll();
}

static void reset(void)
{
    hardware_busy = false;
    boot_ch32_usb_reset();
    boot_platform_transport_poll();
    tx_packets = tx_bytes = 0;
    memset(lengths, 0, sizeof(lengths));
}

int main(void)
{
    uint8_t frame[532];
    for (unsigned i = 0; i < sizeof(frame); i++)
    {
        frame[i] = (uint8_t)i;
    }
    boot_ch32_usb_init();
    assert(init_count == 1);
    for (unsigned size = 0; size <= 532; size += (size == 0 ? 64 : (size == 128 ? 404 : 64)))
    {
        reset();
        boot_platform_transport_send(frame, size);
        boot_platform_transport_poll();
        while (hardware_busy)
        {
            complete();
        }
        assert(tx_bytes == size && !memcmp(transmitted, frame, size));
        assert(tx_packets == size / 64 + 1);
        /* includes short packet or ZLP */
        assert(lengths[tx_packets - 1] == size % 64);
        boot_diag_response_t d;
        assert(boot_platform_transport_get_diag(&d));
        assert(d.tx_enqueue_count == 1 && d.tx_start_count == 1 && d.tx_complete_count == 1);
    }
    reset();
    ready = false;
    boot_platform_transport_send(frame, 20);
    boot_platform_transport_poll();
    assert(!hardware_busy);
    ready = true;
    boot_platform_transport_poll();
    assert(hardware_busy);
    complete();
    reset();
    fail_send = true;
    boot_platform_transport_send(frame, 128);
    boot_platform_transport_poll();
    assert(!hardware_busy);
    fail_send = false;
    boot_platform_transport_poll();
    assert(hardware_busy);
    complete();
    complete();
    complete();
    assert(tx_bytes == 128 && lengths[2] == 0);
    /* A second queued frame cannot overwrite the in-flight frame. */
    reset();
    boot_platform_transport_send(frame, 532);
    boot_platform_transport_send(frame + 1, 65);
    boot_platform_transport_poll();
    while (hardware_busy)
    {
        complete();
    }
    assert(tx_bytes == 597 && !memcmp(transmitted, frame, 532) && !memcmp(transmitted + 532, frame + 1, 65));
    reset();
    boot_platform_transport_send(frame, 128);
    boot_platform_transport_poll();
    reset();
    boot_platform_transport_poll();
    assert(!hardware_busy && !tx_packets);
    bootloader_t boot;
    for (unsigned i = 0; i < 532; i += 64)
    {
        unsigned len = 532 - i > 64 ? 64 : 532 - i;
        boot_ch32_usb_rx(frame + i, len);
        boot_ch32_usb_receive(&boot);
    }
    assert(rx_bytes == 532 && !memcmp(received, frame, 532) && rx_arms == 9);
    boot_ch32_usb_rx(frame, 0);
    boot_ch32_usb_receive(&boot);
    assert(rx_arms == 10 && rx_bytes == 532);
    boot_ch32_usb_rx(frame, 65);
    boot_ch32_usb_receive(&boot);
    assert(rx_bytes == 532);
    /* Discard an old packet on reset, preserve a new packet arriving before poll. */
    boot_ch32_usb_rx(frame, 1);
    boot_ch32_usb_reset();
    boot_ch32_usb_rx(frame + 1, 2);
    boot_platform_transport_poll();
    boot_ch32_usb_receive(&boot);
    assert(rx_bytes == 534 && !memcmp(received + 532, frame + 1, 2));
    boot_ch32_usb_deinit();
    assert(deinit_count == 1 && !mask);
    puts("ALL PASS: CH32 CDC 532-byte frames, packets/ZLP, retries, queue ownership, reset, RX bounds and rearm");
    return 0;
}
