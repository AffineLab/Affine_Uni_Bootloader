#ifndef AFFINE_BOOT_SECURITY_H
#define AFFINE_BOOT_SECURITY_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_image.h"
#include "boot_protocol.h"

#define BOOT_CAP_VERIFY_SIGNATURE   (1UL << 0)
#define BOOT_CAP_ENCRYPTED_STREAM   (1UL << 1)
#define BOOT_CAP_ANTI_ROLLBACK      (1UL << 2)
#define BOOT_CAP_UNSIGNED_UPDATES   (1UL << 3)

#define BOOT_FLAG_VERIFY_SIGNATURE  (1UL << 0)
#define BOOT_FLAG_ENCRYPTED_IMAGE   (1UL << 1)
#define BOOT_FLAG_ANTI_ROLLBACK     (1UL << 2)

typedef struct
{
    uint32_t version;
    uint32_t flags;
    uint32_t target_id;
    uint32_t image_size;
    uint32_t expected_crc32;
    uint32_t current_version;
    uint32_t rollback_floor_version;
    bool has_current_version;
    bool has_rollback_floor;
} boot_security_begin_t;

void boot_security_reset(void);
boot_error_t boot_security_begin(const boot_security_begin_t *request);
boot_error_t boot_security_set_manifest(const boot_manifest_t *manifest);
boot_error_t boot_security_transform_chunk(uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length);
boot_error_t boot_security_note_plaintext(const uint8_t *data, uint32_t length);
boot_error_t boot_security_finalize_metadata(boot_metadata_t *metadata);
boot_error_t boot_security_verify_image(const boot_target_config_t *cfg,
                                        const boot_metadata_t *metadata);
boot_error_t boot_security_verify_image_with_state(const boot_target_config_t *cfg,
                                                   const boot_metadata_t *metadata,
                                                   const boot_security_state_t *candidate_state);
uint32_t boot_security_capabilities(void);
bool boot_security_supports_flags(uint32_t flags);

#endif
