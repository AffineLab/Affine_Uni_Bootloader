#include <assert.h>
#include <string.h>
#include "sim_os.h"
#include "boot_platform.h"
#include "bootloader.h"
#include "boot_crypto.h"
#include "targets/ch32v203/board_config.h"
#include "targets/ch32v203/app_entry.h"
#include "ch32v20x.h"
#include "targets/ch32v203/flash_hw.h"

static uint8_t *flash;
static unsigned erases, programs, kicks, unlocks, locks;
static unsigned fast_pages, modekey_writes, modekey_stage;
static uint32_t fast_error_flags;
static bool corrupt_fast, reject_modekey;
FLASH_Type host_flash = {.CTLR = FLASH_CTLR_LOCK | FLASH_CTLR_FAST_LOCK};
static bool fail_erase, fail_program, corrupt_erase, corrupt_program;
static bool programmed[65536 / 2];
static bootloader_t ctx;
static uint8_t response[BOOT_FRAME_MAX_PAYLOAD];
static uint16_t sequence;
static unsigned jumps;

uint32_t boot_platform_millis(void)
{
    return 0U;
}

bool boot_platform_force_bootloader(void)
{
    return false;
}

void boot_platform_transport_poll(void)
{
}

bool boot_platform_transport_get_diag(boot_diag_response_t *diag)
{
    (void)diag;
    return false;
}

void boot_platform_reboot(void)
{
}

void boot_platform_jump_to_app(uint32_t base)
{
    assert(base == g_target_config.app_base);
    ++jumps;
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    boot_frame_header_t header;
    assert(length >= sizeof(header));
    memcpy(&header, data, sizeof(header));
    assert(header.payload_len <= sizeof(response));
    memcpy(response, data + sizeof(header), header.payload_len);
}

static void fill_erased(uint32_t address, uint32_t length)
{
    for (uint32_t offset = 0U; offset < length; offset += 2U)
    {
        *(uint16_t *)(flash + address - BOOT_CH32_FLASH_BASE + offset) = BOOT_CH32_ERASED_HALFWORD;
        programmed[(address - BOOT_CH32_FLASH_BASE + offset) / 2U] = false;
    }
}

SysTick_Type host_systick;
PFIC_Type host_pfic;
static unsigned stage, mask;

uint32_t boot_ch32_irq_save(void)
{
    uint32_t old = mask;
    mask = 1;
    return old;
}

void boot_ch32_irq_restore(uint32_t old)
{
    assert(mask == 1);
    assert(modekey_stage == 0U && (FLASH->CTLR & FLASH_CTLR_FAST_LOCK) != 0U);
    mask = old;
}

void __disable_irq(void)
{
    assert(stage == 0);
    stage = 1;
}

void boot_ch32_usb_deinit(void)
{
    assert(stage == 1);
    stage = 2;
}

void RCC_DeInit(void)
{
    assert(stage == 2 && !host_systick.CTLR && !host_systick.SR && !host_systick.CNT && !host_systick.CMP);
    for (unsigned i = 0; i < 8; i++)
    {
        assert(host_pfic.IRER[i] == UINT32_MAX && host_pfic.IPRR[i] == UINT32_MAX);
    }
    for (unsigned i = 0; i < 4; i++)
    {
        assert(!host_pfic.VTFADDR[i]);
    }
    stage = 3;
}

void boot_ch32_jump_prepare(void);

void boot_platform_watchdog_kick(void)
{
    assert(!mask);
    ++kicks;
}

void FLASH_Unlock(void)
{
    assert(!mask && modekey_stage == 0U);
    ++unlocks;
    FLASH->KEYR = BOOT_CH32_FLASH_KEY2;
    FLASH->CTLR &= ~FLASH_CTLR_LOCK;
}

void FLASH_Lock(void)
{
    assert(!mask && (FLASH->CTLR & FLASH_CTLR_FAST_LOCK) != 0U);
    ++locks;
    FLASH->CTLR |= FLASH_CTLR_LOCK;
}

