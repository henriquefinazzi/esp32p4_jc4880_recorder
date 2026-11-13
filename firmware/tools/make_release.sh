#!/usr/bin/env bash
# Builds the firmware and writes the files attached to a GitHub release into firmware/release/:
#   p4-recorder-v<version>-full.bin      complete flash image (flash at 0x0; first install)
#   p4-recorder-v<version>-ota.bin       application only (updates: device, web page, ota.sh)
#   p4-recorder-v<version>-licenses.zip  license texts and third-party notices
#   SHA256SUMS.txt
# The version comes from APP_VERSION in src/app.h. Devices look for the "-ota.bin" asset
# when checking for updates, so every release must include it.
#   tools/make_release.sh            (from the firmware folder)
set -euo pipefail
cd "$(dirname "$0")/.."
PIO="${PIO:-pio}"
ESPTOOL="${ESPTOOL:-esptool}"
VERSION=$(sed -n 's/^#define APP_VERSION "\(.*\)"/\1/p' src/app.h)
[ -n "$VERSION" ] || { echo "APP_VERSION not found in src/app.h" >&2; exit 1; }
BUILD="${PLATFORMIO_WORKSPACE_DIR:-.pio}/build/esp32p4"
OUT=release
NAME="p4-recorder-v$VERSION"

"$PIO" run
rm -rf "$OUT" && mkdir -p "$OUT/licenses"
"$ESPTOOL" --chip esp32p4 merge-bin -o "$OUT/$NAME-full.bin" --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x2000 "$BUILD/bootloader.bin" 0x8000 "$BUILD/partitions.bin" \
  0xc10000 "$BUILD/ota_data_initial.bin" 0x10000 "$BUILD/firmware.bin"
cp "$BUILD/firmware.bin" "$OUT/$NAME-ota.bin"
cp ../LICENSE ../THIRD_PARTY_NOTICES.md ../licenses/* "$OUT/licenses/"
cp components/shine/COPYING "$OUT/licenses/COPYING-Shine-LGPL-2.txt"
cp components/fonts/OFL.txt "$OUT/licenses/OFL-Montserrat.txt"
(cd "$OUT" && zip -qr "$NAME-licenses.zip" licenses && rm -rf licenses && shasum -a 256 *.bin *.zip > SHA256SUMS.txt)
echo "Release files for $VERSION:"
ls -l "$OUT"
