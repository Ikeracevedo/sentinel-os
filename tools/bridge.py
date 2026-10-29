"""Puente de telemetria de Sentinel OS. Python >= 3.9; pyserial solo para --port.

El decodificador y el modo replay no necesitan hardware ni dependencias externas.
"""

from __future__ import annotations

import argparse
import _thread
import json
import os
import queue
import sqlite3
import struct
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Iterable

SYNC = b"\xaa\x55"
VERSION = 1
MAX_PAYLOAD = 512
FRAME_EVENTS = 0x01
FRAME_STATS = 0x02
FRAME_LOG = 0x03
FRAME_ALERT = 0x04
FRAME_HELLO = 0x05
FRAME_HEARTBEAT = 0x06
FRAME_TASKS = 0x07
FRAME_CMD = 0x81
FRAME_TRACE_CFG = 0x82
TRACE_ENABLE = 0x01
TRACE_FILTER = 0x02
TRACE_SAMPLE = 0x03
EVENT = struct.Struct("<IBBHII")
HEADER = struct.Struct("<BBHH")
STATS = struct.Struct("<QIIIIHHIIIIIIBBBB")
ALERT = struct.Struct("<BBBBIIIII48s")
TASK = struct.Struct("<BBBBIII16s")
TASK_STATES = ("UNUSED", "READY", "RUNNING", "BLOCKED", "SLEEPING",
               "SUSPENDED", "TERMINATED")


# La entrada y la salida de una consola interactiva ocurren en hilos
# diferentes. Un unico cerrojo evita que un prompt recibido de la ESP32 quede
# intercalado entre los caracteres que el usuario esta escribiendo.
_CONSOLE_LOCK = threading.RLock()


def console_write(text: str) -> None:
    with _CONSOLE_LOCK:
        sys.stdout.write(text)
        sys.stdout.flush()
ALERT_SEVERITIES = ("INFO", "WARN", "CRIT")
ALERT_TYPES = {1: "DEADLOCK", 2: "FRAGMENTACION", 3: "INANICION",
               4: "INVERSION", 5: "JITTER"}


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff, sin reflexion."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def encode_frame(kind: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload supera 512 bytes")
    body = HEADER.pack(VERSION, kind, seq & 0xFFFF, len(payload)) + payload
    return SYNC + body + struct.pack("<H", crc16(body))


@dataclass(frozen=True)
class Frame:
    kind: int
    seq: int
    payload: bytes
    raw: bytes


