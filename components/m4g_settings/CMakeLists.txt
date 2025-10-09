# Skip settings component for RIGHT side builds (no user settings needed)
if(CONFIG_M4G_SPLIT_ROLE_RIGHT)
    # Register as interface-only (no source files) to satisfy dependencies
    idf_component_register(INCLUDE_DIRS "include")
    return()
endif()

idf_component_register(
    SRCS "m4g_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
