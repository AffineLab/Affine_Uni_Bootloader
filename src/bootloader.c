#include "bootloader.h"

#include <string.h>

#include "boot_platform.h"
#include "boot_policy.h"
#include "boot_security.h"

#define BOOT_DEFERRED_ACTION_DELAY_MS 100U
#define BOOT_CRC_PROGRESS_CHUNK       1024U
#define BOOT_METADATA_INVALID_COPY    0xFFFFFFFFUL

static void bootloader_set_operation(bootloader_t *ctx,
                                     boot_operation_t operation,
                                     uint32_t progress,
                                     uint32_t total)
{
    ctx->operation = operation;
    ctx->operation_progress = progress;
    ctx->operation_total = total;
    boot_platform_watchdog_kick();
}

static void bootloader_schedule_action(bootloader_t *ctx, boot_pending_action_t action, uint32_t app_base)
{
    ctx->pending_action = action;
    ctx->pending_app_base = app_base;
    ctx->pending_action_due_ms = boot_platform_millis() + BOOT_DEFERRED_ACTION_DELAY_MS;
}

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
    rsp.operation = (uint32_t)ctx->operation;
    rsp.operation_progress = ctx->operation_progress;
    rsp.operation_total = ctx->operation_total;

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
    boot_metadata_update_crc(metadata);
}

static bool bootloader_current_version(uint32_t *out_version)
{
    boot_metadata_t metadata;

    if (!boot_metadata_select_valid(boot_platform_target(), &metadata))
    {
        return false;
    }

    if (out_version != NULL)
    {
        *out_version = metadata.version;
    }
    return true;
}

static bool bootloader_effective_rollback_floor(uint32_t *out_version)
{
    boot_security_state_t state;

    if (boot_security_state_select_valid(boot_platform_target(), &state))
    {
        if (out_version != NULL)
        {
            *out_version = state.rollback_floor_version;
        }
        return true;
    }

    return bootloader_current_version(out_version);
}

static void bootloader_security_state_with_floor(const boot_target_config_t *cfg,
                                                 boot_security_state_t *state,
                                                 uint32_t floor_version)
{
    boot_security_state_prepare(cfg, state, floor_version);
}

