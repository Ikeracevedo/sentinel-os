# Sentinel OS — Arquitectura Técnica

> Complemento de `00-MASTER.md`. Aquí vive el detalle de ingeniería.
> **Última actualización:** 2026-09-20

---

## 1. Principios de diseño (no negociables)

| # | Principio | Consecuencia práctica |
|---|---|---|
| P1 | **El kernel no conoce la red.** | Cero TCP/IP, cero TLS, cero MQTT dentro del chip. La telemetría sale por UART. |
| P2 | **Observar nunca debe bloquear.** | Ninguna sonda toma un mutex. Ring buffer lock-free. Si se llena, se descarta y se cuenta. |
| P3 | **Prefiero perder datos antes que perturbar.** | `eventos_perdidos` siempre se reporta. Nunca se retransmite. |
| P4 | **La instrumentación debe poder desaparecer.** | `SENTINEL_TRACE=0` produce un binario sin una sola instrucción de traza. |
| P5 | **El observador no escribe estado del kernel.** | Core 1 solo lee. Flujo de datos unidireccional. |
| P6 | **Nada está hecho sin una forma de demostrarlo.** | Cada HU tiene criterios de aceptación verificables. |
| P7 | **Ausencia de datos también es un dato.** | Heartbeat incondicional desde el ISR. |
| P8 | **Prometer solo lo que el silicio permite.** | Sin MMU → no se habla de paginación ni memoria virtual. |

---

## 2. Estructura del repositorio

```
sentinel-os/
├── docs/                       ← documentación (este directorio)
│   ├── README.md
│   ├── 00-MASTER.md
│   ├── 01-ARQUITECTURA.md
│   ├── 02-ROADMAP.md
│   ├── 03-PROTOCOLO.md
│   ├── 04-DECISIONES-ADR.md
│   ├── 05-ESTADO.md
│   ├── 06-CONVENCIONES.md
│   ├── 07-GLOSARIO.md
│   └── tasks/                  ← una épica por archivo
│
├── kernel/                     ← el sistema operativo
│   ├── arch/xtensa/
│   │   ├── start.S             ← punto de entrada, init de pila, .bss
│   │   ├── context.S           ← cambio de contexto + window spill
│   │   ├── vectors.S           ← vector de interrupciones
│   │   └── cpu.h               ← CCOUNT, barreras, secciones críticas
│   ├── core/
│   │   ├── kmain.c             ← arranque del kernel
│   │   ├── sched.c/.h          ← planificador
│   │   ├── task.c/.h           ← TCB, creación/destrucción
│   │   ├── sync.c/.h           ← mutex, semáforos
│   │   ├── time.c/.h           ← tick, sleep, uptime
│   │   └── panic.c             ← manejo de excepciones
│   ├── mm/
│   │   ├── heap.c/.h           ← kmalloc/kfree, coalescencia
│   │   └── stats.c/.h          ← métricas, fragmentación
│   ├── fs/
│   │   ├── vfs.c/.h            ← árbol, resolución de rutas
│   │   └── ramfs.c/.h          ← backend en RAM
│   ├── shell/
│   │   ├── shell.c/.h          ← REPL, parser
│   │   └── cmds/               ← un archivo por comando
│   ├── trace/
│   │   ├── probe.h             ← macros de sonda (compilables a cero)
│   │   ├── ring.c/.h           ← ring buffer SPSC
│   │   └── events.h            ← formato de evento
│   ├── drivers/
│   │   ├── uart.c/.h
│   │   ├── timer.c/.h
│   │   └── gpio.c/.h
│   ├── inject/
│   │   └── faults.c/.h         ← inyector de fallos
│   └── include/
│       └── sentinel/           ← headers públicos
│
├── monitor/                    ← código del core 1 (E13)
│   └── monitor.c
│
├── tools/
│   ├── bridge/                 ← puente Python (E08)
│   │   ├── bridge.py
│   │   ├── protocol.py
│   │   └── sinks/              ← sqlite.py, mqtt.py, file.py
│   ├── testbench/              ← banco de pruebas (E11)
│   └── flash.ps1 / flash.sh
│
├── tests/                      ← tests en host (E17)
│   └── test_heap.c
│
├── cloud/                      ← AWS (E14)
│   ├── lambda/
│   └── schema.sql
│
├── linker.ld                   ← mapa de memoria
├── Makefile
├── .gitignore
└── README.md                   ← README de portafolio
```

