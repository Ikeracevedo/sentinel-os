# E20 — Comandos y Lanzador para la Rúbrica

| Campo | Valor |
|---|---|
| **Estado** | 🟡 En curso: `slice`, `ps` con `QUANTUM` y `stats` validados en placa; faltan `exit`, teardown y lanzador. |
| **Prioridad** | 🔴 **CRÍTICA — exigida por la rúbrica de calificación** |
| **Estimación** | 8,5 h |
| **Depende de** | E02, E03, E06, E07 (todas ✅ completas) |
| **Se ejecuta** | **Después de E11 y E12.** No tocar E11/E12. |
| **Habilita** | E21, E22, E23 |
| **Checkpoint** | contribuye a **CP11 — "Se califica"** |
| **Cubre criterios** | **C2, C5, C6, C8, C9** |
| **Riesgo** | 🟢 Bajo (todo el sustrato ya existe) |

---

## 1. Objetivo

Añadir los comandos y el lanzador que la rúbrica del profesor exige y que Sentinel aún no
tiene: **`slice`, `stats`, `exit`** y el ejecutable **`./minios`**.

---

## 2. Por qué existe

> 🔴 **Esta épica existe por requisito directo de la rúbrica de calificación.**
> Ver `09-RUBRICA-PROFESOR.md` §2 y §5.

De los 9 criterios de la rúbrica, **5 dependen de esta épica**:

| Criterio | Qué falta | HU |
|---|---|---|
| C2 | El ejecutable `./minios` que arranca los 3 componentes | HU-01 |
| C5 | Columna `QUANTUM` en `ps` + documentación de columnas | HU-03 |
| C6 | Comando `slice <ms>` | HU-02 |
| C8 | Comando `stats` unificado | HU-04 |
| C9 | Comando `exit` con liberación verificada + teardown del host | HU-05, HU-06 |

**La buena noticia:** todo el sustrato ya está construido. `quantum_ticks` ya es por tarea y
configurable en el TCB; las métricas ya existen dispersas en `health`, `free`, `jitter` y
`trace stats`. E20 es mayormente **exponer y unificar**, no construir desde cero.

---

## 3. Alcance

### Incluye
- Script lanzador `./minios` (los 3 componentes con un comando)
- Comando `slice [ms]` — quantum en caliente
- Columna `QUANTUM` en `ps`
- Comando `stats` — métricas agregadas unificadas
- Comando `exit` — apagado ordenado con liberación verificada
- Teardown limpio del bridge y dashboard desde el host
- Documentación de cada columna de `ps` (insumo para el video)

### NO incluye
- ❌ El dashboard y el Gantt → **E21**
- ❌ La verificación contra la rúbrica → **E22**
- ❌ Video, capturas, manual → **E23**
- ❌ Puerto POSIX del kernel → descartado (ver `09-RUBRICA-PROFESOR.md` §4)

---

## 4. Historias de Usuario

### HU-E20-01 — Lanzador `./minios` ⭐ `[C2]`
**Como** evaluador, **quiero** arrancar el sistema completo con un solo comando **para**
verificar el criterio C2.

**Criterios de aceptación:**
- [ ] Existe `./minios` en la raíz del repositorio, ejecutable (`chmod +x`)
- [ ] Hay versión para Windows (`minios.ps1` o `minios.cmd`) y para Linux/macOS (`minios`)
- [ ] `./minios` arranca **los 3 componentes en orden** y lo reporta por pantalla:
  1. Kernel: flashea si hace falta y abre el puerto serie
  2. Bridge: `tools/bridge.py` en segundo plano, con su PID visible
  3. Dashboard: servidor local, con su URL visible
