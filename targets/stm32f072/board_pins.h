#ifndef AFFINE_TARGET_STM32F072_BOARD_PINS_H
#define AFFINE_TARGET_STM32F072_BOARD_PINS_H

/*
 * Pins the STM32F072 bootloader uses besides USB. Every value can be
 * overridden from the build (-D...) for a board wired differently.
 */

#include "stm32f0xx.h"

/*
 * UART transport (AFFINE_BOOT_TRANSPORT=uart or dual): USART1 on PA9 (TX) / PA10 (RX),
 * AF1, 115200 8N1. Per ST AN2606 these are also pins the MCU's ROM loader
 * listens on, so a master wired here can also reach the boot guard's ROM
 * fallback (not verified on a board). USART2 is the other supported instance.
 */
#ifndef F072_BOOT_UART_INSTANCE
#define F072_BOOT_UART_INSTANCE 1 /* 1: USART1, 2: USART2 */
#endif
#ifndef F072_BOOT_UART_BAUD
#define F072_BOOT_UART_BAUD 115200UL
#endif
#ifndef F072_BOOT_UART_GPIO
#define F072_BOOT_UART_GPIO    GPIOA
#define F072_BOOT_UART_GPIO_EN RCC_AHBENR_GPIOAEN
#define F072_BOOT_UART_TX_PIN  9U
#define F072_BOOT_UART_RX_PIN  10U
#define F072_BOOT_UART_AF      1U
#endif

/*
 * Optional BOOT-REQUEST input: a master holds it at its active level through
 * this MCU's reset to keep it in the bootloader, without racing the auto-boot
 * window. Off unless the build defines F072_BOOT_REQUEST_GPIO, e.g.
 *   -DF072_BOOT_REQUEST_GPIO=GPIOB -DF072_BOOT_REQUEST_GPIO_EN=RCC_AHBENR_GPIOBEN
 *   -DF072_BOOT_REQUEST_PIN=1 -DF072_BOOT_REQUEST_ACTIVE_HIGH=1
 * The pin is pulled to its inactive level, so leaving it unconnected is safe.
 */
#if defined(F072_BOOT_REQUEST_GPIO)
#if !defined(F072_BOOT_REQUEST_GPIO_EN) || !defined(F072_BOOT_REQUEST_PIN)
#error "F072_BOOT_REQUEST_GPIO needs F072_BOOT_REQUEST_GPIO_EN and F072_BOOT_REQUEST_PIN"
#endif
#ifndef F072_BOOT_REQUEST_ACTIVE_HIGH
#define F072_BOOT_REQUEST_ACTIVE_HIGH 1
#endif
#endif

#endif
