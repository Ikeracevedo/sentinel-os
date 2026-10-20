#!/usr/bin/env python3
"""Primera bateria E11: cinco ventanas de carga nominal con CSV crudo.

Ejecutar desde Windows, con firmware TRANSPORT=1 ya grabado:
    py tools/testbench/run.py --port COM8 --variant trace-on --duration 10

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

STATS_FIELDS = ("uptime_ticks", "heap_total", "heap_free", "heap_largest",
                "heap_peak", "heap_blocks", "heap_free_blocks",
                "events_emitted", "events_dropped", "switch_count",
                "switch_min_cycles", "switch_max_cycles", "switch_avg_cycles",
                "task_count", "trace_enabled", "cpu_idle_pct", "reserved")
RESULT_FIELDS = ("timestamp_utc", "commit", "git_dirty", "variant", "profile", "run",
                 "duration_s", "iterations", "alloc_failures", "stats_samples",
                 "events_emitted_delta", "events_dropped_delta", "switch_count_delta",
                 "crc_errors_delta", "frame_gaps_delta", "status")


class StatsCsv(Sink):
    def __init__(self, path: Path, metadata: dict[str, str], run: int) -> None:
        self.file = path.open("w", newline="", encoding="utf-8")
        self.writer = csv.DictWriter(self.file, fieldnames=("timestamp_utc", "commit",
            "git_dirty",
            "variant", "profile", "run", *STATS_FIELDS))
        self.writer.writeheader()
        self.metadata = metadata
        self.run = run
        self.samples: list[dict] = []
        self.active = False

    def write(self, kind: str, data: dict) -> None:
        if kind == "stats" and self.active:
            self.samples.append(data.copy())
            self.writer.writerow({"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                  **self.metadata, "run": self.run, **data})
            self.file.flush()

    def close(self) -> None:
        self.file.close()


def git_commit() -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                   text=True).strip()


def git_dirty() -> bool:
    return bool(subprocess.check_output(["git", "status", "--porcelain"],
                                        cwd=ROOT, text=True).strip())


def load_counters(text: str) -> tuple[int, int]:
    import re
    match = re.search(r"tiempo=\d+ ms ciclos=(\d+) .*fallos_alloc=(\d+)", text)
    if match is None or "load: perfil=nominal" not in text:
        raise RuntimeError(f"estado de carga incompleto: {text!r}")
    return int(match.group(1)), int(match.group(2))


def run_one(board: Board, sink: StatsCsv, duration: float,
            variant: str) -> dict[str, int]:
    board.bridge.sinks.append(sink)
    expected_trace = int(variant == "trace-on")
    before_crc = board.bridge.decoder.crc_errors
    before_gaps = board.bridge.decoder.gaps
    started = False
    try:
        status = board.command("inject status")
        if "sin escenarios activos" not in status:
            raise RuntimeError(f"escenario previo activo: {status!r}")
        # El comando pudo llegar a la placa aunque se pierda su respuesta.
        # En ese caso tambien intentamos detener la carga.
        started = True
        response = board.command("inject load nominal")
        if "inject load nominal: 5 tareas" not in response:
            raise RuntimeError(f"inicio de carga no confirmado: {response!r}")
        sink.active = True
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            board.pump(min(0.5, deadline - time.monotonic()))
        sink.active = False
        iterations, failures = load_counters(board.command("inject status"))
        if iterations == 0 or failures:
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
            "frame_gaps_delta": board.bridge.decoder.gaps - before_gaps}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--variant", choices=("trace-on", "trace-off"), required=True)
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--output", type=Path, default=ROOT / "experiments" / "e11")
    parser.add_argument("--flash", action="store_true",
                        help="compilar/flashear en WSL y devolver el USB a Windows")
    parser.add_argument("--busid", help="BUSID de usbipd, por ejemplo 1-5")
    parser.add_argument("--distro", default="ubuntu", help="distribucion WSL")
    args = parser.parse_args()
    if args.duration < 2 or args.repeats < 5:
        parser.error("--duration debe ser >= 2 s y --repeats >= 5")
    if args.flash and not args.busid:
        parser.error("--flash requiere --busid")
    if args.flash and args.variant == "trace-off":
        parser.error("TRANSPORT=1 requiere TRACE=1; OFF usa el benchmark UART")
    if args.flash:
        from flash import flash_board
        flash_board(busid=args.busid, port=args.port, distro=args.distro,
                    variant=args.variant, benchmark=False)
    commit = git_commit()
    metadata = {"commit": commit, "git_dirty": str(git_dirty()).lower(),
                "variant": args.variant, "profile": "nominal"}
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    result_path = output / "resultados.csv"
    print(f"commit={commit} git_dirty={metadata['git_dirty']} "
          f"puerto={args.port} variante={args.variant}; "
          f"{args.repeats} corridas de {args.duration:g} s", flush=True)
    board = Board(args.port, args.baud, None)
    try:
        for run in range(1, args.repeats + 1):
            raw_path = output / f"stats_{stamp}_{args.variant}_nominal_r{run:02d}.csv"
            sink = StatsCsv(raw_path, metadata, run)
            try:
                data = run_one(board, sink, args.duration, args.variant)
            except Exception:
                print(f"FAIL corrida {run}: datos parciales conservados en {raw_path}",
                      file=sys.stderr, flush=True)
                raise
            finally:
                board.bridge.sinks.remove(sink)
                sink.close()
            row = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                   **metadata, "run": run, "duration_s": args.duration,
                   **data, "status": "PASS"}
            with result_path.open("a", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=RESULT_FIELDS)
                if stream.tell() == 0:
                    writer.writeheader()
                writer.writerow(row)
            print(f"PASS {run}/{args.repeats}: {data['stats_samples']} stats, "
                  f"{data['iterations']} ciclos de carga, "
                  f"{data['events_dropped_delta']} eventos perdidos", flush=True)
    finally:
        board.close()
    print(f"PASS bateria nominal; CSV: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