---

## 3. Mapa de memoria (ESP32-WROOM-32)

| Región | Dirección | Tamaño | Uso en Sentinel |
|---|---|---|---|
| IRAM0 | `0x4008_0000` | 128 KB | Código del kernel, vectores, ISRs |
| DRAM0 | `0x3FFB_0000` | ~176 KB | `.data`, `.bss`, pilas, heap, ring buffer |
| DRAM1 / RTC | varias | — | No usado inicialmente |
| Flash SPI | `0x4000_0000` (mapeada) | 4 MB | Imagen del binario; FS persistente (E19) |

### Presupuesto de RAM objetivo

| Componente | Reserva |
|---|---|
| `.data` + `.bss` del kernel | ~20 KB |
| Pilas de tareas (8 tareas × 4 KB) | 32 KB |
| Pila de ISR | 4 KB |
| **Heap del sistema** (`kmalloc`) | **96 KB** |
| Ring buffer de traza (2048 × 16 B) | 32 KB |
| Buffers de UART (TX + RX) | 8 KB |
| Margen | resto |

> El FS (RAMFS) vive **dentro del heap**, no en región propia. Eso es intencional: hace que
> crear archivos consuma memoria visible en `free`, lo que produce demostraciones honestas.

### 3.1 Mapa de memoria implementado (`linker.ld`, HU-E01-02)

Símbolos exportados por el linker script y verificados con `xtensa-esp32-elf-nm` sobre el
binario real:

| Símbolo | Dirección | Significado |
|---|---|---|
| `_bss_start` | `0x3FFB_0000` | Inicio de `.bss` — rango a limpiar en el arranque (HU-E01-03) |
| `_bss_end` | `0x3FFB_0000` | Fin de `.bss` (coincide con el inicio: aún no hay variables globales) |
| `_heap_start` | `0x3FFB_0000` | Primer byte disponible para `kmalloc` (E03) |
| `_heap_end` | `0x3FFD_B000` | Último byte disponible para el heap — `_stack_top - 4 KB` |
| `_stack_top` | `0x3FFD_C000` | Tope de la pila principal (crece hacia direcciones menores) |

Layout de DRAM (`0x3FFB_0000`, 176 KB) de menor a mayor dirección:

```
0x3FFB_0000 ──► .data / .bss (kernel)
            ──► heap (kmalloc) — crece hacia arriba
0x3FFD_B000 ──► colchón de 4 KB reservado para la pila
0x3FFD_C000 ──► _stack_top — la pila crece hacia abajo desde aquí
```

> **Nota de diseño (temporal):** heap y pila comparten la misma región DRAM sin guarda
> intermedia. Con una sola tarea (pre-E02) no hay riesgo real de colisión. Cuando E02
> introduzca múltiples pilas de tareas, este esquema se revisa — probablemente cada tarea
> reciba su propia región fija de 4 KB en vez de compartir un tope único.

---

## 4. Modelo de tareas

### 4.1 TCB — Task Control Block

