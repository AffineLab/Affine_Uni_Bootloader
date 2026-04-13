#include "usb.h"

#include "stm32g4xx_hal_pwr.h"

PCD_HandleTypeDef hpcd_USB_FS;

void MX_USB_PCD_Init(void)
{
    hpcd_USB_FS.Instance = USB;
    hpcd_USB_FS.Init.dev_endpoints = 8;
    hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
    hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd_USB_FS.Init.Sof_enable = DISABLE;
    hpcd_USB_FS.Init.low_power_enable = DISABLE;
    hpcd_USB_FS.Init.lpm_enable = DISABLE;
    hpcd_USB_FS.Init.battery_charging_enable = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcdHandle)
{
    RCC_PeriphCLKInitTypeDef periph_clk = {0};

    if (pcdHandle->Instance != USB)
    {
        return;
    }

    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    periph_clk.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_PWR_CLK_ENABLE();
#if defined(PWR_CR2_USV)
    SET_BIT(PWR->CR2, PWR_CR2_USV);
#endif
    __HAL_RCC_USB_CLK_ENABLE();
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle)
{
    if (pcdHandle->Instance != USB)
    {
        return;
    }

    __HAL_RCC_USB_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(USB_HP_IRQn);
    HAL_NVIC_DisableIRQ(USB_LP_IRQn);
}
