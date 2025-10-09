# Skip bridge component for RIGHT side builds (no bridge needed)
if(CONFIG_M4G_SPLIT_ROLE_RIGHT)
    # Register as interface-only (no source files) to satisfy dependencies
    idf_component_register(INCLUDE_DIRS "include")
    return()
endif()

set(M4G_BRIDGE_SRCS "m4g_bridge.c")

idf_component_register(SRCS ${M4G_BRIDGE_SRCS} INCLUDE_DIRS "include" REQUIRES m4g_ble m4g_logging m4g_settings)