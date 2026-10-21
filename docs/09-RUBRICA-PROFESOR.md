# Sentinel OS — Rúbrica del Profesor (Entrega Final)

> 🔴 **DOCUMENTO DE MÁXIMA PRIORIDAD.** Esta es la rúbrica real de calificación.
> Cualquier criterio de aquí tiene prioridad absoluta sobre cualquier funcionalidad de
> Sentinel. Si hay conflicto de tiempo, se sacrifica Sentinel, nunca esto.
>
> **Fuente:** requisitos entregados por el profesor (referencia: `fguzman82/minios-lab`).
> **Aclaración del profesor:** *no* es obligatorio usar su repositorio; lo que pide es que
> el sistema operativo propio **se acomode a estos criterios**.
>
> **Última actualización:** 2026-10-21

---

## 1. Entregables

| # | Entregable | Estado | Notas |
|---|---|---|---|
| D1 | **Link al fork público en GitHub** (fork de `fguzman82/minios-lab`) | ⬜ | ⚠️ Ver §6 — punto a aclarar |
| D2 | **Link al video en YouTube** — máx. 10 min, **con audio explicando** | ⬜ | Guion en E23 |
| D3 | **10 capturas de pantalla**, una por criterio de la rúbrica | ⬜ | 9 criterios + 1 → ver §6 |
| D4 | **Portada** con nombre, código del curso y fecha | ⬜ | Plantilla en E23 |
| D5 | **Manual de uso** de todos los comandos del sistema | ⬜ | Encargo a los agentes (E23) |

> D5 no lo pidió el profesor explícitamente, pero Iker lo quiere y **refuerza todos los
> criterios**: un manual completo demuestra dominio del sistema.

---

## 2. Los 9 criterios de la rúbrica

> Confirmado por Iker: **las 9 escenas del video SON los 9 criterios de la rúbrica.**
> El video debe cubrirlas **en este orden**.

| ID | Criterio (literal del profesor) | Concepto de SO que evalúa |
|---|---|---|
| **C1** | `make clean && make` sin errores | Build reproducible |
| **C2** | Arrancar los 3 componentes: `./minios`, bridge y dashboard | Arquitectura de 3 capas |
| **C3** | Lanzar **mínimo 5 procesos** con `run` | Creación dinámica de procesos |
| **C4** | **Gantt chart** con los 5 procesos y sus colores | Visualización de planificación |
| **C5** | `ps` en miniOS **explicando cada columna** | Tabla de procesos / PCB |
| **C6** | `slice 200` y `slice 1500` — **efecto visible en el Gantt** | Quantum y Round-Robin |
| **C7** | `kill <pid>` y verificar en `ps` y en el dashboard | Terminación de procesos |
| **C8** | `stats` con métricas agregadas | Métricas del planificador |
| **C9** | `exit` y `ps aux \| grep defunct` **vacío en otra terminal** | Limpieza de recursos / zombies |

---

## 3. Criterios de aceptación verificables, uno por uno

> Estos son los criterios que los agentes de IA deben verificar. Cada uno debe ser
> **observable**, no opinable.

### C1 — `make clean && make` sin errores

- [ ] `make clean` borra `build/` completo y termina con código 0
- [ ] `make` compila desde cero y termina con código 0
- [ ] **Cero errores y cero warnings** (el proyecto usa `-Werror`, así que un warning es error)
- [ ] La salida muestra el tamaño del binario (`make size`)
- [ ] Se verifica en una máquina limpia o tras `git clean -xdf`
- [ ] 📸 **Captura:** terminal completa mostrando `make clean && make` y el final sin errores

---

### C2 — Arrancar los 3 componentes

- [ ] Componente 1 — **`./minios`**: un comando único arranca el sistema operativo
      (ver equivalencia en §4)
- [ ] Componente 2 — **bridge**: `tools/bridge.py` conectado, decodificando telemetría
- [ ] Componente 3 — **dashboard**: visible en el navegador, recibiendo datos en vivo
- [ ] Los tres se ven **simultáneamente** en pantalla (terminales divididas + navegador)
- [ ] El bridge reporta tramas OK y 0 errores de CRC
- [ ] El dashboard muestra datos que cambian, no una pantalla estática
- [ ] 📸 **Captura:** pantalla completa con las 3 ventanas visibles a la vez