```c
typedef enum {
    TASK_READY, TASK_RUNNING, TASK_BLOCKED,
    TASK_SLEEPING, TASK_SUSPENDED, TASK_TERMINATED
} task_state_t;

typedef struct tcb {
    uint32_t        *sp;              // puntero de pila guardado (PRIMER campo: lo usa el asm)
    uint32_t        *stack_base;      // base de la pila (para free y canario)
    uint32_t         stack_size;
    uint32_t         canary;          // detección de desbordamiento
    uint8_t          tid;             // id de tarea
    char             name[16];
    task_state_t     state;
    uint8_t          priority;        // 0 = máxima
    uint8_t          base_priority;   // para herencia de prioridad (E18)
    uint32_t         quantum_left;
    uint64_t         wake_at_tick;    // para sleep()
    // --- estadísticas (materia prima de la instrumentación) ---
    uint64_t         cpu_cycles;      // ciclos acumulados en RUNNING
    uint32_t         switches_in;
    uint64_t         ready_since;     // para medir tiempo en cola
    uint64_t         max_ready_wait;  // peor caso observado → inanición
    // --- sincronización (materia prima del grafo de espera) ---
    struct mutex    *waiting_on;      // qué recurso espera  → arista del grafo
    struct mutex    *owned[MAX_OWNED];// qué recursos posee
    uint8_t          owned_count;
    // --- FS ---
    struct vnode    *cwd;             // directorio de trabajo
    struct tcb      *next;            // encadenado en colas
} tcb_t;
```

> **`sp` debe ser el primer campo.** El código en assembly accede a él con desplazamiento 0,
> lo que simplifica y acelera el cambio de contexto.

### 4.2 Estados y transiciones

```
        create()                    schedule()
   ────────────────► READY ◄────────────────── RUNNING
                      ▲   │  quantum agotado /     │
                      │   └───► apropiación ───────┘
          unlock() /  │                            │ mutex_lock() ocupado
          timeout  /  │                            ▼
          wake     │  └──────────────────────  BLOCKED
                   │                               │
                   │         sleep(ms)             │
                   └────────────────────────  SLEEPING
                                                   │ exit()
                                                   ▼
                                              TERMINATED
```

### 4.3 Política de planificación (MVP)

**Prioridades estáticas con Round-Robin dentro de cada nivel.**

- 8 niveles de prioridad (0 = máxima, 7 = idle).
- Una cola `READY` por nivel.
- El scheduler elige siempre la cola no vacía de mayor prioridad.
- Dentro del nivel, Round-Robin con quantum configurable (por defecto 10 ms).
- La tarea `idle` (prioridad 7) nunca termina y contabiliza CPU ociosa.

Tareas del sistema al arrancar:

| TID | Nombre | Prioridad | Función |
|---|---|---|---|
| 0 | `idle` | 7 | Contabiliza CPU ociosa |
| 1 | `shell` | 4 | REPL por UART |
| 2 | `tracer` | 6 | Drena ring buffer (hasta que E13 lo mueva a core 1) |
| 3+ | usuario | 3–5 | Tareas de demostración e inyección |

---

## 5. Gestión de memoria

### 5.1 Estructura del heap

Lista enlazada de bloques con cabecera, dentro de un arreglo estático:

```c
typedef struct {                 // 20 B (igual en la placa y en el PC)
    uint32_t magic;              // 0x5E4715A1 — detección de corrupción
    uint32_t size;               // payload, canario (4 B finales) incluido
    uint32_t next;               // OFFSET en la arena del bloque siguiente (no puntero)
    uint32_t prev;               // OFFSET del anterior
    uint8_t  free;
    uint8_t  owner_tid;          // quién lo pidió → contabilidad por tarea
    uint16_t reserved;
    // payload va inmediatamente después
} block_t;
```

> **Implementación real (E03, ADR-016):** los enlaces son offsets para que `heap.c` compile
> idéntico en el host y se pruebe con `make test`; los recorridos completos se parten en
> tandas de 32 bloques para acotar el tiempo con interrupciones apagadas (~51 µs).

- **Estrategia:** first-fit (best-fit y buddy quedan para E18, como comparativa).
- **Split:** si el bloque encontrado es suficientemente mayor, se parte.
- **Coalescencia:** al liberar, se fusiona con vecinos libres (adelante y atrás).
- **Canario:** patrón al final del payload, verificado en `kfree`.

