# E07: costo de la instrumentacion en la placa

> Medicion preliminar del codigo anterior a la sonda de switch en
> `vectors.S`. Las cifras de abajo no describen aun el codigo actual.
> Hay que repetir las dos compilaciones y las diez corridas antes de usar
> este resultado para cerrar HU-E07-08 y CP5.

Fecha: 2026-10-17. Placa: ESP32-D0WD-V3. CPU bare metal: 40 MHz.
Compilador: xtensa-esp32-elf-gcc 13.2.0, `-O2 -mno-longcalls`.

## Metodo

Se compilaron dos imagenes con el mismo instrumento `TRACE_OVERHEAD_BENCH=1`:
una con `TRACE=0` y otra con `TRACE=1`. En cada imagen se ejecutaron cinco
corridas de 60 s. La carga tiene cinco tareas propias: un coordinador y cuatro
trabajadores. Cada trabajador repite `kmalloc`/`kfree`, toma y libera un mutex,
y duerme; uno tambien cede voluntariamente. El coordinador vacia el ring
buffer en la variante ON para que el experimento incluya el costo del consumo
y pueda comprobar la perdida de eventos. En OFF, la misma llamada al drenaje
queda vacia al compilar. No se escribe por UART durante las ventanas medidas.

`CCOUNT` es el contador de ciclos de 32 bits. A 40 MHz vuelve a cero cada
~107,4 s, por lo que la resta modular de cada ventana de 60 s es valida.
El porcentaje de CPU ociosa se calcula con los ciclos cargados a la tarea idle
divididos por los ciclos transcurridos. El overhead se estima como la resta
entre el porcentaje idle de OFF y el de ON, expresada en puntos porcentuales
de capacidad de una CPU. Esta cifra describe esta carga concreta, no todas las
cargas posibles.

La latencia de switch se mide con `CCOUNT` al entrar y salir de `switch_task`
en C, usando el mismo instrumento en ambas imagenes. Incluye el costo de las
sondas de traza en ON. Todavia no incluye el guardado y la restauracion final
del contexto en `vectors.S`; por ello se rotula `Switch C` y no se usa para
afirmar que HU-E07-03 esta cerrada. Para cada corrida se acumularon numero de
switches, suma, maximo y suma de cuadrados; Python calcula media y desviacion
de las muestras y luego media y desviacion entre las cinco corridas.

## Resultados

| Metrica | TRACE=0, media ± desv. entre corridas | TRACE=1, media ± desv. entre corridas |
|---|---:|---:|
| CPU ociosa | 94,351 ± 0,000 % | 92,233 ± 0,000 % |
| Switch C, media | 152,086 ± 0,000 ciclos | 299,499 ± 0,003 ciclos |
| Switch C, maximo | 167,000 ± 0,000 ciclos | 344,000 ± 0,000 ciclos |
| Switch C, desv. dentro de una corrida | 12,600 ± 0,000 ciclos | 12,426 ± 0,002 ciclos |
| Operaciones completadas por corrida | 25 621,8 | 25 621,8 |
| Eventos por segundo | 0 | 7 279,2 |
| Eventos perdidos y errores | 0 | 0 |

**Overhead observado: 2,118 puntos porcentuales de CPU**, con la misma
cantidad de operaciones en ON y OFF (diferencia 0,00 %). Cumple la meta de
menos de 5 % para esta carga. El costo de emision aislada fue de **48 ciclos
por evento** en la prueba `TRACE_BOARD` sobre la misma placa, equivalente a
1,2 us a 40 MHz. Ese numero mide una emision; el overhead de CPU anterior
incluye tambien filtros, sondas, cambios de tarea y drenaje.

| Tamano de la imagen experimental | TRACE=0 | TRACE=1 | Delta |
|---|---:|---:|---:|
| `.text` del ELF | 37 893 B | 40 921 B | +3 028 B |
| RAM estatica (`.data` + `.bss`) | 173 812 B | 207 124 B | +33 312 B |
| `kernel.bin` | 37 984 B | 41 008 B | +3 024 B |

El delta de RAM incluye el ring buffer de 32 768 B y el estado de traza.
Estos tamanos corresponden a las imagenes experimentales con el instrumento
de benchmark; los binarios normales se reportan por separado.

## Reproduccion

Desde WSL Ubuntu con la placa en `/dev/ttyUSB0`:

```bash
make -B BUILD=build/trace-overhead-on TRACE_OVERHEAD_BOARD_TEST=1 flash
python3 tests/test_trace_overhead_board.py
make -B TRACE=0 BUILD=build/trace-overhead-off TRACE_OVERHEAD_BOARD_TEST=1 flash
python3 tests/test_trace_overhead_board.py
python3 tests/test_trace_overhead_board.py --compare
```

Las capturas crudas estan en `build/trace-overhead-1.json` y
`build/trace-overhead-0.json` (archivos de build). La salida de comparacion fue
`TRACE_OVERHEAD_COMPARE PASS`.

## Sonda final del scheduler (HU-E07-03)

Despues de la comparacion preliminar se amplio la sonda de cambio de tarea
en `vectors.S`. En una prueba separada sobre la placa, `TRACE_SCHED_BOARD`
verifico 306 parejas completas, siete tareas, todos los motivos previstos,
cero errores y cero eventos perdidos. `EV_SWITCH_IN.arg0` dio minimo 481,
media 861 y maximo 1512 ciclos a 40 MHz (media 21,53 us). El intervalo va
desde el inicio de la entrada al vector hasta despues de restaurar a3-a15;
las ultimas instrucciones que restauran a2/a0 y ejecutan `rfe` quedan fuera.
La meta general de menos de 10 us de `docs/05-ESTADO.md` no se alcanzo en
TRACE ON. Esta prueba de scheduler no sustituye las cinco corridas ON/OFF
que hacen falta para medir el overhead del codigo final.
