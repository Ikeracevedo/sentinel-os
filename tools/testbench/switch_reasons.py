#!/usr/bin/env python3
"""Captura complementaria E11 de quantum, bloqueo y yield en TRACE ON.

Es una carga distinta de `inject load nominal`; sus tiempos nunca se agregan
a la bateria nominal de 50 000 cambios. `--pilot` permite una corrida corta.
"""

from __future__ import annotations

import argparse
import csv
import re
import statistics
import sys
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from analyze_switch import REASONS, SAMPLE_FIELDS, SUMMARY_FIELDS, SwitchSink, summary
from memory import heap_state
from run import ROOT, StatsCsv, git_commit, git_dirty, initial_status
from e09_verify import Board
from bridge import Bridge

FIELDS = ("session", "timestamp_utc", "commit", "git_dirty", "variant", "profile",
          "run", "duration_s", "rounds", "switches", "quantum", "block", "yield",
          "preempt", "exit", "stats_samples", "uart_bytes", "crc_errors",
          "frame_gaps", "event_seq_missing", "device_dropped", "status")


def capture_one(board: Board, sink: StatsCsv, duration: float) -> tuple[int, ...]:
    if "sin escenarios activos" not in initial_status(board):
        raise RuntimeError("hay un escenario activo antes de switchbench")
    baseline = heap_state(board.command("free"))
    started = True  # El comando puede ejecutarse aunque se pierda su respuesta.
    board.bridge.sinks.append(sink)
    try:
        response = board.command("inject switchbench")
        if "inject switchbench: 2 tareas" not in response:
            raise RuntimeError(f"inicio no confirmado: {response!r}")
        sink.active = True
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            board.pump(min(0.5, deadline - time.monotonic()))
        sink.active = False
        status = board.command("inject status")
        match = re.search(r"switchbench: rondas=(\d+),(\d+)", status)
        if match is None or "inject: switchbench activo" not in status:
            raise RuntimeError(f"estado incompleto: {status!r}")
        rounds = tuple(map(int, match.groups()))
        if any(value == 0 for value in rounds):
            raise RuntimeError(f"una tarea no progreso: {rounds}")
    finally:
        sink.active = False
        board.bridge.sinks.remove(sink)
        if started:
            stopped = board.command("inject stop switchbench")
            if "inject stop: switchbench detenido y limpio" not in stopped:
                raise RuntimeError(f"limpieza no confirmada: {stopped!r}")
    if "sin escenarios activos" not in board.command("inject status"):
        raise RuntimeError("switchbench quedo activo")
    after = heap_state(board.command("free"))
    if after != baseline:
        raise RuntimeError(f"heap no regreso a linea base: {baseline} -> {after}")
    if len(sink.samples) < 2:
        raise RuntimeError(f"solo {len(sink.samples)} muestras de recursos")
    if any(sample["trace_enabled"] != 1 for sample in sink.samples):
        raise RuntimeError("TRACE no esta activo")
    return (*rounds, len(sink.samples))


