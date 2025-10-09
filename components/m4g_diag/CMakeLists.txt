# m4g_diag can work with or without BLE/bridge depending on variant
set(DIAG_REQUIRES m4g_logging m4g_led m4g_usb)

# Only add BLE and bridge for LEFT and STANDALONE (not RIGHT)
if(NOT CONFIG_M4G_SPLIT_ROLE_RIGHT)
    list(APPEND DIAG_REQUIRES m4g_ble m4g_bridge)
endif()

idf_component_register(
  SRCS "m4g_diag.c"
  INCLUDE_DIRS "include"
  REQUIRES ${DIAG_REQUIRES}
)
