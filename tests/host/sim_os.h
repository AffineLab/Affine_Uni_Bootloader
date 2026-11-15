/*
 * What the host simulators need from the OS, for Windows (MinGW) and Linux:
 * flash mapped at its real address, a millisecond clock, TCP sockets, and
 * pages that count a "flash ECC error" on every read (sim_poison).
 */
#ifndef AFFINE_SIM_OS_H
#define AFFINE_SIM_OS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
typedef int sim_socklen_t;
#define SIM_SEND_FLAGS 0
#define sim_select_nfds(max_fd) 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
typedef int SOCKET;
typedef socklen_t sim_socklen_t;
#define INVALID_SOCKET (-1)
#define closesocket close
#define SIM_SEND_FLAGS MSG_NOSIGNAL
#define sim_select_nfds(max_fd) ((max_fd) + 1)
#endif

/* Maps the simulated flash at the address the bootloader code uses. */
static inline uint8_t *sim_map_flash(uint32_t base, uint32_t size)
{
#ifdef _WIN32
    void *p = VirtualAlloc((void *)(uintptr_t)base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
    void *p = mmap((void *)(uintptr_t)base, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED)
        p = NULL;
#endif
    if (p != (void *)(uintptr_t)base)
    {
        printf("cannot map flash at 0x%08lX\n", (unsigned long)base);
        return NULL;
    }
    return (uint8_t *)p;
}

static inline uint64_t sim_millis(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
#endif
}

static inline void sim_sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000U);
#endif
}

static inline void sim_net_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
}

/* Listens on 127.0.0.1:port; port 0 picks a free one, returned in *bound. */
static inline SOCKET sim_listen(unsigned port, unsigned *bound)
{
    const int one = 1;
    struct sockaddr_in addr = {0};
    sim_socklen_t len = sizeof(addr);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s, 1) != 0)
        return INVALID_SOCKET;
    getsockname(s, (struct sockaddr *)&addr, &len);
    *bound = ntohs(addr.sin_port);
    return s;
}

static inline SOCKET sim_accept(SOCKET server)
{
    const int one = 1;
    SOCKET c = accept(server, NULL, NULL);
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    return c;
}

/*
 * Poisoned flash: every read of the page bumps *counter and then completes, as
 * a read of a half-programmed double word raises the ECC NMI on G431/H503 and
 * the handler counts it and returns. Built on guard pages (Windows) or
 * PROT_NONE plus single-stepping over the access (Linux on x86-64).
 */
#if defined(_WIN32) || (defined(__linux__) && defined(__x86_64__))
#define SIM_HAVE_POISON 1
#else
#define SIM_HAVE_POISON 0
#endif

static volatile uint32_t *g_sim_poison_counter;
static volatile uintptr_t g_sim_poisoned;

#ifdef _WIN32
static LONG CALLBACK sim_on_exception(EXCEPTION_POINTERS *info)
{
    DWORD old;

    if (info->ExceptionRecord->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION)
    {
        (*g_sim_poison_counter)++; /* the access is retried and completes */
        info->ContextRecord->EFlags |= 0x100U;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP)
    {
        /* The guard drops on the first hit: set it again past the access. */
        if (g_sim_poisoned != 0U)
            VirtualProtect((void *)g_sim_poisoned, 1, PAGE_READWRITE | PAGE_GUARD, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#elif SIM_HAVE_POISON
static void sim_on_segv(int sig, siginfo_t *si, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;
    const uintptr_t page = (uintptr_t)si->si_addr & ~(uintptr_t)0xFFFU;

    (void)sig;
    if (g_sim_poisoned == 0U || page != g_sim_poisoned)
    {
        signal(SIGSEGV, SIG_DFL); /* a real crash */
        return;
    }
    (*g_sim_poison_counter)++;
    mprotect((void *)page, 0x1000U, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100; /* trap after this one access */
}

static void sim_on_trap(int sig, siginfo_t *si, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;

    (void)sig;
    (void)si;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    if (g_sim_poisoned != 0U)
        mprotect((void *)g_sim_poisoned, 0x1000U, PROT_NONE);
}
#endif

static inline void sim_poison_init(volatile uint32_t *counter)
{
    g_sim_poison_counter = counter;
#ifdef _WIN32
    AddVectoredExceptionHandler(1, sim_on_exception);
#elif SIM_HAVE_POISON
    {
        struct sigaction sa = {0};
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sa.sa_sigaction = sim_on_segv;
        sigaction(SIGSEGV, &sa, NULL);
        sa.sa_sigaction = sim_on_trap;
        sigaction(SIGTRAP, &sa, NULL);
    }
#endif
}

static inline void sim_poison(uint32_t address, bool on)
{
#ifdef _WIN32
    DWORD old;
    g_sim_poisoned = on ? (uintptr_t)address : 0U;
    VirtualProtect((void *)(uintptr_t)address, 1, on ? (PAGE_READWRITE | PAGE_GUARD) : PAGE_READWRITE, &old);
#elif SIM_HAVE_POISON
    const uintptr_t page = (uintptr_t)address & ~(uintptr_t)0xFFFU;
    g_sim_poisoned = on ? page : 0U;
    mprotect((void *)page, 0x1000U, on ? PROT_NONE : (PROT_READ | PROT_WRITE));
#else
    (void)address;
    (void)on;
#endif
}

#endif
