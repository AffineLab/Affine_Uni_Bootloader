#ifndef AFFINE_BOOT_PROTOCOL_H
#define AFFINE_BOOT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_image.h"

#define BOOT_PROTOCOL_VERSION      0x00010004UL
#define BOOT_FRAME_MAGIC           0x42464641UL
#define BOOT_FRAME_MAX_PAYLOAD     512U
#define BOOT_MANIFEST_MAGIC        0x4D464641UL
#define BOOT_MANIFEST_VERSION      2UL
#define BOOT_MANIFEST_SHA256_SIZE  32U
#define BOOT_MANIFEST_NONCE_SIZE   16U
#define BOOT_MANIFEST_SIGNATURE_SIZE 256U
#define BOOT_MANIFEST_SIZE         364U

typedef enum
{
    BOOT_OP_HELLO      = 0x0001U,
    BOOT_OP_BEGIN      = 0x0002U,
    BOOT_OP_DATA       = 0x0003U,
    BOOT_OP_COMMIT     = 0x0004U,
    BOOT_OP_ABORT      = 0x0005U,
    BOOT_OP_GET_STATUS = 0x0006U,
    BOOT_OP_BOOT_APP   = 0x0007U,
    BOOT_OP_GET_DIAG   = 0x0008U,
    BOOT_OP_GET_METADATA = 0x0009U,
    BOOT_OP_CLEAR_METADATA = 0x000AU,
    BOOT_OP_REBOOT = 0x000BU,
    BOOT_OP_VERIFY_IMAGE = 0x000CU,
    BOOT_OP_SET_MANIFEST = 0x000DU,
    BOOT_OP_GET_DEVICE_INFO = 0x000EU,
    BOOT_OP_RESPONSE   = 0x8000U
} boot_opcode_t;

typedef enum
{
    BOOT_OPERATION_NONE = 0,
    BOOT_OPERATION_ERASE_APP,
    BOOT_OPERATION_ERASE_METADATA,
    BOOT_OPERATION_WRITE_DATA,
    BOOT_OPERATION_VERIFY_IMAGE,
    BOOT_OPERATION_STORE_METADATA,
    BOOT_OPERATION_CLEAR_METADATA,
    BOOT_OPERATION_REBOOT,
    BOOT_OPERATION_BOOT_APP
} boot_operation_t;

typedef enum
{
    BOOT_ERR_NONE = 0,
    BOOT_ERR_BAD_MAGIC,
    BOOT_ERR_BAD_HEADER_CRC,
    BOOT_ERR_BAD_PAYLOAD_CRC,
    BOOT_ERR_UNSUPPORTED_OPCODE,
    BOOT_ERR_BAD_STATE,
    BOOT_ERR_BAD_TARGET,
    BOOT_ERR_RANGE,
    BOOT_ERR_ALIGNMENT,
    BOOT_ERR_FLASH,
    BOOT_ERR_IMAGE_TOO_LARGE,
    BOOT_ERR_IMAGE_CRC,
    BOOT_ERR_NO_VALID_APP,
    BOOT_ERR_MANIFEST,
    BOOT_ERR_SIGNATURE,
    BOOT_ERR_ROLLBACK,
    BOOT_ERR_DECRYPTION
} boot_error_t;

typedef struct
{
    uint32_t magic;
    uint16_t opcode;
    uint16_t sequence;
    uint32_t payload_len;
    uint32_t payload_crc32;
    uint32_t header_crc32;
} boot_frame_header_t;

typedef struct
{
    uint32_t target_id;
    uint32_t board_id;
    uint32_t flash_layout_id;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t version;
    uint32_t flags;
} boot_begin_request_t;

typedef struct
{
    uint32_t offset;
    uint32_t data_len;
} boot_data_prefix_t;

typedef struct
{
    uint32_t image_size;
    uint32_t image_crc32;
} boot_commit_request_t;

