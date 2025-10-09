# Split Keyboard Setup Guide

This guide explains how to build and configure the M4G BLE Bridge for split keyboard configurations, where two ESP32-S3 boards communicate wirelessly via 2.4GHz (ESP-NOW).

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                     SPLIT KEYBOARD SYSTEM                           │
└─────────────────────────────────────────────────────────────────────┘

    Left Keyboard           RIGHT ESP32-S3          LEFT ESP32-S3        Computer
         │                        │                      │                  │
         │                        │                      │                  │
    ┌────▼────┐              ┌────▼────┐          ┌─────▼─────┐       ┌────▼────┐
    │  USB    │              │  USB    │          │   USB     │       │   BLE   │
    │ Receiver│              │ Receiver│          │ Receiver  │       │ Receiver│
    └────┬────┘              └────┬────┘          └─────┬─────┘       └─────▲───┘
         │                        │                     │                   │
         │                        │                     │                   │
         │                   ┌────▼────┐           ┌────▼────┐             │
         │                   │ ESP-NOW │  2.4GHz   │ ESP-NOW │             │
         │                   │   TX    ├───────────►   RX    │             │
         │                   └─────────┘           └────┬────┘             │
         │                                              │                   │
         │                                         ┌────▼────┐             │
         │                                         │  Bridge │             │
         │                                         │ Combine │             │
         │                                         └────┬────┘             │
         │                                              │                   │
         │                                         ┌────▼────┐             │
         └─────────────────────────────────────────►   BLE   ├─────────────┘
                                                   │   TX    │
                                                   └─────────┘

Right Side:                        Left Side:
- Receives USB from right keyboard - Receives USB from left keyboard
- Transmits via ESP-NOW to left    - Receives ESP-NOW from right
- No Bluetooth                     - Combines both keyboards
                                   - Transmits to computer via BLE
```

## Features

- **Wireless Communication**: 2.4GHz ESP-NOW for ultra-low latency (<10ms typical)
- **Encrypted Link**: Optional AES encryption for wireless security
- **Packet Loss Detection**: Sequence number tracking with statistics
- **Automatic Peer Discovery**: Boards find each other automatically via broadcast
- **Independent Power**: Each side powered independently via USB-C
- **Full CharaChorder Support**: Chord detection works across both halves

## Hardware Requirements

### Per Board (2 total)
- ESP32-S3 Development Board with USB-OTG support
- USB-A female connector for keyboard connection
- USB-C power supply (5V)

### Recommended Setup
- Left board: Connect to left keyboard half
- Right board: Connect to right keyboard half
- Both boards: Power via USB-C (can use same power source or separate)

## Software Setup

### 1. Build Left Side Firmware

The left side receives from both the local USB keyboard AND the right side via ESP-NOW, then transmits combined output to the computer via Bluetooth.

```bash
# Using convenience script
./build-left.sh

# Or manually
cp sdkconfig.defaults.left sdkconfig.defaults.split
cat sdkconfig.defaults.split >> sdkconfig.defaults
idf.py set-target esp32s3
idf.py build
```

### 2. Build Right Side Firmware

The right side receives from the local USB keyboard and transmits to the left side via ESP-NOW only (no Bluetooth).

```bash
# Using convenience script
./build-right.sh

# Or manually
cp sdkconfig.defaults.right sdkconfig.defaults.split
cat sdkconfig.defaults.split >> sdkconfig.defaults
idf.py set-target esp32s3
idf.py build
```

### 3. Flash Firmware

**Flash Left Board:**
```bash
./build-left.sh flash

# Or manually
idf.py -p /dev/ttyACM0 flash
```

**Flash Right Board:**
```bash
./build-right.sh flash

