/*
 * The bootloader core in-process, with flash at 0x08000000 and the flash ECC
 * NMI emulated by poisoned pages.
 *
 *   core_test FIXTURE_DIR   (app.bin, plain/ar9/ar5.manifest from run.py)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_os.h"

#include "bootloader.h"
#include "boot_platform.h"
#include "boot_security.h"

#define FLASH_BASE 0x08000000UL
#define FLASH_SIZE 0x20000UL

static const boot_target_config_t g_cfg = {
    .flash_base = FLASH_BASE,
    .flash_size = FLASH_SIZE,
    .app_base = 0x08006000UL,
    .app_size = 0x16000UL,
    .metadata_base = 0x0801C000UL,
    .metadata_size = 0x4000UL,
    .sram_base = 0x20000000UL,
    .sram_size = 0x8000UL,
    .erase_size = 0x2000UL,
    .write_size = 8UL,
    .max_chunk_size = 256UL,
    .target_id = 0x47343331UL,
    .board_id = 0x4D414931UL,
    .board_revision = 1UL,
    .flash_layout_id = 0x47343341UL,
};

static uint8_t *g_flash;
static volatile uint32_t g_ecc;
static uint32_t g_millis;
static int g_force_calls;
static bool g_force_value;
static int g_erase_calls;
static int g_jumps;
static uint16_t g_rsp_opcode;
static uint8_t g_rsp[1024];
static uint32_t g_rsp_len;
static int g_failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static void poison(uint32_t address) { sim_poison(address, true); }
static void unpoison(uint32_t address) { sim_poison(address, false); }

const boot_target_config_t *boot_platform_target(void) { return &g_cfg; }
uint32_t boot_platform_millis(void) { return g_millis; }
bool boot_platform_force_bootloader(void) { g_force_calls++; bool v = g_force_value; g_force_value = false; return v; }
void boot_platform_transport_poll(void) {}
bool boot_platform_transport_get_diag(boot_diag_response_t *d) { memset(d, 0, sizeof(*d)); return true; }
void boot_platform_watchdog_kick(void) {}
void boot_platform_flash_unlock(void) {}
void boot_platform_flash_lock(void) {}
uint32_t boot_platform_flash_ecc_error_count(void) { return g_ecc; }
void boot_platform_jump_to_app(uint32_t app_base) { CHECK(app_base == g_cfg.app_base); g_jumps++; }
void boot_platform_reboot(void) {}

static int g_rsp_count;
static uint16_t g_rsp_sequence;
static int g_fail_writes; /* inject this many flash write failures */
static int g_data_writes;

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    boot_frame_header_t h;
    memcpy(&h, data, sizeof(h));
    g_rsp_opcode = h.opcode;
    g_rsp_sequence = h.sequence;
    g_rsp_len = h.payload_len;
    memcpy(g_rsp, data + sizeof(h), h.payload_len);
    g_rsp_count++;
    (void)length;
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    const uint32_t start = address & ~(g_cfg.erase_size - 1U);
    if (address < g_cfg.app_base || (address + length) > (FLASH_BASE + FLASH_SIZE))
        return BOOT_ERR_RANGE;
    g_erase_calls++;
    memset(g_flash + (start - FLASH_BASE), 0xFF, g_cfg.erase_size);
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint8_t *dst = g_flash + (address - FLASH_BASE);
    if ((address % 8U) || (length % 8U) || address < g_cfg.app_base)
        return BOOT_ERR_ALIGNMENT;
    if (address < g_cfg.metadata_base)
    {
        if (g_fail_writes > 0) { g_fail_writes--; return BOOT_ERR_FLASH; } /* fails before programming */
        g_data_writes++;
    }
    for (uint32_t i = 0; i < length; ++i)
        if (dst[i] != 0xFF) return BOOT_ERR_FLASH; /* NOR: program erased bits only */
    memcpy(dst, data, length);
    return BOOT_ERR_NONE;
}

static const char *g_fixtures = ".";

