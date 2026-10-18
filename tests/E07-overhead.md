# E07: costo de la instrumentacion en la placa

> Resultado final de HU-E07-08: overhead observado de **2,272 puntos
> porcentuales de CPU** con cinco corridas de 60 s por variante. Se midio
> tambien el cambio de contexto desde el vector en TRACE=0 y TRACE=1.

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

Se midieron dos intervalos con `CCOUNT`: `Switch C`, dentro de `switch_task`,
y `Switch completo`, desde la entrada al vector hasta despues de restaurar
a3-a15. Quedan fuera las ultimas instrucciones de a2/a0 y `rfe`. Ambos
instrumentos estan presentes en ON y OFF. En cada corrida se acumularon
numero de switches, suma, maximo y suma de cuadrados. Python calcula la media
y desviacion de las muestras, y luego la media y desviacion entre las cinco
corridas. Las lineas UART se repiten con pausas despues de cada ventana de
60 s; esa salida no entra en las cifras medidas.

## Resultados

| Metrica | TRACE=0, media ± desv. entre corridas | TRACE=1, media ± desv. entre corridas |
|---|---:|---:|
| CPU ociosa | 93,898 ± 0,000 % | 91,626 ± 0,001 % |
| Switch C, media | 159,086 ± 0,000 ciclos | 244,012 ± 0,002 ciclos |
| Switch C, maximo | 174,000 ± 0,000 ciclos | 289,000 ± 0,000 ciclos |
| Switch C, desv. dentro de una corrida | 12,600 ± 0,000 ciclos | 12,651 ± 0,002 ciclos |
| Switch completo, media | 816,860 ± 0,000 ciclos | 1 018,250 ± 0,111 ciclos |
| Switch completo, maximo | 1 398,000 ± 0,000 ciclos | 1 649,800 ± 21,753 ciclos |
| Switch completo, desv. dentro de una corrida | 392,241 ± 0,000 ciclos | 398,620 ± 0,091 ciclos |
| Operaciones completadas por corrida | 25 621,0 | 25 621,8 |
| Eventos por segundo | 0 | 7 279,2 |
| Eventos perdidos y errores | 0 | 0 |

**Overhead observado: 2,272 puntos porcentuales de CPU**, con practicamente
la misma cantidad de operaciones en ON y OFF (diferencia +0,00 %). Cumple la meta de
menos de 5 % para esta carga. El costo de emision aislada fue de **48 ciclos
por evento** en la prueba `TRACE_BOARD` sobre la misma placa, equivalente a
1,2 us a 40 MHz. Ese numero mide una emision; el overhead de CPU observado
incluye tambien filtros, sondas, cambios de tarea y drenaje.

| Tamano de la imagen experimental | TRACE=0 | TRACE=1 | Delta |
|---|---:|---:|---:|
| `.text` del ELF | 38 219 B | 41 299 B | +3 080 B |
| RAM estatica (`.data` + `.bss`) | 173 852 B | 207 172 B | +33 320 B |
| `kernel.bin` | 38 304 B | 41 392 B | +3 088 B |

El delta de RAM incluye el ring buffer de 32 768 B y el estado de traza.
Estos tamanos corresponden a las imagenes experimentales con el instrumento
de benchmark; los binarios normales se reportan por separado.

La regresion final paso: `make test` (58 asserts de heap, 92 de VFS, un millon
de eventos verificados y filtros) y `tools/shell_test.py` en las imagenes
normales TRACE ON y OFF (67 PASS, 0 FAIL cada una). La primera lectura serial
del shell tuvo caracteres perdidos en ambas variantes; al repetir sin cambiar
el kernel, ambas baterias pasaron. La fiabilidad del transporte UART sigue
siendo una limitacion conocida para E08.

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
TRACE ON. En la carga estandar final, la media fue 20,42 us con TRACE OFF y
25,46 us con TRACE ON. El incremento fue 201,39 ciclos (5,03 us) por switch.

## Comparacion preliminar historica

Antes de medir el vector completo, el mismo protocolo dio 2,118 puntos de
CPU de overhead. Ese valor y su latencia del tramo C describen una revision
anterior del codigo; la tabla de resultados de arriba es la referencia final.
