#!/bin/bash
# M4G BLE Bridge - Publish Firmware to Device Manager
# This script builds all firmware variants and publishes them to GitHub Pages

set -e
source ~/esp/esp-idf/export.sh
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="tools/device-manager-web/public/firmware"

echo "=========================================="
echo "M4G BLE Bridge - Publish to Device Manager"
echo "=========================================="
echo ""

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo "❌ Error: Not in a git repository"
    exit 1
fi

# Check for uncommitted changes in firmware directory
if git diff --quiet HEAD -- "$FIRMWARE_DIR" && git diff --cached --quiet HEAD -- "$FIRMWARE_DIR"; then
    FIRMWARE_CLEAN=true
else
    FIRMWARE_CLEAN=false
fi

echo "Step 1: Building all firmware variants..."
echo "------------------------------------------"
if [ ! -f "$SCRIPT_DIR/build-all.sh" ]; then
    echo "❌ Error: build-all.sh not found"
    exit 1
fi

# Run build-all script
"$SCRIPT_DIR/build-all.sh"

echo ""
echo "Step 2: Verifying firmware files..."
echo "------------------------------------------"

# Check that all expected binaries exist
EXPECTED_BINARIES=(
    "build-standalone/M4G_BLE_BRIDGE_STANDALONE_MERGED.bin"
    "build-left/M4G_BLE_BRIDGE_LEFT_MERGED.bin"
    "build-right/M4G_BLE_BRIDGE_RIGHT_MERGED.bin"
)

MISSING_COUNT=0
for binary in "${EXPECTED_BINARIES[@]}"; do
    if [ ! -f "$binary" ]; then
        echo "❌ Missing: $binary"
        MISSING_COUNT=$((MISSING_COUNT + 1))
    else
        SIZE=$(stat -c%s "$binary" 2>/dev/null || stat -f%z "$binary" 2>/dev/null)
        SIZE_MB=$(echo "scale=2; $SIZE / 1048576" | bc 2>/dev/null || echo "N/A")
        echo "✓ Found: $(basename "$binary") (${SIZE_MB} MB)"
    fi
done

if [ $MISSING_COUNT -gt 0 ]; then
    echo ""
    echo "❌ Error: $MISSING_COUNT firmware file(s) missing"
    exit 1
fi

echo ""
echo "Step 3: Checking git status..."
echo "------------------------------------------"

# Get current branch
CURRENT_BRANCH=$(git branch --show-current)
echo "Current branch: $CURRENT_BRANCH"

# Check for uncommitted changes (excluding firmware)
if ! git diff --quiet HEAD -- . ':!tools/device-manager-web/public/firmware'; then
    echo ""
    echo "⚠️  Warning: You have uncommitted changes (excluding firmware):"
    git status --short | grep -v "tools/device-manager-web/public/firmware" || true
    echo ""
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted by user"
        exit 1
    fi
fi

echo ""
echo "Step 4: Preparing firmware for deployment..."
echo "------------------------------------------"

# Update manifest with current timestamp and version info
MANIFEST_FILE="$FIRMWARE_DIR/manifest.json"
if [ -f "$MANIFEST_FILE" ]; then
    echo "✓ Manifest file found: $MANIFEST_FILE"
    
    # Verify all firmware paths in manifest exist
    STANDALONE_BIN=$(grep -o 'firmware/M4G_BLE_BRIDGE_STANDALONE_MERGED.bin\|firmware/M4G_BLE_BRIDGE_MERGED.bin' "$MANIFEST_FILE" | head -1)
    if [ -z "$STANDALONE_BIN" ]; then
        echo "⚠️  Warning: Manifest may need updating for STANDALONE firmware"
    fi
else
    echo "❌ Error: Manifest file not found: $MANIFEST_FILE"
    exit 1
fi

# Check firmware files are in device-manager directory
echo ""
echo "Firmware files in device manager:"
ls -lh "$FIRMWARE_DIR"/*.bin | awk '{print "  " $9 " (" $5 ")"}'

echo ""
echo "Step 5: Git operations..."
echo "------------------------------------------"

# Add firmware files to git
git add "$FIRMWARE_DIR"/*.bin

# Update trigger file to ensure GitHub Actions workflow runs
# (Binary files sometimes don't trigger workflows reliably)
TRIGGER_FILE="tools/device-manager-web/.trigger-deploy"
echo "Deployment triggered: $(date -Iseconds)" > "$TRIGGER_FILE"
git add "$TRIGGER_FILE"

# Check if there are changes to commit
if git diff --cached --quiet HEAD -- "$FIRMWARE_DIR" "$TRIGGER_FILE"; then
    echo "ℹ️  No firmware changes detected - files are already up to date"
    NEED_COMMIT=false
else
    echo "✓ Firmware changes staged for commit"
    NEED_COMMIT=true
fi

if [ "$NEED_COMMIT" = true ]; then
    # Generate commit message with firmware versions
    COMMIT_MSG="Update firmware binaries for Device Manager

Built firmware variants:
- STANDALONE: $(stat -c%s "build-standalone/M4G_BLE_BRIDGE_STANDALONE_MERGED.bin" 2>/dev/null | numfmt --to=iec-i --suffix=B || echo 'N/A')
- LEFT: $(stat -c%s "build-left/M4G_BLE_BRIDGE_LEFT_MERGED.bin" 2>/dev/null | numfmt --to=iec-i --suffix=B || echo 'N/A')
- RIGHT: $(stat -c%s "build-right/M4G_BLE_BRIDGE_RIGHT_MERGED.bin" 2>/dev/null | numfmt --to=iec-i --suffix=B || echo 'N/A')

Timestamp: $(date -u +"%Y-%m-%d %H:%M:%S UTC")"

    echo ""
    echo "Commit message:"
    echo "---"
    echo "$COMMIT_MSG"
    echo "---"
    echo ""
    
    read -p "Commit and push firmware updates? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted by user"
        echo "Note: Changes are staged but not committed"
        exit 1
    fi
    
    # Commit
    git commit -m "$COMMIT_MSG"
    echo "✓ Changes committed"
    
    # Push
    echo ""
    echo "Pushing to remote repository..."
    if git push origin "$CURRENT_BRANCH"; then
        echo "✓ Successfully pushed to origin/$CURRENT_BRANCH"
    else
        echo "❌ Failed to push to remote repository"
        echo "You may need to push manually: git push origin $CURRENT_BRANCH"
        exit 1
    fi
else
    echo "ℹ️  No changes to commit"
fi

echo ""
echo "=========================================="
echo "✓ PUBLISH COMPLETE!"
echo "=========================================="
echo ""
echo "Next Steps:"
echo "  1. GitHub Actions will automatically build and deploy"
echo "  2. Monitor deployment: https://github.com/pkwdata/M4G-BLE-Bridge/actions"
echo "  3. Wait ~2-3 minutes for deployment to complete"
echo "  4. Visit: https://pkwdata.github.io/M4G-BLE-Bridge/"
echo ""
echo "Firmware variants published:"
echo "  • STANDALONE - Single keyboard mode"
echo "  • LEFT - Split keyboard left side (with BLE)"
echo "  • RIGHT - Split keyboard right side (ESP-NOW relay)"
echo ""
echo "=========================================="