typedef struct
{
    uint32_t magic;
    uint32_t manifest_version;
    uint32_t target_id;
    uint32_t board_id;
    uint32_t flash_layout_id;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t firmware_version;
    uint32_t flags;
    uint32_t key_id;
    uint8_t image_sha256[BOOT_MANIFEST_SHA256_SIZE];
    uint8_t encryption_nonce[BOOT_MANIFEST_NONCE_SIZE];
    uint32_t reserved[5];
    uint8_t signature[BOOT_MANIFEST_SIGNATURE_SIZE];
} boot_manifest_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(boot_manifest_t) == BOOT_MANIFEST_SIZE, "boot_manifest_t size mismatch");
#endif

typedef struct
{
    uint32_t protocol_version;
    uint32_t target_id;
    uint32_t flash_size;
    uint32_t app_base;
    uint32_t slot_size;
    uint32_t max_chunk_size;
    uint32_t capabilities;
} boot_hello_response_t;

typedef struct
{
    uint32_t protocol_version;
    uint32_t target_id;
    uint32_t board_id;
    uint32_t board_revision;
    uint32_t flash_layout_id;
    uint32_t flash_base;
    uint32_t flash_size;
    uint32_t app_base;
    uint32_t metadata_base;
    uint32_t metadata_size;
    uint32_t erase_size;
    uint32_t write_size;
    uint32_t max_chunk_size;
    uint32_t capabilities;
} boot_device_info_response_t;

typedef struct
{
    uint32_t status;
    uint32_t last_error;
    uint32_t written_size;
    uint32_t expected_size;
    uint32_t running_crc32;
    uint32_t operation;
    uint32_t operation_progress;
    uint32_t operation_total;
} boot_status_response_t;

typedef struct
{
    uint32_t valid;
    uint32_t copy_count;
    uint32_t selected_copy;
    uint32_t app_valid;
    uint32_t security_state_valid;
    boot_metadata_t metadata;
    boot_security_state_t security_state;
} boot_metadata_response_t;

typedef struct
{
    uint32_t result_error;
    uint32_t metadata_valid;
    uint32_t vector_valid;
    uint32_t image_crc_valid;
    uint32_t security_valid;
    uint32_t image_size;
    uint32_t expected_crc32;
    uint32_t calculated_crc32;
    uint32_t app_base;
} boot_verify_image_response_t;

typedef struct
{
    uint32_t rx_callback_count;
    uint32_t rx_byte_count;
    uint32_t rx_last_len;
    uint32_t rx_fifo_overflow_count;
    uint32_t tx_enqueue_count;
    uint32_t tx_start_count;
    uint32_t tx_complete_count;
    uint32_t tx_busy_reject_count;
    uint32_t tx_queue_drop_count;
    uint32_t tx_last_len;
    uint32_t last_rx_tick_ms;
    uint32_t last_tx_start_tick_ms;
    uint32_t last_tx_complete_tick_ms;
    uint32_t frame_ok_count;
    uint32_t decode_error_count;
    uint32_t last_decoded_opcode;
    uint32_t last_decoded_sequence;
    uint32_t last_decode_error;
} boot_diag_response_t;

typedef struct
{
    uint8_t header_bytes[sizeof(boot_frame_header_t)];
    uint8_t payload[BOOT_FRAME_MAX_PAYLOAD];
    size_t  header_used;
    size_t  payload_used;
    boot_frame_header_t current_header;
} boot_frame_decoder_t;

typedef struct
{
    bool     frame_ready;
    boot_frame_header_t header;
    uint8_t  payload[BOOT_FRAME_MAX_PAYLOAD];
} boot_decoded_frame_t;

uint32_t boot_crc32_update(uint32_t seed, const void *data, size_t length);
void boot_decoder_reset(boot_frame_decoder_t *decoder);
boot_error_t boot_decoder_consume(boot_frame_decoder_t *decoder,
                                  const uint8_t *data,
                                  size_t length,
                                  boot_decoded_frame_t *out_frame);

#endif
