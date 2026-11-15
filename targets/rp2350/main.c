#include "bootloader.h"
#include "boot_platform.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "usb.h"
static bootloader_t g_bootloader;
uint32_t boot_platform_millis(void) { return to_ms_since_boot(get_absolute_time()); }
int main(void)
{
    watchdog_enable(8000U, true);
    bootloader_init(&g_bootloader);
    boot_rp2350_usb_init();
    uint32_t start = boot_platform_millis();
    while ((uint32_t)(boot_platform_millis() - start) < 1000U && g_bootloader.frame_ok_count == 0U)
    {
        boot_rp2350_usb_receive(&g_bootloader);
        bootloader_poll(&g_bootloader);
    }
    if (g_bootloader.frame_ok_count == 0U) bootloader_try_boot_app(&g_bootloader);
    for (;;)
    {
        boot_rp2350_usb_receive(&g_bootloader);
        bootloader_poll(&g_bootloader);
    }
}
