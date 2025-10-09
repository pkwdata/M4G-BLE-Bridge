# Next Steps - Split Keyboard Implementation

## ✅ Implementation Status: COMPLETE

All software components for split keyboard support have been successfully implemented and verified.

## Quick Start for Testing

### 1. Build Left Side Firmware
```bash
cd /home/me/M4G-BLE-Bridge
./build-left.sh
```

Expected output: "Building LEFT side firmware (USB + ESP-NOW RX + BLE)"

### 2. Build Right Side Firmware
```bash
./build-right.sh
```

Expected output: "Building RIGHT side firmware (USB + ESP-NOW TX only)"

### 3. Flash to Hardware

**Left Board:**
```bash
./build-left.sh flash
# Or manually:
# idf.py -p /dev/ttyACM0 flash
```

**Right Board:**
```bash
./build-right.sh flash
# Or manually (use different port):
# idf.py -p /dev/ttyACM1 flash
```

### 4. Monitor Both Boards

**Terminal 1 (Left Board):**
```bash
idf.py -p /dev/ttyACM0 monitor
```

**Terminal 2 (Right Board):**
```bash
idf.py -p /dev/ttyACM1 monitor
```

Look for:
- "LEFT side initialization complete" (left board)
- "RIGHT side initialization complete" (right board)
- "ESP-NOW initialized" (both boards)

### 5. Connect Keyboards and Test

1. Connect left keyboard half to left ESP32-S3 via USB-A
2. Connect right keyboard half to right ESP32-S3 via USB-A
3. Pair computer with "M4G BLE Bridge" via Bluetooth
4. Type on both keyboard halves

## Expected Behavior

### Left Board Serial Output
```
I (1234) M4G-LEFT: Booting M4G BLE Bridge - LEFT SIDE (Split Keyboard)
I (1245) M4G-LEFT: ESP-NOW initialized (role=LEFT, channel=1, MAC=XX:XX:XX:XX:XX:XX, peer=FF:FF:FF:FF:FF:FF)
I (1256) M4G-LEFT: Left side initialization complete
I (2345) M4G-LEFT: Local USB report: slot=0 len=8 charachorder=1
I (2346) M4G-LEFT: Remote (right) report via ESP-NOW: slot=1 len=8 charachorder=1
I (12345) M4G-LEFT: ESP-NOW stats: TX=0 RX=1234 failures=0 lost=0 RSSI=-42
```

### Right Board Serial Output
```
I (1234) M4G-RIGHT: Booting M4G BLE Bridge - RIGHT SIDE (USB-to-ESP-NOW)
I (1245) M4G-RIGHT: ESP-NOW initialized (role=RIGHT, channel=1, MAC=XX:XX:XX:XX:XX:XX, peer=FF:FF:FF:FF:FF:FF)
I (1256) M4G-RIGHT: Right side initialization complete - waiting for USB devices
I (2345) M4G-RIGHT: USB report received: slot=0 len=8 charachorder=1 - forwarding via ESP-NOW
I (2346) M4G-ESPNOW: TX HID: slot=0 len=8 chara=1 seq=1234
I (12345) M4G-RIGHT: ESP-NOW stats: TX=1234 RX=0 failures=0 lost=0 RSSI=-42
```

## Testing Checklist

### Basic Functionality
- [ ] Left board powers on and initializes
- [ ] Right board powers on and initializes
- [ ] Both boards show "ESP-NOW initialized"
- [ ] Left keyboard produces keystrokes
- [ ] Right keyboard produces keystrokes
- [ ] Both keyboards work simultaneously
- [ ] BLE connection to computer successful
- [ ] LED indicators show correct status

### ESP-NOW Communication
- [ ] Right board shows "TX HID" messages
- [ ] Left board shows "Remote (right) report via ESP-NOW" messages
- [ ] Sequence numbers increment properly
- [ ] RSSI values reasonable (-30 to -70 dBm)
- [ ] Packet loss is 0 or very low (<1%)
- [ ] No send failures in statistics

### Performance
- [ ] Latency feels acceptable (<50ms total)
- [ ] No noticeable delay compared to standalone
- [ ] Rapid typing works smoothly
- [ ] CharaChorder chords detected across halves

### Edge Cases
- [ ] Right board powered on before left (auto-discovery)
- [ ] Left board powered on before right (auto-discovery)
- [ ] Disconnect/reconnect right keyboard
- [ ] Disconnect/reconnect left keyboard
- [ ] Move boards apart (test range)
- [ ] Power cycle both boards (clean restart)

## Troubleshooting Quick Reference

### Issue: Right board not connecting to left

**Check:**
1. Both boards use same channel: `CONFIG_M4G_ESPNOW_CHANNEL=1`
2. Both boards use same PMK: `CONFIG_M4G_ESPNOW_PMK="M4G_SPLIT_KEY!!"`
3. Serial output shows "ESP-NOW initialized" on both
4. RSSI value is > -70 dBm

**Solution:**
```bash
# Verify configuration
grep ESPNOW sdkconfig.defaults.left
grep ESPNOW sdkconfig.defaults.right

# Rebuild if needed
./build-left.sh flash
./build-right.sh flash
```

### Issue: High latency or packet loss

**Check:**
```
ESP-NOW stats: TX=1234 RX=1234 failures=0 lost=0 RSSI=-42
```

- `lost` should be 0 or very low
- `failures` should be 0
- `RSSI` should be > -70 dBm

