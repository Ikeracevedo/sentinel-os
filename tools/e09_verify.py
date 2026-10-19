"""E09: verificacion secuencial y recuperable de escenarios en la placa.

Uso: python tools/e09_verify.py --port COM9 --repeats 10
El script se detiene al primer fallo. No reinicia la placa automaticamente.
"""

from __future__ import annotations

import argparse
import json
import re
import time
from pathlib import Path

from bridge import Bridge, FRAME_CMD, Sink, SqliteSink, encode_frame


SCENARIOS = (
    ("deadlock", 1, 2),
    ("leak", 2, 1),
    ("frag", 3, 1),
    ("hog", 4, 1),
    ("prio-inv", 5, 3),
    ("stack-overflow", 7, 1),
)
BASE_FREE = 96676
BASE_BLOCKS = 19


class Capture(Sink):
    def __init__(self) -> None:
        self.logs: list[str] = []
        self.inject_events: list[tuple[int, int]] = []

    def write(self, kind: str, data: dict) -> None:
        if kind == "log":
            self.logs.append(data["text"])
        elif kind == "event" and data["type"] == 0x60:
            self.inject_events.append((data["arg0"], data["arg1"]))


class Board:
    def __init__(self, port: str, baud: int, db: Path | None) -> None:
        import serial

        self.capture = Capture()
        self.serial = serial.Serial(port=None, baudrate=baud, timeout=0.1)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        sinks = (self.capture,) if db is None else (self.capture, SqliteSink(str(db)))
        self.bridge = Bridge(sinks)
        self.pump(0.3)

    def close(self) -> None:
        self.serial.close()
        self.bridge.close()

    def pump(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            chunk = self.serial.read(4096)
            if chunk:
                self.bridge.feed(chunk)

    def command(self, command: str, timeout: float = 5.0) -> str:
        self.capture.logs.clear()
        self.serial.write(encode_frame(FRAME_CMD, 0, command.encode("ascii")))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.serial.read(4096)
            if chunk:
                self.bridge.feed(chunk)
                result = "".join(self.capture.logs)
                echo = result.find(command)
                if echo >= 0 and "sentinel:/" in result[echo + len(command):]:
                    return result
        raise RuntimeError(f"sin respuesta completa a {command!r}")


def require(condition: bool, detail: str) -> None:
    if not condition:
        raise RuntimeError(detail)


def heap_state(text: str) -> tuple[int, int]:
    free = re.search(r"^Libre:\s+(\d+) B", text, re.MULTILINE)
    blocks = re.search(r"^Bloques:\s+(\d+)", text, re.MULTILINE)
    require(free is not None and blocks is not None,
            f"salida de free incompleta: {text!r}")
    return int(free.group(1)), int(blocks.group(1))


def task_row(text: str, name: str) -> list[str]:
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 10 and parts[0].isdigit() and parts[1] == name:
            return parts
    raise RuntimeError(f"no aparece la tarea {name}: {text!r}")


def check_effect(board: Board, name: str, start_text: str) -> str:
    if name == "deadlock":
        board.pump(0.2)
        text = board.command("sync")
        require("2 tareas bloqueadas" in text and
                "dead_a(tid" in text and "dead_b(tid" in text,
                f"no se formo el ciclo de deadlock: {text!r}")
    elif name == "leak":
        board.pump(0.7)
        text = board.command("free")
        free, _ = heap_state(text)
        require(free < BASE_FREE, f"leak no redujo el heap: {text!r}")
    elif name == "frag":
        board.pump(0.3)
        text = start_text + "".join(board.capture.logs)
        require("kmalloc(4096)=FALLO" in text,
                f"frag no produjo fallo de reserva: {text!r}")
        free_text = board.command("free")
        free, _ = heap_state(free_text)
        frag = re.search(r"^Fragmentacion:\s+(\d+)%", free_text, re.MULTILINE)
        require(free > 30000 and frag is not None and int(frag.group(1)) > 50,
                f"frag no dejo suficientes huecos pequenos: {free_text!r}")
    elif name == "hog":
        first = task_row(board.command("ps -v hog"), "hog")
        board.pump(0.6)
        text = board.command("ps -v hog")
        second = task_row(text, "hog")
        require(int(second[7]) > int(first[7]),
                f"hog no acumulo tiempo de CPU: {first!r} -> {second!r}")
    elif name == "prio-inv":
        board.pump(0.2)
        text = board.command("sync")
        require("dueno=baja" in text and "alta(tid" in text,
                f"inversion sin mutex esperado: {text!r}")
        text = board.command("ps -v alta")
        row = task_row(text, "alta")
        require(row[2] == "BLOCKED" and int(row[-1]) > 0,
                f"alta no acumulo bloqueo: {text!r}")
    elif name == "stack-overflow":
        text = board.command("inject status")
        require("DESBORDAMIENTO DE PILA: tarea 'stack_ovf'" in text and
                "sin escenarios activos" in text,
                f"canario sin recuperacion confirmada: {text!r}")
        tasks = board.command("ps -v")
        require("stack_ovf" not in tasks and "shell" in tasks and
                "tracer" in tasks, f"tareas base afectadas: {tasks!r}")
    else:
        raise ValueError(name)
    return text


def run_one(board: Board, name: str, scenario_id: int, expected_tasks: int) -> dict:
    event_index = len(board.capture.inject_events)
    started = False
    try:
        start_text = board.command("inject " + name)
        started = True
        require("inject " + name + ":" in start_text,
                f"no se inicio {name}: {start_text!r}")
        effect_text = check_effect(board, name, start_text)
        board.pump(0.2)
        events = board.capture.inject_events[event_index:]
        require((scenario_id, expected_tasks) in events,
                f"falta EV_INJECT ({scenario_id},{expected_tasks}): {events!r}")
        stop_text = board.command("inject stop " + name)
        expected_stop = ("no esta activo" if name == "stack-overflow"
                         else "detenido y limpio")
        require(expected_stop in stop_text,
                f"stop no confirmo limpieza: {stop_text!r}")
        started = False
        status = board.command("inject status")
        require("sin escenarios activos" in status,
                f"registro activo tras stop: {status!r}")
        if name in ("deadlock", "prio-inv"):
            sync = board.command("sync")
            require("0 mutex" in sync and "0 tareas bloqueadas" in sync,
                    f"mutex residual: {sync!r}")
        free_text = board.command("free")
        free, blocks = heap_state(free_text)
        require((free, blocks) == (BASE_FREE, BASE_BLOCKS),
                f"heap residual tras {name}: {(free, blocks)}")
        return {"scenario": name, "ok": True,
                "event": [scenario_id, expected_tasks],
                "heap_free": free, "heap_blocks": blocks,
                "effect": effect_text.splitlines()[:8]}
    except Exception:
        if started:
            try:
                print(f"Intentando stop de emergencia: {name}", flush=True)
                print(board.command("inject stop " + name), flush=True)
            except Exception as cleanup_error:
                print(f"Stop de emergencia sin confirmacion: {cleanup_error}", flush=True)
        raise


def run_load_event(board: Board) -> dict:
    marker = len(board.capture.inject_events)
    started = False
    try:
        text = board.command("inject load nominal")
        started = True
        require("inject load nominal:" in text, f"load no inicio: {text!r}")
        board.pump(0.6)
        status = board.command("inject status")
        require("load: perfil=nominal" in status and "fallos_alloc=0" in status,
                f"load no progreso: {status!r}")
        cycles = re.search(r"ciclos=(\d+)", status)
        require(cycles is not None and int(cycles.group(1)) > 0,
                f"load no completo ciclos: {status!r}")
        require((6, 5) in board.capture.inject_events[marker:],
                "falta EV_INJECT de load nominal")
        stop = board.command("inject stop load")
        require("detenido y limpio" in stop, f"load stop fallo: {stop!r}")
        started = False
        sync = board.command("sync")
        require("0 mutex" in sync, f"mutex de load residual: {sync!r}")
        free, blocks = heap_state(board.command("free"))
        require((free, blocks) == (BASE_FREE, BASE_BLOCKS),
                f"heap residual de load: {(free, blocks)}")
        return {"scenario": "load nominal", "ok": True, "event": [6, 5],
                "heap_free": free, "heap_blocks": blocks}
    except Exception:
        if started:
            try:
                print(board.command("inject stop load"), flush=True)
            except Exception as cleanup_error:
                print(f"Stop de load sin confirmacion: {cleanup_error}", flush=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--repeats", type=int, default=10)
    parser.add_argument("--db", type=Path,
                        default=Path("build/transport/e09-auto.db"))
    parser.add_argument("--results", type=Path,
                        default=Path("build/transport/e09-auto.jsonl"))
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        parser.error("--repeats debe estar entre 1 y 10")
    args.db.parent.mkdir(parents=True, exist_ok=True)
    args.results.parent.mkdir(parents=True, exist_ok=True)
    board = Board(args.port, args.baud, args.db)
    try:
        with args.results.open("a", encoding="utf-8") as output:
            free, blocks = heap_state(board.command("free"))
            require((free, blocks) == (BASE_FREE, BASE_BLOCKS),
                    f"heap inicial inesperado: {(free, blocks)}")
            require("sin escenarios activos" in board.command("inject status"),
                    "hay escenarios activos antes de empezar")
            for name, scenario_id, tasks in SCENARIOS:
                for attempt in range(1, args.repeats + 1):
                    result = run_one(board, name, scenario_id, tasks)
                    result["attempt"] = attempt
                    output.write(json.dumps(result, ensure_ascii=False) + "\n")
                    output.flush()
                    print(f"OK {name} {attempt}/{args.repeats}: evento y heap limpio",
                          flush=True)
            result = run_load_event(board)
            output.write(json.dumps(result, ensure_ascii=False) + "\n")
            output.flush()
            print("OK load nominal: EV_INJECT y limpieza", flush=True)
            decoder = board.bridge.decoder
            print("Resumen transporte: "
                  f"tramas={decoder.frames_ok} crc={decoder.crc_errors} "
                  f"huecos={decoder.gaps} resyncs={decoder.resyncs} "
                  f"dropped={board.bridge.dropped_device}", flush=True)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    raise SystemExit(main())
