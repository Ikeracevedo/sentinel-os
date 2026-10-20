"""Comprueba inanicion bajo hog y recuperacion mediante inject stop.

Uso: py tools/e10_hog_board.py --port COM8
"""

from __future__ import annotations

import argparse
import re

from bridge import Sink
from e09_verify import Board, heap_state, task_row
from e10_graph_board import check_graph, detector_state


class StarvationEvents(Sink):
    def __init__(self) -> None:
        self.events: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] == 0x50:
            self.events.append(data)


def starvation_state(output: str) -> tuple[int, int, int, int]:
    match = re.search(r"inanicion_chequeos=(\d+) inanicion_alertas=(\d+) "
                      r"espera_max=(\d+) ms umbral=(\d+) ms", output)
    if match is None:
        raise RuntimeError(f"estadisticas de inanicion incompletas: {output!r}")
    return tuple(map(int, match.groups()))


def starvation_alert(output: str) -> tuple[str, int, int, int, int]:
    match = re.search(r"\[ALERTA\] INANICION: (\w+)\(tid (\d+)\) "
                      r"prioridad=(\d+) espera=(\d+) ms umbral=(\d+) ms", output)
    if match is None:
        raise RuntimeError(f"alerta de inanicion ausente: {output!r}")
    name = match.group(1)
    return name, *(int(value) for value in match.groups()[1:])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    board = Board(args.port, args.baud, None)
    observed = StarvationEvents()
    board.bridge.sinks.append(observed)
    attempted = False
    failure: BaseException | None = None
    logs = ""
    event_index = 0
    try:
        board.pump(1.0)
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("hay un escenario activo antes de comenzar")
        before = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        baseline = board.command("detector starve 500")
        checks_before, alerts_before, _, threshold = starvation_state(baseline)
        if checks_before == 0 or threshold != 500:
            raise RuntimeError(f"umbral o chequeos incorrectos: {baseline!r}")
        print(f"base: heap libre={before[0]} B, bloques={before[1]}; "
              f"umbral={threshold} ms", flush=True)

        try:
            attempted = True
            event_index = len(observed.events)
            start = board.command("inject hog")
            if "inject hog: tarea" not in start or "cada 1200 ms" not in start:
                raise RuntimeError(f"hog no confirmado: {start!r}")
            # Consultar al shell mientras corre el hog puede agotar el enlace.
            # Guardamos la alerta y detenemos la carga antes de pedir métricas.
            board.pump(1.6)
            logs = start + "".join(board.capture.logs)
        except BaseException as error:
            failure = error
        finally:
            if attempted:
                for attempt in range(2):
                    try:
                        stop = board.command("inject stop hog")
                        print(stop, flush=True)
                        if "detenido y limpio" in stop:
                            break
                        print("ATENCION: stop sin confirmacion", flush=True)
                    except Exception as error:
                        print(f"ATENCION: stop sin confirmacion: {error}", flush=True)
                    if attempt == 0:
                        board.pump(1.0)

        status = board.command("inject status")
        sync = board.command("sync")
        after = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != before):
            raise RuntimeError(f"limpieza incompleta: status={status!r}, "
                               f"sync={sync!r}, heap={after!r}, base={before!r}")
        print("OK: hog detenido; tareas, mutexes, heap y grafo restaurados",
              flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        name, tid, priority, wait_ms, alert_threshold = starvation_alert(logs)
        if (name != "tracer" or priority != 6 or
                wait_ms <= 500 or alert_threshold != 500):
            raise RuntimeError(f"victima/umbral inesperado: {logs!r}")
        detector_text = board.command("detector")
        checks, alerts, max_wait, _ = starvation_state(detector_text)
        _, _, max_cycles = detector_state(detector_text)
        if (checks <= checks_before or alerts <= alerts_before or
                max_wait < wait_ms or max_cycles >= 4000):
            raise RuntimeError(f"chequeos, alertas o coste incorrectos: {detector_text!r}")
        events = observed.events[event_index:]
        if not any(e["tid"] == tid and e["arg0"] == priority and
                   e["arg1"] >= wait_ms for e in events):
            raise RuntimeError(f"EV_ALERT de inanicion ausente: {events!r}")
        tracer = task_row(board.command("ps -v tracer"), "tracer")
        if int(tracer[8]) <= 500:
            raise RuntimeError(f"max_ready_wait de tracer no aumento: {tracer!r}")
        print(f"inanicion: {name}(tid {tid}), prioridad={priority}, "
              f"espera={wait_ms} ms; maximo detector={max_cycles / 40:.1f} us",
              flush=True)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
