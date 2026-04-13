#include "boot_image.h"

static bool boot_address_in_sram(const boot_target_config_t *cfg, uint32_t addr)
{
    return (addr >= cfg->sram_base) && (addr <= (cfg->sram_base + cfg->sram_size));
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

    if ((cfg->app_base + metadata->image_size) > cfg->metadata_base)
    {
        return false;
    }

    return true;
}
