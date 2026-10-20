# E11: datos experimentales

Los CSV de esta carpeta son datos crudos capturados en placa. `stats_*.csv`
conserva cada muestra de telemetría; `resultados.csv` resume cada ventana sin
eliminar valores atípicos. Se versionan en `experiments/` porque `docs/` está
excluido por `.gitignore`.

La primera batería (`tools/testbench/run.py`) automatiza el perfil nominal,
incluido el flasheo opcional de TRACE ON. Todavía no satisface HU-E11-01:
faltan las otras variantes y escenarios, y una batería completa en placa.

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
