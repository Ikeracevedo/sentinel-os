#!/usr/bin/env python3
"""Captura repetible del heap bajo inject leak o inject frag (Windows/COM)."""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from run import ROOT, StatsCsv, git_commit, git_dirty, initial_status
from e09_verify import Board
from monitor import ResourceMonitor

FIELDS = ("timestamp_utc", "commit", "git_dirty", "variant", "profile", "run",
          "duration_s", "baseline_free", "baseline_largest", "baseline_blocks",
          "min_free", "min_largest", "last_free", "last_largest",
          "max_fragmentation_pct", "post_free", "post_largest", "post_blocks",
          "stats_samples", "uart_bytes", "crc_errors_delta", "frame_gaps_delta",
          "status")


def heap_state(text: str) -> tuple[int, int, int]:
    values = []
    for label in ("Libre", "MayorLibre", "Bloques"):
        match = re.search(rf"^{label}:\s+(\d+)", text, re.MULTILINE)
        if match is None:
            raise RuntimeError(f"respuesta de free incompleta: {text!r}")
        values.append(int(match.group(1)))
    return tuple(values)


def run_one(board: Board, sink: StatsCsv, scenario: str,
            duration: float) -> dict[str, int | float]:
    board.bridge.sinks.append(sink)
    before_crc = board.bridge.decoder.crc_errors
    before_gaps = board.bridge.decoder.gaps
    started = False
    try:
        if "sin escenarios activos" not in initial_status(board):
            raise RuntimeError("hay un escenario activo antes de medir memoria")
        baseline = heap_state(board.command("free"))
        # El comando puede ejecutarse aunque se pierda su respuesta.
        started = True
        response = board.command(f"inject {scenario}")
        if f"inject {scenario}:" not in response:
            raise RuntimeError(f"inicio de {scenario} no confirmado: {response!r}")
        sink.active = True
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            board.pump(min(0.5, deadline - time.monotonic()))
        sink.active = False
        if f"inject: {scenario} activo" not in board.command("inject status"):
            raise RuntimeError(f"{scenario} no seguia activo al terminar la ventana")
    finally:
        sink.active = False
        if started:
            stopped = board.command(f"inject stop {scenario}")
            if f"inject stop: {scenario} detenido y limpio" not in stopped:
                raise RuntimeError(f"limpieza de {scenario} no confirmada: {stopped!r}")
    if "sin escenarios activos" not in board.command("inject status"):
        raise RuntimeError(f"{scenario} quedo activo")
    post = heap_state(board.command("free"))
    if post != baseline:
        raise RuntimeError(f"heap no volvio a linea base: {baseline} -> {post}")
    samples = sink.samples
    if len(samples) < 2 or any(s["trace_enabled"] != 1 for s in samples):
        raise RuntimeError("telemetria insuficiente o TRACE no coincide")
    if any(b["uptime_ticks"] <= a["uptime_ticks"]
           for a, b in zip(samples, samples[1:])):
        raise RuntimeError("uptime no crecio durante la medicion")
    if scenario == "leak" and samples[-1]["heap_free"] >= baseline[0]:
        raise RuntimeError("leak no redujo el heap durante la ventana")
    if scenario == "frag" and not any(s["heap_free"] > 4096 and
                                      s["heap_largest"] < 4096 for s in samples):
        raise RuntimeError("frag no mostro libre total > 4096 con mayor hueco < 4096")
    return {"baseline_free": baseline[0], "baseline_largest": baseline[1],
            "baseline_blocks": baseline[2],
            "min_free": min(s["heap_free"] for s in samples),
            "min_largest": min(s["heap_largest"] for s in samples),
            "last_free": samples[-1]["heap_free"],
            "last_largest": samples[-1]["heap_largest"],
            "max_fragmentation_pct": max(
                100 * (1 - s["heap_largest"] / s["heap_free"])
                for s in samples if s["heap_free"]),
            "post_free": post[0], "post_largest": post[1],
            "post_blocks": post[2], "stats_samples": len(samples),
            "uart_bytes": sink.uart_bytes,
            "crc_errors_delta": board.bridge.decoder.crc_errors - before_crc,
            "frame_gaps_delta": board.bridge.decoder.gaps - before_gaps}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    scenarios = parser.add_mutually_exclusive_group(required=True)
    scenarios.add_argument("--scenario", choices=("leak", "frag"))
    scenarios.add_argument("--all-scenarios", action="store_true",
                           help="cinco o mas corridas de leak y frag en una bateria")
    parser.add_argument("--duration", type=float, default=35)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--pilot", action="store_true",
                        help="una corrida de prueba, separada de las cinco repeticiones")
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--busid")
    parser.add_argument("--distro", default="ubuntu")
    parser.add_argument("--live-resources", action="store_true",
                        help="mostrar CPU, heap y perdidas durante cada corrida")
    args = parser.parse_args()
    if args.duration < 2 or (args.pilot and args.repeats != 1) or (
            not args.pilot and args.repeats < 5):
        parser.error("--duration >= 2 s; --pilot requiere --repeats 1; "
                     "experimento requiere --repeats >= 5")
    if args.pilot and args.all_scenarios:
        parser.error("--pilot requiere elegir un solo --scenario")
    if args.flash and not args.busid:
        parser.error("--flash requiere --busid")
    if args.flash:
        from flash import flash_board
        flash_board(busid=args.busid, port=args.port, distro=args.distro,
                    variant="trace-on", benchmark=False)

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    result_path = output / ("memory_pilots.csv" if args.pilot else "memory_runs.csv")
    if result_path.exists() and result_path.stat().st_size:
        with result_path.open(newline="", encoding="utf-8") as stream:
            if tuple(next(csv.reader(stream))) != FIELDS:
                raise RuntimeError(f"cabecera incompatible en {result_path}")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    metadata = {"commit": git_commit(), "git_dirty": str(git_dirty()).lower(),
                "variant": "trace-on"}
    selected_scenarios = ("leak", "frag") if args.all_scenarios else (args.scenario,)
    print(f"commit={metadata['commit']} git_dirty={metadata['git_dirty']} "
          f"puerto={args.port} escenarios={','.join(selected_scenarios)}; "
          f"{args.repeats} corridas por escenario de {args.duration:g} s", flush=True)
    board = Board(args.port, args.baud, None)
    if args.live_resources:
        board.bridge.sinks.append(ResourceMonitor())
    link_error_runs = 0
    try:
        for scenario in selected_scenarios:
            scenario_metadata = {**metadata, "profile": scenario}
            for run in range(1, args.repeats + 1):
                label = "pilot" if args.pilot else "r"
                raw_path = output / f"memory_{stamp}_trace-on_{scenario}_{label}{run:02d}.csv"
                sink = StatsCsv(raw_path, scenario_metadata, run)
                try:
                    data = run_one(board, sink, scenario, args.duration)
                except Exception:
                    print(f"FAIL {scenario} corrida {run}: "
                          f"datos parciales conservados en {raw_path}",
                          file=sys.stderr, flush=True)
                    raise
                finally:
                    board.bridge.sinks.remove(sink)
                    sink.close()
                link_error = bool(data["crc_errors_delta"] or data["frame_gaps_delta"])
                status = ("PILOT_LINK_ERRORS" if link_error else "PILOT") if args.pilot else (
                    "PASS_LINK_ERRORS" if link_error else "PASS")
                link_error_runs += link_error
                row = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                       **scenario_metadata, "run": run, "duration_s": args.duration,
                       **data, "status": status}
                with result_path.open("a", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=FIELDS)
                    if stream.tell() == 0:
                        writer.writeheader()
                    writer.writerow(row)
                print(f"{status} {scenario} {run}/{args.repeats}: "
                      f"{data['stats_samples']} stats, libre minimo={data['min_free']} B, "
                      f"hueco mayor minimo={data['min_largest']} B, "
                      f"heap restaurado={data['post_free']} B, "
                      f"CRC={data['crc_errors_delta']}, "
                      f"tramas ausentes={data['frame_gaps_delta']}", flush=True)
    finally:
        board.close()
    outcome = ("PILOT" if not link_error_runs else "PILOT_WITH_LINK_ERRORS") if args.pilot else (
        "PASS" if not link_error_runs else "COMPLETE_WITH_LINK_ERRORS")
    print(f"{outcome} bateria {','.join(selected_scenarios)}; "
          f"{link_error_runs} corridas con errores de enlace; CSV: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
