# Resumen de la sesión E11

## Qué se construyó

- Banco automático de cargas `light`, `nominal` y `heavy`, cinco ventanas por
  perfil, con fecha, revisión Git, estado de enlace, CSV de recursos y UART
  crudo. Los escenarios de fuga y fragmentación tienen un banco equivalente.
- Monitor de CPU, heap, hueco mayor, tareas, traza y eventos descartados. La
  lectura en reposo mostró ~3 % de CPU ocupada; no es un termómetro.
- Analizadores de switches, overhead TRACE, detector, telemetría, memoria y
  falsas alertas. Los valores atípicos y capturas fallidas se conservaron.
- Tabla maestra de 24 métricas y cuatro gráficas: histograma de cambios,
  comparación TRACE ON/OFF, evolución del heap y fragmentación.
- Auditoría para revisar que cada evidencia requerida está en Git, incluso
  cuando `docs/` está excluido por `.gitignore`.

## Lo medido en la ESP32 y lo calculado en Windows

| Experimento | Dato observado en placa | Cálculo o comprobación en PC |
|---|---|---|
| Carga nominal, cinco ventanas de 30 s | Cero descartes declarados, 1515–1520 iteraciones por ventana | 57 940 cambios emparejados; media de medias 22,355 ± 0,223 µs. S1 <10 µs **no cumple**. |
| TRACE ON/OFF, cinco ventanas de 60 s cada uno | Estadísticas de CPU y carga de cada variante | Costo CPU +1,285 ± 0,009 puntos porcentuales; S2 <5 **cumple**. RAM estática +33 320 B, binario +3296 B. |
| Detector, 20 inyecciones | 20 alertas de 20 bloqueos | Demora 320,862 ± 158,026 ms; máximo 544,301 ms. Último coste consultado 51,144 ± 0,229 µs. |
| Hora nominal continua | 180 450 iteraciones, 7228 chequeos, cero alertas finales | UART releído: cero alertas recibidas, 14 CRC y 901 huecos; la pérdida limita la completitud del enlace. |
| Telemetría, cinco ventanas nominales y cinco heavy | 49 314 y 108 810 eventos emitidos, cero descartes | Ocupación UART 20,53 ± 0,20 % nominal y 44,09 ± 0,49 % heavy; heavy no saturó la cola de placa. |
| Cronómetro externo, cinco ventanas de 10 s | 12 007 cambios completos por CCOUNT | 24,087 ± 0,007 µs por cambio y cota Windows 4266,3 ± 32,6 µs por cambio. La cota incluye trabajo de tareas y comunicación. |

`±` indica desviación estándar entre corridas salvo aclaración en la tabla
maestra. Ninguna cifra de Windows se presenta como medida directa de un pulso
GPIO. No hubo analizador lógico u osciloscopio, así que la discrepancia
GPIO–CCOUNT <5 % no fue verificable. La hora de falsas alertas es una sola
observación continua de 60 minutos, según su criterio, sin desviación entre
horas. Los errores de enlace, los 301 switches nominales ≥80 µs y todos los
pilotos permanecen en los archivos crudos.

## Incidentes y decisiones

- La carga `heavy` falló al principio porque la placa había quedado con una
  imagen de consola textual. Una captura UART lo reveló; se volvió a grabar
  `TRACE=1 TRANSPORT=1` y después se ejecutó la batería completa.
- Un piloto de fuga tuvo 1 CRC y 1 hueco. El piloto de fragmentación y la
  batería formal de memoria se mantuvieron separados.
- Iker señaló calor intenso una vez en el chip largo junto al LED, alimentado
  solo por USB. Después informó temperatura normal al tacto en varias
  pruebas. No se midieron temperatura ni corriente y no se infiere que el
  kernel haya causado aquel episodio.
- El primer piloto de reloj externo falló por una columna de CSV incorrecta;
  se corrigió y se conservaron sus 4096 bytes UART y fila `FAIL`. Los siguientes
  pilotos y cinco corridas formales pasaron.
- CP7 sigue siendo un cierre separado: E09/E10 estaban verificadas en placa;
  la demostración al profesor y la publicación remota del tag no son criterios
  de E11.

## Para continuar con E12

La comprobación final `py tools/testbench/audit_e11.py` pasó sobre 115
archivos de evidencia incluidos en el índice de Git. Los scripts Python
compilaron sin error; las 11 pruebas del puente pasaron en Windows fuera del
aislamiento y `make test` pasó completo en WSL (incluidos heap, VFS, traza,
protocolo, grafo y pruebas de E10). La batería alternativa de HU-05 se ejecutó
en la placa por COM8 CH340 y sus cinco filas quedaron `PASS`.

Usar `master_results.csv`, las gráficas, `external_clock_method.md` y los
resúmenes específicos como fuente. Explicar S1 fallida, la falta de
instrumento GPIO, la diferencia entre eventos descartados por el dispositivo
y tramas perdidas por Windows, y el costo de la propia sonda. E12 redactará
el informe; E11 entrega los datos y los métodos.
