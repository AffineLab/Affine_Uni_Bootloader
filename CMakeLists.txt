cmake_minimum_required(VERSION 3.20)

project(affine_uni_bootloader C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(boot_core STATIC
    src/boot_protocol.c
    src/boot_image.c
    src/boot_security.c
    src/bootloader.c
)

target_include_directories(boot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(bootloader_stub
    src/main.c
    targets/stub/boot_platform_stub.c
)

target_include_directories(bootloader_stub
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(bootloader_stub PRIVATE boot_core)

option(AFFINE_BUILD_STM32G431 "Build STM32G431 bootloader target" OFF)

if(AFFINE_BUILD_STM32G431)
    set(MAI_STM32_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Mai_stm32")
    set(STM32G431_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/targets/stm32g431")

    add_executable(bootloader_stm32g431
        src/boot_protocol.c
        src/boot_image.c
        src/boot_security.c
        src/bootloader.c
        ${STM32G431_TARGET_DIR}/main.c
        ${STM32G431_TARGET_DIR}/usb.c
        ${STM32G431_TARGET_DIR}/usbd_desc.c
        ${STM32G431_TARGET_DIR}/usbd_cdc_acm_if.c
        ${STM32G431_TARGET_DIR}/boot_platform_stm32g431.c
        ${STM32G431_TARGET_DIR}/stm32g4xx_it.c
        ${MAI_STM32_DIR}/Core/Src/system_stm32g4xx.c
        ${MAI_STM32_DIR}/Core/Startup/startup_stm32g431cbux.s
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/App/usb_device.c
        ${STM32G431_TARGET_DIR}/usbd_conf.c
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Core/Src/usbd_core.c
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Core/Src/usbd_ctlreq.c
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Core/Src/usbd_ioreq.c
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/COMPOSITE/Src/usbd_composite.c
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/CDC_ACM/Src/usbd_cdc_acm.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd_ex.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_usb.c
    )

    target_include_directories(bootloader_stm32g431 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${STM32G431_TARGET_DIR}
        ${MAI_STM32_DIR}/Core/Inc
        ${MAI_STM32_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc
        ${MAI_STM32_DIR}/Drivers/CMSIS/Include
        ${MAI_STM32_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Include
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Core/Inc
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/CDC_ACM/Inc
        ${MAI_STM32_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/COMPOSITE/Inc
    )

    target_compile_definitions(bootloader_stm32g431 PRIVATE
        STM32G431xx
        USE_HAL_DRIVER
    )

    target_compile_options(bootloader_stm32g431 PRIVATE
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -Os
        -flto
        -ffunction-sections
        -fdata-sections
        -fomit-frame-pointer
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
        -Wall
        -Wextra
    )

    target_link_options(bootloader_stm32g431 PRIVATE
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -flto
        --specs=nano.specs
        --specs=nosys.specs
        -T${STM32G431_TARGET_DIR}/STM32G431CBUX_BOOTLOADER.ld
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader_stm32g431.map
    )
endif()
