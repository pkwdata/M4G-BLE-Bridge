# LED GPIO Auto-Detection

## Overview

The M4G BLE Bridge now automatically detects which GPIO pin the status LED is connected to, eliminating the need to manually configure different board types.

## How It Works

### First Boot (Auto-Detection)

On the first boot, the firmware:

1. **Tests common GPIO pins** sequentially:
   - GPIO 48: ESP32-S3-DevKitC-1 NeoPixel
   - GPIO 38: Adafruit QT Py ESP32-S3 NeoPixel
   - GPIO 2: Alternative common pin
   - GPIO 8: Another common pin

2. **Attempts to initialize** a NeoPixel on each pin

3. **Tests the LED** with a brief green pulse (50ms)

4. **Stores the detected GPIO** in NVS (Non-Volatile Storage)

5. **Logs the result** to serial output

### Subsequent Boots (Cached)

On subsequent boots:
- Reads the stored GPIO from NVS
- Skips auto-detection
- Uses the cached value immediately

This makes boot faster and prevents unnecessary GPIO probing.

## Benefits

✅ **Works on multiple boards** without recompilation
✅ **No manual configuration** needed
✅ **Automatic board detection**
✅ **Fast subsequent boots** (cached result)
✅ **Easy to deploy** - same firmware works everywhere

## Supported Boards

- **ESP32-S3-DevKitC-1** (GPIO 48)
- **Adafruit QT Py ESP32-S3** (GPIO 38)
- **Generic ESP32-S3 boards** (GPIO 2, 8, or custom)

## API Functions

### Initialization
```c
esp_err_t m4g_led_init(void);
```
Initializes the LED subsystem. Auto-detects GPIO on first boot or uses cached value.

### Clear Stored GPIO
```c
esp_err_t m4g_led_clear_stored_gpio(void);
```
Clears the stored LED GPIO from NVS, forcing re-detection on next boot.

**Use cases:**
- Moving firmware to a different board
- Testing auto-detection
- Recovering from incorrect detection

### Connection Status
```c
void m4g_led_set_usb_connected(bool connected);
void m4g_led_set_ble_connected(bool connected);
```
Updates LED color based on connection state.

### Manual Color Control
```c
void m4g_led_force_color(uint8_t r, uint8_t g, uint8_t b);
```
Manually set LED color (bypasses auto-state management).

## LED Status Colors

| State | Color | Meaning |
|-------|-------|---------|
| No connections | 🔴 RED | Waiting for USB keyboard and BLE pairing |
| USB only | 🟢 GREEN | USB keyboard connected, waiting for BLE |
| BLE only | 🟡 YELLOW | BLE paired, waiting for USB keyboard |
| Both connected | 🔵 BLUE | Fully operational (USB + BLE) |

## Serial Output Examples

### First Boot (Auto-Detection)
```
I (1234) M4G-LED: Auto-detecting LED GPIO...
D (1235) M4G-LED: Testing GPIO 48
I (1240) M4G-LED: ✓ Detected LED on GPIO 48
I (1241) M4G-LED: Saved LED GPIO 48 to NVS
I (1242) M4G-LED: ✓ LED initialized successfully on GPIO 48
```

### Subsequent Boots (Cached)
```
I (1234) M4G-LED: Using stored LED GPIO 48 from NVS
I (1235) M4G-LED: ✓ LED initialized successfully on GPIO 48
```

### No LED Detected (Fallback)
```
I (1234) M4G-LED: Auto-detecting LED GPIO...
D (1235) M4G-LED: Testing GPIO 48
D (1236) M4G-LED: Testing GPIO 38
D (1237) M4G-LED: Testing GPIO 2
D (1238) M4G-LED: Testing GPIO 8
W (1239) M4G-LED: No LED detected, using Kconfig default GPIO 48
```

## NVS Storage Details

- **Namespace**: `m4g_board`
- **Key**: `led_gpio`
- **Type**: `int32_t`
- **Persistence**: Survives reboots and firmware updates

## Troubleshooting

### LED not working after board change

**Problem**: Firmware works on one board but LED doesn't work after moving to different board.

**Solution**: Clear stored GPIO to force re-detection:
```c
m4g_led_clear_stored_gpio();
esp_restart();
```

Or via serial console command (if implemented):
```
led_reset
reboot
```

### Wrong LED detected

**Problem**: Auto-detection chose the wrong GPIO.

**Solution**:
1. Check if the LED is actually connected to a standard pin
2. Clear stored GPIO and reboot to re-detect
3. If needed, manually set Kconfig value and disable auto-detection

### LED detection takes too long

**Problem**: First boot seems slow.

**Solution**: This is normal behavior - auto-detection tests multiple pins sequentially. Subsequent boots are fast (uses cached value).

## Disabling Auto-Detection

If you want to use compile-time configuration instead:

1. Set the LED GPIO in `Kconfig`:
   ```
   CONFIG_M4G_LED_DATA_GPIO=48
   ```

2. Comment out the auto-detection in `m4g_led.c`:
   ```c
   // int led_gpio = get_led_gpio();  // Auto-detect
   int led_gpio = CONFIG_M4G_LED_DATA_GPIO;  // Use Kconfig value
   ```

## Adding Custom GPIO Pins

To add support for a new board with a different LED GPIO:

Edit `m4g_led.c` and add the GPIO to the candidate list:

```c
const int candidate_pins[] = {
    48,   // ESP32-S3-DevKitC-1
    38,   // Adafruit QT Py ESP32-S3
    2,    // Generic board option 1
    8,    // Generic board option 2
    YOUR_PIN_HERE  // Your custom board
};
```

## Implementation Details

- **Detection method**: Software-only (no hardware strapping required)
- **Test pattern**: Brief green pulse (5/255 brightness, 50ms)
- **Fallback**: Uses Kconfig default if no LED detected
- **Performance**: <1 second on first boot, instant on subsequent boots
- **Memory**: 4 bytes NVS storage
- **Dependencies**: `nvs_flash`, `driver`, `m4g_logging`

## Future Enhancements

Potential improvements for future versions:

- [ ] Support for non-NeoPixel LEDs (simple GPIO)
- [ ] Web interface to manually set LED GPIO
- [ ] Support for multiple LEDs
- [ ] LED power GPIO auto-detection
- [ ] Brightness auto-adjustment based on ambient light

---

**Last Updated**: October 8, 2025  
**Version**: 1.0.0
