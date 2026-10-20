"""Comprueba la alerta de fragmentacion y restaura la placa con inject stop.

Uso: py tools/e10_frag_board.py --port COM8
"""

from __future__ import annotations

import argparse
import re

from bridge import Sink
from e09_verify import Board, heap_state
from e10_graph_board import check_graph


class FragmentationEvents(Sink):
    def __init__(self) -> None:
        self.events: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] == 0x50 and data["tid"] == 0:
            self.events.append(data)


def fragmentation_state(output: str) -> tuple[int, int, int, int, int]:
    match = re.search(r"frag_chequeos=(\d+) frag_alertas=(\d+) "
                      r"frag=(\d+)% libre=(\d+) mayor=(\d+)", output)
    if match is None:
        raise RuntimeError(f"estadisticas de fragmentacion incompletas: {output!r}")
    return tuple(map(int, match.groups()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    board = Board(args.port, args.baud, None)
    observed = FragmentationEvents()
    board.bridge.sinks.append(observed)
    attempted = False
    failure: BaseException | None = None
    try:
        board.pump(1.0)
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("hay un escenario activo antes de comenzar")
        before = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        checks_before, alerts_before, _, _, _ = fragmentation_state(
            board.command("detector"))
        if checks_before == 0:
            raise RuntimeError("la medicion periodica del heap no corre")
        print(f"base: heap libre={before[0]} B, bloques={before[1]}; "
              f"fragmentacion={alerts_before} alertas historicas", flush=True)

        try:
            attempted = True
            event_index = len(observed.events)
            start = board.command("inject frag")
            if "inject frag: tarea fragger" not in start:
                raise RuntimeError(f"inyeccion no confirmada: {start!r}")
            board.pump(1.5)
            logs = start + "".join(board.capture.logs)
            if "[ALERTA] FRAGMENTACION CRITICA:" not in logs:
                raise RuntimeError(f"no aparece alerta critica: {logs!r}")
            text = board.command("detector")
            checks, alerts, percent, free, largest = fragmentation_state(text)
            if (checks <= checks_before or not alerts_before < alerts <= alerts_before + 2 or
                    percent <= 60 or free == 0 or largest >= free):
                raise RuntimeError(f"metricas de fragmentacion inesperadas: {text!r}")
            free_text = board.command("free")
            actual_free, _ = heap_state(free_text)
            if actual_free != free:
                raise RuntimeError(f"libre del detector y de free difieren: {text!r} "
                                   f"vs {free_text!r}")
            events = observed.events[event_index:]
            if not any(e["arg0"] == percent and e["arg1"] == largest
                       for e in events):
                raise RuntimeError(f"EV_ALERT de fragmentacion incorrecto: "
                                   f"{events!r}")
            board.pump(1.2)
            _, alerts_later, _, _, _ = fragmentation_state(board.command("detector"))
            if alerts_later != alerts:
                raise RuntimeError("alerta de fragmentacion repetida cada segundo")
            print(f"fragmentacion critica: {percent}%, libre={free} B, "
                  f"mayor={largest} B; {alerts - alerts_before} alertas "
                  "de umbral y sin repeticion",
                  flush=True)
        except BaseException as error:
            failure = error
        finally:
            if attempted:
                try:
                    stop = board.command("inject stop frag")
                    print(stop, flush=True)
                    if "detenido y limpio" not in stop:
                        print("ATENCION: stop sin confirmacion", flush=True)
                except Exception as error:
                    print(f"ATENCION: stop sin confirmacion: {error}", flush=True)

        status = board.command("inject status")
        sync = board.command("sync")
        after = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != before):
            raise RuntimeError(f"limpieza incompleta: status={status!r}, "
                               f"sync={sync!r}, heap={after!r}, base={before!r}")
        board.pump(1.2)
        _, _, percent_after, _, _ = fragmentation_state(board.command("detector"))
        if percent_after > 30:
            raise RuntimeError(f"fragmentacion residual: {percent_after}%")
        print("OK: stop, tareas, mutexes, heap y grafo restaurados", flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
