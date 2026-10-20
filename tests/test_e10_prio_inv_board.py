"""Parseo de la alerta y las metricas de inversion de prioridad."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_prio_inv_board import inversion_alert, inversion_state


class InversionOutputTests(unittest.TestCase):
    def test_alert(self):
        text = ("[ALERTA] INVERSION: alta=alta(tid 5,p1) "
                "baja=baja(tid 4,p7) media=media(tid 6,p4) "
                "mutex=8 duracion=300 ms bloqueo=410 ms\r\n")
        self.assertEqual(inversion_alert(text), (5, 1, 4, 7, 6, 4, 8, 300, 410))

    def test_stats(self):
        text = ("detector: chequeos=42 inversion_chequeos=210 "
                "inversion_alertas=1 inversion_max=300 ms\r\n")
        self.assertEqual(inversion_state(text), (210, 1, 300))


if __name__ == "__main__":
    unittest.main()
