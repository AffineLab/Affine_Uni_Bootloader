#ifndef AFFINE_BOOT_SECURITY_H
#define AFFINE_BOOT_SECURITY_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_image.h"
#include "boot_protocol.h"

#define BOOT_CAP_VERIFY_SIGNATURE   (1UL << 0)
#define BOOT_CAP_ENCRYPTED_STREAM   (1UL << 1)
#define BOOT_CAP_ANTI_ROLLBACK      (1UL << 2)

#define BOOT_FLAG_VERIFY_SIGNATURE  (1UL << 0)
#define BOOT_FLAG_ENCRYPTED_IMAGE   (1UL << 1)

typedef struct
{
    uint32_t version;
    uint32_t flags;
    uint32_t target_id;
    uint32_t image_size;
    uint32_t expected_crc32;
} boot_security_begin_t;

void boot_security_reset(void);
boot_error_t boot_security_begin(const boot_security_begin_t *request);
boot_error_t boot_security_transform_chunk(uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length);
boot_error_t boot_security_verify_image(const boot_target_config_t *cfg,
                                        const boot_metadata_t *metadata);
uint32_t boot_security_capabilities(void);
bool boot_security_supports_flags(uint32_t flags);

#endif
