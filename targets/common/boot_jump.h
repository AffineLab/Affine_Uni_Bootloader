#ifndef AFFINE_BOOT_JUMP_H
#define AFFINE_BOOT_JUMP_H

#include <stdint.h>

/*
 * Starts other code on its own stack: loads MSP and branches in one asm block.
 *
 * With __set_MSP() followed by a call, the compiler does not know the stack has
 * moved and may still read a spilled local from it. On STM32F072 at -Os it
 * reloaded the reset handler from [sp, #4] after the switch, one word past the
 * end of SRAM, and faulted before the application ran. Both values are inputs
 * here, so they are in registers before MSP changes and nothing reads the stack
 * after it.
 *
 * Always inlined, so the boot guards can use it from their first flash page.
 */
__attribute__((always_inline, noreturn)) static inline void boot_jump_with_stack(uint32_t stack, uint32_t entry)
{
    __asm volatile("msr msp, %0\n\t"
                   "bx  %1"
                   :
                   : "r"(stack), "r"(entry));
    __builtin_unreachable();
}

#endif
