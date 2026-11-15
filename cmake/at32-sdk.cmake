# Artery's sources stay outside this repository and retain their BSP terms.
set(AFFINE_AT32_SDK_DIR "" CACHE PATH "AT32F402_405 firmware library v2.1.5 directory")
set(AFFINE_AT32_SDK_COMMIT "5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3")

if(NOT AFFINE_AT32_SDK_DIR)
    message(FATAL_ERROR "AT32F405 requires -DAFFINE_AT32_SDK_DIR=<AT32F402_405_Firmware_Library> (v2.1.5, ${AFFINE_AT32_SDK_COMMIT}); the SDK is not vendored")
endif()
get_filename_component(AFFINE_AT32_SDK_DIR "${AFFINE_AT32_SDK_DIR}" ABSOLUTE)

set(AT32_SDK_SOURCES
    libraries/cmsis/cm4/device_support/system_at32f402_405.c
    libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f402_405.s
    libraries/drivers/src/at32f402_405_acc.c
    libraries/drivers/src/at32f402_405_crm.c
    libraries/drivers/src/at32f402_405_flash.c
    libraries/drivers/src/at32f402_405_usb.c
    middlewares/usb_drivers/src/usb_core.c
    middlewares/usb_drivers/src/usbd_core.c
    middlewares/usb_drivers/src/usbd_int.c
    middlewares/usb_drivers/src/usbd_sdr.c
)
set(AT32_SDK_INCLUDES
    libraries/cmsis/cm4/device_support
    libraries/cmsis/cm4/core_support
    libraries/drivers/inc
    middlewares/usb_drivers/inc
    middlewares/usbd_class/cdc
)
foreach(_file IN LISTS AT32_SDK_SOURCES)
    if(NOT EXISTS "${AFFINE_AT32_SDK_DIR}/${_file}")
        message(FATAL_ERROR "Invalid AFFINE_AT32_SDK_DIR: missing ${AFFINE_AT32_SDK_DIR}/${_file}; use AT32F402_405_Firmware_Library v2.1.5 (${AFFINE_AT32_SDK_COMMIT})")
    endif()
endforeach()
foreach(_file IN ITEMS libraries/cmsis/cm4/device_support/at32f402_405.h
        libraries/cmsis/cm4/core_support/core_cm4.h
        middlewares/usbd_class/cdc/cdc_class.h)
    if(NOT EXISTS "${AFFINE_AT32_SDK_DIR}/${_file}")
        message(FATAL_ERROR "Invalid AFFINE_AT32_SDK_DIR: missing ${_file}")
    endif()
endforeach()
list(TRANSFORM AT32_SDK_SOURCES PREPEND "${AFFINE_AT32_SDK_DIR}/")
list(TRANSFORM AT32_SDK_INCLUDES PREPEND "${AFFINE_AT32_SDK_DIR}/")