# Or manually (use different port if both connected)
idf.py -p /dev/ttyACM1 flash
```

## Configuration

### WiFi Channel Selection

Both boards must use the same WiFi channel for ESP-NOW communication. Default is channel 1.

To change the channel, edit both `sdkconfig.defaults.left` and `sdkconfig.defaults.right`:

```
CONFIG_M4G_ESPNOW_CHANNEL=6
```

Valid channels: 1-13 (depending on region)

### Encryption

ESP-NOW encryption is **enabled by default** for security. Both boards share a Primary Master Key (PMK).

**To change the encryption key:**

Edit both `sdkconfig.defaults.left` and `sdkconfig.defaults.right`:

```
CONFIG_M4G_ESPNOW_PMK="YourCustomKey16"
```

**IMPORTANT:** The PMK must be exactly 16 characters and must be identical on both boards.

**To disable encryption** (not recommended):

```
CONFIG_M4G_ESPNOW_USE_ENCRYPTION=n
```

### Advanced Configuration

For advanced settings, use the configuration menu:

```bash
idf.py menuconfig
```

Navigate to: **M4G Bridge Options → Split Keyboard Configuration**

Options include:
- Device role (Left/Right/Standalone)
- ESP-NOW channel
- Encryption settings
- Primary Master Key

## Connection Setup

### Physical Connections

1. **Left Board:**
   - USB-A: Connect left keyboard half
   - USB-C: Power supply

2. **Right Board:**
   - USB-A: Connect right keyboard half
   - USB-C: Power supply

3. **Computer:**
   - Pair with "M4G BLE Bridge" via Bluetooth

### First Boot Sequence

1. Power on both boards
2. Wait 5 seconds for ESP-NOW initialization
3. Connect USB keyboards to respective boards
4. Right board will start transmitting to left board
5. Pair computer with left board via Bluetooth
6. Start typing!

## LED Status Indicators

### Left Board (BLE + ESP-NOW RX)
| Color | USB | BLE | ESP-NOW | Meaning |
|-------|-----|-----|---------|---------|
| Red   | No  | No  | -       | Waiting for connections |
| Green | Yes | No  | -       | USB ready, waiting for BLE |
| Yellow| No  | Yes | -       | BLE ready, waiting for USB |
| Blue  | Yes | Yes | Yes     | Fully operational |

### Right Board (ESP-NOW TX only)
| Color | USB | ESP-NOW Peer | Meaning |
|-------|-----|--------------|---------|
| Red   | No  | -            | Waiting for USB |
| Green | Yes | Yes          | Transmitting to left board |
| Yellow| Yes | No           | USB ready, searching for left board |

## Troubleshooting

### Right Board Not Connecting to Left Board

**Symptoms:** Right board shows USB connected but left board doesn't receive data

**Solutions:**
1. Verify both boards use same WiFi channel
2. Check encryption settings match on both boards
3. Verify PMK is identical (case-sensitive, exactly 16 chars)
4. Check serial monitor on both boards for ESP-NOW errors
5. Power cycle both boards

**Check ESP-NOW status:**
```bash
# Left board monitor
idf.py -p /dev/ttyACM0 monitor

# Look for:
# "ESP-NOW initialized (role=LEFT, channel=1, ...)"
# "RX HID: slot=1 ..." (receiving from right board)
```

```bash
# Right board monitor
idf.py -p /dev/ttyACM1 monitor

