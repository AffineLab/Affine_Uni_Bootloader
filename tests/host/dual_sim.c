/* The real targets/stm32f072 main.c and transport.c, the real arbiter and core,
 * over two TCP ports standing in for USB CDC (channel 0) and the UART (channel 1).
 *
 *   dual_sim USB_PORT UART_PORT [noise_channel noise_per_ms fault_per_100k seed]
 *
 * Port 0 picks a free one; the first line printed is "[sim] ports USB UART".
 * noise_channel (-1 = none) gets random bytes, magic fragments and headers with
 * a bad CRC from the "line". fault_per_100k drops/flips bytes both ways. */
/* Simulated resets jump back without unwinding MinGW SEH frames. */
#define __USE_MINGW_SETJMP_NON_SEH
#include "sim_os.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "bootloader.h"
#include "boot_platform.h"

#define FLASH_BASE 0x08000000UL
#define FLASH_SIZE 0x20000UL

int target_main(void);

static const boot_target_config_t g_cfg = {
    .flash_base = FLASH_BASE, .flash_size = FLASH_SIZE,
    .app_base = 0x08006000UL, .app_size = 0x16000UL,
    .metadata_base = 0x0801C000UL, .metadata_size = 0x4000UL,
    .sram_base = 0x20000000UL, .sram_size = 0x8000UL,
    .erase_size = 0x2000UL, .write_size = 8UL, .max_chunk_size = 256UL,
    .target_id = 0x47343331UL, .board_id = 0x4D414931UL, .board_revision = 1UL,
    .flash_layout_id = 0x47343341UL,
};

static uint8_t *g_flash;
static SOCKET g_server[2], g_client[2] = {INVALID_SOCKET, INVALID_SOCKET};
static jmp_buf g_reset;
static int g_noise_channel = -1;
static unsigned g_noise_per_ms, g_fault;
static uint32_t g_rng = 1;
static uint32_t g_last_noise;
static struct { unsigned rx[2], tx_frames[2], noise, faults, overflow[2], resets, jumps; } g_stat;

static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
uint32_t HAL_GetTick(void) { return (uint32_t)sim_millis(); }

const boot_target_config_t *boot_platform_target(void) { return &g_cfg; }
uint32_t boot_platform_millis(void) { return HAL_GetTick(); }
bool boot_platform_force_bootloader(void) { return false; }
void boot_platform_watchdog_kick(void) {}
void boot_platform_flash_unlock(void) {}
void boot_platform_flash_lock(void) {}
uint32_t boot_platform_flash_ecc_error_count(void) { return 0; }

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    const uint32_t start = address & ~(g_cfg.erase_size - 1U);
    if (address < g_cfg.app_base || (address + length) > (FLASH_BASE + FLASH_SIZE)) return BOOT_ERR_RANGE;
    memset(g_flash + (start - FLASH_BASE), 0xFF, g_cfg.erase_size);
    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint8_t *dst = g_flash + (address - FLASH_BASE);
    if ((address % 8U) || (length % 8U) || address < g_cfg.app_base) return BOOT_ERR_ALIGNMENT;
    for (uint32_t i = 0; i < length; ++i) if (dst[i] != 0xFF) return BOOT_ERR_FLASH;
    memcpy(dst, data, length);
    return BOOT_ERR_NONE;
}

static void show_stats(const char *why)
{
    printf("[sim] %s: rx usb=%u uart=%u | answers usb=%u uart=%u | noise=%u faults=%u overflow usb=%u uart=%u | locked=%d resets=%u jumps=%u\n",
           why, g_stat.rx[0], g_stat.rx[1], g_stat.tx_frames[0], g_stat.tx_frames[1], g_stat.noise, g_stat.faults,
           g_stat.overflow[0], g_stat.overflow[1], (int)boot_stm32f072_locked_channel(), g_stat.resets, g_stat.jumps);
}

