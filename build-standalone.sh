#!/bin/bash
# Build script for STANDALONE firmware (single keyboard)
# Usage: ./build-standalone.sh [flash] [monitor]

set -e

BUILD_DIR="build-standalone"

echo "======================================"
echo "Building STANDALONE Firmware"
echo "======================================"
echo ""

# Remove old sdkconfig to force fresh configuration
rm -f sdkconfig sdkconfig.old

# Backup original defaults and create combined file
if [ ! -f sdkconfig.defaults.orig ]; then
    cp sdkconfig.defaults sdkconfig.defaults.orig
fi

# Create temporary combined defaults file
cat sdkconfig.defaults.orig sdkconfig.defaults.standalone > sdkconfig.defaults.tmp
mv sdkconfig.defaults.tmp sdkconfig.defaults

# Set target (will create build directory and load defaults)
idf.py -B "$BUILD_DIR" set-target esp32s3

# Reconfigure to pick up the split keyboard role settings
idf.py -B "$BUILD_DIR" reconfigure

# Build
idf.py -B "$BUILD_DIR" build

# Restore original defaults
mv sdkconfig.defaults.orig sdkconfig.defaults

echo ""
echo "======================================"
echo "STANDALONE firmware built successfully!"
echo "======================================"

# Optional: Flash and monitor
if [[ " $@ " =~ " flash " ]]; then
    echo "Flashing STANDALONE firmware..."
    idf.py -B "$BUILD_DIR" flash
fi

if [[ " $@ " =~ " monitor " ]]; then
    echo "Starting monitor..."
    idf.py -B "$BUILD_DIR" monitor
fi
