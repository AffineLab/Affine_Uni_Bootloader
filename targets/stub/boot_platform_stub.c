#include "boot_platform.h"

#include "targets/stm32f072/board_config.h"

const boot_target_config_t *boot_platform_target(void)
{
    return &g_target_config;
}

uint32_t boot_platform_millis(void)
{
    return 0U;
}

bool boot_platform_force_bootloader(void)
{
    return true;
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
}

void boot_platform_transport_poll(void)
{
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
    if (out_diag == NULL)
    {
        return false;
    }

    *out_diag = (boot_diag_response_t){0};
    return true;
}

void boot_platform_flash_unlock(void)
{
}

void boot_platform_flash_lock(void)
{
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    (void)address;
    (void)length;
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    (void)address;
    (void)data;
    (void)length;
    return BOOT_ERR_NONE;
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    (void)app_base;
    for (;;)
    {
    }
}

void boot_platform_reboot(void)
{
    for (;;)
    {
    }
}
