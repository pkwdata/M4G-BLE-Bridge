# M4G BLE Bridge - Development Roadmap

**Last Updated:** 2025-01-XX  
**Status:** Planning Phase

This document outlines the comprehensive development plan for the next phase of the M4G BLE Bridge project, organized by priority and dependencies.

---

## Table of Contents

1. [Phase 1: Power Management & Battery System](#phase-1-power-management--battery-system)
2. [Phase 2: BLE Security & Multi-Device Support](#phase-2-ble-security--multi-device-support)
3. [Phase 3: LED Control API & Effects](#phase-3-led-control-api--effects)
4. [Phase 4: Trackball Mouse Integration](#phase-4-trackball-mouse-integration)
5. [Phase 5: Firmware Management & OTA Updates](#phase-5-firmware-management--ota-updates)
6. [Phase 6: Multi-Platform Support](#phase-6-multi-platform-support)
7. [Phase 7: Documentation & User Experience](#phase-7-documentation--user-experience)

---

## Phase 1: Power Management & Battery System

**Priority:** HIGH  
**Complexity:** HIGH  
**Estimated Duration:** 4-6 weeks

### Objectives

Enable wireless operation by integrating battery power with intelligent power management for both the ESP32-S3 bridge and connected USB devices.

### Requirements

#### Power Supply Specifications
- **USB Device Power:** 5V @ 3A (CharaChorder dual halves)
- **ESP32-S3 Power:** 3.3V @ 0.5A
- **Separate Power Rails:** Bridge and USB devices powered independently
- **Form Factor:** Small, integrated solution suitable for portable use

#### Key Features

1. **Battery Selection**
   - LiPo 18650 cells (high capacity, compact)
   - Minimum 3500mAh capacity per cell
   - 2S configuration (7.4V nominal) for sufficient voltage headroom
   - Protection circuitry integrated

2. **Power Distribution**
   - **5V Rail (USB):** Boost converter from battery (3A capable)
     - Suggested: TPS61088 or similar high-current boost controller
     - Efficiency target: >90% at full load
   - **3.3V Rail (ESP32-S3):** LDO or buck converter
     - Suggested: AP2112K-3.3 (low quiescent current) or TPS62840
     - Low-power modes supported

3. **Charging System**
   - USB-C PD input (5V/9V/12V)
   - TP4056 or BQ25895 charging IC
   - Balance charging for 2S configuration
   - Status LEDs (charging/full/error)
   - Over-voltage, under-voltage, over-current protection
   - Temperature monitoring

4. **Power Management Features**
   - **Fuel Gauge:** MAX17048 or similar (I2C interface)
   - **Load Switching:** Power gating for USB devices when idle
   - **Sleep Modes:** 
     - Deep sleep when USB keyboard disconnected
     - Light sleep with BLE advertisement only
     - Active mode with full functionality
   - **Wake Sources:**
     - USB device connection
     - BLE connection request
     - Timer-based wake for periodic checks

5. **Safety Features**
   - Battery protection IC (DW01 + FS8205A or equivalent)
   - Thermal monitoring and shutdown
   - Reverse polarity protection
   - Short circuit protection
   - Cell balancing for multi-cell configurations

#### Board-Specific Solutions

##### ESP32-S3 DevKit Custom PCB
- Integrate battery holder for 2x 18650 cells
- Mount all power ICs on compact PCB (50mm x 70mm target)
- Castellated holes for easy integration with DevKit
- Test points for debugging

##### Adafruit Qt Py ESP32-S3
- Ultra-compact design
- Single 18650 cell with boost converter
- Minimize component count
- Utilize Qt Py's built-in battery connector if available
- 3D-printed battery case attachment

##### nRF52840 + MAX3421E Board
- Custom PCB with integrated battery management
- Efficient power routing for both nRF52840 and MAX3421E
- Consider smaller LiPo pouch cells for ultra-compact design
- Target: <40mm x 60mm PCB

#### Software Components

1. **Battery Monitoring Component** (`m4g_power`)
   ```c
   // Power management API
   esp_err_t m4g_power_init(void);
   uint8_t m4g_power_get_battery_percentage(void);
   uint16_t m4g_power_get_battery_voltage_mv(void);
   bool m4g_power_is_charging(void);
   void m4g_power_enter_sleep_mode(m4g_sleep_mode_t mode);
   void m4g_power_enable_usb_power(bool enable);
   ```

2. **Power State Machine**
   - ACTIVE: Full power, keyboard connected
   - IDLE: BLE advertising, keyboard disconnected, USB power off
   - SLEEP: Deep sleep, periodic wake
   - CHARGING: Battery charging, limited functionality

3. **NVS Power Settings**
   - Auto-sleep timeout
   - Low battery warning threshold
   - USB power auto-cutoff timer

#### Deliverables

- [ ] Hardware schematic and PCB design (KiCad)
- [ ] Bill of Materials (BOM) with cost analysis
- [ ] Power management component implementation
- [ ] Battery monitoring and fuel gauge integration
- [ ] Sleep mode implementation with wake sources
- [ ] Power state LED indicators
- [ ] Testing and validation (efficiency, runtime, safety)
- [ ] User documentation for battery operation

---

## Phase 2: BLE Security & Multi-Device Support

**Priority:** HIGH  
**Complexity:** MEDIUM  
**Estimated Duration:** 3-4 weeks

### Objectives

Enhance BLE security with encryption and enable pairing with multiple devices with quick device switching.

### BLE Security Enhancements

#### Current State Analysis
- Basic pairing with `BLE_SM_IO_CAP_NO_IO`
- Bonding enabled (`sm_bonding = 1`)
- No MITM protection (`sm_mitm = 0`)
- No Secure Connections (`sm_sc = 0`)

#### Security Improvements

1. **Enable Secure Connections (BLE 4.2+)**
   ```c
   ble_hs_cfg.sm_sc = 1;  // Enable Secure Connections
   ```
   - ECDH key exchange
   - Stronger encryption (AES-CCM)
   - Passive eavesdropping protection

2. **Enhanced Pairing Options**
   - Support for Just Works pairing (current)
   - Optional passkey display (6-digit code)
   - Out-of-band (OOB) pairing for advanced users

3. **Encryption Requirements**
   - Enforce encryption for all connections
   - Reject unencrypted connections after pairing
   - Automatic re-encryption on reconnection

4. **Privacy Features**
   - Resolvable Private Addresses (RPA)
   - Address rotation every 15 minutes
   - IRK (Identity Resolving Key) storage in NVS

5. **Security Logging**
   - Log pairing attempts and failures
   - Track connection security level
   - Alert on security downgrades

#### Multi-Device Pairing

1. **Bond Storage**
   - Store up to 8 paired devices in NVS
   - Track most recently used device
   - Priority-based connection order

2. **Device Switching Mechanism**
   - **Method 1: Triple-tap button**
     - GPIO button input
     - Cycle through bonded devices
     - LED feedback for current device
   
   - **Method 2: BLE characteristic**
     - Control characteristic for device selection
     - Mobile app or custom tool support
   
   - **Method 3: Keyboard shortcut**
     - Fn + F1/F2/F3 for device slots
     - Not consumed by target device

3. **Connection Management**
   - Disconnect from current device
   - Start directed advertising to next device
   - Fall back to general advertising if no response
   - Auto-reconnect to last connected device on boot

4. **Multi-Device API**
   ```c
   // Multi-device pairing API
   esp_err_t m4g_ble_get_bonded_devices(m4g_bonded_device_t *devices, uint8_t *count);
   esp_err_t m4g_ble_switch_to_device(uint8_t device_index);
   esp_err_t m4g_ble_forget_device(uint8_t device_index);
   esp_err_t m4g_ble_clear_all_bonds(void);
   uint8_t m4g_ble_get_active_device_index(void);
   ```

5. **UI/UX Enhancements**
   - LED color codes for each device slot
   - Voice feedback (optional, via BLE service)
   - Configuration app for device naming

#### Deliverables

- [ ] Implement Secure Connections (sm_sc = 1)
- [ ] Add privacy features (RPA)
- [ ] Implement bond storage and management
- [ ] Add device switching mechanism (button + BLE control)
- [ ] Create multi-device pairing component
- [ ] Add security logging and monitoring
- [ ] Update Kconfig options for security settings
- [ ] Documentation and user guide for multi-device use

---

## Phase 3: LED Control API & Effects

**Priority:** MEDIUM  
**Complexity:** MEDIUM  
**Estimated Duration:** 2-3 weeks

### Objectives

Create a comprehensive LED control API for both the bridge status LED and the CharaChorder keyboard half LEDs, with support for dynamic effects.

### Current LED Implementation

- **Bridge LED:** Simple RGB status indicator (USB/BLE state)
- **Keyboard LEDs:** 4 LEDs per CharaChorder half (currently uncontrolled)

### LED Control Architecture

#### 1. Bridge LED Effects

**Status Indicators (Current)**
- Yellow: BLE only (no USB)
- Blue: USB + BLE connected
- Red: Error state
- Green: Charging
- Off: No power

**New Effects**
- Breathing: Slow pulse (idle/sleeping)
- Rapid blink: Pairing mode
- Rainbow cycle: Device switching mode
- Custom colors: User-configurable via NVS

#### 2. CharaChorder Half LEDs

**Hardware Communication**
- Use USB HID Feature Reports or Output Reports
- Send LED commands through existing USB connection
- Protocol reverse-engineering required

**LED Effects**
- Static color per half
- Typing indicators (reactive)
- Battery level display
- Chord quality feedback (based on deviation tracking)
- Custom animations

#### 3. LED Control API

```c
// m4g_led_api.h

typedef enum {
    M4G_LED_EFFECT_STATIC,
    M4G_LED_EFFECT_BREATHING,
    M4G_LED_EFFECT_BLINK,
    M4G_LED_EFFECT_RAINBOW,
    M4G_LED_EFFECT_TYPING_REACTIVE,
    M4G_LED_EFFECT_BATTERY_LEVEL,
    M4G_LED_EFFECT_CHORD_QUALITY,
    M4G_LED_EFFECT_CUSTOM
} m4g_led_effect_t;

typedef struct {
    uint8_t r, g, b;
    uint8_t brightness;  // 0-255
    uint16_t speed;      // Effect speed (ms)
} m4g_led_config_t;

// Bridge LED API
esp_err_t m4g_led_set_effect(m4g_led_effect_t effect, const m4g_led_config_t *config);
esp_err_t m4g_led_set_custom_animation(const m4g_led_frame_t *frames, uint8_t frame_count);

// CharaChorder Half LEDs API
esp_err_t m4g_keyboard_led_set_effect(uint8_t half_index, m4g_led_effect_t effect, const m4g_led_config_t *config);
esp_err_t m4g_keyboard_led_set_color(uint8_t half_index, uint8_t led_index, uint8_t r, uint8_t g, uint8_t b);
esp_err_t m4g_keyboard_led_set_brightness(uint8_t half_index, uint8_t brightness);
```

#### 4. BLE GATT Service for LED Control

Create a custom LED control service:
```
Service UUID: 0xFFF0 (LED Control Service)
  Characteristic UUID: 0xFFF1 (Bridge LED Control) - Read/Write
  Characteristic UUID: 0xFFF2 (Keyboard LED Control) - Write
  Characteristic UUID: 0xFFF3 (LED Effect Select) - Write
```

**Message Format:**
```c
typedef struct __attribute__((packed)) {
    uint8_t target;       // 0=bridge, 1=left half, 2=right half
    uint8_t effect;       // m4g_led_effect_t
    uint8_t r, g, b;      // RGB values
    uint8_t brightness;   // 0-255
    uint16_t speed;       // Effect speed (ms)
} ble_led_control_msg_t;
```

#### 5. LED Effects Engine

**Component:** `m4g_led_effects`

**Features:**
- Background task for smooth animations
- Frame interpolation for smooth transitions
- Low power mode (reduce brightness/disable when battery low)
- Synchronization between bridge and keyboard LEDs

**Effect Examples:**

1. **Typing Reactive:**
   - Flash LED on each keypress
   - Color intensity based on typing speed
   - Fade out after configurable delay

2. **Battery Level:**
   - Green: >50%
   - Yellow: 20-50%
   - Red: <20%
   - Flashing red: <5%

3. **Chord Quality:**
   - Perfect timing: Green flash
   - Good timing: Blue flash
   - Poor timing: Yellow flash
   - Uses existing deviation tracking

#### 6. Configuration Storage

**NVS Settings:**
```c
typedef struct {
    m4g_led_effect_t bridge_effect;
    m4g_led_config_t bridge_config;
    m4g_led_effect_t keyboard_effect;
    m4g_led_config_t keyboard_config;
    bool sync_leds;  // Sync bridge and keyboard effects
    bool low_power_mode;  // Reduce brightness on battery
} m4g_led_preferences_t;
```

#### Deliverables

- [ ] Enhanced m4g_led component with effects engine
- [ ] LED control API implementation
- [ ] BLE GATT service for LED control
- [ ] CharaChorder USB LED command protocol
- [ ] Example effects (breathing, rainbow, reactive)
- [ ] Configuration app or web interface
- [ ] NVS storage for LED preferences
- [ ] Documentation and API reference

---

## Phase 4: Trackball Mouse Integration

**Priority:** MEDIUM  
**Complexity:** MEDIUM-HIGH  
**Estimated Duration:** 3-4 weeks

### Objectives

Add support for an integrated trackball mouse module with smooth cursor control and button support.

### Hardware Options

#### Option 1: PMW3360 Optical Sensor
- High precision (up to 12000 DPI)
- SPI interface
- Widely available, well-documented
- Used in many gaming mice

#### Option 2: I2C Trackball Modules
- Adafruit Trackball Breakout (ATTINY84-based)
- Smaller form factor
- I2C interface (simpler integration)
- Built-in RGB LED

#### Option 3: ADNS-9800 Laser Sensor
- Very high precision
- SPI interface
- More expensive
- Excellent for low-speed precision

### Recommended: PMW3360 + Custom Trackball

**Rationale:**
- Best balance of performance and cost
- Excellent documentation and Arduino libraries
- Can be adapted to ESP-IDF
- Standard 34mm trackball size

### Hardware Integration

#### Connections (ESP32-S3 DevKit)
```
PMW3360 Module:
  - MOSI -> GPIO 11
  - MISO -> GPIO 13
  - SCK  -> GPIO 12
  - CS   -> GPIO 10
  - MOT  -> GPIO 9 (motion interrupt)

Mouse Buttons (3 buttons):
  - LEFT   -> GPIO 14
  - RIGHT  -> GPIO 21
  - MIDDLE -> GPIO 47
```

#### Physical Design
- Mount trackball module on top/side of bridge enclosure
- 3D-printed housing for trackball and buttons
- Ergonomic placement for thumb operation
- Optional: Removable trackball for cleaning

### Software Implementation

#### 1. PMW3360 Driver Component (`m4g_trackball`)

```c
// m4g_trackball.h

typedef struct {
    int16_t delta_x;
    int16_t delta_y;
    uint8_t buttons;      // Bit 0=left, 1=right, 2=middle
    bool motion_detected;
} m4g_trackball_report_t;

esp_err_t m4g_trackball_init(void);
esp_err_t m4g_trackball_get_report(m4g_trackball_report_t *report);
esp_err_t m4g_trackball_set_dpi(uint16_t dpi);
uint16_t m4g_trackball_get_dpi(void);
void m4g_trackball_reset(void);
```

#### 2. Integration with Bridge

**Merge trackball data with USB mouse data:**
- Combine trackball movement with arrow key mouse emulation
- Priority system (trackball overrides arrow keys)
- Smooth acceleration curves

**Update `m4g_bridge`:**
```c
static void merge_mouse_reports(int16_t usb_dx, int16_t usb_dy, uint8_t usb_buttons,
                                int16_t trackball_dx, int16_t trackball_dy, uint8_t trackball_buttons,
                                int16_t *final_dx, int16_t *final_dy, uint8_t *final_buttons) {
    // Priority: Physical trackball > USB mouse > Arrow keys
    if (trackball_dx != 0 || trackball_dy != 0) {
        *final_dx = trackball_dx;
        *final_dy = trackball_dy;
    } else {
        *final_dx = usb_dx;
        *final_dy = usb_dy;
    }
    *final_buttons = trackball_buttons | usb_buttons;  // Combine buttons
}
```

#### 3. Configuration Options

**DPI Settings:**
- Low: 400 DPI (precision work)
- Medium: 800 DPI (general use)
- High: 1600 DPI (fast cursor movement)
- Adjustable via button combo or BLE characteristic

**Acceleration Curves:**
- Linear: No acceleration
- Moderate: 1.5x at high speed
- Aggressive: 3x at high speed

**Button Mapping:**
- Default: Standard L/R/M buttons
- Custom: Configurable via NVS (e.g., middle = back button)

#### 4. Power Management

- Disable trackball sensor in deep sleep
- Low-power polling mode when idle
- Motion interrupt for instant wake

#### 5. NVS Settings

```c
typedef struct {
    uint16_t dpi;
    uint8_t acceleration_curve;  // 0=off, 1=moderate, 2=aggressive
    uint8_t button_map[3];       // Left, right, middle
    bool invert_x;
    bool invert_y;
    bool swap_xy;
} m4g_trackball_config_t;
```

#### Deliverables

- [ ] PMW3360 SPI driver implementation
- [ ] Button input handling (debouncing)
- [ ] m4g_trackball component
- [ ] Integration with m4g_bridge mouse reporting
- [ ] DPI switching and configuration
- [ ] Acceleration curves
- [ ] 3D-printable trackball housing design
- [ ] PCB design for trackball module mount
- [ ] Documentation and assembly guide

---

## Phase 5: Firmware Management & OTA Updates

**Priority:** HIGH  
**Complexity:** HIGH  
**Estimated Duration:** 4-5 weeks

### Objectives

Enable wireless firmware updates over BLE and 2.4GHz (future) to simplify field updates and eliminate the need for physical connections.

### OTA Update Methods

#### Method 1: BLE OTA (Primary)

**Advantages:**
- Uses existing BLE connection
- No additional hardware required
- Secure, authenticated updates

**Challenges:**
- Slow transfer speed (~10-15 KB/s typical)
- Large firmware size (~1.5 MB)
- Connection stability during transfer

**Implementation:**

1. **Partition Scheme**
   ```
   # partitions.csv
   # Name,   Type, SubType, Offset,  Size
   nvs,      data, nvs,     0x9000,  0x6000
   phy_init, data, phy,     0xf000,  0x1000
   factory,  app,  factory, 0x10000, 0x180000
   ota_0,    app,  ota_0,   0x190000,0x180000
   ota_1,    app,  ota_1,   0x310000,0x180000
   ota_data, data, ota,     0x490000,0x2000
   ```

2. **BLE OTA Service**
   ```
   Service UUID: 0xFE00 (OTA Update Service)
     Characteristic UUID: 0xFE01 (Control) - Write/Notify
     Characteristic UUID: 0xFE02 (Data) - Write
   ```

3. **OTA Protocol**
   ```c
   typedef enum {
       OTA_CMD_BEGIN,      // Start OTA, specify size and hash
       OTA_CMD_DATA,       // Firmware data chunk
       OTA_CMD_END,        // Finalize and verify
       OTA_CMD_ABORT,      // Cancel OTA
       OTA_CMD_STATUS      // Query OTA status
   } ota_command_t;

   typedef struct __attribute__((packed)) {
       uint8_t command;
       uint32_t offset;
       uint16_t length;
       uint8_t data[512];  // Max BLE MTU ~512 bytes
   } ota_packet_t;
   ```

4. **Security Measures**
   - Firmware signature verification (SHA256 + RSA)
   - Rollback protection
   - Version checking (prevent downgrade)
   - Encrypted transfer (BLE encryption)

5. **Robustness Features**
   - Resume capability (track last written offset)
   - CRC checking per chunk
   - Watchdog timer extension during update
   - Automatic rollback on boot failure

6. **User Experience**
   - LED progress indicator (color gradient)
   - BLE notifications for status
   - Estimated time remaining
   - Battery level check (require >30%)

#### Method 2: 2.4GHz OTA (Future Enhancement)

**For nRF52840 + Custom 2.4GHz Protocol:**

- Much faster transfer (100+ KB/s)
- Dedicated OTA channel
- Less impact on normal operation
- Requires additional hardware (2.4GHz transceiver)

**Implementation deferred to Phase 6**

### OTA Component Architecture

```c
// m4g_ota.h

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR
} m4g_ota_state_t;

typedef struct {
    m4g_ota_state_t state;
    uint32_t total_size;
    uint32_t bytes_received;
    uint8_t progress_percent;
    char error_msg[64];
} m4g_ota_status_t;

esp_err_t m4g_ota_init(void);
esp_err_t m4g_ota_begin(uint32_t firmware_size, const uint8_t *expected_hash);
esp_err_t m4g_ota_write_chunk(uint32_t offset, const uint8_t *data, uint16_t length);
esp_err_t m4g_ota_finalize(void);
esp_err_t m4g_ota_abort(void);
void m4g_ota_get_status(m4g_ota_status_t *status);
```

### Configuration & Management

#### Kconfig Options

```kconfig
config M4G_ENABLE_OTA
    bool "Enable OTA Firmware Updates"
    default y
    help
        Enable over-the-air firmware updates via BLE

config M4G_OTA_REQUIRE_SIGNATURE
    bool "Require firmware signature verification"
    depends on M4G_ENABLE_OTA
    default y
    help
        Only accept signed firmware images

config M4G_OTA_MIN_BATTERY_PERCENT
    int "Minimum battery percentage for OTA"
    depends on M4G_ENABLE_OTA
    range 0 100
    default 30
```

#### Runtime Configuration (NVS)

```c
typedef struct {
    bool auto_update_enabled;
    bool beta_channel;
    uint32_t check_interval_hours;  // 0 = manual only
    char update_server_url[128];    // For future cloud updates
} m4g_ota_config_t;
```

### Update Distribution

#### Option 1: Manual Update (Phase 1)
- User downloads firmware .bin file
- Custom mobile app or web tool uploads via BLE
- Simple, no server infrastructure needed

#### Option 2: Cloud-based Updates (Phase 2)
- Firmware hosted on GitHub releases or custom server
- Bridge periodically checks for updates
- User confirms before download
- Requires WiFi or BLE gateway device

### Testing & Validation

**Test Scenarios:**
1. Successful update with small firmware
2. Successful update with full-size firmware
3. Connection loss during update (resume)
4. Invalid signature rejection
5. Low battery prevention
6. Corrupted firmware detection
7. Rollback on boot failure

**Test Tools:**
- Python script for BLE OTA client
- Firmware generator with test signatures
- Automated test suite

### Deliverables

- [ ] OTA partition table configuration
- [ ] m4g_ota component implementation
- [ ] BLE OTA service and characteristics
- [ ] Firmware signature verification
- [ ] Resume capability after interruption
- [ ] Progress indication and status reporting
- [ ] Battery level checking
- [ ] Python OTA client tool
- [ ] Mobile app integration (future)
- [ ] Documentation and user guide
- [ ] Automated testing suite

---

## Phase 6: Multi-Platform Support

**Priority:** MEDIUM-HIGH  
**Complexity:** VERY HIGH  
**Estimated Duration:** 8-12 weeks

### Objectives

Expand platform support to include Adafruit Qt Py ESP32-S3 and nRF52840 + MAX3421E solutions, utilizing the platform abstraction layer.

### Sub-Phase 6A: Adafruit Qt Py ESP32-S3 Build

**Priority:** HIGH (easier than nRF52840)  
**Duration:** 2-3 weeks

#### Hardware Specifications

**Adafruit Qt Py ESP32-S3:**
- Dual USB-C ports (one for power, one for USB OTG)
- ESP32-S3-MINI-1 module (8MB flash, 2MB PSRAM)
- Ultra-compact (22.86mm x 17.78mm)
- NeoPixel RGB LED onboard
- Stemma QT I2C connector

#### Build Configuration

1. **Board-Specific Kconfig**
   ```kconfig
   config M4G_BOARD_QTPY_S3
       bool "Adafruit Qt Py ESP32-S3"
       select M4G_BOARD_HAS_NEOPIXEL
       select M4G_BOARD_USB_OTG_NATIVE
   ```

2. **Pin Mapping**
   ```c
   // boards/qtpy_esp32s3_pins.h
   #define QTPY_USB_OTG_DP_PIN    19
   #define QTPY_USB_OTG_DM_PIN    20
   #define QTPY_NEOPIXEL_PIN      39
   #define QTPY_BUTTON_PIN        0   // Boot button
   #define QTPY_I2C_SDA_PIN       7   // Stemma QT
   #define QTPY_I2C_SCL_PIN       6   // Stemma QT
   ```

3. **LED Driver Adaptation**
   - Use NeoPixel/WS2812 driver instead of RGB GPIO
   - Map status colors to single NeoPixel
   - Brightness control via PWM

4. **Battery Integration**
   - Utilize Stemma QT for I2C fuel gauge
   - Optional LiPo connector (JST PH 2-pin)
   - Compact boost converter for USB 5V

#### Deliverables

- [ ] Qt Py board configuration in Kconfig
- [ ] Pin definitions header file
- [ ] NeoPixel LED driver integration
- [ ] Custom partition table for 8MB flash
- [ ] Build script/documentation for Qt Py
- [ ] 3D-printable enclosure design
- [ ] Battery integration schematic
- [ ] Testing and validation

### Sub-Phase 6B: nRF52840 + MAX3421E Platform

**Priority:** MEDIUM  
**Duration:** 6-9 weeks  
**Complexity:** Very High (new RTOS, new architecture)

#### Architecture Overview

**Hardware Stack:**
- nRF52840 SoC (Cortex-M4, 1MB flash, 256KB RAM, BLE 5.0)
- MAX3421E USB host controller (SPI interface)
- Optional: 2.4GHz proprietary wireless (nRF ESB protocol)

**Software Stack:**
- Zephyr RTOS (instead of ESP-IDF/FreeRTOS)
- Zephyr BLE stack (SoftDevice alternative)
- Custom MAX3421E USB host driver
- Platform abstraction layer (PAL)

#### Development Phases

**Phase 6B-1: Platform Abstraction Layer (PAL)**

Create platform-neutral core bridge logic:

```c
// include/m4g_platform.h

// Timing
uint32_t m4g_platform_millis(void);
void m4g_platform_delay_ms(uint32_t ms);

// Logging
void m4g_platform_log(int level, const char *tag, const char *fmt, ...);

// BLE
typedef struct m4g_ble_platform_ops {
    esp_err_t (*init)(void);
    bool (*is_connected)(void);
    esp_err_t (*send_keyboard_report)(const uint8_t *report, size_t len);
    esp_err_t (*send_mouse_report)(const uint8_t *report, size_t len);
} m4g_ble_platform_ops_t;

// USB
typedef struct m4g_usb_platform_ops {
    esp_err_t (*init)(void);
    esp_err_t (*poll)(void);  // For interrupt-based USB
} m4g_usb_platform_ops_t;

// Platform registration
void m4g_platform_register_ble_ops(const m4g_ble_platform_ops_t *ops);
void m4g_platform_register_usb_ops(const m4g_usb_platform_ops_t *ops);
```

**Phase 6B-2: ESP32-S3 PAL Implementation**

Refactor existing code to use PAL:
- Move ESP-specific code to `platform/esp32s3/`
- Implement platform ops structures
- Ensure existing functionality unchanged

**Phase 6B-3: nRF52840 BLE Implementation**

Implement BLE HOGP using Zephyr BLE stack:
- HID service registration
- GATT characteristics
- Pairing and bonding
- Report notifications

**Phase 6B-4: MAX3421E USB Host Driver**

Custom USB host implementation:
- SPI communication with MAX3421E
- USB state machine (enumeration, configuration)
- HID-specific protocol handling
- Interrupt endpoint transfers
- Optional: Hub support for dual-half keyboards

**Phase 6B-5: Integration & Testing**

Combine all components on nRF52840 platform:
- Bridge logic using PAL
- End-to-end testing
- Performance optimization

#### Directory Structure (Post-Refactor)

```
M4G-BLE-Bridge/
├── bridge_core/              # Platform-neutral core
│   ├── m4g_bridge.c
│   ├── m4g_descriptor.c
│   └── m4g_stats.c
├── platform/
│   ├── esp32s3/             # ESP32-S3 specific
│   │   ├── platform_impl.c
│   │   ├── ble_impl.c
│   │   ├── usb_impl.c
│   │   └── led_impl.c
│   └── nrf52840/            # nRF52840 specific
│       ├── platform_impl.c
│       ├── ble_impl.c
│       ├── max3421e_spi.c
│       ├── usb_host_hid.c
│       └── led_impl.c
├── include/
│   ├── m4g_platform.h
│   └── m4g_bridge.h
├── boards/                  # Board configurations
│   ├── esp32s3_devkit/
│   ├── qtpy_esp32s3/
│   └── nrf52840_dk/
└── CMakeLists.txt / west.yml
```

#### Deliverables

- [ ] Platform abstraction layer design and API
- [ ] Refactor ESP32-S3 code to use PAL
- [ ] Zephyr project setup for nRF52840
- [ ] MAX3421E SPI driver
- [ ] USB host HID stack for MAX3421E
- [ ] BLE HOGP implementation on Zephyr
- [ ] Integration of bridge logic on nRF52840
- [ ] Testing and debugging
- [ ] Performance benchmarking
- [ ] Documentation for dual-platform build

### Sub-Phase 6C: Battery Solutions for Alternate Boards

**Qt Py Battery Module:**
- Ultra-compact LiPo (500-1000 mAh)
- MCP73831 charger IC
- TPS61090 boost converter (5V @ 3A)
- Stackable PCB design

**nRF52840 Battery Module:**
- Integrated battery holder on custom PCB
- BQ25895 USB-C PD charger
- Dual boost converters (3.3V for nRF, 5V for USB)
- Fuel gauge IC (MAX17048)

**Deliverables:**
- [ ] Qt Py battery PCB schematic
- [ ] nRF52840 battery PCB schematic
- [ ] BOM and cost analysis
- [ ] 3D-printable battery cases
- [ ] Power management firmware
- [ ] Testing and validation

---

## Phase 7: Documentation & User Experience

**Priority:** MEDIUM  
**Complexity:** LOW-MEDIUM  
**Estimated Duration:** 2-3 weeks

### Objectives

Improve documentation, streamline build options, and enhance overall user experience.

### Documentation Cleanup

#### Tasks

1. **Consolidate Existing Documentation**
   - Merge redundant sections in Readme.md
   - Move detailed technical docs to separate files
   - Create clear navigation structure

2. **User Guides**
   - Quick Start Guide (5 minutes to first connection)
   - Build Guide (detailed step-by-step with images)
   - Configuration Guide (Kconfig options explained)
   - Troubleshooting Guide (common issues and solutions)
   - LED Status Reference (quick visual guide)

3. **Developer Documentation**
   - Architecture Overview (updated diagrams)
   - Component API Reference
   - Platform Abstraction Layer Guide
   - Contributing Guidelines
   - Code Style Guide

4. **Hardware Documentation**
   - Schematic diagrams (KiCad)
   - PCB layout guidelines
   - 3D models (STEP files)
   - Assembly instructions
   - BOM with supplier links

5. **Advanced Topics**
   - OTA Update Procedure
   - Multi-Device Pairing Setup
   - LED Effect Programming
   - Battery Calibration
   - Power Optimization Tips

#### Proposed Structure

```
docs/
├── user_guide/
│   ├── quick_start.md
│   ├── build_guide.md
│   ├── configuration.md
│   ├── troubleshooting.md
│   └── led_reference.md
├── developer/
│   ├── architecture.md
│   ├── api_reference.md
│   ├── platform_abstraction.md
│   ├── contributing.md
│   └── code_style.md
├── hardware/
│   ├── schematics/
│   ├── pcb_designs/
│   ├── 3d_models/
│   └── assembly_guides/
└── advanced/
    ├── ota_updates.md
    ├── multi_device_pairing.md
    ├── led_programming.md
    ├── battery_management.md
    └── power_optimization.md
```

### Build Options Streamlining

#### Current Issues
- Too many scattered Kconfig options
- Unclear dependencies
- Redundant settings
- Poor organization

#### Improvements

1. **Reorganize Kconfig Menu**
   ```kconfig
   menu "M4G Bridge Configuration"
       
       menu "Hardware Platform"
           choice M4G_BOARD
               # Board selection
           endchoice
       endmenu
       
       menu "USB Configuration"
           # USB host settings
       endmenu
       
       menu "BLE Configuration"
           # BLE pairing, security, multi-device
       endmenu
       
       menu "Power Management"
           # Battery, sleep modes, charging
       endmenu
       
       menu "LED & Effects"
           # LED control, effects, brightness
       endmenu
       
       menu "Advanced Features"
           # OTA, trackball, logging
       endmenu
       
       menu "Developer Options"
           # Debug logging, testing, diagnostics
       endmenu
   endmenu
   ```

2. **Preset Configurations**
   - `sdkconfig.defaults.devkit` - ESP32-S3 DevKit
   - `sdkconfig.defaults.qtpy` - Qt Py ESP32-S3
   - `sdkconfig.defaults.nrf52840` - nRF52840 board
   - `sdkconfig.defaults.minimal` - Bare minimum features
   - `sdkconfig.defaults.full` - All features enabled

3. **Configuration Validator**
   - Script to check for invalid combinations
   - Warn about performance implications
   - Suggest optimizations

### Runtime Configuration Enhancement

#### Extend NVS Settings System

**Already Implemented (from m4g_settings component):**
- Chord timing settings
- Key repeat settings
- Deviation tracking

**Additional Settings Needed:**
```c
typedef enum {
    // Existing settings
    M4G_SETTING_CHORD_DELAY_MS,
    M4G_SETTING_CHORD_TIMEOUT_MS,
    // ... existing settings ...
    
    // New settings
    M4G_SETTING_BLE_DEVICE_NAME,
    M4G_SETTING_AUTO_SLEEP_TIMEOUT,
    M4G_SETTING_LOW_BATTERY_WARNING,
    M4G_SETTING_LED_BRIGHTNESS,
    M4G_SETTING_LED_EFFECT_BRIDGE,
    M4G_SETTING_LED_EFFECT_KEYBOARD,
    M4G_SETTING_TRACKBALL_DPI,
    M4G_SETTING_TRACKBALL_ACCEL,
    M4G_SETTING_OTA_AUTO_UPDATE,
    M4G_SETTING_USB_POWER_TIMEOUT,
    M4G_SETTING_MAX
} m4g_setting_id_t;
```

#### Configuration Interface Options

1. **BLE Configuration Service**
   - Read/write settings via BLE characteristics
   - Mobile app for configuration
   - Import/export settings profiles

2. **Web Interface (Future)**
   - ESP32-S3 WiFi AP mode
   - Web-based configuration UI
   - Real-time status monitoring

3. **CLI Tool**
   - Serial port configuration utility
   - Batch configuration scripts
   - Factory reset command

### Deliverables

- [ ] Reorganized documentation structure
- [ ] User guides (quick start, build, config, troubleshooting)
- [ ] Developer documentation (architecture, API reference)
- [ ] Hardware documentation (schematics, assembly)
- [ ] Reorganized Kconfig menu with logical grouping
- [ ] Preset configuration files for different boards
- [ ] Configuration validator script
- [ ] Extended NVS settings for all configurable options
- [ ] BLE configuration service
- [ ] CLI configuration tool
- [ ] Updated Readme.md with clear navigation

---

## Implementation Priority Matrix

| Phase | Priority | Complexity | Dependencies | Estimated Duration |
|-------|----------|------------|--------------|-------------------|
| Phase 1: Power & Battery | HIGH | HIGH | None | 4-6 weeks |
| Phase 2: BLE Security & Multi-Device | HIGH | MEDIUM | None | 3-4 weeks |
| Phase 5: OTA Updates | HIGH | HIGH | Phase 1 (battery check) | 4-5 weeks |
| Phase 6A: Qt Py Support | HIGH | MEDIUM | None | 2-3 weeks |
| Phase 3: LED Control API | MEDIUM | MEDIUM | None | 2-3 weeks |
| Phase 4: Trackball | MEDIUM | MEDIUM-HIGH | None | 3-4 weeks |
| Phase 6B: nRF52840 Platform | MEDIUM-HIGH | VERY HIGH | Phase 6A (PAL) | 6-9 weeks |
| Phase 7: Documentation | MEDIUM | LOW-MEDIUM | All phases | 2-3 weeks |

**Recommended Implementation Order:**

1. **Phase 6A** (Qt Py) - Quick win, proves multi-board capability
2. **Phase 2** (BLE Security) - Important security enhancement
3. **Phase 1** (Battery) - Enables wireless operation
4. **Phase 5** (OTA) - Depends on battery, critical for field updates
5. **Phase 3** (LED API) - User experience enhancement
6. **Phase 4** (Trackball) - Hardware add-on, independent development
7. **Phase 6B** (nRF52840) - Large effort, can be parallel with others
8. **Phase 7** (Documentation) - Ongoing, finalize at end

---

## Success Metrics

### Performance Targets
- **Battery Life:** 8+ hours continuous typing with CharaChorder
- **Charging Time:** <3 hours for full charge
- **BLE Latency:** <10ms keystroke to BLE transmission
- **OTA Update Time:** <5 minutes for full firmware
- **Multi-Device Switch Time:** <2 seconds
- **Trackball Precision:** 800 DPI minimum, 0.1mm accuracy

### Quality Targets
- **Uptime:** 99.9% (no crashes during 8-hour typing session)
- **OTA Success Rate:** >99% (with proper battery level)
- **BLE Connection Stability:** >99% (maintain connection for 8 hours)
- **Power Efficiency:** >85% overall system efficiency

### User Experience Targets
- **Setup Time:** <5 minutes from unboxing to first keystroke
- **Configuration Time:** <2 minutes to adjust major settings
- **LED Feedback:** <100ms response time to status changes
- **Documentation Completeness:** 100% of features documented

---

## Risk Assessment & Mitigation

### High-Risk Items

1. **Battery Safety**
   - **Risk:** Fire/explosion from improper charging or protection failure
   - **Mitigation:** Multiple layers of protection (IC, fuse, thermal), extensive testing, certified components

2. **OTA Brick Risk**
   - **Risk:** Device becomes unusable after failed OTA
   - **Mitigation:** Dual partition scheme, automatic rollback, signature verification, battery level check

3. **MAX3421E Complexity**
   - **Risk:** USB host driver development underestimated
   - **Mitigation:** Phase approach, start with single device, extensive reference code, budget extra time

4. **BLE Security Vulnerabilities**
   - **Risk:** Eavesdropping or unauthorized access
   - **Mitigation:** Secure Connections, encryption enforcement, regular security audits

### Medium-Risk Items

1. **Component Availability**
   - **Risk:** Supply chain issues for specialized ICs
   - **Mitigation:** Multiple supplier options, alternative component selection

2. **Power Efficiency**
   - **Risk:** Battery life below targets
   - **Mitigation:** Early prototyping, power profiling, optimization iterations

3. **Platform Abstraction Complexity**
   - **Risk:** PAL design requires major refactoring
   - **Mitigation:** Incremental refactoring, maintain ESP32-S3 build throughout

---

## Resource Requirements

### Hardware Development Tools
- Logic analyzer (USB protocol debugging)
- Power analyzer (battery optimization)
- BLE sniffer (protocol debugging)
- Oscilloscope (signal integrity)
- 3D printer (enclosure prototyping)
- Soldering station and hot air rework

### Software Development Tools
- ESP-IDF v6.0+
- Zephyr RTOS SDK (for nRF52840)
- KiCad (PCB design)
- FreeCAD / Fusion 360 (3D modeling)
- Python (OTA tools, testing scripts)

### Development Boards
- ESP32-S3-DevKitC-1 (x2 for testing)
- Adafruit Qt Py ESP32-S3 (x2)
- nRF52840 DK (x2)
- MAX3421E breakout boards (x3)
- PMW3360 trackball modules (x2)

### Components for Prototyping
- LiPo batteries (various sizes)
- Charging ICs (TP4056, BQ25895)
- Boost converters (TPS61088, etc.)
- Protection ICs
- Connectors (JST, USB-C)
- Passive components assortment

---

## Timeline Estimate

**Aggressive Timeline (Full-time development):** 6-9 months  
**Realistic Timeline (Part-time development):** 12-18 months  
**Conservative Timeline (weekends/evenings):** 18-24 months

**Milestone Schedule (Realistic Timeline):**

- **Month 1-2:** Phase 6A (Qt Py) complete
- **Month 3-4:** Phase 2 (BLE Security) complete
- **Month 5-8:** Phase 1 (Battery) complete with testing
- **Month 9-11:** Phase 5 (OTA) complete
- **Month 12-13:** Phase 3 (LED API) complete
- **Month 14-16:** Phase 4 (Trackball) complete
- **Month 17-22:** Phase 6B (nRF52840) complete
- **Month 23-24:** Phase 7 (Documentation) and final polish

---

## Conclusion

This roadmap provides a comprehensive plan for the next phase of the M4G BLE Bridge project. The focus is on wireless operation, enhanced security, multi-platform support, and improved user experience.

**Key Priorities:**
1. Enable wireless operation with battery power
2. Enhance BLE security and add multi-device support
3. Streamline configuration and add OTA updates
4. Expand hardware platform support
5. Improve documentation and user experience

The modular approach allows for incremental development and testing, with each phase building on previous work. The platform abstraction layer will future-proof the project for additional hardware variants.

**Next Steps:**
1. Review and approve this roadmap
2. Set up development environment for Phase 6A (Qt Py)
3. Order necessary hardware and components
4. Begin implementation following the priority order

---

## Change Log

- **2025-01-XX:** Initial roadmap document created
