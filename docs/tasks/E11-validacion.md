# E11 — Banco de Pruebas y Validación Experimental

| Campo | Valor |
|---|---|
| **Estado** | 🟡 Campaña ejecutada y auditada: 6/6 historias evaluadas, 5/6 cumplen sus criterios aplicables; HU-02 no cumple S1. Meta GPIO <5 % no comprobable sin instrumento. |
| **Prioridad** | 🔴 MVP — **NO CORTAR NUNCA** |
| **Estimación** | 6 h |
| **Depende de** | E07, E09, E10 |
| **Habilita** | E12 |
| **Checkpoint** | contribuye a CP8 |
| **Riesgo** | 🟢 Bajo |

---

## 1. Objetivo

Convertir el proyecto de **una demo que funciona** en **un experimento con resultados
numéricos**.

---

## 2. Por qué existe

> ⚠️ **ESTA ES LA ÉPICA QUE MÁS SUBE LA NOTA POR HORA INVERTIDA, Y LA QUE TODO EL MUNDO
> OMITE.**

La tentación al llegar aquí será enorme: *"el sistema ya funciona, la demo se ve bien,
paso directo a la documentación"*. **Es el error más caro del proyecto.**

### La diferencia que produce E11

| Sin E11 | Con E11 |
|---|---|
| "Mi cambio de contexto es rápido" | "Media 4,2 µs, máximo 6,1 µs, σ=0,8 µs sobre 50.000 muestras" |
| "La instrumentación tiene poco overhead" | "2,3% de CPU, 18 ciclos por evento, +1.847 bytes de binario" |
| "El detector funciona" | "Detecta en 340 ms de media sobre 20 inyecciones, 0 falsos positivos en 1 h" |
| Una demostración | **Un resultado** |
| Un estudiante | **Un ingeniero** |

Y lo más importante: **es lo que hace verdadera la tesis del proyecto.** Sentinel afirma que
mide el costo de observar. Si no hay medición, la afirmación es marketing.

---

## 3. Alcance

### Incluye
- Banco de pruebas automatizado (scripts que corren escenarios y recogen datos)
- Medición de latencia de cambio de contexto
- **Medición del overhead de instrumentación (TRACE ON vs OFF)**
- Medición de rendimiento del detector
- Validación cruzada con GPIO + analizador lógico
- Análisis de error y repetibilidad
- Tabla de resultados final + gráficas

### NO incluye
- ❌ El informe escrito → **E12** (aquí se generan los datos, allí se redactan)
- ❌ Comparación entre políticas de planificación → **E18**
- ❌ El experimento core 0 vs core 1 → **E13**

---

## 4. Historias de Usuario

### HU-E11-01 — Banco de pruebas automatizado
**Como** experimentador, **quiero** ejecutar experimentos sin intervención manual **para**
que sean repetibles y comparables.

**Criterios de aceptación:**
- [x] `tools/testbench/run.py` ejecuta una batería completa de perfiles de carga
- [x] Para cada experimento: flashea la variante correcta, arranca el puente, dispara la
      carga, recoge datos durante T segundos, exporta CSV
- [x] Cada experimento se repite **≥ 5 veces** automáticamente
- [x] Los resultados se etiquetan con: variante de build, perfil de carga, fecha, commit hash
- [x] Genera un `resultados.csv` consolidado
- [x] Un experimento completo corre sin intervención humana

Verificado en placa con `--all-profiles --flash --busid 1-5`: 5 corridas por
`light`, `nominal` y `heavy`, CSV y UART versionados. `TRACE=0` usa
`overhead.py` porque el transporte binario de `run.py` requiere `TRACE=1`;
ambas variantes del experimento de overhead se flashearon y midieron en
esa batería específica. Los errores de enlace de cuatro ventanas se
conservaron y etiquetaron.

**Estimación:** 1,5 h

> **La repetición ≥5 veces no es un detalle:** sin ella, no puedes reportar desviación
> estándar, y sin desviación estándar un número aislado no significa nada.

---

### HU-E11-02 — Latencia de cambio de contexto ⭐
**Como** autor, **quiero** caracterizar la métrica estrella del proyecto **para** poder
reportarla con rigor.