---

### C3 — Lanzar mínimo 5 procesos con `run`

- [ ] `run <programa>` crea una tarea nueva y **devuelve su TID**
- [ ] Se lanzan **≥5 procesos** de tipos distintos
      (disponibles: `blink`, `counter`, `hog`, `sleeper`, `alloc`, `starve`)
- [ ] Cada uno aparece en `ps` con TID, nombre y estado
- [ ] Cada uno aparece en el dashboard
- [ ] Ninguno falla al crearse; el heap lo soporta (`free` lo confirma)
- [ ] 📸 **Captura:** los 5 comandos `run` y el `ps` resultante con las 5 tareas

---

### C4 — Gantt chart con los 5 procesos y sus colores

- [ ] Eje X = tiempo, eje Y = una fila por proceso
- [ ] **Un color distinto por proceso**, con leyenda visible
- [ ] Se distinguen los estados (RUNNING / READY / BLOCKED / SLEEPING) por color o patrón
- [ ] Se aprecia la **alternancia Round-Robin** entre tareas de igual prioridad
- [ ] Resolución suficiente para ver los turnos individuales (zoom hasta ms)
- [ ] Los 5 procesos de C3 aparecen todos
- [ ] 📸 **Captura:** el Gantt con los 5 procesos y la leyenda de colores

---

### C5 — `ps` explicando cada columna

- [ ] `ps` imprime una tabla con cabecera legible
- [ ] **Cada columna tiene significado documentado** y se explica en el video:

| Columna | Significado que hay que explicar en el video |
|---|---|
| `TID` | Identificador de la tarea (equivalente al PID) |
| `NOMBRE` | Nombre de la tarea |
| `ESTADO` | READY / RUNNING / BLOCKED / SLEEPING / TERMINATED |
| `PRI` | Prioridad (0 = máxima, 7 = idle) |
| `CPU%` | Porcentaje de CPU consumido en la ventana reciente |
| `MEM` | Bytes alocados en el heap por esa tarea |
| `SWITCHES` | Veces que la tarea entró a ejecutar (`ps -v`) |
| `ESPERA` | Peor tiempo en cola READY, en ms (`ps -v`) |
| `QUANTUM` | Quantum asignado en ms ← **añadir, lo exige C6** |

- [ ] El shell aparece en su propia lista (prueba de que es una tarea más)
- [ ] La suma de `CPU%` es coherente (≈100% incluyendo `idle`)
- [ ] `ps -v` muestra las columnas extendidas
- [ ] 📸 **Captura:** salida de `ps` y de `ps -v` completas

---

### C6 — `slice 200` y `slice 1500` con efecto visible en el Gantt

- [ ] `slice` sin argumentos muestra el quantum actual
- [ ] `slice <ms>` cambia el quantum **en caliente**, sin reiniciar
- [ ] `slice 200` → turnos cortos: **muchos cambios de contexto, barras finas** en el Gantt
- [ ] `slice 1500` → turnos largos: **pocos cambios, barras anchas** en el Gantt
- [ ] La diferencia es **evidente visualmente**, no hay que explicarla para verla
- [ ] `ps` refleja el nuevo quantum en la columna `QUANTUM`
- [ ] `stats` refleja el cambio en switches/segundo
- [ ] Rango validado y documentado (mín. 1 ms, máx. razonable); valores inválidos se rechazan
- [ ] 📸 **Captura:** Gantt con `slice 200` y Gantt con `slice 1500`, lado a lado

> 🎓 **El concepto que hay que explicar en el video:** quantum corto = más reactivo pero
> más overhead de cambio de contexto; quantum largo = menos overhead pero peor latencia de
> respuesta. **Y Sentinel puede medir ese compromiso con números reales** (`stats`), lo que
> va más allá de lo que pide el criterio.

---

### C7 — `kill <pid>` verificado en `ps` y en el dashboard

