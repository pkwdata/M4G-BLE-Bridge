set(M4G_BRIDGE_SRCS "m4g_bridge.c")

# Only build test stub if manually enabled (e.g., -DM4G_BUILD_BRIDGE_STUB=ON)
if(M4G_BUILD_BRIDGE_STUB)
	list(APPEND M4G_BRIDGE_SRCS "test_bridge_stub.c")
endif()

idf_component_register(SRCS ${M4G_BRIDGE_SRCS} INCLUDE_DIRS "include" REQUIRES m4g_ble m4g_logging)