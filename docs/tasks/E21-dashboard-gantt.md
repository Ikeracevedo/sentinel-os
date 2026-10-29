# E21 — Dashboard con Gantt Chart

| Campo | Valor |
|---|---|
| **Estado** | 🟡 Implementada y validada en placa; quedan pruebas de entrega |
| **Prioridad** | 🔴 **CRÍTICA — cuello de botella de la rúbrica** |
| **Estimación** | 8 h |
| **Depende de** | E08 (bridge ✅), E20 (para `slice` en el Gantt) |
| **Se ejecuta** | Después de E11, E12 y E20 |
| **Checkpoint** | contribuye a **CP11 — "Se califica"** |
| **Cubre criterios** | **C2, C4, C6, C7** (4 de 9) |
| **Riesgo** | 🔴 Alto — pozo sin fondo de tiempo |

---

## 1. Objetivo

Un dashboard en el navegador que muestre el **Gantt chart de los procesos con colores**, en
vivo, reaccionando a `run`, `slice` y `kill`.

---

## 2. Por qué existe

> 🔴 **Es el cuello de botella de toda la entrega.**
> **4 de los 9 criterios de la rúbrica dependen de este dashboard:**
>
> | Criterio | Qué necesita del dashboard |
> |---|---|
> | **C2** | Que exista y sea uno de los 3 componentes |
> | **C4** | El Gantt con 5 procesos y sus colores — **el criterio es el Gantt** |
> | **C6** | Que el efecto de `slice 200` vs `slice 1500` sea **visible en el Gantt** |
> | **C7** | Que `kill` se refleje en el dashboard |

Sin E21, el proyecto no puede aprobar la rúbrica, por bueno que sea el kernel.

### ⚠️ Pero es la épica más peligrosa del proyecto

Es infinitamente más agradable hacer un dashboard bonito que depurar liberación de memoria.
**Esa es exactamente la razón por la que es peligrosa.**

| Enfoque | Tiempo | Resultado |
|---|---|---|
| **HTML + Chart.js servido por el bridge** | **6–8 h** | Funcional, controlable, cumple los 4 criterios ✅ |
| Grafana + SQLite | 4 h | Series temporales sí, **pero el Gantt es difícil** ⚠️ |
| React + WebSockets + diseño propio | 25 h+ | Bonito, y el resto sin terminar ❌ |

⏱️ **TIMEBOX ESTRICTO: 8 horas. Decisión recomendada: HTML + Chart.js.**

> **Por qué no Grafana aquí** (a diferencia de lo que decía E16): Grafana es excelente para
> series temporales, pero **el Gantt de procesos por estado no es un panel nativo** y
> forzarlo cuesta más que escribirlo. Y el Gantt **es** el criterio C4. Registrar como
> **ADR-016**.

---

## 3. Alcance

### Incluye
- Servidor HTTP mínimo dentro del bridge
- Página única autocontenida (HTML + CSS + JS inline)
- **Gantt chart de procesos con un color por proceso y leyenda**
- Marcadores de eventos (`run`, `kill`, `slice`) sobre el Gantt
- Paneles de apoyo: CPU, heap, switches/s
- Actualización en vivo
- Legibilidad en proyector

### NO incluye
- ❌ Nube AWS → **E14** (backlog)
- ❌ Autenticación, multiusuario
- ❌ Diseño visual personalizado más allá de lo legible
- ❌ Botones de control remoto (nice-to-have, al final si sobra tiempo)

---

## 4. Historias de Usuario

### HU-E21-01 — Servidor y transporte de datos al navegador
**Como** dashboard, **quiero** recibir la telemetría del bridge **para** poder dibujarla.

**Criterios de aceptación:**
- [ ] El bridge expone un servidor HTTP local (`http.server` de la librería estándar)
- [ ] `GET /` sirve la página del dashboard
- [ ] `GET /api/state` devuelve JSON con el estado actual: tareas, heap, switches, quantum
- [ ] `GET /api/timeline?desde=<t>` devuelve los eventos de cambio de contexto recientes
- [ ] Sin dependencias externas de Python más allá de lo ya usado (`pyserial`)
- [ ] El navegador hace polling cada 500 ms (más simple y robusto que WebSockets)
- [ ] ✅ **Funciona sin internet** (todo local, sin CDN — Chart.js embebido o inline)
- [ ] Si el bridge pierde el puerto serie, el dashboard lo indica en vez de congelarse
- [ ] Ventana de datos acotada en memoria (últimos 60 s) para no crecer sin límite

**Estimación:** 2 h

> ⚠️ **Sin internet es obligatorio.** La sustentación puede ser en un aula sin WiFi. Nada
> de `<script src="https://cdn...">`. Chart.js se descarga una vez y se versiona en el
> repositorio, o se dibuja el Gantt con Canvas puro (ver HU-02).

---

### HU-E21-02 — Gantt chart con colores ⭐⭐ `[C4]`
**Como** evaluador, **quiero** ver qué proceso corrió en cada instante **para** verificar el
criterio C4.