- [ ] `kill <tid>` termina la tarea indicada
- [ ] La tarea **desaparece de `ps`** inmediatamente
- [ ] La tarea **desaparece del dashboard** (su fila del Gantt termina)
- [ ] `free` muestra que su memoria **se liberó** (pila + heap de la tarea)
- [ ] `kill` de un TID inexistente da un error claro, no un fallo
- [ ] No se puede matar `idle` (tid 0) ni el propio shell → error claro
- [ ] 📸 **Captura:** `ps` antes, `kill`, `ps` después, y el dashboard reflejándolo

---

### C8 — `stats` con métricas agregadas

- [ ] `stats` imprime métricas globales del sistema. Mínimo obligatorio:

| Métrica | Por qué está |
|---|---|
| Uptime | Tiempo desde el arranque |
| Total de cambios de contexto | Actividad del planificador |
| Cambios de contexto por segundo | Tasa actual |
| Latencia de cambio de contexto (media / mín / máx) | **Métrica estrella de Sentinel** |
| Quantum actual | Enlaza con C6 |
| Tareas: total / activas / terminadas | Estado del sistema |
| CPU ociosa (%) | Carga del sistema |
| Heap: total / usado / libre / fragmentación | Gestión de memoria |
| Eventos de traza emitidos / perdidos | Honestidad de la instrumentación |

- [ ] Los números son **coherentes** con `ps`, `free` y el dashboard
- [ ] `stats` tras `slice 200` vs `slice 1500` muestra diferencia en switches/s
- [ ] 📸 **Captura:** salida completa de `stats`

> 💡 **Aquí Sentinel supera la rúbrica.** La mayoría de proyectos mostrarán un contador de
> switches. Tú puedes mostrar **latencia de cambio de contexto medida en ciclos de CPU** y
> **eventos de traza perdidos**. Eso es material de E07/E11 que la rúbrica no pedía.

---

### C9 — `exit` y `ps aux | grep defunct` vacío

> ⚠️ **Este es el criterio con conflicto de plataforma.** Ver §4 para la solución.

- [ ] `exit` en la consola de miniOS inicia un apagado ordenado
- [ ] **Lado kernel:** termina todas las tareas de usuario, libera pilas y heap,
      cierra descriptores, y reporta lo liberado
- [ ] Tras `exit`, `ps` muestra **solo las tareas del sistema** (idle, shell)
- [ ] Tras `exit`, `free` vuelve al **estado base exacto** (sin fugas residuales)
- [ ] `exit` emite un resumen: tareas terminadas, bytes liberados, eventos pendientes
- [ ] **Lado host:** `exit` apaga limpiamente el bridge y el dashboard
      (son procesos reales de Linux)
- [ ] ✅ **En otra terminal: `ps aux | grep defunct` devuelve vacío** → demuestra que el
      launcher no dejó procesos zombie
- [ ] 📸 **Captura:** `exit`, el `ps`/`free` posterior, y la otra terminal con
      `ps aux | grep defunct` vacío

---

## 4. Matriz de equivalencias: Linux vs ESP32 bare-metal

> **El problema:** la rúbrica está escrita para un simulador de Linux. Sentinel es un
> kernel bare-metal en silicio. Dos criterios no se pueden cumplir literalmente sin
> adaptación.
>
> **La solución elegida (decisión de Iker):** la vía más rápida, implementando dentro de la
> consola del SO lo que haga falta. **Resultado: los 9 criterios se cumplen, 7 de forma
> literal y 2 mediante equivalencias defendibles.**

| Criterio | Lo que asume la rúbrica | Qué hace Sentinel | ¿Literal? |
|---|---|---|---|
| C1 | `make clean && make` | **Idéntico.** Ya existe. | ✅ Literal |
| **C2** | `./minios` es un ejecutable de Linux | **`./minios` existe como script lanzador** que hace `make flash` + abre el monitor serie + arranca bridge y dashboard. **Se escribe y se ejecuta `./minios` igual que en la rúbrica.** | ✅ Literal (ver nota) |
| C3 | `run` crea procesos | **Idéntico.** `run` crea tareas con TID. | ✅ Literal |
| C4 | Gantt con colores | **Idéntico.** Dashboard propio. | ✅ Literal |
| C5 | `ps` con columnas | **Idéntico**, y con más columnas. | ✅ Literal |
| C6 | `slice <ms>` | **Idéntico.** Quantum en caliente. | ✅ Literal |
| C7 | `kill <pid>` | **Idéntico.** `kill <tid>`. | ✅ Literal |
| C8 | `stats` | **Idéntico**, y con métricas que la rúbrica no pedía. | ✅ Literal |
| **C9** | `ps aux \| grep defunct` vacío → el simulador no dejó zombies en Linux | **Doble demostración:** (a) *lado kernel*: `exit` libera todo y `free` vuelve al estado base; (b) *lado host*: el launcher, el bridge y el dashboard **sí son procesos reales de Linux**, y `ps aux \| grep defunct` sale vacío de verdad. | 🟡 Equivalente + literal parcial |

