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
