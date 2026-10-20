"""Valida health y deadlock con ciclo real y limpieza verificada.

Uso: py tools/e10_health_board.py --port COM8
"""

from __future__ import annotations

import argparse
import re

from e09_verify import Board, heap_state
from e10_graph_board import check_graph


CATEGORIES = ("deadlocks", "fragmentacion", "inanicion",
              "inversion de prioridad", "jitter de switch",
              "eventos perdidos", "heap libre")


def check_health(text: str, deadlock: bool) -> None:
    for category in CATEGORIES:
        if f"health: {category} " not in text:
            raise RuntimeError(f"falta categoria {category}: {text!r}")
    wanted = "health: deadlocks CRIT; ciclo=2 tareas" if deadlock else \
             "health: deadlocks OK; ciclo=0 tareas"
    if wanted not in text or "health: jitter de switch " not in text:
        raise RuntimeError(f"resumen de salud incorrecto: {text!r}")
    if not deadlock:
        for category in ("fragmentacion", "inanicion", "inversion de prioridad",
                         "jitter de switch", "heap libre"):
            if f"health: {category} OK;" not in text:
                raise RuntimeError(f"{category} no recuperada: {text!r}")


def check_deadlock(text: str, expected_tasks: int) -> None:
    match = re.search(r"deadlock: (?:sin ciclo;|ciclo=([^\r\n;]+); "
                      r"tareas=(\d+)) captura=(\d+) ciclos", text)
    if match is None:
        raise RuntimeError(f"chequeo manual incompleto: {text!r}")
    path, count, cycles = match.groups()
    if int(cycles) >= 2000:
        raise RuntimeError(f"captura manual demasiado larga: {cycles} ciclos")
    if expected_tasks == 0:
        if path is not None:
            raise RuntimeError(f"ciclo inesperado: {text!r}")
    else:
        tids = [int(part) for part in path.split(" -> ")]
        if (int(count) != expected_tasks or len(tids) != expected_tasks + 1 or
                tids[0] != tids[-1] or len(set(tids[:-1])) != expected_tasks):
            raise RuntimeError(f"ciclo manual incorrecto: {text!r}")
    print(f"deadlock manual: {expected_tasks} tareas; captura={cycles} ciclos "
          f"({int(cycles) / 40:.1f} us)", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    board = Board(args.port, args.baud, None)
    attempted = False
    failure: BaseException | None = None
    try:
        board.pump(1.0)
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("hay un escenario activo antes de comenzar")
        before = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        check_deadlock(board.command("deadlock"), 0)
        check_health(board.command("health"), deadlock=False)
        print("OK: siete categorias de health en reposo", flush=True)

        try:
            attempted = True
            start = board.command("inject deadlock")
            if "inject deadlock: 2 tareas" not in start:
                raise RuntimeError(f"inyeccion no confirmada: {start!r}")
            board.pump(0.2)
            check_deadlock(board.command("deadlock"), 2)
            check_health(board.command("health"), deadlock=True)
            print("OK: chequeo manual y health muestran el ciclo", flush=True)
        except BaseException as error:
            failure = error
        finally:
            if attempted:
                for attempt in range(2):
                    try:
                        stop = board.command("inject stop deadlock")
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
        check_deadlock(board.command("deadlock"), 0)
        check_health(board.command("health"), deadlock=False)
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != before):
            raise RuntimeError(f"limpieza incompleta: {status!r} {sync!r} "
                               f"heap={after!r}, base={before!r}")
        print("OK: stop, siete categorias, heap y grafo restaurados", flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
