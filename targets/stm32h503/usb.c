#include "usb.h"

PCD_HandleTypeDef hpcd_USB_DRD_FS;

void MX_USB_PCD_Init(void)
{
    hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
    hpcd_USB_DRD_FS.Init.dev_endpoints = 8;
    hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;
    hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;
    hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
    hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
    hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
    hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;
    hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
    hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcdHandle)
{
    RCC_PeriphCLKInitTypeDef periph_clk = {0};

    if (pcdHandle->Instance != USB_DRD_FS)
    {
        return;
    }

    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    periph_clk.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_USB_CLK_ENABLE();
    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle)
{
    if (pcdHandle->Instance != USB_DRD_FS)
    {
        return;
    }

    __HAL_RCC_USB_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
}
