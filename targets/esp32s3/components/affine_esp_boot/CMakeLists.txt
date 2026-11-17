idf_component_register(SRCS "affine_esp_boot.c" INCLUDE_DIRS "../../include"
    REQUIRES app_update esp_system WHOLE_ARCHIVE)
target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/rtc_request.ld")
if(CONFIG_SECURE_BOOT OR CONFIG_SECURE_BOOT_V2_ENABLED OR
   CONFIG_SECURE_FLASH_ENC_ENABLED OR CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK)
    message(FATAL_ERROR "This target forbids secure boot, flash encryption and IDF eFuse anti-rollback")
endif()
if(NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    message(FATAL_ERROR "This target requires otadata-only app rollback")
endif()
if(NOT CONFIG_ESP_CONSOLE_UART_DEFAULT OR NOT CONFIG_ESP_CONSOLE_SECONDARY_NONE)
    message(FATAL_ERROR "This target requires console/logs on UART0 only")
endif()
