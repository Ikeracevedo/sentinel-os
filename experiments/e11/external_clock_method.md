# E11 · validación cruzada sin instrumento GPIO

Iker confirmó que no dispone de analizador lógico ni osciloscopio. Por ello
se aplica el método alternativo de HU‑E11‑05: contar `N` cambios de contexto
y medir el tiempo total de la ventana con un cronómetro **externo a la
ESP32**, `time.perf_counter_ns()` de Windows. El escenario `switchbench`
genera dos tareas del mismo firmware y el puente conserva cada UART; las
duraciones individuales se reconstruyen de `CCOUNT` a 40 MHz. Se harán
cinco ventanas de igual duración. Los errores y valores atípicos se
conservarán.

En cada corrida se calculan:

- `N`: parejas `SWITCH_OUT`/`SWITCH_IN` completas del UART.
- `T_W/N`: tiempo transcurrido en Windows dividido por `N`.
- `Σ CCOUNT_switch/(40 MHz · N)`: duración media observada de los cambios.
- `Σ CCOUNT_switch/(40 MHz · T_W)`: fracción de la ventana ocupada por el
  camino observado de cambio.

`T_W/N` es **una cota superior**, no otra medida aislada del switch: incluye
la ejecución de las tareas, sus llamadas a `yield`, intervalos de sueño,
interrupciones y tiempo de comunicación. El reloj de Windows tampoco está
sincronizado ciclo a ciclo con la ESP32. La diferencia entre ambas cifras
**no mide el error de CCOUNT** y este método no puede comprobar la meta de
discrepancia GPIO–CCOUNT <5 %. No se montó un GPIO porque sin instrumento
su pulso no podría medirse. La captura de pantalla del instrumento no existe;
se conserva la salida del cronómetro y el UART como evidencia alternativa.

## Resultados observados

La batería formal `20261021T055311Z` produjo cinco ventanas de 10 s,
**12 007 switches emparejados** (2 422, 2 425, 2 313, 2 422 y 2 425), sin
errores CRC, huecos, pérdidas de secuencia de eventos ni descartes declarados
por la placa. CCOUNT dio **24,087 ± 0,007 µs/switch** y la cota de Windows
**4 266,3 ± 32,6 µs/switch**; `±` es la desviación estándar entre las cinco
corridas. Las ventanas de Windows fueron de 10,002–10,323 s porque incluyen
la recepción y el despacho de bytes en el PC. El cociente entre ambas cifras
no representa discrepancia de relojes ni error de la sonda. La fracción de
tiempo ocupada por el camino de cambio observado fue 0,557–0,568 % de cada
ventana, calculada desde CCOUNT.

El piloto fallido `20261021T054100Z` también se conserva: un error de
columnas del CSV detuvo la captura antes de iniciar `switchbench`. Después de
corregirlo, el piloto `20261021T054837Z` pasó con 1 211 cambios en 5,166 s.
Las cinco corridas formales se hicieron con la revisión `20c474a`; la columna
`git_dirty=true` registra que había archivos de la tabla maestra y auditoría
todavía sin versionar. El código de la medición ya estaba fijado en ese commit.
