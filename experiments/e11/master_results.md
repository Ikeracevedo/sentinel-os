# E11 · tabla maestra de resultados

Generada por `tools/testbench/master_results.py` desde los CSV de análisis. Las fuentes conservan todos los crudos y valores atípicos. `σ` representa variación entre corridas salvo nota contraria.

| Métrica | Condición | Meta | Medido | Incertidumbre | Repeticiones | Resultado | Fuente | Limitación |
|---|---|---|---|---|---:|---|---|---|
| Switch observado: media | nominal, TRACE ON | < 10 µs | 22.355 µs | ±0.223 µs entre corridas | 5 | NO CUMPLE | switch_summary_20261021T012850Z.csv | Incluye la sonda de medición; meta S1 fallida. |
| Switch observado: p99 | nominal, TRACE ON | sin meta | 42.425 µs | ±2.264 µs entre p99 por corrida | 5 | MEDIDO | switch_summary_20261021T012850Z.csv | Percentil pooled, incluidos valores atípicos. |
| Switch observado: mediana | nominal, TRACE ON | sin meta | 15.925 µs | ±0.000 µs entre corridas | 5 | MEDIDO | switch_summary_20261021T012850Z.csv | Percentil pooled; atípicos conservados. |
| Switch observado: p95 | nominal, TRACE ON | sin meta | 37.875 µs | ±0.079 µs entre corridas | 5 | MEDIDO | switch_summary_20261021T012850Z.csv | Percentil pooled; atípicos conservados. |
| Jitter observado: σ individual | nominal, TRACE ON | sin meta | 11.299 µs | ±0.113 µs entre σ de corridas | 5 | MEDIDO | switch_summary_20261021T012850Z.csv | σ pooled de 57 940 cambios; incluye valores atípicos. |
| Switch observado: extremos | nominal, TRACE ON | sin meta | 11.975–99.35 µs | σ individual 11.299 µs | 57 940 muestras | MEDIDO | switch_summary_20261021T012850Z.csv |  |
| Costo de TRACE en CPU | nominal, ON−OFF | < 5 puntos % | +1.285 puntos % | ±0.009 puntos % | 5+5 | CUMPLE | nominal_overhead_summary.csv |  |
| Costo integral por evento | nominal, ON−OFF | sin meta | 109.111 ciclos/evento | ±0.849 ciclos/evento | 5 pares | MEDIDO | nominal_overhead_summary.csv | Incluye el consumo y drenaje de la traza. |
| Meta de sonda aislada E07 | nominal, ON−OFF | < 50 ciclos/evento aislado | 109.111 ciclos/evento integral | ±0.849 ciclos/evento | 5 pares | NO COMPARABLE | nominal_overhead_summary.csv | El costo integral de E11 incluye drenaje; la sonda aislada E07 midió 48 ciclos. |
| RAM estática de TRACE | nominal, ON−OFF | sin meta | +33320 B | aritmética exacta de dos imágenes; variación entre builds no estimada | 1+1 | MEDIDO | nominal_overhead_summary.csv |  |
| Tamaño binario de TRACE | nominal, ON−OFF | sin meta | +3296 B | aritmética exacta de dos imágenes; variación entre builds no estimada | 1+1 | MEDIDO | nominal_overhead_summary.csv |  |
| Cambio de throughput | nominal, ON−OFF | sin meta | +0.000 ops/s | ±0.021 ops/s | 5+5 | MEDIDO | nominal_overhead_summary.csv |  |
| Demora del detector | 20 deadlocks de 2 tareas | < 1 s | 320.862 ms; máximo 544.301 ms | ±158.026 ms entre inyecciones | 20 | CUMPLE | detector_summary_20261021T042124Z.csv |  |
| Tasa de detección | 20 deadlocks de 2 tareas | 100 % | 100 % (20/20) | intervalo binomial 95 %: 83,2–100 % | 20 | CUMPLE | detector_summary_20261021T042124Z.csv |  |
| Costo del detector | última ejecución consultada | < 100 µs | 51.144 µs | ±0.229 µs | 20 | CUMPLE | detector_summary_20261021T042124Z.csv | No identifica el chequeo exacto que emitió cada alerta. |
| Telemetría emitida/descartada | nominal | < 1 % descartado | 49314/0 eventos | ±42.8 emitidos/10 s entre corridas; 0 descartados observados | 5 | CUMPLE | telemetry_summary_20261021T002832Z.csv |  |
| Ocupación UART | nominal | sin meta | 20.53 % de 921600 bit/s | ±0.20 puntos % | 5 | MEDIDO | telemetry_summary_20261021T002832Z.csv | Cota inferior si Windows perdió bytes. |
| Telemetría emitida/descartada | heavy | sin meta | 108810/0 eventos | ±224.1 emitidos/10 s entre corridas; 0 descartados observados | 5 | MEDIDO | telemetry_summary_20261021T002832Z.csv | No hubo saturación de placa en heavy. |
| Ocupación UART | heavy | sin meta | 44.09 % de 921600 bit/s | ±0.49 puntos % | 5 | MEDIDO | telemetry_summary_20261021T002832Z.csv | Cota inferior si Windows perdió bytes. |
| Heap libre mínimo | fuga controlada | sin meta | 85084 B | ±250 B | 5 | MEDIDO | memory_summary_20261021T011525Z.csv |  |
| Fragmentación máxima | fragmentación controlada | sin meta | 92.398 % | ±0.000 puntos % | 5 | MEDIDO | memory_summary_20261021T011525Z.csv |  |
| Falsas alertas | nominal, una hora continua | 0 | 0 en 3608.9 s | conteo exacto en la hora; extrapolación no estimada | 1 hora | CUMPLE | false_positives_summary_20261021T043250Z.csv |  |
| Cronómetro externo por switch | switchbench | alternativa sin instrumento | cota Windows 4266.3 µs/switch; CCOUNT 24.087 µs/switch | σ entre corridas: 32.6 y 0.007 µs | 5 | LIMITADO | external_clock_runs.csv | Windows incluye carga, sleeps y comunicación; no prueba <5 %. |
| Discrepancia GPIO–CCOUNT | sin instrumento | < 5 % | no medida | no estimable | 0 | NO COMPROBABLE | external_clock_method.md | No hubo analizador ni osciloscopio; la alternativa no aísla el switch. |
