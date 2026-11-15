/* The bootloader core behind a TCP socket, standing in for a board on a UART:
 * boot_test.py --transport uart socket://127.0.0.1:PORT talks to it.
 *
 *   uart_sim PORT [drop_byte insert_byte flip_bit drop_reply app_ms seed reply_noise]
 *
 * PORT 0 picks a free port; the first line printed is "[sim] port N".
 * drop/insert/flip: per 100k bytes, both ways; drop_reply, reply_noise (stray
 * bytes in front of an answer): per 1k answers.
 * app_ms > 0: the "board" starts in its application (deaf) and is reset into
 * the bootloader every app_ms, which then waits the usual 1 s auto-boot
 * window before running the application again (if one is valid). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_os.h"

#include "bootloader.h"
#include "boot_platform.h"

#define FLASH_BASE 0x08000000UL
#define FLASH_SIZE 0x20000UL
#define AUTO_BOOT_WINDOW_MS 1000U

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
static SOCKET g_client = INVALID_SOCKET;
static bootloader_t g_ctx;
static unsigned g_drop_byte, g_insert_byte, g_flip_bit, g_drop_reply, g_app_ms, g_reply_noise;
static uint32_t g_rng = 1;
static bool g_in_app;
static bool g_reset_pending;
static uint64_t g_app_since;
static struct { unsigned rx, dropped, inserted, flipped, replies, replies_lost, reply_noise, jumps, resets; } g_stat;

static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint64_t now_ms(void) { return sim_millis(); }

const boot_target_config_t *boot_platform_target(void) { return &g_cfg; }
uint32_t boot_platform_millis(void) { return (uint32_t)now_ms(); }
bool boot_platform_force_bootloader(void) { return false; }
void boot_platform_transport_poll(void) {}
bool boot_platform_transport_get_diag(boot_diag_response_t *d) { memset(d, 0, sizeof(*d)); return true; }
void boot_platform_watchdog_kick(void) {}
void boot_platform_flash_unlock(void) {}
void boot_platform_flash_lock(void) {}
uint32_t boot_platform_flash_ecc_error_count(void) { return 0; }

void boot_platform_jump_to_app(uint32_t app_base)
{
    printf("[sim] jump to application at 0x%08lX\n", (unsigned long)app_base);
    g_stat.jumps++;
    g_in_app = true;
    g_app_since = now_ms();
}

void boot_platform_reboot(void)
{
    printf("[sim] reboot\n");
    g_reset_pending = true;
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    g_stat.replies++;
    if (g_drop_reply && (rnd() % 1000U) < g_drop_reply)
    {
        g_stat.replies_lost++;
        return;
    }
    if (g_client != INVALID_SOCKET)
    {
        /* The same line faults on the way back, plus now and then a burst of
         * noise in front of the answer. */
        uint8_t out[2048];
        size_t m = 0;
        if (g_reply_noise && (rnd() % 1000U) < g_reply_noise)
        {
            unsigned k = 1U + rnd() % 24U;
            while (k--) out[m++] = (uint8_t)rnd();
            g_stat.reply_noise++;
        }
        for (size_t i = 0; i < length && m < sizeof(out) - 1; ++i)
        {
            uint8_t v = data[i];
            if (g_insert_byte && (rnd() % 100000U) < g_insert_byte) { out[m++] = (uint8_t)rnd(); g_stat.inserted++; }
            if (g_drop_byte && (rnd() % 100000U) < g_drop_byte) { g_stat.dropped++; continue; }
            if (g_flip_bit && (rnd() % 100000U) < g_flip_bit) { v ^= (uint8_t)(1U << (rnd() % 8U)); g_stat.flipped++; }
            out[m++] = v;
        }
        if (getenv("SIMDEBUG"))
            printf("[sim] t=%llu reply %u->%u bytes\n", (unsigned long long)(now_ms() % 100000), (unsigned)length, (unsigned)m);
        send(g_client, (const char *)out, (int)m, SIM_SEND_FLAGS);
    }
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    const uint32_t start = address & ~(g_cfg.erase_size - 1U);
    if (address < g_cfg.app_base || (address + length) > (FLASH_BASE + FLASH_SIZE))
        return BOOT_ERR_RANGE;
    sim_sleep_ms(20); /* a page erase takes a while */
    memset(g_flash + (start - FLASH_BASE), 0xFF, g_cfg.erase_size);
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint8_t *dst = g_flash + (address - FLASH_BASE);
    if ((address % 8U) || (length % 8U) || address < g_cfg.app_base)
        return BOOT_ERR_ALIGNMENT;
    for (uint32_t i = 0; i < length; ++i)
        if (dst[i] != 0xFF) return BOOT_ERR_FLASH;
    memcpy(dst, data, length);
    return BOOT_ERR_NONE;
}

/* Power-on / reset: the bootloader starts, listens for the auto-boot window. */
static uint64_t g_window_start;
static bool g_window_open;

static void board_reset(void)
{
    g_stat.resets++;
    g_in_app = false;
    bootloader_init(&g_ctx);
    g_window_start = now_ms();
    g_window_open = true;
    printf("[sim] reset: bootloader listening\n");
}

