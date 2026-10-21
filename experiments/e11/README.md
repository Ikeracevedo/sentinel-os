# E11: datos experimentales

Los CSV de esta carpeta son datos crudos capturados en placa. `stats_*.csv`
conserva cada muestra de telemetría; `resultados.csv` resume cada ventana sin
eliminar valores atípicos. Se versionan en `experiments/` porque `docs/` está
excluido por `.gitignore`.
Desde la siguiente batería, cada `stats_*.csv` tendrá un `.uart` con los bytes
recibidos durante la ventana activa de carga. `uart_bytes` cuenta esos bytes
y permite calcular la ocupación **observada** del UART como
`uart_bytes × 10 / (baud × duración_s) × 100`. Los 10 bits son inicio, ocho
datos y parada de cada byte. Si hubo errores del enlace, esta ocupación es
una cota inferior del tráfico real. Las filas históricas dejan `uart_bytes`
vacío porque sus bytes crudos no se capturaron; no se reconstruyen.

`tools/testbench/run.py` automatiza cada perfil de carga `light`, `nominal` o
`heavy` con `--profile`, o los tres en orden con `--all-profiles`. Cada perfil
tiene al menos cinco corridas, con CSV crudo individual y una fila por corrida
en `resultados.csv`. El flasheo de TRACE ON es opcional con `--flash`; la
variante TRACE OFF usa otro capturador porque no puede emitir la telemetría
binaria que consume este script. HU-E11-01 todavía requiere probar esta batería
en placa y agregar los escenarios que faltan. Las pérdidas de eventos y los
fallos de reserva bajo `heavy` se conservan como resultados observados, sin
descartar corridas por saturación.

El intento `stats_20261021T001537Z_trace-on_heavy_r01.csv` quedó solo con su
encabezado: el primer `inject status` no recibió una respuesta completa antes
de iniciar la carga. No representa una corrida `heavy` ni entra en
`resultados.csv`. Se conserva como evidencia de un intento fallido. El banco
ahora reintenta únicamente esa consulta inicial y muestra contadores del
puente y texto parcial si vuelve a fallar; esos datos permitirán diagnosticar
la causa sin atribuirla de antemano a la carga pesada.
El segundo intento, `stats_20261021T001708Z_trace-on_heavy_r01.csv`, tampoco
inició carga: recibió cero tramas válidas, tres resincronizaciones y ninguna
respuesta del shell tras dos consultas. También se conserva solo con
encabezado. La presencia de COM8 se confirmó como CH340 USB `1a86:7523` en
BUSID `1-5`; esto identifica el adaptador, pero no confirma el firmware que
estaba ejecutando la placa.
La captura `diagnostic_preflash_COM8.uart` resolvió la duda: los 73 bytes son
texto del shell (`sentinel: comando no encontrado: U` y un prompt), no tramas
del protocolo binario. La placa estaba ejecutando una imagen de consola de
texto cuando se intentó `heavy`; no se infiere de esos bytes cuándo ni por qué
cambió la imagen. Se debe volver a flashear `TRANSPORT=1 TRACE=1` antes de
repetir la carga.

La repetición `stats_20261021T002116Z_trace-on_heavy_r*.csv` sí se realizó
tras compilar y flashear `TRACE=1 TRANSPORT=1` en WSL. El flasheo verificó el
hash escrito; el archivo `build/transport/kernel.bin` del PC tiene SHA-256
`bff03e07669539478aff4ea097e77e94122cb1b866e35800b18156e65fea9bd`.
Las cinco ventanas de 10 s completaron 1630,4 ± 3,6 iteraciones por corrida
(media ± desviación estándar entre corridas, calculada en el PC), sin fallos
de reserva. La placa reportó cero eventos descartados en las cinco. El puente
registró, respectivamente, errores CRC `5, 8, 5, 6, 1` y huecos de secuencia
`6, 10, 9, 14, 1`: 25 errores CRC y 40 tramas ausentes en total. Estos
contadores cubren la consulta inicial, la ventana de carga y la limpieza de
cada corrida; no localizan los errores dentro de los 10 s de carga. La quinta
corrida conserva sus 11 muestras frente a 10 en las otras cuatro. La duración
corta tampoco prueba por sí sola si la placa llega a saturarse.

En `resultados.csv` el estado `PASS_LINK_ERRORS` significa que arrancar, medir
y limpiar se completó, pero que el enlace PC-placa tuvo errores CRC o huecos
de trama. Esta reclasificación se hizo para las cinco filas `heavy` ya
capturadas; los valores de medición no se modificaron. El cero de descartes
en el dispositivo no equivale a recepción perfecta en el PC.

