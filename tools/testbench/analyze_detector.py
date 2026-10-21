#!/usr/bin/env python3
"""Verifica los UART de 20 inyecciones y resume la demora del detector.

Uso: py tools/testbench/analyze_detector.py --session 20261021T042124Z
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from bridge import Bridge, Sink  # noqa: E402


class Events(Sink):
    def __init__(self) -> None:
        self.events: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] in (0x50, 0x60):
            self.events.append(data)


def replay(path: Path) -> tuple[list[dict], Bridge]:
    sink = Events()
    bridge = Bridge([sink])
    with path.open("rb") as stream:
        while chunk := stream.read(4096):
            bridge.feed(chunk)
    return sink.events, bridge


def summary(session: str, directory: Path) -> list[dict]:
    with (directory / "detector_runs.csv").open(newline="", encoding="utf-8") as stream:
        rows = [row for row in csv.DictReader(stream) if row["session"] == session]
    if len(rows) != 20 or {int(row["run"]) for row in rows} != set(range(1, 21)):
        raise ValueError("la sesión debe contener exactamente las corridas 1..20")
    commits = {row["commit"] for row in rows}
    if len(commits) != 1 or {row["git_dirty"] for row in rows} != {"false"}:
        raise ValueError("el firmware/código debe proceder de un único commit limpio")
    if {row["variant"] for row in rows} != {"trace-on"} or \
            {row["scenario"] for row in rows} != {"deadlock-2"}:
        raise ValueError("variante o escenario incompatibles")

    delays_ms: list[float] = []
    costs_us: list[float] = []
    checks = 0
    raw_bytes = 0
    for row in rows:
        if row["status"] != "PASS" or row["error"]:
            raise ValueError(f"corrida {row['run']} sin PASS: {row['error']}")
        path = directory / f"detector_{session}_r{int(row['run']):02d}.uart"
        size = path.stat().st_size
        if size != int(row["uart_bytes"]):
            raise ValueError(f"tamaño UART inconsistente en corrida {row['run']}")
        events, bridge = replay(path)
        injected = [event for event in events if event["type"] == 0x60 and event["arg0"] == 1]
        alerts = [event for event in events if event["type"] == 0x50]
        if len(injected) != 1 or len(alerts) != 2:
            raise ValueError(f"eventos incompletos en corrida {row['run']}")
        if injected[0]["cycles"] != int(row["injected_cycles"]) or \
                alerts[0]["cycles"] != int(row["alert_cycles"]):
            raise ValueError(f"marcas CCOUNT incompatibles en corrida {row['run']}")
        if bridge.decoder.crc_errors or bridge.decoder.gaps or \
                int(row["crc_errors_delta"]) or int(row["frame_gaps_delta"]) or \
                int(row["dropped_device_delta"]):
            raise ValueError(f"enlace con errores en corrida {row['run']}")
        # CCOUNT es de 32 bits y avanza a 40 MHz. El CSV original restó dos
        # marcas truncadas a microsegundos; esa resta puede diferir <1 us.
        cycles = (alerts[0]["cycles"] - injected[0]["cycles"]) & 0xffffffff
        delay_us = cycles / 40
        if abs(delay_us - int(row["delay_us"])) >= 1:
            raise ValueError(f"demora incompatible en corrida {row['run']}")
        delays_ms.append(delay_us / 1000)
        costs_us.append(int(row["last_cost_cycles"]) / 40)
        checks += int(row["detector_checks"])
        raw_bytes += size

    commit = commits.pop()
    values = [
        ("detection_rate", 100.0, 0.0, 100.0, 100.0, "%"),
        ("delay_ms", statistics.mean(delays_ms), statistics.stdev(delays_ms),
         min(delays_ms), max(delays_ms), "ms"),
        ("last_execution_cost_us", statistics.mean(costs_us), statistics.stdev(costs_us),
         min(costs_us), max(costs_us), "us"),
        ("detector_checks", float(checks), 0.0, float(checks), float(checks), "chequeos"),
        ("raw_uart_bytes", float(raw_bytes), 0.0, float(raw_bytes), float(raw_bytes), "bytes"),
    ]
    return [{"session": session, "commit": commit, "metric": metric,
             "mean": f"{mean:.6f}", "sd_between_runs": f"{sd:.6f}",
             "min": f"{minimum:.6f}", "max": f"{maximum:.6f}",
             "n": 20, "unit": unit}
            for metric, mean, sd, minimum, maximum, unit in values]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    directory = args.directory.resolve()
    rows = summary(args.session, directory)
    destination = directory / f"detector_summary_{args.session}.csv"
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    for row in rows:
        print(f"{row['metric']}: {row['mean']} ± {row['sd_between_runs']} "
              f"{row['unit']} (min={row['min']}, max={row['max']})")
    print(f"Resumen: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
