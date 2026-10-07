#!/bin/bash
# build_cores3.sh - build, and optionally flash, the M5Stack CoreS3 (ILI9342C +
# FT6336, AXP2101 + AW9523B, 8 MB quad PSRAM).
#
#   tools/build_cores3.sh                   # build only
#   tools/build_cores3.sh <port>            # build and flash the app (+ bootloader, partition table)
#   tools/build_cores3.sh <port> --model    # ... and model/out/model_q4.bin at 0x290000 (once per board)
#
# <port> is the board's stable path, /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00,
# never a /dev/ttyACM<n>: those numbers shuffle between plug-ins. Flashing is the
# human's step (brief: Alan + Hermes flash together after QA); the rig only ever
# runs this with no <port> (build only).
#
# Its own build directory and sdkconfig, so the other boards' builds are
# untouched: sdkconfig.defaults, then sdkconfig.defaults.cores3 on top.
set -u
cd "$(dirname "$0")/../firmware"
BUILD=build_cores3
PORT="${1:-}"
IDF=${IDF_PATH:-$HOME/.espressif/esp-idf/v5.5}
. "$IDF/export.sh" > /dev/null 2>&1 || { echo "build_cores3: ESP-IDF export failed ($IDF)"; exit 1; }
# The defaults only fill in what an sdkconfig lacks, so a changed default never
# reaches one that already exists: start the sdkconfig afresh whenever a
# defaults file is newer than it.
if [ -f "$BUILD/sdkconfig" ] && { [ sdkconfig.defaults -nt "$BUILD/sdkconfig" ] || [ sdkconfig.defaults.cores3 -nt "$BUILD/sdkconfig" ]; }; then
  echo "build_cores3: a defaults file changed - regenerating $BUILD/sdkconfig"
  rm -f "$BUILD/sdkconfig"
fi
idf.py -B "$BUILD" -D SDKCONFIG="$BUILD/sdkconfig" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.cores3" build \
  2>&1 | grep -E "error|warning: .*display_port_spi|binary size|build complete|Project build complete"
[ "${PIPESTATUS[0]}" -eq 0 ] || { echo "build_cores3: BUILD FAILED - nothing flashed"; exit 1; }
[ -n "$PORT" ] || exit 0
case "$PORT" in /dev/serial/by-id/*) ;; *) echo "build_cores3: give the by-id path, not $PORT"; exit 1;; esac
if [[ " $* " == *" --model "* ]]; then
  python -m esptool --chip esp32s3 -p "$PORT" -b 921600 write_flash 0x290000 ../model/out/model_q4.bin 2>&1 | grep -E "Wrote|verified|rror"
fi
idf.py -B "$BUILD" -D SDKCONFIG="$BUILD/sdkconfig" -p "$PORT" -b 921600 flash 2>&1 | grep -E "verified|Hard resetting|rror" | tail -3
