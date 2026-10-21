#!/usr/bin/env python3
"""E11: observa carga nominal sin inyección y conserva CSV y UART crudo.

Piloto: py tools/testbench/false_positives.py --port COM8 --duration 30 --pilot
Formal: py tools/testbench/false_positives.py --port COM8 --duration 3600
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))

from bridge import FileSink  # noqa: E402
from e09_verify import Board, heap_state  # noqa: E402
from e10_graph_board import detector_state  # noqa: E402
from e10_nominal_board import AlertCounter, load_status, no_cycle, pathology_alerts  # noqa: E402
from run import git_commit, git_dirty  # noqa: E402

FIELDS = ("session", "timestamp_utc", "commit", "git_dirty", "duration_target_s",
          "elapsed_s", "phase", "load_iterations", "alloc_failures",
          "detector_checks_delta", "deadlock_alerts_delta", "frag_alerts_delta",
          "starve_alerts_delta", "event_alerts", "frame_alerts", "last_cost_us",
          "max_cost_us", "heap_free_b", "crc_errors_delta", "frame_gaps_delta",
          "dropped_device_delta", "uart_bytes", "status", "error")


def last_cost_us(output: str) -> float:
    match = re.search(r"ultimo=(\d+) ciclos", output)
    if match is None:
        raise RuntimeError("falta el costo de la última ejecución del detector")
    return int(match.group(1)) / 40


def check_observation(board: Board, observed: AlertCounter, *,
                      baseline: dict, elapsed: float, target: int,
                      phase: str) -> dict:
    status = board.command("inject status")
    _, iterations, alloc_failures = load_status(status)
    no_cycle(board.command("graph"))
    detector = board.command("detector")
    checks, alerts, max_cycles = detector_state(detector)
    frag, starve = pathology_alerts(detector)
    heap_free, _ = heap_state(board.command("free"))
    row = {"elapsed_s": f"{elapsed:.3f}", "phase": phase,
           "load_iterations": iterations, "alloc_failures": alloc_failures,
           "detector_checks_delta": checks - baseline["checks"],
           "deadlock_alerts_delta": alerts - baseline["alerts"],
           "frag_alerts_delta": frag - baseline["frag"],
           "starve_alerts_delta": starve - baseline["starve"],
           "event_alerts": observed.event_alerts,
           "frame_alerts": observed.frame_alerts,
           "last_cost_us": f"{last_cost_us(detector):.3f}",
           "max_cost_us": f"{max_cycles / 40:.3f}",
           "heap_free_b": heap_free,
           "crc_errors_delta": board.bridge.decoder.crc_errors - baseline["crc"],
           "frame_gaps_delta": board.bridge.decoder.gaps - baseline["gaps"],
           "dropped_device_delta": board.bridge.dropped_device - baseline["dropped"]}
    if (iterations == 0 or alloc_failures or alerts != baseline["alerts"] or
            frag != baseline["frag"] or starve != baseline["starve"] or
            observed.event_alerts or observed.frame_alerts):
        raise RuntimeError(f"falsa alerta o carga defectuosa: {row}")
    if checks - baseline["checks"] < elapsed * 2 - 5:
        raise RuntimeError(f"chequeos insuficientes: {row}")
    if elapsed >= target and row["detector_checks_delta"] == 0:
        raise RuntimeError("no hubo ejecuciones del detector")
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--duration", type=int, default=3600)
    parser.add_argument("--pilot", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    if args.duration < 10:
        parser.error("--duration debe ser al menos 10 s")
    if not args.pilot and args.duration < 3600:
        parser.error("la prueba formal requiere al menos 3600 s; use --pilot")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    session = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    raw_path = output / f"false_positives_{session}.uart"
    csv_path = output / ("false_positives_pilots.csv" if args.pilot else
                         "false_positives_runs.csv")
    board = Board(args.port, args.baud, None)
    observed = AlertCounter()
    raw = FileSink(str(raw_path))
    board.bridge.sinks.extend((observed, raw))
    metadata = {"session": session, "commit": git_commit(),
                "git_dirty": str(git_dirty()).lower(),
                "duration_target_s": args.duration}
    baseline: dict | None = None
    started = False
    start_time = 0.0
    error: BaseException | None = None
    try:
        with csv_path.open("a", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            if stream.tell() == 0:
                writer.writeheader()
            try:
                board.pump(1.0)
                if "sin escenarios activos" not in board.command("inject status"):
                    raise RuntimeError("escenario activo antes de la prueba")
                initial_heap = heap_state(board.command("free"))
                no_cycle(board.command("graph"))
                detector = board.command("detector")
                checks, alerts, _ = detector_state(detector)
                frag, starve = pathology_alerts(detector)
                if checks == 0:
                    raise RuntimeError("el detector no ha ejecutado chequeos")
                baseline = {"checks": checks, "alerts": alerts,
                            "frag": frag, "starve": starve,
                            "crc": board.bridge.decoder.crc_errors,
                            "gaps": board.bridge.decoder.gaps,
                            "dropped": board.bridge.dropped_device}
                response = board.command("inject load nominal")
                started = True
                if "inject load nominal: 5 tareas" not in response:
                    raise RuntimeError(f"carga no confirmada: {response!r}")
                start_time = time.monotonic()
                interval = 30 if args.pilot else 60
                next_report = interval
                previous_iterations = 0
                while time.monotonic() - start_time < args.duration:
                    elapsed = time.monotonic() - start_time
                    board.pump(min(5.0, args.duration - elapsed))
                    if observed.event_alerts or observed.frame_alerts:
                        raise RuntimeError("alerta recibida durante la carga")
                    elapsed = time.monotonic() - start_time
                    if elapsed < next_report and elapsed < args.duration:
                        continue
                    row = check_observation(board, observed, baseline=baseline,
                                            elapsed=elapsed, target=args.duration,
                                            phase="minute")
                    if int(row["load_iterations"]) <= previous_iterations:
                        raise RuntimeError("la carga dejó de avanzar")
                    previous_iterations = int(row["load_iterations"])
                    writer.writerow({**metadata,
                                     "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                     **row, "status": "PASS"})
                    stream.flush()
                    print(f"[{elapsed:.0f}/{args.duration} s] "
                          f"iteraciones={row['load_iterations']} "
                          f"chequeos={row['detector_checks_delta']} "
                          f"alertas=0 CRC={row['crc_errors_delta']} "
                          f"huecos={row['frame_gaps_delta']}", flush=True)
                    next_report += interval
                final = check_observation(board, observed, baseline=baseline,
                                          elapsed=time.monotonic() - start_time,
                                          target=args.duration, phase="final")
                writer.writerow({**metadata,
                                 "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                 **final, "status": "PASS"})
                stream.flush()
            except BaseException as caught:
                error = caught
            finally:
                if started:
                    try:
                        stopped = board.command("inject stop load")
                        if "detenido y limpio" not in stopped:
                            raise RuntimeError(f"limpieza no confirmada: {stopped!r}")
                        if baseline is not None:
                            if "sin escenarios activos" not in board.command("inject status"):
                                raise RuntimeError("escenario activo tras limpieza")
                            if heap_state(board.command("free")) != initial_heap:
                                raise RuntimeError("heap no restaurado")
                            no_cycle(board.command("graph"))
                    except Exception as caught:
                        error = RuntimeError(f"{error}; limpieza: {caught}")
                raw.flush()
                writer.writerow({**metadata,
                                 "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                 "elapsed_s": f"{time.monotonic() - start_time:.3f}" if started else "0",
                                 "phase": "end", "uart_bytes": raw_path.stat().st_size,
                                 "status": "PASS" if error is None else "FAIL",
                                 "error": "" if error is None else str(error)})
                stream.flush()
        print(f"{'PASS' if error is None else 'FAIL'}: "
              f"{args.duration} s nominales; CSV={csv_path}; UART={raw_path}; "
              f"{board.bridge.report()}", flush=True)
        if error is not None:
            raise error
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
