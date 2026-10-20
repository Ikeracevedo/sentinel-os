"""Valida el registro, FRAME_ALERT y SQLite con un deadlock recuperable.

Uso: py tools/e10_alerts_board.py --port COM8
"""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path

from bridge import Sink
from e09_verify import Board, heap_state
from e10_graph_board import check_graph


class AlertCapture(Sink):
    def __init__(self) -> None:
        self.alerts: list[dict] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "alert":
            self.alerts.append(data)


def require_alert_list(text: str, count: int) -> None:
    if f"alerts: {count} recientes" not in text:
        raise RuntimeError(f"registro de alertas inesperado: {text!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    db = Path("build/transport/e10-alerts.db")
    db.parent.mkdir(parents=True, exist_ok=True)
    board = Board(args.port, args.baud, db)
    capture = AlertCapture()
    board.bridge.sinks.append(capture)
    attempted = False
    failure: BaseException | None = None
    baseline = None
    try:
        board.pump(1.0)
        try:
            status = board.command("inject status")
        except RuntimeError:
            # Abrir COM puede coincidir con el arranque; esta consulta es inocua.
            board.pump(1.0)
            status = board.command("inject status", timeout=8.0)
        if "sin escenarios activos" not in status:
            raise RuntimeError("hay un escenario activo antes de comenzar")
        preflight = board.command("alerts")
        if "alerts: " not in preflight or "recientes" not in preflight:
            raise RuntimeError(
                "firmware anterior en la placa: falta el comando alerts; "
                "flashea build/transport/kernel.bin antes de esta prueba"
            )
        baseline = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        board.command("alerts -c")
        require_alert_list(board.command("alerts"), 0)

        try:
            attempted = True
            start = board.command("inject deadlock")
            if "inject deadlock: 2 tareas" not in start:
                raise RuntimeError(f"inyeccion no confirmada: {start!r}")
            board.pump(0.8)
            check_graph(board.command("graph"), expect_cycle=2)
            listing = board.command("alerts")
            require_alert_list(listing, 1)
            if "CRIT DEADLOCK: Ciclo de espera" not in listing:
                raise RuntimeError(f"alerta ausente del registro: {listing!r}")
            observed = [a for a in capture.alerts if a.get("type") == 1]
            if len(observed) != 1 or observed[0].get("severity") != 2:
                raise RuntimeError(f"FRAME_ALERT incorrecto: {capture.alerts!r}")
            board.pump(1.1)
            require_alert_list(board.command("alerts"), 1)
            print("OK: registro y FRAME_ALERT con deadlock; sin repeticion", flush=True)
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
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != baseline):
            raise RuntimeError(f"limpieza incompleta: {status!r} {sync!r} "
                               f"heap={after!r}, base={baseline!r}")
        board.command("alerts -c")
        require_alert_list(board.command("alerts"), 0)
        print("OK: stop, heap, grafo y registro restaurados", flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
    finally:
        board.close()

    with sqlite3.connect(db) as conn:
        row = conn.execute("""SELECT seq,tiempo_ms,severidad,tipo,dato0,dato1,dato2
            FROM alertas WHERE tipo=1 ORDER BY id DESC LIMIT 1""").fetchone()
    if row is None or row[0] != observed[0]["sequence"] or row[2] != 2:
        raise RuntimeError(f"FRAME_ALERT no persistio en SQLite: {row!r}")
    print(f"OK: alerta #{row[0]} persistida en {db}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
