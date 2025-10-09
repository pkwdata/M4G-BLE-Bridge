# Select main source file and dependencies based on split keyboard role
# Note: Config variables may not be available on first CMake run, so we include
# all possible components and let conditional compilation handle the rest
if(DEFINED CONFIG_M4G_SPLIT_ROLE_LEFT AND CONFIG_M4G_SPLIT_ROLE_LEFT)
    set(MAIN_SRCS "main_left.c")
    set(MAIN_REQUIRES nvs_flash freertos log esp_system bt driver usb esp_wifi esp_netif esp_event platform m4g_logging m4g_led m4g_ble m4g_usb m4g_bridge m4g_espnow m4g_diag m4g_settings)
    message(STATUS "Building LEFT side firmware (USB + ESP-NOW RX + BLE)")
elseif(DEFINED CONFIG_M4G_SPLIT_ROLE_RIGHT AND CONFIG_M4G_SPLIT_ROLE_RIGHT)
    set(MAIN_SRCS "main_right.c")
    # RIGHT side doesn't need Bluetooth stack (bt, m4g_ble) or bridge/settings  
    set(MAIN_REQUIRES nvs_flash freertos log esp_system driver usb esp_wifi esp_netif esp_event m4g_logging m4g_led m4g_usb m4g_espnow m4g_diag)
    message(STATUS "Building RIGHT side firmware (USB + ESP-NOW TX only)")
else()
    # Standalone mode (original single keyboard) OR config not yet loaded
    # Include m4g_espnow just in case (will be unused in standalone builds)
    set(MAIN_SRCS "main.c")
    set(MAIN_REQUIRES nvs_flash freertos log esp_system bt driver usb platform m4g_logging m4g_led m4g_ble m4g_usb m4g_bridge m4g_espnow m4g_diag m4g_settings)
    message(STATUS "Building STANDALONE firmware (single keyboard, original behavior)")
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${MAIN_REQUIRES}
)