static void feed(const uint8_t *in, int n)
{
    uint8_t out[8192];
    int m = 0;
    for (int i = 0; i < n; ++i)
    {
        uint8_t b = in[i];
        g_stat.rx++;
        if (g_insert_byte && (rnd() % 100000U) < g_insert_byte) { out[m++] = (uint8_t)rnd(); g_stat.inserted++; }
        if (g_drop_byte && (rnd() % 100000U) < g_drop_byte) { g_stat.dropped++; continue; }
        if (g_flip_bit && (rnd() % 100000U) < g_flip_bit) { b ^= (uint8_t)(1U << (rnd() % 8U)); g_stat.flipped++; }
        out[m++] = b;
    }
    if (!g_in_app)
    {
        const uint32_t ok = g_ctx.frame_ok_count, errs = g_ctx.decode_error_count;
        bootloader_receive_bytes(&g_ctx, out, (size_t)m);
        if (getenv("SIMDEBUG"))
            printf("[sim] t=%llu rx %d->%d ok+%lu err+%lu last_err=%lu hdr=%u pay=%u\n",
                   (unsigned long long)(now_ms() % 100000), n, m,
                   (unsigned long)(g_ctx.frame_ok_count - ok), (unsigned long)(g_ctx.decode_error_count - errs),
                   (unsigned long)g_ctx.last_decode_error, (unsigned)g_ctx.decoder.header_used,
                   (unsigned)g_ctx.decoder.payload_used);
    }
}

int main(int argc, char **argv)
{
    SOCKET server;
    unsigned port;

    if (argc < 2) { fprintf(stderr, "usage: uart_sim PORT [drop insert flip drop_reply app_ms seed reply_noise]\n"); return 2; }
    if (argc > 2) g_drop_byte = (unsigned)atoi(argv[2]);
    if (argc > 3) g_insert_byte = (unsigned)atoi(argv[3]);
    if (argc > 4) g_flip_bit = (unsigned)atoi(argv[4]);
    if (argc > 5) g_drop_reply = (unsigned)atoi(argv[5]);
    if (argc > 6) g_app_ms = (unsigned)atoi(argv[6]);
    if (argc > 7) g_rng = (uint32_t)strtoul(argv[7], NULL, 0) | 1U;
    if (argc > 8) g_reply_noise = (unsigned)atoi(argv[8]);
    setvbuf(stdout, NULL, _IONBF, 0);

    g_flash = sim_map_flash(FLASH_BASE, FLASH_SIZE);
    if (g_flash == NULL) return 2;
    memset(g_flash, 0xFF, FLASH_SIZE);

    sim_net_init();
    server = sim_listen((unsigned)atoi(argv[1]), &port);
    if (server == INVALID_SOCKET)
    {
        printf("cannot listen on %s\n", argv[1]);
        return 2;
    }
    printf("[sim] port %u\n", port);
    printf("[sim] drop=%u insert=%u flip=%u drop_reply=%u app_ms=%u\n",
           g_drop_byte, g_insert_byte, g_flip_bit, g_drop_reply, g_app_ms);

    if (g_app_ms)
    {
        g_in_app = true;
        g_app_since = now_ms();
    }
    else
    {
        board_reset();
    }

    for (;;)
    {
        fd_set rd;
        struct timeval tv = {0, 1000};
        FD_ZERO(&rd);
        FD_SET(server, &rd);
        if (g_client != INVALID_SOCKET) FD_SET(g_client, &rd);

        if (select(sim_select_nfds(g_client != INVALID_SOCKET && g_client > server ? g_client : server), &rd, NULL, NULL, &tv) > 0)
        {
            if (FD_ISSET(server, &rd))
            {
                SOCKET c = sim_accept(server);
                if (g_client != INVALID_SOCKET) closesocket(g_client);
                g_client = c;
                printf("[sim] host connected\n");
            }
            if (g_client != INVALID_SOCKET && FD_ISSET(g_client, &rd))
            {
                uint8_t buf[4096];
                int n = recv(g_client, (char *)buf, sizeof(buf), 0);
                if (n <= 0)
                {
                    closesocket(g_client);
                    g_client = INVALID_SOCKET;
                    printf("[sim] host gone: rx=%u dropped=%u inserted=%u flipped=%u replies=%u lost=%u noisy=%u jumps=%u resets=%u\n",
                           g_stat.rx, g_stat.dropped, g_stat.inserted, g_stat.flipped, g_stat.replies,
                           g_stat.replies_lost, g_stat.reply_noise, g_stat.jumps, g_stat.resets);
                }
                else
                {
                    feed(buf, n);
                }
            }
        }

        if (g_reset_pending)
        {
            g_reset_pending = false;
            board_reset();
        }
        if (g_in_app)
        {
            if (g_app_ms && (now_ms() - g_app_since) >= g_app_ms)
                board_reset();
            continue;
        }

        bootloader_poll(&g_ctx);
        if (g_window_open && (now_ms() - g_window_start) >= AUTO_BOOT_WINDOW_MS)
        {
            g_window_open = false;
            if (g_ctx.frame_ok_count == 0U)
                bootloader_try_boot_app(&g_ctx);
        }
        else if (g_window_open && g_ctx.frame_ok_count != 0U)
        {
            g_window_open = false;
            printf("[sim] host caught the auto-boot window\n");
        }
    }
}