static uint8_t *load(const char *name, uint32_t *len)
{
    char path[1024];
    FILE *f;
    uint8_t *buf = malloc(65536);

    snprintf(path, sizeof(path), "%s/%s", g_fixtures, name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("cannot open %s\n", path);
        exit(2);
    }
    *len = (uint32_t)fread(buf, 1, 65536, f);
    fclose(f);
    return buf;
}

static bootloader_t g_ctx;
static uint16_t g_seq;

static void xsend(uint16_t opcode, const void *payload, uint32_t len)
{
    uint8_t frame[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
    boot_frame_header_t h = {BOOT_FRAME_MAGIC, opcode, g_seq++, len, 0, 0};
    h.payload_crc32 = boot_crc32_update(0, payload, len);
    h.header_crc32 = boot_crc32_update(0, &h, sizeof(h) - 4);
    memcpy(frame, &h, sizeof(h));
    if (len) memcpy(frame + sizeof(h), payload, len);
    g_rsp_opcode = 0;
    bootloader_receive_bytes(&g_ctx, frame, sizeof(h) + len);
    CHECK(g_rsp_opcode == (opcode | BOOT_OP_RESPONSE));
}

static uint32_t last_error(void) { return ((boot_status_response_t *)g_rsp)->last_error; }
static uint32_t last_status(void) { return ((boot_status_response_t *)g_rsp)->status; }

static void begin(const boot_manifest_t *m, uint32_t image_len, uint32_t crc, uint32_t flags, uint32_t sv)
{
    boot_begin_request_t b = {g_cfg.target_id, g_cfg.board_id, g_cfg.flash_layout_id, image_len, crc,
                              m ? m->firmware_version : 1U, m ? m->flags : flags, m ? m->security_version : sv,
                              m ? m->min_source_version : 0U};
    xsend(BOOT_OP_BEGIN, &b, sizeof(b));
}

static uint32_t send_data(const uint8_t *img, uint32_t len)
{
    uint8_t p[8 + 256];
    for (uint32_t off = 0; off < len; off += 256)
    {
        uint32_t n = (len - off) < 256 ? (len - off) : 256;
        ((boot_data_prefix_t *)p)->offset = off;
        ((boot_data_prefix_t *)p)->data_len = n;
        memcpy(p + 8, img + off, n);
        xsend(BOOT_OP_DATA, p, 8 + n);
        if (last_error() != 0) return last_error();
    }
    return 0;
}

static void commit(uint32_t len, uint32_t crc)
{
    boot_commit_request_t c = {len, crc};
    xsend(BOOT_OP_COMMIT, &c, sizeof(c));
}

static void reset_ctx(bool force)
{
    g_force_value = force;
    g_force_calls = 0;
    bootloader_init(&g_ctx);
    CHECK(g_force_calls == 1);
}

static uint32_t copy_addr(uint32_t copy) { return g_cfg.metadata_base + copy * g_cfg.erase_size; }

/* ---------------- lossy channel ---------------- */

static uint32_t g_rng = 1;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

typedef struct
{
    unsigned drop_byte, insert_byte, flip_bit;       /* per 100k bytes */
    unsigned truncate, drop_reply, noise_between;    /* per 1k frames */
} faults_t;

static struct { unsigned frames, sent, dropped, inserted, flipped, truncated, replies_lost, retries; } g_stat;

/* Feeds bytes in random chunk sizes, like USB packets or a UART FIFO drain,
 * with the clock moving a little per chunk. */
static void feed(const uint8_t *bytes, uint32_t n)
{
    uint32_t off = 0;
    while (off < n)
    {
        uint32_t c = 1 + rnd() % 64;
        if (c > n - off) c = n - off;
        bootloader_receive_bytes(&g_ctx, bytes + off, c);
        g_millis += 1;
        bootloader_poll(&g_ctx);
        off += c;
    }
}

static void idle(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 10) { g_millis += 10; bootloader_poll(&g_ctx); }
}

