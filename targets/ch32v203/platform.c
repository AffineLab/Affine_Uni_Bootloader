#include "boot_platform.h"
#include "board_config.h"
#include "app_entry.h"
#include "ch32v20x.h"
#include "usb.h"
#include "flash_hw.h"
#include "targets/common/boot_jump_riscv.h"

void boot_ch32_jump_prepare(void);

void boot_ch32_flash_modekey_write(uint32_t key)
{
    FLASH->MODEKEYR = key;
}

bool boot_platform_force_bootloader(void)
{
    const bool result = boot_ch32_request_consume((volatile uint32_t *)BOOT_CH32_REQUEST_ADDRESS);
    __asm volatile("fence rw, rw" : : : "memory");
    return result;
}

/* Reload only; never start or reconfigure an inherited watchdog. */
void boot_platform_watchdog_kick(void)
{
    IWDG->CTLR = 0xAAAAU;
}

void boot_platform_reboot(void)
{
    /*
     * A system reset with USBD still active left the next boot hung in USB
     * bring-up on a C8T6 board, so detach and reset USB first, as for the jump.
     */
    __disable_irq();
    boot_ch32_usb_deinit();
    NVIC_SystemReset();
    for (;;)
    {
    }
}

void boot_platform_jump_to_app(uint32_t base)
{
    const uint32_t words[2] =
    {
        *(volatile uint32_t *)base, *(volatile uint32_t *)(base + 4U)
    };
    boot_ch32_app_entry_t jump;
    if (!boot_ch32_entry_prepare(&g_target_config, base, words, &jump))
    {
        return;
    }
    boot_ch32_jump_prepare();
    boot_jump_riscv(jump.entry, jump.mtvec);
}