**Criterios de aceptación:**
- [ ] Eje X = tiempo; eje Y = **una fila por proceso**, etiquetada con TID y nombre
- [ ] **Un color distinto por proceso**, asignado de forma estable (el mismo TID siempre
      tiene el mismo color)
- [ ] **Leyenda visible** que asocia color ↔ proceso
- [ ] Los **estados se distinguen**: RUNNING sólido, READY semitransparente o rayado,
      BLOCKED/SLEEPING en gris
- [ ] Se aprecia la **alternancia Round-Robin** entre tareas de igual prioridad
- [ ] Ventana de tiempo seleccionable: 1 s / 5 s / 10 s / 30 s
- [ ] Resolución suficiente para ver turnos individuales con `slice 200`
- [ ] ✅ **Los 5 procesos de C3 aparecen todos simultáneamente con colores distintos**
- [ ] Rendimiento: no se traba con 60 s de datos y 8 tareas
- [ ] Legible a 4 metros en proyector (probado)

**Estimación:** 3 h

**Notas técnicas:**
> El Gantt se construye de los eventos `EV_SWITCH_OUT` / `EV_SWITCH_IN` que **ya emite E07**.
> Cada par define un intervalo `[t_inicio, t_fin]` para un TID en estado RUNNING. Los huecos
> entre que una tarea sale y vuelve a entrar son READY o BLOCKED, deducibles del motivo del
> switch (`quantum` / `block` / `yield`), que el protocolo **ya transporta**.
>
> **Canvas puro es suficiente y evita la dependencia de Chart.js.** Dibujar rectángulos de
> colores en una cuadrícula temporal son ~80 líneas de JS. Recomendado: Canvas para el
> Gantt, Chart.js solo para las líneas de CPU/heap (o Canvas también, si se quiere cero
> dependencias).

> 🎯 **Este es el panel que gana el criterio C4 y además demuestra visualmente que tu
> scheduler de E02 funciona.** Es lo mismo que muestra Tracealyzer, construido por ti.

---

### HU-E21-03 — Efecto de `slice` visible en el Gantt ⭐ `[C6]`
**Como** evaluador, **quiero** ver la diferencia entre quantums **para** verificar el
criterio C6.

**Criterios de aceptación:**
- [ ] `slice 200` → el Gantt muestra **barras anchas, pocos cambios** por ventana de tiempo
- [ ] `slice 1500` → el Gantt muestra **barras muy anchas, muchos menos cambios**
      *(nota: 200 ms ya es un quantum largo; ver abajo)*
- [ ] El cambio de quantum se marca en el Gantt con una **línea vertical etiquetada**
      (`slice 200 →`), usando el evento `EV_SLICE_CHANGE` de E20-HU-02
- [ ] El valor del quantum actual se muestra en un indicador del dashboard
- [ ] ✅ **La diferencia es evidente sin explicación verbal** — es el criterio
- [ ] El contador de switches/s del dashboard refleja el cambio
- [ ] Probado también con `slice 10` (el valor por defecto) para tener tres referencias

**Estimación:** 1,5 h

> ⚠️ **Detalle importante para la demo.** La rúbrica pide `slice 200` y `slice 1500`. Con el
> quantum por defecto de Sentinel (10 ms), **200 ms ya es 20× más largo**. Así que en el
> Gantt los dos valores de la rúbrica se verán **ambos como barras anchas**, y la diferencia
> entre 200 y 1500 será de 7,5× — visible, pero menos dramática que comparar contra 10 ms.
>
> **Recomendación para el video:** mostrar los tres: `slice 10` (default, barras finas),
> `slice 200` (anchas), `slice 1500` (muy anchas). Así se cumple el criterio literalmente
> **y** la demostración del concepto es mucho más fuerte. Mencionar en el guion de E23.

---

### HU-E21-04 — `run` y `kill` reflejados en el dashboard `[C3, C7]`
**Como** evaluador, **quiero** que el dashboard reaccione a los comandos **para** verificar
los criterios C3 y C7.

**Criterios de aceptación:**
- [ ] Al hacer `run <programa>`, aparece una **fila nueva** en el Gantt con color propio
- [ ] El evento se marca con una línea vertical etiquetada (`run counter →`)
- [ ] Al hacer `kill <tid>`, la fila de esa tarea **termina visiblemente**
- [ ] La fila de la tarea terminada se marca (sombreado o etiqueta `TERMINATED`)
- [ ] El panel de tareas (tabla) se sincroniza con `ps` del kernel
- [ ] El panel de memoria refleja la liberación tras `kill`
- [ ] ✅ **Latencia de reflejo < 2 s** desde el comando hasta el dashboard
- [ ] `exit` (E20-HU-05) hace que el dashboard muestre el estado de apagado

**Estimación:** 1 h

---

### HU-E21-05 — Paneles de apoyo y legibilidad
**Como** presentador, **quiero** un dashboard legible en proyector **para** que la demo se
entienda sin explicación.

