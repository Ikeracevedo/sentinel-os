"""Comprueba una sola captura del grafo con deadlock y limpia siempre el escenario.

Uso: py tools/e10_graph_board.py --port COM8
Mantiene el puerto abierto durante inyeccion, lectura y parada.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from e09_verify import Board, heap_state


def edges_and_cycles(output: str) -> tuple[set[tuple[int, int]], int]:
    edges = {(int(a), int(b)) for a, b in
             re.findall(r"^\s*(\d+) -> (\d+)\s*$", output, re.MULTILINE)}
    match = re.search(r"aristas=(\d+) captura=(\d+) ciclos", output)
    if match is None or len(edges) != int(match.group(1)):
        raise RuntimeError(f"salida del grafo incompleta: {output!r}")
    return edges, int(match.group(2))


def check_graph(output: str, expect_cycle: bool) -> int:
    edges, cycles = edges_and_cycles(output)
    if expect_cycle:
        if len(edges) != 2 or not all((b, a) in edges for a, b in edges):
            raise RuntimeError(f"se esperaba ciclo de dos tareas: {output!r}")
    elif edges:
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
    args = parser.parse_args()

    board = Board(args.port, args.baud, Path("build/transport/e10-graph.db"))
    attempted = False
    try:
        board.pump(1.0)
        status = board.command("inject status")
        if "sin escenarios activos" not in status:
            raise RuntimeError(f"detener primero el escenario activo: {status!r}")
        before = heap_state(board.command("free"))
        print(f"base: heap libre={before[0]} B, bloques={before[1]}", flush=True)
        check_graph(board.command("graph"), expect_cycle=False)

        try:
            attempted = True  # incluso si se pierde la respuesta, intentar stop
            start = board.command("inject deadlock")
            if "inject deadlock: 2 tareas" not in start:
                raise RuntimeError(f"inicio no confirmado: {start!r}")
            board.pump(0.2)
            check_graph(board.command("graph"), expect_cycle=True)
            sync = board.command("sync")
            if "2 tareas bloqueadas" not in sync:
                raise RuntimeError(f"sync no confirma el bloqueo: {sync!r}")
            print("deadlock: dos tareas bloqueadas y ciclo visible", flush=True)
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
        check_graph(board.command("graph"), expect_cycle=False)
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != before):
            raise RuntimeError(f"limpieza incompleta: status={status!r} sync={sync!r} "
                               f"heap={after!r} (base={before!r})")
        print("OK: stop, mutexes, tareas bloqueadas, heap y grafo restaurados",
              flush=True)
        print(board.bridge.report(), flush=True)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
