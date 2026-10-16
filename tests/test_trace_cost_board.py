#!/usr/bin/env python3
"""Captura la misma carga en TRACE=0/1 y compara ciclos en la placa."""

import glob
import json
from pathlib import Path
import re
import sys
import time

import serial


RESULT_RE = re.compile(
    r"TRACE_COST: trace=(\d+) muestras=(\d+) heap_avg=(\d+) "
    r"heap_min=(\d+) lock_avg=(\d+) lock_min=(\d+) "
    r"perdidos=(\d+) errores=(\d+)"
)
BUILD = Path("build")


def result_path(trace: int) -> Path:
    return BUILD / f"trace-cost-{trace}.json"


def capture() -> int:
    ports = sorted(glob.glob("/dev/ttyUSB*"))
    if not ports:
        print("FAIL: WSL no ve /dev/ttyUSB*")
        return 1

    data = bytearray()
    with serial.Serial(ports[-1], 57600, timeout=0.2) as port:
        port.setDTR(False)
        port.setRTS(True)
        time.sleep(0.1)
        port.setRTS(False)

        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            data.extend(port.read(max(1, port.in_waiting)))
            if b"TRACE_COST_DONE " in data:
                tail = data.split(b"TRACE_COST_DONE ", 1)[1]
                if b"\n" in tail:
                    break

    output = data.decode(errors="replace")
    match = RESULT_RE.search(output)
    if match is None or "TRACE_COST_DONE PASS" not in output:
        print("FAIL: medicion ausente o fallida; salida final:")
        print(output[-1500:])
        return 1

    keys = ("trace", "muestras", "heap_avg", "heap_min",
            "lock_avg", "lock_min", "perdidos", "errores")
    result = dict(zip(keys, map(int, match.groups())))
    if result["trace"] not in (0, 1) or result["muestras"] != 256:
        print("FAIL: variante o numero de muestras inesperado")
        return 1

    BUILD.mkdir(exist_ok=True)
    result_path(result["trace"]).write_text(json.dumps(result, indent=2) + "\n")
    print(match.group(0))
    print("TRACE_COST_DONE PASS")
    print(f"Guardado: {result_path(result['trace'])}")
    return 0


def compare() -> int:
    try:
        off = json.loads(result_path(0).read_text())
        on = json.loads(result_path(1).read_text())
    except (OSError, ValueError) as exc:
        print(f"FAIL: faltan mediciones de ambas variantes: {exc}")
        return 1

    for name in ("heap_avg", "heap_min", "lock_avg", "lock_min"):
        delta = on[name] - off[name]
        print(f"{name}: TRACE=0 {off[name]} ciclos, TRACE=1 {on[name]} ciclos, "
              f"diferencia {delta:+d} ciclos")

    if on["muestras"] != off["muestras"] or any(
        item["errores"] != 0 or item["perdidos"] != 0 for item in (on, off)
    ):
        print("FAIL: las muestras no son comparables")
        return 1
    if on["heap_avg"] <= off["heap_avg"] or on["lock_avg"] <= off["lock_avg"]:
        print("FAIL: no se observa sobrecoste en ambos caminos medidos")
        return 1

    print("TRACE_COST_COMPARE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(compare() if sys.argv[1:] == ["--compare"] else capture())
