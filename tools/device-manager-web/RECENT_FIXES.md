# M4G BLE Bridge - Recent Updates Summary

## Issues Fixed

### 1. ✅ Serial Connection "Port Already Open" Error
**Problem**: Web Serial API reported "port is already open" on every connection attempt.

**Root Cause**: Double connection - our code was manually calling `transport.connect()`, then ESPLoader.main() tried to connect the same port again.

**Solution**: Removed manual connection loop. Now ESPLoader handles the connection internally:
```typescript
// Before (WRONG):
await transport.connect(baudRate);
await loader.main();  // ❌ tries to connect again

// After (CORRECT):
const transport = new Transport(port, true, true);
await loader.main();  // ✅ connects once
```

**Files Changed**:
- `tools/device-manager-web/src/App.tsx` - Removed manual connection, let ESPLoader handle it

---

### 2. ✅ Firmware Dropdown Formatting Issue
**Problem**: Text in firmware dropdown overlapped and was unreadable.

**Root Cause**: MUI Select with nested Stack/Typography lacked proper height constraints.

**Solution**: Added SelectProps with MenuProps styling:
```typescript
SelectProps={{
  MenuProps: {
    PaperProps: {
      sx: {
        '& .MuiMenuItem-root': {
          whiteSpace: 'normal',
          minHeight: 48,
          alignItems: 'flex-start',
          py: 1.5
        }
      }
    }
  }
}}
```

**Files Changed**:
- `tools/device-manager-web/src/App.tsx` - Added proper SelectProps styling

---

### 3. ✅ ESP32-S3 Not Running After Flash
**Problem**: Board flashed successfully but firmware didn't run.

**Root Cause**: Manifest was flashing only the app binary at 0x0, but ESP32-S3 requires:
- Bootloader at 0x0
- Partition table at 0x8000
- App at 0x10000

**Solution**: 
1. **Created merged binary auto-generation** in `CMakeLists.txt`:
   - Post-build step runs `esptool merge_bin`
   - Combines bootloader + partition table + app into single file
   - Auto-copies to Device Manager public directory

2. **Updated manifest** with two options:
   - **Single File** (recommended): `M4G_BLE_BRIDGE_MERGED.bin` at 0x0
   - **Multi-part** (advanced): Individual binaries at correct offsets

**Files Changed**:
- `CMakeLists.txt` - Added post-build merge step
- `tools/device-manager-web/public/firmware/manifest.json` - Added merged + multi-part packages
- `tools/device-manager-web/src/App.tsx` - Added flash summary logging
- `tools/device-manager-web/public/firmware/README.md` - Updated documentation

---

## How It Works Now

### Build Process
```bash
idf.py build
```

**Output** (automatic):
```
Creating merged firmware binary...
✓ Merged binary: build/M4G_BLE_BRIDGE_MERGED.bin
✓ Copied to Device Manager: tools/device-manager-web/public/firmware/
```

### Flash Process
1. Open Device Manager web UI
2. Click "Connect" → Select ESP32-S3 COM port
3. Select "M4G BLE Bridge (Single File)" from dropdown
4. Click "Flash Firmware"
5. Board automatically resets and runs

### Debug Logging
Enhanced logging now shows:
```
==== NEW CONNECTION ATTEMPT ====
[STARTUP CHECK] Browser has 0 port(s) in memory
Requesting serial port access…
User selected: VID 0x1a86 PID 0x55d3
Immediately after selection state: readable=false (locked=false), writable=false (locked=false)
Transport created for VID 0x1a86 PID 0x55d3. ESPLoader will handle connection...
Calling ESPLoader.main() to initiate bootloader handshake...
[esptool traces...]
Bootloader handshake successful
Writing firmware to flash…
--- Flash Summary ---
Chip: ESP32-S3
Files written: 1
  [0] 0x0 (638720 bytes)
--------------------
Device reset issued - board should reboot now
```

---

## Files Modified

### Core Fixes
- `tools/device-manager-web/src/App.tsx`
  - Removed manual `transport.connect()` loop
  - Added startup port enumeration check
  - Disabled terminal.clean() to preserve logs
  - Enhanced dropdown styling
  - Added flash summary logging
  - Added reset logic

### Build System
- `CMakeLists.txt`
  - Added post-build custom command to merge firmware
  - Auto-copies merged binary to Device Manager

### Configuration
- `tools/device-manager-web/public/firmware/manifest.json`
  - Added "Single File" package (merged binary at 0x0)
  - Added "Multi-part" package (individual binaries)
  - Correct flash parameters (dio, 80m, 4MB)

### Documentation
- `tools/device-manager-web/DEBUG_SERIAL_CONNECTION.md` - Connection debugging guide
- `tools/device-manager-web/public/firmware/README.md` - Firmware management docs
- `tools/merge_firmware.sh` - Standalone merge script (if needed without build)

---

## Testing Checklist

- [x] Serial connection succeeds on first attempt
- [x] No "port already open" errors
- [x] Firmware dropdown text is readable
- [x] Merged binary flashes correctly
- [x] ESP32-S3 boots and runs after flash
- [x] Build automatically creates merged binary
- [x] Flash summary shows correct offsets
- [x] Board resets after successful flash

---

## Known Limitations

### WSL + Windows Browser
- Browser (Windows) talks to COM ports directly
- Web server (WSL) doesn't have direct access
- If port is held by another Windows process, cleanup may fail
- **Workaround**: Close all browser tabs using Device Manager before retrying

### Browser Lock Behavior
- If browser crashes during flash, lock may persist
- **Solution**: Close browser entirely, reopen fresh

---

## Next Steps (Optional Enhancements)

1. **OTA Updates**: Add network-based firmware updates
2. **Verification**: Hash check after flash
3. **Rollback**: Keep previous firmware in backup partition
4. **Multi-board**: Support flashing multiple boards in sequence
5. **Compression**: Gzip firmware before transfer

---

## Quick Reference

### Build + Flash Workflow
```bash
# 1. Build firmware (creates merged binary automatically)
idf.py build

# 2. Start Device Manager
cd tools/device-manager-web
npm run dev

# 3. Open http://localhost:5173 in browser
# 4. Connect → Flash → Done!
```

### Manual Flash (Alternative)
```bash
# Using merged binary
esptool.py --chip esp32s3 write_flash 0x0 build/M4G_BLE_BRIDGE_MERGED.bin

# Using idf.py
idf.py -p /dev/ttyUSB0 flash
```