- [ ] Al terminar, entrega el control a la **consola del SO** (el prompt de Sentinel)
- [ ] Salida esperada:
```
[minios] Sentinel OS — arrancando los 3 componentes...
[minios] 1/3 kernel .......... flasheado y arrancado (COM3 @ 921600)
[minios] 2/3 bridge .......... activo (PID 48213)
[minios] 3/3 dashboard ....... http://localhost:8080
[minios] listo. Consola del SO:

sentinel:/home/iker$
```
- [ ] `./minios --no-flash` omite el flasheo (para demos rápidas)
- [ ] `./minios --port COM3` permite indicar el puerto
- [ ] Si un componente falla, lo reporta claramente y **no deja los otros colgados**
- [ ] Registra los PIDs del bridge y dashboard en `build/minios.pids` (lo usa HU-06)

**Estimación:** 2 h

**Notas técnicas:**
> El script es el pegamento que convierte tres comandos manuales en el `./minios` que pide
> la rúbrica. **No hay trampa:** es un ejecutable real que arranca el sistema real. La
> diferencia con el laboratorio de referencia es que el kernel corre en silicio, lo cual es
> una ventaja.
>
> ⚠️ **Guardar los PIDs es obligatorio.** Sin `build/minios.pids`, el `exit` de HU-06 no
> puede recoger el bridge y el dashboard, y el criterio C9 (`ps aux | grep defunct` vacío)
> no se puede demostrar.

---

### HU-E20-02 — Comando `slice` ⭐ `[C6]`
**Como** evaluador, **quiero** cambiar el quantum en caliente **para** ver su efecto en el
Gantt y verificar el criterio C6.

**Criterios de aceptación:**
- [x] `slice` sin argumentos imprime el quantum actual en ms y en ticks
- [x] `slice <ms>` cambia el quantum **sin reiniciar el sistema**
- [x] El cambio aplica a **todas las tareas**, actuales y futuras
      (nuevo valor por defecto + actualización de `quantum_ticks` de las existentes)
- [x] `slice 200` y `slice 1500` funcionan, que son los valores exactos de la rúbrica
- [x] Rango validado: mínimo 1 ms, máximo 10000 ms; fuera de rango → error claro
- [x] Argumento no numérico → error claro, no un valor basura
- [x] El cambio es **visible en `ps`** (columna `QUANTUM`)
- [x] El cambio es **visible en `stats`** (switches/segundo cambia de forma medible)
- [x] El cambio emite un evento de traza para que el dashboard lo marque en el Gantt
- [x] ✅ **Verificado: `slice 200` produce ≥5× más cambios de contexto/s que `slice 1500`**
- [x] `help slice` explica el uso y el compromiso conceptual

**Estimación:** 2 h

**Notas técnicas:**
```c
// El sustrato ya existe en kernel/include/sentinel/task.h:
#define TASK_DEFAULT_QUANTUM_TICKS 10u   /* 10 ms a 1000 Hz */
uint32_t quantum_ticks;   /* por tarea, ya configurable */
uint32_t quantum_left;
```
Hace falta:
1. Una variable global `g_default_quantum_ticks` que reemplace el `#define` como valor por
   defecto en `task_create`
2. `sched_set_quantum(ms)` que la actualice y recorra la tabla de tareas
3. Emitir `EV_SLICE_CHANGE` (nuevo tipo de evento) para que el dashboard lo anote

> ⚠️ **Cuidado con la tarea actual:** si se baja el quantum a un valor menor que
> `quantum_left`, hay que recortar `quantum_left` al nuevo máximo, o la tarea actual
> correrá más de lo debido una última vez.

> 🎓 **Lo que hay que explicar en el video:** quantum corto = sistema más reactivo pero más
> overhead de cambio de contexto; quantum largo = menos overhead pero peor latencia. **Y
> Sentinel puede medir ese compromiso** con `stats`, lo que va más allá del criterio.

---

### HU-E20-03 — Columna `QUANTUM` y documentación de `ps` `[C5]`
**Como** evaluador, **quiero** que `ps` muestre el quantum **para** poder explicar cada
columna y verificar el criterio C5.

