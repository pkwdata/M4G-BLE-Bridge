# ESP32-S3 USB-to-BLE Bridge

A wireless bridge that converts USB keyboards to Bluetooth HID devices using ESP32-S3. Originally designed for the **CharaChorder MasterForge** keyboard, this project enables any USB keyboard to work wirelessly via Bluetooth Low Energy (BLE).

**✨ Recent Updates:**
- Enhanced CharaChorder support with chord processing delay and dual-device connectivity
- Platform abstraction layer foundation for future nRF52840+MAX3421E support
- Improved USB enumeration with CharaChorder-specific detection
- Configurable chord processing timing to prevent interference with ChordBot

This project was originally started from ths project <https://github.com/jmdmahdi/ESP32-USB-TO-BLE>
Then with the help of Claude code I was able to modify this project to provide support to the CharaChorder Master Forge. The M4G (MasterForge) requires bridge mode support on the esp32-s3. Because of this I had to move the project from PlatformIO to IDF.

## Features

### CharaChorder MasterForge Support 🎯
- **Automatic Detection**: Recognizes CharaChorder devices by VID/PID (0x1A40:0x0101)
- **Dual-Half Tracking**: Monitors both keyboard halves and requires both for connectivity
- **Chord Processing Delay**: Configurable 10-1000ms delay to allow internal chord processing
- **Smart Translation**: Avoids interfering with ChordBot's chord-to-word expansion

### Universal Keyboard Support ⌨️
- **USB Host Support**: Full USB host functionality with hub support for complex keyboards
- **HID Report Translation**: Real-time 15-byte to 8-byte HID report conversion
- **Auto-reconnection**: Automatic USB device detection and BLE advertising restart
- **Low-latency Processing**: Optimized for responsive keystroke forwarding

### Advanced BLE Features 📡
- **BLE HID Profile**: Complete BLE HID Over GATT Profile (HOGP) implementation
- **Windows Compatibility**: Optimized for Windows 10/11 with proper encryption and notification handling
- **Duplicate Suppression**: Optional bandwidth optimization by filtering redundant reports
- **Robust Bonding**: Automatic reconnection with previously paired devices

### Platform & Architecture 🔧
- **Modular Components**: Clean separation of logging, LED status, USB host, BLE HID, and translation (bridge) logic
- **Platform Abstraction**: Foundation for multi-platform support (ESP32-S3, future nRF52840+MAX3421E)
- **Persistent Boot Logs**: Captures early boot / crash logs into NVS and dumps them on next startup
- **Advanced Configuration**: Comprehensive Kconfig menu system for customization

## Hardware Requirements

### Primary Hardware

- **ESP32-S3 Development Board** (tested with ESP32-S3-DevKitC-1)
  - USB-OTG support required
  - Minimum 4MB flash recommended
- **USB-A Female Connector** (for keyboard connection)
- **Power Supply** (5V via USB-C or external power)

### Supported Keyboards

- **CharaChorder MasterForge** (primary target)
- Any USB HID keyboard with standard 8-byte reports
- USB hub-based keyboards (dual-half keyboards)

## Software Requirements

### ESP-IDF Setup

This project requires ESP-IDF v6.0 or later.

#### Install ESP-IDF

```bash
# Clone ESP-IDF
git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git

# Install ESP-IDF
cd esp-idf
./install.sh

# Set up environment (add to .bashrc for persistence)
. ./export.sh
```text

#### Verify Installation

```bash
idf.py --version
# Should show: ESP-IDF v6.0.x
```text

## Build and Flash Instructions

### 1. Clone and Configure

```bash
# Clone this repository
git clone [repository-url]
cd M4G-BLE-BRIDGE

# Configure project for your ESP32-S3
idf.py set-target esp32s3
```text

### 2. Build

```bash
idf.py build
```text

### 3. Flash

```bash
# Replace /dev/ttyACM0 with your ESP32-S3 port
idf.py -p /dev/ttyACM0 flash
```text

### 4. Monitor

```bash
idf.py -p /dev/ttyACM0 monitor
# Press Ctrl+] to exit monitor
```

