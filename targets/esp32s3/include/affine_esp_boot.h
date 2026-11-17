#ifndef AFFINE_ESP_BOOT_H
#define AFFINE_ESP_BOOT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Link the affine_esp_boot component in BOTH images to reserve the same NOLOAD
 * RTC slow-memory mailbox at 0x50001ff8. Returns only on failure. */
esp_err_t affine_esp_enter_bootloader(void);
bool affine_esp_take_boot_request(void);

#ifdef __cplusplus
}
#endif
#endif
