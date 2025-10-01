cmake_minimum_required(VERSION 3.16)

# Board: Adafruit QT Py ESP32-S3
set(IDF_TARGET "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(__ROOT_KCONFIG "${CMAKE_CURRENT_LIST_DIR}/Kconfig")
project(M4G_BLE_BRIDGE)

# Ensure platform abstraction component can include headers via project-root relative paths
idf_build_get_component_property(platform_component platform COMPONENT_LIB)
if(platform_component)
    target_include_directories(${platform_component} PUBLIC ${CMAKE_SOURCE_DIR})
endif()