**Criterios de aceptación:**
- [x] Se recogen ≥ 50.000 muestras de duración de switch bajo carga `nominal`
- [x] Se calculan: mínimo, máximo, media, mediana, σ, p95, p99
- [x] Se genera un **histograma** de la distribución
- [x] Se desglosa por motivo: quantum agotado / bloqueo / yield voluntario
- [ ] **Meta S1 <10 µs incumplida:** media nominal observada 22,355 ±
      0,223 µs, frente a < 10 µs
- [x] Se analizan y explican los valores atípicos, sin descartarlos
- [x] Resultado registrado en `05-ESTADO.md` §4

Las 57 940 muestras nominales corresponden a `block` y `preempt`; el
escenario nominal no produce `quantum` ni `yield` en esas ventanas. Una
batería complementaria `switchbench` de cinco corridas sí obtuvo 571
`quantum` y 799 `yield`, sin mezclarlas con la media nominal. Los 301
valores ≥80 µs coinciden con entradas periódicas al detector. La medición
incluye la sonda y no permite atribuir el coste exacto de cada instrucción.
La caracterización experimental está verificada; HU-02 no puede declararse
cerrada contra todos sus criterios porque S1 **no** está alcanzada.

**Estimación:** 1 h

> 💡 **El histograma es más interesante que la media.** Si es bimodal, hay dos caminos de
> código distintos y eso es un hallazgo que merece explicación. **Los datos que sorprenden
> son los que más valen en un informe.**

---

### HU-E11-03 — Overhead de instrumentación ⭐⭐
**Como** autor, **quiero** cuantificar el costo de observar **para** presentar el resultado
central de la tesis.

**Criterios de aceptación:**
- [x] Experimento con **dos variantes**: `TRACE=0` y `TRACE=1`, misma carga `nominal`,
      mismo tiempo, 5 repeticiones cada una
- [x] Se comparan: latencia de switch (media/σ), % CPU idle, throughput de la carga
- [x] **Se calcula y reporta:**
      - ciclos por evento de traza
      - % de CPU consumido por la instrumentación
      - bytes de RAM ocupados
      - delta de tamaño del binario
- [x] Se reporta la **incertidumbre** (desviación entre repeticiones)
- [x] ✅ **Meta: overhead < 5% de CPU** (criterio de éxito S2)
- [x] Gráfica comparativa generada

Verificado en placa con 5 × 60 s por variante y crudos versionados en
`experiments/e11/` (`7142029`). Resultado nominal: +1,285 ± 0,009 puntos
porcentuales de CPU; la meta S2 se cumple. El switch observado aumentó
+4,9466 µs; la meta S1 de HU-E11-02 sigue sin cumplirse.

**Estimación:** 1 h

> 🎯 **ESTE ES EL RESULTADO CENTRAL DEL PROYECTO.**
>
> *"Toda herramienta de observación perturba lo que observa. La mayoría de proyectos ignoran
> ese costo. Yo lo medí: mi instrumentación cuesta N ciclos por evento y X% de CPU."*
>
> Si solo tuvieras tiempo para una HU de toda E11, sería esta.

---

### HU-E11-04 — Rendimiento del detector y de la telemetría
**Como** autor, **quiero** caracterizar el diagnóstico **para** respaldar las afirmaciones
de E10.

**Criterios de aceptación — detector:**
- [x] 20 inyecciones de deadlock → se mide el tiempo hasta la detección en cada una
- [x] Se reporta: media, máximo, tasa de detección (100% observado)
- [x] 1 hora de carga `nominal` sin inyección → cero falsos positivos observados
- [x] Se mide el coste del detector en µs por ejecución

**Criterios de aceptación — telemetría:**
- [x] Bajo carga `nominal`: eventos emitidos, perdidos, % perdidos
- [x] Bajo carga `heavy`: los mismos datos; no se observó saturación de placa
- [x] Se reporta la utilización del canal UART (% del ancho de banda teórico)
- [x] ✅ **Meta: < 1% perdidos bajo carga nominal** (criterio de éxito S5)