## Connection Setup

### Hardware Connections

```
USB Keyboard ──USB-A──> ESP32-S3 ──BLE──> Computer/Device
```

1. **Connect Keyboard**: Plug USB keyboard into ESP32-S3 USB port
2. **Power ESP32-S3**: Connect via USB-C or external 5V supply
3. **Power M4G**: Currently the M4G will require USB-C power to one of the many available USB-C ports
4. **Pair with Computer**: Look for "M4G BLE Bridge" in Bluetooth settings

### Pairing Process

1. Flash firmware and power on ESP32-S3
2. On your computer, open Bluetooth settings
3. Look for "M4G BLE Bridge" and select it
4. Complete pairing (encryption is automatic)
5. Type on your USB keyboard - keystrokes should appear on computer

## LED Status Meaning

| Color | USB Connected | BLE Connected | Meaning |
|-------|---------------|---------------|---------|
| Red   | No            | No            | Idle / waiting for both sides |
| Green | Yes           | No            | USB keyboard detected; waiting for BLE peer |
| Yellow| No            | Yes           | BLE bonded/connected; waiting for keyboard |
| Blue  | Yes           | Yes           | Bridge active (typing should work) |

Implemented in the `m4g_led` component so status logic is isolated from main application code.

## Configuration

### Key Configuration Files

#### `sdkconfig.defaults`

Contains all ESP32-S3 and Bluetooth optimizations:

- USB Host configuration with hub support
- NimBLE stack configuration for Windows compatibility
- Memory and performance optimizations

#### `main/main.c`

Core application with configurable parameters:

```c
// BLE device name
#define DEVICE_NAME "M4G BLE Bridge"

// USB enumeration settings
#define CHARACHORDER_VID 0x1A40
#define CHARACHORDER_PID 0x0101
```

### Project Menu (Kconfig)

Custom project options appear under the menu title **"M4G Bridge Options"** in `menuconfig`.

Implementation details:

- The symbols are defined in the root `Kconfig.projbuild` file (standard ESP-IDF inclusion point)
- `main/Kconfig` is only a stub to avoid duplicate definitions after restructuring
- If you ever do a deep clean and the menu seems missing, press `/` in `menuconfig` and search for e.g. `M4G_ENABLE_ARROW_MOUSE` to confirm inclusion
- Run `idf.py fullclean menuconfig` if cache issues hide the menu after file moves

Logging persistence defaults: enabled on QT Py board, disabled on DevKit; toggle via `M4G_LOG_PERSISTENCE`.

### Customization

- **Device Name**: Change `DEVICE_NAME` in main.c
- **USB Vendor/Product IDs**: Modify VID/PID constants for different keyboards
- **BLE Parameters**: Adjust advertising intervals and connection parameters

## Troubleshooting

### Common Issues

#### 1. USB Device Not Detected

```
Check: USB cable connections
Check: ESP32-S3 power supply (ensure 5V)
Check: USB host configuration in sdkconfig.defaults
```

#### 2. BLE Connection Fails

```
Check: Device appears as "M4G BLE Bridge" in Bluetooth scan
Check: Clear Bluetooth cache on Windows
Check: Verify BLE compatibility mode in sdkconfig.defaults
```

#### 3. No Keystrokes Forwarded

```
Check: Monitor output shows "HID report from CharaChorder"
Check: BLE encryption completed ("Encryption enabled successfully")
Check: Notifications enabled ("force-enabled" after 3 seconds)
```

#### 4. Compilation Errors

```bash
# Clean and rebuild
idf.py fullclean
idf.py build
```

### Debug Monitoring

Enable detailed logging by monitoring serial output:

```bash
idf.py monitor
```

Look for these key indicators:

- USB enumeration: "CharaChorder hub device detected"
- BLE connection: "BLE connection established"
- Keystroke forwarding: "HID report from CharaChorder"

## Architecture

### High-Level Data Path

