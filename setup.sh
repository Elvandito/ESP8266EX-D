#!/usr/bin/env bash
#
# ESP8266 Deauther + Beacon Spam - Installer
# For Linux / macOS. Usage: bash install.sh
set -euo pipefail

# ---------- CONFIG ----------
PATCH_FILE="$(cd "$(dirname "$0")" && pwd)/libnet80211.a.patched"
PLATFORMIO="espressif8266@2.6.3"
BAUD=115200
FLASH_ADDR=0x0
# ----------------------------

echo "============================================"
echo "  ESP8266 Deauther + Beacon Spam - Installer"
echo "============================================"

# ---------- Step 1: Check Python 3 ----------
echo
echo "[1/5] Checking Python 3..."
if command -v python3 >/dev/null 2>&1; then
  PY_CMD="python3"
elif command -v python >/dev/null 2>&1 && python --version 2>&1 | grep -q "^Python 3"; then
  PY_CMD="python"
else
  echo "[ERROR] Python 3 not found."
  echo "Install it via your package manager, e.g.:"
  echo "  Debian/Ubuntu: sudo apt install python3 python3-pip"
  echo "  macOS:         brew install python"
  exit 1
fi
echo "Using: $PY_CMD"
$PY_CMD --version

# ---------- Step 2: Install packages ----------
echo
echo "[2/5] Installing PlatformIO and esptool..."
$PY_CMD -m pip install --user -U platformio esptool
echo "Packages installed."
PLATFORMIO_CMD="$PY_CMD -m platformio"
ESPTool_CMD="$PY_CMD -m esptool"

# ---------- Step 3: SDK patch ----------
echo
echo "[3/5] Patching ESP8266 SDK (libnet80211.a)..."
SDK_LIB=""
for d in "$HOME"/.platformio/packages/framework-arduinoespressif8266*; do
  candidate="$d/tools/sdk/lib/NONOSDK22x_190703/libnet80211.a"
  if [ -f "$candidate" ]; then
    SDK_LIB="$candidate"
    break
  fi
done
if [ -z "$SDK_LIB" ]; then
  echo "[ERROR] ESP8266 framework not found. Run this once first:"
  echo "  $PLATFORMIO_CMD pkg install -g -p $PLATFORMIO"
  exit 1
fi
if [ ! -f "$SDK_LIB.stock.bak" ]; then
  cp "$SDK_LIB" "$SDK_LIB.stock.bak"
  echo "Backup created: libnet80211.a.stock.bak"
else
  echo "Backup already exists, skipping."
fi
if [ ! -f "$PATCH_FILE" ]; then
  echo "[ERROR] Patched library not found: $PATCH_FILE"
  echo "Download it from:"
  echo "  https://raw.githubusercontent.com/SpacehuhnTech/arduino-esp8266/deauther/tools/sdk/lib/NONOSDK22x_190703/libnet80211.a"
  echo "and save it as libnet80211.a.patched next to this script."
  exit 1
fi
if [ "$(stat -c%s "$PATCH_FILE" 2>/dev/null || stat -f%z "$PATCH_FILE")" -lt 100000 ]; then
  echo "[ERROR] $PATCH_FILE looks wrong (too small)."
  exit 1
fi
cp "$PATCH_FILE" "$SDK_LIB"
echo "SDK patched: $SDK_LIB"

# ---------- Step 4: Build ----------
echo
echo "[4/5] Building firmware..."
$PLATFORMIO_CMD run
echo "Build complete: .pio/build/d1mini/firmware.bin"

FIRMWARE="firmware.bin"
if [ -f ".pio/build/d1mini/firmware.bin" ]; then
  FIRMWARE=".pio/build/d1mini/firmware.bin"
fi

# ---------- Step 5: Flash ----------
echo
echo "[5/5] Flashing firmware to device..."
echo
echo "Detecting serial ports..."
if ls /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* >/dev/null 2>&1; then
  ls /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* 2>/dev/null
else
  echo "  (none detected)"
fi
read -rp "Enter serial port (e.g. /dev/ttyUSB0): " PORT
if [ -z "$PORT" ]; then
  echo "[ERROR] No port given."
  exit 1
fi
read -rp "Flash to $PORT? [Y/n]: " CONFIRM
if [ "$CONFIRM" != "Y" ] && [ "$CONFIRM" != "y" ] && [ -n "$CONFIRM" ]; then
  echo "Aborted."
  exit 0
fi
$ESPTool_CMD --port "$PORT" --baud "$BAUD" write_flash "$FLASH_ADDR" "$FIRMWARE"
echo
echo "============================================"
echo "  Done! Open a serial monitor at 115200 baud."
echo "  Commands: /scan  /status  /help"
echo "============================================"