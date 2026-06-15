#include "usbd_core.h"
#include "usbd_composite.h"
#include "usbd_cdc_acm.h"
#include "usb.h"

#include <stddef.h>

#define USB_PMA_EP0_SIZE            0x40U
#define USB_PMA_START               0x40U

static USBD_StatusTypeDef usb_status_from_hal(HAL_StatusTypeDef status)
{
    switch (status)
    {
    case HAL_OK:
        return USBD_OK;
    case HAL_BUSY:
        return USBD_BUSY;
    case HAL_ERROR:
    case HAL_TIMEOUT:
    default:
        return USBD_FAIL;
    }
}

static USBD_HandleTypeDef *usb_device_from_pcd(PCD_HandleTypeDef *hpcd)
{
    return (hpcd != NULL) ? (USBD_HandleTypeDef *)hpcd->pData : NULL;
}

static PCD_HandleTypeDef *pcd_from_usb_device(USBD_HandleTypeDef *pdev)
{
    return (pdev != NULL) ? (PCD_HandleTypeDef *)pdev->pData : NULL;
}

static USBD_StatusTypeDef configure_pma_endpoint(uint16_t ep_addr, uint16_t pma_addr)
{
    return usb_status_from_hal(HAL_PCDEx_PMAConfig(&hpcd_USB_FS, ep_addr, PCD_SNG_BUF, pma_addr));
}

static USBD_StatusTypeDef configure_pma(void)
{
    uint16_t pma_addr = USB_PMA_START;

    if (configure_pma_endpoint(0x00U, pma_addr) != USBD_OK)
    {
        return USBD_FAIL;
    }
    pma_addr += USB_PMA_EP0_SIZE;

    if (configure_pma_endpoint(0x80U, pma_addr) != USBD_OK)
    {
        return USBD_FAIL;
    }
    pma_addr += USB_PMA_EP0_SIZE;

#if (USBD_USE_CDC_ACM == 1)
    for (uint8_t index = 0U; index < USBD_CDC_ACM_COUNT; ++index)
    {
        if (configure_pma_endpoint(CDC_IN_EP[index], pma_addr) != USBD_OK)
        {
            return USBD_FAIL;
        }
        pma_addr += CDC_DATA_FS_MAX_PACKET_SIZE;

        if (configure_pma_endpoint(CDC_OUT_EP[index], pma_addr) != USBD_OK)
        {
            return USBD_FAIL;
        }
        pma_addr += CDC_DATA_FS_MAX_PACKET_SIZE;

        if (configure_pma_endpoint(CDC_CMD_EP[index], pma_addr) != USBD_OK)
        {
            return USBD_FAIL;
        }
        pma_addr += CDC_CMD_PACKET_SIZE;
    }
#endif

    return USBD_OK;
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_SetupStage(pdev, (uint8_t *)hpcd->Setup);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_DataOutStage(pdev, epnum, hpcd->OUT_ep[epnum].xfer_buff);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_DataInStage(pdev, epnum, hpcd->IN_ep[epnum].xfer_buff);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_SOF(pdev);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev == NULL)
    {
        return;
    }

    if (hpcd->Init.speed != PCD_SPEED_FULL)
    {
        Error_Handler();
        return;
    }

    (void)USBD_LL_SetSpeed(pdev, USBD_SPEED_FULL);
    (void)USBD_LL_Reset(pdev);
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_Suspend(pdev);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_Resume(pdev);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_IsoOUTIncomplete(pdev, epnum);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_IsoINIncomplete(pdev, epnum);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_DevConnected(pdev);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#endif
{
    USBD_HandleTypeDef *pdev = usb_device_from_pcd(hpcd);

    if (pdev != NULL)
    {
        (void)USBD_LL_DevDisconnected(pdev);
    }
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    if ((pdev == NULL) || (pdev->id != DEVICE_FS))
    {
        return USBD_FAIL;
    }

    hpcd_USB_FS.pData = pdev;
    pdev->pData = &hpcd_USB_FS;

    if (configure_pma() != USBD_OK)
    {
        return USBD_FAIL;
    }

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_SOF_CB_ID, PCD_SOFCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_SETUPSTAGE_CB_ID, PCD_SetupStageCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_RESET_CB_ID, PCD_ResetCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_SUSPEND_CB_ID, PCD_SuspendCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_RESUME_CB_ID, PCD_ResumeCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_CONNECT_CB_ID, PCD_ConnectCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterCallback(&hpcd_USB_FS, HAL_PCD_DISCONNECT_CB_ID, PCD_DisconnectCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterDataOutStageCallback(&hpcd_USB_FS, PCD_DataOutStageCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterDataInStageCallback(&hpcd_USB_FS, PCD_DataInStageCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterIsoOutIncpltCallback(&hpcd_USB_FS, PCD_ISOOUTIncompleteCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
    if (HAL_PCD_RegisterIsoInIncpltCallback(&hpcd_USB_FS, PCD_ISOINIncompleteCallback) != HAL_OK)
    {
        return USBD_FAIL;
    }
#endif

    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_DeInit(hpcd));
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_Start(hpcd));
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_Stop(hpcd));
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_Open(hpcd, ep_addr, ep_mps, ep_type));
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_Close(hpcd, ep_addr));
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_Flush(hpcd, ep_addr));
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_SetStall(hpcd, ep_addr));
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_ClrStall(hpcd, ep_addr));
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_SetAddress(hpcd, dev_addr));
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_Transmit(hpcd, ep_addr, pbuf, size));
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return USBD_FAIL;
    }

    return usb_status_from_hal(HAL_PCD_EP_Receive(hpcd, ep_addr, pbuf, size));
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);
    uint8_t epnum = ep_addr & 0x7FU;

    if (hpcd == NULL)
    {
        return 0U;
    }

    if ((ep_addr & 0x80U) != 0U)
    {
        return hpcd->IN_ep[epnum].is_stall;
    }

    return hpcd->OUT_ep[epnum].is_stall;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pcd_from_usb_device(pdev);

    if (hpcd == NULL)
    {
        return 0U;
    }

    return HAL_PCD_EP_GetRxCount(hpcd, ep_addr);
}

void USBD_LL_Delay(uint32_t delay_ms)
{
    HAL_Delay(delay_ms);
}
