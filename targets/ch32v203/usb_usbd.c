#include "usb_lib.h"
#include "usb.h"
#include "usb_hw.h"
#include "UART.h"
#include "usb_pwr.h"
#include "usb_istr.h"
#include "hw_config.h"

struct boot_ch32_line_state Uart =
{
    {
        0x00, 0xC2, 0x01, 0x00, 0, 0, 8, 0
    }
};

uint8_t USBD_Endp3_Busy;
void USB_LP_CAN1_RX0_IRQHandler(void)__attribute__((interrupt("machine")));

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    USB_Istr();
}

void EP1_IN_Callback(void)
{
}

void EP2_OUT_Callback(void)
{
    uint8_t packet[64];
    uint16_t len = GetEPRxCount(ENDP2);
    SetEPRxStatus(ENDP2, EP_RX_NAK);
    if (len <= 64U)
    {
        PMAToUserBufferCopy(packet, GetEPRxAddr(ENDP2), len);
        boot_ch32_usb_rx(packet, len);
    }
}

void EP3_IN_Callback(void)
{
    USBD_Endp3_Busy = 0;
    boot_ch32_usb_tx_done();
}

void boot_ch32_hw_rx_arm(void)
{
    SetEPRxValid(ENDP2);
}

bool boot_ch32_hw_ready(void)
{
    return bDeviceState == CONFIGURED;
}

bool boot_ch32_hw_send(const uint8_t *data, uint16_t len)
{
    if (USBD_Endp3_Busy || len > 64U)
    {
        return false;
    }
    USB_SIL_Write(0x83, (uint8_t *)data, len);
    USBD_Endp3_Busy = 1;
    SetEPTxStatus(ENDP3, EP_TX_VALID);
    return true;
}

void USB_Port_Set(FunctionalState state, FunctionalState pullup)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIOA->CFGHR = (GPIOA->CFGHR & 0xFFF00FFFU) | (state ? 0x00044000U : 0x00033000U);
    GPIOA->OUTDR &= ~(3U << 11);
    if (pullup)
    {
        EXTEN->EXTEN_CTR |= EXTEN_USBD_PU_EN;
    }
    else
    {
        EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
    }
}

void Enter_LowPowerMode(void)
{
    bDeviceState = SUSPENDED;
}

void Leave_LowPowerMode(void)
{
    bDeviceState = Device_Info.Current_Configuration ? CONFIGURED : ATTACHED;
}

void boot_ch32_hw_init(void)
{
    /*
     * Start from a reset USBD whatever ran before: after the ROM ISP's reset
     * or a soft reset with USB active, bring-up otherwise hung on a C8T6 board.
     * The interrupt is enabled only once the SDK init has cleared the state.
     */
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_USB, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_USB, DISABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
    USB_Init();
    NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
}

void boot_ch32_hw_deinit(void)
{
    boot_ch32_hw_detach();
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    USB_Port_Set(DISABLE, DISABLE);
    PowerOff();
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_USB, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_USB, DISABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, DISABLE);
}
