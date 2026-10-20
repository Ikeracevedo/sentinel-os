"""El arnes debe interpretar el estado real con CRLF y esperas transitorias."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_nominal_board import load_status, no_cycle, pathology_alerts, jitter_metrics


class NominalBoardOutputTests(unittest.TestCase):
    def test_nominal_status(self):
        text = ("inject status\r\ninject: load activo; tareas: 5 6 7 8 9\r\n"
                "load: perfil=nominal tiempo=30012 ms ciclos=1500 "
                "tasa=49.9/s fallos_alloc=0\r\nsentinel:/home/iker$ ")
        self.assertEqual(load_status(text), (30012, 1500, 0))

    def test_wait_edge_without_cycle(self):
        text = ("grafo de espera (tid -> tid):\r\n  5 -> 6\r\n"
                "sin ciclos\r\naristas=1 captura=980 ciclos\r\n")
        no_cycle(text)

    def test_pathology_counters(self):
        text = "frag_alertas=0 inanicion_chequeos=60 inanicion_alertas=0\r\n"
        self.assertEqual(pathology_alerts(text), (0, 0))

    def test_jitter_metrics(self):
        text = "jitter: muestras=64 min=18 us max=31 us media=22 us desviacion=3 us\r\n"
        self.assertEqual(jitter_metrics(text), (64, 18, 31, 22, 3))


if __name__ == "__main__":
    unittest.main()
