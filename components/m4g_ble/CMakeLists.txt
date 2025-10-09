# Skip BLE component for RIGHT side builds (no Bluetooth needed)
if(CONFIG_M4G_SPLIT_ROLE_RIGHT)
    # Register as interface-only (no source files) to satisfy dependencies
    idf_component_register(INCLUDE_DIRS "include")
    return()
endif()

idf_component_register(SRCS "m4g_ble.c"
                       INCLUDE_DIRS "include"
                       REQUIRES m4g_logging m4g_led m4g_bridge nvs_flash bt
                       EMBED_TXTFILES "hid_report_map.txt")
