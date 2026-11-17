#ifndef AFFINE_BOOT_JUMP_RISCV_H
#define AFFINE_BOOT_JUMP_RISCV_H

#include <stdint.h>

/* Entry is the SDK _start instruction, which establishes its own sp and gp.
 * mtvec mode 3 is QingKe absolute-address vector-table mode, not standard
 * RISC-V vectored JAL slots. No compiler-generated work follows the branch. */
__attribute__((always_inline, noreturn)) static inline void boot_jump_riscv(uint32_t entry, uint32_t mtvec)
{
    __asm volatile("csrw mtvec, %0\n\tfence rw, rw\n\tfence.i\n\tjr %1" : : "r"(mtvec), "r"(entry) : "memory");
    __builtin_unreachable();
}

#endif
