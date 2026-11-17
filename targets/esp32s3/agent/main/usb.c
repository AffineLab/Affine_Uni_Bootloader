#include "platform.h"
#include "boot_platform.h"

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

static StreamBufferHandle_t rx;
static StaticStreamBuffer_t rx_control;
static uint8_t rx_storage[8193];
static portMUX_TYPE diag_lock = portMUX_INITIALIZER_UNLOCKED;
static boot_diag_response_t diag;

typedef struct {
    size_t length;
    size_t sent;
    uint8_t bytes[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
} tx_packet_t;
static tx_packet_t tx[8];
/* Only the core task touches the TX queue. TinyUSB callbacks only enqueue RX. */
static unsigned tx_head, tx_tail, tx_count;

static void receive_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t bytes[256];
    size_t length;
    uint32_t received = 0;
    portENTER_CRITICAL(&diag_lock);
    ++diag.rx_callback_count;
    portEXIT_CRITICAL(&diag_lock);
    /* esp_tinyusb invokes this in its task, not an interrupt. Never call the
     * shared core, flash routines, or blocking transmit from a USB callback. */
    while (tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, bytes, sizeof(bytes), &length) == ESP_OK && length) {
        const size_t accepted = xStreamBufferSend(rx, bytes, length, 0);
        portENTER_CRITICAL(&diag_lock);
        diag.rx_byte_count += length;
        received += length;
        diag.rx_last_len = received;
        diag.last_rx_tick_ms = boot_platform_millis();
        if (accepted != length) ++diag.rx_fifo_overflow_count;
        portEXIT_CRITICAL(&diag_lock);
    }
}

esp_err_t affine_usb_init(void)
{
    rx = xStreamBufferCreateStatic(sizeof(rx_storage), 1, rx_storage, &rx_control);
    if (!rx) return ESP_ERR_NO_MEM;
    /* 3.0.1's default descriptors use the Kconfig VID/PID/strings, one CDC
     * interface, and a twelve-hex-digit serial derived from ESP_MAC_BASE. */
    const tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) return err;
    const tinyusb_config_cdcacm_t cdc = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = receive_callback,
    };
    err = tinyusb_cdcacm_init(&cdc);
    if (err != ESP_OK) return err;
    return affine_log_cdc_init();
}

bool affine_usb_receive(bootloader_t *ctx, TickType_t wait_ticks)
{
    uint8_t bytes[256];
    /* Bound each turn so a continuously transmitting host cannot starve poll. */
    const size_t length = xStreamBufferReceive(rx, bytes, sizeof(bytes), wait_ticks);
    if (length) bootloader_receive_bytes(ctx, bytes, length);
    return length != 0;
}

static void log_response(const uint8_t *data, size_t length)
{
    boot_frame_header_t header;
    boot_status_response_t status;
    if (!data || length < sizeof(header) + sizeof(status)) return;
    memcpy(&header, data, sizeof(header));
    if (header.opcode != (BOOT_OP_BEGIN | BOOT_OP_RESPONSE) &&
        header.opcode != (BOOT_OP_COMMIT | BOOT_OP_RESPONSE) &&
        header.opcode != (BOOT_OP_BOOT_APP | BOOT_OP_RESPONSE)) return;
    memcpy(&status, data + sizeof(header), sizeof(status));
    ESP_LOGI("affine", "%s status=%" PRIu32 " error=%" PRIu32 " written=%" PRIu32,
             header.opcode == (BOOT_OP_BEGIN | BOOT_OP_RESPONSE) ? "BEGIN" :
             header.opcode == (BOOT_OP_COMMIT | BOOT_OP_RESPONSE) ? "COMMIT" : "BOOT_APP",
             status.status, status.last_error, status.written_size);
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    log_response(data, length);
    portENTER_CRITICAL(&diag_lock);
    if (!data || length > sizeof(tx[0].bytes) || tx_count == 8) {
        ++diag.tx_queue_drop_count;
        portEXIT_CRITICAL(&diag_lock);
        return;
    }
    ++diag.tx_enqueue_count;
    portEXIT_CRITICAL(&diag_lock);
    tx[tx_head].length = length;
    tx[tx_head].sent = 0;
    memcpy(tx[tx_head].bytes, data, length);
    tx_head = (tx_head + 1) % 8;
    ++tx_count;
}

void boot_platform_transport_poll(void)
{
    affine_log_cdc_poll();
    if (!tx_count) return;
    tx_packet_t *packet = &tx[tx_tail];
    const size_t previous = packet->sent;
    packet->sent += tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
        packet->bytes + packet->sent, packet->length - packet->sent);
    esp_err_t err = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    portENTER_CRITICAL(&diag_lock);
    if (!previous && packet->sent) {
        ++diag.tx_start_count;
        diag.tx_last_len = packet->length;
        diag.last_tx_start_tick_ms = boot_platform_millis();
    }
    if (packet->sent == packet->length && err == ESP_OK) {
        ++diag.tx_complete_count;
        diag.last_tx_complete_tick_ms = boot_platform_millis();
        tx_tail = (tx_tail + 1) % 8;
        --tx_count;
    } else {
        ++diag.tx_busy_reject_count;
    }
    portEXIT_CRITICAL(&diag_lock);
}

bool affine_usb_tx_pending(void) { return tx_count != 0; }

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
    if (!out_diag) return false;
    portENTER_CRITICAL(&diag_lock);
    *out_diag = diag;
    portEXIT_CRITICAL(&diag_lock);
    return true;
}
