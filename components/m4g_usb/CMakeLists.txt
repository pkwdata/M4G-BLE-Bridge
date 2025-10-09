# m4g_usb component can work with or without BLE/bridge depending on variant
set(USB_REQUIRES m4g_logging m4g_led usb)

# Only add BLE and bridge for LEFT and STANDALONE (not RIGHT)
if(NOT CONFIG_M4G_SPLIT_ROLE_RIGHT)
    list(APPEND USB_REQUIRES m4g_ble m4g_bridge)
endif()

idf_component_register(SRCS "m4g_usb.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ${USB_REQUIRES})