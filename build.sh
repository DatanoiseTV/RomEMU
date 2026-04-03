#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ---- Check prerequisites ----

if [ -z "${IDF_PATH:-}" ]; then
    error "IDF_PATH is not set. Source the ESP-IDF export script first:"
    echo "  . \$IDF_PATH/export.sh"
    exit 1
fi

if ! command -v idf.py &>/dev/null; then
    error "idf.py not found in PATH. Source the ESP-IDF export script first."
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    error "python3 not found."
    exit 1
fi

# ---- Check required frontend files ----

FRONTEND_DIR="$SCRIPT_DIR/frontend"
REQUIRED_FILES=("index.html" "style.css" "app.js" "htmx.min.js")

for f in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$FRONTEND_DIR/$f" ]; then
        error "Missing frontend file: frontend/$f"
        exit 1
    fi
done

# ---- Step 1: Embed frontend ----

info "Embedding frontend files..."
python3 tools/embed_frontend.py "$FRONTEND_DIR" main/embedded_files.h

if [ ! -f main/embedded_files.h ]; then
    error "Failed to generate embedded_files.h"
    exit 1
fi

# Validate embedded_files.h has all expected symbols
for sym in index_html_gz style_css_gz app_js_gz htmx_min_js_gz; do
    if ! grep -q "$sym" main/embedded_files.h; then
        error "embedded_files.h is missing symbol: $sym"
        exit 1
    fi
done
info "Frontend embedded OK"

# ---- Step 2: Validate source files ----

info "Checking source files..."
SOURCES=(
    main/main.c
    main/spi_flash_emu.c
    main/spi_flash_emu.h
    main/spi_flash_commands.h
    main/i2c_eeprom_emu.c
    main/i2c_eeprom_emu.h
    main/rom_store.c
    main/rom_store.h
    main/access_log.c
    main/access_log.h
    main/wifi_manager.c
    main/wifi_manager.h
    main/web_server.c
    main/web_server.h
    main/web_handlers.c
    main/sse_manager.c
    main/sse_manager.h
    main/gpio_control.c
    main/gpio_control.h
    main/lz4_block.c
    main/lz4_block.h
    main/compressed_store.c
    main/compressed_store.h
    main/romemu_common.h
    main/pin_config.h
    main/embedded_files.h
    main/CMakeLists.txt
    CMakeLists.txt
    sdkconfig.defaults
    partitions.csv
)

MISSING=0
for f in "${SOURCES[@]}"; do
    if [ ! -f "$f" ]; then
        error "Missing: $f"
        MISSING=1
    fi
done
if [ "$MISSING" -ne 0 ]; then
    exit 1
fi
info "All source files present"

# ---- Step 3: Set target if needed ----

if [ ! -f sdkconfig ] || ! grep -q 'CONFIG_IDF_TARGET="esp32s3"' sdkconfig 2>/dev/null; then
    info "Setting target to esp32s3..."
    idf.py set-target esp32s3
fi

# ---- Step 4: Build ----

info "Building firmware..."
idf.py build

# ---- Step 5: Report ----

echo ""
info "=== Build successful ==="
echo ""

if [ -f build/esp32-romemu.bin ]; then
    BIN_SIZE=$(stat -f%z build/esp32-romemu.bin 2>/dev/null || stat -c%s build/esp32-romemu.bin 2>/dev/null)
    info "Firmware: build/esp32-romemu.bin ($(( BIN_SIZE / 1024 )) KB)"
fi

echo ""
info "Flash with:  idf.py -p /dev/ttyUSB0 flash monitor"
info "Or:          idf.py -p /dev/cu.usbserial-* flash monitor"
echo ""
