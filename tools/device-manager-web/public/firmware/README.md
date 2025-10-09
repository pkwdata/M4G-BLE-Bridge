# Firmware Files for M4G Device Manager

This directory contains firmware binaries served by the web-based Device Manager.

## 🚀 Quick Start

### Standalone Firmware (Single Keyboard)

```bash
# From project root
idf.py build

# Post-build automatically creates and copies:
# → M4G_BLE_BRIDGE_MERGED.bin (ready to flash at 0x0)
```

### Split Keyboard Firmware (Two Boards)

**Build Left Side (BLE to computer):**
```bash
./build-left.sh

# Auto-generates and copies:
# → M4G_BLE_BRIDGE_LEFT_MERGED.bin
```

**Build Right Side (Wireless to left):**
```bash
./build-right.sh

# Auto-generates and copies:
# → M4G_BLE_BRIDGE_RIGHT_MERGED.bin
```

## Files

### ✅ Merged Binaries (Recommended)

**Standalone Mode:**
- **`M4G_BLE_BRIDGE_MERGED.bin`** - Single keyboard firmware
  - USB to Bluetooth bridge
  - Original firmware for one keyboard
  - Flash address: `0x0`

**Split Keyboard Mode:**
- **`M4G_BLE_BRIDGE_LEFT_MERGED.bin`** - Left board firmware
  - Connects to left keyboard via USB
  - Receives from right board via 2.4GHz ESP-NOW
  - Transmits combined output via Bluetooth
  - Flash address: `0x0`

- **`M4G_BLE_BRIDGE_RIGHT_MERGED.bin`** - Right board firmware
  - Connects to right keyboard via USB
  - Transmits to left board via 2.4GHz ESP-NOW
  - No Bluetooth (acts as wireless bridge)
  - Flash address: `0x0`

### Individual Components (Advanced)
- `bootloader.bin` - Bootloader (0x0)
- `partition-table.bin` - Partition table (0x8000)
- `M4G_BLE_BRIDGE.bin` - Application (0x10000)

## Manifest Configuration

The `manifest.json` file defines firmware options in the Device Manager UI:

```json
{
  "defaultBaud": 921600,
  "packages": [
    {
      "id": "m4g-ble-bridge-standalone",
      "name": "M4G BLE Bridge (Standalone)",
      "description": "Single keyboard mode...",
      "images": [
        { "path": "firmware/M4G_BLE_BRIDGE_MERGED.bin", "address": "0x0" }
      ]
    },
    {
      "id": "m4g-ble-bridge-split-left",
      "name": "M4G BLE Bridge (Split - LEFT Side)",
      "description": "Split keyboard LEFT board...",
      "images": [
        { "path": "firmware/M4G_BLE_BRIDGE_LEFT_MERGED.bin", "address": "0x0" }
      ]
    },
    {
      "id": "m4g-ble-bridge-split-right",
      "name": "M4G BLE Bridge (Split - RIGHT Side)",
      "description": "Split keyboard RIGHT board...",
      "images": [
        { "path": "firmware/M4G_BLE_BRIDGE_RIGHT_MERGED.bin", "address": "0x0" }
      ]
    }
  ]
}
```

All paths are relative to the web server root.

## Building Firmware

See the main project documentation:
- [README_SPLIT_KEYBOARD.md](../../../../README_SPLIT_KEYBOARD.md) - Split keyboard overview
- [QUICK_START_SPLIT.md](../../../../QUICK_START_SPLIT.md) - Quick start guide
- [SPLIT_KEYBOARD_SETUP.md](../../../../SPLIT_KEYBOARD_SETUP.md) - Complete guide
