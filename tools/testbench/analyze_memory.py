#!/usr/bin/env python3
"""Valida y grafica una bateria E11 de cinco corridas leak y frag.

Ejemplo: py tools/testbench/analyze_memory.py --session 20261021T011525Z
El analisis nunca elimina muestras ni valores atipicos.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics
from datetime import datetime
from pathlib import Path

from run import ROOT, STATS_FIELDS
from bridge import Bridge, Sink


class StatsCollector(Sink):
    def __init__(self) -> None:
        self.samples: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "stats":
            self.samples.append(data)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def read_run(data: Path, session: str, profile: str, run: int,
             summaries: list[dict[str, str]]) -> dict:
    path = data / f"memory_{session}_trace-on_{profile}_r{run:02d}.csv"
    uart = path.with_suffix(".uart")
    samples = read_csv(path)
    if len(samples) < 2 or any(row["profile"] != profile or
                               int(row["run"]) != run or
                               int(row["trace_enabled"]) != 1 for row in samples):
        raise ValueError(f"muestras incompletas o etiquetas incorrectas: {path}")
    uptimes = [int(row["uptime_ticks"]) for row in samples]
    if any(b <= a for a, b in zip(uptimes, uptimes[1:])):
        raise ValueError(f"uptime no crece: {path}")
    last = datetime.fromisoformat(samples[-1]["timestamp_utc"])
    matching = [row for row in summaries if row["profile"] == profile and
                int(row["run"]) == run and 0 <=
                (datetime.fromisoformat(row["timestamp_utc"]) - last).total_seconds() < 30]
    if len(matching) != 1:
        raise ValueError(f"se esperaba un resumen unico para {path}; encontrados {len(matching)}")
    summary = matching[0]
    if (summary["status"] not in ("PASS", "PASS_LINK_ERRORS") or
            summary["variant"] != "trace-on" or
            summary["commit"] != samples[0]["commit"] or
            any(row["commit"] != summary["commit"] for row in samples) or
            any(row["git_dirty"] != summary["git_dirty"] for row in samples) or
            int(summary["stats_samples"]) != len(samples) or
            int(summary["uart_bytes"]) != uart.stat().st_size or
            int(summary["post_free"]) != int(summary["baseline_free"]) or
            int(summary["post_largest"]) != int(summary["baseline_largest"]) or
            int(summary["post_blocks"]) != int(summary["baseline_blocks"])):
        raise ValueError(f"resumen no concuerda o heap sin restaurar: {path}")
    free_values = [int(row["heap_free"]) for row in samples]
    largest_values = [int(row["heap_largest"]) for row in samples]
    frag_values = [100 * (1 - largest / free) for free, largest in
                   zip(free_values, largest_values) if free]
    if (int(summary["min_free"]) != min(free_values) or
            int(summary["min_largest"]) != min(largest_values) or
            int(summary["last_free"]) != free_values[-1] or
            int(summary["last_largest"]) != largest_values[-1] or
            not frag_values or not math.isclose(
                float(summary["max_fragmentation_pct"]), max(frag_values),
                abs_tol=1e-6)):
        raise ValueError(f"estadisticas derivadas no concuerdan: {path}")
    collector = StatsCollector()
    bridge = Bridge((collector,))
    with uart.open("rb") as stream:
        while chunk := stream.read(4096):
            bridge.feed(chunk)
    if len(collector.samples) != len(samples) or any(
            int(row[field]) != decoded[field]
            for row, decoded in zip(samples, collector.samples)
            for field in STATS_FIELDS):
        raise ValueError(f"UART crudo no reproduce las muestras: {uart}")
    if (bridge.decoder.crc_errors > int(summary["crc_errors_delta"]) or
            bridge.decoder.gaps > int(summary["frame_gaps_delta"])):
        raise ValueError(f"errores del UART mayores que el resumen: {uart}")
    return {"samples": samples, "summary": summary, "uart": uart,
            "raw_crc": bridge.decoder.crc_errors,
            "raw_gaps": bridge.decoder.gaps}


def sample_value(row: dict[str, str], profile: str) -> float:
    free = int(row["heap_free"])
    if profile == "leak":
        return float(free)
    return 100 * (1 - int(row["heap_largest"]) / free) if free else 0.0


def plot_runs(runs: list[dict], profile: str, destination: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(9, 4.8))
    color = "#2b6b9a" if profile == "leak" else "#b45b35"
    common = min(len(run["samples"]) for run in runs)
    shared = [[] for _ in range(common)]
    times = [[] for _ in range(common)]
    for index, run in enumerate(runs):
        rows = run["samples"]
        start = int(rows[0]["uptime_ticks"])
        x = [(int(row["uptime_ticks"]) - start) / 1000 for row in rows]
        y = [sample_value(row, profile) for row in rows]
        ax.plot(x, y, color=color, alpha=0.27, linewidth=1.3,
                label="corridas individuales" if index == 0 else None)
        for i in range(common):
            times[i].append(x[i])
            shared[i].append(y[i])
    x_mean = [statistics.mean(group) for group in times]
    y_mean = [statistics.mean(group) for group in shared]
    y_sd = [statistics.stdev(group) for group in shared]
    ax.plot(x_mean, y_mean, color=color, linewidth=2.3, label="media de 5 corridas")
    ax.fill_between(x_mean, [m - s for m, s in zip(y_mean, y_sd)],
                    [m + s for m, s in zip(y_mean, y_sd)], color=color,
                    alpha=0.18, label="± desviación entre corridas")
    if profile == "leak":
        baseline = int(runs[0]["summary"]["baseline_free"])
        ax.axhline(baseline, color="#555555", linestyle="--", linewidth=1,
                   label="heap libre inicial")
    ax.set_xlabel("Tiempo desde la primera muestra (s)")
    ax.set_ylabel("Heap libre (B)" if profile == "leak" else "Fragmentación estimada (%)")
    ax.set_title("E11: fuga controlada" if profile == "leak" else
                 "E11: fragmentación controlada")
    ax.grid(alpha=0.25)
    ax.legend(fontsize=8)
    fig.text(0.5, 0.01, "Todas las muestras aparecen en las curvas individuales; "
             "la banda usa los instantes presentes en las cinco corridas.",
             ha="center", fontsize=8)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(destination, dpi=160)
    plt.close(fig)


def metrics(runs: list[dict], baud: int) -> dict[str, tuple[list[float], str]]:
    return {
        "min_heap_free": ([float(run["summary"]["min_free"]) for run in runs], "B"),
        "max_fragmentation": ([float(run["summary"]["max_fragmentation_pct"])
                               for run in runs], "%"),
        "cpu_busy_est": ([statistics.mean(100 - int(row["cpu_idle_pct"])
                                          for row in run["samples"]) for run in runs], "%"),
        "uart_observed": ([100 * int(run["summary"]["uart_bytes"]) * 10 /
                           (baud * float(run["summary"]["duration_s"]))
                           for run in runs], "%"),
        "stats_samples": ([float(len(run["samples"])) for run in runs], "muestras"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True,
                        help="marca UTC en nombres de archivos, YYYYMMDDTHHMMSSZ")
    parser.add_argument("--data", type=Path, default=ROOT / "experiments" / "e11")
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()
    if not re.fullmatch(r"\d{8}T\d{6}Z", args.session) or args.baud <= 0:
        parser.error("sesion debe ser YYYYMMDDTHHMMSSZ y baud > 0")
    data = args.data.resolve()
    summaries = read_csv(data / "memory_runs.csv")
    groups = {profile: [read_run(data, args.session, profile, run, summaries)
                        for run in range(1, 6)] for profile in ("leak", "frag")}
    all_runs = groups["leak"] + groups["frag"]
    if (len({run["summary"]["commit"] for run in all_runs}) != 1 or
            len({run["summary"]["duration_s"] for run in all_runs}) != 1 or
            len({run["summary"]["variant"] for run in all_runs}) != 1):
        raise ValueError("las diez corridas deben compartir commit, duracion y variante")
    result = data / f"memory_summary_{args.session}.csv"
    with result.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("session", "commit", "profile",
            "metric", "mean", "sd_between_runs", "n", "unit"))
        writer.writeheader()
        for profile, runs in groups.items():
            for metric, (values, unit) in metrics(runs, args.baud).items():
                writer.writerow({"session": args.session,
                    "commit": runs[0]["summary"]["commit"], "profile": profile,
                    "metric": metric, "mean": f"{statistics.mean(values):.6f}",
                    "sd_between_runs": f"{statistics.stdev(values):.6f}",
                    "n": len(values), "unit": unit})
            plot_runs(runs, profile, data / f"memory_{profile}_{args.session}.png")
    print(f"PASS: 5 leak + 5 frag; 10 UART y CSV verificados; "
          f"{sum(run['raw_crc'] for run in all_runs)} CRC y "
          f"{sum(run['raw_gaps'] for run in all_runs)} huecos en ventanas")
    print(f"Tabla: {result}; graficas: memory_leak_{args.session}.png, "
          f"memory_frag_{args.session}.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
