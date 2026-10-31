/*
 * Boot guard for boards that cannot be rescued with a button or a debugger
 * (Monica Lite r1: NRST tied to the supply, BOOT0 pulled down, no SWD pads).
 *
 * The guard lives in the first flash page together with the vector table and
 * the reset code. It runs before anything else and checks that the rest of
 * the bootloader is complete. If it is not, it starts the MCU's own USB
 * loader in system memory instead of running into half-written code.
 *
 * That makes installing or replacing the bootloader restartable: write the
 * first page first, then the rest. Power lost at any point after the first
 * page leaves a board that comes up in the ROM loader and can be written
 * again. Only the write of the first page itself is unprotected.
 *
 * Nothing here may use code or data outside the first page, or any RAM other
 * than the stack: .data and .bss are not set up yet.
 */
#include <stdint.h>

#include "stm32f0xx.h"

#include "boot_protocol.h"

#define BOOT_GUARD_MAGIC           0x44524147UL /* "GARD" */
#define BOOT_GUARD_RECORD_ADDRESS  0x080000C0UL /* right behind the 48-word vector table */
#define BOOT_GUARD_FLASH_END       0x08006000UL /* end of the bootloader region */
#define BOOT_GUARD_ROM_LOADER      0x1FFFC800UL /* STM32F072 system memory */

/* Shares the RAM word of the bootloader request (above the bootloader's RAM). */
#define BOOT_GUARD_REQUEST_ADDRESS (0x20000000UL + 0x3F00UL)
#define BOOT_GUARD_ROM_REQUEST     0x55444621UL /* "!FDU" */

typedef struct
{
    uint32_t magic;
    uint32_t body_length; /* bytes from _guard_end to the end of the image; 0 = not stamped, no check */
    uint32_t body_sum;
    uint32_t protocol_version; /* for the application: which bootloader started it */
} boot_guard_record_t;

extern const uint32_t _guard_end[];

/* Stamped after the build by tools/patch_boot_guard.py. */
__attribute__((section(".guard_info"), used))
const boot_guard_record_t g_boot_guard_record = {BOOT_GUARD_MAGIC, 0U, 0U, BOOT_PROTOCOL_VERSION};

__attribute__((section(".guard"), noinline, noreturn))
static void boot_guard_enter_rom_loader(void)
{
    const uint32_t *loader = (const uint32_t *)BOOT_GUARD_ROM_LOADER;

    /* Cortex-M0 has no vector table register: map system memory at address 0. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;
    (void)RCC->APB2ENR;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | SYSCFG_CFGR1_MEM_MODE_0;
    (void)SYSCFG->CFGR1;

    __set_MSP(loader[0]);
    ((void (*)(void))loader[1])();

    for (;;)
    {
    }
}

__attribute__((section(".guard"), noinline, used))
void boot_guard(void)
{
    /* Read through a volatile pointer: the values are stamped into the binary
     * after linking and must not be folded in at compile time. */
    const volatile boot_guard_record_t *record = (const volatile boot_guard_record_t *)BOOT_GUARD_RECORD_ADDRESS;
    const uint32_t *word = _guard_end;
    uint32_t length = record->body_length;
    uint32_t sum = 0U;

    /*
     * The ROM loader starts a freshly written image by jumping to it: its USB
     * device is still attached, its interrupt still enabled and system memory
     * still mapped at address 0, where the vectors are fetched from. Undo
     * that; after a real reset none of this changes anything.
     */
    NVIC->ICER[0] = 0xFFFFFFFFUL;
    if ((RCC->APB1ENR & RCC_APB1ENR_USBEN) != 0U)
    {
        RCC->APB1RSTR |= RCC_APB1RSTR_USBRST;
        RCC->APB1RSTR &= ~RCC_APB1RSTR_USBRST;
    }
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;
    (void)RCC->APB2ENR;
    SYSCFG->CFGR1 &= ~SYSCFG_CFGR1_MEM_MODE;
    (void)SYSCFG->CFGR1;

#if defined(AFFINE_BOOT_F072_ROM_LOADER_ESCAPE)
    {
        volatile uint32_t *request = (volatile uint32_t *)BOOT_GUARD_REQUEST_ADDRESS;

        if (*request == BOOT_GUARD_ROM_REQUEST)
        {
            *request = 0U;
            boot_guard_enter_rom_loader();
        }
    }
#endif

    if (record->magic != BOOT_GUARD_MAGIC)
    {
        boot_guard_enter_rom_loader();
    }

    if (length == 0U)
    {
        return; /* an image that was never stamped: a developer build */
    }

    if (((length & 3U) != 0U) || (length > (BOOT_GUARD_FLASH_END - (uint32_t)word)))
    {
        boot_guard_enter_rom_loader();
    }

    for (uint32_t remaining = length; remaining != 0U; remaining -= 4U)
    {
        sum = ((sum << 5) | (sum >> 27)) ^ *word++;
    }

    if (sum != record->body_sum)
    {
        boot_guard_enter_rom_loader();
    }
}
