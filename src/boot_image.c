#include "boot_image.h"

#include <string.h>

#include "boot_protocol.h"

uint32_t boot_metadata_crc32(const boot_metadata_t *metadata)
{
    boot_metadata_t copy;

    if (metadata == NULL)
    {
        return 0U;
    }

    copy = *metadata;
    copy.metadata_crc32 = 0U;
    return boot_crc32_update(0U, &copy, sizeof(copy));
}

void boot_metadata_update_crc(boot_metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return;
    }

    metadata->metadata_crc32 = 0U;
    metadata->metadata_crc32 = boot_metadata_crc32(metadata);
}

uint32_t boot_security_state_crc32(const boot_security_state_t *state)
{
    boot_security_state_t copy;

    if (state == NULL)
    {
        return 0U;
    }

    copy = *state;
    copy.state_crc32 = 0U;
    return boot_crc32_update(0U, &copy, sizeof(copy));
}

void boot_security_state_update_crc(boot_security_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->state_crc32 = 0U;
    state->state_crc32 = boot_security_state_crc32(state);
}

void boot_security_state_prepare(const boot_target_config_t *cfg,
                                 boot_security_state_t *state,
                                 uint32_t rollback_floor_version)
{
    if ((cfg == NULL) || (state == NULL))
    {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->magic = BOOT_SECURITY_STATE_MAGIC;
    state->format_version = BOOT_SECURITY_STATE_VERSION;
    state->target_id = cfg->target_id;
    state->rollback_floor_version = rollback_floor_version;
    state->flags = BOOT_SECURITY_STATE_FLAG_ROLLBACK_FLOOR_VALID;
    boot_security_state_update_crc(state);
}

static bool boot_address_in_sram(const boot_target_config_t *cfg, uint32_t addr)
{
    const uint32_t sram_end = cfg->sram_base + cfg->sram_size;
    return (addr >= cfg->sram_base) && (addr <= sram_end);
}

static bool boot_address_in_app_slot(const boot_target_config_t *cfg, uint32_t addr)
{
    return (addr >= cfg->app_base) && (addr < cfg->metadata_base);
}

bool boot_image_vector_is_valid(const boot_target_config_t *cfg, uint32_t app_base)
{
    const uint32_t *vt = (const uint32_t *)app_base;
    const uint32_t initial_sp = vt[0];
    const uint32_t reset_handler = vt[1];
    const uint32_t reset_handler_addr = reset_handler & ~1UL;

    if (!boot_address_in_sram(cfg, initial_sp))
    {
        return false;
    }

    if ((reset_handler & 1U) == 0U)
    {
        return false;
    }

    return boot_address_in_app_slot(cfg, reset_handler_addr);
}

bool boot_metadata_is_valid(const boot_target_config_t *cfg, const boot_metadata_t *metadata)
{
    const uint32_t slot_size = cfg->metadata_base - cfg->app_base;

    if (metadata->magic != BOOT_METADATA_MAGIC)
    {
        return false;
    }

    if (metadata->target_id != cfg->target_id)
    {
        return false;
    }

    if (metadata->app_base != cfg->app_base)
    {
        return false;
    }

    if (metadata->image_size == 0U)
    {
        return false;
    }

    if (metadata->image_size > slot_size)
    {
        return false;
    }

    return (metadata->metadata_crc32 != 0U) && (metadata->metadata_crc32 == boot_metadata_crc32(metadata));
}

bool boot_metadata_select_valid(const boot_target_config_t *cfg, boot_metadata_t *out_metadata)
{
    const uint32_t copy_count = boot_metadata_copy_count(cfg);

    for (uint32_t copy = 0U; copy < copy_count; ++copy)
    {
        const uint32_t address = boot_metadata_copy_address(cfg, copy);
        const boot_metadata_t *candidate = (const boot_metadata_t *)address;

        if (!boot_metadata_is_valid(cfg, candidate))
        {
            continue;
        }

        if (out_metadata != NULL)
        {
            *out_metadata = *candidate;
        }
        return true;
    }

    return false;
}

bool boot_security_state_is_valid(const boot_target_config_t *cfg, const boot_security_state_t *state)
{
    const uint32_t state_end = boot_security_state_offset(cfg) + (uint32_t)sizeof(*state);

    if (state_end > cfg->erase_size)
    {
        return false;
    }

    if (state->magic != BOOT_SECURITY_STATE_MAGIC)
    {
        return false;
    }

    if (state->format_version != BOOT_SECURITY_STATE_VERSION)
    {
        return false;
    }

    if (state->target_id != cfg->target_id)
    {
        return false;
    }

    if ((state->flags & BOOT_SECURITY_STATE_FLAG_ROLLBACK_FLOOR_VALID) == 0U)
    {
        return false;
    }

    return (state->state_crc32 != 0U) && (state->state_crc32 == boot_security_state_crc32(state));
}

bool boot_security_state_select_valid(const boot_target_config_t *cfg, boot_security_state_t *out_state)
{
    const uint32_t copy_count = boot_metadata_copy_count(cfg);

    for (uint32_t copy = 0U; copy < copy_count; ++copy)
    {
        const uint32_t address = boot_security_state_copy_address(cfg, copy);
        const boot_security_state_t *candidate = (const boot_security_state_t *)address;

        if (!boot_security_state_is_valid(cfg, candidate))
        {
            continue;
        }

        if (out_state != NULL)
        {
            *out_state = *candidate;
        }
        return true;
    }

    return false;
}

bool boot_image_crc_is_valid(const boot_metadata_t *metadata)
{
    const uint8_t *image = (const uint8_t *)metadata->app_base;
    return boot_crc32_update(0U, image, metadata->image_size) == metadata->image_crc32;
}

bool boot_image_is_committed_and_valid(const boot_target_config_t *cfg, const boot_metadata_t *metadata)
{
    if (!boot_metadata_is_valid(cfg, metadata))
    {
        return false;
    }

    if (!boot_image_vector_is_valid(cfg, metadata->app_base))
    {
        return false;
    }

    return boot_image_crc_is_valid(metadata);
}
