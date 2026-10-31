#include "boot_platform.h"

#include <string.h>

#include "stm32f0xx.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_flash.h"
#include "stm32f0xx_hal_flash_ex.h"

#include "targets/common/boot_target_common.h"
#include "targets/stm32f072/board_config.h"
#include "targets/stm32f072/board_pins.h"

#define F072_BOOT_REQUEST_SIGNATURE   0x424F4F54UL
#define F072_BOOT_REQUEST_ADDRESS     (0x20000000UL + 0x3F00UL)
#define F072_VECTOR_TABLE_WORDS       48U

const boot_target_config_t *boot_platform_target(void)
{
    return &g_target_config;
}

uint32_t boot_platform_millis(void)
{
    return HAL_GetTick();
}

#if defined(F072_BOOT_REQUEST_GPIO)
/* Runs before HAL_Init(), from bootloader_init(): registers only. */
static bool boot_f072_request_pin_active(void)
{
    const uint32_t shift = 2U * F072_BOOT_REQUEST_PIN;
    uint32_t level;

    RCC->AHBENR |= F072_BOOT_REQUEST_GPIO_EN;
    (void)RCC->AHBENR;
    F072_BOOT_REQUEST_GPIO->MODER &= ~(3UL << shift); /* input */
    /* Pulled to the inactive level, so an unconnected pin asks for nothing. */
    F072_BOOT_REQUEST_GPIO->PUPDR = (F072_BOOT_REQUEST_GPIO->PUPDR & ~(3UL << shift)) |
                                    ((F072_BOOT_REQUEST_ACTIVE_HIGH ? 2UL : 1UL) << shift);
    for (volatile uint32_t i = 0U; i < 200U; ++i) /* let the pull settle */
    {
    }
    level = (F072_BOOT_REQUEST_GPIO->IDR >> F072_BOOT_REQUEST_PIN) & 1U;
    return level == (F072_BOOT_REQUEST_ACTIVE_HIGH ? 1U : 0U);
}
#endif

bool boot_platform_force_bootloader(void)
{
    volatile uint32_t *request = (volatile uint32_t *)F072_BOOT_REQUEST_ADDRESS;
    bool requested = false;

    if (*request == F072_BOOT_REQUEST_SIGNATURE)
    {
        *request = 0U;
        __DSB();
        __ISB();
        requested = true;
    }

#if defined(F072_BOOT_REQUEST_GPIO)
    /* A master MCU holding the BOOT-REQUEST line through reset. */
    if (boot_f072_request_pin_active())
    {
        requested = true;
    }
#endif

    return requested;
}

void boot_platform_watchdog_kick(void)
{
#if defined(IWDG)
    IWDG->KR = 0xAAAAU;
#endif
}

uint32_t boot_platform_flash_ecc_error_count(void)
{
    return 0U; /* no flash ECC on this part */
}

void boot_platform_flash_unlock(void)
{
    HAL_FLASH_Unlock();
}

void boot_platform_flash_lock(void)
{
    HAL_FLASH_Lock();
}

boot_error_t boot_platform_flash_erase(uint32_t address, uint32_t length)
{
    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }

    if (!boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }

    const uint32_t erase_start = address - ((address - g_target_config.flash_base) % g_target_config.erase_size);
    const uint32_t erase_end = address + length;
    for (uint32_t page_address = erase_start; page_address < erase_end; page_address += g_target_config.erase_size)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t page_error = 0U;

        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = page_address;
        erase.NbPages = 1U;

        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }
    }

    return BOOT_ERR_NONE;
}

boot_error_t boot_platform_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

    if (length == 0U)
    {
        return BOOT_ERR_NONE;
    }

    if (!boot_target_flash_write_length_is_aligned(&g_target_config, length))
    {
        return BOOT_ERR_ALIGNMENT;
    }

    if (!boot_target_flash_range_is_valid(&g_target_config, address, length))
    {
        return BOOT_ERR_RANGE;
    }

    if (!boot_target_flash_write_address_is_aligned(&g_target_config, address))
    {
        return BOOT_ERR_ALIGNMENT;
    }

    while (offset < length)
    {
        uint16_t halfword;

        memcpy(&halfword, &data[offset], sizeof(halfword));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + offset, halfword) != HAL_OK)
        {
            return BOOT_ERR_FLASH;
        }
        offset += sizeof(halfword);
    }

    return BOOT_ERR_NONE;
}

void boot_platform_jump_to_app(uint32_t app_base)
{
    void (*app_reset_handler)(void);
    uint32_t app_stack = *(volatile uint32_t *)app_base;
    uint32_t app_reset = *(volatile uint32_t *)(app_base + 4U);

    __disable_irq();

    HAL_DeInit();
    HAL_RCC_DeInit();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (uint32_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    for (uint32_t i = 0U; i < F072_VECTOR_TABLE_WORDS; ++i)
    {
        ((volatile uint32_t *)g_target_config.sram_base)[i] = *(volatile uint32_t *)(app_base + (i * sizeof(uint32_t)));
    }
    __HAL_SYSCFG_REMAPMEMORY_SRAM();
    __DSB();
    __ISB();

    __set_MSP(app_stack);
    app_reset_handler = (void (*)(void))app_reset;
    app_reset_handler();

    for (;;)
    {
    }
}

void boot_platform_reboot(void)
{
#if defined(AFFINE_BOARD_MONICA_LITE_R1)
    /*
     * NRST is tied to the supply on this board, so a reset the MCU asks for
     * itself never happens. Put the hardware back into its reset state by
     * hand and start again from the reset vector.
     */
    const uint32_t *vectors = (const uint32_t *)g_target_config.flash_base;

    __disable_irq();

    HAL_DeInit();
    HAL_RCC_DeInit();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    for (uint32_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* The USB pull-up went away with the peripheral: give the host time to notice. */
    for (volatile uint32_t i = 0U; i < 100000UL; ++i)
    {
    }

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_FLASH();
    __DSB();
    __ISB();

    __enable_irq();
    __set_MSP(vectors[0]);
    ((void (*)(void))vectors[1])();

    for (;;)
    {
    }
#else
    NVIC_SystemReset();
#endif
}