**Criterios de aceptación:**
- [ ] `ps` añade la columna `QUANTUM` (ms)
- [ ] Las columnas quedan alineadas y legibles en 80 caracteres
- [ ] `ps -v` mantiene las columnas extendidas (`SWITCHES`, `ESPERA`, `PILA`)
- [ ] Cada columna está documentada en `docs/MANUAL-DE-USO.md` (E23) con su significado
- [ ] `help ps` lista y explica brevemente cada columna
- [ ] El shell aparece en su propia salida de `ps` (prueba de que es una tarea más)
- [ ] La suma de `CPU%` es coherente (≈100% contando `idle`)
- [ ] Formato objetivo:
```
sentinel:/$ ps
TID  NOMBRE      ESTADO     PRI  QUANTUM   CPU%    MEM
  0  idle        READY        7     10ms   78.4%      0
  1  shell       RUNNING      4     10ms    1.2%    256
  2  tracer      READY        6     10ms    1.8%   1024
  3  blink       SLEEPING     5     10ms    0.3%      0
  4  counter     READY        5     10ms    6.1%    128
  5  hog         READY        5     10ms   12.2%      0
```

**Estimación:** 1 h

---

### HU-E20-04 — Comando `stats` ⭐ `[C8]`
**Como** evaluador, **quiero** métricas agregadas del sistema en un solo comando **para**
verificar el criterio C8.

**Criterios de aceptación:**
- [ ] `stats` imprime, como mínimo:

| Grupo | Métricas |
|---|---|
| Sistema | uptime, frecuencia de CPU, quantum actual |
| Planificador | switches totales, switches/s, latencia de switch (media/mín/máx/σ) |
| Tareas | total, activas, terminadas, CPU ociosa % |
| Memoria | heap total/usado/libre, mayor bloque libre, fragmentación %, pico |
| Instrumentación | eventos emitidos, eventos perdidos, % perdidos |

- [ ] Los números son **coherentes** con `ps`, `free`, `health` y el dashboard
      (verificado comparando las cuatro salidas)
- [ ] `stats` reutiliza las fuentes existentes (`heap_stats`, estadísticas de `sched`,
      `trace stats`) — **no duplica contadores**
- [ ] `stats -r` reinicia los contadores acumulados (útil entre escenas de la demo)
- [ ] ✅ **`stats` tras `slice 200` vs `slice 1500` muestra diferencia clara en switches/s**
- [ ] Cabe en una pantalla de 80×25 (para que la captura sea legible)
- [ ] Formato objetivo:
```
sentinel:/$ stats
=== SISTEMA ===
  Uptime ................. 00:04:17.342
  Frecuencia CPU ......... 160 MHz
  Quantum actual ......... 10 ms

=== PLANIFICADOR ===
  Cambios de contexto .... 25 847
  Switches por segundo ... 100.4
  Latencia switch ........ media 11.2 us | min 9.8 | max 24.1 | sigma 1.4

=== TAREAS ===
  Total .................. 6 (5 activas, 1 terminada)
  CPU ociosa ............. 78.4 %

=== MEMORIA ===
  Heap ................... 98304 B total | 12480 usado | 85824 libre
  Mayor bloque libre ..... 84992 B
  Fragmentacion .......... 1.0 %   Pico de uso: 14208 B

=== INSTRUMENTACION ===
  Eventos emitidos ....... 51 694
  Eventos perdidos ....... 0  (0.00 %)
```

**Estimación:** 2 h

> 💡 **Aquí Sentinel supera la rúbrica.** La mayoría de proyectos mostrarán un contador de
> switches. Tú muestras **latencia de cambio de contexto medida en ciclos de CPU** y
> **eventos de traza perdidos reportados honestamente**. Eso es E07/E11 pagando dividendos.

---

### HU-E20-05 — Comando `exit` (lado kernel) ⭐ `[C9]`
**Como** evaluador, **quiero** un apagado ordenado **para** verificar que no quedan recursos
sin liberar (criterio C9).

