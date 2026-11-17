#include "ch32v20x.h"
#include "ch32v20x_usb.h"
#include "debug.h"
#include "usb_hw.h"

/* SDK IAP_2_APP holds both pull-ups off for 50 ms before resetting clocks. */
void boot_ch32_hw_detach(void)
{
    EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
    USBFSD->BASE_CTRL &= ~USBFS_UC_DEV_PU_EN;
    Delay_Ms(50U);
}
