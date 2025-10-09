# M4G BLE Bridge - Build Instructions

This project supports three firmware variants for different use cases:

## Firmware Variants

### 1. **STANDALONE** (Single Keyboard)
- Original single keyboard mode
- USB → BLE bridge for one keyboard
- Uses `main.c`

### 2. **LEFT** (Split Keyboard - Left Side)
- Receives from local USB keyboard (left half)
- Receives from RIGHT side via ESP-NOW
- Combines both inputs and transmits via Bluetooth
- Uses `main_left.c`

### 3. **RIGHT** (Split Keyboard - Right Side)
- Receives from local USB keyboard (right half)
- Transmits to LEFT side via ESP-NOW
- No Bluetooth output (acts as relay only)
- Uses `main_right.c`

## Building Firmware

### Prerequisites
Make sure ESP-IDF environment is loaded:
```bash
. $HOME/esp/esp-idf/export.sh
```

### Build Individual Variants

#### Standalone Mode
```bash
./build-standalone.sh
```
Output: `build/M4G_BLE_BRIDGE_STANDALONE_MERGED.bin`

#### Left Side (Split Keyboard)
```bash
./build-left.sh
```
Output: `build/M4G_BLE_BRIDGE_LEFT_MERGED.bin`

#### Right Side (Split Keyboard)
```bash
./build-right.sh
```
Output: `build/M4G_BLE_BRIDGE_RIGHT_MERGED.bin`

### Build All Variants at Once
```bash
./build-all.sh
```
This script:
- Builds all three variants sequentially
- Automatically switches configurations between builds
- Copies binaries to device-manager-web
- Shows build summary with file sizes and timing

## Output Files

Each build produces:
- **Standard binaries** (in `build/`):
  - `M4G_BLE_BRIDGE.bin` - Application binary
  - `bootloader/bootloader.bin` - Bootloader
  - `partition_table/partition-table.bin` - Partition table

- **Merged binary** (in `build/`):
  - `M4G_BLE_BRIDGE_{VARIANT}_MERGED.bin` - Single-file flashable binary

- **Web assets** (in `tools/device-manager-web/public/firmware/`):
  - `M4G_BLE_BRIDGE_{VARIANT}_MERGED.bin` - Copy for web-based updater

## Flashing Firmware

### Using build script
```bash
./build-left.sh flash          # Build and flash
./build-left.sh flash monitor  # Build, flash, and open serial monitor
```

### Using idf.py
```bash
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Using merged binary
```bash
python -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0 build/M4G_BLE_BRIDGE_LEFT_MERGED.bin
```

### Using web-based OTA (if available)
Visit the device manager web interface and select the appropriate firmware variant.

## Build System Details

### Configuration Switching
Each build script:
1. Checks current build role (stored in `build/.role`)
2. Cleans build directory if switching variants
3. Combines `sdkconfig.defaults` with variant-specific config
4. Reconfigures CMake with new settings
5. Builds and generates variant-specific merged binary

### Merged Binary Generation
The CMakeLists.txt automatically:
- Detects the current variant from Kconfig
- Names the merged binary appropriately
- Copies to device-manager-web assets
- Only runs after `.bin` generation completes

## Troubleshooting

### Build fails with "idf.py: command not found"
Solution: Load ESP-IDF environment first:
```bash
. $HOME/esp/esp-idf/export.sh
```

### Wrong firmware variant built
Solution: Clean and rebuild:
```bash
rm -rf build
./build-left.sh  # or your desired variant
```

### Merged binary not found
Solution: The merged binary is created after the main build completes. Check for errors in the build output.

## Development Workflow

### For Split Keyboard Development
1. Build and flash LEFT firmware to one ESP32-S3
2. Build and flash RIGHT firmware to another ESP32-S3
3. Power on RIGHT side first, then LEFT side
4. Pair LEFT side with your computer via Bluetooth

### For Testing Standalone Mode
1. Build and flash STANDALONE firmware
2. Pair with your computer via Bluetooth
3. Connect USB keyboard

## See Also
- `Readme.md` - Project overview and features
- `docs/` - Additional documentation
- `Kconfig` - Configuration options
