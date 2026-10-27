# Sentinel OS — Estado y Progreso

> 📌 **DOCUMENTO VIVO.** Se actualiza al empezar y al terminar cada sesión de trabajo.
> Es lo primero que se lee al retomar el proyecto.
>
> **Última actualización:** 2026-10-21

---

## 1. Estado global

| Campo | Valor |
|---|---|
| **Fase actual** | 🟡 **E12 — Documentación y sustentación**, con E20-HU-02 (`slice`) iniciada por solicitud de Iker. E11 conserva 5/6 historias que cumplen y S1 fallida; E12 y E20 siguen abiertas. |
| **Checkpoint alcanzado** | 🟡 **CP7 — "Se diagnostica"** validado en placa e integrado en `main` (`04703e9`), con tag local `CP7`; falta la demostración al profesor y no se verificó la publicación remota del tag. |
| **Siguiente checkpoint** | **CP8 — "Se demuestra"**, requiere completar E11 y E12. |
| **Épica en curso** | **E12** en `feature/documentacion-e12`. Usa la tabla maestra E11, 115 evidencias versionadas y la rúbrica formal R-002. El video de portafolio de 3 min y el video calificado de hasta 10 min son entregables distintos. |
| **Horas invertidas** | E01 ≈ 18 h; E02 y E03: **completar con las horas reales** |
| **Bloqueantes** | Ninguno |
| **Riesgo activo más alto** | Iker ubicó el calor anterior en el chip largo junto al LED, probablemente el puente USB-serie; no hay temperatura ni consumo medidos. Las observaciones posteriores no sustituyen una medición térmica. Sin analizador/osciloscopio no se pudo probar la discrepancia GPIO–CCOUNT <5 %. CP7 mantiene pendiente la demostración al profesor. |

---

## 2. Progreso por checkpoint

| CP | Nombre | Estado | Fecha |
|---|---|---|---|
| CP0 | Late | ✅ Completado | 2026-09-20 |
| CP1 | Respira | ✅ Completado | 2026-10-12 |
| CP2 | Vive | ✅ Completado | 2026-10-12 |
| CP3 | Recuerda | ✅ Completado | 2026-10-12 |
| CP4 | ⭐ Obedece (mínimo entregable) | ✅ Completado (pendiente video) | 2026-10-13 |
| CP5 | Se observa | ✅ Validado en placa y etiquetado | 2026-10-17 |
| CP6 | Se comunica | ✅ Validado en placa y etiquetado | 2026-10-18 |
| CP7 | ⭐ Se diagnostica | 🟡 Validado en placa, fusionado en `main` y etiquetado localmente; pendiente demostración y verificación del tag remoto | 2026-10-20 |
| CP8 | Se demuestra | ⬜ Pendiente | — |
| CP9 | Se desdobla | ⬜ Diferido (backlog) | — |
| CP10 | Se conecta | ⬜ Diferido (backlog) | — |
| **CP11** | 🔴 **Se califica** — los 9 criterios de la rúbrica en ✅ | ⬜ Pendiente | — |
| **CP12** | 🔴 **Se entrega** — repo, video, capturas, portada, manual | ⬜ Pendiente | — |

> 📌 **Cambio de prioridad (2026-10-21):** llegó la rúbrica formal del profesor (R-002).
> **CP11 y CP12 tienen prioridad sobre CP9 y CP10.** Ver `09-RUBRICA-PROFESOR.md` y
> `10-PLAN-MVP-REUNION.md`.

Leyenda: ⬜ Pendiente · 🟡 En curso · ✅ Completado · ⏭️ Descartado

---

## 3. Progreso por épica

| ID | Épica | Estado | HUs | Horas | Notas |
|---|---|---|---|---|---|
| E01 | Fundación bare-metal | ✅ | 9/9 | 18/16 | **Completa.** CP0 alcanzado, lista para E02 |
| E02 | Scheduler | ✅ | 11/11 | —/19,5 | **Completa.** Soak 1 h sin errores (CP1, CP2) |
| E03 | Memoria | ✅ | 8/8 | —/9 | **Completa** (CP3). Dos criterios de comando (`ps`, `free -v`) esperan a E06 |
| E04 | Sincronización | ✅ | 6/6 | —/6 | **Completa.** 29 pruebas en placa |
| E05 | VFS | ✅ | 8/8 | —/9 | **Completa.** 92 asserts en host + pruebas en placa |
| E06 | Shell | ✅ | 10/10* | —/10 | ⭐ CP4. *Faltan el video de 60 s y mostrarlo (tuyos) |
| E07 | Instrumentación | ✅ | 8/8 | —/8 | CP5 validado y etiquetado: 48 ciclos/evento, overhead +2,272 puntos de CPU. Meta general de switch < 10 µs no alcanzada. |
| E08 | Transporte y puente | ✅ | 7/7 | —/7,5 | Imagen final: 600,719 s, 156096 eventos y 0 pérdidas; SQLite/replay coinciden. Tracer 1,75%. CP6 validado y etiquetado. |
| E09 | Inyección de fallos | ✅ | 6/6 | 0/4 | Seis fallos 10/10 en placa, limpieza exacta y EV_INJECT; carga nominal reproducible. |
| E10 | Detección determinista | ✅ | 6/6 | —/6 | Historias verificadas en placa: deadlock <1 s, coste <100 µs, una hora nominal sin falsas alertas, patologías y comandos de diagnóstico. PR #2 fusionado en `main`; tag CP7 local. Demostración al profesor pendiente. |
| E11 | Validación experimental | 🟡 | 5/6 cumplen; 6/6 evaluadas | —/6 | Banco, latencia, overhead, detector/telemetría, alternativa externa y tabla/gráficas. S1 incumplida; S2 y S5 cumplidas; GPIO <5 % no comprobable. Auditoría PASS: 115 archivos de evidencia en Git (`710000c`). |
| E12 | Documentación | 🟡 | 1/6 cerrada (README); ~90% de preparación | —/8,5 | Informe y PPTX preparados y verificados; video publicado, ensayos y simulacro pendientes. P8 sin formato confirmado. CP8 abierto. |
| E13 | Dual-core AMP | ⬜ | 0/7 | 0/12 | |
| E14 | Nube AWS | ⬜ | 0/6 | 0/10 | |
| E15 | Análisis predictivo | ⬜ | 0/4 | 0/5 | |
| E16 | Dashboard | ⬜ | 0/6 | 0/8 | |
| E17 | CI y tests | ⬜ | 0/4 | 0/5 | |
| E18 | Políticas avanzadas | ⬜ | 0/5 | 0/14 | Backlog |
| E19 | Persistencia en flash | ⬜ | 0/4 | 0/12 | Backlog |
| **E20** | 🔴 **Comandos y lanzador (rúbrica)** | 🟡 | 0/6 | 0/8,5 | `slice` medido en placa: 60 vs 8 rotaciones en 12 s (7,5×); `stats` y `help` en validación. |
| **E21** | 🔴 **Dashboard con Gantt** | ⬜ | 0/5 | 0/8 | ⚠️ Cuello de botella: 4 criterios dependen → C2, C4, C6, C7 |
| **E22** | 🔴 **Validación de la rúbrica** | ⬜ | 0/5 | 0/5 | ⭐ CP11. Verifica los 9 + 10 capturas |
| **E23** | 🔴 **Entregables y manual de uso** | ⬜ | 0/6 | 0/9 | ⭐ CP12. Video, portada, repo, **MANUAL-DE-USO.md** |

