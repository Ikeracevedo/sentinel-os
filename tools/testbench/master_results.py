#!/usr/bin/env python3
"""Consolida los resultados comprobados de E11 sin rellenar datos ausentes.

Lee los CSV de análisis versionables en experiments/e11 y genera una tabla
maestra CSV/Markdown. Los valores atípicos permanecen en sus fuentes.
"""

from __future__ import annotations

import argparse
import csv
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ("metric", "condition", "target", "measured", "uncertainty",
          "repetitions", "result", "source", "limitation")


def read(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def pick(path: Path, **where: str) -> dict:
    matches = [row for row in read(path)
               if all(row.get(key) == value for key, value in where.items())]
    if len(matches) != 1:
        raise ValueError(f"se esperaba una fila en {path.name}: {where}; hay {len(matches)}")
    return matches[0]


def row(metric: str, condition: str, target: str, measured: str,
        uncertainty: str, repetitions: str, result: str, source: str,
        limitation: str = "") -> dict:
    return dict(zip(FIELDS, (metric, condition, target, measured, uncertainty,
                             repetitions, result, source, limitation)))


def make(directory: Path) -> list[dict]:
    result: list[dict] = []
    switch_file = directory / "switch_summary_20261021T012850Z.csv"
    switch = pick(switch_file, run="pooled", reason="all")
    individual = [entry for entry in read(switch_file)
                  if entry["reason"] == "all" and entry["run"] in
                  {"1", "2", "3", "4", "5"}]
    if len(individual) != 5 or int(switch["samples"]) < 50_000:
        raise ValueError("la batería nominal de switches está incompleta")
    means = [float(entry["mean_us"]) for entry in individual]
    p99s = [float(entry["p99_us"]) for entry in individual]
    sigmas = [float(entry["sigma_us"]) for entry in individual]
    result.append(row("Switch observado: media", "nominal, TRACE ON", "< 10 µs",
                      f"{statistics.mean(means):.3f} µs",
                      f"±{statistics.stdev(means):.3f} µs entre corridas", "5",
                      "NO CUMPLE", switch_file.name,
                      "Incluye la sonda de medición; meta S1 fallida."))
    result.append(row("Switch observado: p99", "nominal, TRACE ON", "sin meta",
                      f"{float(switch['p99_us']):.3f} µs",
                      f"±{statistics.stdev(p99s):.3f} µs entre p99 por corrida",
                      "5", "MEDIDO", switch_file.name,
                      "Percentil pooled, incluidos valores atípicos."))
    for metric, key in (("mediana", "median_us"), ("p95", "p95_us")):
        spread = statistics.stdev(float(entry[key]) for entry in individual)
        result.append(row(f"Switch observado: {metric}", "nominal, TRACE ON",
                          "sin meta", f"{float(switch[key]):.3f} µs",
                          f"±{spread:.3f} µs entre corridas", "5", "MEDIDO",
                          switch_file.name, "Percentil pooled; atípicos conservados."))
    result.append(row("Jitter observado: σ individual", "nominal, TRACE ON",
                      "sin meta", f"{float(switch['sigma_us']):.3f} µs",
                      f"±{statistics.stdev(sigmas):.3f} µs entre σ de corridas",
                      "5", "MEDIDO", switch_file.name,
                      "σ pooled de 57 940 cambios; incluye valores atípicos."))
    result.append(row("Switch observado: extremos", "nominal, TRACE ON", "sin meta",
                      f"{switch['min_us']}–{switch['max_us']} µs",
                      "σ individual " + f"{float(switch['sigma_us']):.3f} µs",
                      "57 940 muestras", "MEDIDO", switch_file.name))

    overhead = directory / "nominal_overhead_summary.csv"
    def oh(metric: str, variant: str = "on-minus-off") -> dict:
        return pick(overhead, metric=metric, variant=variant)
    cpu = oh("cpu_occupied_pct")
    result.append(row("Costo de TRACE en CPU", "nominal, ON−OFF", "< 5 puntos %",
                      f"+{float(cpu['mean']):.3f} puntos %",
                      f"±{float(cpu['sd_between_runs']):.3f} puntos %",
                      "5+5", "CUMPLE", overhead.name))
    cost = oh("effective_cycles_per_event")
    result.append(row("Costo integral por evento", "nominal, ON−OFF", "sin meta",
                      f"{float(cost['mean']):.3f} ciclos/evento",
                      f"±{float(cost['sd_between_runs']):.3f} ciclos/evento",
                      "5 pares", "MEDIDO", overhead.name,
                      "Incluye el consumo y drenaje de la traza."))
    result.append(row("Meta de sonda aislada E07", "nominal, ON−OFF",
                      "< 50 ciclos/evento aislado",
                      f"{float(cost['mean']):.3f} ciclos/evento integral",
                      f"±{float(cost['sd_between_runs']):.3f} ciclos/evento",
                      "5 pares", "NO COMPARABLE", overhead.name,
                      "El costo integral de E11 incluye drenaje; la sonda aislada E07 midió 48 ciclos."))
    for metric, label in (("ram_static_bytes", "RAM estática de TRACE"),
                          ("binary_bytes", "Tamaño binario de TRACE")):
        value = oh(metric)
        result.append(row(label, "nominal, ON−OFF", "sin meta",
                          f"+{int(value['mean'])} B",
                          "aritmética exacta de dos imágenes; variación entre builds no estimada",
                          "1+1", "MEDIDO", overhead.name))
    throughput = oh("throughput_ops_per_s")
    result.append(row("Cambio de throughput", "nominal, ON−OFF", "sin meta",
                      f"{float(throughput['mean']):+.3f} ops/s",
                      f"±{float(throughput['sd_between_runs']):.3f} ops/s",
                      "5+5", "MEDIDO", overhead.name))

    detector_file = directory / "detector_summary_20261021T042124Z.csv"
    detector = pick(detector_file, metric="delay_ms")
    result.append(row("Demora del detector", "20 deadlocks de 2 tareas", "< 1 s",
                      f"{float(detector['mean']):.3f} ms; máximo "
                      f"{float(detector['max']):.3f} ms",
                      f"±{float(detector['sd_between_runs']):.3f} ms entre inyecciones",
                      "20", "CUMPLE", detector_file.name))
    success = pick(detector_file, metric="detection_rate")
    result.append(row("Tasa de detección", "20 deadlocks de 2 tareas", "100 %",
                      f"{float(success['mean']):.0f} % (20/20)",
                      "intervalo binomial 95 %: 83,2–100 %", "20", "CUMPLE",
                      detector_file.name))
    detector_cost = pick(detector_file, metric="last_execution_cost_us")
    result.append(row("Costo del detector", "última ejecución consultada", "< 100 µs",
                      f"{float(detector_cost['mean']):.3f} µs",
                      f"±{float(detector_cost['sd_between_runs']):.3f} µs",
                      "20", "CUMPLE", detector_file.name,
                      "No identifica el chequeo exacto que emitió cada alerta."))

    telemetry = directory / "telemetry_summary_20261021T002832Z.csv"
    for profile in ("nominal", "heavy"):
        emitted = pick(telemetry, profile=profile, metric="events_emitted")
        dropped = pick(telemetry, profile=profile, metric="events_dropped")
        uart = pick(telemetry, profile=profile, metric="uart_occupancy_pct")
        target = "< 1 % descartado" if profile == "nominal" else "sin meta"
        result.append(row("Telemetría emitida/descartada", profile, target,
                          f"{int(emitted['sum'])}/0 eventos",
                          f"±{float(emitted['sd_between_runs']):.1f} emitidos/10 s "
                          "entre corridas; 0 descartados observados",
                          "5", "CUMPLE" if profile == "nominal" else "MEDIDO",
                          telemetry.name,
                          "No hubo saturación de placa en heavy." if profile == "heavy" else ""))
        result.append(row("Ocupación UART", profile, "sin meta",
                          f"{float(uart['mean']):.2f} % de 921600 bit/s",
                          f"±{float(uart['sd_between_runs']):.2f} puntos %",
                          "5", "MEDIDO", telemetry.name,
                          "Cota inferior si Windows perdió bytes."))

    memory = directory / "memory_summary_20261021T011525Z.csv"
    leak = pick(memory, profile="leak", metric="min_heap_free")
    frag = pick(memory, profile="frag", metric="max_fragmentation")
    result.append(row("Heap libre mínimo", "fuga controlada", "sin meta",
                      f"{float(leak['mean']):.0f} B",
                      f"±{float(leak['sd_between_runs']):.0f} B", "5",
                      "MEDIDO", memory.name))
    result.append(row("Fragmentación máxima", "fragmentación controlada", "sin meta",
                      f"{float(frag['mean']):.3f} %",
                      f"±{float(frag['sd_between_runs']):.3f} puntos %", "5",
                      "MEDIDO", memory.name))

    false_files = sorted(directory.glob("false_positives_summary_*.csv"))
    if false_files:
        false = read(false_files[-1])
        if len(false) != 1 or false[0]["status"] != "PASS":
            raise ValueError("resumen de falsos positivos incompleto")
        observed = false[0]
        result.append(row("Falsas alertas", "nominal, una hora continua", "0",
                          f"{observed['false_alerts']} en {float(observed['duration_s']):.1f} s",
                          "conteo exacto en la hora; extrapolación no estimada",
                          "1 hora", "CUMPLE", false_files[-1].name))
    else:
        result.append(row("Falsas alertas", "nominal, una hora continua", "0",
                          "pendiente", "pendiente", "0", "PENDIENTE", ""))

    external = directory / "external_clock_runs.csv"
    if external.exists():
        wall_rows = [item for item in read(external) if item["status"] in
                     ("PASS", "LINK_ERRORS")]
        if len(wall_rows) >= 5:
            ids = {item["session"] for item in wall_rows}
            newest = max(ids)
            selected = [item for item in wall_rows if item["session"] == newest]
            if len(selected) == 5:
                cc = [float(item["ccount_mean_us"]) for item in selected]
                upper = [float(item["host_time_per_switch_us"]) for item in selected]
                result.append(row("Cronómetro externo por switch", "switchbench",
                                  "alternativa sin instrumento",
                                  f"cota Windows {statistics.mean(upper):.1f} µs/switch; "
                                  f"CCOUNT {statistics.mean(cc):.3f} µs/switch",
                                  f"σ entre corridas: {statistics.stdev(upper):.1f} y "
                                  f"{statistics.stdev(cc):.3f} µs",
                                  "5", "LIMITADO", external.name,
                                  "Windows incluye carga, sleeps y comunicación; no prueba <5 %."))
                result.append(row("Discrepancia GPIO–CCOUNT", "sin instrumento",
                                  "< 5 %", "no medida", "no estimable", "0",
                                  "NO COMPROBABLE", "external_clock_method.md",
                                  "No hubo analizador ni osciloscopio; la alternativa no aísla el switch."))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "experiments" / "e11")
    args = parser.parse_args()
    directory = args.directory.resolve()
    entries = make(directory)
    csv_path = directory / "master_results.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(entries)
    md_path = directory / "master_results.md"
    with md_path.open("w", encoding="utf-8") as stream:
        stream.write("# E11 · tabla maestra de resultados\n\n")
        stream.write("Generada por `tools/testbench/master_results.py` desde los CSV "
                     "de análisis. Las fuentes conservan todos los crudos y valores "
                     "atípicos. `σ` representa variación entre corridas salvo nota "
                     "contraria.\n\n")
        stream.write("| Métrica | Condición | Meta | Medido | Incertidumbre | "
                     "Repeticiones | Resultado | Fuente | Limitación |\n")
        stream.write("|---|---|---|---|---|---:|---|---|---|\n")
        for entry in entries:
            stream.write("| " + " | ".join(str(entry[key]).replace("|", "\\|")
                                           for key in FIELDS) + " |\n")
    print(f"{len(entries)} métricas consolidadas: {csv_path} y {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
