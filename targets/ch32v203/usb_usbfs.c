#include "usb.h"
#include "usb_hw.h"
#include "ch32v20x_usbfs_device.h"

struct boot_ch32_line_state Uart =
{
    {
        0x00, 0xC2, 0x01, 0x00, 0, 0, 8, 0
    }
};

void boot_ch32_hw_init(void)
{
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
}

void boot_ch32_hw_deinit(void)
{
    boot_ch32_hw_detach();
    USBFS_Device_Init(DISABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, DISABLE);
}

bool boot_ch32_hw_ready(void)
{
    return USBFS_DevConfig == 1U && !(USBFS_DevSleepStatus & 2U);
}

bool boot_ch32_hw_send(const uint8_t *data, uint16_t len)
{
    return len <= 64U && USBFS_Endp_DataUp(DEF_UEP3, (uint8_t *)data, len, DEF_UEP_CPY_LOAD) == 0;
}

void boot_ch32_hw_rx_arm(void)
{
    USBFSD->UEP2_RX_CTRL = (USBFSD->UEP2_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
}
