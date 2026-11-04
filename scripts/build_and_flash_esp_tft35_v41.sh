#!/usr/bin/env bash
# Build the ESP-TFT35 v4.1 pendant firmware and flash the merged image to the screen.
# Usage: scripts/build_and_flash_esp_tft35_v41.sh [serial-port]
#   Port can also come from $PORT; with neither, a single USB serial device is auto-detected.
#   ESP-IDF: set IDF_DIR=<checkout> to pick one, e.g. IDF_DIR=~/esp/esp-idf-6.1. Otherwise an already
#   activated $IDF_PATH is used, else ~/esp/esp-idf-5.1.5 (the version the project pins) or ~/esp/esp-idf.
set -euo pipefail
cd "$(dirname "$0")/.."

idf_dir="${IDF_DIR:-}"
if [ -z "$idf_dir" ] && [ -z "${IDF_PATH:-}" ]; then
  for dir in "$HOME/esp/esp-idf-5.1.5" "$HOME/esp/esp-idf"; do
    if [ -f "$dir/export.sh" ]; then idf_dir="$dir"; break; fi
  done
fi
if [ -n "$idf_dir" ]; then
  [ -f "$idf_dir/export.sh" ] || { echo "No export.sh in $idf_dir" >&2; exit 1; }
  set +u
  # shellcheck disable=SC1091
  . "$idf_dir/export.sh" > /dev/null
  set -u
fi
: "${IDF_PATH:?ESP-IDF not found: set IDF_DIR, or IDF_PATH after sourcing its export.sh}"

scripts/build_esp_tft35_v41.sh

image=build/esp_tft35_v41/ESP-TFT35-v4.1-fluidnc-pendant-merged.bin
[ -f "$image" ] || { echo "Missing $image after build" >&2; exit 1; }

port="${1:-${PORT:-}}"
if [ -z "$port" ]; then
  shopt -s nullglob
  ports=(/dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM*)
  shopt -u nullglob
  case ${#ports[@]} in
    1) port="${ports[0]}" ;;
    0) echo "No USB serial device found: plug in the screen or pass the port" >&2; exit 1 ;;
    *) echo "Several serial devices found, pass one: ${ports[*]}" >&2; exit 1 ;;
  esac
fi

echo "Flashing $image to $port"
# DIO 40 MHz is required: QIO at 80 MHz makes this board boot-loop.
python -m esptool --chip esp32 --port "$port" --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 40m --flash_size 8MB \
  0x0 "$image"
