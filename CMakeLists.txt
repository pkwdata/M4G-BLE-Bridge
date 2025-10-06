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

# Post-build: Create merged firmware binary for single-file flashing
# This combines bootloader + partition table + app into one binary at 0x0
add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Creating merged firmware binary..."
    COMMAND ${PYTHON} -m esptool 
        --chip esp32s3 
        merge_bin
        --flash_mode dio
        --flash_freq 80m  
        --flash_size 4MB
        -o ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_MERGED.bin
        0x0 ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
        0x8000 ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
        0x10000 ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E echo "✓ Merged binary: ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_MERGED.bin"
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_MERGED.bin
        ${CMAKE_SOURCE_DIR}/tools/device-manager-web/public/firmware/${CMAKE_PROJECT_NAME}_MERGED.bin
    COMMAND ${CMAKE_COMMAND} -E echo "✓ Copied to Device Manager: tools/device-manager-web/public/firmware/"
    COMMENT "Merging firmware components into single flashable binary"
    VERBATIM
)