/* One request the way boot_test.py and the Hub send it: the same frame and
 * sequence again after a timeout, up to `tries` times. Returns true once a
 * matching answer arrives (and was not lost on the way back). */
static bool transact(const faults_t *f, uint16_t opcode, const void *payload, uint32_t len, int tries)
{
    uint8_t frame[sizeof(boot_frame_header_t) + BOOT_FRAME_MAX_PAYLOAD];
    uint8_t wire[2 * sizeof(frame) + 64];
    boot_frame_header_t h = {BOOT_FRAME_MAGIC, opcode, g_seq++, len, 0, 0};
    const uint32_t flen = sizeof(h) + len;

    h.payload_crc32 = boot_crc32_update(0, payload, len);
    h.header_crc32 = boot_crc32_update(0, &h, sizeof(h) - 4);
    memcpy(frame, &h, sizeof(h));
    if (len) memcpy(frame + sizeof(h), payload, len);
    g_stat.frames++;

    for (int attempt = 0; attempt < tries; ++attempt)
    {
        uint32_t w = 0;
        int before = g_rsp_count;

        if (attempt) g_stat.retries++;
        if (rnd() % 1000 < f->noise_between)
        {
            uint32_t k = 1 + rnd() % 8;
            for (uint32_t i = 0; i < k; ++i) wire[w++] = (rnd() & 1) ? 0x41 : (uint8_t)rnd(); /* likes 'A' */
        }
        {
            uint32_t cut = (rnd() % 1000 < f->truncate) ? (rnd() % flen) : flen;
            if (cut != flen) g_stat.truncated++;
            for (uint32_t i = 0; i < cut; ++i)
            {
                if (rnd() % 100000 < f->drop_byte) { g_stat.dropped++; continue; }
                if (rnd() % 100000 < f->insert_byte) { wire[w++] = (uint8_t)rnd(); g_stat.inserted++; }
                wire[w] = frame[i];
                if (rnd() % 100000 < f->flip_bit) { wire[w] ^= (uint8_t)(1u << (rnd() % 8)); g_stat.flipped++; }
                w++;
            }
        }
        g_stat.sent++;
        feed(wire, w);

        if (g_rsp_count != before && g_rsp_opcode == (opcode | BOOT_OP_RESPONSE) && g_rsp_sequence == h.sequence)
        {
            if (rnd() % 1000 < f->drop_reply) { g_stat.replies_lost++; }
            else return true;
        }
        idle(300); /* the host's response timeout, then it resends */
    }
    return false;
}

/* A whole update over the channel; true if it ended COMMITTED with the image
 * in flash. */
enum { UPD_OK = 0, UPD_GAVE_UP = 1, UPD_WRONG = 2 };
static int flash_over_channel(const faults_t *f, const uint8_t *img, uint32_t len, uint32_t crc,
                               const boot_manifest_t *m)
{
    const int tries = 12;
    boot_begin_request_t b = {g_cfg.target_id, g_cfg.board_id, g_cfg.flash_layout_id, len, crc,
                              m ? m->firmware_version : 1U, m ? m->flags : 0U, m ? m->security_version : 0U,
                              m ? m->min_source_version : 0U};
    uint8_t p[8 + 256];

    if (!transact(f, BOOT_OP_BEGIN, &b, sizeof(b), tries)) return UPD_GAVE_UP;
    if (last_error() != 0) { printf("  BEGIN error %u\n", (unsigned)last_error()); return UPD_WRONG; }
    if (m && !transact(f, BOOT_OP_SET_MANIFEST, m, sizeof(*m), tries)) return UPD_GAVE_UP;
    if (m && last_error() != 0) { printf("  SET_MANIFEST error %u\n", (unsigned)last_error()); return UPD_WRONG; }
    for (uint32_t off = 0; off < len; off += 256)
    {
        uint32_t n = (len - off) < 256 ? (len - off) : 256;
        ((boot_data_prefix_t *)p)->offset = off;
        ((boot_data_prefix_t *)p)->data_len = n;
        memcpy(p + 8, img + off, n);
        if (!transact(f, BOOT_OP_DATA, p, 8 + n, tries)) return UPD_GAVE_UP;
        if (last_error() != 0) { printf("  DATA @%u error %u\n", (unsigned)off, (unsigned)last_error()); return UPD_WRONG; }
    }
    {
        boot_commit_request_t c = {len, crc};
        if (!transact(f, BOOT_OP_COMMIT, &c, sizeof(c), tries)) return UPD_GAVE_UP;
        if (last_error() != 0 || last_status() != BOOT_STATUS_COMMITTED)
        {
            printf("  COMMIT error %u status %u\n", (unsigned)last_error(), (unsigned)last_status());
            return UPD_WRONG;
        }
    }
    return (memcmp(g_flash + (g_cfg.app_base - FLASH_BASE), img, len) == 0) ? UPD_OK : UPD_WRONG;
}

