# Quick Start: Split Keyboard Setup

Get your split keyboard wireless setup running in minutes!

## What You Need

- **2x ESP32-S3 boards** (ESP32-S3-DevKitC-1 or similar)
- **2x USB-A cables** (for keyboard connections)
- **2x USB-C cables** (for power)
- **Left and right keyboard halves** (each with USB connection)

## 5-Minute Setup

### Step 1: Build and Flash Left Board

```bash
cd M4G-BLE-Bridge
./build-left.sh flash monitor
```

Wait for "Left side initialization complete" message, then press `Ctrl+]` to exit monitor.

### Step 2: Build and Flash Right Board

Connect the second ESP32-S3 board (use different USB port):

```bash
./build-right.sh flash monitor
```

Wait for "Right side initialization complete", then press `Ctrl+]` to exit.

### Step 3: Connect Keyboards

1. **Left board**: Connect left keyboard half via USB-A
2. **Right board**: Connect right keyboard half via USB-A
3. Both boards should show "USB connected" in logs

### Step 4: Pair with Computer

1. Open Bluetooth settings on your computer
2. Look for "M4G BLE Bridge"
3. Click to pair
4. Done! Start typing on both keyboard halves

## LED Status Quick Reference

### Left Board (connects to computer)
- **Red**: Waiting for connections
- **Green**: USB ready, no Bluetooth
- **Blue**: Everything connected - ready to type!

### Right Board (wireless transmitter)
- **Red**: Waiting for USB
- **Green**: Transmitting to left board
- **Yellow**: USB ready, searching for left board

## Troubleshooting

**Right board not connecting to left board?**
```bash
# Check that both boards use same channel and encryption key
# Default settings should work - if you changed them, verify:
# - CONFIG_M4G_ESPNOW_CHANNEL=1 (same on both)
# - CONFIG_M4G_ESPNOW_PMK="M4G_SPLIT_KEY!!" (same on both)
```

**Only one keyboard working?**
- Check USB connections on both boards
- View logs: `idf.py monitor` to see if data is transmitting

**High latency or missing keys?**
- Move boards closer together (< 5 meters optimal)
- Check for WiFi interference
- View statistics in logs every 10 seconds

## Advanced Configuration

See [SPLIT_KEYBOARD_SETUP.md](SPLIT_KEYBOARD_SETUP.md) for:
- Custom encryption keys
- Channel selection
- Performance tuning
- Multiple split keyboard pairs
- Detailed troubleshooting

## Switching Back to Single Keyboard

```bash
idf.py menuconfig
# M4G Bridge Options → Split Keyboard Configuration → Standalone
idf.py build flash
```

## Questions?

- **Detailed guide**: [SPLIT_KEYBOARD_SETUP.md](SPLIT_KEYBOARD_SETUP.md)
- **Main README**: [Readme.md](Readme.md)
- **Issues**: Create a GitHub issue with logs from both boards
