#include "boot_protocol.h"

#include <string.h>

static void boot_crc32_init_table(uint32_t table[256])
{
    for (uint32_t i = 0; i < 256U; ++i)
    {
        uint32_t crc = i;
        for (uint32_t bit = 0; bit < 8U; ++bit)
        {
            crc = (crc & 1U) ? (0xEDB88320UL ^ (crc >> 1U)) : (crc >> 1U);
        }
        table[i] = crc;
    }
}

uint32_t boot_crc32_update(uint32_t seed, const void *data, size_t length)
{
    static uint32_t table[256];
    static bool table_ready = false;
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = ~seed;

    if (!table_ready)
    {
        boot_crc32_init_table(table);
        table_ready = true;
    }

    for (size_t i = 0; i < length; ++i)
    {
        const uint32_t index = (crc ^ bytes[i]) & 0xFFU;
        crc = table[index] ^ (crc >> 8U);
    }

    return ~crc;
}

void boot_decoder_reset(boot_frame_decoder_t *decoder)
{
    memset(decoder, 0, sizeof(*decoder));
}

bool boot_decoder_in_frame(const boot_frame_decoder_t *decoder)
{
    return decoder->header_used > 0U;
}

/* True while the buffered header bytes are still a prefix of the frame magic. */
static bool boot_decoder_magic_prefix_ok(const boot_frame_decoder_t *decoder)
{
    static const uint8_t magic[4] = {
        (uint8_t)BOOT_FRAME_MAGIC,
        (uint8_t)(BOOT_FRAME_MAGIC >> 8),
        (uint8_t)(BOOT_FRAME_MAGIC >> 16),
        (uint8_t)(BOOT_FRAME_MAGIC >> 24)
    };
    const size_t n = (decoder->header_used < sizeof(magic)) ? decoder->header_used : sizeof(magic);

    return memcmp(decoder->header_bytes, magic, n) == 0;
}

/*
 * The buffered header bytes are not a frame start: drop the first one and look
 * for the magic in the rest. On a byte stream that loses or gains bytes (a
 * UART), this finds the next frame however the stream got out of step, instead
 * of throwing away 20 bytes at a time and staying out of step.
 */
static void boot_decoder_resync(boot_frame_decoder_t *decoder)
{
    do
    {
        decoder->header_used--;
        memmove(decoder->header_bytes, &decoder->header_bytes[1], decoder->header_used);
    } while ((decoder->header_used > 0U) && !boot_decoder_magic_prefix_ok(decoder));
}

static boot_error_t boot_decoder_parse_header(boot_frame_decoder_t *decoder)
{
    memcpy(&decoder->current_header, decoder->header_bytes, sizeof(decoder->current_header));

    if (decoder->current_header.payload_len > BOOT_FRAME_MAX_PAYLOAD)
    {
        return BOOT_ERR_RANGE;
    }

    if (boot_crc32_update(0U,
                          &decoder->current_header,
                          sizeof(decoder->current_header) - sizeof(uint32_t)) != decoder->current_header.header_crc32)
    {
        return BOOT_ERR_BAD_HEADER_CRC;
    }

    return BOOT_ERR_NONE;
}

/*
 * Returns as soon as a frame is ready, leaving any later bytes of data
 * unconsumed: feed it one byte at a time. A byte that cannot belong to a frame
 * is reported as an error, but the decoder stays in step and carries on.
 *
 * A bad payload drops the whole frame and does not look for a frame start
 * inside it: hosts send one request and wait for its answer, so the bytes
 * after a damaged frame are a gap and then the host's retry, and the idle
 * timeout in bootloader_poll() clears any leftover.
 */
boot_error_t boot_decoder_consume(boot_frame_decoder_t *decoder,
                                  const uint8_t *data,
                                  size_t length,
                                  boot_decoded_frame_t *out_frame)
{
    boot_error_t err = BOOT_ERR_NONE;

    out_frame->frame_ready = false;

    while (length > 0U)
    {
        const uint8_t byte = *data++;
        --length;

        if (decoder->header_used < sizeof(boot_frame_header_t))
        {
            decoder->header_bytes[decoder->header_used++] = byte;

            if (!boot_decoder_magic_prefix_ok(decoder))
            {
                boot_decoder_resync(decoder);
                err = BOOT_ERR_BAD_MAGIC;
                continue;
            }

            if (decoder->header_used < sizeof(boot_frame_header_t))
            {
                continue;
            }

            {
                const boot_error_t header_err = boot_decoder_parse_header(decoder);
                if (header_err != BOOT_ERR_NONE)
                {
                    boot_decoder_resync(decoder);
                    err = header_err;
                    continue;
                }
            }

            if (decoder->current_header.payload_len == 0U)
            {
                out_frame->frame_ready = true;
                out_frame->header = decoder->current_header;
                boot_decoder_reset(decoder);
                return BOOT_ERR_NONE;
            }

            continue;
        }

        decoder->payload[decoder->payload_used++] = byte;

        if (decoder->payload_used == decoder->current_header.payload_len)
        {
            if (boot_crc32_update(0U, decoder->payload, decoder->payload_used) != decoder->current_header.payload_crc32)
            {
                boot_decoder_reset(decoder);
                return BOOT_ERR_BAD_PAYLOAD_CRC;
            }

            out_frame->frame_ready = true;
            out_frame->header = decoder->current_header;
            memcpy(out_frame->payload, decoder->payload, decoder->payload_used);
            boot_decoder_reset(decoder);
            return BOOT_ERR_NONE;
        }
    }

    return err;
}
