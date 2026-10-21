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

Resultados: pendientes de la batería en placa.
