#!/bin/bash
# Quick script to check deployment status

echo "🔍 Checking GitHub Pages deployment..."
echo ""

# Check if site is responding
echo "1. Testing site availability..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" https://pkwdata.github.io/M4G-BLE-Bridge/)

if [ "$STATUS" = "200" ]; then
    echo "   ✅ Site is LIVE!"
    echo "   🌐 URL: https://pkwdata.github.io/M4G-BLE-Bridge/"
elif [ "$STATUS" = "404" ]; then
    echo "   ⏳ Site not deployed yet (404)"
    echo "   💡 Check workflow: https://github.com/pkwdata/M4G-BLE-Bridge/actions"
else
    echo "   ⚠️  Unexpected status: $STATUS"
fi

echo ""
echo "2. Checking workflow runs..."
echo "   Visit: https://github.com/pkwdata/M4G-BLE-Bridge/actions/workflows/deploy-device-manager.yml"
echo ""
echo "3. Pages settings:"
echo "   Visit: https://github.com/pkwdata/M4G-BLE-Bridge/settings/pages"
echo ""

# Try to fetch the page
echo "4. Attempting to fetch page..."
curl -s -I https://pkwdata.github.io/M4G-BLE-Bridge/ | head -n 5
