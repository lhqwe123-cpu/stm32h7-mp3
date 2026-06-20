#!/bin/bash
# ============================================================
# MCUboot Signing Key Generation Script
#
# Usage:
#   ./generate_keys.sh
#
# Generated files:
#   - root-ec-p256.pem     : ECDSA P256 private key (keep secret!)
#   - root-ec-p256-pub.pem : ECDSA P256 public key
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KEYS_DIR="${SCRIPT_DIR}/keys"

mkdir -p "${KEYS_DIR}"

echo "=== Generating MCUboot ECDSA P256 Signing Keys ==="

# Check if imgtool is available
if ! python3 -c "import imgtool" 2>/dev/null; then
    echo "Note: imgtool not installed, using openssl to generate keys"
    echo "Consider installing: pip3 install imgtool"
fi

# Generate ECDSA P256 private key
openssl ecparam -name prime256v1 -genkey -noout -out "${KEYS_DIR}/root-ec-p256.pem"

# Export public key
openssl ec -in "${KEYS_DIR}/root-ec-p256.pem" -pubout -out "${KEYS_DIR}/root-ec-p256-pub.pem"

echo "=== Key Generation Complete ==="
echo "Private key: ${KEYS_DIR}/root-ec-p256.pem (keep it safe!)"
echo "Public key:  ${KEYS_DIR}/root-ec-p256-pub.pem"
echo ""
echo "Before building MCUboot, configure the key path in prj.conf:"
echo "  CONFIG_BOOT_SIGNATURE_KEY_FILE=\"keys/root-ec-p256.pem\""
