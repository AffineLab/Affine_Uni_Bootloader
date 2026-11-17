#include <assert.h>
#include <stdio.h>
#include "ch32v20x.h"
#include "ch32v20x_usb.h"
#include "usb_hw.h"

EXTEN_Type host_exten;
USBFSD_Type host_usbfs;
static uint32_t delay_calls;

void Delay_Ms(uint32_t ms)
{
    assert(ms == 50U);
    assert((host_exten.EXTEN_CTR & EXTEN_USBD_PU_EN) == 0U);
    assert((host_usbfs.BASE_CTRL & USBFS_UC_DEV_PU_EN) == 0U);
    ++delay_calls;
}

int main(void)
{
    /* Exercise the exact function called by both USB hardware deinitializers. */
    host_exten.EXTEN_CTR = EXTEN_USBD_PU_EN | 0x80U;
    host_usbfs.BASE_CTRL = USBFS_UC_DEV_PU_EN | 0x01U;
    boot_ch32_hw_detach();
    assert(delay_calls == 1U);
    assert(host_exten.EXTEN_CTR == 0x80U);
    assert(host_usbfs.BASE_CTRL == 0x01U);
    puts("ALL PASS: both CH32 pull-ups off before the 50 ms detach interval");
    return 0;
}
