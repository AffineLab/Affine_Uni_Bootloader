#include "main.h"

void __libc_init_array(void)
{
}

uint32_t __wrap_HAL_RCC_GetSysClockFreq(void)
{
    return HSI_VALUE;
}

uint32_t __wrap_HAL_RCC_GetHCLKFreq(void)
{
    SystemCoreClock = HSI_VALUE;
    return HSI_VALUE;
}