### 5.2 Métricas expuestas

```c
typedef struct {
    uint32_t total;
    uint32_t used;               // total - free (incluye TODAS las cabeceras)
    uint32_t free;
    uint32_t largest_free_block;
    uint32_t block_count;
    uint32_t free_block_count;
    uint32_t peak_used;          // marca de agua
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t double_free_count;  // dobles kfree detectados (no fatales)
    uint32_t walk_restarts;      // recorridos reiniciados por cambios concurrentes
} heap_stats_t;
```

**Índice de fragmentación:**

```
frag = 1 − (largest_free_block / free)      ∈ [0, 1]
```

> Interpretación: `free = 10 KB` pero `largest_free_block = 200 B` → `frag = 0.98`.
> Hay memoria, pero no sirve. **Esto es lo que `free` "normal" no te dice, y por eso
> `free` en Sentinel sí lo muestra.**

---

## 6. Sistema de archivos (VFS + RAMFS)

### 6.1 Nodo

```c
typedef enum { VN_FILE, VN_DIR } vnode_type_t;

typedef struct vnode {
    char            name[32];
    vnode_type_t    type;
    uint32_t        size;          // bytes (archivo) o #hijos (directorio)
    uint8_t         perms;         // rwx simbólicos
    uint64_t        created_tick;
    struct vnode   *parent;
    struct vnode   *first_child;   // si es DIR
    struct vnode   *next_sibling;
    uint8_t        *data;          // si es FILE — kmalloc'd
    uint32_t        capacity;
} vnode_t;
```

Árbol n-ario con lista de hermanos. Simple, suficiente, y **el consumo de memoria es
visible en `free`** — lo que permite demos honestas ("creo 50 archivos y mira el heap").

### 6.2 Resolución de rutas

```
resolve(path, cwd):
    if path[0] == '/':  actual = raíz
    else:               actual = cwd
    para cada componente separado por '/':
        si componente == "."   → continuar
        si componente == ".."  → actual = actual->parent (raíz si NULL)
        si no                  → buscar hijo con ese nombre; si no existe → ENOENT
    devolver actual
```

### 6.3 Árbol precargado al arrancar

```
/
├── bin/          (archivos simbólicos que representan comandos)
├── etc/
│   └── motd      "Sentinel OS v0.1 — built <fecha>"
├── home/
│   └── iker/
│       └── readme.txt
├── proc/         (archivos virtuales — E05 avanzado)
│   ├── tasks
│   ├── meminfo
│   └── uptime
└── tmp/
```

> `/proc` es un guiño directo a Linux y **demuestra que entiendes que en Unix "todo es un
> archivo"**. `cat /proc/meminfo` mostrando el estado real del heap es un momento fuerte de
> la demo.

---

## 7. Instrumentación

### 7.1 Formato de evento (16 bytes, alineado)

```c
typedef struct __attribute__((packed)) {
    uint32_t cycles;      // CCOUNT truncado a 32 bits
    uint8_t  type;        // EV_SWITCH, EV_ALLOC, EV_LOCK, EV_HEARTBEAT...
    uint8_t  tid;         // tarea implicada
    uint16_t seq;         // secuencia local
    uint32_t arg0;        // depende del tipo
    uint32_t arg1;
} trace_event_t;          // 16 bytes exactos
```

### 7.2 Ring buffer SPSC lock-free

```c
#define RING_SIZE 2048            // potencia de 2 → módulo por máscara
static volatile uint32_t   head;  // SOLO escribe el productor (core 0)
static volatile uint32_t   tail;  // SOLO escribe el consumidor
static trace_event_t       buf[RING_SIZE];
static volatile uint32_t   dropped;
```

