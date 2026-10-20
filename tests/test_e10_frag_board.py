"""El arnes reconoce las estadisticas de fragmentacion del shell."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_frag_board import fragmentation_state


class FragmentationOutputTests(unittest.TestCase):
    def test_detector_metrics(self):
        text = ("detector: chequeos=31 alertas=0 ultimo=900 ciclos (22 us) "
                "maximo=1800 ciclos (45 us) grafo_max=900 ciclos "
                "dfs_max=300 ciclos frag_chequeos=15 frag_alertas=1 "
                "frag=96% libre=47000 mayor=3072\r\n")
        self.assertEqual(fragmentation_state(text), (15, 1, 96, 47000, 3072))


if __name__ == "__main__":
    unittest.main()
