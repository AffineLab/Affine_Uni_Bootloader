#include "main.h"
#include "usb.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void USB_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_FS);
}
