/*
 * RP2354A/B: the RP2350 die with 2 MiB of in-package flash on QSPI CS0.
 * Bootloader board header only: the bootloader drives no LED, UART or other
 * board pins, so the QFN-60 pin count below also suits RP2354B. Boards with a
 * crystal other than the SDK's 12 MHz default need their own header.
 */
#ifndef _BOARDS_AFFINE_RP2354_H
#define _BOARDS_AFFINE_RP2354_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define AFFINE_RP2354
#define PICO_RP2350A 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
