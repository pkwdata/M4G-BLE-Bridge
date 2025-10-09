#!/bin/bash
# Build all firmware variants (LEFT, RIGHT, STANDALONE)
# Usage: ./build-all.sh

set -e

echo "======================================"
echo "Building ALL Firmware Variants"
echo "======================================"
echo ""

# Clean between builds to avoid configuration pollution
echo "🧹 Cleaning old builds and configs..."
rm -rf build-left build-right build-standalone sdkconfig sdkconfig.old sdkconfig.defaults.orig
echo ""

# Build LEFT side
echo "📦 Building LEFT side firmware..."
./build-left.sh
echo ""

# Clean config between variants
echo "🧹 Cleaning config before RIGHT build..."
rm -f sdkconfig sdkconfig.old
echo ""

# Build RIGHT side  
echo "📦 Building RIGHT side firmware..."
./build-right.sh
echo ""

# Clean config between variants
echo "🧹 Cleaning config before STANDALONE build..."
rm -f sdkconfig sdkconfig.old
echo ""

# Build STANDALONE
echo "📦 Building STANDALONE firmware..."
./build-standalone.sh
echo ""

echo "======================================"
echo "✅ All firmware variants built!"
echo "======================================"
echo ""
echo "Built firmware:"
echo "  - LEFT:       build-left/M4G_BLE_BRIDGE_LEFT_MERGED.bin"
echo "  - RIGHT:      build-right/M4G_BLE_BRIDGE_RIGHT_MERGED.bin"
echo "  - STANDALONE: build-standalone/M4G_BLE_BRIDGE_STANDALONE_MERGED.bin"
echo ""
