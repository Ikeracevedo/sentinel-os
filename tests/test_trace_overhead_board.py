#!/usr/bin/env python3
"""Captura cinco corridas de 60 s por imagen y compara TRACE=0/1."""

import glob
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

import serial


BUILD = Path("build")
FIELDS = ("trace", "run", "ms", "elapsed", "idle", "switches", "sum",
          "max", "sq_hi", "sq_lo", "ops", "emitted", "dropped", "errors")
ROW_RE = re.compile(r"TRACE_OVERHEAD: " + r" ".join(
    rf"{field}=(\d+)" for field in FIELDS))
FULL_FIELDS = ("trace", "run", "switches", "sum", "max", "sq_hi", "sq_lo")
FULL_RE = re.compile(r"TRACE_OVERHEAD_FULL: " + r" ".join(
    rf"{field}=(\d+)" for field in FULL_FIELDS))


def path_for(trace):
    return BUILD / f"trace-overhead-{trace}.json"


def capture():
    ports = sorted(glob.glob("/dev/ttyUSB*"))
    if not ports:
        print("FAIL: WSL no ve /dev/ttyUSB*", flush=True)
        return 1

    rows = {}
    full_rows = {}
    done = False
    finished = False
    raw = bytearray()
    with serial.Serial(ports[-1], 57600, timeout=0.2) as port:
        port.setDTR(False)
        port.setRTS(True)
        time.sleep(0.1)
        port.setRTS(False)
        deadline = time.monotonic() + 330.0
        while time.monotonic() < deadline:
            raw.extend(port.read(max(1, port.in_waiting)))
            while b"\n" in raw:
                line, _, remainder = raw.partition(b"\n")
                raw = bytearray(remainder)
                decoded = line.decode(errors="replace").strip()
                match = ROW_RE.search(decoded)
                if match:
                    row = dict(zip(FIELDS, map(int, match.groups())))
                    if row["run"] not in rows:
                        rows[row["run"]] = row
                        print(decoded, flush=True)
                full_match = FULL_RE.search(decoded)
                if full_match:
                    full_row = dict(zip(FULL_FIELDS,
                                        map(int, full_match.groups())))
                    if full_row["run"] not in full_rows:
                        full_rows[full_row["run"]] = full_row
                        print(decoded, flush=True)
                if "TRACE_OVERHEAD_DONE " in decoded:
                    print(decoded, flush=True)
                    done = "TRACE_OVERHEAD_DONE PASS" in decoded
                    finished = True
                    break
            if finished:
                break

    if (not done or set(rows) != set(range(1, 6)) or
            set(full_rows) != set(range(1, 6))):
        print(f"FAIL: se recibieron {len(rows)}/5 corridas C y "
              f"{len(full_rows)}/5 corridas completas; fin={done}")
        return 1
    variant = rows[1]["trace"]
    if variant not in (0, 1):
        print("FAIL: variante inesperada")
        return 1
    for row in rows.values():
        full = full_rows[row["run"]]
        if (row["trace"] != variant or row["ms"] != 60000 or
                not 2_200_000_000 <= row["elapsed"] <= 2_600_000_000 or
                row["idle"] > row["elapsed"] or row["switches"] == 0 or
                row["ops"] < 1000 or row["dropped"] != 0 or
                row["errors"] != 0 or
                (variant == 1 and row["emitted"] == 0) or
                (variant == 0 and row["emitted"] != 0) or
                full["trace"] != variant or full["switches"] == 0 or
                abs(full["switches"] - row["switches"]) > 2):
            print(f"FAIL: corrida invalida: {row}")
            return 1
        row.update({f"full_{key}": value for key, value in full.items()
                    if key not in ("trace", "run")})

    BUILD.mkdir(exist_ok=True)
    path_for(variant).write_text(json.dumps([rows[i] for i in range(1, 6)],
                                            indent=2) + "\n")
    print(f"PASS: cinco corridas guardadas en {path_for(variant)}")
    return 0


def average_and_std(values):
    return statistics.mean(values), statistics.stdev(values)


