# M4G Platform Abstraction Layer

This directory contains the platform abstraction layer (PAL) for the M4G BLE Bridge project, enabling support for multiple hardware platforms.

## Supported Platforms

### ESP32-S3 ✅ **(Current Implementation)**
- **Status**: Fully implemented
- **Hardware**: ESP32-S3 with native USB OTG host
- **BLE Stack**: NimBLE (ESP-IDF)
- **USB Stack**: Native ESP-IDF USB Host
- **Build System**: ESP-IDF (CMake)

### nRF52840 + MAX3421E 🚧 **(Future Implementation)**
- **Status**: Stub/placeholder only
- **Hardware**: nRF52840 SoC + MAX3421E USB host controller
- **BLE Stack**: Zephyr Bluetooth stack
- **USB Stack**: Custom MAX3421E driver
- **Build System**: Zephyr (CMake + West)

## Architecture

The platform abstraction provides a unified API that allows the core M4G bridge logic to run unchanged across different platforms:

```
┌─────────────────────────────────────┐
│           Core Application          │
│        (main.c, m4g_bridge)        │
└─────────────────────────────────────┘
                    │
┌─────────────────────────────────────┐
│      Platform Abstraction API      │
│         (m4g_platform.h)           │
└─────────────────────────────────────┘
                    │
    ┌───────────────┴───────────────┐
    │                               │
┌───▼────┐                    ┌─────▼─────┐
│ESP32-S3│                    │nRF52840+  │
│Platform│                    │MAX3421E   │
│Adapter │                    │Platform   │
│        │                    │Adapter    │
└────────┘                    └───────────┘
```

## Platform API

The platform abstraction provides these key interfaces:

### System Lifecycle
- `m4g_platform_init()` - Initialize platform hardware
- `m4g_platform_millis()` - Get milliseconds since boot
- `m4g_platform_delay_ms()` - Delay execution

### USB Host Abstraction
- `m4g_platform_usb_init()` - Initialize USB host functionality
- `m4g_platform_usb_poll()` - Poll for USB events (for MAX3421E)
- `m4g_platform_usb_is_connected()` - Check USB device connectivity

### BLE Abstraction
- `m4g_platform_ble_init()` - Initialize BLE stack
- `m4g_platform_ble_send_keyboard_report()` - Send keyboard HID report
- `m4g_platform_ble_send_mouse_report()` - Send mouse HID report

### LED & Power Management
- `m4g_platform_led_set_state()` - Set status LED
- `m4g_power_enter_light_sleep()` - Enter low-power mode

### Storage & Logging
- `m4g_nvs_*()` - Non-volatile storage operations
- `m4g_log()` - Unified logging interface

## Adding a New Platform

To add support for a new platform:

1. **Create platform directory**: `platform/new_platform/`

2. **Implement platform header**: `m4g_platform_newplatform.h`
   ```c
   typedef struct {
       // Platform-specific configuration
   } m4g_platform_config_t;
   ```

3. **Implement platform adapter**: `m4g_platform_newplatform.c`
   - Implement all functions from `m4g_platform.h`
   - Map to platform-specific APIs

4. **Add build integration**: `CMakeLists.txt` or equivalent

5. **Update main Kconfig**: Add platform selection option

6. **Test and validate**: Ensure all features work correctly

## Configuration

Platform selection is controlled via Kconfig:

```kconfig
choice M4G_TARGET
    prompt "Target Platform"
    default M4G_TARGET_ESP32S3

config M4G_TARGET_ESP32S3
    bool "ESP32-S3 (native host)"

config M4G_TARGET_NRF52840  
    bool "nRF52840 + MAX3421E"
endchoice
```

## Build Process

The platform abstraction is automatically included in the build:

1. **Configuration**: Selected via `idf.py menuconfig` → Platform Configuration
2. **Compilation**: Platform-specific sources are compiled based on config
3. **Linking**: Only the selected platform adapter is linked

## Testing

Each platform should support:
- [ ] Basic USB keyboard connectivity
- [ ] CharaChorder MasterForge dual-device detection  
- [ ] BLE HID keyboard functionality
- [ ] BLE HID mouse functionality (optional)
- [ ] LED status indication
- [ ] Power management features
- [ ] Non-volatile configuration storage

---

*The platform abstraction layer enables the M4G Bridge to scale beyond ESP32-S3 while maintaining compatibility and code reuse.*