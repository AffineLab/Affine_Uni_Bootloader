#ifndef AFFINE_BOOT_RX_ARBITER_H
#define AFFINE_BOOT_RX_ARBITER_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_protocol.h"

/*
 * A bootloader listening on two transports at once locks onto the first one
 * that delivers a whole frame header with a valid CRC, and ignores the other
 * until the next reset. Until then each channel only needs a header's worth of
 * memory here; noise on an unconnected line never gets past the magic and CRC.
 */
#define BOOT_RX_ARBITER_CHANNELS 2U
#define BOOT_RX_ARBITER_NONE     0xFFU

typedef struct
{
    uint8_t window[BOOT_RX_ARBITER_CHANNELS][sizeof(boot_frame_header_t)];
    uint8_t used[BOOT_RX_ARBITER_CHANNELS];
    volatile uint8_t locked; /* read by the receive interrupts */
} boot_rx_arbiter_t;

void boot_rx_arbiter_init(boot_rx_arbiter_t *arbiter);

/*
 * Takes one received byte of an unlocked channel. True when it completed a
 * valid header and locked the channel: the caller then hands the header in
 * window[channel] to the bootloader, followed by the channel's later bytes.
 */
bool boot_rx_arbiter_feed(boot_rx_arbiter_t *arbiter, uint8_t channel, uint8_t byte);

#endif