---

## 4. Métricas alcanzadas

> Se rellenan conforme se midan. Estos números son el resultado del proyecto.

| Métrica | Meta | Medido | Fecha |
|---|---|---|---|
| E11: cronómetro externo, `switchbench`, 5 × 10 s | Alternativa sin instrumento | 12 007 cambios completos; CCOUNT 24,087 ± 0,007 µs/switch; cociente Windows 4266,3 ± 32,6 µs/switch (media ± desviación entre corridas). Cero CRC/huecos/descartes. El cociente Windows incluye tareas y comunicación; discrepancia GPIO–CCOUNT <5 % no medida. | 2026-10-21 |
| E11: tabla maestra y gráficas | Datos e incertidumbre | `experiments/e11/master_results.csv` separa metas cumplidas, fallidas y no comprobables; histograma, comparación ON/OFF, heap `leak` y fragmentación `frag` con crudos conservados. | 2026-10-21 |
| E11: detección de deadlock, 20 inyecciones | 100 % | 20/20; demora 320,862 ± 158,026 ms entre corridas; mínimo 70,294, máximo 544,301 ms. CSV y 20 UART versionados en `bdfc322`; sin errores de enlace ni descartes. | 2026-10-20 |
| E11: coste de última ejecución del detector, 20 inyecciones | — | 51,144 ± 0,229 µs entre corridas; no es necesariamente el coste de la ejecución que produjo cada alerta. | 2026-10-20 |
| E11: telemetría nominal/heavy, cinco ventanas de 10 s por perfil | < 1 % perdidos nominal | Nominal: 49 314 emitidos, 0 descartados, 20,53 ± 0,20 % de UART; heavy: 108 810 emitidos, 0 descartados, 44,09 ± 0,49 % de UART. Ocupación calculada en PC; heavy no saturó la cola de placa. | 2026-10-20 |
| E11: piloto de falsas alertas nominal, 30 s | 0 | 0 alertas en 64 chequeos finales; 1 CRC y 1 hueco en Windows, 0 descartes en placa. La hora formal se completó después. | 2026-10-20 |
| E11: falsas alertas nominal, hora formal | 0 | 0 alertas en 3608,872 s observados, 180 450 iteraciones y 7228 chequeos. UART crudo de 67 610 726 B: 14 CRC y 901 huecos en Windows, 0 descartes en placa; limpieza confirmada. | 2026-10-21 |
| E11: `inject leak`, mínimo de heap libre, 5 × 20 s | — | 85084 ± 250 B (media ± desviación entre cinco corridas completas); línea base 96676 B y restauración exacta en las cinco | 2026-10-20 |
| E11: `inject frag`, fragmentación máxima, 5 × 20 s | — | 92,398 ± 0,000 % calculado en PC; la placa reportó 49936 B libres y hueco mayor de 3796 B en las cinco; restauración exacta | 2026-10-20 |
| E11: enlace durante batería de memoria completa | 0 errores deseable | 0 CRC, 0 huecos y 0 eventos descartados en las 10 ventanas; UART crudo reproduce todos los CSV | 2026-10-20 |
| E11: switch observado, `inject load nominal`, batería preliminar 5 × 10 s | < 10 µs | 19 693 muestras emparejadas; 22,586 µs de media, 15,925 µs de mediana, 99,5 µs de máximo. Una corrida tuvo 1 CRC y 3 tramas ausentes. Sustituida como batería principal por la medición 5 × 30 s; conserva valor histórico. | 2026-10-20 |
| E11: switch observado, `inject load nominal`, 5 × 30 s | < 10 µs | 57 940 cambios emparejados; media de cinco corridas 22,355 ± 0,223 µs; conjunto: mínimo 11,975, mediana 15,925, σ individual 11,299, p95 37,875, p99 42,425, máximo 99,350 µs. Meta no alcanzada; incluye sonda. 99 eventos no recibidos (0,0609 %), 0 descartes en placa. 301 atípicos ≥80 µs se asocian a la activación periódica del detector. | 2026-10-20 |
| E11: desglose complementario de switch, 5 × 10 s | — | 12 107 cambios emparejados, 0 CRC/huecos/descartes. Quantum: 571, 33,550 ± 0,000 µs; bloqueo: 5882, 14,780 ± 0,001 µs; yield: 799, 16,503 ± 0,011 µs; apropiación: 4855, 35,550 ± 0,025 µs (media ± desviación de 5 corridas). Carga distinta de nominal, excluida de su media y de S1. | 2026-10-20 |
| E11: overhead de CPU, carga `overhead-standard` | < 5% | +2,303 ± 0,001 puntos porcentuales (desv. propagada entre 5 corridas por variante); cumple para esta carga distinta de `inject load nominal` | 2026-10-20 |
| E11: overhead de CPU, `inject load nominal`, 5 × 60 s ON/OFF | < 5% | +1,285 ± 0,009 puntos porcentuales (desviación entre 5 corridas por variante); meta S2 cumplida. Costo integral 109,111 ± 0,849 ciclos/evento, distinto de los 48 ciclos aislados de E07. Datos formales versionados en `7142029`. | 2026-10-20 |
| E11: switch completo observado, `inject load nominal`, 5 × 60 s ON/OFF | < 10 µs | OFF 22,6419 ± 0,0002 µs; ON 27,5885 ± 0,0002 µs; σ individual OFF 10,282 y ON 10,490 µs. S1 no cumplida. | 2026-10-20 |
| E11: RAM estática y tamaño de imagen, banco nominal | — | +33 320 B RAM; +3 296 B `.bin`, calculados en PC; una imagen por variante. | 2026-10-20 |
| E11: switch observado, carga `overhead-standard` | < 10 µs | OFF 21,021 ± 0,0002 µs; ON 26,002 ± 0,0023 µs; meta no alcanzada. Incluye la sonda y excluye las últimas instrucciones de retorno | 2026-10-20 |
| E11: RAM estática y tamaño de imagen ON−OFF | — | +33 320 B de RAM y +3 328 B en `.bin`, calculados en PC a partir de las imágenes experimentales actuales | 2026-10-20 |
| E11: carga `heavy`, TRACE ON, 5 × 10 s | — | 1630,4 ± 3,6 iteraciones por corrida (PC, desviación entre 5); cero fallos de reserva y cero descartes reportados por la placa | 2026-10-20 |
| E11: enlace PC bajo `heavy`, 5 × 10 s | — | 25 errores CRC y 40 tramas ausentes entre 5 corridas; los contadores incluyen consultas y limpieza, no solo la ventana de carga | 2026-10-20 |
| E11: UART observado `light`/`nominal`/`heavy`, batería 5 × 10 s cada uno | — | 11,20 ± 0,20 % / 20,53 ± 0,20 % / 44,09 ± 0,49 % de 921600 bit/s; calculado en PC, cota inferior si se perdieron bytes | 2026-10-20 |
| E11: enlace PC en batería de tres perfiles | — | `light`: 0 CRC y 0 huecos; `nominal`: 1 CRC y 3 huecos; `heavy`: 10 CRC y 16 huecos. Placa: cero eventos descartados en los tres perfiles | 2026-10-20 |
| Latencia de cambio de contexto (media) | < 10 µs | OFF 816,860 ciclos (20,42 µs); ON 1 018,250 ciclos (25,46 µs). Meta no alcanzada | 2026-10-17 |
| Latencia de cambio de contexto (máx) | — | OFF 1 398,000; ON 1 649,800 ciclos, media de los máximos de 5 corridas | 2026-10-17 |
| Jitter de switch (desv. estándar) | — | OFF 392,241; ON 398,620 ciclos, media de la desviación por corrida | 2026-10-17 |
| Ciclos por evento de traza | < 50 | 48 ciclos (1,2 µs a 40 MHz) | 2026-10-17 |
| Overhead de instrumentación (% CPU) | < 5% | 2,272 puntos porcentuales, carga estandar de 5 tareas; 5 × 60 s por variante | 2026-10-17 |
| Diferencia de tamaño binario TRACE ON/OFF | — | +3 088 B en `.bin` experimental (+3 080 B `.text`) | 2026-10-17 |
| RAM estatica adicional de traza | — | +33 320 B (`.data` + `.bss`), incluye buffer de 32 KB | 2026-10-17 |
| Eventos perdidos bajo carga nominal | < 1% | 0/436 754 eventos en cada corrida ON | 2026-10-17 |
| Tramo C del switch, media ON/OFF | — | ON 244,012 ± 0,002; OFF 159,086 ± 0,000 ciclos (5 corridas) | 2026-10-17 |
| Tramo C del switch, maximo ON/OFF | — | ON 289; OFF 174 ciclos | 2026-10-17 |
| Tiempo de detección de deadlock | < 1 s | 418,3 ms (2 tareas); 271,1 ms (3 tareas) | 2026-10-19 |
| Coste máximo del detector de deadlock | < 100 µs | 44,9 µs (2 tareas); 48,9 µs (3 tareas) | 2026-10-19 |
| Captura del grafo de espera | < 50 µs | 19,2 µs en reposo; 22,7 µs con ciclo de 2 tareas | 2026-10-20 |
| Falsas alertas de deadlock en carga nominal | 0 en 1 h | 0 alertas nuevas en 3600 s y 7203 chequeos | 2026-10-20 |
| Anticipación de predicción de OOM | ≥ 60% | — | — |
| Tareas concurrentes estables | ≥ 5 | 5 (+ `ctl` + idle) en soak de 1 h, 0 errores | 2026-10-12 |
| Tiempo continuo sin reinicio | ≥ 1 h | 1 h (soak), 0 errores, 0 tareas sin avance | 2026-10-12 |
| Líneas de código del kernel | — | 6143 (`kernel/`, .c .h .S; incluye las pruebas en placa) + 7253 con tests y tools | 2026-10-13 |
| Tamaño del binario | — | ≈ 35,8 KB (`kernel.bin`, modo shell) | 2026-10-13 |
| Costo de estadísticas por cambio de tarea | < 20 ciclos | 16 ciclos | 2026-10-12 |
| `kmalloc` + `kfree` (par) | — | 283 ciclos (≈ 7 µs) | 2026-10-12 |
| Costo del canario del heap | — | ≈ 6 ciclos (≈ 2% del par) | 2026-10-12 |
| Recorrido del heap | tanda < 100 µs | ≈ 64 ciclos/bloque; tanda de 32 bloques ≈ 51 µs | 2026-10-12 |
| Precisión de `sleep(1000)` | error < 2% | 999 ms (0,1%) | 2026-10-12 |
| Pérdida de bytes por el serial | — | 115200: 1–3%; 57600: 0,36% (66/18329) | 2026-10-12 |
| RAM libre tras arrancar | — | Heap: 98 284 B libres de 98 304; `bss` 169 280 B de 180 224 B de DRAM (≈ 10,7 KB de margen) | 2026-10-12 |

