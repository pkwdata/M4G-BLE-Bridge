# Device Manager Web Integration - Split Keyboard Support

## ✅ What Was Updated

Your web-based Device Manager now supports **three firmware options** for flashing:

1. **Standalone Mode** - Single keyboard (original)
2. **Split - LEFT Side** - BLE to computer + ESP-NOW receiver
3. **Split - RIGHT Side** - Wireless transmitter only

## Updated Files

### 1. Manifest Configuration
**File**: `tools/device-manager-web/public/firmware/manifest.json`

```json
{
  "defaultBaud": 921600,
  "packages": [
    {
      "id": "m4g-ble-bridge-standalone",
      "name": "M4G BLE Bridge (Standalone)",
      "description": "Single keyboard mode - USB to Bluetooth..."
    },
    {
      "id": "m4g-ble-bridge-split-left",
      "name": "M4G BLE Bridge (Split - LEFT Side)",
      "description": "Split keyboard LEFT board - Connects to left keyboard..."
    },
    {
      "id": "m4g-ble-bridge-split-right",
      "name": "M4G BLE Bridge (Split - RIGHT Side)",
      "description": "Split keyboard RIGHT board - Connects to right keyboard..."
    }
  ]
}
```

### 2. Build Scripts Auto-Copy
**Files**: `build-left.sh` and `build-right.sh`

Both scripts now automatically copy the built firmware to the device-manager-web directory with proper names:

- `build-left.sh` → `M4G_BLE_BRIDGE_LEFT_MERGED.bin`
- `build-right.sh` → `M4G_BLE_BRIDGE_RIGHT_MERGED.bin`
- Standard build → `M4G_BLE_BRIDGE_MERGED.bin` (unchanged)

### 3. Documentation
**File**: `tools/device-manager-web/public/firmware/README.md`

Updated to explain all three firmware variants and how to build them.

## How Users Will See It

When users open the Device Manager web interface, they'll see a dropdown with three options:

```
┌─────────────────────────────────────────────┐
│ Select Firmware                        ▼   │
├─────────────────────────────────────────────┤
│ ○ M4G BLE Bridge (Standalone)              │
│   Single keyboard mode - USB to Bluetooth  │
│                                             │
│ ○ M4G BLE Bridge (Split - LEFT Side)       │
│   Split keyboard LEFT board - Connects to  │
│   left keyboard via USB, receives from     │
│   right board via 2.4GHz ESP-NOW...        │
│                                             │
│ ○ M4G BLE Bridge (Split - RIGHT Side)      │
│   Split keyboard RIGHT board - Connects to │
│   right keyboard via USB and transmits to  │
│   left board via 2.4GHz ESP-NOW...         │
└─────────────────────────────────────────────┘
```

## Workflow for Split Keyboard Setup

### Using Device Manager Web Interface

**Step 1: Build Firmware**
```bash
# In project root
./build-left.sh    # Generates M4G_BLE_BRIDGE_LEFT_MERGED.bin
./build-right.sh   # Generates M4G_BLE_BRIDGE_RIGHT_MERGED.bin
```

**Step 2: Open Device Manager**
```bash
cd tools/device-manager-web
npm install
npm run dev
# Or visit: https://your-deployed-site.github.io
```

**Step 3: Flash Left Board**
1. Connect first ESP32-S3 board
2. Select "M4G BLE Bridge (Split - LEFT Side)" from dropdown
3. Click "Connect" → Select serial port
4. Click "Flash"
5. Wait for completion

**Step 4: Flash Right Board**
1. Disconnect first board, connect second ESP32-S3 board
2. Select "M4G BLE Bridge (Split - RIGHT Side)" from dropdown
3. Click "Connect" → Select serial port
4. Click "Flash"
5. Wait for completion

**Step 5: Test**
1. Connect left keyboard to left board
2. Connect right keyboard to right board
3. Pair computer with "M4G BLE Bridge" via Bluetooth
4. Type on both keyboards!

## Firmware Files

The device-manager-web expects these files in `public/firmware/`:

```
public/firmware/
├── M4G_BLE_BRIDGE_MERGED.bin        (Standalone)
├── M4G_BLE_BRIDGE_LEFT_MERGED.bin   (Split - Left)
├── M4G_BLE_BRIDGE_RIGHT_MERGED.bin  (Split - Right)
└── manifest.json                     (Configuration)
```

All three files are automatically generated and copied by the build scripts.

## Build Script Integration