static boot_error_t bootloader_erase_range(bootloader_t *ctx,
                                           uint32_t address,
                                           uint32_t length,
                                           boot_operation_t operation)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const uint32_t erase_size = cfg->erase_size;
    uint32_t total_units;
    uint32_t erased_units = 0U;
    uint32_t offset = 0U;

    if (length == 0U)
    {
        bootloader_set_operation(ctx, operation, 0U, 0U);
        return BOOT_ERR_NONE;
    }

    if (erase_size == 0U)
    {
        return BOOT_ERR_RANGE;
    }

    total_units = (length + erase_size - 1U) / erase_size;

    while (offset < length)
    {
        const uint32_t remaining = length - offset;
        const uint32_t chunk_len = (remaining > erase_size) ? erase_size : remaining;
        const boot_error_t err = boot_platform_flash_erase(address + offset, chunk_len);

        if (err != BOOT_ERR_NONE)
        {
            return err;
        }

        ++erased_units;
        bootloader_set_operation(ctx, operation, erased_units, total_units);
        offset += chunk_len;
    }

    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_calculate_image_crc(bootloader_t *ctx,
                                                   const boot_metadata_t *metadata,
                                                   uint32_t *out_crc32)
{
    const uint8_t *image = (const uint8_t *)metadata->app_base;
    uint32_t crc = 0U;
    uint32_t offset = 0U;

    if (out_crc32 == NULL)
    {
        return BOOT_ERR_RANGE;
    }

    bootloader_set_operation(ctx, BOOT_OPERATION_VERIFY_IMAGE, 0U, metadata->image_size);

    while (offset < metadata->image_size)
    {
        const uint32_t remaining = metadata->image_size - offset;
        const uint32_t chunk_len = (remaining > BOOT_CRC_PROGRESS_CHUNK) ? BOOT_CRC_PROGRESS_CHUNK : remaining;

        crc = boot_crc32_update(crc, &image[offset], chunk_len);
        offset += chunk_len;
        bootloader_set_operation(ctx, BOOT_OPERATION_VERIFY_IMAGE, offset, metadata->image_size);
    }

    *out_crc32 = crc;
    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_verify_metadata_image(bootloader_t *ctx,
                                                     const boot_metadata_t *metadata,
                                                     const boot_security_state_t *candidate_security_state,
                                                     boot_verify_image_response_t *rsp)
{
    const boot_target_config_t *cfg = boot_platform_target();
    uint32_t calculated_crc32 = 0U;
    boot_error_t err;

    memset(rsp, 0, sizeof(*rsp));
    rsp->result_error = (uint32_t)BOOT_ERR_NO_VALID_APP;

    if (!boot_metadata_is_valid(cfg, metadata))
    {
        return BOOT_ERR_NO_VALID_APP;
    }

    rsp->metadata_valid = 1U;
    rsp->image_size = metadata->image_size;
    rsp->expected_crc32 = metadata->image_crc32;
    rsp->app_base = metadata->app_base;

    if (!boot_image_vector_is_valid(cfg, metadata->app_base))
    {
        return BOOT_ERR_NO_VALID_APP;
    }
    rsp->vector_valid = 1U;

    err = bootloader_calculate_image_crc(ctx, metadata, &calculated_crc32);
    rsp->calculated_crc32 = calculated_crc32;
    if (err != BOOT_ERR_NONE)
    {
        rsp->result_error = (uint32_t)err;
        return err;
    }

    if (calculated_crc32 != metadata->image_crc32)
    {
        rsp->result_error = (uint32_t)BOOT_ERR_IMAGE_CRC;
        return BOOT_ERR_IMAGE_CRC;
    }
    rsp->image_crc_valid = 1U;

    err = boot_security_verify_image_with_state(cfg, metadata, candidate_security_state);
    if (err != BOOT_ERR_NONE)
    {
        rsp->result_error = (uint32_t)err;
        return err;
    }

    rsp->security_valid = 1U;
    rsp->result_error = (uint32_t)BOOT_ERR_NONE;
    return BOOT_ERR_NONE;
}

static void bootloader_read_metadata(bootloader_t *ctx, boot_metadata_response_t *rsp)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const uint32_t copy_count = boot_metadata_copy_count(cfg);
    boot_security_state_t security_state;

    (void)ctx;
    memset(rsp, 0, sizeof(*rsp));
    rsp->copy_count = copy_count;
    rsp->selected_copy = BOOT_METADATA_INVALID_COPY;
    if (boot_security_state_select_valid(cfg, &security_state))
    {
        rsp->security_state_valid = 1U;
        rsp->security_state = security_state;
    }

    for (uint32_t copy = 0U; copy < copy_count; ++copy)
    {
        const boot_metadata_t *candidate = (const boot_metadata_t *)boot_metadata_copy_address(cfg, copy);

        boot_platform_watchdog_kick();
        if (!boot_metadata_is_valid(cfg, candidate))
        {
            continue;
        }

        rsp->valid = 1U;
        rsp->selected_copy = copy;
        rsp->metadata = *candidate;
        rsp->app_valid = boot_image_vector_is_valid(cfg, candidate->app_base) ? 1U : 0U;
        return;
    }
}