---

## 5. Bitácora de sesiones

### 2026-10-21 — E20-HU-02: `slice` en implementación

- Iker pidió iniciar E20-HU-02 antes del cierre de E11/E12. No se editaron sus archivos de tarea.
- Se añadió `slice [ms]` (1..10000 ms), un valor por defecto del planificador y su aplicación
  a las tareas actuales y futuras. Al reducir el quantum, se recorta el turno restante.
- Se definió `EV_SLICE_CHANGE` (`0x61`) con quantum anterior y nuevo para el futuro Gantt.
- El parser de `slice` pasó una prueba C de host con límites y entradas que provocarían
  overflow. `gcc -fsyntax-only` con `-Wall -Wextra -Werror` pasó en TRACE=0 y TRACE=1.
- Iker ejecutó `make clean && make && make size && make test`: imagen Xtensa creada sin
  errores ni warnings; `slice_parse: PASS`, 62 asserts de heap y 92 de VFS sin fallos,
  más el resto de pruebas de host en PASS/OK. Tamaño: text=57879, data=752,
  bss=211728 B. **Pendiente:** columna `QUANTUM`, `stats` y Gantt.
  HU-02 y C6 no están cerrados; no hay commit propuesto todavía.
- `make flash PORT=/dev/ttyUSB0` verificó el hash de 58720 B escritos en la ESP32.
  La primera captura terminó al abrir `miniterm`. En la siguiente, el prompt respondió a
  `ls` y, tras reiniciar, apareció el banner y volvió `sentinel:/home/iker$` sin PANIC
  visible. Arranque observado en placa.
- Prueba de `slice` observada en placa: 10 → 200 → 1500 ms, sin reinicio (`uptime`
  65489 → 118674 ms). `0`, `10001`, `abc` y `4294977296` dieron error y mantuvieron
  1500 ms; al final se restauró 10 ms. Falta medir la relación de switches entre
  200 y 1500 ms y verificar TCB/`ps`, `stats` y Gantt. HU-02 sigue abierta.
- Con `slice 200`, `run hog` creó TID 4 y 5; `ps -v` los mostró. Tras cambiar a 1500 ms,
  otra muestra de `ps -v` indicó CPU 50%/49% para ambos y el shell seguía respondiendo.
  `kill 4` y `kill 5` devolvieron éxito; el siguiente `ps -v` ya no mostró tareas hog y
  `slice 10` restauró el valor inicial. Los valores `CAMBIOS` incluyen preempciones de
  otras tareas y no prueban por sí solos la relación ≥5×.
- `clear` mostró `␛[2J␛[H` en `miniterm`: su filtro `default` convierte ESC en un símbolo
  visible. `make monitor` ahora pide `-f direct` para pasar la secuencia ANSI al terminal.
  Iker confirmó en captura que la pantalla se limpia; el scrollback/wrap del terminal
  conserva su propia presentación. También confirmó que `reboot` vuelve al banner y prompt.
- Se detectó que el planificador reiniciaba `quantum_left` en cada entrada a CPU, incluso
  después de preempciones de `detector` (cada 20 ms). Iker autorizó corregir este código
  preexistente de E02. Ahora la preempción conserva el remanente; bloqueo, cesión o
  agotamiento inician un turno nuevo. Se aplica igual con TRACE=0 y TRACE=1.
- Compilación Xtensa normal, TRACE=0 y banco E20 `SLICE_BOARD_TEST=1` pasaron con `-Werror`.
  `make test` pasó. Se preparó `tests/test_slice_board.c`: dos tareas CPU, ventanas de 12 s
  para 200/1500 ms, conteo de `EV_SWITCH_OUT` con motivo `QUANTUM`, relación ≥5 y cero
  pérdidas de traza. **Aún no se ha flasheado ni ejecutado este banco en placa.**