def compare():
    try:
        off = json.loads(path_for(0).read_text())
        on = json.loads(path_for(1).read_text())
    except (OSError, ValueError) as exc:
        print(f"FAIL: faltan las dos capturas: {exc}")
        return 1
    if len(off) != 5 or len(on) != 5:
        print("FAIL: cada variante necesita cinco corridas")
        return 1

    def metrics(rows):
        idle_pct = [100.0 * row["idle"] / row["elapsed"] for row in rows]
        def switch_values(prefix):
            count_key = f"{prefix}switches"
            mean_values = [row[f"{prefix}sum"] / row[count_key]
                           for row in rows]
            maximum = [row[f"{prefix}max"] for row in rows]
            deviations = []
            for row, mean in zip(rows, mean_values):
                square_sum = ((row[f"{prefix}sq_hi"] << 32) |
                              row[f"{prefix}sq_lo"])
                variance = max(0.0, square_sum / row[count_key] - mean * mean)
                deviations.append(math.sqrt(variance))
            return mean_values, maximum, deviations

        return (idle_pct, *switch_values(""), *switch_values("full_"))

    a = metrics(off)
    b = metrics(on)
    names = ("CPU ociosa (%)", "Switch C media (ciclos)",
             "Switch C max (ciclos)", "Switch C desv (ciclos)",
             "Switch completo media (ciclos)",
             "Switch completo max (ciclos)",
             "Switch completo desv (ciclos)")
    for name, a_values, b_values in zip(names, a, b):
        am, ast = average_and_std(a_values)
        bm, bst = average_and_std(b_values)
        print(f"{name}: OFF {am:.3f} ± {ast:.3f}; "
              f"ON {bm:.3f} ± {bst:.3f}")

    off_ops = statistics.mean(row["ops"] for row in off)
    on_ops = statistics.mean(row["ops"] for row in on)
    ops_delta = 100.0 * (on_ops - off_ops) / off_ops
    overhead = statistics.mean(a[0]) - statistics.mean(b[0])
    emitted_per_second = statistics.mean(row["emitted"] / 60.0 for row in on)
    print(f"Trabajo completado: OFF {off_ops:.1f}; ON {on_ops:.1f}; "
          f"diferencia {ops_delta:+.2f}%")
    print(f"Eventos/s ON: {emitted_per_second:.1f}; "
          f"overhead CPU observado: {overhead:+.3f} puntos porcentuales")
    try:
        sizes = {}
        for variant, name in ((0, "off"), (1, "on")):
            elf = BUILD / f"trace-overhead-{name}" / "kernel.elf"
            binary = BUILD / f"trace-overhead-{name}" / "kernel.bin"
            output = subprocess.check_output(
                ["xtensa-esp32-elf-size", "-B", str(elf)], text=True)
            text_size, data_size, bss_size = map(
                int, output.splitlines()[-1].split()[:3])
            sizes[variant] = (text_size, data_size, bss_size,
                              binary.stat().st_size)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: no se pudieron leer los tamanos de ambas imagenes: {exc}")
        return 1
    print(f"ELF text: OFF {sizes[0][0]} B, ON {sizes[1][0]} B, "
          f"delta {sizes[1][0] - sizes[0][0]:+d} B")
    print(f"RAM estatica data+bss: OFF {sizes[0][1] + sizes[0][2]} B, "
          f"ON {sizes[1][1] + sizes[1][2]} B, "
          f"delta {(sizes[1][1] + sizes[1][2]) - (sizes[0][1] + sizes[0][2]):+d} B")
    print(f"Imagen .bin: OFF {sizes[0][3]} B, ON {sizes[1][3]} B, "
          f"delta {sizes[1][3] - sizes[0][3]:+d} B")
    print("Emision aislada: 48 ciclos/evento (TRACE_BOARD, medicion previa en placa)")
    if abs(ops_delta) > 5.0:
        print("FAIL: la carga no fue comparable (mas de 5% de diferencia)")
        return 1
    if overhead >= 5.0:
        print("FAIL: overhead >= 5% de CPU")
        return 1
    print("TRACE_OVERHEAD_COMPARE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(compare() if sys.argv[1:] == ["--compare"] else capture())
