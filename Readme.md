# ESP32-S3 USB-to-BLE Bridge

A wireless bridge that converts USB keyboards to Bluetooth HID devices using ESP32-S3. Originally designed for the CharaChorder MasterForge keyboard, this project enables any USB keyboard to work wirelessly via Bluetooth Low Energy (BLE).

This project was originally started from ths project <https://github.com/jmdmahdi/ESP32-USB-TO-BLE>
Then with the help of Claude code I was able to modify this project to provide support to the CharaChorder Master Forge. The M4G (MasterForge) requires bridge mode support on the esp32-s3. Because of this I had to move the project from PlatformIO to IDF.

## Features

- **USB Host Support**: Full USB host functionality with hub support for complex keyboards
- **BLE HID Profile**: Complete BLE HID Over GATT Profile (HOGP) implementation
- **Windows Compatibility**: Optimized for Windows 10/11 with proper encryption and notification handling
- **Real-time Processing**: Low-latency keystroke forwarding with 15-byte to 8-byte HID report conversion
- **Auto-reconnection**: Automatic USB device detection and BLE advertising restart
- **CharaChorder Optimized**: Specifically tuned for CharaChorder's dual ESP32-S3 internal architecture
- **Modular Components**: Clean separation of logging, LED status, USB host, BLE HID, and translation (bridge) logic
- **Persistent Boot Logs**: Captures early boot / crash logs into NVS and dumps them on next startup
- **Runtime Configuration**: NVS-based settings system for adjusting behavior without recompilation

## Quick Links

- **[📋 Development Roadmap](docs/development_roadmap.md)** - Comprehensive plan for future features
- **[📚 Documentation Index](docs/README.md)** - All technical docs and guides
- **[🔧 Tools](tools/README.md)** - Configuration validator and utilities
- **[🐛 Issues](https://github.com/pkwdata/M4G-BLE-Bridge/issues)** - Bug reports and feature requests

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

- The symbols are defined in the repository root `Kconfig` file (set via `__ROOT_KCONFIG` in CMake)
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
3. Buffers physical chord presses until release so raw finger positions never leak over BLE; single-key presses are replayed automatically when no chord output follows
4. Flattens emitted chords into standard boot keyboard reports (modifier + 6 key slots) and forwards them via `m4g_ble`
5. Optionally converts directional key groups (e.g. arrow keys) into relative mouse deltas (enabling rudimentary mouse-emulation)
6. Caches last sent keyboard & mouse reports to suppress redundant notifications

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

> **📋 Comprehensive Roadmap:** See [Development Roadmap](docs/development_roadmap.md) for detailed planning, specifications, and timelines for all future enhancements.

### Quick Summary

The M4G BLE Bridge project has an ambitious roadmap focused on enabling wireless operation, multi-platform support, and advanced features. Key areas of development include:

#### 🔋 **Phase 1: Power Management & Battery System** (4-6 weeks)
- LiPo battery integration (18650 cells, 2S configuration)
- Separate power rails for ESP32-S3 (3.3V @ 0.5A) and USB devices (5V @ 3A)
- USB-C PD charging with protection circuitry
- Fuel gauge monitoring and intelligent power management
- Sleep modes for extended battery life

#### 🔐 **Phase 2: BLE Security & Multi-Device Support** (3-4 weeks)
- Enhanced BLE security with Secure Connections and encryption
- Support for pairing with up to 8 devices
- Quick device switching via button or BLE control
- Privacy features with Resolvable Private Addresses

#### 💡 **Phase 3: LED Control API & Effects** (2-3 weeks)
- Comprehensive LED control for bridge status LED
- Control CharaChorder half LEDs (4 per half)
- Dynamic effects (breathing, rainbow, typing reactive)
- BLE GATT service for remote LED control
- Battery level and chord quality visual feedback

#### 🖱️ **Phase 4: Trackball Mouse Integration** (3-4 weeks)
- PMW3360 optical sensor integration
- Physical trackball module with buttons
- Smooth cursor control with acceleration curves
- Configurable DPI settings (400-1600)
- 3D-printable housing design

#### 📡 **Phase 5: Firmware Management & OTA Updates** (4-5 weeks)
- Wireless firmware updates over BLE
- Secure, authenticated updates with signature verification
- Resume capability after interruption
- Progress indication and battery level checking
- Future: 2.4GHz OTA for faster updates

#### 🔧 **Phase 6: Multi-Platform Support** (8-12 weeks)
- **6A:** Adafruit Qt Py ESP32-S3 build (2-3 weeks)
- **6B:** nRF52840 + MAX3421E platform (6-9 weeks)
  - Platform Abstraction Layer (PAL) for code reuse
  - Custom USB host driver for MAX3421E
  - Zephyr RTOS integration
- **6C:** Battery solutions for all platforms

#### 📚 **Phase 7: Documentation & User Experience** (2-3 weeks)
- Comprehensive user guides and tutorials
- Developer API reference documentation
- Streamlined build options menu
- Enhanced runtime configuration via NVS
- CLI and mobile app for configuration

### Implementation Status

| Phase | Status | Priority | Complexity |
|-------|--------|----------|------------|
| Phase 1: Power & Battery | Planned | HIGH | HIGH |
| Phase 2: BLE Security | Planned | HIGH | MEDIUM |
| Phase 3: LED Control | Planned | MEDIUM | MEDIUM |
| Phase 4: Trackball | Planned | MEDIUM | MEDIUM-HIGH |
| Phase 5: OTA Updates | Planned | HIGH | HIGH |
| Phase 6A: Qt Py | Planned | HIGH | MEDIUM |
| Phase 6B: nRF52840 | Planned | MEDIUM-HIGH | VERY HIGH |
| Phase 7: Documentation | Planned | MEDIUM | LOW-MEDIUM |

### Additional Resources

- **[nRF52840 + MAX3421E Integration Plan](docs/nrf52840_max3421e_plan.md)** - Technical details for alternate platform
- **[CharaChorder Timing Analysis](docs/charachorder_timing_analysis.md)** - Chord detection optimization
- **[Development Roadmap](docs/development_roadmap.md)** - Comprehensive planning document (NEW)

### Contributing to Development

We welcome contributions to any phase of development! See our [Contributing Guidelines](#contributing) for details on:
- Setting up your development environment
- Code style and testing requirements  
- Submitting pull requests
- Hardware testing and validation

For questions about the roadmap or to suggest new features, please open a GitHub issue.

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