def replay(raw_path: Path, session: str, run: int) -> tuple[SwitchSink, Bridge]:
    switch = SwitchSink(session, run)
    bridge = Bridge([switch])
    with raw_path.open("rb") as stream:
        while chunk := stream.read(65536):
            bridge.feed(chunk)
    if switch.pending is not None:
        switch.boundary_out += 1
    return switch, bridge


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--pilot", action="store_true")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--busid")
    parser.add_argument("--distro", default="ubuntu")
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    if args.duration < 2 or args.repeats < 1 or (not args.pilot and args.repeats < 5):
        parser.error("--duration >=2; bateria formal requiere --repeats >=5")
    if args.flash and not args.busid:
        parser.error("--flash requiere --busid")
    if args.flash:
        from flash import flash_board
        flash_board(busid=args.busid, port=args.port, distro=args.distro,
                    variant="trace-on", benchmark=False)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    session = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    metadata = {"commit": git_commit(), "git_dirty": str(git_dirty()).lower(),
                "variant": "trace-on", "profile": "switchbench"}
    result_path = output / ("switchbench_pilots.csv" if args.pilot
                            else "switchbench_runs.csv")
    if result_path.exists() and result_path.stat().st_size:
        with result_path.open(newline="", encoding="utf-8") as stream:
            if tuple(next(csv.reader(stream))) != FIELDS:
                raise RuntimeError(f"cabecera incompatible en {result_path}")
    print(f"session={session} commit={metadata['commit']} "
          f"git_dirty={metadata['git_dirty']} puerto={args.port} "
          f"{args.repeats} corridas de {args.duration:g} s", flush=True)
    board = Board(args.port, args.baud, None)
    all_switch_rows: list[dict] = []
    failed_reasons = 0
    try:
        for run in range(1, args.repeats + 1):
            csv_path = output / f"switchbench_{session}_trace-on_r{run:02d}.csv"
            sink = StatsCsv(csv_path, metadata, run)
            try:
                rounds_a, rounds_b, stats_samples = capture_one(board, sink,
                                                                 args.duration)
            except BaseException:
                print(f"FAIL {run}/{args.repeats}: parciales en {csv_path}",
                      file=sys.stderr, flush=True)
                raise
            finally:
                sink.close()
            raw_path = csv_path.with_suffix(".uart")
            switch, replay_bridge = replay(raw_path, session, run)
            counts = Counter(row["reason"] for row in switch.rows)
            status = ("PASS" if replay_bridge.decoder.crc_errors == 0 and
                      replay_bridge.decoder.gaps == 0 and
                      switch.event_seq_missing == 0 and
                      switch.device_dropped == 0 else "PASS_LINK_ERRORS")
            if counts["quantum"] == 0 or counts["yield"] == 0:
                status = "MISSING_REASON"
                failed_reasons += 1
            all_switch_rows.extend(switch.rows)
            row = {"session": session,
                   "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                   **metadata, "run": run, "duration_s": args.duration,
                   "rounds": rounds_a + rounds_b,
                   "switches": len(switch.rows),
                   **{reason: counts[reason] for reason in REASONS.values()},
                   "stats_samples": stats_samples, "uart_bytes": raw_path.stat().st_size,
                   "crc_errors": replay_bridge.decoder.crc_errors,
                   "frame_gaps": replay_bridge.decoder.gaps,
                   "event_seq_missing": switch.event_seq_missing,
                   "device_dropped": switch.device_dropped,
                   "status": status}
            with result_path.open("a", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=FIELDS)
                if stream.tell() == 0:
                    writer.writeheader()
                writer.writerow(row)
            samples_path = output / f"switchbench_samples_{session}_r{run:02d}.csv"
            with samples_path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=SAMPLE_FIELDS)
                writer.writeheader()
                writer.writerows(switch.rows)
            print(f"{status} {run}/{args.repeats}: rondas={row['rounds']} "
                  f"quantum={counts['quantum']} bloqueo={counts['block']} "
                  f"yield={counts['yield']} switches={len(switch.rows)} "
                  f"CRC={row['crc_errors']} huecos={row['frame_gaps']}", flush=True)
    finally:
        board.close()
    summary_rows = []
    for run in (*range(1, args.repeats + 1), "pooled"):
        run_rows = (all_switch_rows if run == "pooled" else
                    [row for row in all_switch_rows if row["run"] == run])
        for reason in ("all", *REASONS.values()):
            selected = (run_rows if reason == "all" else
                        [row for row in run_rows if row["reason"] == reason])
            summary_rows.append(summary(selected, session, str(run), reason, {}))
    summary_path = output / f"switchbench_summary_{session}.csv"
    with summary_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=SUMMARY_FIELDS[:11])
        writer.writeheader()
        writer.writerows(summary_rows)
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(9, 5))
    for reason in ("quantum", "block", "yield", "preempt"):
        values = [row["latency_us"] for row in all_switch_rows
                  if row["reason"] == reason]
        if values:
            ax.hist(values, bins=45, histtype="step", linewidth=1.8,
                    label=f"{reason} (n={len(values)})")
    ax.set(xlabel="Latencia observada (µs)", ylabel="Cambios de contexto",
           title=f"Switchbench TRACE ON · {session}")
    ax.legend()
    fig.tight_layout()
    fig.savefig(output / f"switchbench_hist_{session}.png", dpi=160)
    plt.close(fig)
    run_means = [float(row["mean_us"]) for row in summary_rows
                 if row["reason"] == "all" and row["run"] != "pooled"]
    print(f"CSV de resultados: {result_path}")
    if len(run_means) > 1:
        print(f"Media entre corridas: {statistics.fmean(run_means):.3f} ± "
              f"{statistics.stdev(run_means):.3f} µs; resumen={summary_path}")
    else:
        print(f"Resumen e histograma exploratorios: {summary_path}")
    return 1 if failed_reasons else 0


if __name__ == "__main__":
    raise SystemExit(main())