# Look for:
# "ESP-NOW initialized (role=RIGHT, channel=1, ...)"
# "TX HID: slot=0 ..." (transmitting to left board)
```

### High Latency or Packet Loss

**Symptoms:** Delayed or missing keystrokes

**Solutions:**
1. Reduce distance between boards (optimal: <5 meters)
2. Remove obstacles between boards
3. Change WiFi channel to avoid interference
4. Check for nearby WiFi networks on same channel
5. Monitor packet loss statistics in serial output

**Check statistics:**
```
ESP-NOW stats: TX=1234 RX=1230 failures=2 lost=4 RSSI=-45
```

- `lost` should be 0 or very low
- `RSSI` should be > -70 dBm for good signal

### Keys Stuck or Not Released

**Symptoms:** Keys remain pressed after release

**Solutions:**
1. This may indicate packet loss - check ESP-NOW statistics
2. Verify both keyboards are properly connected via USB
3. Check for USB connection issues on individual boards
4. Power cycle both boards

### Only One Keyboard Working

**Symptom:** Only left or right keyboard produces output

**Solutions:**
1. **Left keyboard not working:** Check USB connection on left board
2. **Right keyboard not working:** 
   - Check USB connection on right board
   - Verify ESP-NOW communication (see above)
   - Check right board's serial output for "TX HID" messages

## Performance Metrics

### Expected Performance

- **Latency:** 5-15ms typical (USB → ESP-NOW → BLE)
- **Range:** Up to 10 meters line-of-sight (depends on environment)
- **Packet Loss:** <0.1% in optimal conditions
- **Battery Life:** N/A (both boards require continuous power)

### Monitoring Performance

Enable debug logging in `main_left.c` and `main_right.c`:

```c
m4g_log_enable_usb(true);
m4g_log_enable_keypress(true);
```

Statistics are printed every 10 seconds:
```
ESP-NOW stats: TX=5234 RX=5230 failures=0 lost=4 RSSI=-42
```

## Advanced Topics

### Pairing Multiple Split Keyboards

Each split keyboard pair should use a unique PMK to avoid interference:

**Keyboard Pair 1:**
```
CONFIG_M4G_ESPNOW_PMK="KEYBOARD_PAIR_1"
CONFIG_M4G_ESPNOW_CHANNEL=1
```

**Keyboard Pair 2:**
```
CONFIG_M4G_ESPNOW_PMK="KEYBOARD_PAIR_2"
CONFIG_M4G_ESPNOW_CHANNEL=6
```

### Custom MAC Address Filtering

By default, boards use broadcast and auto-discover. For fixed peer MAC addresses:

In `main_left.c` and `main_right.c`, modify:

```c
// Instead of broadcast (0xFF...)
uint8_t peer_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
memcpy(espnow_cfg.peer_mac, peer_mac, 6);
```

### Switching Back to Standalone Mode

To build the original single-keyboard firmware:

```bash
# Use menuconfig to select Standalone
idf.py menuconfig
# M4G Bridge Options → Split Keyboard Configuration → Standalone

# Or edit sdkconfig manually
CONFIG_M4G_SPLIT_ROLE_STANDALONE=y

idf.py build
```

## Building for CharaChorder MasterForge

The CharaChorder MasterForge is ideal for this split setup:

1. Flash **left firmware** to left-side ESP32-S3
2. Flash **right firmware** to right-side ESP32-S3
3. Connect left MasterForge half to left board
4. Connect right MasterForge half to right board
5. Chord detection works across both halves automatically

## Comparison: Split vs Standalone

| Feature | Split Mode | Standalone Mode |
|---------|-----------|-----------------|
| Boards Required | 2 | 1 |
| Keyboards Supported | 2 (left + right) | 1 |
| Wireless Tech | ESP-NOW + BLE | BLE only |
| Latency | +5-10ms (ESP-NOW hop) | Minimal |
| Power Consumption | 2x boards | 1x board |
| Range (keyboard to board) | Wired USB | Wired USB |
| Range (board to computer) | BLE: 10m typical | BLE: 10m typical |
| Complexity | Moderate | Simple |

## FAQ

**Q: Can I use different ESP32-S3 boards for left and right?**  
A: Yes, any ESP32-S3 board with USB-OTG support will work.

**Q: What's the maximum range between left and right boards?**  
A: ESP-NOW typically achieves 10+ meters indoors, 50+ meters outdoors (line-of-sight).

**Q: Does this work with non-CharaChorder keyboards?**  
A: Yes! Any USB HID keyboard will work.

**Q: Can I add more than 2 keyboard halves?**  
A: The current firmware supports 2 halves (2 USB slots in the bridge). Modifications would be needed for more.

**Q: Why not use Bluetooth between the boards?**  
A: ESP-NOW has lower latency (~5-10ms) compared to Bluetooth (~20-50ms) and doesn't require pairing.

**Q: Can I use this for other split keyboard protocols (e.g., TRRS, I2C)?**  
A: This firmware is designed for USB-based split keyboards. Other protocols would require hardware modifications.

## Support

For issues specific to split keyboard mode:

1. Check both boards' serial monitors for error messages
2. Verify configuration matches on both boards (channel, PMK)
3. Test each board individually with a single keyboard
4. Check ESP-NOW statistics for packet loss
5. Create GitHub issue with logs from both boards

## License

MIT License (same as main project)
