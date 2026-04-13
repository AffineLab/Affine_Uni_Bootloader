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

static boot_error_t boot_decoder_parse_header(boot_frame_decoder_t *decoder)
{
    memcpy(&decoder->current_header, decoder->header_bytes, sizeof(decoder->current_header));

    if (decoder->current_header.magic != BOOT_FRAME_MAGIC)
    {
        boot_decoder_reset(decoder);
        return BOOT_ERR_BAD_MAGIC;
    }

    if (decoder->current_header.payload_len > BOOT_FRAME_MAX_PAYLOAD)
    {
        boot_decoder_reset(decoder);
        return BOOT_ERR_RANGE;
    }

    if (boot_crc32_update(0U,
                          &decoder->current_header,
                          sizeof(decoder->current_header) - sizeof(uint32_t)) != decoder->current_header.header_crc32)
    {
        boot_decoder_reset(decoder);
        return BOOT_ERR_BAD_HEADER_CRC;
    }

    return BOOT_ERR_NONE;
}

boot_error_t boot_decoder_consume(boot_frame_decoder_t *decoder,
                                  const uint8_t *data,
                                  size_t length,
                                  boot_decoded_frame_t *out_frame)
{
    memset(out_frame, 0, sizeof(*out_frame));

    while (length > 0U)
    {
        if (decoder->header_used < sizeof(boot_frame_header_t))
        {
            const size_t need = sizeof(boot_frame_header_t) - decoder->header_used;
            const size_t take = (length < need) ? length : need;
            memcpy(&decoder->header_bytes[decoder->header_used], data, take);
            decoder->header_used += take;
            data += take;
            length -= take;

            if (decoder->header_used == sizeof(boot_frame_header_t))
            {
                const boot_error_t header_err = boot_decoder_parse_header(decoder);
                if (header_err != BOOT_ERR_NONE)
                {
                    return header_err;
                }

                if (decoder->current_header.payload_len == 0U)
                {
                    out_frame->frame_ready = true;
                    out_frame->header = decoder->current_header;
                    boot_decoder_reset(decoder);
                    return BOOT_ERR_NONE;
                }
            }

            continue;
        }

        {
            const size_t need = decoder->current_header.payload_len - decoder->payload_used;
            const size_t take = (length < need) ? length : need;
            memcpy(&decoder->payload[decoder->payload_used], data, take);
            decoder->payload_used += take;
            data += take;
            length -= take;
        }

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

    return BOOT_ERR_NONE;
}
