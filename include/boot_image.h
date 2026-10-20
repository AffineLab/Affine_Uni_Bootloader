#ifndef AFFINE_BOOT_IMAGE_H
#define AFFINE_BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_METADATA_MAGIC        0x4D544641UL
/*
 * Layout of boot_metadata_t. Records written before this field existed (352 to
 * 368 bytes, format_version reads 0) are not read back: every bootloader
 * upgrade goes through the updater app, which occupies the application slot,
 * so the real application is flashed again afterwards anyway.
 */
#define BOOT_METADATA_FORMAT_VERSION 2UL
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
    uint32_t board_id;
    uint32_t flash_layout_id;
    uint32_t version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t app_base;
    uint32_t flags;
    uint32_t metadata_crc32;
    uint32_t key_id;
    uint32_t security_version;
    uint32_t min_source_version;
    uint32_t format_version; /* BOOT_METADATA_FORMAT_VERSION */
    uint32_t metadata_reserved[2]; /* keep metadata aligned to the H503 16-byte flash write unit */
    uint8_t image_sha256[32];
    uint8_t encryption_nonce[16];
    uint8_t manifest_signature[256];
} boot_metadata_t;

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t target_id;
    uint32_t board_id;
    uint32_t flash_layout_id;
    uint32_t rollback_floor_version;
    uint32_t flags;
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
    uint32_t security_version;
    uint32_t min_source_version;
    uint32_t app_erased; /* 1 once the pages this session writes are erased */
} boot_session_t;

typedef struct
{
    uint32_t flash_base;
    uint32_t flash_size;
    uint32_t app_base;
    uint32_t app_size;
    uint32_t metadata_base;
    uint32_t metadata_size;
    uint32_t sram_base;
    uint32_t sram_size;
    uint32_t erase_size;
    uint32_t write_size;
    uint32_t max_chunk_size;
    uint32_t target_id;
    uint32_t board_id;
    uint32_t board_revision;
    uint32_t flash_layout_id;
} boot_target_config_t;

static inline uint32_t boot_metadata_copy_count(const boot_target_config_t *cfg)
{
    return (cfg->metadata_size >= (2U * cfg->erase_size)) ? 2U : 1U;
}

static inline uint32_t boot_metadata_copy_address(const boot_target_config_t *cfg, uint32_t copy_index)
{
    return cfg->metadata_base + (copy_index * cfg->erase_size);
}

/*
 * The security state sits at a fixed offset in each metadata erase unit, so a
 * metadata layout change no longer moves it (and loses the rollback floor).
 * Older bootloaders kept it right behind the metadata record, rounded up to
 * the write unit; boot_security_state_select_valid() still finds it there.
 */
#define BOOT_SECURITY_STATE_OFFSET 0x200U

static inline uint32_t boot_security_state_offset(const boot_target_config_t *cfg)
{
    (void)cfg;
    return BOOT_SECURITY_STATE_OFFSET;
}

static inline uint32_t boot_security_state_copy_address(const boot_target_config_t *cfg, uint32_t copy_index)
{
    return boot_metadata_copy_address(cfg, copy_index) + boot_security_state_offset(cfg);
}

/* Copies flash into RAM. False if a flash ECC error was raised while reading,
 * in which case out holds nothing usable. */
bool boot_flash_read(uint32_t address, void *out, uint32_t length);

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
bool boot_metadata_read_copy(const boot_target_config_t *cfg, uint32_t copy_index, boot_metadata_t *out_metadata);
bool boot_security_state_is_valid(const boot_target_config_t *cfg, const boot_security_state_t *state);
bool boot_security_state_select_valid(const boot_target_config_t *cfg, boot_security_state_t *out_state);
bool boot_image_crc_is_valid(const boot_metadata_t *metadata);
bool boot_image_is_committed_and_valid(const boot_target_config_t *cfg, const boot_metadata_t *metadata);

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(boot_metadata_t) == 368U, "boot_metadata_t size mismatch");
_Static_assert((sizeof(boot_metadata_t) % 16U) == 0U, "boot_metadata_t must be an H503 flash-quadword (16B) multiple");
_Static_assert(sizeof(boot_metadata_t) <= BOOT_SECURITY_STATE_OFFSET, "metadata record overlaps the security state");
_Static_assert((BOOT_SECURITY_STATE_OFFSET % 16U) == 0U, "security state must start on a flash write unit");
/* 2 KiB: the smallest metadata erase unit of any target (F072, G431 pages). */
_Static_assert((BOOT_SECURITY_STATE_OFFSET + sizeof(boot_security_state_t)) <= 2048U,
               "security state does not fit into a 2 KiB erase unit");
#endif

#endif
