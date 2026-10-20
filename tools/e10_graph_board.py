"""Comprueba una sola captura del grafo con deadlock y limpia siempre el escenario.

Uso: py tools/e10_graph_board.py --port COM8
Mantiene el puerto abierto durante inyeccion, lectura y parada.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from bridge import Sink
from e09_verify import Board, heap_state


class DetectorEvents(Sink):
    def __init__(self) -> None:
        self.events: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] in (0x50, 0x60):
            self.events.append(data)


def detector_state(output: str) -> tuple[int, int, int]:
    match = re.search(r"detector: chequeos=(\d+) alertas=(\d+) .*"
                      r"maximo=(\d+) ciclos", output)
    if match is None:
        raise RuntimeError(f"estadisticas del detector incompletas: {output!r}")
    return tuple(map(int, match.groups()))


def edges_and_cycles(output: str) -> tuple[set[tuple[int, int]], int]:
    edges = {(int(a), int(b)) for a, b in
             re.findall(r"^\s*(\d+) -> (\d+)\s*$", output, re.MULTILINE)}
    match = re.search(r"aristas=(\d+) captura=(\d+) ciclos", output)
    if match is None or len(edges) != int(match.group(1)):
        raise RuntimeError(f"salida del grafo incompleta: {output!r}")
    return edges, int(match.group(2))


def check_graph(output: str, expect_cycle: int = 0) -> int:
    edges, cycles = edges_and_cycles(output)
    if expect_cycle:
        match = re.search(r"^ciclo: (\d+(?: -> \d+)+)\r?$", output,
                          re.MULTILINE)
        if match is None:
            raise RuntimeError(f"falta la lista ordenada del ciclo: {output!r}")
        tids = [int(tid) for tid in match.group(1).split(" -> ")]
        listed_edges = set(zip(tids, tids[1:]))
        if (len(edges) != expect_cycle or len(tids) != expect_cycle + 1 or
                tids[0] != tids[-1] or len(set(tids[:-1])) != expect_cycle or
                listed_edges != edges):
            raise RuntimeError(f"ciclo de {expect_cycle} tareas incorrecto: {output!r}")
    elif edges or "sin ciclos" not in output:
        raise RuntimeError(f"grafo con aristas fuera de la inyeccion: {output!r}")
    if cycles >= 2000:
        raise RuntimeError(f"captura de {cycles} ciclos supera el limite de 50 us")
    print(f"grafo: {sorted(edges)}; captura={cycles} ciclos ({cycles / 40:.1f} us)",
          flush=True)
    return cycles


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--tasks", type=int, choices=(2, 3), default=2)
    args = parser.parse_args()

    board = Board(args.port, args.baud, Path("build/transport/e10-graph.db"))
    detector_events = DetectorEvents()
    board.bridge.sinks.append(detector_events)
    attempted = False
    try:
        board.pump(1.0)
        status = board.command("inject status")
        if "sin escenarios activos" not in status:
            raise RuntimeError(f"detener primero el escenario activo: {status!r}")
        before = heap_state(board.command("free"))
        print(f"base: heap libre={before[0]} B, bloques={before[1]}", flush=True)
        check_graph(board.command("graph"))
        runs_before, alerts_before, _ = detector_state(board.command("detector"))
        if runs_before == 0 or alerts_before != 0:
            raise RuntimeError("detector no corre o ya habia alertas antes de inyectar")

        failure: Exception | None = None
        try:
            attempted = True  # incluso si se pierde la respuesta, intentar stop
            event_index = len(detector_events.events)
            start = board.command("inject deadlock" +
                                  (" 3" if args.tasks == 3 else ""))
            if f"inject deadlock: {args.tasks} tareas" not in start:
                raise RuntimeError(f"inicio no confirmado: {start!r}")
            board.pump(0.7)
            alert_text = start + "".join(board.capture.logs)
            if "[ALERTA] DEADLOCK:" not in alert_text:
                raise RuntimeError(f"el detector no imprimio la alerta: {alert_text!r}")
            graph_text = board.command("graph")
            check_graph(graph_text, expect_cycle=args.tasks)
            sync = board.command("sync")
            if f"{args.tasks} tareas bloqueadas" not in sync:
                raise RuntimeError(f"sync no confirma el bloqueo: {sync!r}")
            detector_text = board.command("detector")
            runs, alerts, max_cycles = detector_state(detector_text)
            if runs <= runs_before or alerts != alerts_before + 1 or max_cycles >= 4000:
                raise RuntimeError(f"alerta/coste inesperado: {detector_text!r}")
            events = detector_events.events[event_index:]
            injected = next((e for e in events if e["type"] == 0x60 and
                             e["arg0"] == 1), None)
            edge_events = [e for e in events if e["type"] == 0x50]
            edges, _ = edges_and_cycles(graph_text)
            emitted_edges = {(e["tid"], e["arg1"]) for e in edge_events}
            if (injected is None or len(edge_events) != args.tasks or
                    emitted_edges != edges or
                    any(e["arg0"] == 0 for e in edge_events)):
                raise RuntimeError(f"EV_ALERT no coincide con el grafo: {events!r}")
            delay_us = edge_events[0]["t_abs_us"] - injected["t_abs_us"]
            if not 0 <= delay_us < 1_000_000:
                raise RuntimeError(f"deteccion tardia: {delay_us} us")
            board.pump(1.1)
            _, alerts_later, _ = detector_state(board.command("detector"))
            if alerts_later != alerts:
                raise RuntimeError("el detector repitio la misma alerta")
            print(f"deadlock: {args.tasks} tareas bloqueadas y ciclo visible",
                  flush=True)
            print(f"detector: alerta en {delay_us / 1000:.1f} ms; "
                  f"{args.tasks} EV_ALERT; maximo {max_cycles / 40:.1f} us; "
                  "sin repeticion", flush=True)
        except Exception as error:
            failure = error
        finally:
            if attempted:
                try:
                    stop = board.command("inject stop deadlock")
                    print(stop, flush=True)
                    if "detenido y limpio" not in stop:
                        print("ATENCION: parada sin confirmacion; revisar inject status",
                              flush=True)
                except Exception as error:
                    print(f"ATENCION: parada sin confirmacion: {error}", flush=True)

        status = board.command("inject status")
        sync = board.command("sync")
        after = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != before):
            raise RuntimeError(f"limpieza incompleta: status={status!r} sync={sync!r} "
                               f"heap={after!r} (base={before!r})")
        print("OK: stop, mutexes, tareas bloqueadas, heap y grafo restaurados",
              flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