class Decoder:
    """Consume bloques arbitrarios y recupera tramas despues de ruido o CRC malo."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.frames_ok = 0
        self.crc_errors = 0
        self.gaps = 0
        self.resyncs = 0
        self.last_seq: int | None = None

    def feed(self, chunk: bytes) -> list[Frame]:
        self.buffer.extend(chunk)
        out: list[Frame] = []
        while True:
            pos = self.buffer.find(SYNC)
            if pos < 0:
                # Conservar AA: puede ser el inicio de un sync dividido en dos reads.
                keep = 1 if self.buffer.endswith(b"\xaa") else 0
                if len(self.buffer) > keep:
                    self.resyncs += 1
                    del self.buffer[:len(self.buffer) - keep]
                break
            if pos:
                self.resyncs += 1
                del self.buffer[:pos]
            if len(self.buffer) < 8:
                break
            version, kind, seq, length = HEADER.unpack_from(self.buffer, 2)
            if version != VERSION or length > MAX_PAYLOAD:
                self.resyncs += 1
                del self.buffer[0]
                continue
            total = 10 + length
            if len(self.buffer) < total:
                break
            body = bytes(self.buffer[2:8 + length])
            got = struct.unpack_from("<H", self.buffer, 8 + length)[0]
            if crc16(body) != got:
                self.crc_errors += 1
                self.resyncs += 1
                del self.buffer[0]
                continue
            raw = bytes(self.buffer[:total])
            del self.buffer[:total]
            if kind == FRAME_HELLO:
                # Un reinicio comienza otra sesion y reinicia SEQ.
                self.last_seq = None
            if self.last_seq is not None:
                delta = (seq - self.last_seq) & 0xFFFF
                if 1 < delta < 0x8000:
                    self.gaps += delta - 1
            self.last_seq = seq
            self.frames_ok += 1
            out.append(Frame(kind, seq, body[6:], raw))
        return out


class CycleClock:
    """Extiende CCOUNT de 32 a 64 bits; requiere eventos frecuentes (< un wrap)."""

    def __init__(self, hz: int = 40_000_000) -> None:
        self.hz = hz
        self.last: int | None = None
        self.wraps = 0

    def reset(self) -> None:
        self.last = None
        self.wraps = 0

    def microseconds(self, cycles: int) -> int:
        if self.last is not None and cycles < self.last and self.last - cycles > 0x80000000:
            self.wraps += 1
        self.last = cycles
        return ((self.wraps << 32) + cycles) * 1_000_000 // self.hz


class Sink:
    def write(self, kind: str, data: dict) -> None:
        raise NotImplementedError

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.flush()


class ConsoleSink(Sink):
    def write(self, kind: str, data: dict) -> None:
        if kind == "log":
            console_write(data["text"])
        elif kind in ("hello", "alert"):
            print(f"[{kind}] {data}", file=sys.stderr)


class FileSink(Sink):
    """Guarda todos los bytes recibidos, incluso ruido y tramas corruptas."""

    def __init__(self, path: str) -> None:
        self.file = open(path, "wb")

    def write(self, kind: str, data: dict) -> None:
        if kind == "raw":
            self.file.write(data["bytes"])

    def flush(self) -> None:
        self.file.flush()

    def close(self) -> None:
        self.file.close()


class SqliteSink(Sink):
    def __init__(self, path: str) -> None:
        self.db = sqlite3.connect(path)
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS sesiones (
                id INTEGER PRIMARY KEY, inicio TEXT DEFAULT CURRENT_TIMESTAMP,
                version TEXT, build TEXT, trace_enabled INTEGER, notas TEXT);
            CREATE TABLE IF NOT EXISTS eventos (
                id INTEGER PRIMARY KEY, sesion_id INTEGER REFERENCES sesiones(id),
                t_abs_us INTEGER, cycles INTEGER, tipo INTEGER, tid INTEGER,
                seq INTEGER, arg0 INTEGER, arg1 INTEGER);
            CREATE INDEX IF NOT EXISTS ix_eventos_tiempo ON eventos(sesion_id,t_abs_us);
            CREATE INDEX IF NOT EXISTS ix_eventos_tipo ON eventos(sesion_id,tipo);
            CREATE TABLE IF NOT EXISTS stats (
                id INTEGER PRIMARY KEY, sesion_id INTEGER REFERENCES sesiones(id),
                uptime_ticks INTEGER, heap_total INTEGER, heap_free INTEGER,
                heap_largest INTEGER, heap_peak INTEGER, events_emitted INTEGER,
                events_dropped INTEGER, switch_count INTEGER, switch_avg_cycles INTEGER,
                cpu_idle_pct INTEGER);
            CREATE TABLE IF NOT EXISTS alertas (
                id INTEGER PRIMARY KEY, sesion_id INTEGER REFERENCES sesiones(id),
                mensaje TEXT, seq INTEGER, tiempo_ms INTEGER, severidad INTEGER,
                tipo INTEGER, dato0 INTEGER, dato1 INTEGER, dato2 INTEGER);
            CREATE TABLE IF NOT EXISTS tareas (
                sesion_id INTEGER REFERENCES sesiones(id), tid INTEGER,
                nombre TEXT, PRIMARY KEY(sesion_id,tid));
        """)
        # Bases creadas antes de E10 conservan la tabla con solo mensaje.
        existing = {row[1] for row in self.db.execute("PRAGMA table_info(alertas)")}
        for column in ("seq", "tiempo_ms", "severidad", "tipo",
                       "dato0", "dato1", "dato2"):
            if column not in existing:
                self.db.execute(f"ALTER TABLE alertas ADD COLUMN {column} INTEGER")
        self.session: int | None = None
        self.pending: list[tuple] = []

    def _ensure_session(self) -> None:
        if self.session is None:
            # El puente puede conectarse despues de FRAME_HELLO. No perder los
            # eventos recuperados mientras espera el siguiente arranque.
            cur = self.db.execute("""INSERT INTO sesiones(version,build,notas)
                VALUES('1','desconocido','conexion a mitad de flujo')""")
            self.session = cur.lastrowid

    def write(self, kind: str, data: dict) -> None:
        if kind == "hello":
            self.flush()
            cur = self.db.execute(
                "INSERT INTO sesiones(version,build,trace_enabled) VALUES(?,?,?)",
                (str(data.get("version", 1)), data.get("build", ""), data.get("trace_enabled")))
            self.session = cur.lastrowid
            self.db.commit()
        elif kind == "event":
            self._ensure_session()
            self.pending.append((self.session, data["t_abs_us"], data["cycles"],
                                 data["type"], data["tid"], data["seq"],
                                 data["arg0"], data["arg1"]))
            if len(self.pending) >= 100:
                self.flush()
        elif kind == "stats":
            self._ensure_session()
            self.db.execute("""INSERT INTO stats(sesion_id,uptime_ticks,heap_total,
                heap_free,heap_largest,heap_peak,events_emitted,events_dropped,
                switch_count,switch_avg_cycles,cpu_idle_pct) VALUES(?,?,?,?,?,?,?,?,?,?,?)""",
                (self.session, data["uptime_ticks"], data["heap_total"],
                 data["heap_free"], data["heap_largest"], data["heap_peak"],
                 data["events_emitted"], data["events_dropped"],
                 data["switch_count"], data["switch_avg_cycles"],
                 data["cpu_idle_pct"]))
        elif kind == "alert":
            self._ensure_session()
            self.db.execute("""INSERT INTO alertas
                (sesion_id,mensaje,seq,tiempo_ms,severidad,tipo,dato0,dato1,dato2)
                VALUES(?,?,?,?,?,?,?,?,?)""",
                (self.session, data["text"], data.get("sequence"),
                 data.get("timestamp_ms"), data.get("severity"),
                 data.get("type"), data.get("data0"), data.get("data1"),
                 data.get("data2")))

    def flush(self) -> None:
        if self.pending:
            self.db.executemany("""INSERT INTO eventos
                (sesion_id,t_abs_us,cycles,tipo,tid,seq,arg0,arg1)
                VALUES(?,?,?,?,?,?,?,?)""", self.pending)
            self.pending.clear()
        self.db.commit()

    def close(self) -> None:
        self.flush()
        self.db.close()


