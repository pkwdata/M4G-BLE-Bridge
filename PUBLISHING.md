# Publishing Firmware to M4G Device Manager

## Overview

The M4G Device Manager is hosted on GitHub Pages and allows users to flash firmware directly from their web browser using Web Serial API.

**Live URL**: https://pkwdata.github.io/M4G-BLE-Bridge/

## Quick Publish

To build and publish all firmware variants:

```bash
./publish.sh
```

This script will:
1. ✅ Build all three firmware variants (STANDALONE, LEFT, RIGHT)
2. ✅ Verify all binaries were created successfully
3. ✅ Stage firmware files for git commit
4. ✅ Prompt for confirmation before committing
5. ✅ Push to GitHub to trigger automatic deployment
6. ✅ Provide deployment status URL

## Manual Publish Process

If you prefer to publish manually:

### Step 1: Build All Firmware
```bash
./build-all.sh
```

### Step 2: Verify Firmware Files
```bash
ls -lh build/M4G_BLE_BRIDGE_*_MERGED.bin
ls -lh tools/device-manager-web/public/firmware/*.bin
```

You should see:
- `M4G_BLE_BRIDGE_STANDALONE_MERGED.bin`
- `M4G_BLE_BRIDGE_LEFT_MERGED.bin`
- `M4G_BLE_BRIDGE_RIGHT_MERGED.bin`

### Step 3: Commit and Push
```bash
git add tools/device-manager-web/public/firmware/*.bin
git commit -m "Update firmware binaries for Device Manager"
git push origin master
```

### Step 4: Monitor Deployment
Visit: https://github.com/pkwdata/M4G-BLE-Bridge/actions

Wait 2-3 minutes for the "Deploy Device Manager to GitHub Pages" workflow to complete.

### Step 5: Verify Deployment
Visit: https://pkwdata.github.io/M4G-BLE-Bridge/

You should see all three firmware options available for flashing.

## Firmware Variants Published

### 1. **STANDALONE** (Single Keyboard)
- **File**: `M4G_BLE_BRIDGE_STANDALONE_MERGED.bin`
- **Description**: USB to Bluetooth bridge for one keyboard
- **Use Case**: Original single keyboard setups

### 2. **LEFT** (Split Keyboard - Left Side)
- **File**: `M4G_BLE_BRIDGE_LEFT_MERGED.bin`
- **Description**: Receives from local USB + ESP-NOW, transmits via Bluetooth
- **Use Case**: Left half of split keyboard setup (connects to computer)

### 3. **RIGHT** (Split Keyboard - Right Side)
- **File**: `M4G_BLE_BRIDGE_RIGHT_MERGED.bin`
- **Description**: Receives from local USB, transmits via ESP-NOW only
- **Use Case**: Right half of split keyboard setup (relay only)

## Deployment Architecture

```
Local Build
    ↓
build/M4G_BLE_BRIDGE_*_MERGED.bin
    ↓ (automatic copy via CMake)
tools/device-manager-web/public/firmware/*.bin
    ↓ (git commit + push)
GitHub Repository
    ↓ (GitHub Actions workflow)
GitHub Pages Deployment
    ↓
https://pkwdata.github.io/M4G-BLE-Bridge/
```

## Manifest Configuration

The firmware variants are defined in:
```
tools/device-manager-web/public/firmware/manifest.json
```

Each entry includes:
- Unique ID
- Display name and description
- Chip family (ESP32S3)
- Flash parameters (4MB, DIO mode, 80MHz)
- Binary file path and flash address (0x0)

## Troubleshooting

### Firmware not appearing in Device Manager
**Problem**: Built firmware doesn't show up on the website

**Solutions**:
1. Check that binaries exist in `tools/device-manager-web/public/firmware/`
2. Verify manifest.json has correct file paths
3. Ensure GitHub Actions deployment succeeded
4. Clear browser cache and refresh

### Build fails in publish.sh
**Problem**: ESP-IDF environment not available

**Solution**:
```bash
. $HOME/esp/esp-idf/export.sh
./publish.sh
```

### GitHub Actions fails to deploy
**Problem**: Deployment workflow shows errors

**Solutions**:
1. Check workflow logs: https://github.com/pkwdata/M4G-BLE-Bridge/actions
2. Verify GitHub Pages is enabled in repository settings
3. Ensure workflow has write permissions (Settings → Actions → General)
4. Check that `vite.config.ts` has correct base path: `/M4G-BLE-Bridge/`

### Wrong firmware variant deployed
**Problem**: Only one variant appears or old files shown

**Solutions**:
1. Verify all three .bin files are in the firmware directory
2. Check manifest.json paths match actual filenames
3. Force rebuild: `rm -rf build && ./build-all.sh`
4. Clear deployment cache: make a dummy commit to trigger redeploy

## Testing Deployment Locally

Before publishing, test the web interface locally:

```bash
cd tools/device-manager-web
npm install
npm run dev
```

Visit: http://localhost:5173/M4G-BLE-Bridge/

To test the production build:
```bash
npm run build
npm run preview
```

## Deployment Checklist

Before running `./publish.sh`:

- [ ] ESP-IDF environment is loaded
- [ ] All code changes are committed
- [ ] Build system is clean (or run `rm -rf build`)
- [ ] Manifest.json is up to date
- [ ] Git repository is on `master` branch
- [ ] No uncommitted firmware changes

After running `./publish.sh`:

- [ ] All three binaries built successfully
- [ ] Git push completed successfully
- [ ] GitHub Actions workflow started
- [ ] Wait 2-3 minutes for deployment
- [ ] Test on live site: https://pkwdata.github.io/M4G-BLE-Bridge/
- [ ] Verify all three firmware options are available
- [ ] Test flashing with an actual ESP32-S3 device

## Binary Sizes

Typical sizes for firmware binaries:
- STANDALONE: ~1.0 MB
- LEFT: ~1.0 MB  
- RIGHT: ~0.8 MB (smaller as it doesn't include Bluetooth stack)

If binaries are significantly larger or smaller, investigate build configuration.

## Update Frequency

Publish firmware when:
- ✅ New features are added
- ✅ Bug fixes are implemented
- ✅ Configuration changes affect user experience
- ✅ Security updates are available
- ❌ During active development (use local builds)
- ❌ For experimental features (create separate branch)

## Related Documentation

- `BUILD.md` - Build instructions for all variants
- `DEPLOYMENT_CHECKLIST.md` - GitHub Pages setup details
- `Readme.md` - Project overview and features
- `QUICK_START_SPLIT.md` - Split keyboard setup guide

---

**Last Updated**: October 8, 2025
**Repository**: https://github.com/pkwdata/M4G-BLE-Bridge
**Device Manager**: https://pkwdata.github.io/M4G-BLE-Bridge/