En 20 inyecciones: 20/20 detectadas, demora 320,862 ± 158,026 ms, máximo
544,301 ms. El último coste consultado del detector fue 51,144 ± 0,229 µs,
sin atribuirlo a la ejecución exacta que generó cada alerta. La hora
nominal `20261021T043250Z` produjo 7.228 chequeos y cero alertas. El
enlace Windows tuvo 14 CRC y 901 huecos, preservados y documentados; los
contadores internos sostienen la comprobación pese a esa pérdida parcial.
Telemetría nominal: 49.314 eventos emitidos, cero descartados en cinco
ventanas; heavy: 108.810 emitidos, cero descartados. Heavy no saturó la
cola del dispositivo, aunque el enlace PC perdió más tramas. UART nominal
20,53 ± 0,20 % y heavy 44,09 ± 0,49 % del baud teórico. Los UART/CSV
crudos y resúmenes están en `experiments/e11/`.

**Estimación:** 1 h

> **Documentar la saturación bajo carga `heavy` no es admitir un fallo: es caracterizar un
> límite.** "Mi sistema satura a partir de 5.400 eventos/s, y aquí está el cálculo de por
> qué" es mejor que fingir que no hay límite.

---

### HU-E11-05 — Validación cruzada con hardware
**Como** científico, **quiero** verificar mis mediciones con un método independiente
**para** poder afirmar que son correctas.

**Criterios de aceptación:**
- [ ] Un GPIO se pone en alto al entrar al cambio de contexto y en bajo al salir
- [ ] El pulso se mide con analizador lógico u osciloscopio
- [ ] Se compara la medición por hardware con la de `CCOUNT`
- [ ] **La discrepancia es < 5%** y está explicada (el overhead del propio GPIO)
- [ ] Captura de pantalla del instrumento incluida como evidencia
- [x] Si no hay instrumento disponible: método alternativo (contar N switches y medir el
      tiempo total con un cronómetro externo) y **documentar la limitación honestamente**

Iker confirmó que no dispone de analizador lógico ni osciloscopio. La batería
alternativa `20261021T055311Z` obtuvo 12 007 switches completos en cinco
ventanas: CCOUNT 24,087 ± 0,007 µs por switch y cociente del cronómetro de
Windows 4266,3 ± 32,6 µs por switch, media ± desviación entre corridas. El
cronómetro incluye la ejecución de tareas y comunicación, por lo que **no**
es una medida aislada del switch y **no** comprueba una discrepancia <5 %.
Tampoco existe captura de pantalla de un instrumento. Método, UART y CSV en
`experiments/e11/external_clock_method.md` y `external_clock_*`.

**Estimación:** 1 h

> 🔬 **Esta HU es lo que responde la pregunta "¿cómo sabes que tus mediciones son
> correctas?"**
>
> Validación cruzada por dos métodos independientes es el estándar experimental. Si tu
> contador de ciclos y un instrumento externo coinciden, tienes evidencia. Si solo tienes
> uno, tienes una afirmación.
>
> ❓ **Pregunta abierta Q7:** ¿hay analizador lógico disponible en el laboratorio? Si sí,
> esta HU es fácil. Si no, usar el método alternativo y decirlo.

---

### HU-E11-06 — Tabla de resultados y gráficas
**Como** autor del informe, **quiero** todos los resultados consolidados **para** que E12
solo tenga que redactar.

**Criterios de aceptación:**
- [x] Tabla maestra con todas las métricas, su meta, el valor medido y ✅/❌
- [x] Gráficas generadas: histograma de latencia, comparativa ON/OFF, evolución del heap
      bajo `inject leak`, fragmentación bajo `inject frag`
- [x] Todas las gráficas con ejes etiquetados y unidades
- [x] Todos los resultados estadísticos indican incertidumbre o límite de medición
- [x] Los datos crudos (CSV y UART) quedan versionados en el repositorio
- [x] `05-ESTADO.md` §4 actualizado con las métricas E11

`experiments/e11/master_results.csv` separa **CUMPLE**, **NO CUMPLE**,
**MEDIDO**, **LIMITADO** y **NO COMPROBABLE**. El informe narrativo corresponde a
E12; E11 entrega aquí los datos, métodos, cifras y gráficas.

**Estimación:** 0,5 h

