get_filename_component(BOOT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(SRCS "main.c" "platform.c" "usb.c" "log_cdc.c"
    "${BOOT_ROOT}/src/bootloader.c" "${BOOT_ROOT}/src/boot_protocol.c"
    "${BOOT_ROOT}/src/boot_image.c" "${BOOT_ROOT}/src/boot_security.c"
    "${BOOT_ROOT}/src/boot_crypto.c"
    INCLUDE_DIRS "." "${BOOT_ROOT}/include"
    REQUIRES affine_esp_boot app_update esp_timer esp_system spi_flash esp_hw_support
        bootloader_support esp_tinyusb esp_hal_usb)
target_compile_definitions(${COMPONENT_LIB} PRIVATE AFFINE_BOOT_ESP32S3=1)
if(CONFIG_SPIRAM)
    message(FATAL_ERROR "The ESP32-S3 agent requires PSRAM disabled")
endif()
set(AFFINE_BOOT_KEYS_HEADER "" CACHE FILEPATH "Optional generated shared-core key override header")
if(AFFINE_BOOT_KEYS_HEADER)
    get_filename_component(_keys "${AFFINE_BOOT_KEYS_HEADER}" ABSOLUTE)
    if(NOT EXISTS "${_keys}")
        message(FATAL_ERROR "Missing key override: ${_keys}")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "AFFINE_BOOT_KEYS_OVERRIDE_HEADER=\"${_keys}\"")
endif()
