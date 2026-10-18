"""Pruebas puramente locales del protocolo; no necesitan placa ni pyserial."""

import sqlite3
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from bridge import (Bridge, CycleClock, Decoder, EVENT, FRAME_EVENTS,
                    FRAME_HELLO, FRAME_LOG, FRAME_STATS, STATS, SqliteSink,
                    crc16, encode_frame)


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


if __name__ == "__main__":
    unittest.main()
