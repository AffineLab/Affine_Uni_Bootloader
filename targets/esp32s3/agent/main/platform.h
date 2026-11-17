#ifndef AFFINE_ESP_PLATFORM_H
#define AFFINE_ESP_PLATFORM_H
#include "bootloader.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t affine_platform_init(bootloader_t *ctx);
esp_err_t affine_usb_init(void);
/* Feeds at most one 256-byte chunk to the core, waiting up to wait_ticks for
 * bytes; true if it fed any. */
bool affine_usb_receive(bootloader_t *ctx, TickType_t wait_ticks);
bool affine_usb_tx_pending(void);
/* Test-build log mirror on CDC 1 (CONFIG_AFFINE_AGENT_LOG_CDC); no-ops otherwise. */
void affine_log_cdc_early_init(void);
esp_err_t affine_log_cdc_init(void);
void affine_log_cdc_poll(void);
#endif
