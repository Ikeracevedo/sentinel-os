#!/usr/bin/env python3
"""Monitor de recursos E11; reproduce UART o escucha un puerto sin enviar comandos.

La ocupacion de CPU es una estimacion obtenida de 100 - cpu_idle_pct.
No mide temperatura, corriente ni consumo electrico.
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from bridge import Bridge, FileSink, Sink  # noqa: E402

FIELDS = ("timestamp_utc", "uptime_ticks", "cpu_idle_pct", "cpu_busy_est_pct",
          "heap_total", "heap_free", "heap_largest", "heap_blocks",
          "heap_free_blocks", "fragmentation_est_pct", "task_count",
          "trace_enabled", "events_emitted", "events_dropped", "switch_count")


class ResourceMonitor(Sink):
    """Muestra cada FRAME_STATS; se puede adjuntar a otro Bridge ya abierto."""

    def __init__(self, csv_path: Path | None = None) -> None:
        self.samples = 0
        self.busy_streak = 0
        self.last_dropped: int | None = None
        self.csv_file = None
        self.writer = None
        if csv_path is not None:
            csv_path.parent.mkdir(parents=True, exist_ok=True)
            self.csv_file = csv_path.open("w", newline="", encoding="utf-8")
            self.writer = csv.DictWriter(self.csv_file, fieldnames=FIELDS)
            self.writer.writeheader()

    def write(self, kind: str, data: dict) -> None:
        if kind != "stats":
            return
        free = data["heap_free"]
        busy = 100 - data["cpu_idle_pct"]
        frag = round(100 * (1 - data["heap_largest"] / free), 1) if free else None
        row = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
               **{key: data[key] for key in FIELDS if key in data},
               "cpu_busy_est_pct": busy, "fragmentation_est_pct": frag}
        self.samples += 1
        if self.writer is not None:
            self.writer.writerow(row)
            self.csv_file.flush()
        print(f"uptime={data['uptime_ticks']} ticks | CPU ocupada ~{busy}% "
              f"(idle {data['cpu_idle_pct']}%) | heap {free}/{data['heap_total']} B "
              f"| hueco mayor {data['heap_largest']} B | tareas {data['task_count']} "
              f"| traza {data['trace_enabled']} | eventos perdidos {data['events_dropped']}",
              flush=True)
        self.busy_streak = self.busy_streak + 1 if busy >= 95 else 0
        if self.busy_streak == 5:
            print("AVISO: ocupacion estimada >=95% durante cinco muestras; "
                  "esto no mide temperatura.", flush=True)
        if data["heap_total"] and free * 10 < data["heap_total"]:
            print("AVISO: menos del 10% del heap esta libre.", flush=True)
        dropped = data["events_dropped"]
        if self.last_dropped is not None and dropped > self.last_dropped:
            print(f"AVISO: {dropped - self.last_dropped} eventos perdidos "
                  "desde la muestra anterior.", flush=True)
        self.last_dropped = dropped

    def close(self) -> None:
        if self.csv_file is not None:
            self.csv_file.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--replay", type=Path, help="UART crudo existente; no usa placa")
    source.add_argument("--port", help="Puerto serie Windows; solo lectura")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--duration", type=float, default=0,
                        help="Segundos en modo puerto; 0 = hasta Ctrl+C")
    parser.add_argument("--csv", type=Path, help="Guardar muestras decodificadas")
    parser.add_argument("--raw", type=Path, help="Guardar todos los bytes UART recibidos")
    args = parser.parse_args()
    if args.duration < 0:
        parser.error("--duration debe ser >= 0")
    monitor = ResourceMonitor(args.csv)
    sinks: list[Sink] = [monitor]
    if args.raw is not None:
        args.raw.parent.mkdir(parents=True, exist_ok=True)
        sinks.append(FileSink(str(args.raw)))
    bridge = Bridge(sinks)
    try:
        if args.replay is not None:
            with args.replay.open("rb") as stream:
                while chunk := stream.read(4096):
                    bridge.feed(chunk)
        else:
            import serial
            port = serial.Serial(port=None, baudrate=args.baud, timeout=0.2)
            port.dtr = False
            port.rts = False
            port.port = args.port
            try:
                port.open()
                deadline = time.monotonic() + args.duration if args.duration else None
                while deadline is None or time.monotonic() < deadline:
                    chunk = port.read(4096)
                    if chunk:
                        bridge.feed(chunk)
            except KeyboardInterrupt:
                pass
            finally:
                port.close()
    finally:
        bridge.close()
    print(f"{monitor.samples} muestras de recursos; {bridge.report()}")
    if monitor.samples == 0:
        print("No se recibieron tramas STATS; comprobar imagen binaria y puerto.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