void FLASH_ClearFlag(uint32_t f)
{
    assert(f == (FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR | FLASH_STATR_PGERR));
    FLASH->STATR &= ~f;
}

FLASH_Status FLASH_ErasePage(uint32_t addr)
{
    assert(mask && addr >= 0x08006000 && addr <= 0x0800F000 && !(addr & 4095));
    assert((FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
    ++erases;
    if (fail_erase)
    {
        return FLASH_TIMEOUT;
    }
    fill_erased(addr, 4096U);
    if (corrupt_erase)
    {
        flash[addr - BOOT_CH32_FLASH_BASE] = 0U;
    }
    return FLASH_COMPLETE;
}

FLASH_Status FLASH_ProgramHalfWord(uint32_t addr, uint16_t data)
{
    assert(mask && addr >= 0x08006000 && addr <= 0x0800FFFE && !(addr & 1));
    assert((FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
    assert((FLASH->CTLR & FLASH_CTLR_FAST_LOCK) != 0U);
    ++programs;
    if (fail_program)
    {
        FLASH->STATR |= FLASH_FLAG_WRPRTERR;
        return FLASH_ERROR_WRP;
    }
    uint16_t *p = (uint16_t *)(flash + addr - 0x08000000);
    assert(!programmed[(addr - BOOT_CH32_FLASH_BASE) / 2U]);
    programmed[(addr - BOOT_CH32_FLASH_BASE) / 2U] = true;
    *p = corrupt_program ? (uint16_t)(data ^ 1U) : data;
    FLASH->STATR |= FLASH_FLAG_EOP;
    return FLASH_COMPLETE;
}

void boot_ch32_flash_modekey_write(uint32_t key)
{
    assert(mask && (FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
    assert(FLASH->KEYR == BOOT_CH32_FLASH_KEY2);
    if (modekey_stage == 0U)
    {
        assert(key == BOOT_CH32_FLASH_KEY1);
        assert((FLASH->CTLR & FLASH_CTLR_FAST_LOCK) != 0U);
        modekey_stage = 1U;
    }
    else
    {
        assert(modekey_stage == 1U && key == BOOT_CH32_FLASH_KEY2);
        if (!reject_modekey)
        {
            FLASH->CTLR &= ~FLASH_CTLR_FAST_LOCK;
        }
        modekey_stage = 2U;
    }
    FLASH->MODEKEYR = key;
    ++modekey_writes;
}

void FLASH_Lock_Fast(void)
{
    assert(mask && modekey_stage == 2U);
    assert((FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
    FLASH->CTLR |= FLASH_CTLR_FAST_LOCK;
    modekey_stage = 0U;
}

FLASH_Status FLASH_GetStatus(void)
{
    assert(mask);
    /* Match the vendor helper: PGERR is deliberately omitted here. */
    if ((FLASH->STATR & 1U) != 0U)
    {
        return FLASH_BUSY;
    }
    return (FLASH->STATR & FLASH_FLAG_WRPRTERR) != 0U ? FLASH_ERROR_WRP : FLASH_COMPLETE;
}

void FLASH_ProgramPage_Fast(uint32_t address, uint32_t *data)
{
    assert(mask && modekey_stage == 2U && (FLASH->CTLR & FLASH_CTLR_FAST_LOCK) == 0U);
    assert((FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
    assert((address % 256U) == 0U && ((uintptr_t)data % 4U) == 0U);
    assert(address >= g_target_config.app_base && address + 256U <= g_target_config.metadata_base);
    assert(FLASH->MODEKEYR == BOOT_CH32_FLASH_KEY2 && FLASH->STATR == 0U);
    for (uint32_t offset = 0U; offset < 256U; offset += 2U)
    {
        const uint32_t index = (address + offset - BOOT_CH32_FLASH_BASE) / 2U;
        assert(!programmed[index]);
        programmed[index] = true;
    }
    memcpy(flash + address - BOOT_CH32_FLASH_BASE, data, 256U);
    if (corrupt_fast)
    {
        /* A late mismatch must be found by whole-page verification. */
        flash[address - BOOT_CH32_FLASH_BASE + 254U] ^= 1U;
    }
    FLASH->STATR |= FLASH_FLAG_EOP | fast_error_flags;
    ++fast_pages;
}

static void test_fast_pages(void)
{
    const uint32_t app = g_target_config.app_base;
    _Alignas(4) uint8_t storage[1025];
    uint8_t *data = storage + 1U;
    for (uint32_t i = 0U; i < sizeof(storage) - 1U; ++i)
    {
        data[i] = (uint8_t)i;
    }
    assert(((uintptr_t)data & 3U) != 0U);
    boot_platform_flash_unlock();
    assert(boot_platform_flash_erase(app, 8192U) == BOOT_ERR_NONE);
    unsigned fast_before = fast_pages;
    unsigned half_before = programs;
    const unsigned unlock_before = unlocks;
    const unsigned keys_before = modekey_writes;
    assert(boot_platform_flash_write(app, data, 256U) == BOOT_ERR_NONE);
    assert(fast_pages == fast_before + 1U && programs == half_before);
    assert(modekey_writes == keys_before + 2U && unlocks == unlock_before);
    assert((FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
    assert(memcmp(flash + app - BOOT_CH32_FLASH_BASE, data, 256U) == 0);
    assert(boot_platform_flash_write(app + 256U, data, 2U) == BOOT_ERR_NONE);
    assert(programs == half_before + 1U);

    /* Two partial calls never combine into a fast page, even in one sector. */
    fast_before = fast_pages;
    half_before = programs;
    assert(boot_platform_flash_write(app + 512U, data, 128U) == BOOT_ERR_NONE);
    assert(boot_platform_flash_write(app + 640U, data, 128U) == BOOT_ERR_NONE);
    assert(fast_pages == fast_before && programs == half_before + 128U);
    assert(boot_platform_flash_write(app + 768U, data, 512U) == BOOT_ERR_NONE);
    assert(fast_pages == fast_before + 2U && programs == half_before + 128U);

    /* A covered aligned page inside an unaligned call still uses fast mode. */
    fast_before = fast_pages;
    half_before = programs;
    assert(boot_platform_flash_write(app + 1282U, data, 510U) == BOOT_ERR_NONE);
    assert(fast_pages == fast_before + 1U && programs == half_before + 127U);
    assert(memcmp(flash + app - BOOT_CH32_FLASH_BASE + 1282U, data, 510U) == 0);
    assert(boot_platform_flash_write(app + 3840U, data, 512U) == BOOT_ERR_NONE);
    assert(fast_pages == fast_before + 3U);

    /* Every halfword must pass preflight before opening fast mode. */
    assert(boot_platform_flash_erase(app, 4096U) == BOOT_ERR_NONE);
    const uint16_t erased_payload = BOOT_CH32_ERASED_HALFWORD;
    assert(boot_platform_flash_write(app + 254U, (const uint8_t *)&erased_payload, 2U) == BOOT_ERR_NONE);
    fast_before = fast_pages;
    const unsigned preflight_keys = modekey_writes;
    assert(boot_platform_flash_write(app, data, 256U) == BOOT_ERR_FLASH);
    assert(fast_pages == fast_before && modekey_writes == preflight_keys);
    assert(boot_platform_flash_erase(app, 4096U) == BOOT_ERR_NONE);
    *(uint16_t *)(flash + app - BOOT_CH32_FLASH_BASE + 254U) = 0U;
    assert(boot_platform_flash_write(app, data, 256U) == BOOT_ERR_FLASH);
    assert(fast_pages == fast_before && modekey_writes == preflight_keys);

    for (unsigned failure = 0U; failure < 4U; ++failure)
    {
        boot_platform_flash_lock();
        boot_platform_flash_unlock();
        assert(boot_platform_flash_erase(app, 4096U) == BOOT_ERR_NONE);
        fast_error_flags = failure == 0U ? FLASH_STATR_PGERR :
                           failure == 1U ? FLASH_FLAG_WRPRTERR : 0U;
        corrupt_fast = failure == 2U;
        reject_modekey = failure == 3U;
        fast_before = fast_pages;
        half_before = programs;
        assert(boot_platform_flash_write(app, data, 256U) == BOOT_ERR_FLASH);
        assert(fast_pages == fast_before + (reject_modekey ? 0U : 1U) && programs == half_before);
        assert(!mask && (FLASH->CTLR & FLASH_CTLR_FAST_LOCK) != 0U);
        assert((FLASH->CTLR & FLASH_CTLR_LOCK) == 0U);
        /* All bits were consumed, even when unlock failed and reads stay E339. */
        assert(boot_platform_flash_write(app + 254U, data, 2U) == BOOT_ERR_FLASH);
        assert(fast_pages == fast_before + (reject_modekey ? 0U : 1U) && programs == half_before);
        fast_error_flags = 0U;
        corrupt_fast = false;
        reject_modekey = false;
    }

    boot_platform_flash_lock();
    boot_platform_flash_unlock();
    const unsigned erases_before = erases;
    const unsigned kicks_before = kicks;
    assert(boot_platform_flash_erase(app, g_target_config.app_size) == BOOT_ERR_NONE);
    fast_before = fast_pages;
    half_before = programs;
    for (uint32_t offset = 0U; offset < g_target_config.app_size; offset += 256U)
    {
        assert(boot_platform_flash_write(app + offset, data, 256U) == BOOT_ERR_NONE);
    }
    assert(erases == erases_before + 8U);
    assert(fast_pages == fast_before + 128U && programs == half_before);
    assert(kicks == kicks_before + 8U + 128U);
    boot_platform_flash_lock();
}

static void send_request(uint16_t opcode, const void *payload, uint32_t length)
{
    uint8_t frame[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
    boot_frame_header_t header =
    {
        BOOT_FRAME_MAGIC, opcode, sequence++, length, 0U, 0U
    };
    header.payload_crc32 = boot_crc32_update(0U, payload, length);
    header.header_crc32 = boot_crc32_update(0U, &header, sizeof(header) - 4U);
    memcpy(frame, &header, sizeof(header));
    if (length != 0U)
    {
        memcpy(frame + sizeof(header), payload, length);
    }
    bootloader_receive_bytes(&ctx, frame, sizeof(header) + length);
}

static uint32_t last_error(void)
{
    boot_status_response_t status;
    memcpy(&status, response, sizeof(status));
    return status.last_error;
}

static void read_fixture(const char *directory, const char *name, void *out, size_t size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE * file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(out, 1U, size, file) == size);
    assert(fgetc(file) == EOF);
    fclose(file);
}

static void test_updates(const char *directory)
{
    uint8_t image[514];
    read_fixture(directory, "ch32-app.bin", image, sizeof(image));
    assert(image[sizeof(image) - 1U] == 0xFFU);
    const uint32_t crc = boot_crc32_update(0U, image, sizeof(image));
    for (unsigned mode = 0U; mode < 3U; ++mode)
    {
        boot_manifest_t manifest;
        memset(&manifest, 0, sizeof(manifest));
        if (mode != 0U)
        {
            read_fixture(directory, mode == 1U ? "ch32-signed.manifest" : "ch32-encrypted.manifest",
                &manifest, sizeof(manifest));
        }
        bootloader_init(&ctx);
        boot_begin_request_t request =
        {
            g_target_config.target_id, g_target_config.board_id, g_target_config.flash_layout_id,
                sizeof(image), crc, mode ? manifest.firmware_version : 1U, mode ? manifest.flags : 0U,
                mode ? manifest.security_version : 0U, 0U
        };
        send_request(BOOT_OP_BEGIN, &request, sizeof(request));
        assert(last_error() == BOOT_ERR_NONE);
        if (mode != 0U)
        {
            send_request(BOOT_OP_SET_MANIFEST, &manifest, sizeof(manifest));
            assert(last_error() == BOOT_ERR_NONE);
        }
        uint8_t uploaded[sizeof(image)];
        memcpy(uploaded, image, sizeof(image));
        if (mode == 2U)
        {
            const uint8_t key[16] =
            {
                0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
            };
            boot_aes128_ctr_context_t aes;
            boot_aes128_ctr_init(&aes, key, manifest.encryption_nonce);
            boot_aes128_ctr_xcrypt(&aes, 0U, uploaded, sizeof(uploaded));
        }
        const unsigned fast_before = fast_pages;
        const unsigned half_before = programs;
        for (uint32_t offset = 0U; offset < sizeof(image); offset += 256U)
        {
            uint8_t payload[sizeof(boot_data_prefix_t) + 256U];
            const uint32_t length = sizeof(image) - offset > 256U ? 256U : sizeof(image) - offset;
            const boot_data_prefix_t prefix =
            {
                offset, length
            };
            memcpy(payload, &prefix, sizeof(prefix));
            memcpy(payload + sizeof(prefix), uploaded + offset, length);
            send_request(BOOT_OP_DATA, payload, sizeof(prefix) + length);
            assert(last_error() == BOOT_ERR_NONE);
            /* Repeated DATA is deduplicated instead of programming cells twice. */
            --sequence;
            send_request(BOOT_OP_DATA, payload, sizeof(prefix) + length);
            assert(last_error() == BOOT_ERR_NONE);
        }
        assert(fast_pages == fast_before + 2U && programs == half_before + 1U);
        boot_commit_request_t commit =
        {
            sizeof(image), crc
        };
        send_request(BOOT_OP_COMMIT, &commit, sizeof(commit));
        assert(last_error() == BOOT_ERR_NONE);
        assert(fast_pages == fast_before + 2U); /* Metadata records stay standard. */
        assert(memcmp(flash + 0x6000U, image, sizeof(image)) == 0);
        assert(*(uint16_t *)(flash + 0x6000U + sizeof(image)) == BOOT_CH32_ERASED_HALFWORD);
        boot_metadata_t metadata;
        assert(boot_metadata_select_valid(&g_target_config, &metadata));
        assert(boot_image_is_committed_and_valid(&g_target_config, &metadata));
        boot_security_state_t state;
        assert(boot_security_state_select_valid(&g_target_config, &state) == (mode != 0U));
        send_request(BOOT_OP_VERIFY_IMAGE, NULL, 0U);
        boot_verify_image_response_t verify;
        memcpy(&verify, response, sizeof(verify));
        assert(verify.result_error == BOOT_ERR_NONE);
        send_request(BOOT_OP_CLEAR_METADATA, NULL, 0U);
        assert(last_error() == BOOT_ERR_NONE);
        assert(!boot_metadata_select_valid(&g_target_config, NULL));
        assert(boot_security_state_select_valid(&g_target_config, &state) == (mode != 0U));
    }
}

int main(int argc, char * * argv)
{
    flash = sim_map_flash(0x08000000, 65536);
    assert(flash);
    fill_erased(BOOT_CH32_FLASH_BASE, BOOT_CH32_FLASH_SIZE);
    const boot_target_config_t *cfg = boot_platform_target();
    assert(cfg->flash_size == 65536 && cfg->write_size == 2);
    assert(cfg->app_base == 0x08006000 && cfg->app_size == 32768 && cfg->metadata_base == 0x0800E000);
    assert(boot_metadata_copy_count(cfg) == 2 && boot_metadata_copy_address(cfg, 1) == 0x0800F000);
    assert(cfg->sram_base + cfg->sram_size == BOOT_CH32_REQUEST_ADDRESS);
    uint8_t bytes[368];
    memset(bytes, 0xA5, sizeof(bytes));
    assert(!boot_metadata_select_valid(cfg, NULL));
    assert(!boot_security_state_select_valid(cfg, NULL));
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    bootloader_init(&ctx);
    bootloader_try_boot_app(&ctx);
    assert(jumps == 0U);
    boot_platform_flash_unlock();
    /* E339 readback alone is not proof of an erase in this boot. */
    assert(boot_platform_flash_write(cfg->app_base, bytes, 2) == BOOT_ERR_FLASH);
    assert(boot_platform_flash_erase(cfg->app_base, 8192) == BOOT_ERR_NONE);
    assert(boot_platform_flash_write(cfg->app_base, bytes + 1, 2) == BOOT_ERR_NONE);
    assert(boot_platform_flash_write(cfg->app_base + 4094, bytes, 4) == BOOT_ERR_NONE);
    assert(programs == 3 && !memcmp(flash + 0x6FFE, bytes, 4));
    assert(boot_platform_flash_erase(cfg->app_base + 2, 4095) == BOOT_ERR_NONE && erases == 4);
    for (unsigned i = 0x6000; i < 0x8000; i += 2)
    {
        assert(*(uint16_t *)(flash + i) == BOOT_CH32_ERASED_HALFWORD);
    }
    for (unsigned copy = 0; copy < 2; copy++)
    {
        uint32_t meta = boot_metadata_copy_address(cfg, copy);
        assert(boot_platform_flash_erase(meta, 4096) == BOOT_ERR_NONE);
        assert(boot_platform_flash_write(meta, bytes, sizeof(bytes)) == BOOT_ERR_NONE);
        assert(boot_platform_flash_write(meta + 0x200, bytes, 32) == BOOT_ERR_NONE);
    }
    assert(boot_platform_flash_erase(cfg->metadata_base, 4096) == BOOT_ERR_NONE);
    assert(*(uint16_t *)(flash + 0xE000) == BOOT_CH32_ERASED_HALFWORD && flash[0xF000] == 0xA5);
    /* independent copies */
    unsigned before = programs + erases;
    assert(boot_platform_flash_write(cfg->app_base + 1, bytes, 2) == BOOT_ERR_ALIGNMENT);
    assert(boot_platform_flash_write(cfg->app_base, bytes, 3) == BOOT_ERR_ALIGNMENT);
    assert(boot_platform_flash_write(0x08005FFE, bytes, 2) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_erase(0x08005FFF, 1) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_write(0x0800FFFE, bytes, 4) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_erase(0x0800FFFF, 2) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_erase(0xFFFFF000, 0x2000) == BOOT_ERR_RANGE);
    assert(boot_platform_flash_write(cfg->app_base, NULL, 2) == BOOT_ERR_RANGE);
    assert(programs + erases == before);
    assert(boot_platform_flash_write(0, NULL, 0) == BOOT_ERR_NONE);
    assert(boot_platform_flash_erase(0, 0) == BOOT_ERR_NONE);
    fail_program = true;
    assert(boot_platform_flash_write(cfg->app_base, bytes, 2) == BOOT_ERR_FLASH);
    fail_program = false;
    fail_erase = true;
    assert(boot_platform_flash_erase(cfg->app_base, 4096) == BOOT_ERR_FLASH);
    fail_erase = false;
    boot_platform_flash_lock();
    assert(unlocks == 1 && locks == 1 && kicks == programs + erases && !mask);
    /* The actual core dispatches to CH32 validation, not Arm vector rules. */
    uint32_t *entry = (uint32_t *)(flash + 0x6000);
    entry[0] = 0x1000006FU;
    entry[1] = cfg->app_base - cfg->flash_base;
    /* JAL x0,+256 */
    assert(boot_image_vector_is_valid(cfg, cfg->app_base));
    boot_ch32_app_entry_t jump;
    assert(boot_ch32_entry_prepare(cfg, cfg->app_base, entry, &jump));
    assert(jump.entry == 0x00006000U && jump.mtvec == 0x00006007U);
    entry[1] = cfg->app_base;
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    entry[1] = cfg->app_base - cfg->flash_base;
    entry[0] = 0xA201U;
    assert(boot_image_vector_is_valid(cfg, cfg->app_base));
    /* C.J +256 */
    entry[0] = 0x0000006FU;
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    /* self-loop */
    entry[0] = 0x100000EFU;
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    /* JAL ra */
    entry[0] = 0xFFDFF06FU;
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    /* backwards */
    entry[0] = 0x0000806FU;
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    /* +32 KiB */
    entry[0] = 0x1000006FU;
    entry[1] = 0;
    assert(!boot_image_vector_is_valid(cfg, cfg->app_base));
    assert(!boot_ch32_entry_prepare(cfg, cfg->app_base + 4, entry, &jump));
    volatile uint32_t request = BOOT_CH32_REQUEST_SIGNATURE;
    assert(boot_ch32_request_consume(&request) && request == 0 && !boot_ch32_request_consume(&request));
    request = 123;
    assert(!boot_ch32_request_consume(&request) && request == 123);
    memset(&host_systick, 0xA5, sizeof(host_systick));
    memset(&host_pfic, 0xA5, sizeof(host_pfic));
    boot_ch32_jump_prepare();
    assert(stage == 3);
    boot_platform_flash_unlock();
    /* Programmed E339 and FFFF are data, not evidence that a cell is blank. */
    assert(boot_platform_flash_erase(cfg->app_base, 4096) == BOOT_ERR_NONE);
    uint16_t same_as_erased = BOOT_CH32_ERASED_HALFWORD;
    assert(boot_platform_flash_write(cfg->app_base, (uint8_t *)& same_as_erased, 2) == BOOT_ERR_NONE);
    unsigned prior_programs = programs;
    assert(boot_platform_flash_write(cfg->app_base, (uint8_t *)& same_as_erased, 2) == BOOT_ERR_FLASH);
    assert(programs == prior_programs);
    uint16_t ones = 0xFFFFU;
    assert(boot_platform_flash_write(cfg->app_base + 2U, (uint8_t *)& ones, 2) == BOOT_ERR_NONE);
    assert(*(uint16_t *)(flash + 0x6002U) == ones);
    assert(boot_platform_flash_write(cfg->app_base + 2U, bytes, 2) == BOOT_ERR_FLASH);
    assert(boot_platform_flash_erase(cfg->app_base, 4096) == BOOT_ERR_NONE);
    *(uint16_t *)(flash + 0x6000U) = 0U;
    prior_programs = programs;
    assert(boot_platform_flash_write(cfg->app_base, bytes, 2) == BOOT_ERR_FLASH);
    assert(programs == prior_programs);
    corrupt_erase = true;
    assert(boot_platform_flash_erase(cfg->app_base, 4096) == BOOT_ERR_FLASH);
    corrupt_erase = false;
    assert(boot_platform_flash_write(cfg->app_base, bytes, 2) == BOOT_ERR_FLASH);
    assert(boot_platform_flash_erase(cfg->app_base, 4096) == BOOT_ERR_NONE);
    corrupt_program = true;
    assert(boot_platform_flash_write(cfg->app_base, bytes, 2) == BOOT_ERR_FLASH);
    corrupt_program = false;
    assert(boot_platform_flash_write(cfg->app_base, bytes, 2) == BOOT_ERR_FLASH);
    boot_platform_flash_lock();
    test_fast_pages();
    assert(argc == 2);
    test_updates(argv[1]);
    puts("ALL PASS: CH32 fast pages/MODEKEYR/FLOCK/errors/tails, E339, alias, unsigned/signed/encrypted updates");
    return 0;
}
