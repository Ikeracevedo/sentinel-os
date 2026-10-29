#!/usr/bin/env python3
"""Reconstruye duraciones y motivos de switch desde UART crudo de E11.

Ejemplo: py tools/testbench/analyze_switch.py --session 20261021T002832Z
La duracion observada incluye la sonda; CCOUNT funciona a 40 MHz.
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from bridge import Bridge, Sink  # noqa: E402

FREQ_CYCLES_PER_US = 40
REASONS = {1: "quantum", 2: "block", 3: "yield", 4: "preempt", 5: "exit"}
SAMPLE_FIELDS = ("session", "run", "event_seq", "out_seq", "tid_out", "tid_in",
                 "reason", "cycles", "latency_us", "t_abs_us")
SUMMARY_FIELDS = ("session", "run", "reason", "samples", "min_us", "max_us",
                  "mean_us", "median_us", "sigma_us", "p95_us", "p99_us",
                  "crc_errors", "frame_gaps", "event_seq_gaps", "event_seq_missing",
                  "events_observed", "unpaired_in",
                  "unpaired_out", "boundary_in", "boundary_out",
                  "device_dropped", "status")


def percentile(values: list[float], percent: int) -> float:
    """Percentil empírico de rango más cercano, sobre la muestra completa."""
    index = max(0, (percent * len(values) + 99) // 100 - 1)
    return values[index]


class SwitchSink(Sink):
    def __init__(self, session: str, run: int) -> None:
        self.session, self.run = session, run
        self.rows: list[dict] = []
        self.pending: dict | None = None
        self.last_seq: int | None = None
        self.event_seq_gaps = 0
        self.event_seq_missing = 0
        self.events_observed = 0
        self.unpaired_in = 0
        self.unpaired_out = 0
        self.boundary_in = 0
        self.boundary_out = 0
        self.device_dropped = 0
        self.hello = 0
        self.seen_in = False

    def write(self, kind: str, data: dict) -> None:
        if kind == "hello":
            self.hello += 1
            self.pending = None
            self.last_seq = None
        elif kind == "stats":
            self.device_dropped = max(self.device_dropped, data["events_dropped"])
        elif kind == "event":
            self.events_observed += 1
            seq = data["seq"]
            if self.last_seq is not None:
                gap = (seq - self.last_seq) & 0xffff
                if gap != 1:
                    self.event_seq_gaps += 1
                    if 1 < gap < 0x8000:
                        self.event_seq_missing += gap - 1
                    if self.pending is not None:
                        self.unpaired_out += 1
                        self.pending = None
            self.last_seq = seq
            if data["type"] == 1:
                if self.pending is not None:
                    self.unpaired_out += 1
                self.pending = data
            elif data["type"] == 2:
                out = self.pending
                self.pending = None
                if out is None or out["arg0"] != data["tid"]:
                    if out is None and not self.seen_in:
                        self.boundary_in += 1
                    else:
                        self.unpaired_in += 1
                    if out is not None:
                        self.unpaired_out += 1
                    self.seen_in = True
                    return
                self.seen_in = True
                self.rows.append({"session": self.session, "run": self.run,
                                  "event_seq": seq, "out_seq": out["seq"],
                                  "tid_out": out["tid"], "tid_in": data["tid"],
                                  "reason": REASONS.get(out["arg1"],
                                                        f"unknown-{out['arg1']}"),
                                  "cycles": data["arg0"],
                                  "latency_us": data["arg0"] / FREQ_CYCLES_PER_US,
                                  "t_abs_us": data["t_abs_us"]})


def summary(rows: list[dict], session: str, run: str, reason: str,
            quality: dict) -> dict:
    values = sorted(float(row["latency_us"]) for row in rows)
    result = {"session": session, "run": run, "reason": reason,
              "samples": len(values), **quality}
    if not values:
        result.update({key: "" for key in SUMMARY_FIELDS[4:11]})
    else:
        result.update({"min_us": values[0], "max_us": values[-1],
                       "mean_us": statistics.fmean(values),
                       "median_us": statistics.median(values),
                       "sigma_us": statistics.stdev(values) if len(values) > 1 else 0,
                       "p95_us": percentile(values, 95),
                       "p99_us": percentile(values, 99)})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True, help="ID UTC de la batería nominal")
    parser.add_argument("--input", type=Path, default=ROOT / "experiments" / "e11")
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    source = args.input.resolve()
    raw_paths = sorted(source.glob(f"stats_{args.session}_trace-on_nominal_r*.uart"))
    if len(raw_paths) != 5:
        parser.error(f"se esperaban 5 UART nominales; encontrados {len(raw_paths)}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    all_rows: list[dict] = []
    summaries: list[dict] = []
    quality_totals: Counter = Counter()
    for run, path in enumerate(raw_paths, 1):
        expected = f"_r{run:02d}.uart"
        if not path.name.endswith(expected):
            raise RuntimeError(f"falta el archivo de corrida {run}: {path.name}")
        sink = SwitchSink(args.session, run)
        bridge = Bridge([sink])
        with path.open("rb") as stream:
            while chunk := stream.read(65536):
                bridge.feed(chunk)
        if sink.pending is not None:
            sink.boundary_out += 1
        quality = {"crc_errors": bridge.decoder.crc_errors,
                   "frame_gaps": bridge.decoder.gaps,
                   "event_seq_gaps": sink.event_seq_gaps,
                   "event_seq_missing": sink.event_seq_missing,
                   "events_observed": sink.events_observed,
                   "unpaired_in": sink.unpaired_in,
                   "unpaired_out": sink.unpaired_out,
                   "boundary_in": sink.boundary_in,
                   "boundary_out": sink.boundary_out,
                   "device_dropped": sink.device_dropped}
        clean = all(quality[key] == 0 for key in
                 ("crc_errors", "frame_gaps", "event_seq_gaps", "unpaired_in",
                  "unpaired_out", "device_dropped"))
        missing_fraction = sink.event_seq_missing / max(
            1, sink.events_observed + sink.event_seq_missing)
        status = ("PASS" if clean else "PASS_LINK_ERRORS"
                  if missing_fraction < 0.01 and sink.device_dropped == 0
                  else "LINK_ERRORS")
        quality["status"] = status
        quality_totals.update({key: value for key, value in quality.items()
                               if key != "status"})
        all_rows.extend(sink.rows)
        for reason in ("all", *REASONS.values()):
            subset = sink.rows if reason == "all" else [row for row in sink.rows
                                                        if row["reason"] == reason]
            summaries.append(summary(subset, args.session, str(run), reason, quality))
        print(f"corrida {run}: {len(sink.rows)} switches, {status}; "
              f"CRC={quality['crc_errors']} huecos={quality['frame_gaps']}")

    with (output / f"switch_samples_{args.session}.csv").open(
            "w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=SAMPLE_FIELDS)
        writer.writeheader()
        writer.writerows(all_rows)
    pooled_quality = dict(quality_totals)
    pooled_clean = all(quality_totals[key] == 0 for key in
                                ("crc_errors", "frame_gaps", "event_seq_gaps",
                                 "unpaired_in", "unpaired_out", "device_dropped"))
    pooled_missing_fraction = (quality_totals["event_seq_missing"] /
        max(1, quality_totals["events_observed"] + quality_totals["event_seq_missing"]))
    pooled_quality["status"] = ("PASS" if pooled_clean else "PASS_LINK_ERRORS"
                                if pooled_missing_fraction < 0.01 and
                                quality_totals["device_dropped"] == 0
                                else "LINK_ERRORS")
    for reason in ("all", *REASONS.values()):
        subset = all_rows if reason == "all" else [row for row in all_rows
                                                   if row["reason"] == reason]
        summaries.append(summary(subset, args.session, "pooled", reason,
                                 pooled_quality))
    with (output / f"switch_summary_{args.session}.csv").open(
            "w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=SUMMARY_FIELDS)
        writer.writeheader()
        writer.writerows(summaries)
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(9, 5))
    latencies = [row["latency_us"] for row in all_rows]
    ax.hist(latencies, bins=80, color="#277da1")
    ax.set(xlabel="Latencia observada (µs)", ylabel="Cambios de contexto",
           title=f"TRACE ON · nominal · {args.session} · n={len(latencies)}")
    fig.tight_layout()
    fig.savefig(output / f"switch_hist_{args.session}.png", dpi=160)
    plt.close(fig)
    print(f"Total: {len(all_rows)} switches; "
          f"media={summaries[-6]['mean_us']:.3f} µs; "
          f"eventos ausentes={quality_totals['event_seq_missing']} "
          f"({100 * pooled_missing_fraction:.4f}%); "
          f"calidad={pooled_quality['status']}; CSV e histograma en {output}")
    return 0 if len(all_rows) >= 50000 and pooled_quality["status"] != "LINK_ERRORS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
