# Platform-specific build rules for ESP32-S3

if(CONFIG_M4G_TARGET_ESP32S3)
    # Add ESP32-S3 platform adapter sources
    set(PLATFORM_SOURCES 
        ${CMAKE_CURRENT_LIST_DIR}/m4g_platform_esp32s3.c
    )
    
    # Add platform include directory
    set(PLATFORM_INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../common
    )
    
    # ESP32-S3 specific component dependencies
    set(PLATFORM_REQUIRES
        esp_hw_support
        driver
        nvs_flash
        bt
    )
    
    set(PLATFORM_PRIV_REQUIRES
        # Private requirements go here
    )
    
    message(STATUS "Building for ESP32-S3 platform")
endif()