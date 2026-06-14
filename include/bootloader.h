#ifndef AFFINE_BOOTLOADER_H
#define AFFINE_BOOTLOADER_H

#include <stddef.h>
#include <stdint.h>

#include "boot_image.h"
#include "boot_protocol.h"

typedef enum
{
    BOOT_STATUS_IDLE = 0,
    BOOT_STATUS_READY,
    BOOT_STATUS_RECEIVING,
    BOOT_STATUS_COMMITTED,
    BOOT_STATUS_ERROR,
    BOOT_STATUS_ERASING,
    BOOT_STATUS_VERIFYING
} boot_status_t;

typedef enum
{
    BOOT_PENDING_ACTION_NONE = 0,
    BOOT_PENDING_ACTION_REBOOT,
    BOOT_PENDING_ACTION_BOOT_APP
} boot_pending_action_t;

typedef struct
{
    boot_status_t status;
    boot_error_t last_error;
    boot_session_t session;
    boot_frame_decoder_t decoder;
    boot_metadata_t metadata_shadow;
    uint32_t frame_ok_count;
    uint32_t decode_error_count;
    uint32_t last_decoded_opcode;
    uint32_t last_decoded_sequence;
    uint32_t last_decode_error;
    boot_operation_t operation;
    uint32_t operation_progress;
    uint32_t operation_total;
    boot_pending_action_t pending_action;
    uint32_t pending_action_due_ms;
    uint32_t pending_app_base;
} bootloader_t;

void bootloader_init(bootloader_t *ctx);
void bootloader_poll(bootloader_t *ctx);
void bootloader_receive_bytes(bootloader_t *ctx, const uint8_t *data, size_t length);
void bootloader_try_boot_app(bootloader_t *ctx);

#endif