- La relación de cambios *por quantum* y la de cambios *totales* son métricas distintas:
  `detector` y `trace-heap` producen cambios periódicos independientes del valor de slice.
  La métrica total requerida por C6 sigue sin demostrar; no se marca HU-02 como cerrada.
- Se añadió `QUANTUM` a la tabla de `ps` y `help ps` explica sus columnas. La imagen Xtensa
  normal compiló con este ajuste; queda por comprobar su presentación real en placa.
- Primera corrida del banco E20 en placa, tras flasheo con hash verificado: dos arranques
  dieron `200ms=0 1500ms=0 perdidos=0` y `SLICE_BOARD_DONE FAIL`. No se atribuye aún el
  cero al planificador: el banco no imprimía si se crearon/corrieron sus trabajadores ni
  cuántos cambios de otros motivos recibió. Se añadió diagnóstico de TID, trabajo,
  eventos de salida, preempciones y mínimo de quantum restante; compiló en Xtensa y
  espera repetición en placa. HU-02/C6 siguen abiertos.
- La segunda corrida diagnosticó TID 3/4, ~30 millones de iteraciones por trabajador,
  6004 salidas de esos trabajadores, 6000 preempciones, mínimo `quantum_left=5` y cero
  eventos perdidos. El propio observador del banco despertaba cada 5 ms, divisor exacto
  de 200 y 1500 ms; al coincidir con el fin de turno, la salida quedaba clasificada como
  preempción de prioridad superior. Se cambió el observador a intervalos de 1003 ms para
  quitar esa sincronización y reducir su interferencia. Compilación Xtensa PASS; repetir
  en placa antes de concluir si el scheduler cumple la relación ≥5×.
- Tercera corrida en placa: ~30,7 millones de iteraciones por trabajador, 686 salidas
  totales, 348 de trabajadores, 326 preempciones, cero pérdidas. La traza etiquetó solo
  11 rotaciones `QUANTUM` con 200 ms y 4 con 1500 ms (2,75×): no cumple ≥5× con ese
  contador. Se añadió una fuente del planificador `sched_quantum_rotations()` que cuenta
  vencimientos de quantum con otra tarea de igual prioridad lista, incluso si en ese
  mismo tick despierta una de prioridad superior y el evento se etiqueta `PREEMPT`.
  El banco compara ese contador y conserva el conteo de trazas como diagnóstico. Las
  imágenes Xtensa TRACE=1/0 compilaron; falta la nueva medición en placa.
- Cuarta corrida en placa con el contador del planificador: 60 rotaciones en 12 s a
  200 ms y 8 a 1500 ms, relación **7,5×**, `trace=11,4`, cero eventos perdidos,
  `SLICE_BOARD_DONE PASS`. El shell respondió a `ls` después del banco. En la misma
  imagen Iker comprobó `slice 200` y `slice 1500`: `ps` mostró el quantum solicitado
  en idle, trace-heap, shell y detector; se restauró `slice 10`. C6 queda demostrado
  para rotaciones entre pares, distinguiéndolas de todos los cambios de contexto.
- Se amplió el ancho de nombre en `ps` para separar `trace-heap` de su estado y se
  implementó `stats` con las cinco secciones de la rúbrica. Reutiliza los contadores
  del planificador y las fuentes existentes de tareas, heap y traza. `stats -r` fija
  bases para esta vista sin alterar los contadores usados por otros comandos. Las
  imágenes Xtensa TRACE=1 y TRACE=0 compilaron con `-Werror`.
- Iker validó `stats` en placa: a 200 ms con dos tareas `hog`, 96 rotaciones / 19 s
  (5,05/s), CPU ociosa 0%; a 1500 ms, 18 / 27 s (0,66/s). Tras `kill 4` y `kill 5`,
  `stats` mostró 6 creadas, 4 activas, 2 terminadas y 99% de CPU ociosa; `ps`
  confirmó que no quedaban tareas `hog`. El heap usado siguió en 1628 B porque
  `hog` no reserva heap; la liberación de memoria de una tarea `alloc` aún requiere
  prueba específica. Con `TRANSPORT=0`, el ring de traza lleno explica 100% de eventos
  perdidos en la ventana; se aclaró en la salida de `stats` y en `help stats`.
- El primer `stats` tras flashear llegó entrecortado por el enlace serial. Se añadió
  `uart_flush()` al final de `stats` y `help`. En la siguiente captura Iker obtuvo
  `stats -r` y `stats` completos, así como `help stats`, `help ps` y `help slice`.
  La salida larga de `help` resultó difícil de leer: mezclaba sintaxis y descripción
  de 29 comandos y el terminal partía líneas sin conservar sangría. Se reorganizó
  en cinco categorías con una línea por comando, `help <categoria>` filtra una
  categoría y `help <comando>` muestra uso y efectos con ancho limitado a 76 columnas.
  El índice incluye los 29 comandos registrados; imágenes Xtensa TRACE=1/0 compilan
  con `-Werror`. Iker verificó en placa `help` completo: cinco grupos legibles,
  los 29 comandos presentes y retorno correcto al prompt. Aún falta comprobar la
  presentación de `help <categoria>` y `help <comando>` tras este rediseño.

### 2026-10-21 — E12: consolidación documental y portafolio (en curso)

- Se verificó la auditoría E11: 115 archivos de evidencia versionados, seis
  historias evaluadas y cinco con criterios aplicables cumplidos. S1 sigue
  fallida; la comparación GPIO–CCOUNT no fue posible sin instrumento.
- Se separaron la rúbrica formal R-002 y sus entregables E20–E23 del video de
  portafolio de tres minutos previsto por E12. El formato académico P8 aún no
  está confirmado.
- Se abrió `feature/documentacion-e12`, se seleccionaron fuentes documentales
  para Git y se redactaron README e informe editables. El GIF se genera de un
  UART real de E11 y está rotulado como reproducción, no como sesión en vivo.
- Se generaron una presentación editable de 12 láminas, guion y subtítulos de
  180 s, guía de preguntas y borrador de LinkedIn. Se revisaron visualmente las
  12 láminas y las ocho páginas del DOCX; el verificador local confirmó enlaces,
  cifras, figuras y duración de los subtítulos.
- Pendiente: las acciones humanas de grabación, publicación, dos ensayos y
  simulacro; confirmar el formato académico P8. CP8 permanece abierto.

### 2026-10-21 — cierre experimental de E11
- La hora nominal de HU-04 acabó con 180 450 iteraciones, 7228 chequeos y
  cero alertas; se conservó el UART de 67,6 MB y se releyó en el PC. Los 14
  CRC y 901 huecos del enlace limitan la completitud del flujo recibido.
- Sin analizador ni osciloscopio, HU-05 usó cinco ventanas `switchbench` y
  `perf_counter_ns()` de Windows. Los 12 007 switches completos dieron
  24,087 ± 0,007 µs por CCOUNT y una cota de 4266,3 ± 32,6 µs por switch
  al dividir el tiempo de Windows. La cota incluye tareas y comunicación;
  no permite afirmar una discrepancia <5 %.
- HU-06 generó una tabla maestra de resultados y conservó las cuatro
  gráficas junto a UART/CSV crudos. S1 quedó incumplida aunque la latencia
  fue caracterizada; S2 y S5 se cumplieron. La redacción del informe será E12.
