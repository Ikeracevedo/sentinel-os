#!/usr/bin/env python3
"""E11 HU05: cronómetro de Windows frente a CCOUNT, sin instrumento GPIO.

Cuenta cambios durante cinco ventanas switchbench de 10 s y guarda UART.
El tiempo Windows incluye el trabajo de las tareas; es una cota superior,
no una segunda medición directa de la duración de cada switch.
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))

from bridge import Bridge  # noqa: E402
from e09_verify import Board, heap_state  # noqa: E402
from analyze_switch import SwitchSink  # noqa: E402
from run import StatsCsv, git_commit, git_dirty, initial_status  # noqa: E402

FIELDS = ("session", "timestamp_utc", "commit", "git_dirty", "run",
          "workload", "host_elapsed_ns", "switches", "ccount_switch_cycles",
          "ccount_mean_us", "host_time_per_switch_us", "switch_fraction_pct",
          "crc_errors", "frame_gaps", "event_seq_missing", "device_dropped",
          "uart_bytes", "status", "error")


def run_one(board: Board, sink: StatsCsv, duration: float) -> int:
    if "sin escenarios activos" not in initial_status(board):
        raise RuntimeError("escenario activo antes de empezar")
    heap_before = heap_state(board.command("free"))
    board.bridge.sinks.append(sink)
    attempted = False
    elapsed_ns = 0
    failure: Exception | None = None
    try:
        attempted = True
        response = board.command("inject switchbench")
        if "inject switchbench: 2 tareas" not in response:
            raise RuntimeError("switchbench no confirmado")
        sink.active = True
        started = time.perf_counter_ns()
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            board.pump(min(0.2, deadline - time.monotonic()))
        elapsed_ns = time.perf_counter_ns() - started
        sink.active = False
        status = board.command("inject status")
        if "inject: switchbench activo" not in status:
            raise RuntimeError("switchbench dejó de estar activo")
    except Exception as error:
        failure = error
    finally:
        sink.active = False
        board.bridge.sinks.remove(sink)
        if attempted:
            try:
                stopped = board.command("inject stop switchbench")
                if "detenido y limpio" not in stopped:
                    raise RuntimeError("limpieza de switchbench no confirmada")
            except Exception as error:
                failure = RuntimeError(f"{failure}; limpieza: {error}")
    if "sin escenarios activos" not in board.command("inject status") or \
            heap_state(board.command("free")) != heap_before:
        raise RuntimeError("estado o heap no restaurado")
    if failure is not None:
        raise failure
    return elapsed_ns


def replay(path: Path, session: str, run: int) -> tuple[SwitchSink, Bridge]:
    switch = SwitchSink(session, run)
    bridge = Bridge([switch])
    with path.open("rb") as stream:
        while chunk := stream.read(65536):
            bridge.feed(chunk)
    return switch, bridge


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--pilot", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    if args.duration < 2 or args.repeats < 1 or (not args.pilot and args.repeats < 5):
        parser.error("duración >=2 s y batería formal de al menos 5 corridas")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    session = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    metadata = {"commit": git_commit(), "git_dirty": str(git_dirty()).lower(),
                "workload": "switchbench"}
    result = output / ("external_clock_pilots.csv" if args.pilot else
                       "external_clock_runs.csv")
    board = Board(args.port, args.baud, None)
    rows: list[dict] = []
    try:
        with result.open("a", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            if stream.tell() == 0:
                writer.writeheader()
            for run in range(1, 2 if args.pilot else args.repeats + 1):
                stats_path = output / f"external_clock_{session}_r{run:02d}.csv"
                sink = StatsCsv(stats_path, {"commit": metadata["commit"],
                                              "git_dirty": metadata["git_dirty"],
                                              "variant": "trace-on",
                                              "profile": "switchbench"}, run)
                row = {"session": session,
                       "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                       **metadata, "run": run, "status": "FAIL", "error": ""}
                try:
                    elapsed_ns = run_one(board, sink, args.duration)
                except Exception as error:
                    row["error"] = str(error)
                finally:
                    sink.close()
                raw_path = stats_path.with_suffix(".uart")
                if not row["error"]:
                    switch, replay_bridge = replay(raw_path, session, run)
                    n = len(switch.rows)
                    if n == 0:
                        row["error"] = "sin cambios de contexto completos"
                    else:
                        cycles = sum(int(sample["cycles"]) for sample in switch.rows)
                        row.update({"host_elapsed_ns": elapsed_ns,
                                    "switches": n,
                                    "ccount_switch_cycles": cycles,
                                    "ccount_mean_us": f"{cycles / (40 * n):.6f}",
                                    "host_time_per_switch_us":
                                        f"{elapsed_ns / (1000 * n):.6f}",
                                    "switch_fraction_pct":
                                        f"{100 * cycles / (40_000_000 * elapsed_ns / 1e9):.6f}",
                                    "crc_errors": replay_bridge.decoder.crc_errors,
                                    "frame_gaps": replay_bridge.decoder.gaps,
                                    "event_seq_missing": switch.event_seq_missing,
                                    "device_dropped": switch.device_dropped})
                        row["status"] = ("PASS" if not (replay_bridge.decoder.crc_errors or
                            replay_bridge.decoder.gaps or switch.event_seq_missing or
                            switch.device_dropped) else "LINK_ERRORS")
                row["uart_bytes"] = raw_path.stat().st_size
                writer.writerow(row)
                stream.flush()
                rows.append(row)
                print(f"{row['status']} {run}/{1 if args.pilot else args.repeats}: "
                      f"N={row.get('switches', 0)} "
                      f"Windows={int(row.get('host_elapsed_ns', 0)) / 1e9:.3f} s "
                      f"CCOUNT={row.get('ccount_mean_us', '?')} us/switch",
                      flush=True)
                if row["status"] == "FAIL":
                    print(f"Datos parciales conservados: {row['error']}", file=sys.stderr)
                    return 1
    finally:
        board.close()
    if not args.pilot:
        means = [float(row["ccount_mean_us"]) for row in rows]
        host = [float(row["host_time_per_switch_us"]) for row in rows]
        print(f"CCOUNT media entre corridas: {statistics.mean(means):.3f} ± "
              f"{statistics.stdev(means):.3f} us/switch; "
              f"Windows cota superior: {statistics.mean(host):.1f} ± "
              f"{statistics.stdev(host):.1f} us/switch; CSV={result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