```
┌────────────┐   Raw HID (CharaChorder / Std)   ┌────────────┐  Std 8‑byte / Mouse  ┌──────────────┐  GATT Notifications  ┌──────────────┐
│ USB Device │ ───────────────────────────────▶ │  m4g_usb   │ ───────────────────▶ │  m4g_bridge  │ ─────────────────────▶ │  m4g_ble     │
└────────────┘                                 └────────────┘                       └──────────────┘                        └──────────────┘
  ▲                                              │                                      │                                        │
  │                                              │ Logs (macro)                         │ Logs (macro)                         │ Logs (macro)
  │                                              ▼                                      ▼                                        ▼
  │                                          m4g_logging  ◀─────────────────────────────┴────────────────────────────────────────┘
  │                                                                                                                            
  └─────────────────────────── LED State (m4g_led observes USB + BLE connectivity) ────────────────────────────────────────────▶
```

### Component Responsibilities

| Component      | Responsibility |
|----------------|----------------|
| `m4g_logging`  | Unified logging macro (ESP_LOG wrapper) + persistent NVS ring buffer for boot log dump |
| `m4g_led`      | Maps aggregated connection state (USB present / BLE connected) to a 4‑color status LED |
| `m4g_usb`      | USB Host stack init, device enumeration, HID interrupt IN transfer scheduling, raw report capture |
| `m4g_ble`      | BLE GAP + GATT (HID over GATT Profile), bonding, notifications for keyboard & (future) mouse reports |
| `m4g_bridge`   | Translates raw device reports into standard 8‑byte keyboard HID + optional mouse movement, de-chording / key state management |
| `main`         | Orchestration only: initialize NVS/logging, BLE, bridge, USB, LED |

### Translation (Bridge) Layer

The CharaChorder (and potentially other advanced keyboards) can emit chorded or extended reports. The `m4g_bridge` component:

1. Receives raw HID packets from `m4g_usb`
2. Maintains an active key set / chord state
3. Flattens chords into a standard boot keyboard report (modifier + 6 key slots)
4. Optionally converts directional key groups (e.g. arrow keys) into relative mouse deltas (enabling rudimentary mouse-emulation) and forwards via `m4g_ble`
5. Caches last sent keyboard & mouse reports to suppress redundant notifications

This keeps USB acquisition and BLE transport unaware of device‑specific translation rules.

### Persistent Logging

Early boot and intermittent issues are captured using a lightweight append-only log stored in NVS. On each boot:

1. Stored log entries (previous session) are dumped to the console
2. The buffer is cleared
3. New entries accumulate during runtime (size bounded)

Enable / disable category logging with compile-time flags (e.g. `ENABLE_DEBUG_USB`, `ENABLE_DEBUG_BLE`) defined in the logging header. All components use the same `LOG_AND_SAVE(level, tag, fmt, ...)` macro so critical events are preserved even if a crash occurs before they reach the monitor.

### Runtime Sequence

1. NVS + logging system initialize and prior boot logs are emitted
2. BLE stack starts and advertises HID service
3. Bridge initializes internal key state
4. USB host starts and begins enumerating devices / scheduling HID IN transfers
5. As soon as raw reports arrive they are translated and sent over BLE notifications (once a BLE client connects & enables notifications or auto-force logic engages)
6. LED component updates color whenever USB device presence or BLE connection state changes

### Why This Modularity Matters

- Easier future swaps (e.g. replace BLE HID with 2.4GHz link) without touching USB layer
- Simplified debugging: isolate whether a problem is acquisition (usb), translation (bridge), or transport (ble)
- Unit / integration test hooks: each component can add self-test functions invoked during init

### Future Extension Points

- Multi-keyboard merge (aggregate multiple USB endpoints) inside `m4g_bridge`
- Advanced macro / text expansion dictionary before emitting BLE reports
- Dynamic configuration service (BLE characteristic or simple RPC) to toggle translation behaviors at runtime

### Previous Simplification Note

Earlier revisions performed conversion directly in `main.c` / USB callback. Those responsibilities were intentionally migrated into `m4g_bridge` to eliminate code duplication and to keep `m4g_usb` a pure acquisition layer.

## Future Development

