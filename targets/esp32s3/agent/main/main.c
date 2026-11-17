#include "platform.h"
#include "boot_platform.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bootloader_t core;

/* Block on the RX stream buffer (one tick at most) rather than sleep: the
 * task wakes as soon as the host's next frame arrives. Answers still queued
 * for TX are retried without waiting. */
static void service(void)
{
    (void)affine_usb_receive(&core, affine_usb_tx_pending() ? 0 : 1);
    bootloader_poll(&core);
}

void app_main(void)
{
    affine_log_cdc_early_init();
    esp_err_t err = affine_platform_init(&core);
    if (err != ESP_OK) {
        ESP_LOGE("affine", "platform init failed: %s", esp_err_to_name(err));
        return;
    }
    bootloader_init(&core);
    err = affine_usb_init();
    if (err != ESP_OK) {
        ESP_LOGE("affine", "USB init failed: %s", esp_err_to_name(err));
        /* Keep the watchdog serviced without repeated restart/boot attempts. */
        for (;;) { boot_platform_watchdog_kick(); vTaskDelay(1); }
    }
    const uint32_t start = boot_platform_millis();
    while ((uint32_t)(boot_platform_millis() - start) < 1000U && core.frame_ok_count == 0) service();
    if (core.frame_ok_count == 0) bootloader_try_boot_app(&core);
    for (;;) service();
}
