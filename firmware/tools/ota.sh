#!/usr/bin/env bash
# Builds and sends the firmware to the recorder over Wi-Fi (no cable).
#   tools/ota.sh [network-name]        e.g.: tools/ota.sh p4recorder
# Asks for the browser access password. User: RECORDER_USER (default "recorder").
# To avoid typing the password: RECORDER_NETRC=<.netrc file with "machine <name>.local login ... password ...">.
# Send only, without building: SKIP_BUILD=1 tools/ota.sh
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${1:-p4recorder}.local"
PIO="${PIO:-pio}"
BIN="${PLATFORMIO_WORKSPACE_DIR:-.pio}/build/esp32p4/firmware.bin"

if [ -z "${SKIP_BUILD:-}" ]; then
  "$PIO" run
fi
[ -f "$BIN" ] || { echo "firmware.bin not found at $BIN" >&2; exit 1; }

if [ -n "${RECORDER_NETRC:-}" ]; then
  AUTH=(--netrc-file "$RECORDER_NETRC")
else
  AUTH=(-u "${RECORDER_USER:-recorder}")
fi

echo "Sending $(du -h "$BIN" | cut -f1) to http://$HOST ..."
curl --fail-with-body -sS "${AUTH[@]}" -H "Content-Type: application/octet-stream" \
  --data-binary @"$BIN" "http://$HOST/api/firmware"
echo
echo "Sent. The recorder restarts in a few seconds."