### Nota sobre C2 — por qué `./minios` sí es literal

No hay trampa: **`./minios` será un ejecutable real** en la raíz del repositorio. Lo que
hace por dentro es orquestar el arranque del sistema (flashear si hace falta, abrir el
puerto serie, levantar bridge y dashboard). Desde la perspectiva del evaluador:

```bash
$ ./minios
[minios] Sentinel OS — arrancando los 3 componentes...
[minios] 1/3 kernel .......... flasheado y arrancado (COM3 @ 921600)
[minios] 2/3 bridge .......... activo (PID 48213)
[minios] 3/3 dashboard ....... http://localhost:8080
[minios] listo. Consola del SO:

sentinel:/home/iker$
```

**Eso es exactamente lo que pide C2.** La diferencia con el laboratorio de referencia es
dónde corre el kernel (silicio vs. proceso de Linux), y eso es una **ventaja**, no una
carencia.

### Nota sobre C9 — qué decir en el video

Esta es la frase a usar, y es honesta:

> "El criterio pide que no queden procesos zombie. Hay que distinguir dos planos.
>
> En el plano del kernel, mi SO **no puede tener zombies en el sentido POSIX**, porque no
> hay `fork()` ni espacios de direcciones separados: el ESP32 no tiene MMU. Lo equivalente
> es que `exit` libere todos los recursos de las tareas, y eso lo demuestro: `ps` queda con
> solo las tareas del sistema y `free` vuelve byte a byte al estado base.
>
> En el plano del host, el launcher, el bridge y el dashboard **sí son procesos reales de
> Linux**. Si mi `exit` no los recogiera correctamente, aparecerían como `defunct`. Aquí
> está la otra terminal: `ps aux | grep defunct`, vacío."

> 🎯 **Esa respuesta demuestra más dominio que cumplir el criterio literalmente**, porque
> exige entender qué *es* un zombie y por qué su existencia depende del modelo de procesos.

---

## 5. Estado actual de cobertura

> Basado en el código existente a 2026-10-21 (E01–E10 completas, E11 en curso).

| Criterio | ¿Qué ya existe? | ¿Qué falta? | Épica | Est. |
|---|---|---|---|---|
| **C1** | ✅ Makefile con `clean`, `all`, `size`, `test` | Verificar 0 warnings desde cero | E22 | 0,5 h |
| **C2** | 🟡 `bridge.py` existe; kernel arranca | **Script `./minios`** + **dashboard** | E20, E21 | 3 h |
| **C3** | ✅ `run` con 6 programas (`blink`, `counter`, `hog`, `sleeper`, `alloc`, `starve`) | Nada. Ya cumple. | — | 0 h |
| **C4** | ❌ No hay dashboard | **Gantt con colores** | **E21** | 8 h |
| **C5** | ✅ `ps` y `ps -v` | Columna `QUANTUM` + documentar columnas | E20 | 1 h |
| **C6** | 🟡 `quantum_ticks` es por tarea y configurable en el TCB | **Comando `slice`** | **E20** | 2 h |
| **C7** | ✅ `kill <tid>` | Verificar reflejo en dashboard | E21 | 0,5 h |
| **C8** | 🟡 Existen `health`, `free`, `jitter`, `trace stats` dispersos | **Comando `stats` unificado** | **E20** | 2 h |
| **C9** | ❌ No hay `exit` | **Comando `exit`** + teardown del launcher | **E20** | 3 h |

### Resumen honesto

