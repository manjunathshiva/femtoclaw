#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_NAME="femtoclaw.bin"
PORT="${OTA_PORT:-8080}"

# Build firmware (incremental — only rebuilds changed files)
IDF_VERSION="${IDF_VERSION:-v5.5.2}"
ESP_ROOT="${ESP_ROOT:-$HOME/.espressif}"
DEFAULT_IDF_DIR="$ESP_ROOT/esp-idf-$IDF_VERSION"
IDF_DIR="${IDF_DIR:-${IDF_PATH:-$DEFAULT_IDF_DIR}}"

if [[ ! -f "$IDF_DIR/export.sh" ]]; then
    echo "ESP-IDF not found at: $IDF_DIR" >&2
    echo "Run scripts/setup_idf_macos.sh first, or set IDF_DIR/IDF_PATH." >&2
    exit 1
fi

# shellcheck source=/dev/null
. "$IDF_DIR/export.sh"

echo "=== Building firmware (incremental) ==="
cd "$PROJECT_ROOT"
idf.py build

# Find the .bin in build/
BIN_PATH="$PROJECT_ROOT/build/$BIN_NAME"
if [[ ! -f "$BIN_PATH" ]]; then
    echo "ERROR: $BIN_PATH not found after build." >&2
    exit 1
fi

echo ""
echo "=== Firmware ready ==="
echo "  File: $BIN_PATH"
echo "  Size: $(wc -c < "$BIN_PATH" | tr -d ' ') bytes"

# Detect local IP
LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null || true)
if [[ -z "$LOCAL_IP" ]]; then
    LOCAL_IP=$(ipconfig getifaddr en1 2>/dev/null || true)
fi
if [[ -z "$LOCAL_IP" ]]; then
    LOCAL_IP="<YOUR_IP>"
    echo "  WARNING: Could not detect local IP. Replace <YOUR_IP> below."
fi

echo ""
echo "=== Run this on the device ==="
echo "  ota http://$LOCAL_IP:$PORT/$BIN_NAME"
echo ""
echo "=== Starting HTTP server on port $PORT ==="
cd "$PROJECT_ROOT/build"
python3 -m http.server "$PORT"
