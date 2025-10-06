# GitHub Release Integration Guide

This guide explains how to use GitHub Releases to host firmware binaries that the Device Manager can download.

## Setup

### 1. Configure Manifest (Already Done)

The manifest now has two firmware options:

```json
{
  "packages": [
    {
      "id": "m4g-ble-bridge-latest-release",
      "name": "M4G BLE Bridge (Latest Release)",
      "images": [
        {
          "path": "https://github.com/pkwdata/M4G-BLE-Bridge/releases/latest/download/M4G_BLE_BRIDGE_MERGED.bin",
          "address": "0x0"
        }
      ]
    },
    {
      "id": "m4g-ble-bridge-local",
      "name": "M4G BLE Bridge (Local Build)",
      "images": [
        {
          "path": "/firmware/M4G_BLE_BRIDGE_MERGED.bin",
          "address": "0x0"
        }
      ]
    }
  ]
}
```

### 2. GitHub Actions Workflow (Automated Releases)

The workflow `.github/workflows/build-release.yml` will:
- Build firmware when you push a version tag
- Create a GitHub Release
- Upload firmware binaries as release assets

**To create a release:**

```bash
# Commit your changes
git add .
git commit -m "Release v1.0.0"

# Create and push a version tag
git tag v1.0.0
git push origin v1.0.0

# GitHub Actions will automatically:
# 1. Build the firmware
# 2. Create a release
# 3. Upload M4G_BLE_BRIDGE_MERGED.bin
```

### 3. Manual Release (Alternative)

If you don't want to use GitHub Actions:

1. Build firmware locally:
   ```bash
   idf.py build
   ```

2. Go to GitHub repository → Releases → "Draft a new release"

3. Create a new tag (e.g., `v1.0.0`)

4. Upload `build/M4G_BLE_BRIDGE_MERGED.bin`

5. Publish release

## How It Works

### Device Manager Firmware Loading

When users open the Device Manager:

1. **Latest Release** option fetches from:
   ```
   https://github.com/pkwdata/M4G-BLE-Bridge/releases/latest/download/M4G_BLE_BRIDGE_MERGED.bin
   ```
   - Always gets the latest stable release
   - No need to update the web app
   - Users always get newest firmware

2. **Local Build** option uses:
   ```
   /firmware/M4G_BLE_BRIDGE_MERGED.bin
   ```
   - For developers testing local builds
   - Requires running `idf.py build` first
   - File must be in `public/firmware/` directory

## CORS Considerations

GitHub serves release assets with proper CORS headers, so the Device Manager can download them directly from the browser. No proxy needed!

## URL Patterns

### Latest Release (Recommended)
```
https://github.com/{owner}/{repo}/releases/latest/download/{filename}
```
- Always points to newest release
- Users automatically get updates

### Specific Version
```
https://github.com/{owner}/{repo}/releases/download/v1.0.0/{filename}
```
- Fixed version
- Good for testing specific releases

### Raw File (Not Recommended)
```
https://raw.githubusercontent.com/{owner}/{repo}/{branch}/{path}
```
- Requires committing binaries to git
- Large binaries bloat repository
- Avoid this approach

## Testing

### Test Locally First
```bash
# 1. Build firmware
idf.py build

# 2. Start dev server
cd tools/device-manager-web
npm run dev

# 3. Open http://localhost:5173
# 4. Select "Local Build" and flash
```

### Test Remote Release
```bash
# 1. Create a release on GitHub (see step 2 above)

# 2. Open Device Manager
cd tools/device-manager-web
npm run dev

# 3. Select "Latest Release" - it will download from GitHub
# 4. Flash to device
```

## Deployment Options

### Option A: GitHub Pages (Free)
Host the Device Manager itself on GitHub Pages:

```bash
# Build the web app
cd tools/device-manager-web
npm run build

# Deploy dist/ to gh-pages branch
# Users access at: https://pkwdata.github.io/M4G-BLE-Bridge/
```

### Option B: Self-Hosted
Run the Device Manager locally or on your own server:
```bash
cd tools/device-manager-web
npm run dev  # or npm run build + serve dist/
```

Both work with GitHub release URLs!

## Troubleshooting

### "Failed to fetch firmware"
- **Check**: Does the release exist? Visit the releases URL in your browser
- **Check**: Is the filename correct in manifest.json?
- **Fix**: Ensure you've created at least one release

### "CORS error"
- This shouldn't happen with GitHub releases (they include CORS headers)
- If it does, try accessing the Device Manager via HTTPS instead of HTTP

### "Download is slow"
- GitHub CDN is usually fast
- Large downloads (>1MB) may take a few seconds
- Progress is shown in Device Manager logs

## Example Workflow

```bash
# 1. Make changes to firmware
vim components/m4g_bridge/m4g_bridge.c

# 2. Commit
git add .
git commit -m "Fix key repeat issue"

# 3. Tag release
git tag v1.0.1
git push origin v1.0.1

# 4. GitHub Actions builds and releases automatically

# 5. Users refresh Device Manager and see latest firmware
```

## Advanced: Multiple Firmware Variants

You can add multiple packages to manifest.json:

```json
{
  "packages": [
    {
      "id": "stable",
      "name": "Stable Release",
      "path": "https://github.com/pkwdata/M4G-BLE-Bridge/releases/latest/download/M4G_BLE_BRIDGE_MERGED.bin"
    },
    {
      "id": "beta",
      "name": "Beta (Testing)",
      "path": "https://github.com/pkwdata/M4G-BLE-Bridge/releases/download/v2.0.0-beta/M4G_BLE_BRIDGE_MERGED.bin"
    },
    {
      "id": "local",
      "name": "Local Build",
      "path": "/firmware/M4G_BLE_BRIDGE_MERGED.bin"
    }
  ]
}
```

## Summary

✅ **Manifest configured** - Points to GitHub releases  
✅ **GitHub Actions workflow ready** - Auto-builds on tag push  
✅ **Local option available** - For development  
✅ **No server needed** - GitHub hosts the binaries  
✅ **Always up-to-date** - Users get latest firmware automatically  

**Next step**: Create your first release with `git tag v1.0.0 && git push origin v1.0.0`
