#!/bin/bash
# ============================================================
# MCUboot Bootloader Build Script (STM32H743IIT6)
#
# Usage:
#   ./build_mcuboot.sh [clean]
#
# Prerequisites:
#   1. Zephyr SDK installed
#   2. Signing keys generated (run generate_keys.sh)
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_NAME="mcuboot"
BUILD_DIR="${SCRIPT_DIR}/build"
BOARD="stm32h743iit6"

echo "=== Building MCUboot Bootloader ==="
echo "Project dir: ${SCRIPT_DIR}"
echo "Target board: ${BOARD}"

# Clean old build (optional)
if [ "$1" == "clean" ]; then
    echo "Cleaning old build directory..."
    rm -rf "${BUILD_DIR}"
fi

# Create build directory
mkdir -p "${BUILD_DIR}"

# Run CMake configuration
echo ""
echo "--- CMake Configuration ---"
cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" \
    -DBOARD="${BOARD}" \
    -DCMAKE_BUILD_TYPE=MinSizeRel

# Build
echo ""
echo "--- Building ---"
cmake --build "${BUILD_DIR}" -- -j$(nproc)

echo ""
echo "=== Build Complete ==="
echo "Bootloader firmware: ${BUILD_DIR}/zephyr/zephyr.hex"
echo "Bootloader firmware: ${BUILD_DIR}/zephyr/zephyr.bin"