class DashboardState(Sink):
    """Vista acotada y segura de la telemetría para el navegador local.

    El bridge sigue siendo el único consumidor del puerto serie. El servidor HTTP
    solo expone copias JSON: nunca recibe comandos ni controla la placa.
    """

    def __init__(self, window_us: int = 60_000_000) -> None:
        self.lock = threading.Lock()
        self.window_us = window_us
        self.tasks: dict[int, dict] = {}
        self.timeline: deque[dict] = deque()
        self.running_since: dict[int, int] = {}
        self.markers: deque[dict] = deque(maxlen=128)
        self.alerts: deque[dict] = deque(maxlen=16)
        self.stats: dict = {}
        self.last_heartbeat = 0.0
        self.last_event_us = 0
        self.started_at = time.monotonic()

    @staticmethod
    def _state_name(value: int) -> str:
        return TASK_STATES[value] if 0 <= value < len(TASK_STATES) else "?"

    def _trim_timeline(self) -> None:
        cutoff = self.last_event_us - self.window_us
        while self.timeline and self.timeline[0]["end_us"] < cutoff:
            self.timeline.popleft()
        while self.markers and self.markers[0]["at_us"] < cutoff:
            self.markers.popleft()

    def write(self, kind: str, data: dict) -> None:
        with self.lock:
            if kind == "hello":
                self.tasks.clear()
                self.timeline.clear()
                self.running_since.clear()
                self.markers.clear()
                self.alerts.clear()
                self.stats.clear()
                self.last_event_us = 0
                self.last_heartbeat = time.monotonic()
            elif kind == "tasks":
                self.tasks = {task["tid"]: task for task in data["tasks"]}
            elif kind == "stats":
                self.stats = dict(data)
            elif kind == "heartbeat":
                self.last_heartbeat = time.monotonic()
            elif kind == "alert":
                self.alerts.append(dict(data))
            elif kind != "event":
                return

            if kind != "event":
                return
            event_type = data["type"]
            tid = data["tid"]
            now_us = data["t_abs_us"]
            self.last_event_us = max(self.last_event_us, now_us)
            if event_type == 0x02:  # EV_SWITCH_IN
                self.running_since[tid] = now_us
            elif event_type == 0x01:  # EV_SWITCH_OUT
                start_us = self.running_since.pop(tid, None)
                if start_us is not None and now_us >= start_us:
                    self.timeline.append({"tid": tid, "start_us": start_us,
                                          "end_us": now_us,
                                          "reason": data["arg1"]})
            elif event_type == 0x30:  # EV_TASK_CREATE
                self.markers.append({"at_us": now_us, "kind": "run",
                                     "tid": tid, "label": f"TID {tid} creado"})
            elif event_type == 0x31:  # EV_TASK_EXIT
                self.markers.append({"at_us": now_us, "kind": "kill",
                                     "tid": tid, "label": f"TID {tid} terminado"})
                self.running_since.pop(tid, None)
            elif event_type == 0x61:  # EV_SLICE_CHANGE
                self.markers.append({"at_us": now_us, "kind": "slice", "tid": tid,
                                     "label": f"slice {data['arg0']} → {data['arg1']} ms",
                                     "quantum_ms": data["arg1"]})
            self._trim_timeline()

    def snapshot(self) -> dict:
        with self.lock:
            now_us = self.last_event_us
            connected = time.monotonic() - self.last_heartbeat < 2.5
            return {"connected": connected, "age_ms": int((time.monotonic() -
                    self.last_heartbeat) * 1000) if self.last_heartbeat else None,
                    "now_us": now_us, "window_us": self.window_us,
                    "tasks": sorted((dict(task) for task in self.tasks.values()),
                                    key=lambda task: task["tid"]),
                    "timeline": list(self.timeline), "markers": list(self.markers),
                    "stats": dict(self.stats), "alerts": list(self.alerts)}


class DashboardServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], state: DashboardState,
                 page: bytes) -> None:
        self.state = state
        self.page = page
        super().__init__(address, DashboardHandler)


class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - nombre impuesto por BaseHTTPRequestHandler
        if self.path == "/":
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(self.server.page)
            return
        if self.path == "/api/state":
            payload = json.dumps(self.server.state.snapshot(), separators=(",", ":")).encode()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)
            return
        self.send_error(HTTPStatus.NOT_FOUND, "Ruta no encontrada")

    def log_message(self, _format: str, *_args: object) -> None:
        """El polling del navegador no debe llenar la consola del bridge."""


def start_dashboard(state: DashboardState, host: str, port: int) -> DashboardServer:
    page = (Path(__file__).with_name("dashboard.html").read_bytes())
    server = DashboardServer((host, port), state, page)
    threading.Thread(target=server.serve_forever, daemon=True,
                     name="sentinel-dashboard").start()
    return server


def start_shell_input() -> queue.Queue[str]:
    """Lee comandos humanos sin bloquear el bucle que recibe la telemetría."""
    commands: queue.Queue[str] = queue.Queue()

    def windows_reader() -> None:
        """Dibuja la linea local sin el eco diferido de Windows.

        El modo de linea estandar deja a Windows hacer eco mientras la ESP32
        responde asincronamente. Eso podia unir ``help`` con un prompt remoto.
        Aqui el bridge controla caracteres y salto de linea, como un terminal.
        """
        import msvcrt

        line: list[str] = []
        while True:
            char = msvcrt.getwch()
            if char == "\x03":
                # getwch() consume Ctrl+C como caracter, a diferencia de
                # sys.stdin. Interrumpimos el hilo principal para que el
                # bloque finally cierre COM y SQLite ordenadamente.
                console_write("^C\r\n")
                _thread.interrupt_main()
                return
            if char in ("\r", "\n"):
                # La ESP32 emite CR/LF antes de su prompt. No dibujarlo aqui:
                # al llegar ambos juntos evitamos filas vacias y prompts
                # concatenados cuando el usuario pulsa Enter varias veces.
                commands.put("".join(line))
                line.clear()
            elif char in ("\x08", "\x7f"):
                if line:
                    line.pop()
                    console_write("\b \b")
            elif char in ("\x00", "\xe0"):
                # Consumir flechas y otras teclas extendidas: no son ordenes
                # ASCII validas para FRAME_CMD.
                msvcrt.getwch()
            elif " " <= char <= "~":
                line.append(char)
                console_write(char)

    def reader() -> None:
        if os.name == "nt" and sys.stdin.isatty():
            windows_reader()
            return
        for line in sys.stdin:
            # Conservar Enter vacio: el shell remoto debe comportarse como
            # una terminal, mostrando un nuevo prompt en vez de parecer
            # bloqueado. Solo quitamos el fin de linea del terminal host.
            commands.put(line.rstrip("\r\n"))

    threading.Thread(target=reader, daemon=True, name="sentinel-shell-input").start()
    return commands


