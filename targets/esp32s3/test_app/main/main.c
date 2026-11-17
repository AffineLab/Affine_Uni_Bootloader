#include "affine_esp_boot.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    ESP_LOGI("affine-test", "Affine ESP32-S3 test app version %s",
             esp_app_get_description()->version);
#if CONFIG_AFFINE_TEST_CRASH
    ESP_LOGW("affine-test", "Deliberate reset before marking valid: expect factory rollback");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
#else
    ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    ESP_LOGI("affine-test", "App confirmed; returning to agent in 3 seconds");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_err_t err = affine_esp_enter_bootloader();
    ESP_LOGE("affine-test", "Agent re-entry failed: %s", esp_err_to_name(err));
#endif
}
