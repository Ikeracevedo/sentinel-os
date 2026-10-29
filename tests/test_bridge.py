"""Pruebas puramente locales del protocolo; no necesitan placa ni pyserial."""

import sqlite3
import struct
import sys
import tempfile
import unittest
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from bridge import (ALERT, Bridge, CycleClock, DashboardState, Decoder, EVENT, FRAME_ALERT,
                    FRAME_EVENTS, FRAME_HELLO, FRAME_LOG, FRAME_STATS, FRAME_TASKS, STATS,
                    TASK, SqliteSink, crc16, encode_frame, shell_command_payload,
                    start_dashboard)


class ProtocolTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(crc16(b"123456789"), 0x29B1)

    def test_split_sync_noise_crc_and_wrap(self):
        decoder = Decoder()
        good = encode_frame(FRAME_LOG, 65535, b"ok")
        bad = bytearray(encode_frame(FRAME_LOG, 0, b"bad"))
        bad[-1] ^= 1
        stream = b"ruido\xaa" + good + bytes(bad) + encode_frame(FRAME_LOG, 0, b"next")
        frames = []
        for byte in stream:
            frames.extend(decoder.feed(bytes([byte])))
        self.assertEqual([f.payload for f in frames], [b"ok", b"next"])
        self.assertEqual(decoder.crc_errors, 1)
        self.assertEqual(decoder.gaps, 0)
        self.assertGreater(decoder.resyncs, 0)

    def test_recover_after_corrupted_length(self):
        decoder = Decoder()
        invalid = b"\xaa\x55\x01\x03\x00\x00\xff\xff"
        frames = decoder.feed(invalid + encode_frame(FRAME_LOG, 8, b"resync"))
        self.assertEqual([f.payload for f in frames], [b"resync"])

    def test_gap_and_hello_reset(self):
        decoder = Decoder()
        decoder.feed(encode_frame(FRAME_LOG, 10))
        decoder.feed(encode_frame(FRAME_LOG, 13))
        self.assertEqual(decoder.gaps, 2)
        decoder.feed(encode_frame(FRAME_HELLO, 0, b"\x01build"))
        decoder.feed(encode_frame(FRAME_LOG, 1))
        self.assertEqual(decoder.gaps, 2)

    def test_cycle_wrap_at_40_mhz(self):
        clock = CycleClock()
        first = clock.microseconds(0xFFFFFF00)
        second = clock.microseconds(0x00000100)
        self.assertEqual(second - first, 512 * 1_000_000 // 40_000_000)

    def test_sqlite_batch_and_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = str(Path(tmp) / "sentinel.db")
            sink = SqliteSink(db)
            bridge = Bridge([sink])
            bridge.feed(encode_frame(FRAME_HELLO, 0, b"\x01build1"))
            events = b"".join(EVENT.pack(i, 0x40, 0, i, i, 0) for i in range(16))
            bridge.feed(encode_frame(FRAME_EVENTS, 1, events))
            bridge.close()
            conn = sqlite3.connect(db)
            try:
                self.assertEqual(conn.execute("SELECT count(*) FROM sesiones").fetchone()[0], 1)
                self.assertEqual(conn.execute("SELECT count(*) FROM eventos").fetchone()[0], 16)
            finally:
                conn.close()

    def test_sqlite_midstream_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = str(Path(tmp) / "sentinel.db")
            bridge = Bridge([SqliteSink(db)])
            event = EVENT.pack(12, 0x40, 0, 7, 100, 0)
            bridge.feed(encode_frame(FRAME_EVENTS, 90, event))
            bridge.close()
            conn = sqlite3.connect(db)
            try:
                self.assertEqual(conn.execute("SELECT COUNT(*) FROM sesiones").fetchone()[0], 1)
                self.assertEqual(conn.execute("SELECT COUNT(*) FROM eventos").fetchone()[0], 1)
                self.assertEqual(conn.execute("SELECT notas FROM sesiones").fetchone()[0],
                                 "conexion a mitad de flujo")
            finally:
                conn.close()

    def test_structured_alert_is_saved_in_sqlite(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = str(Path(tmp) / "sentinel.db")
            bridge = Bridge([SqliteSink(db)])
            payload = ALERT.pack(1, 2, 1, 0, 7, 1234, 4, 2, 9,
                                 b"Ciclo de espera\x00".ljust(48, b"\x00"))
            bridge.feed(encode_frame(FRAME_ALERT, 1, payload))
            bridge.close()
            conn = sqlite3.connect(db)
            try:
                row = conn.execute("""SELECT mensaje,seq,tiempo_ms,severidad,tipo,
                    dato0,dato1,dato2 FROM alertas""").fetchone()
                self.assertEqual(row[1:], (7, 1234, 2, 1, 4, 2, 9))
                self.assertIn("CRIT DEADLOCK: Ciclo de espera", row[0])
            finally:
                conn.close()

    def test_old_alert_table_is_migrated(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = str(Path(tmp) / "sentinel.db")
            conn = sqlite3.connect(db)
            conn.execute("CREATE TABLE alertas (id INTEGER PRIMARY KEY, "
                         "sesion_id INTEGER, mensaje TEXT)")
            conn.close()
            bridge = Bridge([SqliteSink(db)])
            bridge.feed(encode_frame(FRAME_ALERT, 1, b"alerta antigua"))
            bridge.close()
            conn = sqlite3.connect(db)
            try:
                self.assertEqual(conn.execute("SELECT mensaje FROM alertas")
                                 .fetchone()[0], "alerta antigua")
                self.assertIn("tiempo_ms", {row[1] for row in
                              conn.execute("PRAGMA table_info(alertas)")})
            finally:
                conn.close()

    def test_device_drop_count_does_not_go_backwards_after_stats(self):
        bridge = Bridge()
        fields = [0] * len(STATS.unpack(bytes(STATS.size)))
        fields[8] = 3264  # events_dropped: ring + emisor
        bridge.feed(encode_frame(FRAME_STATS, 1, STATS.pack(*fields)))
        bridge.feed(encode_frame(FRAME_EVENTS, 2,
                                 EVENT.pack(1, 0x40, 0, 1, 100, 2716)))
        self.assertEqual(bridge.dropped_device, 3264)
        bridge.feed(encode_frame(FRAME_HELLO, 0, b"\x01nuevo"))
        self.assertEqual(bridge.dropped_device, 0)

    def test_shell_prompt_split_across_log_frames(self):
        bridge = Bridge()
        bridge.feed(encode_frame(FRAME_HELLO, 0, b"\x01build"))
        bridge.feed(encode_frame(FRAME_LOG, 1, b"sentinel:/home/"))
        self.assertFalse(bridge.shell_ready)
        bridge.feed(encode_frame(FRAME_LOG, 2, b"iker$ "))
        self.assertTrue(bridge.shell_ready)

    def test_shell_command_waits_for_next_prompt(self):
        bridge = Bridge()
        bridge.feed(encode_frame(FRAME_LOG, 1, b"sentinel:/home/iker$ "))
        self.assertTrue(bridge.shell_ready)
        bridge.shell_command_sent()
        self.assertFalse(bridge.shell_ready)
        bridge.feed(encode_frame(FRAME_LOG, 2, b"salida parcial\r\n"))
        self.assertFalse(bridge.shell_ready)
        bridge.feed(encode_frame(FRAME_LOG, 3, b"sentinel:/home/iker$ "))
        self.assertTrue(bridge.shell_ready)
        bridge.feed(encode_frame(FRAME_HELLO, 0, b"\x01nuevo"))
        self.assertFalse(bridge.shell_ready)

    def test_empty_terminal_line_has_a_valid_shell_payload(self):
        self.assertEqual(shell_command_payload(""), b" ")
        self.assertEqual(shell_command_payload("   \t"), b" ")
        self.assertEqual(shell_command_payload("ps -v"), b"ps -v")

    def test_dashboard_state_reconstructs_task_and_cpu_interval(self):
        state = DashboardState()
        bridge = Bridge([state])
        task_rows = bytearray(16 * TASK.size)
        for index in range(16):
            TASK.pack_into(task_rows, index * TASK.size,
                           0xFF, 0, 0, 0, 0, 0, 0, b"\0" * 16)
        TASK.pack_into(task_rows, 0, 4, 2, 5, 0, 200, 123, 256,
                       b"counter\0".ljust(16, b"\0"))
        bridge.feed(encode_frame(FRAME_TASKS, 1, bytes(task_rows)))
        bridge.feed(encode_frame(FRAME_EVENTS, 2,
                                 EVENT.pack(4000, 0x02, 4, 1, 0, 0)))
        bridge.feed(encode_frame(FRAME_EVENTS, 3,
                                 EVENT.pack(8000, 0x01, 4, 2, 5, 1)))
        bridge.feed(encode_frame(FRAME_EVENTS, 4,
                                 EVENT.pack(9000, 0x61, 1, 3, 10, 200)))
        snapshot = state.snapshot()
        self.assertEqual(snapshot["tasks"][0]["name"], "counter")
        self.assertEqual(snapshot["tasks"][0]["quantum_ms"], 200)
        self.assertEqual(snapshot["timeline"][0]["tid"], 4)
        self.assertEqual(snapshot["timeline"][0]["end_us"] -
                         snapshot["timeline"][0]["start_us"], 100)
        self.assertEqual(snapshot["markers"][0]["label"], "slice 10 → 200 ms")

    def test_dashboard_serves_page_and_json_without_external_dependencies(self):
        state = DashboardState()
        server = start_dashboard(state, "127.0.0.1", 0)
        base = f"http://127.0.0.1:{server.server_address[1]}"
        try:
            with urllib.request.urlopen(base + "/", timeout=2) as response:
                self.assertIn(b"Sentinel OS", response.read())
            with urllib.request.urlopen(base + "/api/state", timeout=2) as response:
                payload = response.read().decode("utf-8")
                self.assertIn('"timeline":[]', payload)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