La batería automática `stats_20261021T002832Z_trace-on_*` flasheó una sola
imagen `TRACE=1 TRANSPORT=1` y ejecutó cinco corridas de 10 s por perfil sin
intervención entre ellas. Cada corrida tiene CSV de muestras y UART crudo; se
comprobó que `uart_bytes` coincide con el tamaño del archivo y que los
contadores de las muestras son crecientes. La incertidumbre siguiente es la
desviación estándar entre las cinco corridas, calculada en el PC sin excluir
valores atípicos:

| Perfil | Iteraciones por 10 s | UART observado | Descartes en placa | CRC del puente | Tramas ausentes |
|---|---:|---:|---:|---:|---:|
| `light` | 106,0 ± 1,4 | 11,20 ± 0,20 % | 0 | 0 | 0 |
| `nominal` | 516,0 ± 2,2 | 20,53 ± 0,20 % | 0 | 1 | 3 |
| `heavy` | 1626,2 ± 3,4 | 44,09 ± 0,49 % | 0 | 10 | 16 |

La ocupación UART es el cociente entre bytes recibidos y la capacidad
teórica de 921600 bit/s con 10 bits por byte. Cuenta solo los bytes vistos
por Windows durante la ventana de carga; si el enlace pierde bytes, es una
cota inferior del tráfico emitido. El decodificador volvió a procesar los
15 UART crudos: nueve de los diez errores CRC y 15 de las 16 tramas ausentes
de `heavy` ocurrieron dentro de las ventanas de carga; los restantes se
contaron durante consultas o limpieza. Una corrida `nominal` tuvo un error CRC
y tres tramas ausentes, todas dentro de su ventana. La placa no reportó
descartes de su búfer en estas ventanas cortas. El resultado no demuestra
saturación ni cumplimiento de HU-E11-04 en un intervalo largo.

La primera captura del 20 de octubre de 2026 (`stats_20261020T170143Z_*`)
queda conservada como piloto. Las cinco corridas iniciaron y detuvieron la
carga con éxito, sin pérdidas observadas, pero el resumen incluyó muestras
posteriores a `inject stop load`. Ese comando reinicia `switch_count`; por
eso los deltas de switch de esa captura no son válidos. `resultados.csv`
identifica esas filas con `PILOT_INVALID_WINDOW`. El script corregido graba
solo las muestras recibidas durante la ventana de carga.

`tools/testbench/overhead.py` captura el benchmark E07 de cinco ventanas de
60 s por variante. Guarda todos los bytes UART en `.uart`, cada línea recibida
en CSV, y las cinco mediciones completas en `overhead_runs.csv`. La etiqueta
`overhead-standard` identifica esta carga particular: no equivale al comando
`inject load nominal`.

Ambos capturadores aceptan `--flash --busid 1-5` desde Windows. Esa opción
entrega el CH340 a WSL, compila y graba la variante, devuelve el puerto a
Windows y comienza la captura sin intervención intermedia. El `BUSID` se debe
comprobar con `usbipd list` antes de cada ejecución; el número de COM también
puede cambiar. `run.py` solo permite flasheo automático TRACE ON con
`TRANSPORT=1`, porque el firmware prohíbe `TRANSPORT=1` con `TRACE=0`.

El traspaso automático se verificó en placa en la sesión de archivos
`stats_20261020T195444Z_*`: WSL compiló y flasheó la imagen
`build/transport/kernel.bin` (SHA-256
`143125afdd30ef61c71fbcdbc832c003cb62f09acb403909b20299c31c1398cba`),
el USB volvió a COM8 y cinco corridas nominales de 10 s terminaron sin
pérdidas, errores CRC ni huecos de trama. Esto verifica el ciclo para **un
escenario**; la batería completa de HU-E11-01 sigue pendiente.

`overhead_summary.csv` y `overhead_comparison.png` se generan con
`tools/testbench/analyze_overhead.py`, indicando los IDs de las dos sesiones.
Para estas capturas:

```powershell
py tools/testbench/analyze_overhead.py --on-session 20261020T172954Z --off-session 20261020T175606Z
```

La desviación de cada variante es la desviación estándar **entre sus cinco
corridas**. La incertidumbre de una diferencia ON−OFF se estima como la raíz
de la suma de los cuadrados de esas dos desviaciones, porque las variantes se
midieron en bloques separados. No incluye error sistemático del reloj ni
valida por sí sola la latencia real sin el coste de la sonda. La duración de
switch aquí es la **observada con el propio instrumento** y excluye las últimas
instrucciones del retorno de interrupción. RAM y tamaño `.bin` se calculan de
los ELF y binarios del PC, no son mediciones hechas en la placa.
