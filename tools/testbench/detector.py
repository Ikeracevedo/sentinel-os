#!/usr/bin/env python3
"""E11: mide deadlocks repetidos con eventos CCOUNT y UART crudo por intento.

Piloto: py tools/testbench/detector.py --port COM8 --pilot --flash --busid 1-5
Bateria: py tools/testbench/detector.py --port COM8 --repeats 20
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import sys
import time
from contextlib import redirect_stdout
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))

from bridge import FileSink, Sink  # noqa: E402
from e09_verify import Board, heap_state  # noqa: E402
from e10_graph_board import check_graph, detector_state, edges_and_cycles  # noqa: E402
from run import git_commit, git_dirty  # noqa: E402

FIELDS = ("session", "timestamp_utc", "commit", "git_dirty", "variant",
          "scenario", "run", "injected_cycles", "alert_cycles", "delay_us",
          "detector_checks", "detector_alerts_delta", "last_cost_cycles",
          "max_cost_cycles", "crc_errors_delta", "frame_gaps_delta",
          "dropped_device_delta", "uart_bytes", "status", "error")


class DetectorEvents(Sink):
    def __init__(self) -> None:
        self.events: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] in (0x50, 0x60):
            self.events.append(data.copy())


def last_cost_cycles(output: str) -> int:
    match = re.search(r"ultimo=(\d+) ciclos", output)
    if match is None:
        raise RuntimeError(f"falta ultimo costo del detector: {output!r}")
    return int(match.group(1))


def verify_graph(output: str, expect_cycle: int = 0) -> None:
    with redirect_stdout(io.StringIO()):
        check_graph(output, expect_cycle=expect_cycle)


def run_one(board: Board, events: DetectorEvents) -> dict[str, int]:
    status = board.command("inject status")
    if "sin escenarios activos" not in status:
        raise RuntimeError(f"escenario activo antes de empezar: {status!r}")
    baseline_heap = heap_state(board.command("free"))
    verify_graph(board.command("graph"))
    before_checks, before_alerts, _ = detector_state(board.command("detector"))
    before_crc = board.bridge.decoder.crc_errors
    before_gaps = board.bridge.decoder.gaps
    before_dropped = board.bridge.dropped_device
    index = len(events.events)
    attempted = False
    failure: Exception | None = None
    result: dict[str, int] = {}
    try:
        attempted = True
        start = board.command("inject deadlock")
        if "inject deadlock: 2 tareas" not in start:
            raise RuntimeError(f"inicio no confirmado: {start!r}")
        deadline = time.monotonic() + 1.2
        while time.monotonic() < deadline:
            new = events.events[index:]
            if sum(e["type"] == 0x50 for e in new) >= 2:
                break
            board.pump(0.05)
        new = events.events[index:]
        injected = [e for e in new if e["type"] == 0x60 and e["arg0"] == 1]
        alerts = [e for e in new if e["type"] == 0x50]
        if len(injected) != 1 or len(alerts) != 2:
            raise RuntimeError(f"EV_INJECT/EV_ALERT incompletos: {new!r}")
        delay_us = alerts[0]["t_abs_us"] - injected[0]["t_abs_us"]
        if not 0 <= delay_us < 1_000_000:
            raise RuntimeError(f"demora de deteccion inesperada: {delay_us} us")
        graph_text = board.command("graph")
        verify_graph(graph_text, expect_cycle=2)
        edges, _ = edges_and_cycles(graph_text)
        if {(e["tid"], e["arg1"]) for e in alerts} != edges or \
                any(e["arg0"] == 0 for e in alerts):
            raise RuntimeError("EV_ALERT no coincide con el ciclo de mutexes")
        if "2 tareas bloqueadas" not in board.command("sync"):
            raise RuntimeError("sync no confirma dos tareas bloqueadas")
        output = board.command("detector")
        checks, alert_count, max_cycles = detector_state(output)
        cost = last_cost_cycles(output)
        if checks <= before_checks or alert_count != before_alerts + 1:
            raise RuntimeError(f"contador del detector inesperado: {output!r}")
        result = {"injected_cycles": injected[0]["cycles"],
                  "alert_cycles": alerts[0]["cycles"], "delay_us": delay_us,
                  "detector_checks": checks - before_checks,
                  "detector_alerts_delta": alert_count - before_alerts,
                  "last_cost_cycles": cost, "max_cost_cycles": max_cycles}
    except Exception as error:
        failure = error
    finally:
        if attempted:
            try:
                stop = board.command("inject stop deadlock")
                if "detenido y limpio" not in stop:
                    raise RuntimeError(f"parada sin confirmar: {stop!r}")
            except Exception as error:
                if failure is None:
                    failure = error
                else:
                    failure = RuntimeError(f"{failure}; limpieza: {error}")

    # La fila PASS exige restauracion completa, incluso si hubo una alerta.
    try:
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("inject status muestra escenario activo")
        sync = board.command("sync")
        if "0 mutex" not in sync or "0 tareas bloqueadas" not in sync:
            raise RuntimeError(f"sincronizacion sin restaurar: {sync!r}")
        if heap_state(board.command("free")) != baseline_heap:
            raise RuntimeError("heap no restaurado")
        verify_graph(board.command("graph"))
    except Exception as error:
        if failure is None:
            failure = error
        else:
            failure = RuntimeError(f"{failure}; estado final: {error}")

    result.update({"crc_errors_delta": board.bridge.decoder.crc_errors - before_crc,
                   "frame_gaps_delta": board.bridge.decoder.gaps - before_gaps,
                   "dropped_device_delta": board.bridge.dropped_device - before_dropped})
    if failure is not None:
        raise failure
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--repeats", type=int, default=20)
    parser.add_argument("--pilot", action="store_true",
                        help="un solo intento, guardado aparte")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--busid", help="BUSID usbipd, por ejemplo 1-5")
    parser.add_argument("--distro", default="ubuntu")
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    if args.repeats < 1 or args.repeats > 100:
        parser.error("--repeats debe estar entre 1 y 100")
    if args.flash and not args.busid:
        parser.error("--flash requiere --busid")
    if args.flash:
        from flash import flash_board
        flash_board(busid=args.busid, port=args.port, distro=args.distro,
                    variant="trace-on", benchmark=False)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    session = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    count = 1 if args.pilot else args.repeats
    destination = output / ("detector_pilots.csv" if args.pilot else "detector_runs.csv")
    metadata = {"session": session, "commit": git_commit(),
                "git_dirty": str(git_dirty()).lower(), "variant": "trace-on",
                "scenario": "deadlock-2"}
    board = Board(args.port, args.baud, None)
    events = DetectorEvents()
    board.bridge.sinks.append(events)
    try:
        with destination.open("a", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            if stream.tell() == 0:
                writer.writeheader()
            for run in range(1, count + 1):
                raw_path = output / f"detector_{session}_r{run:02d}.uart"
                raw = FileSink(str(raw_path))
                board.bridge.sinks.append(raw)
                row = {**metadata, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                       "run": run, "status": "FAIL", "error": ""}
                try:
                    row.update(run_one(board, events))
                    if (row["crc_errors_delta"] or row["frame_gaps_delta"] or
                            row["dropped_device_delta"]):
                        row["status"] = "LINK_ERRORS"
                        row["error"] = "enlace con CRC, huecos o descartes de placa"
                    else:
                        row["status"] = "PASS"
                except Exception as error:
                    row["error"] = str(error)
                finally:
                    board.bridge.sinks.remove(raw)
                    raw.close()
                    row["uart_bytes"] = raw_path.stat().st_size
                    writer.writerow(row)
                    stream.flush()
                print(f"{row['status']} {run}/{count}: "
                      f"demora={int(row.get('delay_us', 0)) / 1000:.1f} ms, "
                      f"coste_ultimo={int(row.get('last_cost_cycles', 0)) / 40:.1f} us, "
                      f"CRC={row.get('crc_errors_delta', '?')} "
                      f"huecos={row.get('frame_gaps_delta', '?')}; "
                      f"UART={raw_path}", flush=True)
                if row["status"] != "PASS":
                    print(f"Bateria detenida; datos parciales conservados: {row['error']}",
                          file=sys.stderr)
                    return 1
        print(f"PASS: {count} detecciones, CSV={destination}; "
              f"{board.bridge.report()}", flush=True)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
