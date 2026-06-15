#include "usbd_cdc_acm_if.h"

#include <string.h>

#include "main.h"
#include "usb_device.h"

#define APP_RX_DATA_SIZE  256U

extern USBD_HandleTypeDef hUsbDevice;
extern void boot_stm32f072_usb_receive_isr(const uint8_t *data, uint32_t length);

static uint8_t RX_Buffer[NUMBER_OF_CDC][APP_RX_DATA_SIZE];
static USBD_CDC_ACM_LineCodingTypeDef Line_Coding[NUMBER_OF_CDC];

static int8_t CDC_Init(uint8_t cdc_ch);
static int8_t CDC_DeInit(uint8_t cdc_ch);
static int8_t CDC_Control(uint8_t cdc_ch, uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len);
static int8_t CDC_TransmitCplt(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ACM_ItfTypeDef USBD_CDC_ACM_fops = {
    CDC_Init,
    CDC_DeInit,
    CDC_Control,
    CDC_Receive,
    CDC_TransmitCplt
};

static int8_t CDC_Init(uint8_t cdc_ch)
{
    memset(&Line_Coding[cdc_ch], 0, sizeof(Line_Coding[cdc_ch]));
    Line_Coding[cdc_ch].bitrate = 115200U;
    Line_Coding[cdc_ch].format = 0U;
    Line_Coding[cdc_ch].paritytype = 0U;
    Line_Coding[cdc_ch].datatype = 8U;
    USBD_CDC_SetRxBuffer(cdc_ch, &hUsbDevice, RX_Buffer[cdc_ch]);
    return (int8_t)USBD_OK;
}

static int8_t CDC_DeInit(uint8_t cdc_ch)
{
    (void)cdc_ch;
    return (int8_t)USBD_OK;
}

static int8_t CDC_Control(uint8_t cdc_ch, uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)length;

    switch (cmd)
    {
        case CDC_SET_LINE_CODING:
            Line_Coding[cdc_ch].bitrate = (uint32_t)(pbuf[0] | (pbuf[1] << 8) | (pbuf[2] << 16) | (pbuf[3] << 24));
            Line_Coding[cdc_ch].format = pbuf[4];
            Line_Coding[cdc_ch].paritytype = pbuf[5];
            Line_Coding[cdc_ch].datatype = pbuf[6];
            break;

        case CDC_GET_LINE_CODING:
            pbuf[0] = (uint8_t)(Line_Coding[cdc_ch].bitrate);
            pbuf[1] = (uint8_t)(Line_Coding[cdc_ch].bitrate >> 8);
            pbuf[2] = (uint8_t)(Line_Coding[cdc_ch].bitrate >> 16);
            pbuf[3] = (uint8_t)(Line_Coding[cdc_ch].bitrate >> 24);
            pbuf[4] = Line_Coding[cdc_ch].format;
            pbuf[5] = Line_Coding[cdc_ch].paritytype;
            pbuf[6] = Line_Coding[cdc_ch].datatype;
            break;

        default:
            break;
    }

    return (int8_t)USBD_OK;
}

static int8_t CDC_Receive(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len)
{
    boot_stm32f072_usb_diag_note_rx(*Len);
    boot_stm32f072_usb_receive_isr(Buf, *Len);
    USBD_CDC_SetRxBuffer(cdc_ch, &hUsbDevice, Buf);
    USBD_CDC_ReceivePacket(cdc_ch, &hUsbDevice);
    return (int8_t)USBD_OK;
}

static int8_t CDC_TransmitCplt(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    (void)cdc_ch;
    (void)Buf;
    (void)epnum;
    boot_stm32f072_usb_diag_note_tx_complete((Len != NULL) ? *Len : 0U);
    return (int8_t)USBD_OK;
}

uint8_t CDC_IsBusy(uint8_t ch)
{
    extern USBD_CDC_ACM_HandleTypeDef CDC_ACM_Class_Data[];
    return (CDC_ACM_Class_Data[ch].TxState != 0U) ? 1U : 0U;
}

uint8_t CDC_Transmit(uint8_t ch, uint8_t *buf, uint16_t len)
{
    extern USBD_CDC_ACM_HandleTypeDef CDC_ACM_Class_Data[];

    if (CDC_ACM_Class_Data[ch].TxState != 0U)
    {
        return USBD_BUSY;
    }

    USBD_CDC_SetTxBuffer(ch, &hUsbDevice, buf, len);
    return USBD_CDC_TransmitPacket(ch, &hUsbDevice);
}
