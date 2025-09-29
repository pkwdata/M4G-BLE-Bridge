# nRF52840 + MAX3421E Platform Implementation

This directory contains the platform-specific implementation for nRF52840 SoC with external MAX3421E USB host controller.

## Status: **STUB IMPLEMENTATION**

The files in this directory are currently stubs and placeholders for the actual nRF52840 implementation. To complete this implementation, the following would be required:

## Required Components

### 1. Zephyr RTOS Integration
- [ ] Port build system to support Zephyr alongside ESP-IDF
- [ ] Implement Zephyr-based logging, timing, and task management
- [ ] Create Zephyr device tree overlay for hardware configuration

### 2. MAX3421E USB Host Driver
- [ ] Implement SPI communication with MAX3421E
- [ ] Create USB host state machine for device enumeration
- [ ] Implement HID device discovery and report processing
- [ ] Add support for USB hub detection (for CharaChorder dual-half)

### 3. nRF52840 BLE Stack Integration
- [ ] Implement HID over GATT service using Zephyr Bluetooth stack
- [ ] Add pairing and bonding support
- [ ] Implement keyboard and mouse report characteristics
- [ ] Add advertising and connection management

### 4. Hardware Abstraction
- [ ] GPIO configuration for SPI, LED, and control signals
- [ ] Power management for low-power operation
- [ ] Non-volatile storage using Zephyr settings or flash API

## Hardware Requirements

To use this platform, you would need:

1. **nRF52840 Development Kit** or compatible board
2. **MAX3421E USB Host Controller** breakout board
3. **SPI connections** between nRF52840 and MAX3421E:
   - MOSI, MISO, SCK pins
   - Chip Select (CS) pin
   - Interrupt (INT) pin from MAX3421E to nRF52840
   - Optional: Reset pin
4. **Status LED** connected to GPIO
5. **USB connector** for downstream devices (connected to MAX3421E)

## Build System Changes Required

To support nRF52840 builds, the project would need:

1. **Conditional CMake logic** to switch between ESP-IDF and Zephyr builds
2. **Zephyr project configuration** (prj.conf, boards/, etc.)
3. **West manifest** for Zephyr dependencies
4. **Cross-platform component isolation** to build only relevant parts

## Development Plan

1. **Phase 1**: Create minimal nRF52840 BLE-only build (no USB)
2. **Phase 2**: Add MAX3421E SPI communication and basic USB enumeration  
3. **Phase 3**: Implement full CharaChorder support with dual-device detection
4. **Phase 4**: Optimize power consumption and add advanced features

## Testing Strategy

1. **Unit tests** for MAX3421E driver on actual hardware
2. **Integration tests** with various USB keyboards
3. **CharaChorder compatibility** verification
4. **Power consumption** measurement and optimization
5. **BLE compliance** testing with multiple host devices

---

*This is a future expansion of the M4G BLE Bridge project. The current focus remains on ESP32-S3 implementation and CharaChorder compatibility.*