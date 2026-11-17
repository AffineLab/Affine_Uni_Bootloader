/* CDC line state only; none of the SDK UART or DMA code is linked. */
#ifndef AFFINE_CH32_UART_COMPAT_H
#define AFFINE_CH32_UART_COMPAT_H

#include "debug.h"
#include "usb.h"

extern struct boot_ch32_line_state
{
    uint8_t Com_Cfg[8];
} Uart;

static inline void UART2_USB_Init(void)
{
}

#endif
