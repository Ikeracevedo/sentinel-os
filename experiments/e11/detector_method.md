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
hora nominal sin falsos positivos quedaban pendientes al cerrar el piloto.

## Batería formal de 20 inyecciones

La sesión `20261021T042124Z` empleó el commit limpio `0323e6a` y el mismo
firmware `TRACE=1`, `TRANSPORT=1` en las 20 corridas. El analizador
`tools/testbench/analyze_detector.py` volvió a decodificar cada UART y
comprobó una inyección, dos alertas, marcas `CCOUNT` idénticas al CSV,
tamaño del crudo y ausencia de CRC, huecos y descartes. Total: 20/20
detecciones, 79 chequeos y 1.101.824 bytes UART conservados.

La demora entre la inyección y la primera alerta, recalculada de los ciclos
sin redondear, fue **320,862 ± 158,026 ms** (desviación entre 20 corridas),
con mínimo **70,294 ms** y máximo **544,301 ms**. No se excluyeron extremos.
El detector corre cada 500 ms (`DETECTOR_PERIOD_MS`); la fase entre el
momento de inyectar y el siguiente chequeo explica buena parte de la
dispersión. El tiempo de creación del escenario y otros trabajos del SO
pueden añadir demora; esta batería no permite atribuirles una cantidad
exacta.

El coste de la última ejecución consultada en cada corrida fue
**51,144 ± 0,229 µs**. Esta no es necesariamente la misma ejecución que
encontró el ciclo. El CSV de captura restó dos tiempos ya truncados a
microsegundos; en cinco filas su demora difiere menos de 1 µs de la resta
directa de ciclos. El resumen formal usa los ciclos originales y conserva
las filas originales sin corregirlas.

La hora nominal sin inyección sigue pendiente. E10 registró previamente
una hora sin falsas alertas, pero esa prueba no conservó el UART crudo
requerido para la evidencia de E11.
