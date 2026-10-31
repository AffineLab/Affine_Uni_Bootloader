/*
 * The transport the core talks to, chosen at build time
 * (AFFINE_BOOT_TRANSPORT): transport_usb.c, transport_uart.c, or both. With
 * both, answers go out on the channel main.c locked onto.
 */
#include "boot_platform.h"

#include "main.h"

#include "targets/common/boot_rx_arbiter.h"

void boot_stm32f072_transport_init(void)
{
#if defined(AFFINE_BOOT_TRANSPORT_DUAL)
    boot_stm32f072_usb_transport_init();
    boot_stm32f072_uart_transport_init();
#elif defined(AFFINE_BOOT_TRANSPORT_UART)
    boot_stm32f072_uart_transport_init();
#else
    boot_stm32f072_usb_transport_init();
#endif
}

void boot_stm32f072_transport_note_rx_overflow(uint32_t channel)
{
#if defined(AFFINE_BOOT_TRANSPORT_DUAL)
    if (channel == BOOT_RX_CHANNEL_UART)
    {
        boot_stm32f072_uart_transport_note_rx_overflow();
    }
    else
    {
        boot_stm32f072_usb_transport_note_rx_overflow();
    }
#elif defined(AFFINE_BOOT_TRANSPORT_UART)
    (void)channel;
    boot_stm32f072_uart_transport_note_rx_overflow();
#else
    (void)channel;
    boot_stm32f072_usb_transport_note_rx_overflow();
#endif
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
#if defined(AFFINE_BOOT_TRANSPORT_DUAL)
    /* The core only answers frames, and frames only arrive after the lock. */
    if (boot_stm32f072_locked_channel() == BOOT_RX_CHANNEL_UART)
    {
        boot_stm32f072_uart_transport_send(data, length);
    }
    else
    {
        boot_stm32f072_usb_transport_send(data, length);
    }
#elif defined(AFFINE_BOOT_TRANSPORT_UART)
    boot_stm32f072_uart_transport_send(data, length);
#else
    boot_stm32f072_usb_transport_send(data, length);
#endif
}

void boot_platform_transport_poll(void)
{
#if !defined(AFFINE_BOOT_TRANSPORT_UART)
    boot_stm32f072_usb_transport_poll(); /* the UART sends without a queue */
#endif
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
#if defined(AFFINE_BOOT_TRANSPORT_DUAL)
    return (boot_stm32f072_locked_channel() == BOOT_RX_CHANNEL_UART)
        ? boot_stm32f072_uart_transport_get_diag(out_diag)
        : boot_stm32f072_usb_transport_get_diag(out_diag);
#elif defined(AFFINE_BOOT_TRANSPORT_UART)
    return boot_stm32f072_uart_transport_get_diag(out_diag);
#else
    return boot_stm32f072_usb_transport_get_diag(out_diag);
#endif
}