### Planned Features

#### Hardware Enhancements

- [ ] **Qt Pi Format Adaptation**: Design compact Qt Pi-compatible PCB form factor
- [ ] **nRF52840 Integration**: Add nRF52840 for enhanced BLE performance and power efficiency
- [ ] **Battery Power System**: Integrate LiPo battery with charging circuit and power management
- [ ] **Power Control**: Add sleep modes and power optimization for battery operation
- [ ] **Dev Board Options**: Also researching option to use the MAC3421E as the Host controller with the nRF52840.

#### Software Features

- [ ] **Universal Keyboard Support**: Expand compatibility to all USB HID keyboards
- [ ] **Multi-device Pairing**: Support connection to multiple devices simultaneously
- [ ] **Wireless Keyboard Halves**: Enable wireless communication between split keyboard halves
- [ ] **Advanced HID Features**: Support for media keys, macros, and programmable functions

#### Connectivity Improvements

- [ ] **2.4GHz Wireless**: Add dedicated wireless protocol for ultra-low latency
- [ ] **USB-C Hub Support**: Expand to support USB-C keyboards and devices
- [ ] **Hot-swap Support**: Dynamic keyboard detection and switching
- [ ] **Mobile Compatibility**: Enhanced support for Android and iOS devices

### Development Roadmap

#### Phase 1: Hardware Miniaturization

- Design Qt Pi-compatible PCB
- Integrate battery and charging system
- Add power management ICs

#### Phase 2: Enhanced Connectivity

- Implement nRF52840 dual-chip architecture
- Add 2.4GHz wireless for keyboard halves
- Develop low-power communication protocols

#### Phase 3: Universal Compatibility

- Create generic USB HID detection
- Implement device-specific optimizations
- Add configuration management system

#### Phase 4: Battery Support / 3d printed case

- Remove the Wires!
- 3d printed case that attached to the M4G rails
- Battery case. Thinking of powering with LiPo 18650s
- Power controller and recharge, and over voltage protection

#### Phase 5: Advanced Features

- Multi-device connection management
- Real-time latency optimization
- Mobile app for configuration

## Contributing

### Development Setup

1. Fork this repository
2. Follow build instructions above
3. Create feature branch: `git checkout -b feature-name`
4. Test thoroughly with your hardware
5. Submit pull request with detailed description

### Code Style

- Follow ESP-IDF coding standards
- Use descriptive variable names
- Add comprehensive logging for debugging
- Include error handling for all operations

### Testing

- Test with multiple keyboard types
- Verify Windows/Mac/Linux compatibility
- Validate power consumption measurements
- Test edge cases (disconnect/reconnect, sleep/wake)

## License

MIT License

## Support

For issues and questions:

1. Check troubleshooting section above
2. Review debug output via `idf.py monitor`
3. Create GitHub issue with detailed logs
4. Include hardware setup and ESP-IDF version

## Acknowledgments

- <https://github.com/jmdmahdi/ESP32-USB-TO-BLE>
- ESP-IDF team for USB Host and NimBLE implementations
- CharaChorder team for hardware specifications
- ESP32 community for BLE HID examples and debugging

## Known Issues

Legend: (S) Solved, (O) Open

- (S) USB disconnect previously required bridge reset
- (S) BLE reconnection instability (now handled with proper advertising restart + bonding)
- (S) LED not updating to Blue when both links active
- (O) Mouse function: basic arrow→mouse mapping exists but full CharaChorder mouse feature set not yet implemented
- (O) Power delivery: insufficient to power both bridge + keyboard from a single cable on some setups (interim: dual-cable or powered hub) — targeted for battery redesign
- (O) Occasional chord edge cases: certain rapid chord sequences can produce stuck keys (bridge state machine refinement pending)
- (Planned) Add self-test assertions (BLE characteristic handle resolution, USB report reception within timeout) on boot

If you encounter a new issue, please open a GitHub issue with:

1. ESP-IDF version
2. Keyboard model & VID/PID
3. Serial log excerpt (including persistent boot log dump)
4. Steps to reproduce
