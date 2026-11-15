#ifndef AFFINE_AT32_PERIPH_CONF_H
#define AFFINE_AT32_PERIPH_CONF_H

#define HEXT_VALUE 12000000U
#define HEXT_STARTUP_TIMEOUT 0x3000U
/* SDK convention: HICK_VALUE describes HICK48 / 6. */
#define HICK_VALUE 8000000U
#define LEXT_VALUE 32768U
#define ACC_MODULE_ENABLED
#define CRM_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define PWC_MODULE_ENABLED
#define USB_MODULE_ENABLED
#define WDT_MODULE_ENABLED
#include "at32f402_405_acc.h"
#include "at32f402_405_crm.h"
#include "at32f402_405_flash.h"
#include "at32f402_405_pwc.h"
#include "at32f402_405_usb.h"
#include "at32f402_405_wdt.h"

#endif
