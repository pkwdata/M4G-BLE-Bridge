# GitHub Pages Deployment

The M4G BLE Bridge Device Manager is automatically deployed to GitHub Pages.

## Live URL
🌐 **https://pkwdata.github.io/M4G-BLE-Bridge/**

## How It Works

### Automatic Deployment
- Every push to `master` that changes files in `tools/device-manager-web/` triggers a deployment
- GitHub Actions builds the React app and deploys to GitHub Pages
- Deployment typically takes 2-3 minutes

### Manual Deployment
You can also manually trigger deployment:
1. Go to https://github.com/pkwdata/M4G-BLE-Bridge/actions
2. Select "Deploy Device Manager to GitHub Pages"
3. Click "Run workflow"

## Repository Settings Required

To enable GitHub Pages, you need to:

1. Go to repository **Settings** → **Pages**
2. Under "Build and deployment":
   - Source: **GitHub Actions**
   - (The workflow will handle the rest)

## First-Time Setup

After pushing the workflow file, you may need to:

1. Go to **Settings** → **Pages**
2. Ensure "Source" is set to **GitHub Actions** (not "Deploy from a branch")
3. Wait 2-3 minutes for the first deployment
4. Visit https://pkwdata.github.io/M4G-BLE-Bridge/

## Features Available Online

✅ **Web Serial API** - Flash firmware directly from browser (Chrome/Edge only)  
✅ **GitHub Release Downloads** - Automatically fetch latest firmware  
✅ **Local Builds** - Upload your own firmware files  
✅ **Dark Theme** - Polished UI matching CharaChorder style  
✅ **Real-time Logging** - Monitor flash progress

## Local Development vs GitHub Pages

| Feature | Local (`npm run dev`) | GitHub Pages |
|---------|----------------------|--------------|
| Base URL | `http://localhost:5173/` | `https://pkwdata.github.io/M4G-BLE-Bridge/` |
| Firmware Path | `/firmware/...` | `/M4G-BLE-Bridge/firmware/...` |
| Build Command | `npm run dev` | `npm run build` (automated) |
| Live Reload | ✅ Yes | ❌ No |

## Troubleshooting

### Page shows 404
- Check that GitHub Pages is enabled in Settings → Pages
- Ensure workflow completed successfully in Actions tab
- Wait a few minutes for CDN propagation

### Firmware files not loading
- Verify `public/firmware/` files are in the repository
- Check browser console for CORS errors
- Ensure `base: '/M4G-BLE-Bridge/'` is set in `vite.config.ts`

### Web Serial not working
- GitHub Pages uses HTTPS (required for Web Serial)
- Only works in Chrome, Edge, and Opera
- Firefox/Safari do not support Web Serial API

## Updating the Deployment

Changes are deployed automatically on push to master:

```bash
# Make changes to Device Manager
cd tools/device-manager-web
npm run dev  # Test locally

# Commit and push
git add .
git commit -m "Update Device Manager UI"
git push origin master

# Deployment happens automatically!
```

## Monitoring Deployments

Watch deployment progress:
- **Actions**: https://github.com/pkwdata/M4G-BLE-Bridge/actions
- **Pages Dashboard**: Settings → Pages → Visit site

Build logs show:
1. Node.js setup
2. npm install
3. Vite build
4. Upload to GitHub Pages
5. Deployment status
