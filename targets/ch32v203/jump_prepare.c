#include "ch32v20x.h"
#include "usb.h"

/* Kept separate so host fakes exercise the actual register cleanup. */
void boot_ch32_jump_prepare(void)
{
    __disable_irq();
    boot_ch32_usb_deinit();
    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = 0;
    for (unsigned i = 0; i < 8U; i++)
    {
        PFIC->IRER[i] = 0xFFFFFFFFU;
        PFIC->IPRR[i] = 0xFFFFFFFFU;
    }
    for (unsigned i = 0; i < 4U; i++)
    {
        PFIC->VTFADDR[i] = 0;
    }
    RCC_DeInit();
}
