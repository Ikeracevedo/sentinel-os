#!/usr/bin/env python3
"""Bateria E11 de cargas light, nominal y heavy con CSV crudo.

Ejecutar desde Windows, con firmware TRANSPORT=1 ya grabado:
    py tools/testbench/run.py --port COM8 --variant trace-on --profile nominal --duration 10

Agregar --flash --busid 1-5 para compilar y grabar desde WSL en la misma orden.
Los demas escenarios se incorporaran en otra iteracion. Ninguna fila se
presenta como medicion valida si falla la limpieza.
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from bridge import Sink  # noqa: E402
from e09_verify import Board  # noqa: E402
from monitor import ResourceMonitor  # noqa: E402

STATS_FIELDS = ("uptime_ticks", "heap_total", "heap_free", "heap_largest",
                "heap_peak", "heap_blocks", "heap_free_blocks",
                "events_emitted", "events_dropped", "switch_count",
                "switch_min_cycles", "switch_max_cycles", "switch_avg_cycles",
                "task_count", "trace_enabled", "cpu_idle_pct", "reserved")
RESULT_FIELDS = ("timestamp_utc", "commit", "git_dirty", "variant", "profile", "run",
                 "duration_s", "iterations", "alloc_failures", "stats_samples",
                 "events_emitted_delta", "events_dropped_delta", "switch_count_delta",
                 "crc_errors_delta", "frame_gaps_delta", "uart_bytes", "status")
LOAD_WORKERS = {"light": 2, "nominal": 5, "heavy": 8}


class StatsCsv(Sink):
    def __init__(self, path: Path, metadata: dict[str, str], run: int) -> None:
        self.file = path.open("w", newline="", encoding="utf-8")
        self.raw_file = path.with_suffix(".uart").open("wb")
        self.writer = csv.DictWriter(self.file, fieldnames=("timestamp_utc", "commit",
            "git_dirty",
            "variant", "profile", "run", *STATS_FIELDS))
        self.writer.writeheader()
        self.metadata = metadata
        self.run = run
        self.samples: list[dict] = []
        self.uart_bytes = 0
        self.active = False

    def write(self, kind: str, data: dict) -> None:
        if kind == "raw" and self.active:
            chunk = data["bytes"]
            self.raw_file.write(chunk)
            self.uart_bytes += len(chunk)
        elif kind == "stats" and self.active:
            self.samples.append(data.copy())
            self.writer.writerow({"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                  **self.metadata, "run": self.run, **data})
            self.file.flush()

    def close(self) -> None:
        self.file.close()
        self.raw_file.close()


def git_commit() -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                   text=True).strip()


def git_dirty() -> bool:
    return bool(subprocess.check_output(["git", "status", "--porcelain"],
                                        cwd=ROOT, text=True).strip())


def load_counters(text: str, profile: str) -> tuple[int, int]:
    import re
    match = re.search(r"tiempo=\d+ ms ciclos=(\d+) .*fallos_alloc=(\d+)", text)
    if match is None or f"load: perfil={profile}" not in text:
        raise RuntimeError(f"estado de carga incompleto: {text!r}")
    return int(match.group(1)), int(match.group(2))


def initial_status(board: Board) -> str:
    """Reintentar solo la consulta inicial, segura si el puerto acaba de abrirse."""
    for attempt in range(2):
        try:
            return board.command("inject status")
        except RuntimeError:
            print(f"sin respuesta inicial (intento {attempt + 1}/2): "
                  f"{board.bridge.report()} hello={board.bridge.hello_count} "
                  f"texto_parcial={''.join(board.capture.logs)[-200:]!r}",
                  file=sys.stderr, flush=True)
            if attempt:
                raise
            board.pump(2.0)
    raise AssertionError("reintento inicial inalcanzable")


def run_one(board: Board, sink: StatsCsv, duration: float,
            variant: str, profile: str) -> dict[str, int]:
    board.bridge.sinks.append(sink)
    expected_trace = int(variant == "trace-on")
    before_crc = board.bridge.decoder.crc_errors
    before_gaps = board.bridge.decoder.gaps
    started = False
    try:
        status = initial_status(board)
        if "sin escenarios activos" not in status:
            raise RuntimeError(f"escenario previo activo: {status!r}")
        # El comando pudo llegar a la placa aunque se pierda su respuesta.
        # En ese caso tambien intentamos detener la carga.
        started = True
        response = board.command(f"inject load {profile}")
        if f"inject load {profile}: {LOAD_WORKERS[profile]} tareas" not in response:
            raise RuntimeError(f"inicio de carga no confirmado: {response!r}")
        sink.active = True
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            board.pump(min(0.5, deadline - time.monotonic()))
        sink.active = False
        iterations, failures = load_counters(board.command("inject status"), profile)
        if iterations == 0 or (failures and profile != "heavy"):
            raise RuntimeError(f"carga sin avance o con fallos: {iterations}, {failures}")
    finally:
        sink.active = False
        if started:
            stopped = board.command("inject stop load")
            if "detenido y limpio" not in stopped:
                raise RuntimeError(f"limpieza no confirmada: {stopped!r}")
    if "sin escenarios activos" not in board.command("inject status"):
        raise RuntimeError("el escenario quedo activo")
    samples = sink.samples
    if len(samples) < 2:
        raise RuntimeError(f"solo {len(samples)} muestras de telemetria")
    if any(sample["trace_enabled"] != expected_trace for sample in samples):
        raise RuntimeError("TRACE observado no coincide con la variante declarada")
    first, last = samples[0], samples[-1]
    for name in ("events_emitted", "events_dropped", "switch_count"):
        if last[name] < first[name]:
            raise RuntimeError(f"contador {name} retrocedio; posible reinicio")
    return {"iterations": iterations, "alloc_failures": failures,
            "stats_samples": len(samples),
            "events_emitted_delta": last["events_emitted"] - first["events_emitted"],
            "events_dropped_delta": last["events_dropped"] - first["events_dropped"],
            "switch_count_delta": last["switch_count"] - first["switch_count"],
            "crc_errors_delta": board.bridge.decoder.crc_errors - before_crc,
            "frame_gaps_delta": board.bridge.decoder.gaps - before_gaps,
            "uart_bytes": sink.uart_bytes}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--variant", choices=("trace-on", "trace-off"), required=True)
    profiles = parser.add_mutually_exclusive_group()
    profiles.add_argument("--profile", choices=tuple(LOAD_WORKERS), default="nominal")
    profiles.add_argument("--all-profiles", action="store_true",
                          help="ejecutar light, nominal y heavy en una sola bateria")
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    parser.add_argument("--flash", action="store_true",
                        help="compilar/flashear en WSL y devolver el USB a Windows")
    parser.add_argument("--busid", help="BUSID de usbipd, por ejemplo 1-5")
    parser.add_argument("--distro", default="ubuntu", help="distribucion WSL")
    parser.add_argument("--live-resources", action="store_true",
                        help="mostrar CPU, heap y perdidas durante cada corrida")
    args = parser.parse_args()
    if args.duration < 2 or args.repeats < 5:
        parser.error("--duration debe ser >= 2 s y --repeats >= 5")
    if args.flash and not args.busid:
        parser.error("--flash requiere --busid")
    if args.variant == "trace-off":
        parser.error("TRANSPORT=1 requiere TRACE=1; OFF usa overhead.py por ahora")
    if args.flash:
        from flash import flash_board
        flash_board(busid=args.busid, port=args.port, distro=args.distro,
                    variant=args.variant, benchmark=False)
    commit = git_commit()
    metadata = {"commit": commit, "git_dirty": str(git_dirty()).lower(),
                "variant": args.variant}
    selected_profiles = tuple(LOAD_WORKERS) if args.all_profiles else (args.profile,)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    result_path = output / "resultados.csv"
    if result_path.exists() and result_path.stat().st_size:
        with result_path.open(newline="", encoding="utf-8") as stream:
            if tuple(next(csv.reader(stream))) != RESULT_FIELDS:
                raise RuntimeError(f"cabecera incompatible en {result_path}")
    print(f"commit={commit} git_dirty={metadata['git_dirty']} "
          f"puerto={args.port} variante={args.variant} "
          f"perfiles={','.join(selected_profiles)}; "
          f"{args.repeats} corridas por perfil de {args.duration:g} s", flush=True)
    board = Board(args.port, args.baud, None)
    if args.live_resources:
        board.bridge.sinks.append(ResourceMonitor())
    link_error_runs = 0
    try:
        for profile in selected_profiles:
            profile_metadata = {**metadata, "profile": profile}
            for run in range(1, args.repeats + 1):
                raw_path = output / f"stats_{stamp}_{args.variant}_{profile}_r{run:02d}.csv"
                sink = StatsCsv(raw_path, profile_metadata, run)
                try:
                    data = run_one(board, sink, args.duration, args.variant, profile)
                except Exception:
                    print(f"FAIL {profile} corrida {run}: datos parciales conservados en {raw_path}",
                          file=sys.stderr, flush=True)
                    raise
                finally:
                    board.bridge.sinks.remove(sink)
                    sink.close()
                row = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                       **profile_metadata, "run": run, "duration_s": args.duration,
                       **data,
                       "status": ("PASS_LINK_ERRORS" if data["crc_errors_delta"] or
                                  data["frame_gaps_delta"] else "PASS")}
                link_error_runs += row["status"] == "PASS_LINK_ERRORS"
                with result_path.open("a", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=RESULT_FIELDS)
                    if stream.tell() == 0:
                        writer.writeheader()
                    writer.writerow(row)
                print(f"{row['status']} {profile} {run}/{args.repeats}: "
                      f"{data['stats_samples']} stats, "
                      f"{data['iterations']} iteraciones de carga, "
                      f"{data['alloc_failures']} fallos de reserva, "
                      f"{data['events_dropped_delta']} descartes en placa, "
                      f"{data['crc_errors_delta']} errores CRC, "
                      f"{data['frame_gaps_delta']} tramas ausentes, "
                      f"{data['uart_bytes']} bytes UART", flush=True)
    finally:
        board.close()
    outcome = "PASS" if link_error_runs == 0 else "COMPLETE_WITH_LINK_ERRORS"
    print(f"{outcome} bateria {','.join(selected_profiles)}; "
          f"{link_error_runs} corridas con errores de enlace; CSV: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
