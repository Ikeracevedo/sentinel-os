"""Carga nominal de E10 con vigilancia continua y parada recuperable.

Prueba corta: py tools/e10_nominal_board.py --port COM8 --duration 30
Observacion de una hora: py tools/e10_nominal_board.py --port COM8 --duration 3600
"""

from __future__ import annotations

import argparse
import re
import time

from bridge import Sink
from e09_verify import Board, heap_state
from e10_graph_board import detector_state, edges_and_cycles


class AlertCounter(Sink):
    def __init__(self) -> None:
        self.event_alerts = 0
        self.frame_alerts = 0

    def write(self, kind: str, data: dict) -> None:
        if kind == "event" and data["type"] == 0x50:
            self.event_alerts += 1
        elif kind == "alert":
            self.frame_alerts += 1


def load_status(output: str) -> tuple[int, int, int]:
    if "load: perfil=nominal" not in output:
        raise RuntimeError(f"carga nominal no activa: {output!r}")
    match = re.search(r"tiempo=(\d+) ms ciclos=(\d+) .*fallos_alloc=(\d+)", output)
    if match is None:
        raise RuntimeError(f"estado de carga incompleto: {output!r}")
    return tuple(map(int, match.groups()))


def pathology_alerts(output: str) -> tuple[int, int]:
    frag = re.search(r"frag_alertas=(\d+)", output)
    starve = re.search(r"inanicion_alertas=(\d+)", output)
    if frag is None or starve is None:
        raise RuntimeError(f"contadores de patologias incompletos: {output!r}")
    return int(frag.group(1)), int(starve.group(1))


def no_cycle(output: str) -> None:
    edges, cycles = edges_and_cycles(output)
    if "sin ciclos" not in output or cycles >= 2000:
        raise RuntimeError(f"grafo nominal inesperado: {output!r}")
    print(f"grafo nominal: {len(edges)} aristas, sin ciclos; "
          f"captura={cycles / 40:.1f} us", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--duration", type=int, default=3600,
                        help="segundos de carga; 3600 para observar una hora")
    args = parser.parse_args()
    if args.duration < 1:
        parser.error("--duration debe ser positivo")

    # Una hora de eventos de switch puede llenar gigabytes de SQLite. Esta
    # prueba consume y cuenta las tramas en memoria sin guardar cada evento.
    board = Board(args.port, args.baud, None)
    observed = AlertCounter()
    board.bridge.sinks.append(observed)
    attempted = False
    failure: BaseException | None = None
    before: tuple[int, int] | None = None
    try:
        board.pump(1.0)
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("hay un escenario activo antes de comenzar")
        before = heap_state(board.command("free"))
        no_cycle(board.command("graph"))
        detector_before = board.command("detector")
        runs_before, alerts_before, _ = detector_state(detector_before)
        pathologies_before = pathology_alerts(detector_before)
        if runs_before == 0:
            raise RuntimeError("detector sin chequeos periodicos")
        print(f"base: heap libre={before[0]} B, bloques={before[1]}; "
              f"detector={runs_before} chequeos, {alerts_before} alertas",
              flush=True)

        try:
            attempted = True
            start = board.command("inject load nominal")
            if "inject load nominal: 5 tareas" not in start:
                raise RuntimeError(f"carga no confirmada: {start!r}")
            started_at = time.monotonic()
            next_report = 30.0 if args.duration < 3600 else 60.0
            previous_iterations = 0
            while True:
                elapsed = time.monotonic() - started_at
                if elapsed >= args.duration:
                    break
                board.pump(min(10.0, args.duration - elapsed))
                if observed.event_alerts or observed.frame_alerts:
                    raise RuntimeError("llego una alerta durante carga nominal")
                elapsed = time.monotonic() - started_at
                if elapsed < next_report and elapsed < args.duration:
                    continue
                status = board.command("inject status")
                load_ms, iterations, alloc_failures = load_status(status)
                if iterations <= previous_iterations or alloc_failures != 0:
                    raise RuntimeError(f"carga sin avance o con fallos: {status!r}")
                previous_iterations = iterations
                no_cycle(board.command("graph"))
                detector_text = board.command("detector")
                runs, alerts, max_cycles = detector_state(detector_text)
                if (alerts != alerts_before or
                        pathology_alerts(detector_text) != pathologies_before or
                        max_cycles >= 4000):
                    raise RuntimeError("falsa alerta o coste >100 us: "
                                       f"chequeos={runs}, alertas={alerts}, "
                                       f"maximo={max_cycles} ciclos")
                if observed.event_alerts or observed.frame_alerts:
                    raise RuntimeError("llego una alerta durante carga nominal")
                print(f"[{elapsed:.0f}/{args.duration} s] "
                      f"carga={load_ms / 1000:.0f} s, ciclos={iterations}, "
                      f"detector={runs} chequeos, alertas nuevas=0, "
                      f"maximo={max_cycles / 40:.1f} us", flush=True)
                next_report += 30.0 if args.duration < 3600 else 60.0

            # Comprobacion final aunque el ultimo intervalo no termine en
            # un minuto exacto.
            _, iterations, alloc_failures = load_status(board.command("inject status"))
            detector_text = board.command("detector")
            runs, alerts, max_cycles = detector_state(detector_text)
            no_cycle(board.command("graph"))
            if (iterations == 0 or iterations < previous_iterations or
                    alloc_failures != 0 or alerts != alerts_before or
                    pathology_alerts(detector_text) != pathologies_before or
                    max_cycles >= 4000 or observed.event_alerts or
                    observed.frame_alerts):
                raise RuntimeError("condiciones nominales no cumplidas al final")
            if runs - runs_before < args.duration * 2 - 4:
                raise RuntimeError("el detector no ejecuto suficientes chequeos")
            print(f"observacion: {args.duration} s, sin alertas falsas; "
                  f"{runs - runs_before} chequeos; maximo={max_cycles / 40:.1f} us",
                  flush=True)
        except BaseException as error:
            failure = error
        finally:
            if attempted:
                try:
                    stop = board.command("inject stop load")
                    print(stop, flush=True)
                    if "detenido y limpio" not in stop:
                        print("ATENCION: stop no confirmado", flush=True)
                except Exception as error:
                    print(f"ATENCION: stop sin confirmacion: {error}", flush=True)

        status = board.command("inject status")
        sync = board.command("sync")
        after = heap_state(board.command("free"))
        no_cycle(board.command("graph"))
        if ("sin escenarios activos" not in status or "0 mutex" not in sync or
                "0 tareas bloqueadas" not in sync or after != before):
            raise RuntimeError(f"limpieza incompleta: status={status!r}, "
                               f"sync={sync!r}, heap={after!r}, base={before!r}")
        print("OK: load detenido; mutexes, tareas, heap y grafo restaurados",
              flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        print(f"OK: {args.duration} s nominales sin falsos positivos de "
              "deadlock, fragmentacion ni inanicion", flush=True)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
