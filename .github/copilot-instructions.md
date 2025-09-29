# M4G BLE Bridge - AI Coding Agent Instructions

## Project Overview
ESP32-S3 USB-to-BLE bridge converting USB keyboards (especially CharaChorder MasterForge) to Bluetooth HID devices. Uses ESP-IDF v6.0+ with modular component architecture.

## Architecture & Data Flow
```
USB Keyboard → m4g_usb → m4g_bridge → m4g_ble → BLE HID → Computer
                     ↓
                m4g_logging (NVS persistence)
                     ↓  
                  m4g_led (status)
```

**Core Components** (in `components/`):
- `m4g_usb`: USB host with hub support for dual-half keyboards
- `m4g_bridge`: 15-byte to 8-byte HID report translation + duplicate suppression  
- `m4g_ble`: BLE HID Over GATT Profile (HOGP) implementation
- `m4g_logging`: Persistent boot/crash log storage in NVS with `LOG_AND_SAVE` macro
- `m4g_led`: Status indication
- `m4g_diag`: System diagnostics

**Key Pattern**: Components expose clean C APIs in `include/*.h` and register callbacks with each other (e.g., USB → Bridge → BLE data flow).

## Critical Build/Debug Workflows

### ESP-IDF Commands (Always use these, never generic cmake)
```bash
# Target must be set first for ESP32-S3
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Configuration Management
- `sdkconfig.defaults`: Base config optimized for USB host + BLE HID + Windows compatibility
- `Kconfig`: Project-specific options surfaced at the top level (`M4G Bridge Options` menu)
- Use `idf.py menuconfig` to modify, never edit `sdkconfig` directly

### Debug Logging Control
```c
// In main.c - toggle these for different subsystems
m4g_log_enable_usb(true);    // USB enumeration/reports
m4g_log_enable_keypress(true); // Key translation 
m4g_log_enable_ble(false);    // BLE stack (very verbose)
```

## Project-Specific Conventions

### Error Handling Pattern
```c
if (m4g_component_init() != ESP_OK)
    LOG_AND_SAVE(true, E, TAG, "Component init failed");
```
Use `LOG_AND_SAVE` for persistent logging, not bare `ESP_LOGE`.

### Component Initialization Order (critical)
```c
// From main.c - this order matters for dependencies
m4g_led_init();      // First - provides status during init
m4g_ble_init();      // Before bridge - BLE stack startup
m4g_bridge_init();   // Before USB - registers callback
m4g_usb_init();      // Last - starts enumeration
```

### HID Report Processing Chain
1. USB delivers raw 15-byte reports via callback to `m4g_bridge_process_usb_report()`
2. Bridge extracts keys, handles arrow→mouse mapping, builds 8-byte HID report
3. Duplicate suppression via `CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION`
4. BLE transmission via `m4g_ble_send_keyboard_report()`

### Memory & Task Management
- Main task stack: 16384 bytes (for USB complexity)
- USB enumeration timeouts optimized for hubs: 1000ms debounce
- Components create their own tasks internally - don't spawn additional tasks

## Configuration Symbols (Key Ones)
- `CONFIG_M4G_BOARD_DEVKIT` vs `CONFIG_M4G_BOARD_QTPY`: Affects log persistence
- `CONFIG_M4G_ENABLE_ARROW_MOUSE`: Arrow keys → mouse movement translation
- `CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION`: BLE bandwidth optimization
- `CONFIG_M4G_LOG_PERSISTENCE`: NVS log storage (board-dependent default)

## Hardware Integration Notes
- **ESP32-S3 USB OTG**: Native USB host, no external controller needed
- **CharaChorder Specific**: Dual ESP32-S3 halves appear as USB hub with 2 HID devices
- **BLE Compatibility**: Configured for Windows 10/11 (no BLE 5.0 features, legacy pairing only)

## Testing & Validation
- Monitor USB enumeration: Enable USB logging and watch for device detection
- Verify BLE reports: Use Windows Bluetooth troubleshooter or BLE scanner apps  
- Check persistent logs: `m4g_log_dump_and_clear()` shows boot/crash history from NVS
- Stack watermarks: Configure `CONFIG_M4G_STACK_WATERMARK_PERIOD_MS` for memory debugging

## Future Platform Plans
See `docs/nrf52840_max3421e_plan.md` for planned nRF52840+MAX3421E port using platform abstraction layer. Current code is ESP32-S3 specific but designed for eventual multi-platform support.

## Common Gotchas
- **USB enumeration fails**: Check `sdkconfig.defaults` for proper USB host timeouts
- **BLE won't pair**: Verify legacy pairing mode, disable BLE 5.0 features in config
- **Missing logs**: Ensure NVS init before `m4g_logging_set_nvs_ready()`
- **Component build errors**: Use `idf.py build` not cmake directly
- **Stack overflow**: Increase main task stack if adding complexity to main loop