def shell_command_payload(command: str) -> bytes:
    """Convierte una linea local en una orden valida para el shell remoto.

    FRAME_CMD no acepta payload vacio. Un espacio es semanticamente una linea
    vacia para el parser del shell, pero hace que este produzca el proximo
    prompt como lo haria Bash al pulsar Enter.
    """
    wire_command = command if command.strip() else " "
    try:
        payload = wire_command.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("el comando debe contener solo ASCII") from exc
    if not 1 <= len(payload) <= 127 or any(b < 0x20 or b > 0x7E for b in payload):
        raise ValueError("comando invalido (1..127 ASCII imprimibles)")
    return payload


class Bridge:
    def __init__(self, sinks: Iterable[Sink] = ()) -> None:
        self.decoder = Decoder()
        self.clock = CycleClock()
        self.sinks = list(sinks)
        self.dropped_device = 0
        self.events = 0
        self.hello_count = 0
        self.shell_ready = False
        self.log_tail = ""

    def publish(self, kind: str, data: dict) -> None:
        for sink in self.sinks:
            sink.write(kind, data)

    def shell_command_sent(self) -> None:
        """Marca el shell como ocupado hasta observar su siguiente prompt.

        El shell de la ESP32 ejecuta una linea cada vez.  Sin esta barrera un
        usuario podia escribir `help`, `ps` y `stats` rapidamente; el bridge
        transmitia las tres tramas seguidas y las salidas largas competian por
        la cola UART.  El prompt de vuelta es nuestro acuse de recibo natural.
        """
        self.shell_ready = False
        self.log_tail = ""

    def feed(self, chunk: bytes) -> None:
        self.publish("raw", {"bytes": chunk})
        for frame in self.decoder.feed(chunk):
            p = frame.payload
            if frame.kind == FRAME_HELLO:
                self.hello_count += 1
                self.shell_ready = False
                self.log_tail = ""
                self.clock.reset()
                self.dropped_device = 0
                # Payload v1: trace_enabled (1 B), build ASCII restante.
                self.publish("hello", {"version": 1,
                                       "trace_enabled": p[0] if p else None,
                                       "build": p[1:].decode("ascii", "replace") if p else ""})
            elif frame.kind == FRAME_EVENTS:
                if len(p) == 0 or len(p) % EVENT.size:
                    continue
                for values in EVENT.iter_unpack(p):
                    cycles, kind, tid, seq, arg0, arg1 = values
                    self.events += 1
                    if kind == 0x40:
                        # El latido de E07 cuenta solo descartes del ring de
                        # traza. FRAME_STATS tambien incluye los del emisor.
                        self.dropped_device = max(self.dropped_device, arg1)
                    self.publish("event", {"cycles": cycles, "type": kind,
                                           "tid": tid, "seq": seq, "arg0": arg0,
                                           "arg1": arg1,
                                           "t_abs_us": self.clock.microseconds(cycles)})
            elif frame.kind == FRAME_STATS and len(p) == STATS.size:
                values = STATS.unpack(p)
                keys = ("uptime_ticks", "heap_total", "heap_free", "heap_largest",
                        "heap_peak", "heap_blocks", "heap_free_blocks",
                        "events_emitted", "events_dropped", "switch_count",
                        "switch_min_cycles", "switch_max_cycles", "switch_avg_cycles",
                        "task_count", "trace_enabled", "cpu_idle_pct", "reserved")
                data = dict(zip(keys, values))
                self.dropped_device = max(self.dropped_device,
                                          data["events_dropped"])
                self.publish("stats", data)
            elif frame.kind == FRAME_TASKS and len(p) == 16 * TASK.size:
                tasks = []
                for values in TASK.iter_unpack(p):
                    tid, state, priority, _reserved, quantum, cpu_low, heap, raw_name = values
                    if tid == 0xFF:
                        continue
                    name = raw_name.split(b"\x00", 1)[0].decode("ascii", "replace")
                    tasks.append({"tid": tid, "name": name or f"tarea-{tid}",
                                  "state": TASK_STATES[state] if state < len(TASK_STATES) else "?",
                                  "priority": priority, "quantum_ms": quantum,
                                  "cpu_cycles_low": cpu_low, "heap_bytes": heap})
                self.publish("tasks", {"tasks": tasks})
            elif frame.kind == FRAME_ALERT and len(p) == ALERT.size and p[0] == 1:
                version, severity, alert_type, _reserved, sequence, timestamp_ms, \
                    data0, data1, data2, message = ALERT.unpack(p)
                label = (ALERT_SEVERITIES[severity]
                         if severity < len(ALERT_SEVERITIES) else "?")
                kind_name = ALERT_TYPES.get(alert_type, "?")
                message_text = message.split(b"\x00", 1)[0].decode("ascii", "replace")
                self.publish("alert", {"text": f"#{sequence} t={timestamp_ms} ms "
                             f"{label} {kind_name}: {message_text} "
                             f"datos={data0},{data1},{data2}",
                             "sequence": sequence, "timestamp_ms": timestamp_ms,
                             "severity": severity, "type": alert_type,
                             "data0": data0, "data1": data1, "data2": data2})
            elif frame.kind in (FRAME_LOG, FRAME_ALERT):
                text = p.decode("ascii", "replace")
                if frame.kind == FRAME_LOG:
                    self.log_tail = (self.log_tail + text)[-128:]
                    prompt = self.log_tail.rfind("sentinel:")
                    if prompt >= 0 and "$ " in self.log_tail[prompt:]:
                        self.shell_ready = True
                self.publish("log" if frame.kind == FRAME_LOG else "alert",
                             {"text": text})
            elif frame.kind == FRAME_HEARTBEAT:
                self.publish("heartbeat", {})

    def report(self) -> str:
        d = self.decoder
        return (f"tramas_ok={d.frames_ok} crc_errors={d.crc_errors} "
                f"huecos={d.gaps} resyncs={d.resyncs} "
                f"dropped_device={self.dropped_device} eventos={self.events}")

    def close(self) -> None:
        for sink in self.sinks:
            sink.close()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Puerto del ESP32: COM3 o /dev/ttyUSB0")
    source.add_argument("--replay", help="Archivo binario grabado")
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--sink", choices=("console", "sqlite"), action="append")
    ap.add_argument("--db", default="sentinel.db")
    ap.add_argument("--record", help="Grabar bytes crudos")
    ap.add_argument("--serve", type=int, metavar="PUERTO",
                    help="Publicar el dashboard local en http://127.0.0.1:PUERTO")
    action = ap.add_mutually_exclusive_group()
    action.add_argument("--cmd", help="Enviar comando al shell remoto")
    action.add_argument("--trace-enable", choices=("on", "off"),
                        help="Encender o apagar la traza en caliente")
    action.add_argument("--trace-filter", nargs=2, metavar=("TIPO", "on|off"),
                        help="Activar o desactivar un tipo de evento (ej. 0x01 on)")
    action.add_argument("--trace-sample", type=int, metavar="N",
                        help="Muestreo 1 de N para cambios de tarea (1..65535)")
    ap.add_argument("--shell", action="store_true",
                    help="Reenviar al SO los comandos escritos en esta terminal")
    ap.add_argument("--reset", action="store_true",
                    help="Reiniciar ESP32 al abrir el puerto para capturar HELLO")
    ap.add_argument("--stats", action="store_true", help="Mostrar contadores cada 5 s")
    ap.add_argument("--duration", type=float,
                    help="Terminar la captura del puerto tras N segundos")
    args = ap.parse_args(argv)
    has_action = (args.cmd is not None or args.trace_enable is not None or
                  args.trace_filter is not None or args.trace_sample is not None)
    if has_action and not args.port:
        ap.error("los comandos descendentes requieren --port")
    if args.reset and not args.port:
        ap.error("--reset requiere --port")
    if args.duration is not None and (not args.port or args.duration <= 0):
        ap.error("--duration requiere --port y un numero positivo de segundos")
    if args.serve is not None and not 1 <= args.serve <= 65535:
        ap.error("--serve requiere un puerto entre 1 y 65535")
    if args.shell and not args.port:
        ap.error("--shell requiere --port")
    outbound = None
    if args.cmd is not None:
        try:
            command = args.cmd.encode("ascii")
        except UnicodeEncodeError:
            ap.error("el comando debe contener solo ASCII")
        if not 1 <= len(command) <= 127 or any(b < 0x20 or b > 0x7E for b in command):
            ap.error("el comando debe tener 1..127 caracteres ASCII imprimibles")
        outbound = encode_frame(FRAME_CMD, 0, command)
    elif args.trace_enable is not None:
        payload = bytes((TRACE_ENABLE, int(args.trace_enable == "on")))
        outbound = encode_frame(FRAME_TRACE_CFG, 0, payload)
    elif args.trace_filter is not None:
        try:
            event_type = int(args.trace_filter[0], 0)
        except ValueError:
            ap.error("TIPO debe ser un entero (ej. 1 o 0x01)")
        state = args.trace_filter[1]
        if not 0 <= event_type <= 255 or state not in ("on", "off"):
            ap.error("TIPO debe estar entre 0 y 255 y el estado debe ser on/off")
        payload = bytes((TRACE_FILTER, event_type, int(state == "on")))
        outbound = encode_frame(FRAME_TRACE_CFG, 0, payload)
    elif args.trace_sample is not None:
        if not 1 <= args.trace_sample <= 65535:
            ap.error("--trace-sample requiere N entre 1 y 65535")
        payload = bytes((TRACE_SAMPLE, args.trace_sample & 0xFF,
                         args.trace_sample >> 8))
        outbound = encode_frame(FRAME_TRACE_CFG, 0, payload)
    # Una sesion interactiva debe mostrar las respuestas del SO.  SQLite es
    # almacenamiento, no una consola; por eso --shell conserva ConsoleSink
    # aunque el usuario tambien solicite --sink sqlite.
    sink_choices = list(args.sink or ["console"])
    if args.shell and "console" not in sink_choices:
        sink_choices.insert(0, "console")
    sinks: list[Sink] = []
    for choice in sink_choices:
        sinks.append(SqliteSink(args.db) if choice == "sqlite" else ConsoleSink())
    if args.record:
        sinks.append(FileSink(args.record))
    dashboard = None
    dashboard_server = None
    if args.serve is not None:
        dashboard = DashboardState()
        sinks.append(dashboard)
    bridge = Bridge(sinks)
    stream = None
    outbound_at = None
    shell_commands = start_shell_input() if args.shell else None
    outbound_seq = 1
    try:
        if dashboard is not None:
            dashboard_server = start_dashboard(dashboard, "127.0.0.1", args.serve)
            print(f"dashboard: http://127.0.0.1:{args.serve}", file=sys.stderr)
        if args.port:
            try:
                import serial
            except ImportError as exc:
                raise SystemExit("Falta pyserial: python3 -m pip install pyserial") from exc
            # Configurar DTR/RTS antes de abrir COM: una conexion para --cmd
            # no debe reiniciar accidentalmente el ESP32.
            # La consola interactiva debe reaccionar al Enter humano sin la
            # pausa perceptible del timeout anterior de 500 ms. Los frames
            # siguen llegando por bloques; 10 ms conserva CPU para el
            # dashboard y hace que Enter se sienta inmediato.
            stream = serial.Serial(port=None, baudrate=args.baud, timeout=0.01)
            stream.dtr = False
            stream.rts = False
            stream.port = args.port
            stream.open()
            if args.reset:
                stream.setDTR(False)
                stream.setRTS(True)
                time.sleep(0.1)
                stream.setRTS(False)
            if outbound is not None:
                # Abrir el puerto puede reiniciar la placa incluso sin --reset.
                # Esperar el prompt evita enviar durante el arranque. Si una
                # trama LOG se pierde, el plazo da una ruta de recuperación.
                outbound_at = time.monotonic() + 2.0
        else:
            stream = Path(args.replay).open("rb")
        last_report = time.monotonic()
        deadline = last_report + args.duration if args.duration is not None else None
        while True:
            chunk = stream.read(4096)
            if chunk:
                bridge.feed(chunk)
            elif args.replay:
                break
            now = time.monotonic()
            if outbound is not None and (bridge.shell_ready or
                                         (outbound_at is not None and now >= outbound_at)):
                stream.write(outbound)
                outbound = None
            if shell_commands is not None and bridge.shell_ready:
                try:
                    command = shell_commands.get_nowait()
                except queue.Empty:
                    command = None
                if command is not None:
                    try:
                        payload = shell_command_payload(command)
                    except ValueError as exc:
                        print(f"bridge: {exc}", file=sys.stderr)
                        continue
                    stream.write(encode_frame(FRAME_CMD, outbound_seq, payload))
                    outbound_seq = (outbound_seq + 1) & 0xFFFF
                    bridge.shell_command_sent()
            if deadline is not None and now >= deadline:
                break
            if args.stats and now - last_report >= 5:
                print(bridge.report(), file=sys.stderr)
                last_report = now
    except KeyboardInterrupt:
        pass
    finally:
        if stream is not None:
            stream.close()
        bridge.close()
        if dashboard_server is not None:
            dashboard_server.shutdown()
            dashboard_server.server_close()
    print(bridge.report(), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
