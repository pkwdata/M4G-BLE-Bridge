# GitHub Pages Deployment Checklist

## ⚠️ REQUIRED: One-Time Repository Setup

The GitHub Actions workflow is configured, but **you must manually enable GitHub Pages** in the repository settings:

### Step-by-Step:

1. **Go to Repository Settings**
   - Visit: https://github.com/pkwdata/M4G-BLE-Bridge/settings/pages
   - Or navigate: Repository → Settings → Pages (in left sidebar)

2. **Configure Source**
   - Under "Build and deployment"
   - **Source**: Select **"GitHub Actions"** (NOT "Deploy from a branch")
   - Click Save if prompted

3. **Wait for Deployment**
   - Go to: https://github.com/pkwdata/M4G-BLE-Bridge/actions
   - Look for "Deploy Device Manager to GitHub Pages" workflow
   - Should show as running or completed (green checkmark)
   - Takes ~2-3 minutes

4. **Verify Deployment**
   - Once complete, visit: **https://pkwdata.github.io/M4G-BLE-Bridge/**
   - You should see the Device Manager interface

## Troubleshooting

### "Pages is not enabled" Error
- **Problem**: Repository settings don't have Pages enabled
- **Solution**: Follow Step 1-2 above to enable GitHub Actions as source

### Workflow Not Running
- **Problem**: No workflow appears in Actions tab
- **Solution**: Make a commit that changes files in `tools/device-manager-web/`
- **Quick fix**: 
  ```bash
  cd tools/device-manager-web
  touch .deploy-trigger
  git add .deploy-trigger
  git commit -m "Trigger deployment"
  git push
  ```

### 404 Error on Deployed Site
- **Problem**: Site deployed but shows 404
- **Solution**: 
  - Verify `base: '/M4G-BLE-Bridge/'` is in `vite.config.ts`
  - Check deployment used correct artifact path: `tools/device-manager-web/dist`
  - Review workflow logs for build errors

### Firmware Files Not Loading
- **Problem**: Device Manager loads but can't find firmware
- **Solutions**:
  1. Ensure `public/firmware/M4G_BLE_BRIDGE_MERGED.bin` exists
  2. Build locally first: `idf.py build` (creates merged binary)
  3. Commit the binary: `git add tools/device-manager-web/public/firmware/`
  4. Push: `git push`

### Permission Denied During Deployment
- **Problem**: GitHub Actions can't deploy to Pages
- **Solution**: 
  - Go to Settings → Actions → General
  - Under "Workflow permissions"
  - Select "Read and write permissions"
  - Check "Allow GitHub Actions to create and approve pull requests"
  - Save

## Current Workflow Status

The workflow is configured to run on:
- ✅ Every push to `master` that changes `tools/device-manager-web/**`
- ✅ Every change to `.github/workflows/deploy-device-manager.yml`
- ✅ Manual trigger via Actions tab

Check current status: https://github.com/pkwdata/M4G-BLE-Bridge/actions/workflows/deploy-device-manager.yml

## Quick Commands

### Local Testing Before Deploy
```bash
cd tools/device-manager-web
npm run build
npm run preview  # Test production build locally
```

### Manual Trigger via gh CLI (if installed)
```bash
gh workflow run deploy-device-manager.yml
```

### Check Deployment Status
```bash
gh run list --workflow=deploy-device-manager.yml --limit 5
```

## What Gets Deployed

From your local `tools/device-manager-web/`:
- ✅ Built React application (`dist/` after build)
- ✅ Firmware files from `public/firmware/`
- ✅ All assets, images, icons
- ❌ Source code (`.tsx`, `.ts` files - only built output)
- ❌ `node_modules/` (not needed in deployment)

## Next Steps After Successful Deployment

1. **Update documentation** with the live URL
2. **Test Web Serial** on the deployed site with ESP32-S3
3. **Share the link** - anyone can use it to flash firmware!
4. **Consider custom domain** (optional) via Settings → Pages → Custom domain

---

**Last Updated**: October 6, 2025  
**Deployment URL**: https://pkwdata.github.io/M4G-BLE-Bridge/
