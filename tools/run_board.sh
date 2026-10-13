#!/usr/bin/env bash
# Compila, flashea y (segun el modo) captura la salida de la placa (WSL).
#   tools/run_board.sh tests [segundos] [archivo]   bateria de pruebas del kernel (RUN_SHELL=0)
#   tools/run_board.sh soak  [segundos] [archivo]   soak de 5 tareas (reporte cada 30 s)
#   tools/run_board.sh shell                        arranca en el shell y lo deja corriendo
# Imprime el RESUMEN de las pruebas y los bytes perdidos (TXBYTES).
set -e
cd "$(dirname "$0")/.."
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
MODE="${1:-tests}"
make clean >/dev/null
case "$MODE" in
    shell)
        make flash 2>&1 | grep -E "Wrote|Hash|error:|\*\*\*" || true
        ;;
    soak)
        SECS="${2:-3600}"
        make flash EXTRA="-DRUN_SHELL=0 -DRUN_SOAK=1 -DSOAK_SECONDS=${SECS}u -DSOAK_REPORT_S=30u" 2>&1 | grep -E "Wrote|Hash|error:|\*\*\*" || true
        python3 tools/capture.py "$((SECS + 200))" "${3:-/tmp/soak.bin}" --baud 57600
        ;;
    *)
        make flash EXTRA="-DRUN_SHELL=0" 2>&1 | grep -E "Wrote|Hash|error:|\*\*\*" || true
        python3 tools/capture.py "${2:-150}" "${3:-/tmp/board.bin}" --baud 57600
        ;;
esac
