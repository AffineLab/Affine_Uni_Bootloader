#include "boot_security.h"

void boot_security_reset(void)
{
}

boot_error_t boot_security_begin(const boot_security_begin_t *request)
{
    (void)request;
    return BOOT_ERR_NONE;
}

boot_error_t boot_security_transform_chunk(uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length)
{
    (void)offset;
    (void)data;
    (void)length;
    return BOOT_ERR_NONE;
}

boot_error_t boot_security_verify_image(const boot_target_config_t *cfg,
                                        const boot_metadata_t *metadata)
{
    (void)cfg;
    (void)metadata;
    return BOOT_ERR_NONE;
}

uint32_t boot_security_capabilities(void)
{
    return 0U;
}

bool boot_security_supports_flags(uint32_t flags)
{
    return flags == 0U;
}