- La temperatura del chip largo no se midió con sensor; un episodio de calor
  al tacto no se usó para inferir carga de CPU. La prueba posterior de reposo
  mostró CPU ocupada ~3 %, y el chip se sintió normal en varias baterías.

> Formato: fecha · horas · qué se hizo · qué se aprendió · qué quedó pendiente

### 2026-10-20 — E11: banco inicial y comparación TRACE ON/OFF
- Iker observó la placa ESP32 muy caliente al tacto después de la batería de
  carga. En la foto `experiments/e11/placa_zona_caliente.jpg` señaló el chip
  largo de muchas patitas junto al LED rojo, probablemente el puente USB-serie
  por su ubicación y el identificador CH340 visto antes; la marca no se lee.
  El conector USB no le pareció caliente.
  La placa se enfrió al desconectarla. No hay medición de temperatura ni
  corriente. Iker confirmó alimentación solo por USB, sin cables externos en
  los pines ni conexiones debajo a la protoboard. No observa daños visibles ni
  olor a quemado en la cara inferior; no dispone de multímetro ni puede enviar
  foto del reverso. Tras pausar las cargas, se hizo una lectura breve en reposo
  con el monitor: 10 muestras en COM8, CPU ocupada estimada 3 %, heap libre
  96676 B, cero pérdidas, CRC o huecos. UART y CSV coinciden; el chip largo
  siguió a temperatura normal según Iker, sin termómetro. La relación con la carga del SO es una
  hipótesis. El monitor de CPU, heap y telemetría pasó una reproducción UART
  local de 10 muestras históricas; esos indicadores no miden temperatura.
- El piloto `inject leak` de 10 s guardó diez muestras y UART crudo. El heap
  libre bajó de 96676 a 91076 B y volvió a 96676 B; los bloques regresaron a
  19. El UART reprodujo las diez muestras y confirmó un CRC erróneo y una
  trama ausente dentro de la ventana. Se conserva como piloto separado de las
  cinco repeticiones exigidas. Iker informó que el chip largo siguió normal.
- El piloto `inject frag` de 10 s guardó diez muestras y UART crudo. La placa
  reportó 49936 B libres, pero mayor hueco de 3796 B; el PC calculó 92,4 %
  de fragmentación según la métrica del banco. Tras parar, regresó a 96676 B
  y 19 bloques. Replay sin CRC ni huecos. Iker no comprobó temperatura en
  este piloto. Sus datos no se mezclaron con la batería completa posterior.
- La primera batería `memory.py --all-scenarios` se interrumpió por Ctrl+C
  durante `leak 4/5`: tres corridas `leak` completas y una captura parcial
  de seis muestras se conservan; no se atribuye a un fallo de placa. La
  segunda batería, sesión UTC `20261021T011525Z`, completó 5 `leak` + 5
  `frag` de 20 s. Los diez UART reproducen sus CSV, sin CRC ni huecos, y
  cada corrida volvió a 96676 B/19 bloques. El análisis y ambas gráficas
  reproducibles están en `experiments/e11/`; la sesión interrumpida no se
  mezcló con las cinco repeticiones de la sesión completa.
- La batería `--all-profiles --flash` compiló/grabó una sola imagen TRACE ON y
  completó 15 corridas sin intervención: cinco por perfil `light`, `nominal`
  y `heavy`, 10 s cada una. Los 15 CSV y 15 UART crudos concuerdan con
  `resultados.csv`; todos los contadores de muestras crecieron. La ocupación
  UART observada subió de 11,20 ± 0,20 % (`light`) a 20,53 ± 0,20 %
  (`nominal`) y 44,09 ± 0,49 % (`heavy`). La placa reportó cero descartes;
  Windows observó 1 CRC/3 huecos en `nominal` y 10 CRC/16 huecos en `heavy`.
  Esta diferencia distingue el búfer de la placa del enlace al PC y deja
  pendiente la caracterización de la causa de los errores.
- La extensión del banco ejecutó cinco corridas `light` de 10 s: 106
  iteraciones en cada una, cero descartes, CRC y huecos. La primera prueba
  `heavy` falló antes de la carga: la placa tenía una imagen de shell de texto
  y el puente esperaba tramas binarias. Se guardaron el diagnóstico UART y dos
  CSV vacíos, sin tratarlos como mediciones. Tras flashear `TRACE=1
  TRANSPORT=1`, las cinco corridas `heavy` completaron 1630,4 ± 3,6
  iteraciones. La placa reportó cero descartes, pero el puente observó 25
  errores CRC y 40 tramas ausentes; las cinco filas se etiquetaron
  `PASS_LINK_ERRORS`. El siguiente capturador guarda también bytes UART de cada
  ventana para calcular ocupación del canal sin reconstruir datos no medidos.
- En placa, cinco ventanas nominales de 10 s con TRACE ON produjeron CSV de
  telemetría y cero eventos perdidos. El primer piloto incluyó muestras después
  de detener la carga, cuando `switch_count` se reinicia; se conservaron los
  crudos, se marcaron sus filas inválidas y se corrigió la ventana del banco.
- En la carga `overhead-standard`, se capturaron cinco corridas de 60 s por
  variante. OFF completó 25 621 operaciones por corrida y ON 25 621,8 de
  media; no hubo pérdidas ni errores. El overhead observado fue 2,303 ± 0,001
  puntos porcentuales de CPU. El switch observado fue 21,021 µs OFF y
  26,002 µs ON, por encima de la meta de 10 µs. Las capturas completas,
  tabla y gráfica están versionadas en `experiments/e11/`.
- El banco `run.py --flash --busid 1-5` ejecutó en una sola orden la
  compilación y grabación WSL, devolución del CH340 a COM8 y cinco corridas
  nominales de 10 s en Windows. Las cinco pasaron sin pérdidas, CRC ni huecos.
  Esto verifica la automatización de un escenario, no la batería completa.
- Estos resultados no se extrapolan al comando `inject load nominal`, que es
  otro perfil. E11 sigue en 0/6 historias completas. No hay instrumento GPIO;
  HU-E11-05 usará el método alternativo y explicará su limitación.
- Para HU-E11-03 se preparó un banco que invoca directamente el mismo camino
  de `inject load nominal` en TRACE=0 y TRACE=1, sin `TRANSPORT` en ninguna
  variante y con la misma sonda de switch compilada en ambas. Compilaron las
  dos imágenes; falta medir 5 × 60 s por variante. El piloto queda separado
  de las corridas formales.
- Piloto TRACE=1 del nuevo banco en placa: 10 s, 502 iteraciones (50,2/s),
  47 111 eventos emitidos, cero descartes y cero fallos de reserva. Calculado
  en PC desde los contadores: CPU ocupada 10,256 %, switch completo observado
  27,588 µs. `inject stop load` limpió el escenario. UART/CSV crudos en
  `experiments/e11/`. El piloto TRACE=0 también pasó: 502 iteraciones,
  cero errores, 8,978 % CPU ocupada y 22,641 µs por switch observado.
  Diferencia piloto ON-OFF: +1,278 puntos porcentuales de CPU y +4,947 µs.
  Son resultados de una sola corrida por variante, sin incertidumbre; no
  entran en la comparación formal.
