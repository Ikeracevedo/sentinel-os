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
