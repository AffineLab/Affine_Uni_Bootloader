/*
 * Boot guard for the STM32G431 bootloader (the G431 port of
 * targets/stm32f072/boot_guard_stm32f072.c).
 *
 * The guard lives in the first flash page together with the vector table, the
 * reset code and the NMI handler. It runs before anything else and checks that
 * the rest of the bootloader is complete. If it is not, it starts the MCU's own
 * USB DFU loader in system memory instead of running into half-written code.
 *
 * That makes replacing the bootloader (targets/stm32g431_blupdater) restartable:
 * write the first page first, then the rest. Power lost at any point after the
 * first page leaves a board that comes up in the ROM loader and can be written
 * again. Only the erase and write of the first page itself is unprotected.
 *
 * Nothing here may use code or data outside the first page, or any RAM other
 * than the stack: .data and .bss are not set up yet. (The NMI handler's error
 * counter is in .bss; counting into it before startup zeroes it is harmless.)
 */
#include <stdint.h>

#include "stm32g4xx.h"

#include "boot_protocol.h"

#define BOOT_GUARD_MAGIC           0x44524147UL /* "GARD" */
#define BOOT_GUARD_RECORD_ADDRESS  0x080001D8UL /* right behind the 118-word vector table */
#define BOOT_GUARD_FLASH_END       0x08006000UL /* end of the bootloader region */
#define BOOT_GUARD_ROM_LOADER      0x1FFF0000UL /* STM32G431 system memory */

typedef struct
{
    uint32_t magic;
    uint32_t body_length; /* bytes from _guard_end to the end of the image; 0 = not stamped, no check */
    uint32_t body_sum;
    uint32_t protocol_version; /* for the application: which bootloader started it */
} boot_guard_record_t;

extern const uint32_t _guard_end[];
extern volatile uint32_t g_boot_flash_ecc_errors; /* boot_platform_stm32g431.c */

/* Stamped after the build by tools/patch_boot_guard.py. */
__attribute__((section(".guard_info"), used))
const boot_guard_record_t g_boot_guard_record = {BOOT_GUARD_MAGIC, 0U, 0U, BOOT_PROTOCOL_VERSION};

__attribute__((section(".guard"), noinline, noreturn))
static void boot_guard_enter_rom_loader(void)
{
    const uint32_t *loader = (const uint32_t *)BOOT_GUARD_ROM_LOADER;

    /* Map system memory at address 0 and take its vectors, as a BOOT0 start would. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->MEMRMP = (SYSCFG->MEMRMP & ~SYSCFG_MEMRMP_MEM_MODE) | SYSCFG_MEMRMP_MEM_MODE_0;
    (void)SYSCFG->MEMRMP;
    SCB->VTOR = BOOT_GUARD_ROM_LOADER;
    __DSB();
    __ISB();

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
    const uint32_t length = record->body_length;
    uint32_t ecc_errors;
    uint32_t sum = 0U;

    /*
     * The ROM loader starts a freshly written image by jumping to it, with its
     * USB device still attached, its interrupts still enabled and system memory
     * still mapped at address 0. Undo that; after a real reset none of this
     * changes anything.
     */
    for (uint32_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
    }
    if ((RCC->APB1ENR1 & RCC_APB1ENR1_USBEN) != 0U)
    {
        RCC->APB1RSTR1 |= RCC_APB1RSTR1_USBRST;
        RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_USBRST;
    }
    for (uint32_t i = 0U; i < (sizeof(NVIC->ICPR) / sizeof(NVIC->ICPR[0])); ++i)
    {
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->MEMRMP &= ~SYSCFG_MEMRMP_MEM_MODE;
    (void)SYSCFG->MEMRMP;
    /* Our own vectors, so a flash ECC error below reaches our NMI handler. */
    SCB->VTOR = FLASH_BASE;
    __DSB();
    __ISB();

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

    /* A double-word left half-written by a power loss reads with an ECC error. */
    ecc_errors = g_boot_flash_ecc_errors;
    for (uint32_t remaining = length; remaining != 0U; remaining -= 4U)
    {
        sum = ((sum << 5) | (sum >> 27)) ^ *word++;
    }

    if ((sum != record->body_sum) || (g_boot_flash_ecc_errors != ecc_errors))
    {
        boot_guard_enter_rom_loader();
    }
}