int main(int argc, char **argv)
{
    uint32_t img_len, mlen;
    uint8_t *img;
    uint32_t crc;
    boot_manifest_t plain, ar9, ar5;
    boot_metadata_response_t md;
    boot_security_state_t st;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1) g_fixtures = argv[1];
    img = load("app.bin", &img_len);
    crc = boot_crc32_update(0, img, img_len);
    memcpy(&plain, load("plain.manifest", &mlen), sizeof(plain));
    memcpy(&ar9, load("ar9.manifest", &mlen), sizeof(ar9));
    memcpy(&ar5, load("ar5.manifest", &mlen), sizeof(ar5));

    g_flash = sim_map_flash(FLASH_BASE, FLASH_SIZE);
    if (g_flash == NULL) return 2;
    memset(g_flash, 0xFF, FLASH_SIZE);
    sim_poison_init(&g_ecc);

    /* --- T1: unsigned update, metadata format, auto boot --- */
    reset_ctx(false);
    begin(NULL, img_len, crc, 0, 0);
    CHECK(last_error() == 0 && last_status() == BOOT_STATUS_RECEIVING);
    CHECK(send_data(img, img_len) == 0);
    commit(img_len, crc);
    CHECK(last_error() == 0 && last_status() == BOOT_STATUS_COMMITTED);
    CHECK(((boot_metadata_t *)(uintptr_t)copy_addr(0))->format_version == BOOT_METADATA_FORMAT_VERSION);
    CHECK(((boot_metadata_t *)(uintptr_t)copy_addr(1))->format_version == BOOT_METADATA_FORMAT_VERSION);
    reset_ctx(false);
    g_jumps = 0;
    bootloader_try_boot_app(&g_ctx);
    CHECK(g_jumps == 1);
    printf("T1 unsigned flow done\n");

    /* --- T2: boot request latched at init, survives host traffic --- */
    reset_ctx(true);
    xsend(BOOT_OP_HELLO, NULL, 0);
    g_jumps = 0;
    bootloader_try_boot_app(&g_ctx);
    CHECK(g_jumps == 0);
    CHECK(g_force_calls == 1); /* nothing reads the word again */
    reset_ctx(false);          /* the next reset: request is gone */
    bootloader_try_boot_app(&g_ctx);
    CHECK(g_jumps == 1);
    printf("T2 boot request done\n");

    /* --- T3: signed BEGIN erases nothing until SET_MANIFEST --- */
    reset_ctx(false);
    g_erase_calls = 0;
    begin(&plain, img_len, crc, 0, 0);
    CHECK(last_error() == 0 && last_status() == BOOT_STATUS_RECEIVING);
    CHECK(g_erase_calls == 0);
    CHECK(memcmp(g_flash + 0x6000, img, img_len) == 0); /* old app intact */
    {
        uint8_t p[8 + 8] = {0};
        ((boot_data_prefix_t *)p)->data_len = 8;
        xsend(BOOT_OP_DATA, p, sizeof(p));
        CHECK(last_error() == BOOT_ERR_MANIFEST);
    }
    {
        boot_manifest_t forged = plain;
        forged.signature[10] ^= 1U;
        xsend(BOOT_OP_SET_MANIFEST, &forged, sizeof(forged));
        CHECK(last_error() == BOOT_ERR_SIGNATURE);
        CHECK(g_erase_calls == 0);
        CHECK(memcmp(g_flash + 0x6000, img, img_len) == 0);
    }
    xsend(BOOT_OP_SET_MANIFEST, &plain, sizeof(plain));
    CHECK(last_error() == 0 && last_status() == BOOT_STATUS_RECEIVING);
    CHECK(g_erase_calls == 1); /* 4 KiB image, one 8 KiB unit */
    xsend(BOOT_OP_SET_MANIFEST, &plain, sizeof(plain)); /* host retry */
    CHECK(last_error() == 0 && g_erase_calls == 1);
    xsend(BOOT_OP_SET_MANIFEST, &ar9, sizeof(ar9));     /* a different one */
    CHECK(last_error() == BOOT_ERR_BAD_STATE);
    CHECK(send_data(img, img_len) == 0);
    commit(img_len, crc);
    CHECK(last_error() == 0 && last_status() == BOOT_STATUS_COMMITTED);
    reset_ctx(false);
    g_jumps = 0;
    bootloader_try_boot_app(&g_ctx);
    CHECK(g_jumps == 1);
    printf("T3 signed flow done\n");

    /* --- T4: ECC error in metadata copy 0 -> copy 1; in the image -> no boot --- */
    if (!SIM_HAVE_POISON)
    {
        printf("T4 ECC skipped: no poisoned pages on this host\n");
        goto after_ecc;
    }
    poison(copy_addr(0));
    xsend(BOOT_OP_GET_METADATA, NULL, 0);
    memcpy(&md, g_rsp, sizeof(md));
    CHECK(md.valid == 1 && md.selected_copy == 1);
    CHECK(g_ecc > 0);
    unpoison(copy_addr(0));
    poison(g_cfg.app_base + 0x1000); /* second 4 KiB page of the image */
    xsend(BOOT_OP_VERIFY_IMAGE, NULL, 0);
    CHECK(((boot_verify_image_response_t *)g_rsp)->result_error == BOOT_ERR_FLASH);
    reset_ctx(false);
    g_jumps = 0;
    bootloader_try_boot_app(&g_ctx);
    CHECK(g_jumps == 0);
    unpoison(g_cfg.app_base + 0x1000);
    bootloader_try_boot_app(&g_ctx);
    CHECK(g_jumps == 1);
    printf("T4 ECC done (ecc count %u)\n", (unsigned)g_ecc);
