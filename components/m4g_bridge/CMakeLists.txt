set(M4G_BRIDGE_SRCS "m4g_bridge.c")

idf_component_register(SRCS ${M4G_BRIDGE_SRCS} INCLUDE_DIRS "include" REQUIRES m4g_ble m4g_logging m4g_settings)