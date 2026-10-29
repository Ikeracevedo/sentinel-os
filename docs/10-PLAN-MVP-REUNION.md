# Plan de MVP para la Reunión con el Profesor

> 🎯 **Propósito:** separar con honestidad **qué se puede mostrar YA** de **qué falta**, para
> que la reunión sea una demostración de avance sólido y no una lista de disculpas.
>
> **Fecha de la reunión:** _(pendiente)_
> **Última actualización:** 2026-10-21

---

## 1. La idea central de la reunión

No vas a mostrar un proyecto terminado. Vas a mostrar **un sistema operativo que arranca,
planifica, gestiona memoria y se diagnostica a sí mismo**, más un plan explícito y
cuantificado de lo que falta para la rúbrica.

> **La frase con la que abres:**
>
> "Profe, tengo un microkernel bare-metal corriendo en una ESP32: planificador apropiativo,
> gestión dinámica de memoria, sistema de archivos y shell propios, sin FreeRTOS ni ningún
> SDK por debajo. Ya está validado en placa con mediciones reales. De sus 9 criterios, **3
> los cumplo hoy tal como los pide**, y le traigo medido exactamente qué falta para los
> otros 6 y en qué orden lo voy a hacer. Además le quiero mostrar algo que la rúbrica no
> pedía."

Eso cambia el marco: no estás pidiendo indulgencia, estás reportando avance con números.

---

## 2. BLOQUE A — Lo que se muestra YA (sin trabajo adicional)

> Todo esto está **validado en placa** y etiquetado en Git (CP0–CP7). No requiere ni una
> hora más de desarrollo.

### A.1 Criterios de la rúbrica ya cubiertos

| Criterio | Estado | Cómo se demuestra |
|---|---|---|
| **C1** — `make clean && make` sin errores | ✅ **Listo** | Compila con `-Werror`, cero warnings |
| **C3** — ≥5 procesos con `run` | ✅ **Listo** | `run blink`, `counter`, `hog`, `sleeper`, `alloc`, `starve` → `ps` |
| **C5** — `ps` explicando columnas | 🟡 **Casi** | `ps` y `ps -v` funcionan; falta la columna `QUANTUM` |
| **C7** — `kill <pid>` verificado en `ps` | 🟡 **Casi** | `kill` funciona y se ve en `ps` y `free`; falta el reflejo en dashboard |

**Resultado: 2 criterios completos + 2 al 80%.**

### A.2 El sistema operativo funcionando (lo que el profesor pidió en clase)

| Qué | Comando | Checkpoint |
|---|---|---|
| Arranque bare-metal sin SDK | — | CP0 ✅ |
| Multitarea apropiativa, ≥5 tareas, 1 h estable | `ps` | CP1, CP2 ✅ |
| Gestión dinámica de memoria con fragmentación | `free`, `free -v` | CP3 ✅ |
| Shell con comandos tipo Unix | `ls`, `cd`, `pwd`, `cat`, `mkdir`, `rm`, `write` | CP4 ✅ |
| Sistema de archivos jerárquico + `/proc` | `cat /proc/meminfo` | CP4 ✅ |

### A.3 Lo que la rúbrica NO pedía — tu diferenciador ⭐

> **Esto es lo más importante de la reunión.** La rúbrica mide un scheduler Round-Robin con
> visualización. Tú tienes eso **y además** un sistema que se diagnostica solo.

| Qué mostrar | Comando | Por qué impresiona |
|---|---|---|
| **Instrumentación medida** | `trace stats` | 48 ciclos por evento, overhead +2,27 puntos de CPU. **Medido, no estimado.** |
| **Deadlock provocado y detectado** | `inject deadlock` → alerta en < 1 s | Teoría de grafos, no adivinanza. Ciclo en el grafo de espera. |
| **Fuga de memoria** | `inject leak` → `free` | El heap cae de forma monótona y visible |
| **Fragmentación** | `inject frag` → `free -v` | `kmalloc` falla con memoria libre. El concepto, demostrado. |
| **Inanición** | `inject hog` → `ps -v` | `max_ready_wait` crece en las tareas de baja prioridad |
| **Inversión de prioridad** | `inject prio-inv` | **El bug del Mars Pathfinder, reproducido** |
| **Telemetría a 921600 baudios** | bridge en vivo | 600 s, 156 096 eventos, **0 pérdidas** |
| **Diagnóstico unificado** | `health`, `graph`, `alerts` | El sistema se explica a sí mismo |

### A.4 Guion del Bloque A (8 minutos)

