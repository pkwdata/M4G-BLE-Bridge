idf_component_register(
    SRCS "m4g_espnow.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_wifi esp_netif esp_event m4g_logging
)