- Batería formal HU-E11-03: cinco ventanas de 60 s por variante con el mismo
  `inject load nominal` y el mismo commit `5568566`. Las diez corridas pasaron,
  sin fallos de reserva ni eventos descartados. La CPU ocupada ON−OFF fue
  +1,285 ± 0,009 puntos porcentuales; throughput sin diferencia detectable;
  el switch completo aumentó +4,9466 ± 0,0003 µs. La comparación, σ de las
  duraciones individuales, tamaños y cálculo integral de ciclos/evento están
  en `experiments/e11/nominal_overhead_method.md`. Los UART/CSV formales aún
  se versionaron en `7142029`; HU-E11-03 cerrada.
- Para HU-E11-04 se reutilizaron los UART y CSV de la batería E11 de cinco
  corridas por perfil `nominal`/`heavy`: nominal emitió 49 314 eventos sin
  descartes de placa y ocupó 20,53 ± 0,20 % del baud; heavy emitió 108 810
  sin descartes de placa y ocupó 44,09 ± 0,49 %. El enlace PC observó
  respectivamente 1 CRC/3 huecos y 10 CRC/16 huecos. La saturación interna
  de la placa no se observó; el detalle reproducible está en
  `experiments/e11/telemetry_method.md`. La hora sin falsos positivos de E10
  sigue como evidencia histórica, pero no produjo UART crudo E11.

### 2026-10-20 — E10 integrado por PR #2; cierre de CP7 pendiente
- El merge `04703e9` de `feature/deteccion` está en `origin/main` e incluye el
  último commit E10 `636b447`. Su árbol de archivos es idéntico al de la rama
  de trabajo. Se descargaron referencias y etiquetas; `CP7` aún no aparece.
- Las seis historias de E10 están verificadas en la placa. La última prueba de
  `health` y `deadlock` mostró siete categorías correctas, el ciclo real de dos
  tareas y restauración de grafo, heap y sincronización tras `inject stop`.
- Regresión local después del merge: `make test` completo en WSL pasó,
  incluidos 13 grafos sintéticos, el anillo de alertas, protocolo, puente y
  arneses E10. No se repitió una prueba de placa después del merge porque el
  árbol del firmware es idéntico al que ya se validó en hardware.
- Próximo paso de cierre: crear y publicar el tag `CP7` sobre el merge de
  `main`, y registrar la demostración al profesor. Los documentos de `docs/`
  están ignorados por Git y por ahora solo se actualizan localmente.

### 2026-10-17 — E07: coste ON/OFF medido en placa
- Con `TRACE=1` y `TRACE=0` se ejecutaron cinco corridas reproducibles de 60 s
  por variante, cada una con cinco tareas de carga, heap y mutex. El trabajo
  completado fue igual: 25 621,8 operaciones por corrida de media.
- Con la sonda final del vector, CPU ociosa: OFF 93,898 %; ON 91,626 %.
  La diferencia fue **2,272 puntos porcentuales de CPU**, inferior a la meta
  del 5 % en esta carga. El trabajo completado vario +0,00 %.
- La variante ON emitio ~7 279 eventos/s, sin perdidas ni errores. Emision
  aislada: 48 ciclos/evento. RAM estatica adicional: 33 320 B; imagen
  experimental `.bin`: +3 088 B. Metodo y dispersion en
  `tests/E07-overhead.md`.
- HU-E07-03: en la placa, 306 parejas completas de cambio de contexto, 0
  errores y 0 pérdidas. CCOUNT desde el vector hasta casi el retorno: mínimo
  481, media 861 y máximo 1512 ciclos. La carga estándar final midió OFF
  816,860 y ON 1 018,250 ciclos de media. A 40 MHz son 20,42 y 25,46 µs:
  la meta de 10 µs no se alcanzó. `make test` pasó (58 asserts de heap,
  92 de VFS, 1M de eventos y filtros). Las imágenes normales TRACE ON y OFF
  compilaron y dieron 67/67 pruebas del shell en placa. Hubo fallos
  intermitentes de lectura UART en la primera pasada de cada variante; la
  repetición pasó sin cambiar el kernel. CP5 validado y etiquetado.

### 2026-09-20 — Planificación (0 h de código)
- Definida la arquitectura completa, 19 épicas, 148 funcionalidades.
- **Descubrimiento crítico:** el profesor exige comandos tipo Linux (`ls`, `cd`, `pwd`).
  Se añadieron E05 (VFS) y E06 (Shell) al MVP, que no estaban contempladas.
- Confirmado el hardware: ESP32-WROOM-32, Xtensa LX6, doble núcleo → AMP viable, pero
  con *window spill* obligatorio.
- 11 ADRs registradas.
- **Pendiente:** confirmar fecha de entrega y si el proyecto es individual o grupal.

### 2026-09-20 — E01: toolchain, arranque y primer flasheo real (~9 h)
- **HU-E01-01:** instalado `xtensa-esp32-elf-gcc` v13.2.0 y `esptool.py` v4.12.0 vía
  instalador de ESP-IDF (solo target `esp32`, sin usar el framework). `Makefile` con
  `-Wall -Wextra -Werror -ffreestanding -fno-builtin -nostdlib`, compila limpio.
- **HU-E01-02:** `linker.ld` con regiones IRAM/DRAM y símbolos `_bss_start`, `_bss_end`,
  `_heap_start`, `_heap_end`, `_stack_top` exportados y verificados con `nm`. Documentado
  el mapa de memoria real en `01-ARQUITECTURA.md` §3.1.
- **HU-E01-03:** `start.S` inicializa la pila y limpia `.bss` a ceros antes de saltar a
  `kmain()`.
- **HU-E01-04 ⭐ (la de mayor riesgo, superada):** primer flasheo real en hardware
  (ESP32-D0WD-V3). Imagen puesta en `0x1000` vía `esptool.py elf2image` +
  `write_flash` — el ROM la trata como si fuera el second-stage bootloader, sin
  ESP-IDF/FreeRTOS de por medio (confirmado con `nm | grep -i freertos` → vacío).
  LED en GPIO2 parpadeando a ~1 Hz, controlado por driver propio (`kernel/drivers/gpio.c`).
- Entorno de trabajo resuelto: WSL2 Ubuntu + `usbipd-win` para pasar la ESP32 (USB-serial
  CH340) desde Windows a WSL.

### 2026-09-20 — HU-E01-05: UART + `kprintf` — CP0 alcanzado ⭐ (~4 h, dos bugs reales)
- Driver UART (`kernel/drivers/uart.c`) con `uart_init/putc/puts/getc`, y `kprintf`
  (`kernel/lib/kprintf.c`) desde cero con `%d %u %x %s %c %p`, sin libc.
- Compilado con `-mabi=call0` (ABI de llamadas plana, sin ventanas de registro) para
  evitar el riesgo de desbordamiento de ventana Xtensa (R1) en cadenas de llamadas
  profundas — decisión a revisar en E02 cuando el scheduler necesite el contexto completo.
- **Bug real #1 — reloj APB mal asumido:** se calculó el divisor de baudrate asumiendo
  80 MHz (el valor típico post-inicialización de ESP-IDF). En bare-metal puro, sin esa
  inicialización, el chip corre a 40 MHz (el cristal, sin multiplicar). Cada byte
  transmitido salía corrupto/partido en el receptor. Diagnosticado capturando bytes
  crudos por Python (`pyserial`) en vez de confiar en cómo una terminal dibuja caracteres
  raros — así se confirmó que era un problema de framing, no de cable/USB/WSL.
