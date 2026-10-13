#!/usr/bin/env bash
# Compila, flashea y captura la salida de la placa (WSL). Uso:
#   tools/run_board.sh [segundos_maximos] [archivo_salida]
# Imprime el resumen: RESUMEN de las pruebas y los bytes perdidos (TXBYTES).
set -e
cd "$(dirname "$0")/.."
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
make clean >/dev/null
make flash 2>&1 | grep -E "Wrote|Hash|error:|\*\*\*" || true
python3 tools/capture.py "${1:-150}" "${2:-/tmp/board.bin}" --baud 57600
