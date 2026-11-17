#include "boot_platform.h"
#include "board_config.h"
#include "tusb.h"
#include "hardware/resets.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/nvic.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"
#include "targets/common/boot_jump.h"

bool boot_platform_force_bootloader(void)
{
    bool requested = watchdog_hw->scratch[BOOT_RP2350_REQUEST_SCRATCH] == BOOT_RP2350_REQUEST_MAGIC;
    watchdog_hw->scratch[BOOT_RP2350_REQUEST_SCRATCH] = 0U;
    return requested;
}

void boot_platform_watchdog_kick(void)
{
    /* This target owns its watchdog: main enables a known 8-second timeout.
     * No SDK-private reload value from a previous app is assumed. */
    watchdog_update();
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    const uint32_t *vectors = (const uint32_t *)(uintptr_t)app_base;
    uint32_t stack = vectors[0], entry = vectors[1];
    (void)save_and_disable_interrupts();
    tud_disconnect();
    (void)tud_deinit(0);
    reset_block_mask(1U << RESET_USBCTRL);
    systick_hw->csr = 0U;
    systick_hw->rvr = 0U;
    systick_hw->cvr = 0U;
    timer_hw_t *const timers[] = {timer0_hw, timer1_hw};
    for (unsigned i = 0; i < 2U; ++i)
    {
        timers[i]->inte = 0U;
        timers[i]->intf = 0U;
        timers[i]->armed = 15U;
        timers[i]->intr = 15U;
    }
    for (unsigned i = 0; i < sizeof(nvic_hw->icer) / sizeof(nvic_hw->icer[0]); ++i)
    {
        nvic_hw->icer[i] = UINT32_MAX;
        nvic_hw->icpr[i] = UINT32_MAX;
    }
    scb_hw->icsr = M33_ICSR_PENDSVCLR_BITS | M33_ICSR_PENDSTCLR_BITS;
    /* App runtime safely switches sys/ref sources before PLL init, then
     * resets/unresets peripherals. Do not disturb XIP or reset live PLLs. */
    watchdog_disable();
    __asm volatile("msr msplim, %0\n\tmsr psplim, %0" : : "r"(0U) : "memory");
    scb_hw->vtor = app_base;
    __dsb();
    __isb();
    /* SDK runtime_init_per_core_irq_priorities enables interrupts. */
    boot_jump_with_stack(stack, entry);
}

void boot_platform_reboot(void)
{
    watchdog_reboot(0U, 0U, 0U);
    for (;;) __wfi();
}
