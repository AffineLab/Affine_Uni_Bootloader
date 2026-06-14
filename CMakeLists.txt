cmake_minimum_required(VERSION 3.20)

project(affine_uni_bootloader C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES "1" CACHE STRING "Allow unsigned firmware updates")
set(AFFINE_BOOT_REQUIRE_SIGNED_UPDATES "0" CACHE STRING "Require signed firmware updates and signed committed apps")
set(AFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK "1" CACHE STRING "Allow signed manifests to opt into anti-rollback")
set(AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED "0" CACHE STRING "Require anti-rollback for every signed update")
set(AFFINE_BOOT_PRESERVE_SECURITY_STATE_ON_CLEAR_METADATA "1" CACHE STRING "Preserve rollback state when clearing app metadata")
set(AFFINE_BOOT_KEYS_HEADER "" CACHE FILEPATH "Optional generated boot key override header")

function(affine_bool_define out value)
    if("${value}")
        set(${out} 1 PARENT_SCOPE)
    else()
        set(${out} 0 PARENT_SCOPE)
    endif()
endfunction()

function(affine_apply_boot_policy target_name)
    affine_bool_define(_allow_unsigned "${AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES}")
    affine_bool_define(_require_signed "${AFFINE_BOOT_REQUIRE_SIGNED_UPDATES}")
    affine_bool_define(_allow_optional_rollback "${AFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK}")
    affine_bool_define(_require_rollback "${AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED}")
    affine_bool_define(_preserve_security_state "${AFFINE_BOOT_PRESERVE_SECURITY_STATE_ON_CLEAR_METADATA}")

    target_compile_definitions(${target_name} PRIVATE
        AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES=${_allow_unsigned}
        AFFINE_BOOT_REQUIRE_SIGNED_UPDATES=${_require_signed}
        AFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK=${_allow_optional_rollback}
        AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED=${_require_rollback}
        AFFINE_BOOT_PRESERVE_SECURITY_STATE_ON_CLEAR_METADATA=${_preserve_security_state}
    )

    if(AFFINE_BOOT_KEYS_HEADER)
        get_filename_component(_boot_keys_header "${AFFINE_BOOT_KEYS_HEADER}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${_boot_keys_header}")
            message(FATAL_ERROR "AFFINE_BOOT_KEYS_HEADER does not exist: ${_boot_keys_header}")
        endif()
        target_compile_definitions(${target_name} PRIVATE
            "AFFINE_BOOT_KEYS_OVERRIDE_HEADER=\"${_boot_keys_header}\"")
    endif()
endfunction()

add_library(boot_core STATIC
    src/boot_protocol.c
    src/boot_crypto.c
    src/boot_image.c
    src/boot_security.c
    src/bootloader.c
)