**Criterios de aceptación:**
- [ ] Panel de **CPU**: % por tarea + % ociosa
- [ ] Panel de **memoria**: heap libre, mayor bloque libre, fragmentación %
- [ ] Panel de **planificador**: switches/s, latencia media, quantum actual
- [ ] Panel de **salud**: estado del heartbeat (🟢 vivo / 🔴 sin latido), eventos perdidos
- [ ] Panel de **alertas**: las de E10 (deadlock, fragmentación, inanición)
- [ ] Fuente ≥ 16 px; alto contraste; colores distinguibles en escala de grises
- [ ] **Pocos paneles grandes**, no muchos pequeños (legibilidad a 4 m)
- [ ] El Gantt ocupa al menos el 50% de la pantalla (es el criterio C4)
- [ ] Probado en pantalla grande / proyector antes de grabar

**Estimación:** 0,5 h

> 💡 **El panel de alertas es gratis y te distingue.** E10 ya genera las alertas; mostrarlas
> en el dashboard añade un criterio que la rúbrica no pedía. Si durante la demo haces
> `inject deadlock` y aparece una alerta roja, eso está por encima de lo exigido.

---

## 5. Notas técnicas críticas

### La regla de autocontrol

> **Cada hora invertida en hacer el dashboard más bonito es una hora no invertida en los
> criterios que faltan.**

Señales de que te estás pasando:
- Estás eligiendo paletas de colores
- Estás peleando con CSS
- Estás instalando un framework de frontend
- Llevas más de 8 horas en esta épica

**Si ves cualquiera de estas: para.**

### Qué se reutiliza (no empezar de cero)

| Lo que hace falta | Ya existe en |
|---|---|
| Decodificación de tramas | `tools/bridge.py` (E08) |
| Eventos de switch con motivo | E07 + `03-PROTOCOLO.md` §4 |
| Métricas de heap y CPU | `FRAME_STATS` (E08) |
| Alertas | E10 (`FRAME_ALERT`) |
| Persistencia para replay | SQLite del bridge (E08) |

**E21 es sobre todo una capa de presentación sobre datos que ya llegan.**

### El modo replay salva la demo

El bridge ya tiene `--replay` (E08-HU-06). Si el hardware falla el día de la sustentación,
se reproduce una sesión grabada y **el dashboard se ve idéntico**. Verificar que el
dashboard funciona en modo replay — es el seguro de vida de la presentación.

---

## 6. Riesgos

| # | Riesgo | Mitigación |
|---|---|---|
| ⚠️ **R21.1** | **Consume tiempo desproporcionado** | **Timebox 8 h. Canvas puro, no frameworks.** |
| R21.2 | No funciona sin internet | Cero CDN; todo inline o versionado |
| R21.3 | El Gantt se traba con muchos datos | Ventana acotada a 60 s; decimar si hace falta |
| R21.4 | Ilegible en proyector | Probar en pantalla grande antes de grabar |
| R21.5 | `slice 200` vs `1500` poco diferenciable | Mostrar también `slice 10` (ver HU-03) |
| R21.6 | El hardware falla en la demo | Verificar el dashboard en modo `--replay` |

---

## 7. Definición de Hecho

- [ ] Las 5 HUs cumplen sus criterios
- [ ] ADR-016 registrada (Canvas/Chart.js en vez de Grafana, con el por qué)
- [ ] ✅ **Gantt con 5 procesos, colores distintos y leyenda** `[C4]`
- [ ] ✅ **El dashboard es el 3er componente de `./minios`** `[C2]`
- [ ] ✅ **`slice 200` vs `slice 1500` visiblemente distintos en el Gantt** `[C6]`
- [ ] ✅ **`run` y `kill` reflejados en < 2 s** `[C3, C7]`
- [ ] Funciona sin internet
- [ ] Funciona en modo `--replay` (seguro de la demo)
- [ ] Legible a 4 m en proyector (probado)
- [ ] **Menos de 8 horas invertidas** (verificar en la bitácora)
- [ ] Capturas guardadas para E22/E23
- [ ] `05-ESTADO.md` actualizado

---

## 8. Referencias

- `03-PROTOCOLO.md` §4 — tipos de evento de switch (la materia prima del Gantt)
- `tools/bridge.py` — el decodificador a extender
- MDN Canvas API — `fillRect`, `fillText` es todo lo que hace falta
- Percepio Tracealyzer — referencia visual del Gantt de tareas
- Tufte, *The Visual Display of Quantitative Information*

---

## 9. Bitácora

| Fecha | Horas | Qué se hizo | Aprendizaje |
|---|---|---|---|
| — | — | — | — |
| 2026-10-29 | — | Bridge HTTP local, Gantt Canvas, tabla de tareas, marcadores `run`/`slice`/`kill` y terminal UART estable | Serializar una orden hasta el prompt siguiente evita mezclar salida y conserva la telemetría |
