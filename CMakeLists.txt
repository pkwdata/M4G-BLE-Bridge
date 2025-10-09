cmake_minimum_required(VERSION 3.16)

# M4G BLE Bridge - Multi-platform USB-to-BLE HID bridge
# Supports: ESP32-S3 DevKit, Adafruit QT Py ESP32-S3, nRF52840+MAX3421E (future)
set(IDF_TARGET "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(__ROOT_KCONFIG "${CMAKE_CURRENT_LIST_DIR}/Kconfig")
project(M4G_BLE_BRIDGE)

# Platform abstraction component can access project root for includes
if("__idf_platform" IN_LIST component_targets)
    idf_component_get_property(platform_lib platform COMPONENT_LIB)
    target_include_directories(${platform_lib} PUBLIC ${CMAKE_SOURCE_DIR})
endif()

# Determine firmware variant name based on split keyboard role configuration
# Check if config is defined to avoid defaults on first CMake run
if(DEFINED CONFIG_M4G_SPLIT_ROLE_LEFT AND CONFIG_M4G_SPLIT_ROLE_LEFT)
    set(FIRMWARE_VARIANT "LEFT")
elseif(DEFINED CONFIG_M4G_SPLIT_ROLE_RIGHT AND CONFIG_M4G_SPLIT_ROLE_RIGHT)
    set(FIRMWARE_VARIANT "RIGHT")
else()
    set(FIRMWARE_VARIANT "STANDALONE")
endif()

# Post-build: Create merged firmware binary for single-file flashing
# This combines bootloader + partition table + app into one binary at 0x0
# Note: This custom target runs after the standard .bin generation
add_custom_target(merged_binary
    COMMAND ${CMAKE_COMMAND} -E echo "Creating merged firmware binary for ${FIRMWARE_VARIANT} variant..."
    COMMAND ${PYTHON} -m esptool 
        --chip esp32s3 
        merge_bin
        --flash_mode dio
        --flash_freq 80m  
        --flash_size 4MB
        -o ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_${FIRMWARE_VARIANT}_MERGED.bin
        0x0 ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
        0x8000 ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
        0x10000 ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E echo "✓ Merged binary: ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_${FIRMWARE_VARIANT}_MERGED.bin"
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_${FIRMWARE_VARIANT}_MERGED.bin
        ${CMAKE_SOURCE_DIR}/tools/device-manager-web/public/firmware/${CMAKE_PROJECT_NAME}_${FIRMWARE_VARIANT}_MERGED.bin
    COMMAND ${CMAKE_COMMAND} -E echo "✓ Copied to Device Manager: tools/device-manager-web/public/firmware/${CMAKE_PROJECT_NAME}_${FIRMWARE_VARIANT}_MERGED.bin"
    DEPENDS gen_project_binary
    COMMENT "Merging firmware components into single flashable binary (${FIRMWARE_VARIANT})"
    VERBATIM
)

# Make the merged binary part of the default build
add_dependencies(app merged_binary)