after_ecc:

    /* --- T5: a record without format_version is not used --- */
    for (uint32_t copy = 0; copy < 2; ++copy)
    {
        boot_metadata_t *m = (boot_metadata_t *)(uintptr_t)copy_addr(copy);
        m->format_version = 0;
        boot_metadata_update_crc(m);
    }
    xsend(BOOT_OP_GET_METADATA, NULL, 0);
    CHECK(((boot_metadata_response_t *)g_rsp)->valid == 0);
    printf("T5 format version done\n");

    /* --- T6: legacy security state is found, enforced and migrated --- */
    memset(g_flash + (g_cfg.metadata_base - FLASH_BASE), 0xFF, g_cfg.metadata_size);
    boot_security_state_prepare(&g_cfg, &st, 7);
    memcpy(g_flash + (copy_addr(0) - FLASH_BASE) + 360, &st, sizeof(st)); /* 360-byte layout, 8-byte unit */
    CHECK(boot_security_state_select_valid(&g_cfg, &st) && st.rollback_floor_version == 7);
    reset_ctx(false);
    g_erase_calls = 0;
    begin(&ar5, img_len, crc, 0, 0);
    CHECK(last_error() == BOOT_ERR_ROLLBACK && g_erase_calls == 0);
    begin(&ar9, img_len, crc, 0, 0);
    CHECK(last_error() == 0 && g_erase_calls == 0);
    xsend(BOOT_OP_SET_MANIFEST, &ar9, sizeof(ar9));
    CHECK(last_error() == 0 && g_erase_calls == 1);
    CHECK(send_data(img, img_len) == 0);
    commit(img_len, crc);
    CHECK(last_error() == 0);
    {
        const boot_security_state_t *fixed =
            (const boot_security_state_t *)(uintptr_t)(copy_addr(0) + BOOT_SECURITY_STATE_OFFSET);
        CHECK(boot_security_state_is_valid(&g_cfg, fixed) && fixed->rollback_floor_version == 9);
        CHECK(*(uint32_t *)(uintptr_t)(copy_addr(0) + 360) != BOOT_SECURITY_STATE_MAGIC ||
              memcmp((void *)(uintptr_t)(copy_addr(0) + 360), &st, sizeof(st)) != 0);
    }
    /* CLEAR_METADATA keeps the floor at the fixed offset */
    xsend(BOOT_OP_CLEAR_METADATA, NULL, 0);
    CHECK(last_error() == 0);
    CHECK(boot_security_state_select_valid(&g_cfg, &st) && st.rollback_floor_version == 9);
    printf("T6 security state done\n");

    /* --- T7: noise in front of a frame, decode errors stay out of status --- */
    reset_ctx(false);
    {
        static const uint8_t junk[] = {0x41, 0x41, 0x46, 0x00, 0x41, 0x46, 0x46, 0x41, 0xFF, 0x41};
        int before = g_rsp_count;
        bootloader_receive_bytes(&g_ctx, junk, sizeof(junk));
        CHECK(g_rsp_count == before);
        xsend(BOOT_OP_GET_STATUS, NULL, 0);
        CHECK(last_error() == 0 && last_status() == BOOT_STATUS_READY);
        CHECK(g_ctx.decode_error_count > 0);
    }
    printf("T7 resync done\n");

    /* --- T8: a partial frame that goes quiet is dropped by the idle timeout --- */
    {
        boot_frame_header_t h = {BOOT_FRAME_MAGIC, BOOT_OP_HELLO, 0x1234, 0, 0, 0};
        h.payload_crc32 = boot_crc32_update(0, NULL, 0);
        h.header_crc32 = boot_crc32_update(0, &h, sizeof(h) - 4);
        bootloader_receive_bytes(&g_ctx, (const uint8_t *)&h, 11); /* first 11 bytes only */
        CHECK(boot_decoder_in_frame(&g_ctx.decoder));
        idle(50);
        CHECK(boot_decoder_in_frame(&g_ctx.decoder)); /* not yet */
        idle(100);
        CHECK(!boot_decoder_in_frame(&g_ctx.decoder));
        CHECK(g_ctx.last_decode_error == BOOT_ERR_FRAME_TIMEOUT);
        xsend(BOOT_OP_HELLO, NULL, 0); /* the retry decodes on its own */
    }
    printf("T8 idle timeout done\n");

    /* --- T9: lost answers: DATA and COMMIT are not run twice; a failed DATA runs again --- */
    {
        const faults_t none = {0};
        uint8_t p[8 + 256];
        boot_begin_request_t b = {g_cfg.target_id, g_cfg.board_id, g_cfg.flash_layout_id, img_len, crc, 1, 0, 0, 0};
        uint16_t seq;

        reset_ctx(false);
        CHECK(transact(&none, BOOT_OP_BEGIN, &b, sizeof(b), 1) && last_error() == 0);
        ((boot_data_prefix_t *)p)->offset = 0;
        ((boot_data_prefix_t *)p)->data_len = 256;
        memcpy(p + 8, img, 256);
        seq = g_seq;
        g_data_writes = 0;
        xsend(BOOT_OP_DATA, p, sizeof(p));     /* answer "lost" */
        CHECK(last_error() == 0);
        g_seq = seq;
        xsend(BOOT_OP_DATA, p, sizeof(p));     /* the same frame again */
        CHECK(last_error() == 0 && g_data_writes == 1 && g_ctx.session.written_size == 256);

        ((boot_data_prefix_t *)p)->offset = 256;
        memcpy(p + 8, img + 256, 256);
        seq = g_seq;
        g_fail_writes = 1;
        xsend(BOOT_OP_DATA, p, sizeof(p));
        CHECK(last_error() == BOOT_ERR_FLASH);
        g_seq = seq;
        xsend(BOOT_OP_DATA, p, sizeof(p));     /* retry of a failed one runs */
        CHECK(last_error() == 0 && g_ctx.session.written_size == 512);

        for (uint32_t off = 512; off < img_len; off += 256)
        {
            ((boot_data_prefix_t *)p)->offset = off;
            memcpy(p + 8, img + off, 256);
            xsend(BOOT_OP_DATA, p, sizeof(p));
        }
        seq = g_seq;
        commit(img_len, crc);
        CHECK(last_error() == 0 && last_status() == BOOT_STATUS_COMMITTED);
        g_seq = seq;
        commit(img_len, crc);                  /* COMMIT again after a lost answer */
        CHECK(last_error() == 0 && last_status() == BOOT_STATUS_COMMITTED);
        xsend(BOOT_OP_GET_STATUS, NULL, 0);    /* read-only in between keeps nothing stale */
        CHECK(last_error() == 0);
    }
    printf("T9 retry dedupe done\n");

    /* --- T10: whole updates over a lossy channel --- */
    {
        const faults_t clean = {0};
        /* about one frame in five damaged on the way in, one answer in ten lost */
        const faults_t lossy = {.drop_byte = 30, .insert_byte = 30, .flip_bit = 30,
                                .truncate = 30, .drop_reply = 100, .noise_between = 150};
        /* far worse than any real link: the host may give up, but never wrongly */
        const faults_t brutal = {.drop_byte = 100, .insert_byte = 100, .flip_bit = 100,
                                 .truncate = 100, .drop_reply = 300, .noise_between = 500};
        const struct { const char *name; const faults_t *f; int runs; } cfgs[] = {
            {"lossy", &lossy, 300}, {"brutal", &brutal, 200}};

        reset_ctx(false);
        CHECK(flash_over_channel(&clean, img, img_len, crc, NULL) == UPD_OK);
        for (int c = 0; c < 2; ++c)
        {
            int ok = 0, gave_up = 0, wrong = 0;
            memset(&g_stat, 0, sizeof(g_stat));
            for (int seed = 1; seed <= cfgs[c].runs; ++seed)
            {
                const bool use_signed = (seed % 3) == 0;
                int r;
                g_rng = (uint32_t)seed * 2654435761u + (uint32_t)c;
                reset_ctx(false);
                r = flash_over_channel(cfgs[c].f, img, img_len, crc, use_signed ? &plain : NULL);
                if (r == UPD_OK)
                {
                    ok++;
                    g_jumps = 0;
                    reset_ctx(false);
                    bootloader_try_boot_app(&g_ctx);
                    CHECK(g_jumps == 1);
                }
                else if (r == UPD_GAVE_UP) gave_up++;
                else { wrong++; printf("  %s seed %d: wrong result\n", cfgs[c].name, seed); }
            }
            CHECK(wrong == 0);
            if (c == 0) CHECK(ok == cfgs[c].runs);
            printf("T10 %s: %d ok, %d host gave up, %d wrong of %d updates; %u frames, %u sends; "
                   "%u bytes dropped, %u inserted, %u bits flipped, %u truncated, %u answers lost\n",
                   cfgs[c].name, ok, gave_up, wrong, cfgs[c].runs, g_stat.frames, g_stat.sent, g_stat.dropped,
                   g_stat.inserted, g_stat.flipped, g_stat.truncated, g_stat.replies_lost);
        }
    }

    printf(g_failures ? "%d FAILURE(S)\n" : "ALL PASS\n", g_failures);
    return g_failures ? 1 : 0;
}
