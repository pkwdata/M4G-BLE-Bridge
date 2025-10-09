# LED Status Indicators

The M4G BLE Bridge uses a single RGB NeoPixel LED to indicate connection status. The LED behavior differs between firmware variants.

## STANDALONE Mode

**Single keyboard connected directly via USB → Bluetooth to computer**

| Color | Connections | Meaning |
|-------|-------------|---------|
| 🔴 **RED** (solid) | None | No USB keyboard or Bluetooth connection |
| 🔴 **RED** → 🔵 **BLUE** (flashing) | None, BLE advertising | Waiting for computer to pair via Bluetooth |
| 🟢 **GREEN** (solid) | USB only, not advertising | USB keyboard connected, BLE not started |
| 🟢 **GREEN** → 🔵 **BLUE** (flashing) | USB only, BLE advertising | USB ready, waiting for computer to pair |
| 🟡 **YELLOW** | BLE only | Bluetooth paired but no USB keyboard |
| 🔵 **BLUE** (solid) | USB + BLE | ✅ Fully operational (keyboard → BLE → computer) |

**Flashing Pattern:** When BLE is advertising (looking for connection), the LED flashes between the base state (RED/GREEN) and BLUE every 500ms. Once paired, it becomes solid BLUE.

---

## LEFT Side (Split Keyboard)

**Receives from local USB + remote ESP-NOW → Transmits via Bluetooth**

| Color | Local USB | ESP-NOW (Right) | Bluetooth | Meaning |
|-------|-----------|-----------------|-----------|---------|
| 🔴 **RED** (solid) | ❌ | ❌ | ❌ | No connections |
| 🔴 **RED** → 🔵 **BLUE** (flashing) | ❌ | ❌ | Advertising | Waiting for connections and BLE pairing |
| 🟣 **MAGENTA** (solid) | ✅ | ❌ | ❌ | Left keyboard only |
| 🟣 **MAGENTA** → 🔵 **BLUE** (flashing) | ✅ | ❌ | Advertising | Left USB active, advertising BLE (no right half) |
| 🟣 **MAGENTA** (solid) | ✅ | ❌ | ✅ | Left keyboard + BLE (waiting for right half) |
| 🟢 **GREEN** (solid) | ✅ | ✅ | ❌ | Both keyboard halves connected (not advertising) |
| 🟢 **GREEN** → 🔵 **BLUE** (flashing) | ✅ | ✅ | Advertising | Both halves ready, waiting for BLE pairing |
| 🔵 **BLUE** (solid) | ✅ | ✅ | ✅ | ✅ Fully operational (both halves → BLE → computer) |

**Key insight:** MAGENTA = left half only. GREEN = both halves. BLUE flashing = actively advertising. Solid BLUE = everything connected!

---

## RIGHT Side (Split Keyboard)

**Receives from local USB → Transmits via ESP-NOW to LEFT**

| Color | USB | Meaning |
|-------|-----|---------|
| 🔴 **RED** | ❌ | No USB keyboard connected |
| 🟢 **GREEN** | ✅ | USB keyboard connected (relaying to LEFT via ESP-NOW) |

*Note: RIGHT side doesn't have Bluetooth, so it's simpler. It just shows local USB status.*

---

## LED Detection

The LED GPIO is auto-detected on first boot:
- **ESP32-S3 DevKit**: GPIO 48 (data)
- **Adafruit QT Py ESP32-S3**: GPIO 39 (data) + GPIO 38 (power enable)

The detected configuration is saved to NVS for faster subsequent boots.

### Manual LED GPIO Reset

If LED detection fails or you want to force re-detection:

```bash
# Via serial console (if available)
m4g_led_clear_stored_gpio()

# Via NVS erase
esptool.py --chip esp32s3 erase_region 0x9000 0x6000
```

---

## Brightness

Default brightness: `30` (on scale of 0-255)  
Adjustable via `LED_BRIGHTNESS` in `components/m4g_led/m4g_led.c`

Lower brightness saves power and is easier on the eyes in dark environments.
