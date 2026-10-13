#!/usr/bin/env bash
# Compila, flashea y (segun el modo) captura la salida de la placa (WSL).
#   tools/run_board.sh tests [segundos] [archivo]   bateria de pruebas del kernel (RUN_SHELL=0)
#   tools/run_board.sh shell                          arranca en el shell y lo deja corriendo
# Imprime el RESUMEN de las pruebas y los bytes perdidos (TXBYTES).
set -e
cd "$(dirname "$0")/.."
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
MODE="${1:-tests}"
make clean >/dev/null
if [ "$MODE" = "shell" ]; then
    make flash 2>&1 | grep -E "Wrote|Hash|error:|\*\*\*" || true
else
    make flash EXTRA="-DRUN_SHELL=0" 2>&1 | grep -E "Wrote|Hash|error:|\*\*\*" || true
    python3 tools/capture.py "${2:-150}" "${3:-/tmp/board.bin}" --baud 57600
fi