### Left Side Build
```bash
./build-left.sh

# Output:
# ======================================
# Building LEFT SIDE Firmware
# ======================================
# 
# [build output...]
# 
# ======================================
# LEFT firmware built successfully!
# ======================================
# ✓ Firmware copied to device-manager-web: M4G_BLE_BRIDGE_LEFT_MERGED.bin
```

### Right Side Build
```bash
./build-right.sh

# Output:
# ======================================
# Building RIGHT SIDE Firmware
# ======================================
# 
# [build output...]
# 
# ======================================
# RIGHT firmware built successfully!
# ======================================
# ✓ Firmware copied to device-manager-web: M4G_BLE_BRIDGE_RIGHT_MERGED.bin
```

### Standalone Build
```bash
idf.py build

# Output:
# [build output...]
# ✓ Merged binary: build/M4G_BLE_BRIDGE_MERGED.bin
# ✓ Copied to Device Manager: tools/device-manager-web/public/firmware/
```

## Deployment

When you deploy the device-manager-web (e.g., to GitHub Pages), all three firmware variants will be available for users to flash directly from their web browser using Web Serial API.

### Deploying to GitHub Pages

```bash
cd tools/device-manager-web
npm run build
# Deploy the 'dist' folder to GitHub Pages
```

The manifest.json is automatically included and users can select which firmware to flash.

## Configuration Options

Each firmware package in manifest.json includes:

```json
{
  "id": "unique-identifier",
  "name": "Display Name",
  "description": "Help text shown to user",
  "chipFamily": "ESP32S3",
  "flashSize": "4MB",
  "flashMode": "dio",
  "flashFreq": "80m",
  "eraseAll": false,
  "images": [
    {
      "path": "firmware/FILENAME.bin",
      "address": "0x0"
    }
  ]
}
```

## User Documentation

Update your deployed site to include links to split keyboard documentation:

- [QUICK_START_SPLIT.md](../QUICK_START_SPLIT.md)
- [SPLIT_KEYBOARD_SETUP.md](../SPLIT_KEYBOARD_SETUP.md)
- [README_SPLIT_KEYBOARD.md](../README_SPLIT_KEYBOARD.md)

## Testing the Integration

### Local Testing

1. **Build all firmware variants**:
```bash
./build-left.sh
./build-right.sh
idf.py build
```

2. **Start device manager locally**:
```bash
cd tools/device-manager-web
npm install
npm run dev
```

3. **Open in browser**: http://localhost:5173

4. **Verify dropdown shows three options**

5. **Test flashing each variant**

### Verification Checklist

- [ ] Manifest.json has all three packages
- [ ] All three firmware files exist in public/firmware/
- [ ] Dropdown shows all three options in UI
- [ ] Descriptions are clear and helpful
- [ ] Left firmware flashes successfully
- [ ] Right firmware flashes successfully
- [ ] Standalone firmware still works

## Advantages of Device Manager Integration

### For Users
✅ **No command line** - Flash from web browser  
✅ **Clear options** - Descriptive names and help text  
✅ **One-click flashing** - Select option and click Flash  
✅ **Cross-platform** - Works on Windows, Mac, Linux  
✅ **No software install** - Just open browser  

### For Developers
✅ **Automatic updates** - Build scripts copy firmware  
✅ **Version control** - Firmware in git repository  
✅ **Easy deployment** - GitHub Pages or any static host  
✅ **No backend needed** - All client-side via Web Serial  

## Troubleshooting

### Firmware not appearing in dropdown?
- Check `manifest.json` syntax (valid JSON)
- Verify firmware files exist in `public/firmware/`
- Clear browser cache and reload

### Flash fails?
- Verify correct board selected (LEFT vs RIGHT)
- Check USB connection
- Ensure ESP32-S3 is in boot mode (automatic for most boards)
- Try lower baud rate (460800 instead of 921600)

### Wrong firmware flashed?
- Flash the correct variant (LEFT or RIGHT)
- No permanent damage - just reflash with correct version
- Verify by checking serial output after boot

## Summary

✅ **Device Manager Updated** - Three firmware options available  
✅ **Build Scripts Enhanced** - Auto-copy to device-manager-web  
✅ **Documentation Updated** - Clear instructions for users  
✅ **Web UI Ready** - Users can flash from browser  
✅ **Backward Compatible** - Standalone mode still available  

Users can now flash split keyboard firmware directly from the web interface without using command-line tools!
