# E07: linea base de costo de instrumentacion

Fecha: 2026-10-15. Placa: ESP32-D0WD-V3, CPU configurada a 40 MHz.
Compilador: xtensa-esp32-elf-gcc 13.2.0, `-O2`.

Se midio la misma carga en ambas imagenes: 256 pares `kmalloc(32)`/`kfree`
y 256 pares `mutex_lock`/`mutex_unlock` sobre un mutex libre. CCOUNT rodeo cada
par; no hubo salida UART dentro de los bucles. Los promedios incluyen el costo
de leer CCOUNT y la posible interferencia ocasional del timer. La minima
observada ayuda a distinguir esa interferencia. Esta es una sola ejecucion,
util como linea base para HU-E07-08 y E11, no como intervalo estadistico.

| Camino | TRACE=0 promedio | TRACE=1 promedio | Delta promedio | TRACE=0 minimo | TRACE=1 minimo |
|---|---:|---:|---:|---:|---:|
| `kmalloc` + `kfree` | 787 | 925 | +138 ciclos | 765 | 899 |
| `mutex_lock` + `mutex_unlock` | 158 | 370 | +212 ciclos | 155 | 362 |

Ambas cargas: 256 muestras, 0 errores, 0 eventos perdidos.
`tests/test_trace_cost_board.py --compare`: `TRACE_COST_COMPARE PASS`.

Tamanos del ELF normal, medidos con `tests/check_trace_off.py`:

| Variante | `.text` | `.data` | `.bss` |
|---|---:|---:|---:|
| TRACE=1 | 37 770 B | 4 B | 206 484 B |
| TRACE=0 | 37 062 B | 4 B | 173 700 B |
| Ahorro al apagar traza | 708 B | 0 B | 32 784 B |

El test de simbolos confirma que el ELF `TRACE=0` no define funciones `trace_*`.
El shell completo paso con ambas variantes: 67 PASS, 0 FAIL. En una primera
ejecucion de TRACE=0 hubo dos fallos no reproducidos; el banner de esa captura
contenia bytes corruptos del puerto serie.

## Reproduccion

```bash
make
make TRACE=0
python3 tests/check_trace_off.py

make TRACE=0 BUILD=build/trace-cost-off TRACE_COST_BOARD_TEST=1 flash
python3 tests/test_trace_cost_board.py
make BUILD=build/trace-cost-on TRACE_COST_BOARD_TEST=1 flash
python3 tests/test_trace_cost_board.py
python3 tests/test_trace_cost_board.py --compare
```