/* The sockets: what the USB and USART interrupts do on the board. */
static void pump(void)
{
    fd_set rd;
    struct timeval tv = {0, 200};
    SOCKET max_fd = 0;
    FD_ZERO(&rd);
    for (int c = 0; c < 2; ++c)
    {
        FD_SET(g_server[c], &rd);
        if (g_server[c] > max_fd) max_fd = g_server[c];
        if (g_client[c] != INVALID_SOCKET)
        {
            FD_SET(g_client[c], &rd);
            if (g_client[c] > max_fd) max_fd = g_client[c];
        }
    }
    (void)max_fd;
    if (select(sim_select_nfds(max_fd), &rd, NULL, NULL, &tv) > 0)
    {
        for (int c = 0; c < 2; ++c)
        {
            if (FD_ISSET(g_server[c], &rd))
            {
                if (g_client[c] != INVALID_SOCKET) closesocket(g_client[c]);
                g_client[c] = sim_accept(g_server[c]);
            }
            if (g_client[c] != INVALID_SOCKET && FD_ISSET(g_client[c], &rd))
            {
                uint8_t in[4096], out[4096];
                int n = recv(g_client[c], (char *)in, sizeof(in), 0), m = 0;
                if (n <= 0)
                {
                    closesocket(g_client[c]);
                    g_client[c] = INVALID_SOCKET;
                    show_stats(c == 0 ? "usb host gone" : "uart host gone");
                    continue;
                }
                g_stat.rx[c] += (unsigned)n;
                for (int i = 0; i < n; ++i)
                {
                    if (g_fault && (rnd() % 100000U) < g_fault) { g_stat.faults++; if (rnd() & 1U) continue; in[i] ^= 0x10; }
                    out[m++] = in[i];
                }
                boot_stm32f072_receive_isr(c == 0 ? BOOT_RX_CHANNEL_USB : BOOT_RX_CHANNEL_UART, out, (uint32_t)m);
            }
        }
    }

    if (g_noise_channel >= 0 && HAL_GetTick() != g_last_noise)
    {
        g_last_noise = HAL_GetTick();
        for (unsigned k = 0; k < g_noise_per_ms; ++k)
        {
            uint8_t b[20];
            uint32_t len = 1;
            switch (rnd() % 8U)
            {
                case 0: { /* magic alone */
                    const uint32_t magic = BOOT_FRAME_MAGIC; memcpy(b, &magic, 4); len = 2 + rnd() % 3U; break; }
                case 1: { /* a whole header, CRC wrong */
                    boot_frame_header_t h = {BOOT_FRAME_MAGIC, BOOT_OP_HELLO, 1, 0, 0, rnd() | 1U};
                    memcpy(b, &h, sizeof(h)); len = sizeof(h); break; }
                default: b[0] = (uint8_t)rnd(); break;
            }
            g_stat.noise += len;
            boot_stm32f072_receive_isr(g_noise_channel == 0 ? BOOT_RX_CHANNEL_USB : BOOT_RX_CHANNEL_UART, b, len);
        }
    }
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    printf("[sim] jump to application at 0x%08lX\n", (unsigned long)app_base);
    g_stat.jumps++;
    show_stats("in application");
    for (;;) sim_sleep_ms(50);
}

void boot_platform_reboot(void)
{
    printf("[sim] reboot\n");
    longjmp(g_reset, 1);
}

static void tx(int c, const uint8_t *data, size_t length)
{
    uint8_t out[2048];
    size_t m = 0;
    g_stat.tx_frames[c]++;
    for (size_t i = 0; i < length; ++i)
    {
        uint8_t v = data[i];
        if (g_fault && (rnd() % 100000U) < g_fault) { g_stat.faults++; if (rnd() & 1U) continue; v ^= 0x10; }
        out[m++] = v;
    }
    if (g_client[c] != INVALID_SOCKET) send(g_client[c], (const char *)out, (int)m, SIM_SEND_FLAGS);
}

void boot_stm32f072_usb_transport_init(void) {}
void boot_stm32f072_uart_transport_init(void) {}
void boot_stm32f072_usb_transport_note_rx_overflow(void) { g_stat.overflow[0]++; }
void boot_stm32f072_uart_transport_note_rx_overflow(void) { g_stat.overflow[1]++; }
void boot_stm32f072_usb_transport_send(const uint8_t *data, size_t length) { tx(0, data, length); }
void boot_stm32f072_uart_transport_send(const uint8_t *data, size_t length) { tx(1, data, length); }
void boot_stm32f072_usb_transport_poll(void) { pump(); }
bool boot_stm32f072_usb_transport_get_diag(boot_diag_response_t *d) { memset(d, 0, sizeof(*d)); d->rx_byte_count = 1; return true; }
bool boot_stm32f072_uart_transport_get_diag(boot_diag_response_t *d) { memset(d, 0, sizeof(*d)); d->rx_byte_count = 2; return true; }

int main(int argc, char **argv)
{
    unsigned port[2];

    if (argc < 3) { fprintf(stderr, "usage: dual_sim USB_PORT UART_PORT [noise_channel noise_per_ms fault_per_100k seed]\n"); return 2; }
    if (argc > 3) g_noise_channel = atoi(argv[3]);
    if (argc > 4) g_noise_per_ms = (unsigned)atoi(argv[4]);
    if (argc > 5) g_fault = (unsigned)atoi(argv[5]);
    if (argc > 6) g_rng = (uint32_t)strtoul(argv[6], NULL, 0) * 2654435761UL | 1U;
    setvbuf(stdout, NULL, _IONBF, 0);

    g_flash = sim_map_flash(FLASH_BASE, FLASH_SIZE);
    if (g_flash == NULL) return 2;
    memset(g_flash, 0xFF, FLASH_SIZE);

    sim_net_init();
    for (int c = 0; c < 2; ++c)
    {
        g_server[c] = sim_listen((unsigned)atoi(argv[1 + c]), &port[c]);
        if (g_server[c] == INVALID_SOCKET)
        {
            printf("cannot listen on %s\n", argv[1 + c]);
            return 2;
        }
    }
    printf("[sim] ports %u %u\n", port[0], port[1]);
    printf("[sim] noise_channel=%d noise_per_ms=%u fault=%u\n", g_noise_channel, g_noise_per_ms, g_fault);

    setjmp(g_reset);
    g_stat.resets++;
    printf("[sim] reset\n");
    return target_main();
}
