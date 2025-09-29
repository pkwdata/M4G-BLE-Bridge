# Platform Abstraction Layer Component

# Include platform-specific build files
include(${CMAKE_CURRENT_LIST_DIR}/esp32s3/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/nrf52840/CMakeLists.txt)

# Default to ESP32-S3 if no platform is explicitly selected
if(NOT DEFINED PLATFORM_SOURCES)
    include(${CMAKE_CURRENT_LIST_DIR}/esp32s3/CMakeLists.txt)
    message(STATUS "No platform selected, defaulting to ESP32-S3")
endif()

# Create platform abstraction component
idf_component_register(
    SRCS ${PLATFORM_SOURCES}
    INCLUDE_DIRS ${PLATFORM_INCLUDES}
    REQUIRES ${PLATFORM_REQUIRES}
    PRIV_REQUIRES ${PLATFORM_PRIV_REQUIRES}
)