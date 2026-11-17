#include "platform.h"
#include "affine_esp_boot.h"
#include "boot_platform.h"
#include "boot_target_ids.h"

#include <inttypes.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_flash_mmap.h"

#define MAP_PHYSICAL 0x110000U
#define MAP_SIZE 0x410000U
#define APP_PHYSICAL 0x120000U
#define APP_SIZE 0x400000U
#define ERASE_SIZE 4096U

static const char *TAG = "affine";
static boot_target_config_t target;
static spi_flash_mmap_handle_t map_handle;
static const esp_partition_t *app;
static bootloader_t *core;
static bool unlocked;
static bool wdt_subscribed;

esp_err_t affine_platform_init(bootloader_t *ctx)
{
    const void *mapping;
    const esp_partition_t *meta = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x40, "affmeta");
    const esp_partition_t *running = esp_ota_get_running_partition();
    app = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                  ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!meta || !app || !running || running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY ||
        running->address != 0x10000U || running->size != 0x100000U ||
        meta->address != MAP_PHYSICAL || meta->size != 0x10000U ||
        app->address != APP_PHYSICAL || app->size != APP_SIZE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = spi_flash_mmap(MAP_PHYSICAL, MAP_SIZE, SPI_FLASH_MMAP_DATA,
                                  &mapping, &map_handle);
    if (err != ESP_OK) return err;
    target = (boot_target_config_t) {
        .flash_base = (uint32_t)(uintptr_t)mapping, .flash_size = MAP_SIZE,
        .metadata_base = (uint32_t)(uintptr_t)mapping, .metadata_size = 0x2000U,
        .app_base = (uint32_t)(uintptr_t)mapping + (APP_PHYSICAL - MAP_PHYSICAL),
        .app_size = APP_SIZE, .erase_size = ERASE_SIZE, .write_size = 4U,
        .max_chunk_size = 256U,
        /* ESP images have segments, not ARM vectors; SRAM bounds are unused. */
        .sram_base = 0, .sram_size = 0,
        .target_id = BOOT_TARGET_ID_ESP32S3, .board_id = BOOT_BOARD_ID_ES3_WEACT_N16R8,
        .board_revision = 1, .flash_layout_id = BOOT_LAYOUT_ID_ES3_A,
    };
    core = ctx;
    /* Main task owns the core. Subscribe only if the startup-enabled TWDT is
     * present; yield as well so the watched idle tasks can run. */
    if (esp_task_wdt_status(NULL) == ESP_ERR_NOT_FOUND)
        wdt_subscribed = esp_task_wdt_add(NULL) == ESP_OK;
    else
        wdt_subscribed = esp_task_wdt_status(NULL) == ESP_OK;
    ESP_LOGI(TAG, "reset=%d map=%08" PRIx32 " app=%08" PRIx32,
             esp_reset_reason(), target.flash_base, target.app_base);
    return ESP_OK;
}

const boot_target_config_t *boot_platform_target(void) { return &target; }
uint32_t boot_platform_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool boot_platform_force_bootloader(void)
{
    const bool requested = affine_esp_take_boot_request();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t err = esp_ota_get_state_partition(app, &state);
    const bool rolled_back = err == ESP_OK &&
        (state == ESP_OTA_IMG_ABORTED || state == ESP_OTA_IMG_INVALID);
    ESP_LOGI(TAG, "request=%d ota_0 state=%d state_result=%s forced=%d",
             requested, state, esp_err_to_name(err), requested || rolled_back);
    return requested || rolled_back;
}

void boot_platform_watchdog_kick(void)
{
    static TickType_t last_yield;
    if (wdt_subscribed) (void)esp_task_wdt_reset();
    /* Avoid sleeping on every 1 KiB of CRC/SHA. Yield at least once per 10 ms
     * (or tick on a slower tick clock), including within long erase loops. */
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - last_yield) >= (pdMS_TO_TICKS(10) ?: 1)) {
        vTaskDelay(1);
        last_yield = xTaskGetTickCount();
    }
}

void boot_platform_flash_unlock(void) { unlocked = true; }
void boot_platform_flash_lock(void) { unlocked = false; }
uint32_t boot_platform_flash_ecc_error_count(void) { return 0U; }

static bool translate(uint32_t address, uint32_t length, uint32_t *physical)
{
    if (!unlocked || address < target.flash_base || length == 0 ||
        address - target.flash_base >= MAP_SIZE ||
        length > MAP_SIZE - (address - target.flash_base)) return false;
    *physical = MAP_PHYSICAL + address - target.flash_base;
    return true;
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    uint32_t physical;
    if (!translate(address, length, &physical)) return BOOT_ERR_RANGE;
    /* The core hands over its last unit shorter than erase_size (image size,
     * not sector size): erase every sector the range touches, as the other
     * targets do. The mapping starts on a sector boundary, so rounding stays
     * inside it. */
    const uint32_t head = physical % ERASE_SIZE;
    address -= head;
    length = ((length + head + ERASE_SIZE - 1U) / ERASE_SIZE) * ERASE_SIZE;
    if (!translate(address, length, &physical)) return BOOT_ERR_RANGE;
    /* esp_flash_api.c: flash_end_flush_cache -> memspi_host_flush_cache ->
     * spi_flash_check_and_flush_cache invalidates mapped MMU pages on S3. */
    for (uint32_t off = 0; off < length; off += ERASE_SIZE) {
        boot_platform_watchdog_kick();
        if (esp_flash_erase_region(NULL, physical + off, ERASE_SIZE) != ESP_OK)
            return BOOT_ERR_FLASH;
        const volatile uint32_t *words = (const volatile uint32_t *)(uintptr_t)(address + off);
        for (uint32_t i = 0; i < ERASE_SIZE / sizeof(*words); ++i)
            if (words[i] != UINT32_MAX) return BOOT_ERR_FLASH;
    }
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t physical;
    if (!data || !translate(address, length, &physical)) return BOOT_ERR_RANGE;
    if ((physical % 4U) || (length % 4U)) return BOOT_ERR_ALIGNMENT;
    boot_platform_watchdog_kick();
    if (esp_flash_write(NULL, data, physical, length) != ESP_OK) return BOOT_ERR_FLASH;
    /* Also fail the operation if the DATA mapping doesn't read back new bytes. */
    return memcmp((const void *)(uintptr_t)address, data, length) == 0
        ? BOOT_ERR_NONE : BOOT_ERR_FLASH;
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    esp_err_t err = app_base == target.app_base
        ? esp_ota_set_boot_partition(app) : ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "BOOT_APP select result=%s", esp_err_to_name(err));
    if (err == ESP_OK) esp_restart();
    /* The platform ABI returns void. Publish the deferred IDF verification
     * failure through the owning core's next GET_STATUS; never retry in a loop. */
    core->last_error = BOOT_ERR_NO_VALID_APP;
    core->status = BOOT_STATUS_ERROR;
}

void boot_platform_reboot(void) { esp_restart(); }
