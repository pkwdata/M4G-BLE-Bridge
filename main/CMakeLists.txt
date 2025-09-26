idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip nvs_flash freertos log esp_system bt driver usb m4g_logging m4g_led m4g_ble m4g_usb m4g_bridge m4g_diag
)