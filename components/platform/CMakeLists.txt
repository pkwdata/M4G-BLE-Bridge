# Platform Abstraction Layer Component
# Provides unified API for different hardware targets (ESP32-S3, nRF52840+MAX3421E)

idf_component_register(
    SRCS "m4g_platform_esp32s3.c"
    INCLUDE_DIRS "include"
    REQUIRES m4g_usb m4g_ble m4g_bridge m4g_led m4g_logging nvs_flash
)

# Allow this component to access project root for includes
target_include_directories(${COMPONENT_LIB} PUBLIC ${CMAKE_SOURCE_DIR})
