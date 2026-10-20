"""Valida la alerta de jitter con umbral temporal y lo restaura siempre.

Uso: py tools/e10_jitter_board.py --port COM8
"""

from __future__ import annotations

import argparse
import re

from e09_verify import Board, heap_state
from e10_graph_board import check_graph, detector_state
from e10_nominal_board import jitter_metrics


def jitter_state(output: str) -> tuple[int, int, int]:
    match = re.search(r"jitter_chequeos=(\d+) jitter_alertas=(\d+) "
                      r"jitter_umbral=(\d+) us", output)
    if match is None:
        if "jitter: muestras=" in output and "jitter_chequeos=" not in output:
            raise RuntimeError(
                "la placa ejecuta el firmware anterior, que mide jitter pero "
                "aun no tiene su detector; flashea build/transport/kernel.bin "
                "y repite esta prueba"
            )
        raise RuntimeError(f"estadisticas de jitter incompletas: {output!r}")
    return tuple(map(int, match.groups()))


def jitter_alert(output: str) -> tuple[int, int, int, int, int, int]:
    match = re.search(r"\[ALERTA\] JITTER: desviacion=(\d+) us "
                      r"umbral=(\d+) us media=(\d+) us min=(\d+) us "
                      r"max=(\d+) us muestras=(\d+)", output)
    if match is None:
        raise RuntimeError(f"alerta de jitter incompleta: {output!r}")
    return tuple(map(int, match.groups()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    board = Board(args.port, args.baud, None)
    lowered = False
    load_attempted = False
    failure: BaseException | None = None
    logs = ""
    before = None
    checks_before = alerts_before = 0
    try:
        board.pump(1.0)
        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("hay un escenario activo antes de comenzar")
        before = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        baseline = board.command("detector")
        checks_before, alerts_before, threshold = jitter_state(baseline)
        samples, minimum, maximum, mean, deviation = jitter_metrics(baseline)
        if (checks_before == 0 or threshold != 50 or samples != 64 or
                not minimum <= mean <= maximum or deviation > 50):
            raise RuntimeError(f"base de jitter inesperada: {baseline!r}")
        if "jitter de switch OK" not in board.command("health"):
            raise RuntimeError("health no muestra el jitter inicial")
        print(f"base: jitter={mean}±{deviation} us [{minimum},{maximum}]; "
              f"umbral={threshold} us", flush=True)

        try:
            lowered = True
            start = board.command("detector jitter 1")
            load_attempted = True
            load = board.command("inject load nominal")
            if "inject load nominal: 5 tareas" not in load:
                raise RuntimeError(f"carga no confirmada: {load!r}")
            board.pump(2.2)
            logs = start + load + "".join(board.capture.logs)
            active = board.command("detector")
            checks, alerts, active_threshold = jitter_state(active)
            _, _, max_cycles = detector_state(active)
            if (checks <= checks_before or alerts != alerts_before + 1 or
                    active_threshold != 1 or max_cycles >= 4000):
                raise RuntimeError(f"alerta o coste inesperado: {active!r}")
            deviation_alert, threshold_alert, alert_mean, alert_min, alert_max, alert_samples = jitter_alert(logs)
            if (deviation_alert <= threshold_alert or threshold_alert != 1 or
                    alert_samples != 64 or not alert_min <= alert_mean <= alert_max):
                raise RuntimeError(f"datos de alerta incorrectos: {logs!r}")
            print(f"alerta: desviacion={deviation_alert} us > "
                  f"umbral={threshold_alert} us; detector maximo "
                  f"{max_cycles / 40:.1f} us", flush=True)
        except BaseException as error:
            failure = error
        finally:
            if load_attempted:
                for attempt in range(2):
                    try:
                        stop = board.command("inject stop load")
                        print(stop, flush=True)
                        if "detenido y limpio" in stop:
                            break
                        print("ATENCION: stop sin confirmacion", flush=True)
                    except Exception as error:
                        print(f"ATENCION: stop sin confirmacion: {error}", flush=True)
                    if attempt == 0:
                        board.pump(1.0)
            if lowered:
                try:
                    restored = board.command("detector jitter 50")
                    print("umbral restaurado a 50 us", flush=True)
                    if jitter_state(restored)[2] != 50:
                        raise RuntimeError(f"umbral no restaurado: {restored!r}")
                except Exception as error:
                    print(f"ATENCION: umbral sin restaurar: {error}", flush=True)
                    if failure is None:
                        failure = error

        if "sin escenarios activos" not in board.command("inject status"):
            raise RuntimeError("escenario activo al terminar")
        sync = board.command("sync")
        if "0 mutex" not in sync or "0 tareas bloqueadas" not in sync:
            raise RuntimeError(f"mutex o bloqueo residual: {sync!r}")
        after = heap_state(board.command("free"))
        check_graph(board.command("graph"))
        if after != before:
            raise RuntimeError(f"heap alterado: {before!r} -> {after!r}")
        health = board.command("health")
        if "umbral=50 us" not in health or "jitter de switch OK" not in health:
            raise RuntimeError(f"health no confirmo recuperacion: {health!r}")
        print("OK: umbral, heap, grafo y estado nominal restaurados", flush=True)
        print(board.bridge.report(), flush=True)
        if failure is not None:
            raise failure
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
