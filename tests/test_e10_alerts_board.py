"""Parseo de la lista de alertas del shell."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_alerts_board import require_alert_list


class AlertListTests(unittest.TestCase):
    def test_count(self):
        require_alert_list("alerts\r\nalerts: 1 recientes\r\n#1 t=500 ms CRIT "
                           "DEADLOCK: Ciclo de espera datos=4,2,9\r\n", 1)

    def test_bad_count(self):
        with self.assertRaisesRegex(RuntimeError, "registro de alertas"):
            require_alert_list("alerts: 0 recientes\r\n", 1)


if __name__ == "__main__":
    unittest.main()
