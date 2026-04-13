#ifndef AFFINE_BOOT_PROTOCOL_H
#define AFFINE_BOOT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_PROTOCOL_VERSION      0x00010000UL
#define BOOT_FRAME_MAGIC           0x42464641UL
#define BOOT_FRAME_MAX_PAYLOAD     512U

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
    BOOT_OP_RESPONSE   = 0x8000U
} boot_opcode_t;

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
    BOOT_ERR_NO_VALID_APP
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
    uint32_t status;
    uint32_t last_error;
    uint32_t written_size;
    uint32_t expected_size;
    uint32_t running_crc32;
} boot_status_response_t;

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
