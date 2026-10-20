"""Verifica que el arnes interprete la salida real del shell con CRLF."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_graph_board import check_graph, detector_state


class GraphBoardOutputTests(unittest.TestCase):
    def test_no_cycle(self):
        text = ("grafo de espera (tid -> tid):\r\n"
                "  (sin aristas)\r\n"
                "sin ciclos\r\n"
                "aristas=0 captura=717 ciclos\r\n")
        self.assertEqual(check_graph(text), 717)

    def test_ordered_three_task_cycle(self):
        text = ("grafo de espera (tid -> tid):\r\n"
                "  4 -> 5\r\n  5 -> 6\r\n  6 -> 4\r\n"
                "ciclo: 4 -> 5 -> 6 -> 4\r\n"
                "aristas=3 captura=920 ciclos\r\n")
        self.assertEqual(check_graph(text, expect_cycle=3), 920)

    def test_detector_stats(self):
        text = ("detector\r\ndetector: chequeos=8 alertas=1 "
                "ultimo=950 ciclos (23 us) maximo=1700 ciclos (42 us)\r\n")
        self.assertEqual(detector_state(text), (8, 1, 1700))


if __name__ == "__main__":
    unittest.main()
