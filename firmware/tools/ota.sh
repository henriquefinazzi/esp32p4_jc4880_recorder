#!/usr/bin/env bash
# Compila e envia o firmware ao gravador pelo Wi-Fi (sem cabo).
#   tools/ota.sh [nome-na-rede]        ex.: tools/ota.sh p4recorder
# Pede a senha do acesso pelo navegador. Usuário: RECORDER_USER (padrão "recorder").
# Para não digitar a senha: RECORDER_NETRC=<arquivo .netrc com "machine <nome>.local login ... password ...">.
# Só compilar ou só enviar: SKIP_BUILD=1 tools/ota.sh
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${1:-p4recorder}.local"
PIO="${PIO:-pio}"
BIN="${PLATFORMIO_WORKSPACE_DIR:-.pio}/build/esp32p4/firmware.bin"

if [ -z "${SKIP_BUILD:-}" ]; then
  "$PIO" run
fi
[ -f "$BIN" ] || { echo "firmware.bin não encontrado em $BIN" >&2; exit 1; }

if [ -n "${RECORDER_NETRC:-}" ]; then
  AUTH=(--netrc-file "$RECORDER_NETRC")
else
  AUTH=(-u "${RECORDER_USER:-recorder}")
fi

echo "Enviando $(du -h "$BIN" | cut -f1) para http://$HOST ..."
curl --fail-with-body -sS "${AUTH[@]}" -H "Content-Type: application/octet-stream" \
  --data-binary @"$BIN" "http://$HOST/api/firmware"
echo
echo "Enviado. O gravador reinicia em alguns segundos."
