# E11 · observación nominal sin falsos positivos

`tools/testbench/false_positives.py` ejecuta `inject load nominal` sin
inyectar ningún bloqueo. Cuenta alertas tanto en eventos de traza como en
tramas dedicadas, y compara los contadores de deadlock, fragmentación e
inanición con los valores previos. Cada minuto verifica que la carga avance,
que el detector siga ejecutándose y que el grafo no contenga ciclos. Guarda
un CSV de controles y todos los bytes UART recibidos. Al finalizar detiene
la carga y comprueba la restauración del heap y del grafo.

El piloto de 30 s `20261021T042937Z` observó 1.520 iteraciones y 62
chequeos en el primer control; al control final fueron 1.565 iteraciones y
64 chequeos. No hubo alertas ni descartes de la placa. Hubo **1 CRC y 1
hueco de trama en Windows**, que se conservan como errores de enlace. Una
segunda decodificación del UART de 634.880 bytes encontró igualmente cero
eventos/tramas de alerta, 1 CRC y 1 hueco. La comprobación de los contadores
del detector aporta evidencia adicional frente a una alerta que pudiera
haberse perdido justo en esa trama. El piloto valida el procedimiento, no
sustituye la hora continua requerida por HU‑E11‑04.

## Hora formal

La sesión `20261021T043250Z` empleó el código del commit `bdfc322` y
observó `inject load nominal` durante al menos 3.600 s continuos. El
control al segundo 3.600 registró 180.010 iteraciones y 7.210 chequeos;
el control final a 3.608,872 s registró 180.450 iteraciones y 7.228
chequeos. Las alertas de deadlock, fragmentación e inanición permanecieron
en cero, igual que las tramas/eventos de alerta recibidos. La limpieza
devolvió el escenario y el heap a su estado inicial. Es una observación
continua de una hora, **no** cinco repeticiones de una hora; la variación
entre horas no está estimada.

El UART crudo mide 67.610.726 bytes. La reproducción en PC encontró cero
alertas y reprodujo exactamente los **14 CRC** y **901 huecos de trama**
registrados en vivo; el dispositivo reportó cero descartes. Los huecos
impiden afirmar que cada evento UART fue recibido. Los contadores del
detector consultados cada minuto y al final son la evidencia principal
para cero falsas alertas de deadlock durante la hora. El resultado no se
extrapola a funcionamiento indefinido.

La columna `git_dirty=true` de esta sesión es un error de etiquetado del
script original: creó el archivo UART sin seguimiento antes de consultar
`git status`. El repositorio estaba limpio antes de ejecutarlo y el campo
`commit=bdfc322...` identifica el código de la captura. El defecto del
etiquetado no altera los contadores ni los bytes UART.