| | |
|---|---|
| Criterios ya cubiertos | **3 de 9** (C1, C3, C7 casi) |
| Criterios que requieren comandos nuevos | 4 (C5, C6, C8, C9) → **E20, ~8 h** |
| Criterios que requieren el dashboard | 3 (C2, C4, C7) → **E21, ~8 h** |
| **Total para cumplir la rúbrica** | **~20 h** (E20 + E21 + E22) |

> ⚠️ **El dashboard (E21) es el cuello de botella: 3 de los 9 criterios dependen de él.**
> No se puede cortar. Pero sí se puede hacer en 8 h si se usa una herramienta existente en
> vez de construir frontend desde cero (ver E21).

---

## 6. Puntos a aclarar con el profesor

> Llevarlos a la reunión. **Preguntar demuestra iniciativa.**

| # | Pregunta | Por qué importa | Prioridad |
|---|---|---|---|
| **A1** | **¿Hay que hacer fork de su repositorio igual, aunque mi proyecto sea propio y no derive de su código?** Un fork de GitHub implica ascendencia de commits que mi proyecto no tiene. Alternativa: repositorio público propio enlazado desde su repo, o un fork con mi proyecto en una rama. | D1 es un entregable. Hacerlo mal puede costar puntos o ensuciar el historial. | 🔴 Alta |
| **A2** | **Son 9 criterios pero pide 10 capturas. ¿Cuál es la décima?** Mi propuesta: portada + link al repositorio. | D3 exige una captura por criterio. | 🔴 Alta |
| **A3** | **¿Acepta las equivalencias de C2 y C9 para un kernel bare-metal sin MMU?** (ver §4) | Si exige literalidad POSIX, hace falta un puerto a Linux: +20 h. | 🔴 Alta |
| A4 | ¿`run` debe lanzar procesos **arbitrarios** o basta con programas integrados? | Sin `exec` ni sistema de archivos ejecutable, solo hay programas integrados. | 🟡 Media |
| A5 | ¿El video debe estar en español? | D2 pide audio explicando. | 🟢 Baja |
| A6 | ¿Hay formato obligatorio para la portada (D4)? | Afecta E23. | 🟢 Baja |
| A7 | ¿Cuál es el **código del curso** exacto y la **fecha** que debe ir en la portada? | Dato literal de D4 que hoy no tengo. | 🔴 Alta |

### Cómo plantear A3 (la pregunta importante)

> "Profe, mi sistema operativo es bare-metal: corre directamente sobre el silicio de una
> ESP32, sin Linux debajo. Eso significa que dos criterios no se pueden cumplir de forma
> literal: `./minios` lo resolví con un script lanzador que arranca los tres componentes, y
> los procesos zombie no existen en mi modelo porque no hay `fork()` ni MMU.
>
> Lo que preparé es una doble demostración: que `exit` libera todos los recursos del kernel
> y que el heap vuelve al estado base, y además que el bridge y el dashboard —que sí son
> procesos reales de Linux— no quedan como `defunct`.
>
> **¿Le parece aceptable esa equivalencia, o prefiere que haga un puerto del scheduler a
> proceso de Linux para cumplirlo literalmente?**"

---

## 7. Trazabilidad: criterio → épica → captura

```
C1 make clean && make ──────► ya existe ──────────────► E22 ──► 📸 01
C2 3 componentes ───────────► E20 (./minios) + E21 ──► E22 ──► 📸 02
C3 5 procesos con run ──────► ya existe ──────────────► E22 ──► 📸 03
C4 Gantt con colores ───────► E21 ────────────────────► E22 ──► 📸 04
C5 ps con columnas ─────────► E20 (col. QUANTUM) ────► E22 ──► 📸 05
C6 slice 200 / 1500 ────────► E20 (slice) + E21 ─────► E22 ──► 📸 06
C7 kill verificado ─────────► ya existe + E21 ───────► E22 ──► 📸 07
C8 stats agregadas ─────────► E20 (stats) ───────────► E22 ──► 📸 08
C9 exit sin zombies ────────► E20 (exit + teardown) ─► E22 ──► 📸 09
D1 fork / repo público ─────► E23 ───────────────────────────► 📸 10
```
