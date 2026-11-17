#ifndef AFFINE_CH32_APP_ENTRY_H
#define AFFINE_CH32_APP_ENTRY_H

#include <stddef.h>

#include "boot_image.h"

typedef struct
{
    uint32_t entry;
    uint32_t mtvec;
} boot_ch32_app_entry_t;

/*
 * SDK D6 .init is a JAL x0 or C.J plus alignment padding, followed by
 * .vector[0] == _start at offset 4. Flash reads use the 0x08000000 window;
 * execution and absolute vector entries use the zero alias, as in SDK IAP.
 */
static inline bool boot_ch32_entry_prepare(const boot_target_config_t *cfg,
                                           uint32_t base,
                                           const uint32_t words[2],
                                           boot_ch32_app_entry_t *jump)
{
    const uint32_t insn = words[0];
    const uint32_t alias = base - cfg->flash_base;
    uint32_t imm;
    int32_t offset;
    if (base != cfg->app_base || base < cfg->flash_base || cfg->app_size < 8U || (base & 3U) != 0U ||
        words[1] != alias)
    {
        return false;
    }
    if ((insn & 0xFFFU) == 0x06FU)
    {
        imm = ((insn >> 31) << 20) | (((insn >> 12) & 255U) << 12) | (((insn >> 20) & 1U) << 11) |
            (((insn >> 21) & 1023U) << 1);
        offset = (int32_t)(imm << 11) >> 11;
    }
    else if ((insn & 0xE003U) == 0xA001U)
    {
        imm = (((insn >> 12) & 1U) << 11) | (((insn >> 11) & 1U) << 4) | (((insn >> 9) & 3U) << 8) |
            (((insn >> 8) & 1U) << 10) | (((insn >> 7) & 1U) << 6) | (((insn >> 6) & 1U) << 7) |
            (((insn >> 3) & 7U) << 1) | (((insn >> 2) & 1U) << 5);
        offset = (int32_t)(imm << 20) >> 20;
    }
    else
    {
        return false;
    }
    /* D6 vectors occupy 63 words, so reset code must follow the table. */
    if (offset < 256 || (uint32_t)offset >= cfg->app_size)
    {
        return false;
    }
    if (jump != NULL)
    {
        jump->entry = alias;
        jump->mtvec = (alias + 4U) | 3U;
    }
    return true;
}

static inline bool boot_ch32_request_consume(volatile uint32_t *request)
{
    if (*request != 0x424F4F54UL)
    {
        return false;
    }
    *request = 0U;
    return true;
}

#endif
