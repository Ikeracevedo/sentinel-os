#!/usr/bin/env python3
"""Comprueba una hora nominal de E11 desde CSV y UART crudo.

Uso: py tools/testbench/analyze_false_positives.py --session YYYYMMDDTHHMMSSZ
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from bridge import Bridge, Sink  # noqa: E402


class Alerts(Sink):
    def __init__(self) -> None:
        self.event_alerts = 0
        self.frame_alerts = 0

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] == 0x50:
            self.event_alerts += 1
        elif kind == "alert":
            self.frame_alerts += 1


def analyze(session: str, directory: Path) -> dict:
    with (directory / "false_positives_runs.csv").open(newline="", encoding="utf-8") as stream:
        rows = [row for row in csv.DictReader(stream) if row["session"] == session]
    minutes = [row for row in rows if row["phase"] == "minute"]
    finals = [row for row in rows if row["phase"] == "final"]
    ends = [row for row in rows if row["phase"] == "end"]
    if len(minutes) < 59 or len(finals) != 1 or len(ends) != 1:
        raise ValueError("faltan controles de la hora, control final o cierre")
    if len({row["commit"] for row in rows}) != 1:
        raise ValueError("el commit cambió durante la sesión")
    if {row["duration_target_s"] for row in rows} != {"3600"}:
        raise ValueError("la duración planificada no fue 3600 s")
    if any(row["status"] != "PASS" or row["error"] for row in rows):
        raise ValueError("hay controles fallidos")
    final = finals[0]
    if float(final["elapsed_s"]) < 3600:
        raise ValueError("la observación terminó antes de una hora")
    for field in ("deadlock_alerts_delta", "frag_alerts_delta", "starve_alerts_delta",
                  "event_alerts", "frame_alerts", "alloc_failures", "dropped_device_delta"):
        if any(int(row[field]) != 0 for row in (*minutes, final)):
            raise ValueError(f"{field} distinto de cero")
    if any(int(row["load_iterations"]) >= int(next_row["load_iterations"])
           for row, next_row in zip(minutes, minutes[1:])):
        raise ValueError("la carga dejó de avanzar")
    if int(final["detector_checks_delta"]) < 7195:
        raise ValueError("el detector no ejecutó suficientes chequeos")
    raw = directory / f"false_positives_{session}.uart"
    size = raw.stat().st_size
    if size != int(ends[0]["uart_bytes"]):
        raise ValueError("tamaño del UART distinto del CSV")
    alerts = Alerts()
    bridge = Bridge([alerts])
    with raw.open("rb") as stream:
        while chunk := stream.read(65536):
            bridge.feed(chunk)
    if alerts.event_alerts or alerts.frame_alerts:
        raise ValueError("el UART crudo contiene una alerta")
    # Los errores del enlace se conservan y reportan; los contadores de
    # alerta en la placa permiten detectar alertas aunque se pierda un frame.
    return {"session": session, "commit": final["commit"],
            "duration_s": final["elapsed_s"],
            "load_iterations": final["load_iterations"],
            "detector_checks": final["detector_checks_delta"],
            "false_alerts": 0, "raw_uart_bytes": size,
            "crc_errors_live": final["crc_errors_delta"],
            "frame_gaps_live": final["frame_gaps_delta"],
            "crc_errors_replay": bridge.decoder.crc_errors,
            "frame_gaps_replay": bridge.decoder.gaps,
            "device_dropped": final["dropped_device_delta"],
            "status": "PASS"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    directory = args.directory.resolve()
    row = analyze(args.session, directory)
    path = directory / f"false_positives_summary_{args.session}.csv"
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=row.keys())
        writer.writeheader()
        writer.writerow(row)
    print(f"PASS: {row['duration_s']} s, {row['detector_checks']} chequeos, "
          f"0 alertas falsas; CRC={row['crc_errors_live']} "
          f"huecos={row['frame_gaps_live']}; resumen={path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