**Tabla maestra real:** `experiments/e11/master_results.csv` y su vista
`master_results.md`, generadas por `tools/testbench/master_results.py`.
Los costes aislado e integral de la traza son métricas diferentes y así
aparecen; la discrepancia GPIO–CCOUNT figura como **NO COMPROBABLE**.

---

## 5. Notas técnicas críticas

### Rigor experimental: las cinco reglas

| # | Regla | Por qué |
|---|---|---|
| 1 | **Una sola variable cambia por experimento** | Si cambias build y carga a la vez, no sabes qué causó la diferencia |
| 2 | **Mínimo 5 repeticiones** | Sin repetición no hay desviación estándar, y sin σ un número no significa nada |
| 3 | **Reportar siempre la incertidumbre** | "4,2 µs" es una afirmación; "4,2 ± 0,3 µs" es una medición |
| 4 | **Documentar los valores atípicos, no borrarlos** | Un outlier explicado es un hallazgo; uno borrado es fraude |
| 5 | **Carga reproducible** (`inject load nominal`) | Sin ella, comparar dos ejecuciones no significa nada |

### El error de medición que hay que evitar

Medir con la instrumentación encendida y reportar ese número como "la latencia del switch"
**es circular**: estás midiendo el switch *más* el costo de medirlo.

Lo correcto:
- Latencia real del switch → medir con GPIO (hardware externo) o restar el overhead conocido
- Latencia observada → con instrumentación, y **decir que incluye el overhead**

**Ser explícito sobre qué incluye cada número es la diferencia entre medir y adivinar.**

### Qué hacer si un resultado no cumple la meta

**Reportarlo.** Un overhead del 7% documentado y explicado ("se debe a que no implementé
agrupación de eventos; con agrupación estimo un 3%") vale más que un 4% inventado.

> Un informe con un resultado que no cumple la meta, analizado honestamente, demuestra más
> madurez que uno donde todo salió perfecto a la primera. Nada sale perfecto a la primera, y
> un evaluador con experiencia lo sabe.

---

## 6. Riesgos

| # | Riesgo | Mitigación |
|---|---|---|
| V1 | **Se corta E11 por falta de tiempo** | ⚠️ **NO CORTAR.** Es la mejor relación nota/esfuerzo. Recortar HU-05 antes que las demás. |
| V2 | No hay analizador lógico | Método alternativo documentado en HU-05 |
| V3 | Resultados no reproducibles | Carga `nominal` determinista (E09-HU-06) |
| V4 | Un resultado no cumple la meta | Reportarlo y analizarlo. Es un resultado válido. |

---

## 7. Definición de Hecho

- [ ] Las 6 HUs cumplen sus criterios: HU-02 falla S1; hay resultados para 6/6
- [x] El banco de pruebas corre sin intervención humana entre repeticiones
- [x] Las baterías comparables tienen ≥ 5 repeticiones; falsas alertas exige una hora continua
- [x] Todas las métricas E11 de `05-ESTADO.md` §4 indican dispersión o límite
- [x] Latencia observada caracterizada con 57 940 muestras; **S1 no cumple**
- [x] Overhead de instrumentación medido y reportado; **S2 cumple**
- [x] Detector: 20/20 detecciones, 0 falsos positivos en 1 h
- [x] Alternativa de cronómetro externo y limitación GPIO documentadas
- [x] Gráficas generadas y datos crudos versionados
- [x] Tabla maestra de resultados completa
- [x] `05-ESTADO.md` actualizado

La definición de éxito global no implica que todas las **metas numéricas** se
cumplan: S1 falló y la meta GPIO–CCOUNT <5 % quedó sin medición. Esto debe
explicarse en E12 sin convertirlo en un PASS.

---

## 8. Referencias

- Jain, *The Art of Computer Systems Performance Analysis* — la biblia del tema
- Gregg, *Systems Performance: Enterprise and the Cloud* — cap. 2 (Methodology)
- Taylor, *An Introduction to Error Analysis* — propagación de incertidumbre
- Hoefler & Belli (2015), *Scientific Benchmarking of Parallel Computing Systems* — reglas
  para reportar resultados de rendimiento honestamente

---

## 9. Bitácora

| Fecha | Horas | Qué se hizo | Aprendizaje |
|---|---|---|---|
| — | — | — | — |
