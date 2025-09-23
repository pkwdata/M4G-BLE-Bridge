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
```

#### Verify Installation

```bash
idf.py --version
# Should show: ESP-IDF v6.0.x
```

## Build and Flash Instructions

### 1. Clone and Configure

```bash
# Clone this repository
git clone [repository-url]
cd M4G-BLE-BRIDGE

# Configure project for your ESP32-S3
idf.py set-target esp32s3
```

### 2. Build

```bash
idf.py build
```

### 3. Flash

```bash
# Replace /dev/ttyACM0 with your ESP32-S3 port
idf.py -p /dev/ttyACM0 flash
```

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

### System Overview

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   USB Keyboard  │────│   ESP32-S3 Host  │────│  BLE Client     │
│                 │    │                  │    │  (Computer)     │
│ - CharaChorder  │    │ - USB Host Stack │    │ - Windows/Mac   │
│ - Standard HID  │    │ - NimBLE Stack   │    │ - Linux/Mobile  │
│ - Hub Support   │    │ - HID Conversion │    │ - Gaming Console│
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### Data Flow

1. **USB Capture**: USB Host library captures HID reports from keyboard
2. **Format Conversion**: 15-byte CharaChorder reports → 8-byte standard HID
3. **BLE Transmission**: NimBLE stack sends reports via GATT notifications
4. **Client Processing**: Computer receives and processes as keyboard input

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

## Known issues

- Disconnecting the USB device requires reset of the Bridge device
- Disconnecting the Bluetooth device does not sucecssfully reconnect, RST Bridge and remove and re-add to OS.
- Mouse Function of the M4G is not working
- Blue LED is not coming on once everything is connected and working
- Not getting enough Power for bridge and keyboard with a single USB cable. Currently using USB Cable to power keyboard and a seperate cable for the bridge device. This will eventually replaced with batteries.