```
1. [0:30] Arranque. "Esto es bare-metal: no hay FreeRTOS ni ESP-IDF debajo."
2. [1:00] ps       → "el shell es una tarea más, planificada por mi scheduler"
3. [1:00] ls, cd, pwd, cat /proc/meminfo  → los requisitos de clase
4. [1:00] run × 5, ps  → criterio C3 cumplido
5. [0:30] free -v  → gestión de memoria con índice de fragmentación
6. [1:30] inject deadlock → alerta de detección en vivo  ⭐ EL MOMENTO FUERTE
7. [1:00] inject frag → "mire: kmalloc de 4 KB falla con 30 KB libres"
8. [1:00] el bridge en vivo → telemetría, 0 eventos perdidos
9. [0:30] trace stats → "mi instrumentación cuesta 2,27 puntos de CPU. Lo medí."
```

> 🎯 **El paso 6 es el que no va a olvidar.** Escribes un comando, pasan tres segundos, y el
> sistema anuncia el deadlock nombrando las tareas y los mutexes implicados.

---

## 3. BLOQUE B — Lo que falta (con números, no con excusas)

> Preséntalo así: una tabla con el criterio, qué falta exactamente, cuánto cuesta y en qué
> épica está planificado. **Tener el costo estimado es lo que lo convierte en un plan.**

| Criterio | Qué falta | Épica | Est. | Prioridad |
|---|---|---|---|---|
| **C6** — `slice 200` / `slice 1500` | Comando `slice`. El quantum ya es configurable por tarea en el TCB; falta exponerlo. | E20-HU-02 | 2 h | 1ª |
| **C5** — columna `QUANTUM` en `ps` | Una columna más | E20-HU-03 | 1 h | 2ª |
| **C8** — `stats` | Comando que unifique métricas que **ya existen** dispersas en `health`, `free`, `jitter`, `trace stats` | E20-HU-04 | 2 h | 3ª |
| **C9** — `exit` sin zombies | Comando `exit` + teardown del host | E20-HU-05/06 | 3,5 h | 4ª |
| **C4** — **Gantt con colores** | **El dashboard.** ⚠️ Es el cuello de botella. | **E21** | **8 h** | 5ª |
| **C2** — 3 componentes | `./minios` + el dashboard de arriba | E20-HU-01 + E21 | 2 h | 6ª |
| **C7** — reflejo en dashboard | Depende de E21 | E21-HU-04 | 0,5 h | incluido |

### Totales

| | |
|---|---|
| Comandos del kernel (E20) | **8,5 h** → cubre C5, C6, C8, C9 y parte de C2 |
| Dashboard (E21) | **8 h** → cubre C4, C7 y el resto de C2 |
| Validación (E22) | **5 h** → verifica los 9 + produce las 10 capturas |
| Entregables y manual (E23) | **9 h** → video, portada, repositorio, manual |
| **TOTAL para entrega completa** | **30,5 h** |

### Qué decirle sobre el reparto

> "Lo que falta se divide en dos bloques muy distintos. Los comandos del kernel son 8 horas
> y son trabajo mecánico: el sustrato ya está, solo hay que exponerlo —el quantum, por
> ejemplo, ya es configurable por tarea en el bloque de control, solo falta el comando que
> lo cambie. El dashboard son otras 8 horas y **es el cuello de botella: tres de sus nueve
> criterios dependen de él**, así que es lo que tiene más riesgo y lo voy a atacar con un
> límite de tiempo estricto."

---

## 4. Las 3 preguntas que tienes que hacer en la reunión

> No las dejes para el final. Son las que definen semanas de trabajo.

### P1 — Sobre las equivalencias (la más importante)

> "Mi sistema es bare-metal: corre directo sobre el silicio, sin Linux debajo. Eso hace que
> dos de sus criterios no se puedan cumplir de forma literal.
>
> El de `./minios` lo resolví con un script lanzador real que arranca los tres componentes,
> así que ahí no hay diferencia práctica.
>
> El de los procesos zombie sí es distinto: **en mi sistema no pueden existir zombies en el
> sentido POSIX**, porque no hay `fork()` ni espacios de direcciones separados — el ESP32 no
> tiene MMU. Lo que preparé es una doble demostración: que `exit` libera todos los recursos
> y el heap vuelve byte a byte al estado base, y además que el bridge y el dashboard —que sí
> son procesos reales de Linux— no quedan como `defunct`.
>
> **¿Le parece aceptable esa equivalencia, o prefiere que porte el scheduler a un proceso de
> Linux para cumplirlo literalmente?"**

*Impacto de la respuesta: si exige literalidad, son ~20 h extra de puerto POSIX.*

### P2 — Sobre el fork

