# E11 · medición del detector de deadlocks

`tools/testbench/detector.py` crea un ciclo entre dos tareas mediante
`inject deadlock` y conserva el UART de cada intento. La demora se calcula
en el PC como la diferencia entre las marcas `CCOUNT` de `EV_INJECT` y del
primer `EV_ALERT`. Ambas marcas provienen de la placa; el retardo del puerto
serie no entra en esa resta. El script exige dos alertas que correspondan a
las aristas del grafo, comprueba los contadores del detector y verifica que
`inject stop deadlock` restaure tareas, mutexes, grafo y heap.

El piloto `20261021T040547Z` produjo `detector_pilots.csv` y un UART de
57.344 bytes. El PC calculó 487.327 µs a partir de marcas de la placa, sin CRC, huecos ni
descartes. Una segunda decodificación del UART en el PC reprodujo las tres
marcas (`EV_INJECT` y dos `EV_ALERT`) y la resta de 487.327 µs. La nueva
decodificación contó menos tramas que la sesión en vivo porque el archivo
empieza después de que el puente abrió el puerto. Su único resincronizado
inicial no equivale a un error de la medición en vivo.

El comando `detector` devuelve el costo de la **última ejecución** y el
máximo acumulado desde el arranque. En el piloto fueron 2.042 y 2.459
ciclos, equivalentes a 51,05 y 61,475 µs a 40 MHz. No se conoce qué
ejecución específica produjo la alerta, por lo que 51,05 µs no se presenta
como costo exacto de esa detección. Las mediciones de 20 inyecciones y la
hora nominal sin falsos positivos quedan pendientes.
