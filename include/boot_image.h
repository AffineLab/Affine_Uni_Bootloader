#ifndef AFFINE_BOOT_IMAGE_H
#define AFFINE_BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_METADATA_MAGIC        0x4D544641UL
#define BOOT_SESSION_STATE_IDLE    0U
#define BOOT_SESSION_STATE_OPEN    1U
#define BOOT_SESSION_STATE_DONE    2U

typedef struct
{
    uint32_t magic;
    uint32_t target_id;
    uint32_t version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t app_base;
    uint32_t flags;
    uint32_t reserved[9];
} boot_metadata_t;

typedef struct
{
    uint32_t state;
    uint32_t version;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t written_size;
    uint32_t running_crc32;
} boot_session_t;

typedef struct
{
    uint32_t flash_base;
    uint32_t flash_size;
    uint32_t app_base;
    uint32_t metadata_base;
    uint32_t metadata_size;
    uint32_t sram_base;
    uint32_t sram_size;
    uint32_t erase_size;
    uint32_t write_size;
    uint32_t max_chunk_size;
    uint32_t target_id;
    const char *target_name;
} boot_target_config_t;

bool boot_image_vector_is_valid(const boot_target_config_t *cfg, uint32_t app_base);
bool boot_metadata_is_valid(const boot_target_config_t *cfg, const boot_metadata_t *metadata);

#endif
