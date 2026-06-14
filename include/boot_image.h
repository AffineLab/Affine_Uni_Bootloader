#ifndef AFFINE_BOOT_IMAGE_H
#define AFFINE_BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_METADATA_MAGIC        0x4D544641UL
#define BOOT_SECURITY_STATE_MAGIC  0x53534641UL
#define BOOT_SECURITY_STATE_VERSION 1UL
#define BOOT_SECURITY_STATE_FLAG_ROLLBACK_FLOOR_VALID (1UL << 0)
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
    uint32_t metadata_crc32;
    uint32_t key_id;
    uint32_t security_reserved[3];
    uint8_t image_sha256[32];
    uint8_t encryption_nonce[16];
    uint8_t manifest_signature[256];
} boot_metadata_t;

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t target_id;
    uint32_t rollback_floor_version;
    uint32_t flags;
    uint32_t reserved[2];
    uint32_t state_crc32;
} boot_security_state_t;

typedef struct
{
    uint32_t state;
    uint32_t version;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t written_size;
    uint32_t running_crc32;
    uint32_t flags;
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
} boot_target_config_t;

static inline uint32_t boot_metadata_copy_count(const boot_target_config_t *cfg)
{
    return (cfg->metadata_size >= (2U * cfg->erase_size)) ? 2U : 1U;
}

static inline uint32_t boot_metadata_copy_address(const boot_target_config_t *cfg, uint32_t copy_index)
{
    return cfg->metadata_base + (copy_index * cfg->erase_size);
}

static inline uint32_t boot_security_state_offset(const boot_target_config_t *cfg)
{
    const uint32_t align = (cfg->write_size == 0U) ? 4U : cfg->write_size;
    return ((uint32_t)sizeof(boot_metadata_t) + align - 1U) & ~(align - 1U);
}

static inline uint32_t boot_security_state_copy_address(const boot_target_config_t *cfg, uint32_t copy_index)
{
    return boot_metadata_copy_address(cfg, copy_index) + boot_security_state_offset(cfg);
}

uint32_t boot_metadata_crc32(const boot_metadata_t *metadata);
void boot_metadata_update_crc(boot_metadata_t *metadata);
uint32_t boot_security_state_crc32(const boot_security_state_t *state);
void boot_security_state_update_crc(boot_security_state_t *state);
void boot_security_state_prepare(const boot_target_config_t *cfg,
                                 boot_security_state_t *state,
                                 uint32_t rollback_floor_version);
bool boot_image_vector_is_valid(const boot_target_config_t *cfg, uint32_t app_base);
bool boot_metadata_is_valid(const boot_target_config_t *cfg, const boot_metadata_t *metadata);
bool boot_metadata_select_valid(const boot_target_config_t *cfg, boot_metadata_t *out_metadata);
bool boot_security_state_is_valid(const boot_target_config_t *cfg, const boot_security_state_t *state);
bool boot_security_state_select_valid(const boot_target_config_t *cfg, boot_security_state_t *out_state);
bool boot_image_crc_is_valid(const boot_metadata_t *metadata);
bool boot_image_is_committed_and_valid(const boot_target_config_t *cfg, const boot_metadata_t *metadata);

#endif
