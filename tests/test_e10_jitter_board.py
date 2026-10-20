"""Parseo del umbral y la alerta de jitter."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_jitter_board import jitter_alert, jitter_state


class JitterOutputTests(unittest.TestCase):
    def test_alert(self):
        text = ("[ALERTA] JITTER: desviacion=11 us umbral=1 us "
                "media=21 us min=12 us max=49 us muestras=64\r\n")
        self.assertEqual(jitter_alert(text), (11, 1, 21, 12, 49, 64))

    def test_stats(self):
        text = "jitter_chequeos=12 jitter_alertas=1 jitter_umbral=50 us\r\n"
        self.assertEqual(jitter_state(text), (12, 1, 50))

    def test_previous_firmware_is_identified(self):
        text = ("detector: inversion_chequeos=10 inversion_alertas=0\r\n"
                "jitter: muestras=64 min=12 us max=88 us media=24 us "
                "desviacion=13 us\r\n")
        with self.assertRaisesRegex(RuntimeError, "firmware anterior"):
            jitter_state(text)


if __name__ == "__main__":
    unittest.main()