- **Bug real #2 — `.rodata` en la región de memoria equivocada:** IRAM en ESP32 solo
  permite accesos de 32 bits alineados. `kprintf` lee el string byte a byte (`l8ui`), y
  el `linker.ld` ponía `.rodata` en IRAM junto con `.text` → excepción real del chip
  (`LoadStoreError`) apenas se intentaba imprimir el banner. Fix: `.rodata` movido a DRAM.
- **Método clave:** ante corrupción "aleatoria" que no cedía a cambios de cable/puerto/
  USB, la prueba decisiva fue reducir el código a la mínima expresión posible (un solo
  `uart_putc('A')`, sin bucles) para aislar la causa por bisección, en vez de seguir
  cambiando cosas a ciegas sobre el código completo.

### 2026-09-20 — HU-E01-06: `CCOUNT` y frecuencia real de CPU (~1 h)
- `cpu_cycles()` (lee `CCOUNT` vía `rsr`) y `cycles_to_us()` en `kernel/arch/xtensa/cpu.h`.
- **Frecuencia de CPU medida en hardware, no asumida:** se cronometró la transmisión de
  10 bytes por UART (duración conocida por el baudrate) contra los ciclos de `CCOUNT`
  transcurridos → **40 MHz real** (no 80/160/240 MHz). Registrado en **ADR-012**.
  Corrige el wrap de `CCOUNT` de ~26,8s (estimado antes de medir) a **~107,4s reales**.
- Bucle vacío de 1000 iteraciones: 19011 ciclos, idéntico en 3 arranques → repetible y
  plausible (~19 ciclos/iteración, coherente con un contador `volatile`).

### 2026-09-22 — HU-E01-07: vector de interrupciones (~3 h, incluye un "bug fantasma")
- Tabla de vectores propia (`kernel/arch/xtensa/vectors.S`) instalada vía `VECBASE`.
  Solo se implementan `_UserExceptionVector` (offset `0x340`, nivel 1 — ahí llegan tanto
  excepciones reales como interrupciones de nivel 1) de verdad; el resto de las entradas
  (ventanas de registro, niveles 2-6, NMI, kernel, doble excepción) son un `j .` de
  seguridad, ya que `-mabi=call0` nunca las dispara.
- `irq_disable()`/`irq_restore()` en `cpu.h`, usando `rsil`/`wsr ps` — anidables porque
  `irq_restore` recibe el `PS` completo anterior, no un simple "encender/apagar".
- Interrupción de prueba: el timer interno de Xtensa (`CCOMPARE0`, interrupt bit 6, nivel 1
  de fábrica) — se arma desde C, se atiende en `irq_dispatch()`, y el ISR guarda/restaura
  los 16 registros en la pila antes y después de llamar al dispatcher en C.
- Direcciones de registros especiales (`EPC_1=177`, `EXCSAVE_1=209`) sacadas del header
  real `xtensa/config/specreg.h` de Espressif — el ensamblador GNU no los reconoce por
  nombre fuera de archivos `.S` preprocesados.
- **"Bug" que consumió media sesión y no era real:** el kernel parecía reiniciarse en
  bucle después de manejar la interrupción. Tras aislar el problema por bisección
  (deshabilitar la interrupción, quitar la tabla de vectores, medir tiempo), la causa
  resultó ser que el botón físico **EN** se presionó varias veces seguidas durante las
  pruebas — cada presión generaba un arranque limpio nuevo, que en la terminal se veía
  igual a un reinicio automático. **Lección:** antes de asumir que el sistema falla,
  verificar que el método de prueba es el que se cree que es.

### 2026-09-22 — HU-E01-08 y HU-E01-09: timer periódico y excepciones — E01 COMPLETA ⭐
- **HU-08:** `timer_init(hz)` reutiliza `CCOMPARE0` de HU-07, pero se re-arma solo en cada
  tick (sumando al `CCOMPARE0` anterior, no a `CCOUNT` actual, para no acumular desfase).
  Medido: 1000 Hz durante 10s reales → `g_ticks = 10000` exacto.
- **HU-09:** `panic(msg)` genérico + `panic_unexpected_exception()` mejorado, imprimiendo
  `EXCCAUSE`, `EPC1` (PC), `EXCVADDR` (dirección que falló) y `PS`. Probado con una
  escritura real a `NULL`: `EXCCAUSE=29, EXCVADDR=0x0`, sistema detenido limpio, sin
  reinicio. Direcciones de registros (`EXCVADDR=238`) sacadas del header real de Espressif.
- **E01 — FUNDACIÓN BARE-METAL: 9/9 HUs completas, ~18h invertidas.** CP0 alcanzado.
  Lista para empezar E02 (scheduler).


### 2026-10-12 — Cierre de E02 y E03 (CP1, CP2, CP3)
- **E03 completa (8/8):** heap con first-fit, split, coalescencia, canario, contabilidad por tarea, métricas y `heap_dump`. `make test`: 51 asserts. En placa: 37/37 PASS.
- **Medir antes de declarar:** `heap_stats` marcó "2 µs" con 1 bloque, pero con 793 bloques una pasada tardaba ~1,3 ms con interrupciones apagadas. Rediseñado por tandas con detección de cambios (ADR-016): tanda ≈ 51 µs.
- **E02 completa (11/11):** canario verificado en cada cambio, `sleep` (999 ms en 1000), macros `PROBE_*`. **Soak 5 min y 1 h: PASS**, `errores=0`, 35 999 tareas efímeras, heap restaurado.
- **Texto perdido en el serial:** instrumento propio (`TXBYTES`) → no es el tick ni el FIFO; baja con la velocidad (115200: 1–3%; 57600: 0,36%). Velocidad por defecto del proyecto: **57600**.
- **Pendiente:** E04 → E05 → E06 (CP4). Decisión de DRAM antes de E07.


### 2026-10-12/13 (noche) — E04, E05 y E06 (CP4)
- **E04:** mutex FIFO con traspaso de propiedad, semáforos (`sem_post` desde el tick), timeouts, grafo de espera (`sync_dump`), `task_kill`. Carrera sin mutex: 64 566 de 180 000; con mutex: exacto.
- **E05:** VFS + RAMFS + `/proc`, portable al PC (92 asserts). Árbol de demostración: 1032 B. **La DRAM se agotó** al ampliar el TCB (overflow de 2880 B) → la arena del heap pasa a SRAM1 (ADR-017), verificada con prueba de patrones.
- **E06:** shell como tarea (sem_wait, sin busy-wait), 19 comandos, historial, flechas. **67/67 pruebas automáticas** conversando con la placa (`tools/shell_test.py`), 200 comandos sin fugas, `reboot`.
- Herramientas nuevas: `tools/run_board.sh`, `tools/capture.py` (detecta el puerto: usbipd puede renumerar), `tools/shell_test.py`.
- **Revisión de código independiente** del diff: 2 bugs reales y 6 riesgos, todos verificados y corregidos con prueba (commit `06189ab`): el árbol del VFS quedaba atribuido a la tarea creadora (`kill` lo liberaba), un mutex podía concederse sin registrarse, carrera de `task_kill` con la reutilización del tid, VFS no atómico entre búsqueda y referencia, `seek` con `INT32_MIN`, lectura partida de `cpu_cycles` en `ps`.
- **Verificación final (placa):** kernel 103/103 · shell 67/67 · **uptime del shell 1 h 1 min 41 s** (1673 comandos, 0 sin respuesta, heap estable) · **soak final del kernel 1 h**: `errores=0`, 35 999 tareas efímeras, 0 tareas sin avance, heap restaurado · PC: 58 + 92 asserts.
- **Entrega:** rama `feature/E04-sincronizacion-E05-vfs-E06-shell` (sin push); guía de repaso en `docs/guias/RESUMEN-NOCHE.md`.

