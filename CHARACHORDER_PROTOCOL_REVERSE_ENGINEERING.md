# CharaChorder Split Protocol Reverse Engineering

## Goal
Understand the USB protocol used between CharaChorder left and right halves to enable wireless ESP-NOW communication.

## Hardware Setup

### Current CharaChorder Configuration
- Left half connects to Right half via USB-C cable
- Right half (with ESP32-S3 inside) presents as single USB HID device to computer
- VID: 0x303A (Espressif)
- PID: 0x829A

### Target Configuration
```
[CC Left] --USB--> [ESP32 LEFT] --ESP-NOW--> [ESP32 RIGHT] <--USB-- [CC Right]
                        |
                        └--BLE--> [Computer]
```

## Step 1: Capture Traffic from Individual Halves

### Test Setup A: Left Half Only
1. Physically disconnect USB-C cable between halves
2. Connect **LEFT half only** to ESP32 RIGHT board via USB
3. Flash RIGHT firmware with raw packet logging enabled
4. Monitor serial output: `idf.py -p /dev/ttyACM1 monitor`
5. Press keys on LEFT half
6. Capture all "RAW USB RX" log messages

### Test Setup B: Right Half Only
1. Connect **RIGHT half only** to ESP32 RIGHT board via USB
2. Monitor serial output
3. Press keys on RIGHT half
4. Capture all "RAW USB RX" log messages

### Expected Data

Look for:
- **HID Keyboard Reports**: Standard 8-byte format `[modifier, reserved, key1-key6]`
- **Vendor-Specific Reports**: Custom protocol data
- **Different endpoint addresses**: Each half may use different USB endpoints

## Step 2: Analyze USB Descriptors

When a half connects, capture:
- Number of interfaces claimed
- Endpoint addresses (IN/OUT)
- HID report descriptors
- Any control transfers sent

## Step 3: Identify Protocol Pattern

Questions to answer:
1. Do both halves send standard HID reports?
2. Is there a "master/slave" relationship?
3. How does the CharaChorder firmware know which half sent data?
4. Are there keep-alive or synchronization packets?

## Step 4: Test with Your Computer

Connect one half directly to your Linux computer:
```bash
# See what USB device appears
lsusb -v -d 303a:829a

# Capture raw HID reports
sudo cat /dev/hidraw0 | xxd

# Or use usbhid-dump
sudo usbhid-dump -d 303a:829a
```

## Current Firmware Changes

### Added Raw Packet Logging
- File: `components/m4g_usb/m4g_usb.c`
- Every USB packet received is logged as hex dump
- Format: `RAW USB RX: ep=0xXX len=N` followed by hex data

### Next Build/Flash
```bash
./build-right.sh flash
idf.py -p /dev/ttyACM1 monitor
```

## Data Collection Template

For each keypress, record:

```
Timestamp: [time]
Half: [LEFT/RIGHT]
Key Pressed: [which key]
Raw Data: [hex bytes]
Endpoint: [0xXX]
Length: [N bytes]
```

## Hypothesis to Test

1. **Simple Pass-Through**: Each half sends standard HID reports, CharaChorder firmware just merges them
2. **Custom Protocol**: Halves send proprietary packets that need decoding
3. **Multiplexed Reports**: Both halves' data combined in single report with identifier byte

## Success Criteria

We've successfully reverse engineered the protocol when:
1. We can identify which half sent a keypress
2. We can decode all key data correctly
3. We can reconstruct the full keyboard state from both halves
4. We can implement the protocol in our ESP32 firmware

## Tools Needed

- USB protocol analyzer (Wireshark with usbmon, or hardware analyzer)
- Documentation of HID protocol: https://www.usb.org/hid
- CharaChorder community/forums for any existing documentation

## Risk Assessment

- **Low Risk**: Standard HID with simple multiplexing - easy to implement
- **Medium Risk**: Custom packets but well-documented format
- **High Risk**: Proprietary encrypted protocol or complex state machine

---

**Start with Step 1A** - disconnect the halves and test the LEFT half connected to your ESP32 board!