target_include_directories(boot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
affine_apply_boot_policy(boot_core)

add_executable(bootloader_stub EXCLUDE_FROM_ALL
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
option(AFFINE_BUILD_STM32H503 "Build STM32H503 bootloader target" OFF)

set(BOOT_TARGET_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/targets/common/boot_target_common.c
)

function(affine_require_existing_path path description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "Vendored ${description} was not found at:\n"
            "  ${path}\n"
            "Restore the required third_party subtree before building this target."
        )
    endif()
endfunction()

if(AFFINE_BUILD_STM32G431)
    set(STM32G431_CUBE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stm32cube_g4")
    set(STM32G431_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/targets/stm32g431")
    set(STM32G431_COMPOSITE_DIR "${STM32G431_CUBE_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE")

    affine_require_existing_path("${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver"
        "STM32G431 HAL driver directory")
    affine_require_existing_path("${STM32G431_CUBE_DIR}/Core/Startup/startup_stm32g431cbux.s"
        "STM32G431 startup file")
    affine_require_existing_path("${STM32G431_COMPOSITE_DIR}/Core/Src/usbd_core.c"
        "STM32G431 USB Device middleware")

    add_executable(bootloader_stm32g431
        src/boot_protocol.c
        src/boot_crypto.c
        src/boot_image.c
        src/boot_security.c
        src/bootloader.c
        ${BOOT_TARGET_COMMON_SOURCES}
        ${STM32G431_TARGET_DIR}/main.c
        ${STM32G431_TARGET_DIR}/usb.c
        ${STM32G431_TARGET_DIR}/usbd_desc.c
        ${STM32G431_TARGET_DIR}/usbd_cdc_acm_if.c
        ${STM32G431_TARGET_DIR}/boot_platform_stm32g431.c
        ${STM32G431_TARGET_DIR}/baremetal_runtime.c
        ${STM32G431_TARGET_DIR}/stm32g4xx_it.c
        ${STM32G431_CUBE_DIR}/Core/Src/system_stm32g4xx.c
        ${STM32G431_CUBE_DIR}/Core/Startup/startup_stm32g431cbux.s
        ${STM32G431_COMPOSITE_DIR}/App/usb_device.c
        ${STM32G431_TARGET_DIR}/usbd_conf.c
        ${STM32G431_COMPOSITE_DIR}/Core/Src/usbd_core.c
        ${STM32G431_COMPOSITE_DIR}/Core/Src/usbd_ctlreq.c
        ${STM32G431_COMPOSITE_DIR}/Core/Src/usbd_ioreq.c
        ${STM32G431_COMPOSITE_DIR}/Class/COMPOSITE/Src/usbd_composite.c
        ${STM32G431_COMPOSITE_DIR}/Class/CDC_ACM/Src/usbd_cdc_acm.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd_ex.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_usb.c
    )

    target_include_directories(bootloader_stm32g431 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${STM32G431_TARGET_DIR}
        ${STM32G431_CUBE_DIR}/Core/Inc
        ${STM32G431_CUBE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc
        ${STM32G431_CUBE_DIR}/Drivers/CMSIS/Include
        ${STM32G431_CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Include
        ${STM32G431_COMPOSITE_DIR}/Core/Inc
        ${STM32G431_COMPOSITE_DIR}/Class/CDC_ACM/Inc
        ${STM32G431_COMPOSITE_DIR}/Class/COMPOSITE/Inc
    )

    target_compile_definitions(bootloader_stm32g431 PRIVATE
        STM32G431xx
        USE_HAL_DRIVER
    )
    affine_apply_boot_policy(bootloader_stm32g431)

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

    set_source_files_properties(
        ${STM32G431_COMPOSITE_DIR}/Class/COMPOSITE/Src/usbd_composite.c
        ${STM32G431_COMPOSITE_DIR}/Class/CDC_ACM/Src/usbd_cdc_acm.c
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
    )

    target_link_options(bootloader_stm32g431 PRIVATE
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -flto
        -nostartfiles
        --specs=nano.specs
        --specs=nosys.specs
        -T${STM32G431_TARGET_DIR}/STM32G431CBUX_BOOTLOADER.ld
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader_stm32g431.map
    )

    if(CMAKE_OBJCOPY)
        add_custom_command(TARGET bootloader_stm32g431 POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:bootloader_stm32g431>
                ${CMAKE_BINARY_DIR}/bootloader_stm32g431.bin
            BYPRODUCTS ${CMAKE_BINARY_DIR}/bootloader_stm32g431.bin
            COMMENT "Generating bootloader_stm32g431.bin"
        )
    endif()
endif()

if(AFFINE_BUILD_STM32H503)
    set(STM32H503_CUBE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stm32cube_h5")
    set(STM32H503_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/targets/stm32h503")
    set(STM32H503_COMPOSITE_DIR "${STM32H503_CUBE_DIR}/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE")

    affine_require_existing_path("${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver"
        "STM32H503 HAL driver directory")
    affine_require_existing_path("${STM32H503_CUBE_DIR}/Core/Startup/startup_stm32h503cbtx.s"
        "STM32H503 startup file")
    affine_require_existing_path("${STM32H503_COMPOSITE_DIR}/Core/Src/usbd_core.c"
        "STM32H503 USB Device middleware")

    add_executable(bootloader_stm32h503
        src/boot_protocol.c
        src/boot_crypto.c
        src/boot_image.c
        src/boot_security.c
        src/bootloader.c
        ${BOOT_TARGET_COMMON_SOURCES}
        ${STM32H503_TARGET_DIR}/main.c
        ${STM32H503_TARGET_DIR}/usb.c
        ${STM32H503_TARGET_DIR}/usbd_desc.c
        ${STM32H503_TARGET_DIR}/usbd_cdc_acm_if.c
        ${STM32H503_TARGET_DIR}/boot_platform_stm32h503.c
        ${STM32H503_TARGET_DIR}/baremetal_runtime.c
        ${STM32H503_TARGET_DIR}/stm32h5xx_it.c
        ${STM32H503_CUBE_DIR}/Core/Src/system_stm32h5xx.c
        ${STM32H503_CUBE_DIR}/Core/Startup/startup_stm32h503cbtx.s
        ${STM32H503_COMPOSITE_DIR}/App/usb_device.c
        ${STM32H503_COMPOSITE_DIR}/Target/usbd_conf.c
        ${STM32H503_COMPOSITE_DIR}/Core/Src/usbd_core.c
        ${STM32H503_COMPOSITE_DIR}/Core/Src/usbd_ctlreq.c
        ${STM32H503_COMPOSITE_DIR}/Core/Src/usbd_ioreq.c
        ${STM32H503_COMPOSITE_DIR}/Class/COMPOSITE/Src/usbd_composite.c
        ${STM32H503_COMPOSITE_DIR}/Class/CDC_ACM/Src/usbd_cdc_acm.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_cortex.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash_ex.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pcd.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pcd_ex.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr_ex.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc_ex.c
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_ll_usb.c
    )

    target_include_directories(bootloader_stm32h503 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${STM32H503_TARGET_DIR}
        ${STM32H503_CUBE_DIR}/Core/Inc
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Inc
        ${STM32H503_CUBE_DIR}/Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
        ${STM32H503_CUBE_DIR}/Drivers/CMSIS/Include
        ${STM32H503_CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32H5xx/Include
        ${STM32H503_COMPOSITE_DIR}/App
        ${STM32H503_COMPOSITE_DIR}/Target
        ${STM32H503_COMPOSITE_DIR}/Core/Inc
        ${STM32H503_COMPOSITE_DIR}/Class/CDC_ACM/Inc
        ${STM32H503_COMPOSITE_DIR}/Class/COMPOSITE/Inc
    )

    target_compile_definitions(bootloader_stm32h503 PRIVATE
        STM32H503xx
        USE_HAL_DRIVER
    )
    affine_apply_boot_policy(bootloader_stm32h503)

    target_compile_options(bootloader_stm32h503 PRIVATE
        -mcpu=cortex-m33
        -mthumb
        -Oz
        -flto
        -ffunction-sections
        -fdata-sections
        -fomit-frame-pointer
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
        -Wall
        -Wextra
    )

    set_source_files_properties(
        ${STM32H503_COMPOSITE_DIR}/Target/usbd_conf.c
        ${STM32H503_COMPOSITE_DIR}/Class/COMPOSITE/Src/usbd_composite.c
        ${STM32H503_COMPOSITE_DIR}/Class/CDC_ACM/Src/usbd_cdc_acm.c
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
    )

    target_link_options(bootloader_stm32h503 PRIVATE
        -mcpu=cortex-m33
        -mthumb
        -flto
        -nostartfiles
        --specs=nano.specs
        --specs=nosys.specs
        -T${STM32H503_TARGET_DIR}/STM32H503CBTX_BOOTLOADER.ld
        -Wl,--gc-sections
        -Wl,--wrap=HAL_RCC_GetSysClockFreq
        -Wl,--wrap=HAL_RCC_GetHCLKFreq
        -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader_stm32h503.map
    )

    if(CMAKE_OBJCOPY)
        add_custom_command(TARGET bootloader_stm32h503 POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:bootloader_stm32h503>
                ${CMAKE_BINARY_DIR}/bootloader_stm32h503.bin
            BYPRODUCTS ${CMAKE_BINARY_DIR}/bootloader_stm32h503.bin
            COMMENT "Generating bootloader_stm32h503.bin"
        )
    endif()

endif()
