idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip nvs_flash freertos log esp_system bt driver usb
)