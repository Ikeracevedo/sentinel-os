"""Comprueba inversion de prioridad y recupera la placa con inject stop.

Uso: py tools/e10_prio_inv_board.py --port COM8
"""

from __future__ import annotations

import argparse
import re

from e09_verify import Board, heap_state
from e10_graph_board import check_graph, detector_state


def inversion_state(output: str) -> tuple[int, int, int]:
    match = re.search(r"inversion_chequeos=(\d+) inversion_alertas=(\d+) "
                      r"inversion_max=(\d+) ms", output)
    if match is None:
        raise RuntimeError(f"estadisticas de inversion incompletas: {output!r}")
    return tuple(map(int, match.groups()))


def inversion_alert(output: str) -> tuple[int, ...]:
    match = re.search(
        r"\[ALERTA\] INVERSION: alta=alta\(tid (\d+),p(\d+)\) "
        r"baja=baja\(tid (\d+),p(\d+)\) "
        r"media=media\(tid (\d+),p(\d+)\) "
        r"mutex=(\d+) duracion=(\d+) ms bloqueo=(\d+) ms", output)
    if match is None:
        raise RuntimeError(f"alerta de inversion incompleta: {output!r}")
    return tuple(map(int, match.groups()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    board = Board(args.port, args.baud, None)
    attempted = False
    failure: BaseException | None = None
    logs = ""
    try:
        board.pump(1.0)
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("hay un escenario activo antes de comenzar")
        before = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        baseline = board.command("detector")
        checks_before, alerts_before, _ = inversion_state(baseline)
        if checks_before == 0:
            raise RuntimeError(f"chequeo de inversion inactivo: {baseline!r}")
        print(f"base: heap libre={before[0]} B, bloques={before[1]}; "
              f"inversion={alerts_before} alertas historicas", flush=True)

        try:
            attempted = True
            start = board.command("inject prio-inv")
            if "baja(7) posee M, alta(1) espera M, media(4) ocupa CPU" not in start:
                raise RuntimeError(f"inyeccion no confirmada: {start!r}")
            board.pump(0.8)
            logs = start + "".join(board.capture.logs)
        except BaseException as error:
            failure = error
        finally:
            if attempted:
                for attempt in range(2):
                    try:
                        stop = board.command("inject stop prio-inv")
                        print(stop, flush=True)
                        logs += stop
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
        print("OK: prio-inv detenido; tareas, mutexes, heap y grafo restaurados",
              flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        high_tid, high_prio, low_tid, low_prio, medium_tid, medium_prio, mutex_id, duration, blocked = inversion_alert(logs)
        if (len({high_tid, low_tid, medium_tid}) != 3 or
                (high_prio, medium_prio, low_prio) != (1, 4, 7) or
                mutex_id == 0 or duration < 100 or blocked < duration):
            raise RuntimeError(f"datos de inversion incorrectos: {logs!r}")
        detector_text = board.command("detector")
        checks, alerts, max_duration = inversion_state(detector_text)
        _, _, max_cycles = detector_state(detector_text)
        if (checks <= checks_before or alerts != alerts_before + 1 or
                max_duration < duration or max_cycles >= 4000):
            raise RuntimeError(f"contador o coste inesperado: {detector_text!r}")
        print(f"inversion: alta(tid {high_tid},p1) espera a "
              f"baja(tid {low_tid},p7); media(tid {medium_tid},p4) "
              f"ocupo CPU durante {duration} ms; detector maximo "
              f"{max_cycles / 40:.1f} us", flush=True)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
