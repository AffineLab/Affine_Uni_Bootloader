#include "bootloader.h"

static bootloader_t g_bootloader;

int main(void)
{
    bootloader_init(&g_bootloader);
    bootloader_try_boot_app(&g_bootloader);

    for (;;)
    {
        bootloader_poll(&g_bootloader);
    }
}
