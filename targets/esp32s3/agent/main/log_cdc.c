#include "platform.h"

#include "sdkconfig.h"

#if CONFIG_AFFINE_AGENT_LOG_CDC
#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "tinyusb_cdc_acm.h"
#include "tusb.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "hal/usb_serial_jtag_ll.h"
#include "freertos/task.h"

/* Any task may log; only the core task drains to CDC 1, so the ring is the
 * one shared object and a spinlock guards it. */
static uint8_t ring[4096];
static size_t ring_head, ring_used;
static portMUX_TYPE ring_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t uart_vprintf;
static bool cdc_ready;
/* Set by the TinyUSB task, acted on by the core task. */
static volatile bool download_requested;

/* Test builds only: opening the log port at 1200 baud reboots into ROM
 * download mode (USB-Serial-JTAG), so the agent can be reflashed without the
 * BOOT button. The protocol interface has no such hook. */
static void line_coding_changed(int itf, cdcacm_event_t *event)
{
    (void)itf;
    if (event->line_coding_changed_data.p_line_coding->bit_rate == 1200U)
        download_requested = true;
}

static int log_vprintf(const char *format, va_list args)
{
    char line[192];
    va_list copy;
    va_copy(copy, args);
    const int length = vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    if (length > 0) {
        const size_t n = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        portENTER_CRITICAL(&ring_lock);
        if (n <= sizeof(ring) - ring_used) {
            for (size_t i = 0; i < n; ++i)
                ring[(ring_head + ring_used + i) % sizeof(ring)] = (uint8_t)line[i];
            ring_used += n;
        }
        portEXIT_CRITICAL(&ring_lock);
    }
    return uart_vprintf ? uart_vprintf(format, args) : length;
}

void affine_log_cdc_early_init(void)
{
    uart_vprintf = esp_log_set_vprintf(log_vprintf);
}

esp_err_t affine_log_cdc_init(void)
{
    const tinyusb_config_cdcacm_t cdc = {
        .cdc_port = TINYUSB_CDC_ACM_1,
        .callback_line_coding_changed = line_coding_changed,
    };
    const esp_err_t err = tinyusb_cdcacm_init(&cdc);
    cdc_ready = err == ESP_OK;
    return err;
}

void affine_log_cdc_poll(void)
{
    uint8_t chunk[256];
    size_t n;
    if (download_requested) {
        ESP_LOGW("affine", "1200 baud on the log port: rebooting into ROM download mode");
        /* The OTG routing of the internal PHY lives in RTC_CNTL_USB_CONF and
         * survives esp_restart(): detach, hand the PHY back to USB-Serial-JTAG
         * (what the ROM download mode talks through), then reboot. */
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        usb_serial_jtag_ll_phy_enable_external(false);
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
    }
    /* Hold lines in the ring until a host opens the port (DTR): TinyUSB drops
     * its TX FIFO on connect, which would lose the boot-time lines. */
    static int64_t connected_at;
    if (!cdc_ready || !tud_cdc_n_connected(TINYUSB_CDC_ACM_1)) {
        connected_at = 0;
        return;
    }
    /* Hosts set DTR and RTS in separate requests, and each line-state change
     * can flush the TX FIFO: let the open settle before writing. */
    if (!connected_at) connected_at = esp_timer_get_time();
    if (esp_timer_get_time() - connected_at < 200000) return;
    portENTER_CRITICAL(&ring_lock);
    n = ring_used < sizeof(chunk) ? ring_used : sizeof(chunk);
    for (size_t i = 0; i < n; ++i) chunk[i] = ring[(ring_head + i) % sizeof(ring)];
    portEXIT_CRITICAL(&ring_lock);
    if (!n) return;
    const size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, chunk, n);
    (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);
    portENTER_CRITICAL(&ring_lock);
    ring_head = (ring_head + queued) % sizeof(ring);
    ring_used -= queued;
    portEXIT_CRITICAL(&ring_lock);
}
#else
void affine_log_cdc_early_init(void) {}
esp_err_t affine_log_cdc_init(void) { return ESP_OK; }
void affine_log_cdc_poll(void) {}
#endif
