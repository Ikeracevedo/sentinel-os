#!/usr/bin/env python3
"""Analiza dos sesiones E11 overhead-standard sin descartar valores atipicos.

Ejemplo:
    py tools/testbench/analyze_overhead.py --on-session 20261020T172954Z \
        --off-session 20261020T175606Z
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import statistics
import struct
from pathlib import Path

from run import ROOT


def elf_sections(path: Path) -> dict[str, int]:
    raw = path.read_bytes()
    if raw[:6] != b"\x7fELF\x01\x01":
        raise ValueError(f"se esperaba ELF32 little-endian: {path}")
    offset = struct.unpack_from("<I", raw, 32)[0]
    size, count, names_index = struct.unpack_from("<HHH", raw, 46)
    headers = [struct.unpack_from("<IIIIIIIIII", raw, offset + i * size)
               for i in range(count)]
    names_header = headers[names_index]
    names = raw[names_header[4]:names_header[4] + names_header[5]]
    return {names[h[0]:].split(b"\0", 1)[0].decode(): h[5]
            for h in headers}


def read_session(path: Path, session: str, variant: str,
                 profile: str = "overhead-standard") -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = [row for row in csv.DictReader(stream)
                if row["session"] == session and row["variant"] == variant]
    expected_trace = int(variant == "trace-on")
    if len(rows) != 5 or {int(row["run"]) for row in rows} != set(range(1, 6)):
        raise ValueError(f"sesion {session}/{variant}: se requieren corridas 1..5")
    if any(int(row["ms"]) != 60_000 or int(row["trace"]) != expected_trace or
           int(row["errors"]) != 0 or row["profile"] != profile
           for row in rows):
        raise ValueError(f"sesion {session}/{variant}: configuracion o errores invalidos")
    return sorted(rows, key=lambda row: int(row["run"]))


def mean_sd(values: list[float]) -> tuple[float, float]:
    return statistics.mean(values), statistics.stdev(values)


def summarize(off: list[dict[str, str]], on: list[dict[str, str]]) -> tuple[list[dict], dict]:
    if len({row["commit"] for row in off + on}) != 1:
        raise ValueError("las variantes provienen de commits diferentes")
    samples = {}
    for variant, rows in (("trace-off", off), ("trace-on", on)):
        samples[variant] = {
            "cpu_occupied_pct": [100 - 100 * int(r["idle"]) / int(r["elapsed"])
                                 for r in rows],
            "switch_full_us": [int(r["full_sum"]) / int(r["full_switches"]) / 40
                               for r in rows],
            "switch_c_us": [int(r["sum"]) / int(r["switches"]) / 40
                            for r in rows],
            "throughput_ops_per_s": [int(r["ops"]) / 60 for r in rows],
            "events_per_s": [int(r["emitted"]) / 60 for r in rows],
            "events_dropped": [int(r["dropped"]) for r in rows],
        }
    units = {"cpu_occupied_pct": "% CPU", "switch_full_us": "us",
             "switch_c_us": "us", "throughput_ops_per_s": "ops/s",
             "events_per_s": "eventos/s", "events_dropped": "eventos"}
    table = []
    for key, unit in units.items():
        for variant in ("trace-off", "trace-on"):
            mean, sd = mean_sd(samples[variant][key])
            table.append({"metric": key, "variant": variant, "mean": f"{mean:.9f}",
                          "sd_between_runs": f"{sd:.9f}", "n": 5, "unit": unit})
    for key in ("cpu_occupied_pct", "switch_full_us", "throughput_ops_per_s"):
        off_mean, off_sd = mean_sd(samples["trace-off"][key])
        on_mean, on_sd = mean_sd(samples["trace-on"][key])
        table.append({"metric": key, "variant": "on-minus-off",
                      "mean": f"{on_mean - off_mean:.9f}",
                      "sd_between_runs": f"{math.hypot(off_sd, on_sd):.9f}",
                      "n": "5+5", "unit": units[key]})
    return table, samples


def plot(samples: dict, destination: Path, profile: str = "overhead-standard") -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(10, 4))
    for ax, key, title, ylabel, color in (
        (axes[0], "cpu_occupied_pct", "CPU ocupada", "CPU ocupada (%)", "#2867a1"),
        (axes[1], "switch_full_us", "Cambio de contexto observado",
         "Duración (µs)", "#c06438"),
    ):
        variants = ("trace-off", "trace-on")
        values = [mean_sd(samples[v][key]) for v in variants]
        ax.bar([0, 1], [v[0] for v in values], color=color, alpha=0.85,
               yerr=[v[1] for v in values], capsize=5)
        ax.set_xticks([0, 1], ["TRACE=0", "TRACE=1"])
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.set_ylim(bottom=0)
        ax.grid(axis="y", alpha=0.2)
    fig.suptitle(f"ESP32 · carga {profile} · 5 × 60 s por variante")
    fig.text(0.5, 0.01, "Barras: media; error: desviación entre corridas (n=5)",
             ha="center", fontsize=9)
    fig.tight_layout(rect=(0, 0.04, 1, 0.95))
    fig.savefig(destination, dpi=160)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--on-session", required=True)
    parser.add_argument("--off-session", required=True)
    parser.add_argument("--profile", choices=("overhead-standard", "nominal"),
                        default="overhead-standard")
    parser.add_argument("--data", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    data = args.data.resolve()
    source = data / ("nominal_overhead_runs.csv" if args.profile == "nominal"
                     else "overhead_runs.csv")
    off = read_session(source, args.off_session, "trace-off", args.profile)
    on = read_session(source, args.on_session, "trace-on", args.profile)
    table, samples = summarize(off, on)
    sizes = {}
    folder_base = "trace-nominal" if args.profile == "nominal" else "trace-overhead"
    for variant, folder in (("trace-off", folder_base + "-off"),
                            ("trace-on", folder_base + "-on")):
        root = ROOT / "build" / folder
        sections = elf_sections(root / "kernel.elf")
        binary = root / "kernel.bin"
        sizes[variant] = {"ram": sum(sections.get(k, 0) for k in
                                     (".data", ".bss", ".dram1")),
                          "bin": binary.stat().st_size,
                          "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    for metric, size_key in (("ram_static_bytes", "ram"),
                             ("binary_bytes", "bin")):
        for variant in ("trace-off", "trace-on"):
            table.append({"metric": metric, "variant": variant,
                          "mean": sizes[variant][size_key], "sd_between_runs": "",
                          "n": 1, "unit": "B"})
        table.append({"metric": metric, "variant": "on-minus-off",
                      "mean": sizes["trace-on"][size_key] - sizes["trace-off"][size_key],
                      "sd_between_runs": "", "n": "1+1", "unit": "B"})
    prefix = "nominal_" if args.profile == "nominal" else ""
    report = data / (prefix + "overhead_summary.csv")
    with report.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("commit", "off_session",
            "on_session", "metric", "variant", "mean", "sd_between_runs",
            "n", "unit"))
        writer.writeheader()
        writer.writerows({"commit": off[0]["commit"],
                          "off_session": args.off_session,
                          "on_session": args.on_session, **row} for row in table)
    plot(samples, data / (prefix + "overhead_comparison.png"), args.profile)
    by_key = {(row["metric"], row["variant"]): row for row in table}
    overhead = by_key[("cpu_occupied_pct", "on-minus-off")]
    latency = by_key[("switch_full_us", "on-minus-off")]
    print(f"CPU TRACE ON-OFF: {float(overhead['mean']):.3f} ± "
          f"{float(overhead['sd_between_runs']):.3f} puntos porcentuales")
    print(f"Switch observado ON-OFF: {float(latency['mean']):.3f} ± "
          f"{float(latency['sd_between_runs']):.3f} us")
    print(f"RAM estatica ON-OFF: {sizes['trace-on']['ram'] - sizes['trace-off']['ram']:+d} B")
    print(f"Imagen .bin ON-OFF: {sizes['trace-on']['bin'] - sizes['trace-off']['bin']:+d} B")
    print(f"SHA256 OFF: {sizes['trace-off']['sha256']}")
    print(f"SHA256 ON:  {sizes['trace-on']['sha256']}")
    print(f"Tabla: {report}; grafica: {data / (prefix + 'overhead_comparison.png')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
