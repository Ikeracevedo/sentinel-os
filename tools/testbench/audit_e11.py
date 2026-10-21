#!/usr/bin/env python3
"""Audita evidencia E11 completa y confirma que los crudos están en Git.

La meta S1 puede estar medida y fallida: se informa, nunca se convierte en
PASS. Las seis historias se juzgan por evidencia de sus criterios.
"""

from __future__ import annotations

import csv
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "experiments" / "e11"


def rows(name: str) -> list[dict]:
    with (EVIDENCE / name).open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def require(condition: bool, detail: str) -> None:
    if not condition:
        raise RuntimeError(detail)


def files(pattern: str, count: int) -> list[Path]:
    matched = sorted(EVIDENCE.glob(pattern))
    require(len(matched) == count, f"{pattern}: {len(matched)} archivos; se esperan {count}")
    require(all(path.stat().st_size > 0 for path in matched),
            f"{pattern}: hay archivos vacíos")
    return matched


def main() -> int:
    evidence: list[Path] = []
    checks: dict[str, str] = {}

    # HU01: perfiles automatizados, cinco ventanas y crudos de cada una.
    for profile in ("light", "nominal", "heavy"):
        evidence += files(f"stats_20261021T002832Z_trace-on_{profile}_r*.csv", 5)
        evidence += files(f"stats_20261021T002832Z_trace-on_{profile}_r*.uart", 5)
    evidence.append(EVIDENCE / "resultados.csv")
    for profile in ("leak", "frag"):
        evidence += files(f"memory_20261021T011525Z_trace-on_{profile}_r*.csv", 5)
        evidence += files(f"memory_20261021T011525Z_trace-on_{profile}_r*.uart", 5)
    evidence.append(EVIDENCE / "memory_summary_20261021T011525Z.csv")
    checks["HU-E11-01"] = "baterías de perfiles y memoria con cinco corridas y crudos"

    # HU02: 57.940 duraciones, resumen y motivos complementarios.
    switch = [row for row in rows("switch_summary_20261021T012850Z.csv")
              if row["run"] == "pooled" and row["reason"] == "all"]
    require(len(switch) == 1 and int(switch[0]["samples"]) >= 50_000,
            "faltan al menos 50.000 switches nominales")
    evidence += files("stats_20261021T012850Z_trace-on_nominal_r*.uart", 5)
    evidence += files("stats_20261021T012850Z_trace-on_nominal_r*.csv", 5)
    evidence += [EVIDENCE / name for name in
                 ("switch_samples_20261021T012850Z.csv",
                  "switch_summary_20261021T012850Z.csv",
                  "switch_hist_20261021T012850Z.png",
                  "switchbench_summary_20261021T030915Z.csv")]
    checks["HU-E11-02"] = "57.940 muestras y motivos; meta S1 medida e incumplida"

    # HU03: cinco corridas por variante y comparación nominal.
    overhead = rows("nominal_overhead_runs.csv")
    for variant in ("trace-on", "trace-off"):
        selected = [row for row in overhead if row["variant"] == variant and
                    row["profile"] == "nominal"]
        require(len(selected) == 5 and {int(row["run"]) for row in selected} ==
                set(range(1, 6)), f"cinco corridas {variant} ausentes")
    evidence += [EVIDENCE / name for name in
                 ("nominal_overhead_runs.csv", "nominal_overhead_summary.csv",
                  "nominal_overhead_comparison.png",
                  "nominal_overhead_20261021T032742Z_trace-off.csv",
                  "nominal_overhead_20261021T032742Z_trace-off.uart",
                  "nominal_overhead_20261021T034018Z_trace-on.csv",
                  "nominal_overhead_20261021T034018Z_trace-on.uart")]
    checks["HU-E11-03"] = "TRACE ON/OFF nominal 5+5, CPU, switch, RAM y binario"

    # HU04: inyecciones, hora nominal y telemetría.
    detector = [row for row in rows("detector_runs.csv")
                if row["session"] == "20261021T042124Z"]
    require(len(detector) == 20 and all(row["status"] == "PASS" for row in detector),
            "20 detecciones completas ausentes")
    evidence += files("detector_20261021T042124Z_r*.uart", 20)
    evidence += [EVIDENCE / name for name in
                 ("detector_runs.csv", "detector_summary_20261021T042124Z.csv",
                  "telemetry_summary_20261021T002832Z.csv")]
    hour = files("false_positives_summary_*.csv", 1)[0]
    session = hour.stem.removeprefix("false_positives_summary_")
    hour_rows = rows(hour.name)
    require(len(hour_rows) == 1 and hour_rows[0]["status"] == "PASS" and
            float(hour_rows[0]["duration_s"]) >= 3600 and
            int(hour_rows[0]["false_alerts"]) == 0,
            "hora nominal sin falsos positivos ausente")
    evidence += [hour, EVIDENCE / "false_positives_runs.csv",
                 EVIDENCE / f"false_positives_{session}.uart"]
    checks["HU-E11-04"] = "20/20 detecciones, una hora sin falsas alertas, telemetría"

    # HU05: cinco mediciones del cronómetro de Windows, limitación explícita.
    wall = rows("external_clock_runs.csv")
    latest = max(row["session"] for row in wall)
    selected = [row for row in wall if row["session"] == latest]
    require(len(selected) == 5 and all(row["status"] in ("PASS", "LINK_ERRORS")
                                       for row in selected),
            "cinco ventanas de cronómetro externo ausentes")
    evidence += files(f"external_clock_{latest}_r*.uart", 5)
    evidence += files(f"external_clock_{latest}_r*.csv", 5)
    evidence += [EVIDENCE / "external_clock_runs.csv",
                 EVIDENCE / "external_clock_method.md"]
    checks["HU-E11-05"] = "alternativa de Windows 5x; sin afirmación de 5 %"

    # HU06: tabla, cuatro gráficas pedidas y métodos.
    master = rows("master_results.csv")
    require(master and not any(row["result"] == "PENDIENTE" for row in master),
            "tabla maestra incompleta")
    evidence += [EVIDENCE / "master_results.csv", EVIDENCE / "master_results.md"]
    for name in ("memory_leak_20261021T011525Z.png",
                 "memory_frag_20261021T011525Z.png"):
        evidence.append(EVIDENCE / name)
    checks["HU-E11-06"] = "tabla con incertidumbre y gráficas; S1 falla declarado"

    evidence = sorted(set(evidence))
    missing = [path for path in evidence if not path.is_file() or path.stat().st_size == 0]
    require(not missing, f"evidencia ausente/vacía: {missing[:5]}")
    tracked = set(subprocess.check_output(["git", "ls-files", "-z", "--",
                                            "experiments/e11"], cwd=ROOT).decode().split("\0"))
    untracked = [path.relative_to(ROOT).as_posix() for path in evidence
                 if path.relative_to(ROOT).as_posix() not in tracked]
    require(not untracked, f"evidencia no versionada: {untracked[:10]}")
    report = {"checks": checks, "evidence_files": len(evidence),
              "failed_goal": "S1: switch nominal observado >=10 us",
              "gpio_limitation": "sin analizador ni osciloscopio; cronómetro externo es cota"}
    destination = EVIDENCE / "audit_e11.json"
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                           encoding="utf-8")
    print(f"PASS: seis historias evaluadas; {len(evidence)} archivos versionados; "
          f"S1 no cumple; informe={destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
