#include "targets/common/boot_rx_arbiter.h"

#include <string.h>

static bool boot_rx_arbiter_magic_prefix_ok(const uint8_t *window, uint32_t used)
{
    static const uint8_t magic[4] = {
        (uint8_t)BOOT_FRAME_MAGIC,
        (uint8_t)(BOOT_FRAME_MAGIC >> 8),
        (uint8_t)(BOOT_FRAME_MAGIC >> 16),
        (uint8_t)(BOOT_FRAME_MAGIC >> 24)
    };

    return memcmp(window, magic, (used < sizeof(magic)) ? used : sizeof(magic)) == 0;
}

static bool boot_rx_arbiter_header_ok(const uint8_t *window)
{
    boot_frame_header_t header;

    memcpy(&header, window, sizeof(header));
    return (header.payload_len <= BOOT_FRAME_MAX_PAYLOAD) &&
           (boot_crc32_update(0U, &header, sizeof(header) - sizeof(uint32_t)) == header.header_crc32);
}

/* Not a frame start: drop the first byte and look for the magic in the rest. */
static void boot_rx_arbiter_resync(uint8_t *window, uint8_t *used)
{
    do
    {
        (*used)--;
        memmove(window, &window[1], *used);
    } while ((*used > 0U) && !boot_rx_arbiter_magic_prefix_ok(window, *used));
}

void boot_rx_arbiter_init(boot_rx_arbiter_t *arbiter)
{
    memset(arbiter, 0, sizeof(*arbiter));
    arbiter->locked = BOOT_RX_ARBITER_NONE;
}

bool boot_rx_arbiter_feed(boot_rx_arbiter_t *arbiter, uint8_t channel, uint8_t byte)
{
    uint8_t *window = arbiter->window[channel];
    uint8_t *used = &arbiter->used[channel];

    window[(*used)++] = byte;

    if (!boot_rx_arbiter_magic_prefix_ok(window, *used))
    {
        boot_rx_arbiter_resync(window, used);
        return false;
    }

    if (*used < sizeof(boot_frame_header_t))
    {
        return false;
    }

    if (!boot_rx_arbiter_header_ok(window))
    {
        boot_rx_arbiter_resync(window, used);
        return false;
    }

    arbiter->locked = channel;
    return true;
}
