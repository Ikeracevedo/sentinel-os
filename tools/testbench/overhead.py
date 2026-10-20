#!/usr/bin/env python3
"""Captura en Windows las cinco corridas de TRACE_OVERHEAD_BOARD_TEST.

La imagen de benchmark se compila y flashea en WSL. Despues de devolver el
CH340 a Windows, ejecutar por ejemplo:
    py tools/testbench/overhead.py --port COM8 --variant trace-on
Tambien acepta --flash --busid 1-5 para hacer el traspaso sin pausa manual.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from run import ROOT, git_commit, git_dirty

FIELDS = ("trace", "run", "ms", "elapsed", "idle", "switches", "sum",
          "max", "sq_hi", "sq_lo", "ops", "emitted", "dropped", "errors")
FULL_FIELDS = ("trace", "run", "switches", "sum", "max", "sq_hi", "sq_lo")


def pattern(prefix: str, fields: tuple[str, ...]) -> re.Pattern[str]:
    return re.compile(re.escape(prefix) + r" " + r" ".join(
        rf"{field}=(\d+)" for field in fields))


ROW_RE = pattern("TRACE_OVERHEAD:", FIELDS)
FULL_RE = pattern("TRACE_OVERHEAD_FULL:", FULL_FIELDS)
CSV_FIELDS = ("session", "timestamp_utc", "commit", "git_dirty", "variant",
              "profile", *FIELDS, *("full_" + key for key in FULL_FIELDS
                                          if key not in ("trace", "run")))


def parse_line(line: str) -> tuple[str, dict[str, int]] | None:
    for label, regex, fields in (("row", ROW_RE, FIELDS),
                                  ("full", FULL_RE, FULL_FIELDS)):
        match = regex.search(line)
        if match:
            return label, dict(zip(fields, map(int, match.groups())))
    return None


def validate(rows: dict[int, dict[str, int]],
             full: dict[int, dict[str, int]], variant: str,
             done: bool) -> list[dict[str, int]]:
    expected = int(variant == "trace-on")
    if not done or set(rows) != set(range(1, 6)) or set(full) != set(range(1, 6)):
        raise RuntimeError(f"captura incompleta: resumen={sorted(rows)}, "
                           f"completo={sorted(full)}, fin={done}")
    result = []
    for run in range(1, 6):
        row, detail = rows[run], full[run]
        if (row["trace"] != expected or detail["trace"] != expected or
                row["run"] != detail["run"] or row["ms"] != 60_000 or
                not 2_200_000_000 <= row["elapsed"] <= 2_600_000_000 or
                row["idle"] > row["elapsed"] or row["switches"] == 0 or
                detail["switches"] == 0 or
                abs(detail["switches"] - row["switches"]) > 2 or
                row["ops"] < 1000 or row["errors"] != 0 or
                (expected == 0 and row["emitted"] != 0) or
                (expected == 1 and row["emitted"] == 0)):
            raise RuntimeError(f"corrida {run} invalida: {row}; {detail}")
        result.append({**row, **{"full_" + key: value
                                 for key, value in detail.items()
                                 if key not in ("trace", "run")}})
    return result


def capture(port_name: str, baud: int, variant: str, output: Path,
            timeout: float) -> list[dict[str, int]]:
    import serial

    output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    prefix = output / f"overhead_{stamp}_{variant}"
    print(f"Capturando {variant} en {port_name}; crudo={prefix}.uart", flush=True)
    rows: dict[int, dict[str, int]] = {}
    full: dict[int, dict[str, int]] = {}
    done = False
    buffer = bytearray()
    serial_port = serial.Serial(port=None, baudrate=baud, timeout=0.2)
    serial_port.dtr = False
    serial_port.rts = False
    serial_port.port = port_name
    serial_port.open()
    try:
        with prefix.with_suffix(".uart").open("wb") as raw, \
                prefix.with_suffix(".csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=("timestamp_utc", "line"))
            writer.writeheader()
            serial_port.setDTR(False)
            serial_port.setRTS(True)
            time.sleep(0.1)
            serial_port.setRTS(False)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                chunk = serial_port.read(max(1, serial_port.in_waiting))
                if not chunk:
                    continue
                raw.write(chunk)
                raw.flush()
                buffer.extend(chunk)
                while b"\n" in buffer:
                    line_bytes, _, tail = buffer.partition(b"\n")
                    buffer = bytearray(tail)
                    line = line_bytes.decode("ascii", errors="replace").strip()
                    writer.writerow({"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                     "line": line})
                    stream.flush()
                    parsed = parse_line(line)
                    if parsed is not None:
                        kind, item = parsed
                        target = rows if kind == "row" else full
                        previous = target.get(item["run"])
                        if previous is not None and previous != item:
                            raise RuntimeError(f"copias UART contradictorias: {previous}; {item}")
                        if previous is None:
                            target[item["run"]] = item
                            print(f"{kind} {item['run']}/5 recibido", flush=True)
                    if "TRACE_OVERHEAD_DONE " in line:
                        done = "TRACE_OVERHEAD_DONE PASS" in line
                        if not done:
                            raise RuntimeError(f"la placa reporto fallo: {line}")
                        break
                if done:
                    break
    finally:
        serial_port.close()
    return validate(rows, full, variant, done)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--variant", choices=("trace-on", "trace-off"), required=True)
    parser.add_argument("--baud", type=int, default=57600)
    parser.add_argument("--timeout", type=float, default=380)
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    parser.add_argument("--flash", action="store_true",
                        help="compilar/flashear en WSL y devolver el USB a Windows")
    parser.add_argument("--busid", help="BUSID de usbipd, por ejemplo 1-5")
    parser.add_argument("--distro", default="ubuntu", help="distribucion WSL")
    args = parser.parse_args()
    if args.timeout < 310:
        parser.error("--timeout debe permitir cinco ventanas de 60 s")
    if args.flash and not args.busid:
        parser.error("--flash requiere --busid")
    if args.flash:
        from flash import flash_board
        flash_board(busid=args.busid, port=args.port, distro=args.distro,
                    variant=args.variant, benchmark=True)
    metadata = {"session": datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"),
                "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "commit": git_commit(), "git_dirty": str(git_dirty()).lower(),
                "variant": args.variant, "profile": "overhead-standard"}
    try:
        rows = capture(args.port, args.baud, args.variant, args.output.resolve(),
                       args.timeout)
    except Exception as error:
        print(f"FAIL: {error}; archivos UART/CSV crudos conservados", file=sys.stderr)
        return 1
    result_path = args.output.resolve() / "overhead_runs.csv"
    with result_path.open("a", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        if stream.tell() == 0:
            writer.writeheader()
        for row in rows:
            writer.writerow({**metadata, **row})
    print(f"PASS: cinco corridas completas; {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