> "Sobre el entregable del fork: mi proyecto no deriva de su código, es un kernel propio
> para otra arquitectura. ¿Prefiere un fork de su repositorio con mi proyecto adentro, o un
> repositorio propio público enlazado? Lo hago como usted prefiera, solo quiero que el
> historial quede limpio."

### P3 — Datos literales que faltan

- ¿El **código exacto del curso** y la **fecha** para la portada?
- Son 9 criterios pero pide 10 capturas — **¿cuál es la décima?**
- ¿Hay formato institucional obligatorio para la portada?

---

## 5. Lo que NO debes hacer en la reunión

| ❌ No hagas esto | ✅ Haz esto |
|---|---|
| Disculparte por lo que falta | Presentarlo con costo estimado y orden |
| Decir "casi lo tengo" | Decir "faltan 2 horas y está planificado en E20-HU-02" |
| Esconder que C4 no está | Decir "es el cuello de botella, 3 criterios dependen de él, lo ataco con timebox" |
| Prometer fechas sin margen | Dar el número de horas y dejar que él calibre |
| Mostrar todo lo de Sentinel y agotar el tiempo | Bloque A en 8 min, Bloque B en 3 min, preguntas en 4 min |
| Dejar las preguntas para el final | Hacerlas cuando queden ≥5 min |

---

## 6. Orden de ejecución tras la reunión

```
SESIÓN ACTUAL (en curso por los agentes de IA)
  └── E11 Validación  +  E12 Documentación     ← NO TOCAR

SIGUIENTE SESIÓN — en este orden estricto
  1. E20-HU-02  slice          2 h   → C6
  2. E20-HU-03  ps QUANTUM     1 h   → C5 ✅
  3. E20-HU-04  stats          2 h   → C8 ✅
  4. E20-HU-05  exit kernel    2 h   → C9 (mitad)
  5. E20-HU-06  teardown host  1,5 h → C9 ✅
  6. E21        dashboard      8 h   → C4 ✅ C7 ✅   ⚠️ cuello de botella
  7. E20-HU-01  ./minios       2 h   → C2 ✅
  8. E22        validación     5 h   → verifica 9/9 + 10 capturas
  9. E23        entregables    9 h   → video, portada, repo, manual
```

**Por qué este orden:** los cuatro primeros pasos son baratos y cierran **3 criterios
completos** en 8 horas. Eso te deja con 5 de 9 criterios cerrados antes de tocar el
dashboard, que es lo riesgoso. Si algo se complica, llegas a la entrega con más criterios
cerrados que si hubieras empezado por el dashboard.

`./minios` va **después** del dashboard porque tiene que arrancarlo.

---

## 7. Checklist antes de la reunión

- [ ] Placa conectada y probada el mismo día (no confiar en que "ayer funcionaba")
- [ ] `make clean && make` ejecutado desde cero sin warnings
- [ ] `inject deadlock` probado 3 veces seguidas — es tu momento fuerte, no puede fallar
- [ ] Bridge probado con el puerto correcto
- [ ] Terminal con fuente grande por si proyecta
- [ ] Este documento abierto en el Bloque B, para mostrar la tabla de lo que falta
- [ ] `09-RUBRICA-PROFESOR.md` §4 a mano, por si pregunta por las equivalencias
- [ ] Las 3 preguntas de §4 anotadas en papel o en el teléfono
- [ ] Sesión de respaldo en modo `--replay` por si el hardware falla

---

## 8. Resumen en una tabla (para mostrarle en pantalla)

| Criterio | Hoy | Falta | Horas |
|---|---|---|---|
| C1 `make clean && make` | ✅ | — | 0 |
| C2 3 componentes | 🟡 33% | `./minios` + dashboard | 2 + E21 |
| C3 5 procesos con `run` | ✅ | — | 0 |
| C4 Gantt con colores | ❌ | Dashboard completo | 8 |
| C5 `ps` por columnas | 🟡 80% | Columna `QUANTUM` | 1 |
| C6 `slice` | ❌ | Comando `slice` | 2 |
| C7 `kill` verificado | 🟡 80% | Reflejo en dashboard | 0,5 |
| C8 `stats` | 🟡 60% | Comando unificado | 2 |
| C9 `exit` sin zombies | ❌ | `exit` + teardown | 3,5 |
| | **2 ✅ · 4 🟡 · 3 ❌** | | **~19 h** |

**Más allá de la rúbrica (ya funcionando):** detección de deadlocks por teoría de grafos,
inyección de 6 patologías, instrumentación con overhead medido, telemetría con 0 pérdidas en
10 minutos continuos, y un detector que corre desde el ISR del timer sin poder ser bloqueado.
