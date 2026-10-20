"""Las salidas con CRLF conservan tarea, prioridad y espera de inanicion."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_hog_board import starvation_alert, starvation_state


class StarvationOutputTests(unittest.TestCase):
    def test_alert(self):
        text = ("[ALERTA] INANICION: tracer(tid 2) prioridad=6 "
                "espera=1001 ms umbral=500 ms\r\n")
        self.assertEqual(starvation_alert(text), ("tracer", 2, 6, 1001, 500))

    def test_stats(self):
        text = ("detector: chequeos=20 alertas=0 maximo=2100 ciclos "
                "inanicion_chequeos=20 inanicion_alertas=1 "
                "espera_max=1001 ms umbral=500 ms\r\n")
        self.assertEqual(starvation_state(text), (20, 1, 1001, 500))


if __name__ == "__main__":
    unittest.main()
