#!/bin/bash
# Build script for STANDALONE firmware (single keyboard)
# Usage: ./build-standalone.sh [flash] [monitor]

set -e

BUILD_DIR="build-standalone"

echo "======================================"
echo "Building STANDALONE Firmware"
echo "======================================"
echo ""

# Use default sdkconfig (no split keyboard options)
# The default configuration is already standalone mode
rm -f sdkconfig sdkconfig.old

# Ensure we're using clean defaults (standalone doesn't need special config)
if [ -f sdkconfig.defaults.orig ]; then
    cp sdkconfig.defaults.orig sdkconfig.defaults
fi

# Set target (will create build directory)
idf.py -B "$BUILD_DIR" set-target esp32s3

# Build
idf.py -B "$BUILD_DIR" build

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
