#include "main.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* The transport's interrupt handler is in usb.c or transport_uart.c. */
