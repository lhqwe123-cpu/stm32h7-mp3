#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="$(basename "${SCRIPT_DIR}")"
DEFAULT_BOARD="stm32h743iit6"
BOARD="${BOARD:-$DEFAULT_BOARD}"
DEFAULT_VERSION="0.0.1"
APP_VERSION="${APP_VERSION:-$DEFAULT_VERSION}"
ZEPHYR_WS="${ZEPHYR_WS:-/home/liu/zephyrproject}"
EXPORT_BASE="${EXPORT_BASE:-/mnt/hgfs/share/zephyr_build}"
EXPORT_DIR="${EXPORT_BASE}/${PROJECT_NAME}"

usage() {
  cat <<'EOF'
Usage:
  ./build.sh [build|rebuild|clean] [board]

Commands:
  build      Incremental build (default)
  rebuild    Pristine rebuild (equivalent to west build -p always)
  clean      Remove build directory only

Environment variables:
  BOARD          Target board (default: stm32h743iit6)
  APP_VERSION    Application version for imgtool signing (default: 0.0.1)
  EXPORT_BASE    Shared folder base path (default: /mnt/hgfs/share/zephyr_build)

Output sync:
  After build/rebuild, sign image with imgtool, then copy *.hex/*.bin/*.elf/*.map to:
  ${EXPORT_BASE}/${PROJECT_NAME}

Examples:
  ./build.sh
  ./build.sh build
  ./build.sh rebuild
  ./build.sh build stm32h743iit6
  BOARD=stm32h743iit6 ./build.sh rebuild
  APP_VERSION=1.0.1 ./build.sh build
  APP_VERSION=2.0.0 BOARD=stm32h743iit6 ./build.sh rebuild
EOF
}

CMD="${1:-build}"
if [[ "${CMD}" == "-h" || "${CMD}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -ge 2 ]]; then
  BOARD="$2"
fi

if [[ "${CMD}" == "clean" ]]; then
  rm -rf "${SCRIPT_DIR}/build"
  echo "Clean done: ${SCRIPT_DIR}/build removed."
  exit 0
fi

if [[ "${CMD}" != "build" && "${CMD}" != "rebuild" ]]; then
  echo "Unsupported command: ${CMD}"
  usage
  exit 1
fi

if [[ ! -f "${ZEPHYR_WS}/.venv/bin/activate" ]]; then
  echo "Missing virtualenv: ${ZEPHYR_WS}/.venv/bin/activate"
  exit 1
fi

if [[ ! -f "${ZEPHYR_WS}/zephyr/zephyr-env.sh" ]]; then
  echo "Missing zephyr-env.sh: ${ZEPHYR_WS}/zephyr/zephyr-env.sh"
  exit 1
fi

source "${ZEPHYR_WS}/.venv/bin/activate"
source "${ZEPHYR_WS}/zephyr/zephyr-env.sh" >/dev/null

cd "${SCRIPT_DIR}"

BUILD_DIR="${SCRIPT_DIR}/build"
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  CACHED_GENERATOR="$(awk -F= '/^CMAKE_GENERATOR:INTERNAL=/{print $2; exit}' "${BUILD_DIR}/CMakeCache.txt")"
  if [[ -n "${CACHED_GENERATOR}" && "${CACHED_GENERATOR}" != "Ninja" ]]; then
    echo "Detected cached generator '${CACHED_GENERATOR}' in ${BUILD_DIR}; forcing pristine rebuild with Ninja."
    CMD="rebuild"
  fi
fi

if [[ "${CMD}" == "rebuild" ]]; then
  west build -p always -b "${BOARD}" .
else
  west build -b "${BOARD}" .
fi

# ---- Sign the application image with imgtool ----
KEY_FILE="${SCRIPT_DIR}/../mcuboot/keys/root-ec-p256.pem"
SIGNED_BIN="${BUILD_DIR}/zephyr/zephyr.signed.bin"
SIGNED_HEX="${BUILD_DIR}/zephyr/zephyr.signed.hex"

if [[ -f "${KEY_FILE}" ]]; then
  echo "Signing application image (version: ${APP_VERSION})..."
  imgtool sign --key "${KEY_FILE}" \
    --header-size 0x400 \
    --align 8 \
    --version ${APP_VERSION} \
    --slot-size 0xC0000 \
    --load-addr 0x08040000 \
    "${BUILD_DIR}/zephyr/zephyr.bin" \
    "${SIGNED_BIN}"
  echo "Signed image: ${SIGNED_BIN}"

  # Also generate signed hex for J-Link loadfile (hex has built-in addresses)
  imgtool sign --key "${KEY_FILE}" \
    --header-size 0x400 \
    --align 8 \
    --version ${APP_VERSION} \
    --slot-size 0xC0000 \
    --load-addr 0x08040000 \
    "${BUILD_DIR}/zephyr/zephyr.hex" \
    "${SIGNED_HEX}"
  echo "Signed hex: ${SIGNED_HEX}"
else
  echo "WARNING: Signing key not found at ${KEY_FILE}, skipping image signing."
fi

# ---- Pack firmware package (.fwpkg) ----
FWPKG_PACK_SCRIPT="${SCRIPT_DIR}/bootloader/scrpt/fwpkg_pack.py"
FWPKG_OUTPUT="${BUILD_DIR}/zephyr/firmware_v${APP_VERSION}.fwpkg"

if [[ -f "${FWPKG_PACK_SCRIPT}" && -f "${SIGNED_BIN}" ]]; then
  # Remove old .fwpkg files before packing new one
  find "${BUILD_DIR}" -name "*.fwpkg" -delete 2>/dev/null || true
  echo "Packing firmware package (version: ${APP_VERSION})..."
  python3 "${FWPKG_PACK_SCRIPT}" pack \
    "${SIGNED_BIN}" \
    "${APP_VERSION}" \
    "${FWPKG_OUTPUT}"
  echo "Firmware package: ${FWPKG_OUTPUT}"
else
  if [[ ! -f "${FWPKG_PACK_SCRIPT}" ]]; then
    echo "WARNING: fwpkg_pack.py not found, skipping package creation."
  fi
fi

rm -rf "${EXPORT_DIR:?}" 2>/dev/null || true
mkdir -p "${EXPORT_DIR}" 2>/dev/null || true

SYNCED_COUNT=0
while IFS= read -r -d '' rel_path; do
  mkdir -p "${EXPORT_DIR}/$(dirname "${rel_path}")"
  cp -f "${BUILD_DIR}/${rel_path}" "${EXPORT_DIR}/${rel_path}"
  SYNCED_COUNT=$((SYNCED_COUNT + 1))
done < <(
  cd "${BUILD_DIR}" && find . -type f \
    \( -iname "*.hex" -o -iname "*.bin" -o -iname "*.elf" -o -iname "*.map" -o -iname "*.fwpkg" \) \
    -printf '%P\0'
)

echo "Artifacts synced to: ${EXPORT_DIR} (${SYNCED_COUNT} files)"
