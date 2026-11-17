#include "affine_esp_boot.h"

#include <stdint.h>
#include "esp_ota_ops.h"
#include "esp_system.h"

#define AFFINE_REQUEST_MAGIC UINT32_C(0x41465333)
/* Same NOLOAD semantics as RTC_NOINIT_ATTR, with a fixed linker address rather
 * than a position dependent on the other RTC variables in each image. */
static volatile uint32_t request[2]
    __attribute__((section(".affine_rtc_request"), used, aligned(4)));

bool affine_esp_take_boot_request(void)
{
    const bool requested = request[0] == AFFINE_REQUEST_MAGIC &&
        request[1] == ~AFFINE_REQUEST_MAGIC;
    request[0] = 0;
    request[1] = 0;
    return requested;
}

esp_err_t affine_esp_enter_bootloader(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) return ESP_ERR_NOT_FOUND;
    request[1] = ~AFFINE_REQUEST_MAGIC;
    request[0] = AFFINE_REQUEST_MAGIC;
    __asm__ volatile ("memw" ::: "memory");
    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        request[0] = 0;
        request[1] = 0;
        return err;
    }
    esp_restart();
    return ESP_FAIL;
}
