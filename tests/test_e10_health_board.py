"""Parseo del chequeo manual y del resumen de salud."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from e10_health_board import check_deadlock, check_health


class HealthOutputTests(unittest.TestCase):
    def test_deadlock_manual(self):
        check_deadlock("deadlock: sin ciclo; captura=770 ciclos\r\n", 0)
        check_deadlock("deadlock: ciclo=5 -> 6 -> 5; tareas=2 "
                       "captura=908 ciclos\r\n", 2)

    def test_health_categories(self):
        text = "\r\n".join((
            "health: deadlocks OK; ciclo=0 tareas",
            "health: fragmentacion OK; frag=0% libre=96676 B mayor=96676 B",
            "health: inanicion OK; espera_actual=0 ms umbral=500 ms",
            "health: inversion de prioridad OK; alertas_historicas=0",
            "health: jitter de switch OK; muestras=64",
            "health: eventos perdidos OK; eventos=0 tramas=0",
            "health: heap libre OK; libre=96676 B (98%)"))
        check_health(text, deadlock=False)
        check_health(text.replace("deadlocks OK; ciclo=0",
                                  "deadlocks CRIT; ciclo=2"), deadlock=True)


if __name__ == "__main__":
    unittest.main()
