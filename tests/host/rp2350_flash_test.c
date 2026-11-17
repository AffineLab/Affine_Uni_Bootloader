#include <assert.h>
#include <string.h>
#include "sim_os.h"
#include "boot_platform.h"
#include "targets/rp2350/board_config.h"
#include "hardware/flash.h"
static uint8_t *flash;
static uint32_t mask, erases, programs, usb_polls, kicks;
static bool fail_program, fail_erase;
uint32_t save_and_disable_interrupts(void) { uint32_t old = mask; mask = 1; return old; }
void restore_interrupts(uint32_t old) { assert(mask == 1); mask = old; }
void boot_rp2350_usb_task(void) { assert(mask == 0); ++usb_polls; }
void boot_platform_watchdog_kick(void) { assert(mask == 0); ++kicks; }
void flash_range_erase(uint32_t offset, size_t length)
{
    assert(mask && offset % 4096 == 0 && length == 4096 && offset + length <= 0x200000);
    if (!fail_erase) memset(flash + offset, 0xff, length);
    ++erases;
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length)
{
    assert(mask && offset % 256 == 0 && length == 256 && offset + length <= 0x200000);
    if (!fail_program) for (unsigned i = 0; i < length; ++i) flash[offset+i] &= data[i];
    ++programs;
}
int main(void)
{
    flash = sim_map_flash(0x10000000, 0x200000); assert(flash);
    memset(flash, 0xff, 0x200000);
    uint32_t app = BOOT_RP2350_APP_BASE;
    uint8_t bytes[368]; memset(bytes, 0xA5, sizeof(bytes));
    assert(boot_platform_flash_write(app, bytes, 4) == BOOT_ERR_NONE);
    assert(boot_platform_flash_write(app+4, bytes, 4) == BOOT_ERR_NONE);
    assert(memcmp(flash+0x8000, bytes, 8) == 0 && flash[0x8008] == 0xff);
    assert(boot_platform_flash_write(app+252, bytes, 8) == BOOT_ERR_NONE);
    assert(programs == 4 && flash[0x80fb] == 0xff && flash[0x8104] == 0xff);
    assert(boot_platform_flash_erase(app, 4097) == BOOT_ERR_NONE && erases == 2);
    for (unsigned i = 0x8000; i < 0xa000; ++i) assert(flash[i] == 0xff);
    for (unsigned copy = 0; copy < 2; ++copy)
    {
        uint32_t meta = 0x101fe000 + copy * 4096;
        assert(boot_platform_flash_write(meta, bytes, sizeof(bytes)) == BOOT_ERR_NONE);
        assert(boot_platform_flash_write(meta+0x200, bytes, 32) == BOOT_ERR_NONE);
        assert(memcmp((void *)(uintptr_t)meta, bytes, sizeof(bytes)) == 0);
        assert(*(uint8_t *)(uintptr_t)(meta+368) == 0xff);
        assert(boot_platform_flash_erase(meta, 4096) == BOOT_ERR_NONE);
        /* clear-metadata preserves state without rewriting the record. */
        assert(boot_platform_flash_write(meta+0x200, bytes, 32) == BOOT_ERR_NONE);
        assert(*(uint32_t *)(uintptr_t)meta == UINT32_MAX);
    }
    uint32_t before = programs + erases;
    assert(boot_platform_flash_write(app, bytes, 3) == BOOT_ERR_ALIGNMENT);
    assert(boot_platform_flash_write(app+1, bytes, 4) == BOOT_ERR_ALIGNMENT);
    assert(boot_platform_flash_write(0x10007ffc, bytes, 4) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_erase(0x10007fff, 1) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_write(0x101ffffc, bytes, 8) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_erase(0xfffff000, 8192) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_write(app, NULL, 4) == BOOT_ERR_RANGE);
    assert(before == programs + erases);
    assert(boot_platform_flash_write(app, NULL, 0) == BOOT_ERR_NONE);
    fail_program = true; assert(boot_platform_flash_write(app, bytes, 4) == BOOT_ERR_FLASH);
    fail_program = false; assert(boot_platform_flash_write(app, bytes, 4) == BOOT_ERR_NONE);
    uint32_t ones = UINT32_MAX;
    assert(boot_platform_flash_write(app, (uint8_t *)&ones, 4) == BOOT_ERR_FLASH);
    fail_erase = true; assert(boot_platform_flash_erase(app, 4096) == BOOT_ERR_FLASH);
    assert(mask == 0 && usb_polls > 0 && kicks >= usb_polls);
    puts("ALL PASS: RP2350 NOR partial/cross-page writes, metadata/state, range, IRQ scope, readback failures");
    return 0;
}