**Escritura (productor):**
```c
uint32_t h = head;
if (((h + 1) & (RING_SIZE-1)) == tail) { dropped++; return; }  // lleno
buf[h] = ev;                                   // (A) escribir dato
__atomic_thread_fence(__ATOMIC_RELEASE);       // BARRERA — crítica
head = (h + 1) & (RING_SIZE-1);                // (B) publicar índice
```

> ⚠️ **Sin la barrera entre (A) y (B), el consumidor puede ver un índice nuevo apuntando a
> basura.** Esto es semántica *release/acquire* y es el punto más sutil del proyecto.

**Por qué no hacen falta atomics:** con un solo productor y un solo consumidor, cada índice
tiene un único escritor. No hay carrera. Solo hace falta ordenamiento.

### 7.3 Sondas compilables a cero

```c
#if SENTINEL_TRACE
  #define PROBE_SWITCH(out, in, cyc)  trace_emit(EV_SWITCH, out, in, cyc)
#else
  #define PROBE_SWITCH(out, in, cyc)  ((void)0)
#endif
```

Con `SENTINEL_TRACE=0` el binario no contiene **ni una sola instrucción** de traza. Eso es
lo que permite la medición ON/OFF de E11.

### 7.4 Heartbeat

El ISR del timer emite `EV_HEARTBEAT` cada N ticks **incondicionalmente**, aunque no pase
nada más. El consumidor declara el sistema muerto si pasan `3×N` sin latido.

> **Esto resuelve el problema del silencio:** sin heartbeat, "sistema bloqueado" y "sistema
> tranquilo" producen exactamente la misma señal (buffer vacío).

---

## 8. Detección de deadlock — grafo de espera

### 8.1 Construcción

De los campos `waiting_on` y `owned[]` del TCB se deriva el grafo:

```
arista  A → B   ⟺   A.waiting_on == m   ∧   m.owner == B
```

### 8.2 Detección

DFS con marcado de tres colores (blanco/gris/negro). **Una arista hacia un nodo gris es un
ciclo → deadlock.** Complejidad O(V+E); con ≤16 tareas es instantáneo.

### 8.3 Dónde corre

| Fase | Ubicación | Razón |
|---|---|---|
| MVP (E10) | Core 0, desde el ISR del timer cada 500 ms | El ISR no puede ser bloqueado por una tarea |
| E13 | Core 1, sobre el estado leído del ring buffer | Observador totalmente aislado |
| E14 | También en la nube, sobre la traza | Permite análisis histórico |

> **Regla:** el detector **nunca toma un mutex**. Si lo hiciera, podría participar en el
> deadlock que intenta detectar.

---

## 9. Arquitectura dual-core (E13)

### 9.1 Reparto

| | Core 0 (PRO_CPU) | Core 1 (APP_CPU) |
|---|---|---|
| Software | Kernel Sentinel completo | Monitor bare-metal (bucle simple) |
| Escribe en el ring | ✅ (head) | ❌ |
| Lee del ring | ❌ | ✅ (tail) |
| Escribe estado del kernel | ✅ | ❌ **nunca** |
| Timestamps | ✅ toma todos | ❌ solo transporta |

### 9.2 Arranque de core 1

Al encender, **core 1 está en reset**. Core 0 debe:

1. Escribir la dirección de entrada en `DPORT_APPCPU_CTRL_D`
2. Habilitar el reloj (`DPORT_APPCPU_CTRL_B`)
3. Liberar el reset (`DPORT_APPCPU_CTRL_A`)
4. Core 1 arranca con su propia pila desde el punto indicado

### 9.3 Los cuatro problemas y sus soluciones

