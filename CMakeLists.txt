cmake_minimum_required(VERSION 3.16)

# M4G BLE Bridge - Multi-platform USB-to-BLE HID bridge
# Supports: ESP32-S3 DevKit, Adafruit QT Py ESP32-S3, nRF52840+MAX3421E (future)
set(IDF_TARGET "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(__ROOT_KCONFIG "${CMAKE_CURRENT_LIST_DIR}/Kconfig")
project(M4G_BLE_BRIDGE)

# Platform abstraction component can access project root for includes
idf_build_get_property(component_targets __COMPONENT_TARGETS)
if("__idf_platform" IN_LIST component_targets)
    idf_component_get_property(platform_lib platform COMPONENT_LIB)
    target_include_directories(${platform_lib} PUBLIC ${CMAKE_SOURCE_DIR})
endif()