**Solutions:**
- Move boards closer together
- Change channel to avoid WiFi interference
- Check for obstacles between boards

### Issue: Only one keyboard works

**Left keyboard not working:**
- Check USB connection on left board
- Verify "Local USB report" messages in left board logs

**Right keyboard not working:**
- Check USB connection on right board
- Verify "TX HID" messages in right board logs
- Check ESP-NOW connection (see above)

## Configuration Changes

### Change WiFi Channel

Edit both `sdkconfig.defaults.left` and `sdkconfig.defaults.right`:
```
CONFIG_M4G_ESPNOW_CHANNEL=6
```

Rebuild both boards:
```bash
./build-left.sh flash
./build-right.sh flash
```

### Change Encryption Key

Edit both files with same key (exactly 16 characters):
```
CONFIG_M4G_ESPNOW_PMK="MyCustomKey12345"
```

Rebuild both boards.

### Disable Encryption (Testing Only)

Edit both files:
```
CONFIG_M4G_ESPNOW_USE_ENCRYPTION=n
```

Rebuild both boards.

## Documentation Reference

- **Quick Start**: [QUICK_START_SPLIT.md](QUICK_START_SPLIT.md)
- **Complete Guide**: [SPLIT_KEYBOARD_SETUP.md](SPLIT_KEYBOARD_SETUP.md)
- **Technical Details**: [SPLIT_KEYBOARD_CHANGES.md](SPLIT_KEYBOARD_CHANGES.md)
- **Implementation Status**: [IMPLEMENTATION_COMPLETE.md](IMPLEMENTATION_COMPLETE.md)

## Build System Reference

### Build Left Side
```bash
./build-left.sh              # Build only
./build-left.sh flash        # Build and flash
./build-left.sh flash monitor # Build, flash, and monitor
```

### Build Right Side
```bash
./build-right.sh              # Build only
./build-right.sh flash        # Build and flash
./build-right.sh flash monitor # Build, flash, and monitor
```

### Build Standalone (Original)
```bash
idf.py build
idf.py flash
idf.py monitor
```

### Clean Build
```bash
idf.py fullclean
./build-left.sh   # or ./build-right.sh
```

## Reporting Issues

When reporting issues, please include:

1. **Which board**: Left, right, or both?
2. **Serial logs**: From both boards (use `idf.py monitor`)
3. **Configuration**: Contents of `sdkconfig` or `sdkconfig.defaults.*`
4. **ESP-NOW stats**: Copy the statistics line from logs
5. **Steps to reproduce**: Exact sequence of actions

Example issue report:
```
Title: Right board not transmitting to left board

Description:
- Left board initializes correctly
- Right board initializes correctly
- Right board shows USB connected
- Right board shows "TX HID" messages
- Left board never shows "Remote (right) report" messages
- ESP-NOW stats show: TX=1234 failures=1234 RSSI=0

Logs:
[Attach left_board.log and right_board.log]

Configuration:
CONFIG_M4G_ESPNOW_CHANNEL=1
CONFIG_M4G_ESPNOW_USE_ENCRYPTION=y
CONFIG_M4G_ESPNOW_PMK="M4G_SPLIT_KEY!!"
```

## Performance Benchmarking

### Measure Latency
```bash
# On left board, enable detailed logging
m4g_log_enable_keypress(true);

# Look for timestamps in logs:
# Right board: "TX HID" timestamp
# Left board: "Remote (right) report" timestamp
# Difference = ESP-NOW latency
```

### Monitor Packet Loss
```bash
# Watch statistics every 10 seconds
# lost=0 is ideal
# lost>10 indicates problems
```

### Check RSSI
```bash
# RSSI values:
# -30 dBm: Excellent
# -50 dBm: Good
# -70 dBm: Fair (usable)
# -85 dBm: Poor (expect issues)
# -90 dBm: Unreliable
```

## Success Criteria

✅ **Minimum Viable**
- Both boards initialize without errors
- ESP-NOW link established (peer_connected=true)
- Left keyboard produces output
- Right keyboard produces output
- BLE connection to computer works

✅ **Production Ready**
- Packet loss < 0.1%
- Send failures = 0
- RSSI > -70 dBm
- Total latency < 50ms
- Stable over 1+ hour continuous use

✅ **Optimal Performance**
- Packet loss = 0
- RSSI > -50 dBm
- Total latency < 30ms
- CharaChorder chords work flawlessly
- No observable difference from wired connection

## What's Next?

### After Hardware Validation

1. **Performance Tuning**
   - Optimize buffer sizes
   - Adjust retry parameters
   - Fine-tune power settings

2. **Feature Enhancements**
   - Battery support planning
   - OTA update mechanism
   - BLE configuration service

3. **Production Preparation**
   - PCB design for compact form factor
   - 3D-printed enclosures
   - Assembly instructions

4. **Community**
   - Share results with CharaChorder community
   - Gather feedback from users
   - Iterate based on real-world usage

## Support

**Questions?** Check the documentation:
- [QUICK_START_SPLIT.md](QUICK_START_SPLIT.md)
- [SPLIT_KEYBOARD_SETUP.md](SPLIT_KEYBOARD_SETUP.md)

**Found a bug?** Create a GitHub issue with:
- Detailed description
- Serial logs from both boards
- Steps to reproduce

**Need help?** Include in your request:
- What you're trying to do
- What actually happens
- Logs from both boards
- Configuration files

---

**Status**: Ready for hardware testing  
**Last Updated**: 2025-10-08  
**Version**: 1.0.0
