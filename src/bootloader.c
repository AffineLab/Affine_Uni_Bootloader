#include "bootloader.h"

#include <string.h>

#include "boot_platform.h"
#include "boot_security.h"

static void bootloader_send_response(uint16_t opcode,
                                     uint16_t sequence,
                                     const void *payload,
                                     uint32_t payload_len)
{
    static uint8_t buffer[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
    boot_frame_header_t header;

    header.magic = BOOT_FRAME_MAGIC;
    header.opcode = (uint16_t)(opcode | BOOT_OP_RESPONSE);
    header.sequence = sequence;
    header.payload_len = payload_len;
    header.payload_crc32 = boot_crc32_update(0U, payload, payload_len);
    header.header_crc32 = boot_crc32_update(0U, &header, sizeof(header) - sizeof(uint32_t));

    memcpy(buffer, &header, sizeof(header));
    if (payload_len > 0U)
    {
        memcpy(&buffer[sizeof(header)], payload, payload_len);
    }

    boot_platform_transport_send(buffer, sizeof(header) + payload_len);
}

static void bootloader_send_status(bootloader_t *ctx, uint16_t opcode, uint16_t sequence)
{
    boot_status_response_t rsp;

    rsp.status = (uint32_t)ctx->status;
    rsp.last_error = (uint32_t)ctx->last_error;
    rsp.written_size = ctx->session.written_size;
    rsp.expected_size = ctx->session.expected_size;
    rsp.running_crc32 = ctx->session.running_crc32;

    bootloader_send_response(opcode, sequence, &rsp, sizeof(rsp));
}

static void bootloader_prepare_metadata(bootloader_t *ctx, boot_metadata_t *metadata)
{
    const boot_target_config_t *cfg = boot_platform_target();

    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = BOOT_METADATA_MAGIC;
    metadata->target_id = cfg->target_id;
    metadata->version = ctx->session.version;
    metadata->image_size = ctx->session.expected_size;
    metadata->image_crc32 = ctx->session.expected_crc32;
    metadata->app_base = cfg->app_base;
    metadata->flags = ctx->session.flags;
}

static boot_error_t bootloader_store_metadata(const boot_metadata_t *metadata)
{
    const boot_target_config_t *cfg = boot_platform_target();

    boot_platform_flash_unlock();

    if (boot_platform_flash_erase(cfg->metadata_base, cfg->metadata_size) != BOOT_ERR_NONE)
    {
        boot_platform_flash_lock();
        return BOOT_ERR_FLASH;
    }

    if (boot_platform_flash_write(cfg->metadata_base, (const uint8_t *)metadata, sizeof(*metadata)) != BOOT_ERR_NONE)
    {
        boot_platform_flash_lock();
        return BOOT_ERR_FLASH;
    }

    boot_platform_flash_lock();
    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_begin_session(bootloader_t *ctx, const boot_begin_request_t *req)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const uint32_t slot_size = cfg->metadata_base - cfg->app_base;
    const boot_security_begin_t sec_req = {
        .version = req->version,
        .flags = req->flags,
        .target_id = req->target_id,
        .image_size = req->image_size,
        .expected_crc32 = req->image_crc32
    };

    if (req->target_id != cfg->target_id)
    {
        return BOOT_ERR_BAD_TARGET;
    }

    if ((req->image_size == 0U) || (req->image_size > slot_size))
    {
        return BOOT_ERR_IMAGE_TOO_LARGE;
    }

    if (!boot_security_supports_flags(req->flags))
    {
        return BOOT_ERR_UNSUPPORTED_OPCODE;
    }

    if (boot_security_begin(&sec_req) != BOOT_ERR_NONE)
    {
        return BOOT_ERR_BAD_STATE;
    }

    boot_platform_flash_unlock();

    if (boot_platform_flash_erase(cfg->app_base, slot_size) != BOOT_ERR_NONE)
    {
        boot_platform_flash_lock();
        return BOOT_ERR_FLASH;
    }

    if (boot_platform_flash_erase(cfg->metadata_base, cfg->metadata_size) != BOOT_ERR_NONE)
    {
        boot_platform_flash_lock();
        return BOOT_ERR_FLASH;
    }

    boot_platform_flash_lock();

    memset(&ctx->session, 0, sizeof(ctx->session));
    ctx->session.state = BOOT_SESSION_STATE_OPEN;
    ctx->session.version = req->version;
    ctx->session.expected_size = req->image_size;
    ctx->session.expected_crc32 = req->image_crc32;
    ctx->session.running_crc32 = 0U;
    ctx->session.flags = req->flags;
    ctx->status = BOOT_STATUS_RECEIVING;
    ctx->last_error = BOOT_ERR_NONE;
    memset(&ctx->metadata_shadow, 0, sizeof(ctx->metadata_shadow));

    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_write_data(bootloader_t *ctx, const uint8_t *payload, uint32_t payload_len)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const boot_data_prefix_t *prefix;
    uint8_t chunk_buffer[BOOT_FRAME_MAX_PAYLOAD];
    const uint8_t *chunk;
    uint32_t address;

    if (ctx->session.state != BOOT_SESSION_STATE_OPEN)
    {
        return BOOT_ERR_BAD_STATE;
    }

    if (payload_len < sizeof(*prefix))
    {
        return BOOT_ERR_RANGE;
    }

    prefix = (const boot_data_prefix_t *)payload;

    if (prefix->offset != ctx->session.written_size)
    {
        return BOOT_ERR_RANGE;
    }

    if (prefix->data_len != (payload_len - sizeof(*prefix)))
    {
        return BOOT_ERR_RANGE;
    }

    if (prefix->data_len > cfg->max_chunk_size)
    {
        return BOOT_ERR_RANGE;
    }

    if (prefix->data_len == 0U)
    {
        return BOOT_ERR_RANGE;
    }

    if (ctx->session.written_size > ctx->session.expected_size)
    {
        return BOOT_ERR_RANGE;
    }

    if (prefix->data_len > (ctx->session.expected_size - ctx->session.written_size))
    {
        return BOOT_ERR_RANGE;
    }

    if ((prefix->data_len % cfg->write_size) != 0U)
    {
        return BOOT_ERR_ALIGNMENT;
    }

    address = cfg->app_base + prefix->offset;
    memcpy(chunk_buffer, payload + sizeof(*prefix), prefix->data_len);
    if (boot_security_transform_chunk(prefix->offset, chunk_buffer, prefix->data_len) != BOOT_ERR_NONE)
    {
        return BOOT_ERR_IMAGE_CRC;
    }
    chunk = chunk_buffer;

    boot_platform_flash_unlock();
    if (boot_platform_flash_write(address, chunk, prefix->data_len) != BOOT_ERR_NONE)
    {
        boot_platform_flash_lock();
        return BOOT_ERR_FLASH;
    }
    boot_platform_flash_lock();

    ctx->session.running_crc32 = boot_crc32_update(ctx->session.running_crc32, chunk, prefix->data_len);
    ctx->session.written_size += prefix->data_len;
    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_commit_session(bootloader_t *ctx, const boot_commit_request_t *req)
{
    boot_metadata_t metadata;

    if (ctx->session.state != BOOT_SESSION_STATE_OPEN)
    {
        return BOOT_ERR_BAD_STATE;
    }

    if (req->image_size != ctx->session.expected_size)
    {
        return BOOT_ERR_RANGE;
    }

    if (req->image_crc32 != ctx->session.expected_crc32)
    {
        return BOOT_ERR_IMAGE_CRC;
    }

    if (ctx->session.written_size != ctx->session.expected_size)
    {
        return BOOT_ERR_RANGE;
    }

    if (ctx->session.running_crc32 != ctx->session.expected_crc32)
    {
        return BOOT_ERR_IMAGE_CRC;
    }

    bootloader_prepare_metadata(ctx, &metadata);

    if (boot_security_verify_image(boot_platform_target(), &metadata) != BOOT_ERR_NONE)
    {
        return BOOT_ERR_IMAGE_CRC;
    }

    if (bootloader_store_metadata(&metadata) != BOOT_ERR_NONE)
    {
        return BOOT_ERR_FLASH;
    }

    ctx->metadata_shadow = metadata;
    ctx->session.state = BOOT_SESSION_STATE_DONE;
    ctx->status = BOOT_STATUS_COMMITTED;
    return BOOT_ERR_NONE;
}

static void bootloader_handle_frame(bootloader_t *ctx, const boot_decoded_frame_t *frame)
{
    boot_error_t err = BOOT_ERR_NONE;

    switch ((boot_opcode_t)frame->header.opcode)
    {
        case BOOT_OP_HELLO:
        {
            const boot_target_config_t *cfg = boot_platform_target();
            boot_hello_response_t rsp;

            rsp.protocol_version = BOOT_PROTOCOL_VERSION;
            rsp.target_id = cfg->target_id;
            rsp.flash_size = cfg->flash_size;
            rsp.app_base = cfg->app_base;
            rsp.slot_size = cfg->metadata_base - cfg->app_base;
            rsp.max_chunk_size = cfg->max_chunk_size;
            rsp.capabilities = boot_security_capabilities();
            bootloader_send_response(frame->header.opcode, frame->header.sequence, &rsp, sizeof(rsp));
            return;
        }

        case BOOT_OP_BEGIN:
            if (frame->header.payload_len != sizeof(boot_begin_request_t))
            {
                err = BOOT_ERR_RANGE;
            }
            else
            {
                err = bootloader_begin_session(ctx, (const boot_begin_request_t *)frame->payload);
            }
            break;

        case BOOT_OP_DATA:
            err = bootloader_write_data(ctx, frame->payload, frame->header.payload_len);
            break;

        case BOOT_OP_COMMIT:
            if (frame->header.payload_len != sizeof(boot_commit_request_t))
            {
                err = BOOT_ERR_RANGE;
            }
            else
            {
                err = bootloader_commit_session(ctx, (const boot_commit_request_t *)frame->payload);
            }
            break;

        case BOOT_OP_ABORT:
            memset(&ctx->session, 0, sizeof(ctx->session));
            boot_security_reset();
            ctx->status = BOOT_STATUS_READY;
            err = BOOT_ERR_NONE;
            break;

        case BOOT_OP_GET_STATUS:
            bootloader_send_status(ctx, frame->header.opcode, frame->header.sequence);
            return;

        case BOOT_OP_GET_DIAG:
        {
            boot_diag_response_t rsp;

            if (!boot_platform_transport_get_diag(&rsp))
            {
                err = BOOT_ERR_BAD_STATE;
                break;
            }

            rsp.frame_ok_count = ctx->frame_ok_count;
            rsp.decode_error_count = ctx->decode_error_count;
            rsp.last_decoded_opcode = ctx->last_decoded_opcode;
            rsp.last_decoded_sequence = ctx->last_decoded_sequence;
            rsp.last_decode_error = (uint32_t)ctx->last_decode_error;
            bootloader_send_response(frame->header.opcode, frame->header.sequence, &rsp, sizeof(rsp));
            return;
        }

        case BOOT_OP_BOOT_APP:
        {
            const boot_target_config_t *cfg = boot_platform_target();
            const boot_metadata_t *metadata = (const boot_metadata_t *)cfg->metadata_base;

            if (boot_image_is_committed_and_valid(cfg, metadata))
            {
                ctx->metadata_shadow = *metadata;
                bootloader_send_status(ctx, frame->header.opcode, frame->header.sequence);
                boot_platform_jump_to_app(cfg->app_base);
                return;
            }
            err = BOOT_ERR_NO_VALID_APP;
            break;
        }

        default:
            err = BOOT_ERR_UNSUPPORTED_OPCODE;
            break;
    }

    ctx->last_error = err;
    if (err != BOOT_ERR_NONE)
    {
        ctx->status = BOOT_STATUS_ERROR;
    }
    else if (ctx->status == BOOT_STATUS_IDLE)
    {
        ctx->status = BOOT_STATUS_READY;
    }

    bootloader_send_status(ctx, frame->header.opcode, frame->header.sequence);
}

void bootloader_init(bootloader_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    boot_security_reset();
    boot_decoder_reset(&ctx->decoder);
    ctx->status = BOOT_STATUS_READY;
}

void bootloader_poll(bootloader_t *ctx)
{
    (void)ctx;
    boot_platform_transport_poll();
}

void bootloader_receive_bytes(bootloader_t *ctx, const uint8_t *data, size_t length)
{
    boot_decoded_frame_t frame;
    size_t index = 0U;

    while (index < length)
    {
        const boot_error_t err = boot_decoder_consume(&ctx->decoder, &data[index], 1U, &frame);
        if (err != BOOT_ERR_NONE)
        {
            ctx->decode_error_count++;
            ctx->last_decode_error = err;
            ctx->last_error = err;
            ctx->status = BOOT_STATUS_ERROR;
            break;
        }

        if (frame.frame_ready)
        {
            ctx->frame_ok_count++;
            ctx->last_decoded_opcode = frame.header.opcode;
            ctx->last_decoded_sequence = frame.header.sequence;
            bootloader_handle_frame(ctx, &frame);
        }

        ++index;
    }
}

void bootloader_try_boot_app(bootloader_t *ctx)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const boot_metadata_t *metadata = (const boot_metadata_t *)cfg->metadata_base;

    if (boot_platform_force_bootloader())
    {
        return;
    }

    if (!boot_metadata_is_valid(cfg, metadata))
    {
        ctx->last_error = BOOT_ERR_NO_VALID_APP;
        ctx->status = BOOT_STATUS_ERROR;
        return;
    }

    if (!boot_image_vector_is_valid(cfg, metadata->app_base))
    {
        ctx->last_error = BOOT_ERR_NO_VALID_APP;
        ctx->status = BOOT_STATUS_ERROR;
        return;
    }

    if (!boot_image_crc_is_valid(metadata))
    {
        ctx->last_error = BOOT_ERR_IMAGE_CRC;
        ctx->status = BOOT_STATUS_ERROR;
        return;
    }

    ctx->metadata_shadow = *metadata;
    boot_platform_jump_to_app(cfg->app_base);
}
