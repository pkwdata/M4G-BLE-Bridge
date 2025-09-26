idf_component_register(
  SRCS "m4g_diag.c"
  INCLUDE_DIRS "include"
  REQUIRES m4g_logging m4g_led m4g_ble m4g_usb m4g_bridge
)
