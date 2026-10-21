# E11 · telemetría bajo carga nominal y heavy

`analyze_telemetry.py --session 20261021T002832Z` selecciona las cinco
corridas de 10 s por perfil de `resultados.csv` y exige el UART crudo
correspondiente a cada una. Comprueba la duración, el número de corrida,
el commit y el tamaño de los archivos. Incluye corridas con CRC o huecos;
ningún valor atípico se retira.

| Perfil | Emitidos/10 s, media ± σ | Descartados por placa | UART observado | Errores del enlace PC |
|---|---:|---:|---:|---:|
| nominal | 9 862,8 ± 42,8 | 0/49 314 (0 %) | 20,53 ± 0,20 % | 1 CRC, 3 tramas ausentes |
| heavy | 21 762,0 ± 224,1 | 0/108 810 (0 %) | 44,09 ± 0,49 % | 10 CRC, 16 tramas ausentes |

La σ es la desviación entre cinco corridas. La ocupación UART se calculó en
el PC como `bytes_UART × 10 / (segundos × 921600) × 100`; 10 es el número
aproximado de bits por byte en UART 8N1. Es una **cota inferior de lo enviado**
si Windows perdió bytes. Los descartes son contadores del dispositivo,
distintos de los errores CRC y huecos observados por el puente en Windows.
La meta S5 de menos de 1 % de descartes bajo `nominal` se cumple. La
saturación de la cola de la placa **no se observó** incluso en `heavy`; sí
crecieron los errores de enlace en el PC. No se infiere una tasa exacta de
saturación a partir de esta batería.

Datos crudos: `stats_20261021T002832Z_trace-on_{nominal,heavy}_r*.uart` y
sus CSV, versionados previamente en el repositorio. Resultado calculado:
`telemetry_summary_20261021T002832Z.csv`. El informe narrativo pertenece a E12.