| Problema | Estado | Solución |
|---|---|---|
| **Coherencia de caché** | ✅ No aplica | La SRAM interna del ESP32 se accede sin caché de datos. La caché solo cubre flash/PSRAM. |
| **Reordenamiento** | ⚠️ Sí aplica | Barreras release/acquire + `volatile`. Ver §7.2. |
| **Atomicidad** | ✅ No hace falta | SPSC: cada índice tiene un único escritor. Sin CAS, sin locks. |
| **Relojes distintos** | ⚠️ Sí aplica | `CCOUNT` es por núcleo. **Todos los timestamps se toman en core 0.** Core 1 solo transporta. |

### 9.4 El experimento estrella

| Configuración | Métrica a medir |
|---|---|
| A — sin instrumentación (`TRACE=0`) | latencia base de switch, % CPU idle |
| B — monitor en core 0 (tarea `tracer`) | latencia, % CPU idle, eventos perdidos |
| C — monitor en core 1 (AMP) | latencia, % CPU idle, eventos perdidos |

> **(B − A)** = costo total de observar.
> **(C − A)** = costo residual tras aislar → **contención de bus de memoria**.
> Ese residuo es el límite físico de la observabilidad en esta arquitectura.

---

## 10. Presupuesto de ancho de banda del UART

```
921600 baudios ÷ 10 bits/byte (8N1) = 92.160 bytes/s
Trama = 10 B cabecera/cola + 16 B evento = 26 B
→ máximo ≈ 3.540 eventos/s   (o ~5.760 si se agrupan varios eventos por trama)
```

Carga esperada:

| Fuente | Eventos/s | Notas |
|---|---|---|
| Context switch (quantum 10 ms) | ~100–300 | Cabe holgado |
| Heartbeat | 10 | Despreciable |
| alloc/free bajo demo | 50–500 | Cabe |
| lock/unlock bajo carga | **1.000–5.000** | ⚠️ Aquí se satura |

**Mitigaciones obligatorias (E07):**
1. Agrupar varios eventos por trama (amortiza la cabecera).
2. Filtros por tipo activables en caliente (`trace filter lock off`).
3. Muestreo 1-de-N para eventos de alta frecuencia.
4. **Siempre** reportar `eventos_perdidos`.

> **Hacer esta cuenta ANTES de programar.** La mayoría descubre la saturación cuando la
> demo se cae en vivo.

---

## 11. Flujo de datos completo

```
1. Ocurre un context switch en core 0
2. La sonda lee CCOUNT antes y después          (6.25 ns/ciclo @160 MHz)
3. Escribe trace_event_t en el ring buffer      (lock-free, ~20 ciclos)
   └─ si está lleno: dropped++ y sigue          (NUNCA bloquea)
4. El monitor (tarea `tracer` o core 1) drena el buffer
5. Serializa en tramas binarias con seq + CRC16
6. TX por UART @921600
7. El puente Python decodifica, valida CRC, detecta huecos por seq
8. Escribe a SQLite (offline) y/o publica MQTT a AWS IoT Core
9. Motor de análisis: grafo de espera, fragmentación, regresión de fuga
10. Dashboard muestra en vivo + alertas
    └─ botón "inject deadlock" → MQTT → puente → UART → kernel
```

---

## 12. Dos caminos para el bring-up (decisión de E01)

| | **Camino A — Bare-metal puro** | **Camino B — Híbrido (plan de rescate)** |
|---|---|---|
| Qué | Linker script propio, `start.S` propio, sin ESP-IDF | ESP-IDF solo como bootloader y toolchain; el kernel toma el control y desactiva FreeRTOS |
| Pureza | ✅ Total — "sin ningún SO por debajo" | ⚠️ Matizada, hay que explicarla |
| Riesgo | 🔴 Alto (imagen, cabecera, ROM bootloader) | 🟢 Bajo |
| Tiempo | 10–16 h | 3–5 h |
| **Decisión** | **Intentar primero, con timebox de 12 h** | **Si se excede el timebox, migrar** |

> Ver `04-DECISIONES-ADR.md` → **ADR-002**. Lo importante no es cuál se elija, sino poder
> defender la elección con sus consecuencias.