static boot_error_t bootloader_clear_metadata(bootloader_t *ctx)
{
    const boot_target_config_t *cfg = boot_platform_target();
    boot_security_state_t security_state;
    const bool preserve_security_state =
        (AFFINE_BOOT_PRESERVE_SECURITY_STATE_ON_CLEAR_METADATA != 0) &&
        boot_security_state_select_valid(cfg, &security_state);
    boot_error_t err;

    if (ctx->session.state == BOOT_SESSION_STATE_OPEN)
    {
        return BOOT_ERR_BAD_STATE;
    }

    ctx->status = BOOT_STATUS_ERASING;
    boot_platform_flash_unlock();
    err = bootloader_erase_range(ctx, cfg->metadata_base, cfg->metadata_size, BOOT_OPERATION_CLEAR_METADATA);
    if ((err == BOOT_ERR_NONE) && preserve_security_state)
    {
        const uint32_t copy_count = boot_metadata_copy_count(cfg);

        for (uint32_t copy = 0U; copy < copy_count; ++copy)
        {
            const uint32_t address = boot_security_state_copy_address(cfg, copy);

            if (boot_platform_flash_write(address,
                                          (const uint8_t *)&security_state,
                                          sizeof(security_state)) != BOOT_ERR_NONE)
            {
                err = BOOT_ERR_FLASH;
                break;
            }
        }
    }
    boot_platform_flash_lock();

    if (err != BOOT_ERR_NONE)
    {
        return err;
    }

    memset(&ctx->metadata_shadow, 0, sizeof(ctx->metadata_shadow));
    memset(&ctx->session, 0, sizeof(ctx->session));
    boot_security_reset();
    ctx->status = BOOT_STATUS_READY;
    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_store_metadata(bootloader_t *ctx,
                                              const boot_metadata_t *metadata,
                                              const boot_security_state_t *security_state)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const uint32_t copy_count = boot_metadata_copy_count(cfg);
    boot_security_state_t preserved_security_state;
    const boot_security_state_t *state_to_store = security_state;

    if ((state_to_store == NULL) &&
        boot_security_state_select_valid(cfg, &preserved_security_state))
    {
        state_to_store = &preserved_security_state;
    }

    if (copy_count == 0U)
    {
        return BOOT_ERR_RANGE;
    }

    boot_platform_flash_unlock();

    for (uint32_t copy = 0U; copy < copy_count; ++copy)
    {
        const uint32_t address = boot_metadata_copy_address(cfg, copy);

        bootloader_set_operation(ctx, BOOT_OPERATION_STORE_METADATA, copy, copy_count);

        if (boot_platform_flash_erase(address, cfg->erase_size) != BOOT_ERR_NONE)
        {
            boot_platform_flash_lock();
            return BOOT_ERR_FLASH;
        }

        if (boot_platform_flash_write(address, (const uint8_t *)metadata, sizeof(*metadata)) != BOOT_ERR_NONE)
        {
            boot_platform_flash_lock();
            return BOOT_ERR_FLASH;
        }

        if ((state_to_store != NULL) &&
            (boot_platform_flash_write(boot_security_state_copy_address(cfg, copy),
                                       (const uint8_t *)state_to_store,
                                       sizeof(*state_to_store)) != BOOT_ERR_NONE))
        {
            boot_platform_flash_lock();
            return BOOT_ERR_FLASH;
        }

        bootloader_set_operation(ctx, BOOT_OPERATION_STORE_METADATA, copy + 1U, copy_count);
    }

    boot_platform_flash_lock();
    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_begin_session(bootloader_t *ctx, const boot_begin_request_t *req)
{
    const boot_target_config_t *cfg = boot_platform_target();
    const uint32_t slot_size = cfg->metadata_base - cfg->app_base;
    uint32_t current_version = 0U;
    uint32_t rollback_floor_version = 0U;
    boot_security_begin_t sec_req;

    memset(&sec_req, 0, sizeof(sec_req));
    sec_req.version = req->version;
    sec_req.flags = req->flags;
    sec_req.target_id = req->target_id;
    sec_req.image_size = req->image_size;
    sec_req.expected_crc32 = req->image_crc32;
    sec_req.has_current_version = bootloader_current_version(&current_version);
    sec_req.current_version = current_version;
    sec_req.has_rollback_floor = bootloader_effective_rollback_floor(&rollback_floor_version);
    sec_req.rollback_floor_version = rollback_floor_version;

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

    {
        const boot_error_t sec_err = boot_security_begin(&sec_req);
        if (sec_err != BOOT_ERR_NONE)
        {
            return sec_err;
        }
    }

    ctx->status = BOOT_STATUS_ERASING;
    boot_platform_flash_unlock();

    if (bootloader_erase_range(ctx, cfg->app_base, req->image_size, BOOT_OPERATION_ERASE_APP) != BOOT_ERR_NONE)
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
    {
        const boot_error_t sec_err = boot_security_transform_chunk(prefix->offset, chunk_buffer, prefix->data_len);
        if (sec_err != BOOT_ERR_NONE)
        {
            return sec_err;
        }
    }
    chunk = chunk_buffer;

    if (boot_security_note_plaintext(chunk, prefix->data_len) != BOOT_ERR_NONE)
    {
        return BOOT_ERR_BAD_STATE;
    }

    boot_platform_flash_unlock();
    if (boot_platform_flash_write(address, chunk, prefix->data_len) != BOOT_ERR_NONE)
    {
        boot_platform_flash_lock();
        return BOOT_ERR_FLASH;
    }
    boot_platform_flash_lock();

    ctx->session.running_crc32 = boot_crc32_update(ctx->session.running_crc32, chunk, prefix->data_len);
    ctx->session.written_size += prefix->data_len;
    bootloader_set_operation(ctx,
                             BOOT_OPERATION_WRITE_DATA,
                             ctx->session.written_size,
                             ctx->session.expected_size);
    return BOOT_ERR_NONE;
}

static boot_error_t bootloader_commit_session(bootloader_t *ctx, const boot_commit_request_t *req)
{
    boot_metadata_t metadata;
    boot_security_state_t new_security_state;
    boot_security_state_t current_security_state;
    boot_security_state_t *security_state_to_store = NULL;
    boot_verify_image_response_t verify_rsp;

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

    {
        const boot_error_t sec_err = boot_security_finalize_metadata(&metadata);
        if (sec_err != BOOT_ERR_NONE)
        {
            return sec_err;
        }
    }

    if (((metadata.flags & BOOT_FLAG_ANTI_ROLLBACK) != 0U) ||
        (((metadata.flags & BOOT_FLAG_VERIFY_SIGNATURE) != 0U) &&
         (AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED != 0)))
    {
        uint32_t floor_version = metadata.version;

        if (boot_security_state_select_valid(boot_platform_target(), &current_security_state) &&
            (current_security_state.rollback_floor_version > floor_version))
        {
            floor_version = current_security_state.rollback_floor_version;
        }

        bootloader_security_state_with_floor(boot_platform_target(), &new_security_state, floor_version);
        security_state_to_store = &new_security_state;
    }

    ctx->status = BOOT_STATUS_VERIFYING;
    if (bootloader_verify_metadata_image(ctx, &metadata, security_state_to_store, &verify_rsp) != BOOT_ERR_NONE)
    {
        return (boot_error_t)verify_rsp.result_error;
    }

    if (bootloader_store_metadata(ctx, &metadata, security_state_to_store) != BOOT_ERR_NONE)
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

        case BOOT_OP_SET_MANIFEST:
            if (frame->header.payload_len != sizeof(boot_manifest_t))
            {
                err = BOOT_ERR_RANGE;
            }
            else
            {
                err = boot_security_set_manifest((const boot_manifest_t *)frame->payload);
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
            bootloader_set_operation(ctx, BOOT_OPERATION_NONE, 0U, 0U);
            ctx->pending_action = BOOT_PENDING_ACTION_NONE;
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

        case BOOT_OP_GET_METADATA:
        {
            boot_metadata_response_t rsp;

            if (frame->header.payload_len != 0U)
            {
                err = BOOT_ERR_RANGE;
                break;
            }

            bootloader_read_metadata(ctx, &rsp);
            bootloader_send_response(frame->header.opcode, frame->header.sequence, &rsp, sizeof(rsp));
            return;
        }

        case BOOT_OP_CLEAR_METADATA:
            if (frame->header.payload_len != 0U)
            {
                err = BOOT_ERR_RANGE;
            }
            else
            {
                err = bootloader_clear_metadata(ctx);
            }
            break;

        case BOOT_OP_REBOOT:
            if (frame->header.payload_len != 0U)
            {
                err = BOOT_ERR_RANGE;
                break;
            }
            ctx->last_error = BOOT_ERR_NONE;
            bootloader_set_operation(ctx, BOOT_OPERATION_REBOOT, 1U, 1U);
            bootloader_send_status(ctx, frame->header.opcode, frame->header.sequence);
            bootloader_schedule_action(ctx, BOOT_PENDING_ACTION_REBOOT, 0U);
            return;

        case BOOT_OP_VERIFY_IMAGE:
        {
            boot_metadata_t metadata;
            boot_verify_image_response_t rsp;

            if (frame->header.payload_len != 0U)
            {
                err = BOOT_ERR_RANGE;
                break;
            }

            ctx->status = BOOT_STATUS_VERIFYING;
            if (boot_metadata_select_valid(boot_platform_target(), &metadata))
            {
                err = bootloader_verify_metadata_image(ctx, &metadata, NULL, &rsp);
            }
            else
            {
                memset(&rsp, 0, sizeof(rsp));
                rsp.result_error = (uint32_t)BOOT_ERR_NO_VALID_APP;
                err = BOOT_ERR_NO_VALID_APP;
            }

            ctx->last_error = err;
            ctx->status = (err == BOOT_ERR_NONE) ? BOOT_STATUS_READY : BOOT_STATUS_ERROR;
            bootloader_send_response(frame->header.opcode, frame->header.sequence, &rsp, sizeof(rsp));
            return;
        }

        case BOOT_OP_BOOT_APP:
        {
            boot_metadata_t metadata;
            boot_verify_image_response_t verify_rsp;

            if (boot_metadata_select_valid(boot_platform_target(), &metadata) &&
                (bootloader_verify_metadata_image(ctx, &metadata, NULL, &verify_rsp) == BOOT_ERR_NONE))
            {
                ctx->metadata_shadow = metadata;
                ctx->last_error = BOOT_ERR_NONE;
                bootloader_set_operation(ctx, BOOT_OPERATION_BOOT_APP, 1U, 1U);
                bootloader_send_status(ctx, frame->header.opcode, frame->header.sequence);
                bootloader_schedule_action(ctx, BOOT_PENDING_ACTION_BOOT_APP, metadata.app_base);
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
    ctx->operation = BOOT_OPERATION_NONE;
    ctx->pending_action = BOOT_PENDING_ACTION_NONE;
}

void bootloader_poll(bootloader_t *ctx)
{
    boot_platform_transport_poll();
    boot_platform_watchdog_kick();

    if (ctx->pending_action == BOOT_PENDING_ACTION_NONE)
    {
        return;
    }

    if ((int32_t)(boot_platform_millis() - ctx->pending_action_due_ms) < 0)
    {
        return;
    }

    switch (ctx->pending_action)
    {
        case BOOT_PENDING_ACTION_REBOOT:
            boot_platform_reboot();
            break;

        case BOOT_PENDING_ACTION_BOOT_APP:
            boot_platform_jump_to_app(ctx->pending_app_base);
            break;

        case BOOT_PENDING_ACTION_NONE:
        default:
            break;
    }

    ctx->pending_action = BOOT_PENDING_ACTION_NONE;
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
    boot_metadata_t metadata;
    boot_verify_image_response_t verify_rsp;

    if (boot_platform_force_bootloader())
    {
        return;
    }

    if (!boot_metadata_select_valid(boot_platform_target(), &metadata))
    {
        ctx->last_error = BOOT_ERR_NO_VALID_APP;
        ctx->status = BOOT_STATUS_ERROR;
        return;
    }

    if (bootloader_verify_metadata_image(ctx, &metadata, NULL, &verify_rsp) != BOOT_ERR_NONE)
    {
        ctx->last_error = (boot_error_t)verify_rsp.result_error;
        ctx->status = BOOT_STATUS_ERROR;
        return;
    }

    ctx->metadata_shadow = metadata;
    boot_platform_jump_to_app(metadata.app_base);
}