**Criterios de aceptación:**
- [ ] `exit` termina **todas las tareas de usuario** (no `idle`, no `shell`, no `tracer`)
- [ ] Libera: pila de cada tarea, memoria del heap de cada tarea, descriptores abiertos
- [ ] Suelta los mutexes que las tareas poseían
- [ ] Detiene los escenarios de inyección activos (`inject stop` implícito)
- [ ] Drena el ring buffer de traza antes de terminar (no pierde eventos pendientes)
- [ ] Imprime un **resumen de liberación**:
```
sentinel:/$ exit
[exit] apagando Sentinel OS...
[exit] tareas de usuario terminadas ..... 5
[exit] pilas liberadas .................. 5  (20480 B)
[exit] bloques de heap liberados ........ 23  (3872 B)
[exit] mutexes liberados ................ 2
[exit] eventos de traza drenados ........ 418
[exit] heap: 85824 B libres (base: 85824 B)  >> SIN FUGAS
[exit] tareas restantes: idle, shell, tracer
[exit] sistema en reposo. Pulse 'reboot' para reiniciar.
```
- [ ] ✅ **`ps` tras `exit` muestra solo las tareas del sistema**
- [ ] ✅ **`free` tras `exit` coincide EXACTAMENTE con el estado base** → sin fugas
- [ ] Si quedara alguna fuga, `exit` **la reporta** en vez de ocultarla
- [ ] `exit` es idempotente: ejecutarlo dos veces no falla
- [ ] `exit` emite `EV_SHUTDOWN` para que el dashboard y el bridge lo sepan

**Estimación:** 2 h

> 🎯 **La línea `>> SIN FUGAS` comparando con el estado base es el equivalente honesto de
> `ps aux | grep defunct` vacío.** Y es más fuerte, porque es un número exacto en bytes, no
> una lista vacía.

---

### HU-E20-06 — Teardown del host (lado Linux) ⭐ `[C9]`
**Como** evaluador, **quiero** comprobar en otra terminal que no quedan procesos zombie
**para** verificar el criterio C9 de forma literal.

**Criterios de aceptación:**
- [ ] El kernel emite `EV_SHUTDOWN`; el bridge lo recibe y cierra limpiamente
- [ ] El bridge hace `flush` de SQLite y cierra el puerto serie antes de salir
- [ ] El launcher recoge los procesos hijos con `wait`/`waitpid` (o el equivalente de la
      shell), **sin dejar zombies**
- [ ] El dashboard se cierra limpiamente
- [ ] `build/minios.pids` se borra al terminar
- [ ] ✅ **En otra terminal, `ps aux | grep defunct` devuelve vacío** (solo la línea del
      propio `grep`, o nada con `grep [d]efunct`)
- [ ] ✅ **Verificado 10 veces consecutivas:** arrancar `./minios`, `exit`, comprobar que no
      hay zombies. 10/10.
- [ ] Si se mata el launcher con Ctrl+C, también recoge a sus hijos (trap/signal handler)
- [ ] Documentado el comando exacto a mostrar en el video:
      `ps aux | grep defunct` y además `ps -eo stat,pid,comm | grep '^Z'`

**Estimación:** 1,5 h

**Notas técnicas:**
> **Por qué esto funciona y es honesto:** el launcher, el bridge y el dashboard **sí son
> procesos reales de Linux**. Si el launcher terminara sin recoger a sus hijos, aparecerían
> como `<defunct>` en `ps aux`. Demostrar que no aparecen es demostrar **exactamente el
> concepto que el criterio evalúa**: que el padre recoge a sus hijos.
>
> El concepto de SO que hay que explicar: un proceso zombie es un hijo que terminó pero cuyo
> estado de salida nadie ha recogido con `wait()`. El padre es responsable. Si el padre
> muere antes, `init`/`systemd` lo adopta y lo recoge.

> ⚠️ **Nota sobre Windows:** si la demo se graba en Windows, `ps aux` no existe. Opciones:
> (a) grabar esa escena en WSL2, (b) usar `Get-Process` con el equivalente y **explicar la
> diferencia**. Recomendado: **WSL2**, para que el comando sea literalmente el de la rúbrica.
> Confirmar con Iker en qué sistema va a grabar.

---

## 5. Notas técnicas críticas

### Orden de implementación

