#include "ch32v20x.h"
#include "usb_hw.h"

uint32_t boot_ch32_irq_save(void)
{
    uint32_t state;
    __asm volatile("csrr %0,0x800" : "=r"(state));
    __disable_irq();
    __asm volatile("" : : : "memory");
    return state;
}

void boot_ch32_irq_restore(uint32_t state)
{
    __asm volatile("" : : : "memory");
    if (state & 8U)
    {
        __enable_irq();
    }
}
