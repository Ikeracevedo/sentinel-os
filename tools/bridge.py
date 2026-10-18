"""Puente de telemetria de Sentinel OS. Python >= 3.9; pyserial solo para --port.

El decodificador y el modo replay no necesitan hardware ni dependencias externas.
"""

from __future__ import annotations

import argparse
import sqlite3
import struct
import sys
import time
from dataclasses import dataclass
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
FRAME_CMD = 0x81
FRAME_TRACE_CFG = 0x82
EVENT = struct.Struct("<IBBHII")
HEADER = struct.Struct("<BBHH")
STATS = struct.Struct("<QIIIIHHIIIIIIBBBB")


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
            print(data["text"], end="", flush=True)
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
                mensaje TEXT);
            CREATE TABLE IF NOT EXISTS tareas (
                sesion_id INTEGER REFERENCES sesiones(id), tid INTEGER,
                nombre TEXT, PRIMARY KEY(sesion_id,tid));
        """)
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
            self.db.execute("INSERT INTO alertas(sesion_id,mensaje) VALUES(?,?)",
                            (self.session, data["text"]))

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


class Bridge:
    def __init__(self, sinks: Iterable[Sink] = ()) -> None:
        self.decoder = Decoder()
        self.clock = CycleClock()
        self.sinks = list(sinks)
        self.dropped_device = 0
        self.events = 0

    def publish(self, kind: str, data: dict) -> None:
        for sink in self.sinks:
            sink.write(kind, data)

    def feed(self, chunk: bytes) -> None:
        self.publish("raw", {"bytes": chunk})
        for frame in self.decoder.feed(chunk):
            p = frame.payload
            if frame.kind == FRAME_HELLO:
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
            elif frame.kind in (FRAME_LOG, FRAME_ALERT):
                self.publish("log" if frame.kind == FRAME_LOG else "alert",
                             {"text": p.decode("ascii", "replace")})
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
    ap.add_argument("--cmd", help="Enviar comando al shell remoto")
    ap.add_argument("--reset", action="store_true",
                    help="Reiniciar ESP32 al abrir el puerto para capturar HELLO")
    ap.add_argument("--stats", action="store_true", help="Mostrar contadores cada 5 s")
    ap.add_argument("--duration", type=float,
                    help="Terminar la captura del puerto tras N segundos")
    args = ap.parse_args(argv)
    if args.cmd and not args.port:
        ap.error("--cmd requiere --port")
    if args.reset and not args.port:
        ap.error("--reset requiere --port")
    if args.duration is not None and (not args.port or args.duration <= 0):
        ap.error("--duration requiere --port y un numero positivo de segundos")
    sinks: list[Sink] = []
    for choice in args.sink or ["console"]:
        sinks.append(SqliteSink(args.db) if choice == "sqlite" else ConsoleSink())
    if args.record:
        sinks.append(FileSink(args.record))
    bridge = Bridge(sinks)
    stream = None
    try:
        if args.port:
            try:
                import serial
            except ImportError as exc:
                raise SystemExit("Falta pyserial: python3 -m pip install pyserial") from exc
            stream = serial.Serial(args.port, args.baud, timeout=0.5)
            if args.reset:
                stream.setDTR(False)
                stream.setRTS(True)
                time.sleep(0.1)
                stream.setRTS(False)
            if args.cmd:
                command = args.cmd.encode("ascii")
                if not command or len(command) >= 128:
                    ap.error("el comando debe ser ASCII y tener 1..127 bytes")
                stream.write(encode_frame(FRAME_CMD, 0, command))
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
    print(bridge.report(), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