---

## 6. Bloqueantes activos

| # | Bloqueante | Desde | Impacto | Acción |
|---|---|---|---|---|
| — | Ninguno | — | — | — |

---

## 7. Aprendizajes y sorpresas

### 2026-10-21 — E12: rúbrica y verificación visual

- La rúbrica formal R-002 ya estaba documentada en `09-RUBRICA-PROFESOR.md`:
  pide un video de hasta 10 min con nueve escenas. El video de 3 min de E12
  es de portafolio y no demuestra esos criterios por sí solo.
- El renderizador DOCX empaquetado no encontró LibreOffice en este equipo.
  Word instalado exportó la copia editable a PDF local para revisar sus ocho
  páginas; se corrigieron un borde heredado en el título y la numeración de
  la bibliografía. El PDF y PNG de revisión quedan fuera de Git.
- Una lámina puede pasar validación estructural sin garantizar legibilidad;
  se inspeccionaron visualmente las 12 renderizaciones de la presentación y
  las dos gráficas principales a tamaño completo.

> Registrar aquí todo lo que se descubra que contradiga el plan. **Esto alimenta el informe
> final y la sección de "lecciones aprendidas" de la sustentación.**

| Fecha | Aprendizaje |
|---|---|
| 2026-09-20 | La SRAM interna del ESP32 se accede sin caché de datos → el problema de coherencia entre núcleos no aplica. Solo hace falta ordenamiento (barreras). |
| 2026-09-20 | `CCOUNT` es por núcleo y hace wrap cada ~26,8 s a 160 MHz. Ambos hechos tienen consecuencias de diseño reales. |
| 2026-09-20 | El ROM bootloader deja **tres watchdogs activos** (RTC, Timer Group 0, Timer Group 1). Si no se desactivan explícitamente en `start.S`, el chip reinicia en bucle apenas expiran — confirmado en hardware real (`RTCWDT_RTC_RESET` primero, luego `TG0WDT_SYS_RESET` al arreglar el primero). Riesgo R2.2 no era teórico. |
| 2026-09-20 | En Xtensa, escribir assembly a mano requiere `.literal_position` explícito y que el `linker.ld` agrupe `*(.literal)` antes de `*(.text)` — si no, `l32r` falla con "literal placed after use". No es un problema al escribir C (el compilador lo maneja solo), solo al escribir `.S` manualmente. |
| 2026-09-20 | El IO_MUX de la ESP32 no tiene una fórmula lineal pin→registro (offsets por tabla, no calculables). Se resolvió consultando directamente los headers de `esp-idf` en vez de adivinar direcciones — evitó horas de debugging por un valor mal recordado. |
| 2026-09-20 | **El reloj APB de 80 MHz "de manual" NO existe en bare-metal puro sin inicialización de ESP-IDF.** Sin esa configuración, el chip corre al reloj del cristal (40 MHz). Cualquier cálculo de timing (baudrate, delays) que asuma 80 MHz está mal en este proyecto hasta que se implemente configuración de reloj propia. Afecta directamente a E07 (mediciones) si no se corrige el supuesto en todos lados. |
| 2026-10-13 | **Una prueba que no puede fallar no prueba nada.** La prueba de la carrera de E04 dio 900 000 de 900 000 con `g_shared++` (ventana de 2 instrucciones y tick periódico determinista: la interrupción nunca caía dentro). Con trabajo real entre leer y escribir, 64 566 de 180 000. |
| 2026-10-13 | **Un kernel sin libc debe proveer `memcpy`/`memset`.** GCC los emite solo (copias de estructuras, bucles de relleno) aunque se compile con `-ffreestanding`; sin ellos, falla el enlace. Hay que desactivar la transformación de bucles dentro de ellos. |
| 2026-10-13 | **usbipd renumera el puerto.** Tras reiniciarse el USB, `/dev/ttyUSB0` queda obsoleto y el dispositivo reaparece como `ttyUSB1`; abrir el viejo cuelga sin error. Las herramientas eligen el puerto más reciente. |
| 2026-10-13 | **El shell debe bloquearse, no sondear.** El tick lee el UART y hace `sem_post`; el shell queda `BLOCKED` y `ps` lo demuestra. Los ganchos del tick tuvieron que ser composables (el test de E04 borraba el del shell). |
| 2026-10-12 | **Presupuesto de DRAM casi agotado.** `bss` = 169 280 B de 180 224 B: heap 96 KB + 16 pilas × 4 KB = 64 KB + el resto. Quedan ≈ 10,7 KB, que además comparten la pila de `kmain`/idle. **E07 (ring buffer 32 KB) no cabe.** Opciones (a verificar contra el TRM): ampliar la región DRAM en `linker.ld` hasta `0x3FFFFFFF` (SRAM1, ≈ +144 KB); usar el IRAM sobrante (≈ 115 KB) para datos de palabra como el ring; reducir `MAX_TASKS`/pilas; eventos de 8 B. Decidir en ADR-013. |
| 2026-10-12 | **Un índice puede mentir.** `frag=1%` con 8 bloques parecía un fallo del índice: el hueco final de 96 KB domina `mayor_libre/libre`. Con el heap lleno y bloques alternos liberados llega a 100%. Hay que provocar la patología para ver la métrica. |
| 2026-10-12 | **Medir con un instrumento propio.** El texto "perdido" se diagnosticó con un contador de bytes enviados (`TXBYTES`) contra los recibidos, no mirando la pantalla. Cuatro hipótesis descartadas por experimento (display, tick, FIFO, kernel) en lugar de por intuición. |
| 2026-10-12 | **`kmalloc` es O(n) bajo candado.** Con ~790 bloques apaga interrupciones ≈ 200 µs. Aceptado en el MVP; lista de libres en E18. |
| 2026-10-12 | **Un soak debe verificar propiedades, no solo "no colgarse".** Cálculo con `SAR`/`LCOUNT` repetido bajo interrupciones y heap con patrones por dirección: un solo registro mal restaurado o un solape de bloques produce un error contable. |
| 2026-09-20 | **IRAM en ESP32 no admite accesos de 8/16 bits, solo de 32 bits alineados.** Cualquier dato leído byte a byte (strings, buffers de bytes) debe vivir en DRAM, nunca en IRAM, aunque sea `const`/`.rodata`. El propio hardware lo hace cumplir con una excepción real (`LoadStoreError`), no es un límite solo documental. |

---

## 8. Cómo actualizar este documento

Al terminar cada sesión:

1. Añadir entrada en **§5 Bitácora** (fecha, horas, qué se hizo, qué se aprendió).
2. Actualizar **§3** (HUs completadas, horas).
3. Si se alcanzó un checkpoint, marcarlo en **§2**.
4. Si se midió algo, registrarlo en **§4**.
5. Si hubo una sorpresa técnica, anotarla en **§7**.
6. Si algo quedó bloqueado, registrarlo en **§6**.

> **Este documento es lo que permite que el proyecto sobreviva a un mes de pausa.**
