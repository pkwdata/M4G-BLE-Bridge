#!/bin/bash
# Verification script for split keyboard implementation
# Checks that all required files and configurations are present

set -e

echo "======================================"
echo "Split Keyboard Setup Verification"
echo "======================================"
echo ""

ERRORS=0
WARNINGS=0

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

check_file() {
    if [ -f "$1" ]; then
        echo -e "${GREEN}✓${NC} Found: $1"
    else
        echo -e "${RED}✗${NC} Missing: $1"
        ((ERRORS++))
    fi
}

check_dir() {
    if [ -d "$1" ]; then
        echo -e "${GREEN}✓${NC} Found directory: $1"
    else
        echo -e "${RED}✗${NC} Missing directory: $1"
        ((ERRORS++))
    fi
}

check_string_in_file() {
    if grep -q "$2" "$1" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Found '$2' in $1"
    else
        echo -e "${YELLOW}⚠${NC} Warning: '$2' not found in $1"
        ((WARNINGS++))
    fi
}

echo "Checking ESP-NOW Component..."
check_dir "components/m4g_espnow"
check_file "components/m4g_espnow/include/m4g_espnow.h"
check_file "components/m4g_espnow/m4g_espnow.c"
check_file "components/m4g_espnow/CMakeLists.txt"
echo ""

echo "Checking Main Application Files..."
check_file "main/main.c"
check_file "main/main_left.c"
check_file "main/main_right.c"
check_file "main/CMakeLists.txt"
echo ""

echo "Checking Build Scripts..."
check_file "build-left.sh"
check_file "build-right.sh"
if [ -f "build-left.sh" ]; then
    if [ -x "build-left.sh" ]; then
        echo -e "${GREEN}✓${NC} build-left.sh is executable"
    else
        echo -e "${YELLOW}⚠${NC} build-left.sh is not executable (run: chmod +x build-left.sh)"
        ((WARNINGS++))
    fi
fi
if [ -f "build-right.sh" ]; then
    if [ -x "build-right.sh" ]; then
        echo -e "${GREEN}✓${NC} build-right.sh is executable"
    else
        echo -e "${YELLOW}⚠${NC} build-right.sh is not executable (run: chmod +x build-right.sh)"
        ((WARNINGS++))
    fi
fi
echo ""

echo "Checking Configuration Files..."
check_file "sdkconfig.defaults.left"
check_file "sdkconfig.defaults.right"
check_file "Kconfig"
echo ""

echo "Checking Documentation..."
check_file "SPLIT_KEYBOARD_SETUP.md"
check_file "QUICK_START_SPLIT.md"
check_file "SPLIT_KEYBOARD_CHANGES.md"
check_file "IMPLEMENTATION_COMPLETE.md"
echo ""

echo "Checking Kconfig Options..."
check_string_in_file "Kconfig" "M4G_SPLIT_ROLE_LEFT"
check_string_in_file "Kconfig" "M4G_SPLIT_ROLE_RIGHT"
check_string_in_file "Kconfig" "M4G_SPLIT_ROLE_STANDALONE"
check_string_in_file "Kconfig" "M4G_ESPNOW_CHANNEL"
check_string_in_file "Kconfig" "M4G_ESPNOW_PMK"
echo ""

echo "Checking Main CMakeLists.txt Integration..."
check_string_in_file "main/CMakeLists.txt" "CONFIG_M4G_SPLIT_ROLE_LEFT"
check_string_in_file "main/CMakeLists.txt" "main_left.c"
check_string_in_file "main/CMakeLists.txt" "main_right.c"
check_string_in_file "main/CMakeLists.txt" "m4g_espnow"
echo ""

echo "Checking ESP-NOW API..."
check_string_in_file "components/m4g_espnow/include/m4g_espnow.h" "m4g_espnow_init"
check_string_in_file "components/m4g_espnow/include/m4g_espnow.h" "m4g_espnow_send_hid_report"
check_string_in_file "components/m4g_espnow/include/m4g_espnow.h" "m4g_espnow_is_peer_connected"
echo ""

echo "Checking Left Main Integration..."
check_string_in_file "main/main_left.c" "m4g_espnow_init"
check_string_in_file "main/main_left.c" "espnow_rx_cb"
check_string_in_file "main/main_left.c" "M4G_ESPNOW_ROLE_LEFT"
check_string_in_file "main/main_left.c" "m4g_ble_init"
echo ""

echo "Checking Right Main Integration..."
check_string_in_file "main/main_right.c" "m4g_espnow_init"
check_string_in_file "main/main_right.c" "m4g_espnow_send_hid_report"
check_string_in_file "main/main_right.c" "M4G_ESPNOW_ROLE_RIGHT"
echo ""

echo "======================================"
echo "Verification Complete"
echo "======================================"
echo ""

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ All checks passed!${NC}"
    echo ""
    echo "You can now build the firmware:"
    echo "  Left side:  ./build-left.sh"
    echo "  Right side: ./build-right.sh"
    echo ""
    echo "See QUICK_START_SPLIT.md for next steps."
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠ Verification completed with $WARNINGS warnings${NC}"
    echo ""
    echo "Warnings are non-critical but should be addressed."
    exit 0
else
    echo -e "${RED}✗ Verification failed with $ERRORS errors and $WARNINGS warnings${NC}"
    echo ""
    echo "Please resolve errors before building."
    exit 1
fi