```
1. HU-02 slice      → es el más simple y desbloquea C6 (el sustrato ya existe)
2. HU-03 ps QUANTUM → depende de slice; 1 hora
3. HU-04 stats      → unifica métricas que ya existen dispersas
4. HU-05 exit       → el más delicado: toca liberación de recursos
5. HU-06 teardown   → depende de exit (EV_SHUTDOWN)
6. HU-01 ./minios   → al final, porque orquesta todo lo anterior
```

> **HU-01 va al final a propósito.** El launcher orquesta el dashboard de E21, así que
> conviene escribirlo cuando ya se sepa cómo se arranca el dashboard.

### No duplicar métricas

`stats` **no debe mantener contadores propios**. Debe leer de las fuentes existentes:

| Métrica | Fuente existente |
|---|---|
| Heap | `heap_stats()` (E03) |
| Switches, latencia | estadísticas del scheduler (E02) + traza (E07) |
| Eventos emitidos/perdidos | `trace stats` (E07) |
| CPU ociosa | tarea `idle` (E02-HU-08) |

Si `stats` duplicara contadores, tarde o temprano darían números distintos que `ps` o
`free`, y **una demo con números incoherentes es peor que no tener `stats`**.

### El riesgo real de `exit`

`exit` toca liberación de recursos, que es donde viven los bugs peores. Reglas:
- Terminar tareas **una por una**, verificando el heap tras cada una
- No liberar la pila de una tarea desde esa misma tarea (usar la tarea del shell)
- Drenar la traza **antes** de terminar el `tracer`
- Si algo no se puede liberar, **reportarlo**, no ignorarlo

> ⚠️ **Si `exit` deja una fuga de 8 bytes y se reporta, es un resultado honesto.
> Si la oculta, es un problema de integridad.** La línea `>> SIN FUGAS` solo se imprime si
> el heap coincide exactamente.

---

## 6. Riesgos

| # | Riesgo | Mitigación |
|---|---|---|
| R20.1 | `slice` muy bajo (1 ms) colapsa el sistema por overhead de switch | Validar rango; probar 1 ms y documentar el comportamiento |
| R20.2 | `exit` deja fugas | Verificación byte a byte contra el estado base; reportar si no cuadra |
| R20.3 | `stats` da números incoherentes con `ps`/`free` | Leer de las fuentes existentes, no duplicar contadores |
| R20.4 | `ps aux` no existe en Windows | Grabar esa escena en WSL2 |
| R20.5 | El launcher deja huérfanos si falla a mitad | Trap de señales + limpieza en caso de error |

---

## 7. Definición de Hecho

- [ ] Las 6 HUs cumplen todos sus criterios
- [ ] ✅ `./minios` arranca los 3 componentes y entrega la consola `[C2]`
- [ ] ✅ `slice 200` y `slice 1500` funcionan y la diferencia es ≥5× en switches/s `[C6]`
- [ ] ✅ `ps` muestra la columna `QUANTUM` y todas las columnas están documentadas `[C5]`
- [ ] ✅ `stats` muestra métricas agregadas coherentes con `ps` y `free` `[C8]`
- [ ] ✅ `exit` libera todo y reporta `>> SIN FUGAS` `[C9]`
- [ ] ✅ `ps aux | grep defunct` vacío, verificado 10/10 veces `[C9]`
- [ ] Compila con `-Werror` sin warnings
- [ ] Validado **en placa**, no solo en host
- [ ] `help` de los 3 comandos nuevos escrito y claro
- [ ] `05-ESTADO.md` actualizado
- [ ] Commits con formato `E20-HU-NN: ...`

---

## 8. Referencias

- `09-RUBRICA-PROFESOR.md` — los criterios que esta épica satisface
- `kernel/include/sentinel/task.h` — `quantum_ticks` ya existente
- `kernel/core/sched.c:462-476` — donde se decrementa el quantum
- `kernel/shell/cmds.c:798-811` — tabla de comandos a extender
- Stevens, *Advanced Programming in the UNIX Environment* — cap. 8 (process control, zombies)

---

## 9. Bitácora

| Fecha | Horas | Qué se hizo | Aprendizaje |
|---|---|---|---|
| — | — | — | — |
