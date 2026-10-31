/*
 * UART transport (AFFINE_BOOT_TRANSPORT=uart): one USART, register level, for
 * boards without USB and for an MCU flashed through a master that forwards
 * the bytes. Pins and baud rate: board_pins.h.
 *
 * Received bytes go from the RX interrupt into main.c's FIFO. Sending blocks
 * until the last byte is out: the protocol answers one request at a time, and
 * reception keeps running in the interrupt meanwhile. The UART has no flow
 * control and loses bytes on overrun or noise; the frame decoder resyncs and
 * the host retries (docs/uart-transport.md).
 */
#include "boot_platform.h"

#include "main.h"

#include "targets/stm32f072/board_pins.h"

#if F072_BOOT_UART_INSTANCE == 1
#define BOOT_UART            USART1
#define BOOT_UART_IRQn       USART1_IRQn
#define BOOT_UART_IRQHandler USART1_IRQHandler
#elif F072_BOOT_UART_INSTANCE == 2
#define BOOT_UART            USART2
#define BOOT_UART_IRQn       USART2_IRQn
#define BOOT_UART_IRQHandler USART2_IRQHandler
#else
#error "F072_BOOT_UART_INSTANCE must be 1 (USART1) or 2 (USART2)"
#endif

#define BOOT_UART_LINE_ERRORS (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)

/*
 * The same counters as the USB transport reports. rx_fifo_overflow_count
 * counts every byte lost before the decoder: a full FIFO and a USART overrun.
 * A byte with a framing or noise error is passed on as it is; the frame CRCs
 * catch it.
 */
static boot_diag_response_t g_uart_diag;

static void boot_uart_clock_enable_and_reset(void)
{
#if F072_BOOT_UART_INSTANCE == 1
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    RCC->CFGR3 &= ~RCC_CFGR3_USART1SW; /* clocked from PCLK */
#else
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    RCC->APB1RSTR |= RCC_APB1RSTR_USART2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST;
#endif
}

static void boot_uart_pin_alternate(uint32_t pin)
{
    const uint32_t shift2 = 2U * pin;
    const uint32_t shift4 = 4U * (pin & 7U);

    F072_BOOT_UART_GPIO->AFR[pin >> 3] = (F072_BOOT_UART_GPIO->AFR[pin >> 3] & ~(0xFUL << shift4)) |
                                         ((uint32_t)F072_BOOT_UART_AF << shift4);
    F072_BOOT_UART_GPIO->OSPEEDR |= 3UL << shift2;
    F072_BOOT_UART_GPIO->MODER = (F072_BOOT_UART_GPIO->MODER & ~(3UL << shift2)) | (2UL << shift2);
}

void boot_stm32f072_transport_init(void)
{
    const uint32_t pclk = HAL_RCC_GetPCLK1Freq();

    /* The USART first, idling high, then the pins: switching TX over to a
     * USART that is not running yet could put a glitch on the line. */
    boot_uart_clock_enable_and_reset();
    BOOT_UART->BRR = (pclk + (F072_BOOT_UART_BAUD / 2U)) / F072_BOOT_UART_BAUD; /* 16x oversampling */
    BOOT_UART->CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE;
    BOOT_UART->CR1 |= USART_CR1_UE;

    RCC->AHBENR |= F072_BOOT_UART_GPIO_EN;
    (void)RCC->AHBENR;
    /* RX pulled up: an unconnected line reads idle, not noise. */
    F072_BOOT_UART_GPIO->PUPDR = (F072_BOOT_UART_GPIO->PUPDR & ~(3UL << (2U * F072_BOOT_UART_RX_PIN))) |
                                 (1UL << (2U * F072_BOOT_UART_RX_PIN));
    boot_uart_pin_alternate(F072_BOOT_UART_TX_PIN);
    boot_uart_pin_alternate(F072_BOOT_UART_RX_PIN);

    HAL_NVIC_SetPriority(BOOT_UART_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(BOOT_UART_IRQn);
}

void BOOT_UART_IRQHandler(void)
{
    const uint32_t isr = BOOT_UART->ISR;

    if ((isr & BOOT_UART_LINE_ERRORS) != 0U)
    {
        BOOT_UART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        if ((isr & USART_ISR_ORE) != 0U)
        {
            g_uart_diag.rx_fifo_overflow_count++;
        }
    }

    if ((isr & USART_ISR_RXNE) != 0U)
    {
        const uint8_t byte = (uint8_t)BOOT_UART->RDR;

        g_uart_diag.rx_callback_count++;
        g_uart_diag.rx_byte_count++;
        g_uart_diag.rx_last_len = 1U;
        g_uart_diag.last_rx_tick_ms = HAL_GetTick();
        boot_stm32f072_receive_isr(&byte, 1U);
    }
}

void boot_stm32f072_transport_note_rx_overflow(void)
{
    g_uart_diag.rx_fifo_overflow_count++;
}

void boot_platform_transport_send(const uint8_t *data, size_t length)
{
    g_uart_diag.tx_enqueue_count++;
    g_uart_diag.tx_start_count++;
    g_uart_diag.tx_last_len = (uint32_t)length;
    g_uart_diag.last_tx_start_tick_ms = HAL_GetTick();

    for (size_t i = 0U; i < length; ++i)
    {
        while ((BOOT_UART->ISR & USART_ISR_TXE) == 0U)
        {
        }
        BOOT_UART->TDR = data[i];
        if ((i & 63U) == 0U)
        {
            boot_platform_watchdog_kick(); /* 532 bytes take 46 ms at 115200 */
        }
    }
    while ((BOOT_UART->ISR & USART_ISR_TC) == 0U)
    {
    }

    g_uart_diag.tx_complete_count++;
    g_uart_diag.last_tx_complete_tick_ms = HAL_GetTick();
}

void boot_platform_transport_poll(void)
{
    /* Nothing queued: send() returns once everything is out. */
}

bool boot_platform_transport_get_diag(boot_diag_response_t *out_diag)
{
    if (out_diag == NULL)
    {
        return false;
    }

    __disable_irq();
    *out_diag = g_uart_diag;
    __enable_irq();
    return true;
}
