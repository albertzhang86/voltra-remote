#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${PORT:?Set PORT to the verified knob port, e.g. /dev/cu.usbmodem...}"
ESPTOOL=".arduino/data/packages/esp32/tools/esptool_py/5.3.1/esptool"
mkdir -p backups
BACKUP="backups/guition-before-voltra-$(date +%Y%m%d-%H%M%S).bin"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 115200 read-flash 0 0x1000000 "$BACKUP"
shasum -a 256 "$BACKUP" > "$BACKUP.sha256"
python3 tools/upload.py --port "$PORT"
printf 'Flashed. Original firmware backup: %s\n' "$BACKUP"
