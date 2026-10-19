/*
 * A record right behind the vector table (0x080000C0) that tells the
 * application which bootloader it was started by. Four words:
 *
 *   magic, two words that mean nothing here, protocol version
 *
 * The guarded variant of the bootloader (boot_guard_stm32f072.c) keeps its
 * own record at the same address, with the version in the same fourth word,
 * so an application looks in one place and accepts either magic.
 */
#include <stdint.h>

#include "boot_protocol.h"

#define BOOT_INFO_MAGIC 0x49424641UL /* "AFBI" */

typedef struct
{
    uint32_t magic;
    uint32_t reserved[2];
    uint32_t protocol_version;
} boot_info_record_t;

__attribute__((section(".boot_info"), used))
const boot_info_record_t g_boot_info_record = {BOOT_INFO_MAGIC, {0U, 0U}, BOOT_PROTOCOL_VERSION};
