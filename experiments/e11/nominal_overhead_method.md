# E11 · comparación nominal TRACE=0 y TRACE=1

El banco experimental llama a `cmd_inject(3, {"inject", "load", "nominal"})`
desde un coordinador en placa. Ejecuta las mismas cinco tareas, prioridad 5,
período 100 ms, reserva/liberación de 64 B, mutex de 1 ms y CPU de 1 ms que
el comando del shell. Un segundo comando `inject stop load` limpia las tareas.
El coordinador añade una tarea idéntica en ambas compilaciones.

Se compilan dos imágenes desde el mismo commit, con `TRANSPORT=0` y UART del
shell a 57600 bit/s. Las dos incluyen la misma sonda `TRACE_OVERHEAD_BENCH`
para observar cambios de contexto. Solo cambia `TRACE=0/1`; en TRACE=1 el
coordinador consume los eventos del anillo cada 1 ms para evitar saturarlo.
Ese consumo también forma parte del costo de observación medido. La sonda
compartida añade perturbación a ambas variantes. El experimento no mide el
costo del transporte de telemetría por UART, que se evalúa por separado.

Cada imagen formal produce cinco ventanas de 60 s. `overhead.py` guarda los
bytes UART y líneas crudas antes de validar las filas. Los pilotos tienen una
ventana de 10 s y archivo de resultados separado; no se mezclan con las diez
corridas formales. Se conservan todas las capturas, incluso fallos y atípicos.
`analyze_overhead.py --profile nominal` compara CPU ocupada, tiempo de
switch, iteraciones/s, RAM estática e imagen binaria. Media y desviación son
entre corridas; los tamaños de imagen son una medición por variante. El
desglose previo `overhead-standard` tiene una carga diferente y no se combina
con este resultado.

Piloto TRACE=1 de 10 s (`20261021T032051Z`): 502 iteraciones, 47 111
eventos emitidos, cero descartes, cero fallos de reserva y limpieza confirmada
por UART. Calculado en PC: 10,256 % CPU ocupada y 27,588 µs por switch
completo observado. Es una sola corrida; no estima incertidumbre ni overhead.

Piloto TRACE=0 de 10 s (`20261021T032359Z`): 502 iteraciones, cero eventos
de traza, cero descartes y cero fallos de reserva, con limpieza confirmada.
Calculado en PC: 8,978 % CPU ocupada y 22,641 µs por switch completo
observado. La diferencia piloto ON-OFF es +1,278 puntos porcentuales de CPU
y +4,947 µs; aún falta la variación entre cinco corridas de cada variante.

## Batería formal en placa

Fuente del firmware de ambas variantes: commit `5568566d70fba286728590232fa81d39d7b556ed`.
Sesiones UTC: `20261021T032742Z` (OFF) y `20261021T034018Z` (ON).
La segunda sesión marca `git_dirty=true` porque los crudos OFF estaban sin
versionar; no había cambios de código entre las dos compilaciones. Cada
variante completó cinco ventanas de 60 s, con 3000 a 3002 iteraciones por
ventana, cero fallos de reserva y cero eventos descartados reportados. Cada
UART contiene el arranque, el `inject stop load` y `TRACE_NOMINAL_DONE PASS`.
En cada variante faltó una de las 60 copias redundantes de una fila de texto;
las cinco filas distintas quedaron completas y sin contradicciones. Las copias
perdidas no se cuentan como eventos de traza descartados por la placa.

| Magnitud | TRACE=0 | TRACE=1 | ON−OFF |
|---|---:|---:|---:|
| CPU ocupada, % | 8,978 ± 0,009 | 10,263 ± 0,001 | +1,285 ± 0,009 puntos |
| Switch completo observado, µs | 22,6419 ± 0,0002 | 27,5885 ± 0,0002 | +4,9466 ± 0,0003 |
| σ de duraciones individuales de switch, µs | 10,282 ± 0,003 | 10,490 ± 0,003 | — |
| Iteraciones/s | 50,007 ± 0,015 | 50,007 ± 0,015 | 0,000 ± 0,021 |
| Eventos emitidos/s | 0 | 4710,02 ± 0,09 | — |
| RAM estática, B | 179 212 | 212 532 | +33 320 |
| Imagen `.bin`, B | 56 128 | 59 424 | +3 296 |

CPU, tiempo y tasa son cálculos en PC a partir de contadores medidos por la
placa. El primer ± de esas filas es la desviación de las cinco corridas; la
incertidumbre de ON−OFF combina ambas desviaciones. La σ de switch es la
dispersión de duraciones individuales dentro de cada corrida; el ± junto a
ella es la variación de esa σ entre corridas. Los tamaños estáticos tienen
una imagen por variante y por ello no llevan incertidumbre estadística.
No se descartó ninguna corrida ni valor atípico. La segunda corrida OFF
tuvo menos CPU ocupada que las otras cuatro y permanece en la media.

La diferencia de CPU dividida entre los eventos ON equivale a
**109,111 ± 0,849 ciclos por evento**, emparejando corridas por número. Es un
costo integral del sistema: incluye sondas, escritura al anillo y consumidor;
no es el tiempo de una sola llamada a `trace_emit`. La medición aislada de E07
fue de 48 ciclos por evento en otra prueba y una sola ejecución.

La meta S2 de menos de 5 puntos porcentuales de CPU se cumple para esta carga
nominal. La meta S1 de switch menor de 10 µs no se cumple: las dos medias
superan 10 µs. El switch observado incluye la sonda compartida y excluye las
últimas instrucciones del retorno de interrupción. `TRANSPORT=0` en ambas
variantes evita mezclar aquí el costo de transmitir telemetría por UART.

Datos: `nominal_overhead_runs.csv`, dos CSV de líneas crudas y dos `.uart`.
Resultados calculados: `nominal_overhead_summary.csv` y
`nominal_overhead_comparison.png`. Este experimento aporta datos a E11; la
redacción del informe corresponde a E12.
