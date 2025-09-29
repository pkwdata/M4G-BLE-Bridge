idf_component_register(SRCS "m4g_ble.c"
                       INCLUDE_DIRS "include"
                       REQUIRES m4g_logging m4g_led m4g_bridge nvs_flash bt
                       EMBED_TXTFILES "hid_report_map.txt")
