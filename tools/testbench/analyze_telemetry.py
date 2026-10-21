#!/usr/bin/env python3
"""Resume telemetria nominal/heavy de una sesion E11 sin excluir enlaces con errores.

Ejemplo: py tools/testbench/analyze_telemetry.py --session 20261021T002832Z
"""

from __future__ import annotations

import argparse
import csv
import statistics
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ("nominal", "heavy")


def session_rows(data: Path, session: str, profile: str,
                 all_rows: list[dict[str, str]]) -> list[dict[str, str]]:
    start = datetime.strptime(session, "%Y%m%dT%H%M%SZ").replace(tzinfo=timezone.utc)
    end = start + timedelta(hours=1)
    result = []
    for run in range(1, 6):
        raw = data / f"stats_{session}_trace-on_{profile}_r{run:02d}.uart"
        if not raw.is_file():
            raise ValueError(f"falta UART crudo: {raw}")
        matches = [row for row in all_rows
                   if row["variant"] == "trace-on" and row["profile"] == profile
                   and int(row["run"]) == run and row["uart_bytes"]
                   and int(row["uart_bytes"]) == raw.stat().st_size
                   and start <= datetime.fromisoformat(row["timestamp_utc"]) < end]
        if len(matches) != 1:
            raise ValueError(f"{profile}/{run}: se esperaba una fila para {raw}; "
                             f"encontradas {len(matches)}")
        row = matches[0]
        if (float(row["duration_s"]) != 10.0 or
                row["status"] not in ("PASS", "PASS_LINK_ERRORS") or
                int(row["alloc_failures"]) != 0):
            raise ValueError(f"{profile}/{run}: configuracion inesperada: {row}")
        result.append(row)
    if len({row["commit"] for row in result}) != 1:
        raise ValueError(f"{profile}: las cinco corridas tienen commits distintos")
    return result


def describe(values: list[float]) -> tuple[float, float, float]:
    return statistics.mean(values), statistics.stdev(values), sum(values)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True, help="ID UTC de los UART crudos")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--data", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    data = args.data.resolve()
    with (data / "resultados.csv").open(newline="", encoding="utf-8") as stream:
        all_rows = list(csv.DictReader(stream))
    result_rows = []
    commits = set()
    for profile in PROFILES:
        rows = session_rows(data, args.session, profile, all_rows)
        commits.update(row["commit"] for row in rows)
        metrics = {
            "events_emitted": ([float(row["events_emitted_delta"]) for row in rows], "eventos/10 s"),
            "events_dropped": ([float(row["events_dropped_delta"]) for row in rows], "eventos/10 s"),
            "dropped_pct": ([100 * int(row["events_dropped_delta"]) /
                              max(1, int(row["events_emitted_delta"]) +
                                  int(row["events_dropped_delta"])) for row in rows], "%"),
            "uart_occupancy_pct": ([100 * int(row["uart_bytes"]) * 10 /
                                    (float(row["duration_s"]) * args.baud)
                                    for row in rows], "% del baud teórico"),
            "crc_errors": ([float(row["crc_errors_delta"]) for row in rows], "errores/10 s"),
            "frame_gaps": ([float(row["frame_gaps_delta"]) for row in rows], "tramas/10 s"),
        }
        for metric, (values, unit) in metrics.items():
            mean, sd, total = describe(values)
            result_rows.append({"session": args.session, "profile": profile,
                                "metric": metric, "mean": f"{mean:.9f}",
                                "sd_between_runs": f"{sd:.9f}",
                                "sum": f"{total:.0f}" if metric not in
                                       ("dropped_pct", "uart_occupancy_pct") else "",
                                "n": 5, "unit": unit})
    if len(commits) != 1:
        raise ValueError("nominal y heavy provienen de commits diferentes")
    destination = data / f"telemetry_summary_{args.session}.csv"
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("session", "commit", "profile",
            "metric", "mean", "sd_between_runs", "sum", "n", "unit"))
        writer.writeheader()
        writer.writerows({"commit": next(iter(commits)), **row} for row in result_rows)
    for profile in PROFILES:
        by_metric = {row["metric"]: row for row in result_rows if row["profile"] == profile}
        emitted = by_metric["events_emitted"]
        uart = by_metric["uart_occupancy_pct"]
        print(f"{profile}: {float(emitted['mean']):.1f} eventos/10 s, "
              f"{by_metric['events_dropped']['sum']} descartados por placa, "
              f"UART {float(uart['mean']):.2f} +/- "
              f"{float(uart['sd_between_runs']):.2f}% del baud; "
              f"CRC={by_metric['crc_errors']['sum']} "
              f"huecos={by_metric['frame_gaps']['sum']}")
    print(f"Resumen: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
