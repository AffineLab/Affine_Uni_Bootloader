#ifndef AFFINE_BOOT_PLATFORM_H
#define AFFINE_BOOT_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_image.h"
#include "boot_protocol.h"

typedef struct
{
    const boot_target_config_t *target;
    uint32_t boot_timeout_ms;
} boot_platform_context_t;

const boot_target_config_t *boot_platform_target(void);
uint32_t boot_platform_millis(void);
bool boot_platform_force_bootloader(void);
void boot_platform_transport_send(const uint8_t *data, size_t length);
void boot_platform_transport_poll(void);
bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag);
void boot_platform_flash_unlock(void);
void boot_platform_flash_lock(void);
boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length);
boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length);
void boot_platform_jump_to_app(uint32_t app_base);
void boot_platform_reboot(void);

#endif
