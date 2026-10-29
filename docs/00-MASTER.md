# SENTINEL OS — Documento Maestro

> **Este es el documento de referencia central del proyecto.**
> Contiene todo el contexto necesario para que cualquier persona o IA entienda el proyecto
> completo y pueda continuarlo sin haber participado en las conversaciones previas.
>
> **Última actualización:** 2026-09-20
> **Versión del documento:** 1.0

---

## ÍNDICE

1. [Identidad del proyecto](#1-identidad-del-proyecto)
2. [El problema que resuelve](#2-el-problema-que-resuelve)
3. [La tesis](#3-la-tesis)
4. [Qué es y qué NO es](#4-qué-es-y-qué-no-es)
5. [El pitch](#5-el-pitch)
6. [Requisitos del profesor (obligatorios)](#6-requisitos-del-profesor-obligatorios)
7. [Hardware confirmado](#7-hardware-confirmado)
8. [Arquitectura general](#8-arquitectura-general)
9. [Catálogo completo de funcionalidades](#9-catálogo-completo-de-funcionalidades)
10. [Mapa de épicas](#10-mapa-de-épicas)
11. [Criterios de éxito](#11-criterios-de-éxito)
12. [Entregables finales](#12-entregables-finales)
13. [Beneficios y público objetivo](#13-beneficios-y-público-objetivo)
14. [Estado del arte y diferenciación](#14-estado-del-arte-y-diferenciación)
15. [Riesgos](#15-riesgos)
16. [Restricciones y contexto del autor](#16-restricciones-y-contexto-del-autor)
17. [Preguntas abiertas](#17-preguntas-abiertas)

---

## 1. Identidad del proyecto

| Campo | Valor |
|---|---|
| **Nombre** | Sentinel OS |
| **Subtítulo** | *A self-observing bare-metal microkernel for ESP32* |
| **Repositorio** | `sentinel-os` |
| **Autor** | Iker Acevedo — Ingeniería de Sistemas e Informática, UPB |
| **Materia** | Sistemas Operativos (2º semestre) |
| **Lenguajes** | C (C11) + Assembly Xtensa + Python (herramientas de host) |
| **Licencia sugerida** | MIT |

### Descripción de una línea (para GitHub / LinkedIn)

> Microkernel bare-metal de doble núcleo para ESP32 con shell tipo Unix, sistema de archivos,
> planificación apropiativa e instrumentación de bajo overhead que diagnostica deadlocks,
> fugas de memoria y fragmentación en tiempo real.

---

## 2. El problema que resuelve

Hay una asimetría incómoda en el software de sistemas:

> **El software que más necesitamos entender es el que menos podemos observar.**

Cuando una aplicación web falla, existen logs, APM, trazas distribuidas, profilers y un
debugger que se puede pausar. Cuando falla un **kernel**, casi no hay nada — porque
cualquier herramienta que lo observe corre *encima* de él o lo *detiene*. Y un kernel
detenido en un breakpoint ya no es el kernel que se quería estudiar: los timers siguen
corriendo, las interrupciones se pierden y el comportamiento temporal —que es justo lo que
fallaba— desaparece.

Esto no es un problema académico. Es la razón de casos históricos reales:

- **Mars Pathfinder (1997):** reinicios en Marte por una **inversión de prioridad** en
  VxWorks. Lo salvaron porque el sistema tenía trazabilidad remota y pudieron diagnosticar
  y parchear a 190 millones de kilómetros. Sin instrumentación, la misión se pierde.
- **Therac-25 (1985–87):** muertes por una condición de carrera en el scheduler, invisible
  desde afuera.
- **Toyota unintended acceleration (2009–13):** el peritaje giró en torno a desbordamiento
  de pila y corrupción de memoria en firmware, y a la imposibilidad de demostrarlo sin
  instrumentación adecuada.

La industria resolvió esto inventando una categoría entera: **la instrumentación de kernel**.
`DTrace` (Sun, 2005), `ftrace` y `eBPF` (Linux), `ETW` (Windows), `Tracealyzer` y
`SystemView` (embebido). Todos responden la misma pregunta:

> ¿Cómo hago que un sistema se explique a sí mismo **mientras sigue vivo**?

---

## 3. La tesis

> **Sentinel OS es esa pregunta, reducida a su mínima expresión ejecutable.**
>
> No es un sistema operativo al que se le agregaron métricas. Es un sistema operativo
> **cuyo propósito de diseño es ser observable**, construido desde el primer byte del
> arranque para explicarse a sí mismo — y con un núcleo físico completo dedicado
> exclusivamente a observar al otro.

### Corolario: el efecto observador

Toda herramienta de observación perturba lo que observa. La mayoría de los proyectos
ignoran ese costo. **Sentinel lo mide.**

Ese es el resultado central del proyecto y su mayor diferenciador académico:

> "Mi instrumentación cuesta N ciclos por evento y X% de CPU. Cuando moví el observador al
> segundo núcleo, ese costo bajó a Y% — pero **no llegó a cero**, porque ambos núcleos
> comparten el bus de memoria. Ese residuo es el límite físico de la observabilidad en esta
> arquitectura, y aquí está medido."

---

## 4. Qué es y qué NO es

### ES

- Un **microkernel bare-metal** (~4.000–6.000 líneas de C + assembly) que corre sin ningún
  SDK ni RTOS por debajo.
- Con **multitarea apropiativa** real (interrupción de timer + cambio de contexto en
  assembly, incluyendo *window spill* de Xtensa).
- Con **gestión dinámica de memoria** propia (heap, allocator, coalescencia, métricas de
  fragmentación).
- Con **primitivas de sincronización** propias (mutex, semáforos, colas de espera, registro
  de propiedad).
- Con **sistema de archivos** jerárquico mínimo y **shell interactivo** con comandos tipo
  Unix (`ls`, `cd`, `pwd`, `cat`, `mkdir`, `rm`, `ps`, `free`, …).
- **Instrumentado** en sus rutas críticas con sondas de costo medido y compilables a cero.
- En **arquitectura AMP** (core 0 = sistema, core 1 = observador dedicado).
- Conectado a un **pipeline de telemetría** con protocolo binario propio → puente → nube.
- Con un **motor de diagnóstico** de dos niveles: determinista (teoría de grafos) y
  predictivo (series temporales).
- Con un **inyector de fallos** que hace la demostración reproducible y verificable.

### NO ES

- Un SO de propósito general (no hay multiusuario, ni red en el kernel, ni POSIX completo).
- Un SO con **memoria virtual ni paginación** — el ESP32 **no tiene MMU**. Esto se declara
  explícitamente en la sustentación antes de que lo pregunten.
- Un wrapper sobre FreeRTOS o ESP-IDF.
- Un proyecto de Machine Learning con un microcontrolador de excusa. La parte predictiva es
  modesta, honesta y secundaria.

---

## 5. El pitch

### 5.1 Versión ascensor (30 segundos)

> "Construí un sistema operativo que **se observa a sí mismo**. Es un microkernel
> bare-metal para microcontrolador —con su propio planificador, gestor de memoria, sistema
> de archivos y shell tipo Unix, sin ningún SO por debajo— pero con sondas instrumentadas
> dentro del núcleo que miden, en ciclos de reloj, cuánto tarda cada cambio de contexto,
> cómo se fragmenta el heap y quién está esperando a quién.
>
> Esa telemetría sale del chip y llega a un motor de análisis que detecta **deadlocks,
> fugas de memoria e inanición de tareas antes de que el sistema colapse**. Y para
> probarlo, el kernel incluye un inyector de fallos: puedo provocar un deadlock real en
> vivo y ver cómo el sistema lo diagnostica solo."

### 5.2 Versión larga (sustentación)

Se desarrolla a partir de las secciones 2, 3 y 14 de este documento. Estructura sugerida:

1. El problema (sección 2) — 2 min
2. La tesis (sección 3) — 1 min
3. Demo en vivo: shell funcionando, `ps`, `free`, `ls` — 2 min
4. Demo en vivo: `inject deadlock` → detección en pantalla — 2 min
5. Los números: overhead medido, latencia de switch, eventos perdidos — 3 min
6. El efecto observador: core 0 vs core 1 — 2 min
7. Qué falta y por qué (honestidad de alcance) — 1 min

### 5.3 Frase de cierre

> "Un proyecto que funciona demuestra que sabes programar.
> Un proyecto que se mide a sí mismo demuestra que sabes de sistemas."

---

## 6. Requisitos del profesor (OBLIGATORIOS)

> ⚠️ **Estos requisitos tienen prioridad absoluta sobre cualquier otra funcionalidad del
> proyecto.** Si hay que sacrificar algo, se sacrifica lo demás, nunca esto.

Según lo indicado en clase, el proyecto final debe ser:

1. **Un sistema operativo minimalista.**
2. Que pueda **hacer las cosas básicas de un sistema operativo**.
3. Que tenga **programas / comandos tipo Linux** ejecutables desde el sistema:
   `ls`, `cd`, `pwd`, y similares.

### Traducción a requisitos técnicos

| Requisito del profesor | Qué implica | Épica |
|---|---|---|
| "Sistema operativo" | Planificación de tareas + gestión de memoria propias | E02, E03 |
| "Minimalista" | Bare-metal, sin SDK, kernel pequeño y comprensible | E01 |
| "Cosas básicas de un SO" | Concurrencia, sincronización, E/S | E02, E03, E04 |
| "Comandos `ls`, `cd`, `pwd`" | **Sistema de archivos jerárquico** | **E05** |
| "Comandos tipo Linux" | **Shell / intérprete de comandos** | **E06** |

> **Nota crítica:** E05 (VFS) y E06 (Shell) NO estaban en la planificación inicial del
> proyecto y fueron añadidas al detectar este requisito. Son **innegociables**.

---

## 7. Hardware confirmado

| Campo | Valor |
|---|---|
| Placa | ESP32 DevKit (serigrafía: `ESP32 WIFI+BT SoC Inside ISM2.4G 802.11/b/g/n`) |
| Módulo | ESP32-WROOM-32 (o compatible) |
| SoC | ESP32-D0WD |
| Arquitectura | **Xtensa LX6, 32-bit, DOBLE NÚCLEO** |
| Frecuencia | Hasta 240 MHz (configurable: 80/160/240) |
| SRAM | 520 KB interna (DRAM + IRAM) |
| Flash | Típicamente 4 MB SPI externa |
| MMU | **NO tiene** (hay MMU de flash/caché, pero no memoria virtual de procesos) |
| Protección de memoria | Limitada (no hay MPU RISC-V PMP; existe control de acceso DPORT) |
| Contador de ciclos | Registro especial `CCOUNT` (por núcleo) |
| Puente USB-Serie | CP2102 o CH340 según la placa |

### Consecuencias de este hardware

| Hecho | Consecuencia para el proyecto |
|---|---|
| ✅ **Doble núcleo** | La arquitectura AMP (E13) es **posible**. Core 0 = sistema, Core 1 = observador. |
| ⚠️ **Xtensa LX6** | El cambio de contexto usa **ventanas de registros (register windows)**. Hay que implementar *window spill*. Es el punto técnico más difícil del proyecto. |
| ⚠️ **Sin MMU** | No hay paginación ni memoria virtual. El proyecto **no puede** ofrecer aislamiento por espacios de direcciones. Se declara explícitamente. |
| ⚠️ **WiFi es blob propietario** | El driver WiFi de Espressif depende de FreeRTOS. **No se usará WiFi desde el kernel.** La telemetría sale por UART a un puente. Ver ADR-003. |
| ✅ **SRAM interna sin caché de datos** | El ring buffer compartido entre núcleos **no tiene problema de coherencia de caché**. Solo hace falta ordenamiento de memoria (barreras). Ver ADR-005. |
| ⚠️ **`CCOUNT` es por núcleo** | Los timestamps deben tomarse **siempre en core 0**, o usar un timer global compartido. Ver ADR-006. |

---

## 8. Arquitectura general

> El detalle completo está en `01-ARQUITECTURA.md`. Aquí va la vista de 10.000 pies.

```
╔═══════════════════════════════════════════════════════════════════════╗
║           L5 · PLANO DE ANÁLISIS   (AWS o modo offline local)         ║
║  Ingesta → persistencia → detección determinista → predicción         ║
║  → alertas → dashboard                                    [E14,E15,E16]║
╚═══════════════════════════════════════════════════════════════════════╝
        ▲ MQTT/TLS (nube)  ó  SQLite (offline)      │ comandos ▼
╔═══════════════════════════════════════════════════════════════════════╗
║           L4 · PUENTE (PC)                                    [E08]   ║
║  Decodifica tramas · valida CRC · detecta huecos · UART↔JSON/MQTT     ║
╚═══════════════════════════════════════════════════════════════════════╝
        ▲ UART 921600 8N1, protocolo binario propio  │ comandos ▼
╔═══════════════════════════════════════════════════════════════════════╗
║  CORE 1 — OBSERVADOR (bare-metal, sin SO)                     [E13]   ║
║  Drena ring buffer · serializa · CRC · TX UART                        ║
║  Vigilante de heartbeat · NUNCA escribe estado del kernel             ║
╚═══════════════════════════════════════════════════════════════════════╝
        ▲ lee (tail)
   ┌────────────────────────────────────────────────┐
   │  RING BUFFER SPSC · SRAM interna · lock-free   │  [E07]
   │  barreras release/acquire · contador perdidos  │
   └────────────────────────────────────────────────┘
        ▲ escribe (head)
╔═══════════════════════════════════════════════════════════════════════╗
║  CORE 0 — SISTEMA (Sentinel Kernel)                                   ║
║  ┌─────────────────────────────────────────────────────────────────┐  ║
║  │ SONDAS  on_switch · on_alloc/free · on_lock/unlock · heartbeat  │  ║
║  │         compilables a coste cero (flag SENTINEL_TRACE)   [E07]  │  ║
║  └─────────────────────────────────────────────────────────────────┘  ║
║  ┌──────────┬──────────┬──────────┬──────────┬──────────┬─────────┐   ║
║  │ SHELL    │  VFS     │SCHEDULER │ MEMORIA  │ SINCRON. │INYECTOR │   ║
║  │ ls,cd,   │ árbol de │ TCB,colas│ heap     │ mutex    │de fallos│   ║
║  │ pwd,cat  │ archivos │apropiativ│ allocator│ semáforos│5 escen. │   ║
║  │ ps,free  │ RAMFS    │prioridad │coalescen.│ ownership│         │   ║
║  │  [E06]   │  [E05]   │  [E02]   │  [E03]   │  [E04]   │  [E09]  │   ║
║  └──────────┴──────────┴──────────┴──────────┴──────────┴─────────┘   ║
║  CONTEXT SWITCH (assembly Xtensa · window spill)              [E02]   ║
╚═══════════════════════════════════════════════════════════════════════╝
╔═══════════════════════════════════════════════════════════════════════╗
║  L0 · HARDWARE ESP32 · sin SDK · linker script propio         [E01]   ║
║  Timer · UART · GPIO · CCOUNT · vector de interrupciones              ║
╚═══════════════════════════════════════════════════════════════════════╝
```

---

## 9. Catálogo completo de funcionalidades

> 148 funcionalidades identificadas. Prioridad: 🔴 núcleo innegociable · 🟡 alto valor · 🟢 lujo

### 9.1 Arranque y plataforma `[E01]`

| # | Funcionalidad | Pri |
|---|---|---|
| 1 | Arranque bare-metal sin ESP-IDF ni FreeRTOS | 🔴 |
| 2 | Linker script propio (.text/.data/.bss/.rodata, mapa de RAM) | 🔴 |
| 3 | Código de arranque en assembly (pila, limpieza .bss, salto a `kmain`) | 🔴 |
| 4 | Imagen flasheable con cabecera válida para el ROM bootloader | 🔴 |
| 5 | Driver UART propio (TX bloqueante → TX/RX por interrupción con buffer) | 🔴 |
| 6 | Driver de timer para tick del sistema | 🔴 |
| 7 | Habilitación y lectura del contador de ciclos `CCOUNT` | 🔴 |
| 8 | Vector de interrupciones propio y gestión de niveles de IRQ | 🔴 |
| 9 | `kprintf` mínimo (sin libc) | 🔴 |
| 10 | Manejador de excepciones (panic con volcado de registros) | 🔴 |
| 11 | Driver GPIO (validación de tiempos con osciloscopio) | 🟡 |
| 12 | Configuración de frecuencia de CPU | 🟡 |
| 13 | Watchdog de hardware configurado y alimentado | 🟢 |

### 9.2 Planificación `[E02]`

| # | Funcionalidad | Pri |
|---|---|---|
| 14 | Estructura TCB (contexto, pila, estado, prioridad, estadísticas) | 🔴 |
| 15 | Pilas independientes por tarea, tamaño configurable | 🔴 |
| 16 | Cambio de contexto en assembly + **window spill Xtensa** | 🔴 |
| 17 | Planificación cooperativa (`yield`) | 🔴 |
| 18 | Planificación **apropiativa** por interrupción de timer | 🔴 |
| 19 | Round-Robin con quantum configurable | 🔴 |
| 20 | Prioridades estáticas con apropiación inmediata | 🔴 |
| 21 | Estados completos (READY/RUNNING/BLOCKED/SUSPENDED/TERMINATED) | 🔴 |
| 22 | Tarea idle con contabilidad de CPU ociosa | 🔴 |
| 23 | API de creación/destrucción de tareas en runtime | 🔴 |
| 24 | `sleep(ms)` con lista de temporizadores ordenada | 🔴 |
| 25 | Estadísticas por tarea (tiempo CPU, #switches, tiempo en cola) | 🔴 |
| 26 | Detección de desbordamiento de pila (canario por tarea) | 🟡 |
| 27 | Envejecimiento (aging) para prevenir inanición | 🟡 |
| 28 | Políticas intercambiables en caliente (RR / prioridad / EDF) | 🟢 |
| 29 | Herencia de prioridad en mutex | 🟢 |
| 30 | Afinidad de núcleo | 🟢 |

### 9.3 Gestión de memoria `[E03]`

| # | Funcionalidad | Pri |
|---|---|---|
| 31 | Heap propio sobre región estática, con metadatos por bloque | 🔴 |
| 32 | `kmalloc` / `kfree` funcionales | 🔴 |
| 33 | Estrategia first-fit | 🔴 |
| 34 | Coalescencia de bloques libres adyacentes | 🔴 |
| 35 | Métricas: total, libre, usado, mayor bloque libre, #bloques | 🔴 |
| 36 | **Índice de fragmentación** `1 − (mayor_libre / total_libre)` | 🔴 |
| 37 | Volcado del mapa de heap por comando | 🔴 |
| 38 | Canarios de integridad (detección de corrupción) | 🟡 |
| 39 | Contabilidad de memoria **por tarea** | 🟡 |
| 40 | Marcas de agua (peak usage histórico) | 🟡 |
| 41 | Pool allocator de tamaño fijo (para estructuras del kernel) | 🟡 |
| 42 | Comparativa experimental first-fit vs best-fit vs buddy | 🟢 |
| 43 | Buddy allocator como segunda implementación | 🟢 |

### 9.4 Sincronización `[E04]`

| # | Funcionalidad | Pri |
|---|---|---|
| 44 | Mutex con cola de espera y registro de propietario | 🔴 |
| 45 | Semáforos contadores | 🔴 |
| 46 | Secciones críticas (deshabilitar/restaurar interrupciones) | 🔴 |
| 47 | **Registro global "quién posee qué / quién espera qué"** | 🔴 |
| 48 | Timeouts en adquisición de mutex | 🟡 |
| 49 | Colas de mensajes entre tareas | 🟡 |
| 50 | Spinlocks inter-núcleo (instrucción `S32C1I`) | 🟡 |
| 51 | Variables de condición / grupos de eventos | 🟢 |

### 9.5 Sistema de archivos `[E05]` ← **REQUISITO DEL PROFESOR**

| # | Funcionalidad | Pri |
|---|---|---|
| 52 | Estructura de nodo (inodo simplificado): tipo, nombre, tamaño, datos | 🔴 |
| 53 | Árbol jerárquico de directorios con raíz `/` | 🔴 |
| 54 | Directorio de trabajo actual (CWD) por shell | 🔴 |
| 55 | Resolución de rutas absolutas (`/a/b/c`) | 🔴 |
| 56 | Resolución de rutas relativas (`.`, `..`, `nombre`) | 🔴 |
| 57 | Crear directorio | 🔴 |
| 58 | Crear archivo | 🔴 |
| 59 | Escribir / leer contenido de archivo | 🔴 |
| 60 | Borrar archivo y directorio (con verificación de vacío) | 🔴 |
| 61 | Listar contenido de directorio | 🔴 |
| 62 | Metadatos: tamaño, tipo, permisos simbólicos | 🔴 |
| 63 | RAMFS: el FS vive en heap, se pierde al reiniciar | 🔴 |
| 64 | API tipo POSIX: `open`, `read`, `write`, `close`, `seek` | 🟡 |
| 65 | Descriptores de archivo por tarea | 🟡 |
| 66 | FS precargado con contenido de demostración al arrancar | 🟡 |
| 67 | Archivos virtuales tipo `/proc` (`/proc/tasks`, `/proc/meminfo`) | 🟡 |
| 68 | Persistencia en flash SPI (sobrevive reinicio) | 🟢 |
| 69 | Enlaces simbólicos / duros | 🟢 |
| 70 | Permisos reales con verificación | 🟢 |

### 9.6 Shell `[E06]` ← **REQUISITO DEL PROFESOR**

| # | Funcionalidad | Pri |
|---|---|---|
| 71 | REPL sobre UART con prompt | 🔴 |
| 72 | Lectura de línea con eco y borrado (backspace) | 🔴 |
| 73 | Parser de comandos con argumentos (tokenización) | 🔴 |
| 74 | Tabla de comandos registrables | 🔴 |
| 75 | `help` — lista de comandos | 🔴 |
| 76 | `ls [ruta]` — listar directorio | 🔴 |
| 77 | `cd <ruta>` — cambiar directorio | 🔴 |
| 78 | `pwd` — mostrar ruta actual | 🔴 |
| 79 | `cat <archivo>` — mostrar contenido | 🔴 |
| 80 | `mkdir <nombre>` — crear directorio | 🔴 |
| 81 | `touch <nombre>` / `write <archivo> <texto>` — crear/escribir | 🔴 |
| 82 | `rm <ruta>` — borrar | 🔴 |
| 83 | `echo <texto>` | 🔴 |
| 84 | `ps` — listar tareas con estado, prioridad y CPU | 🔴 |
| 85 | `free` — estado del heap y fragmentación | 🔴 |
| 86 | `uptime` — tiempo desde el arranque | 🔴 |
| 87 | `clear` — limpiar pantalla (secuencias ANSI) | 🔴 |
| 88 | `reboot` — reinicio del sistema | 🔴 |
| 89 | `uname` — información del sistema | 🔴 |
| 90 | El shell corre **como una tarea más** del scheduler | 🔴 |
| 91 | `kill <tid>` — terminar tarea | 🟡 |
| 92 | `run <programa>` — lanzar tarea integrada | 🟡 |
| 93 | `top` — vista continua de tareas | 🟡 |
| 94 | `trace on/off/filter` — control de instrumentación | 🟡 |
| 95 | `inject <escenario>` — inyección de fallos | 🟡 |
| 96 | Historial de comandos (flechas arriba/abajo) | 🟡 |
| 97 | Autocompletado con TAB | 🟢 |
| 98 | Redirección `>` y tuberías `\|` | 🟢 |
| 99 | Variables de entorno | 🟢 |
| 100 | Scripts ejecutables desde el FS | 🟢 |

### 9.7 Instrumentación `[E07]` — *el corazón de la tesis*

| # | Funcionalidad | Pri |
|---|---|---|
| 101 | Sondas en context switch (entrada/salida, duración en ciclos) | 🔴 |
| 102 | Sondas en alloc/free (tamaño, dirección, tarea, resultado) | 🔴 |
| 103 | Sondas en lock/unlock/block (quién, qué recurso, cuánto esperó) | 🔴 |
| 104 | **Ring buffer SPSC lock-free** con barreras de memoria | 🔴 |
| 105 | Contador de `eventos_perdidos` por saturación | 🔴 |
| 106 | Formato de evento binario compacto y de tamaño fijo | 🔴 |
| 107 | **Compilación condicional a coste cero** (`SENTINEL_TRACE=0`) | 🔴 |
| 108 | **Heartbeat periódico** desde el ISR (detecta el silencio) | 🔴 |
| 109 | **Medición del overhead de las propias sondas** (ON vs OFF) | 🔴 |
| 110 | Filtros de evento activables en caliente | 🟡 |
| 111 | Muestreo 1-de-N para eventos de alta frecuencia | 🟡 |
| 112 | Histogramas en dispositivo (latencia de switch, tiempo en cola) | 🟡 |
| 113 | Snapshot completo del estado bajo demanda | 🟡 |
| 114 | Trace ring post-mortem (volcar últimos N eventos al fallar) | 🟢 |

### 9.8 Transporte y puente `[E08]`

| # | Funcionalidad | Pri |
|---|---|---|
| 115 | Protocolo de trama (sync, versión, tipo, seq, len, payload, CRC16) | 🔴 |
| 116 | Detección de pérdida por número de secuencia | 🔴 |
| 117 | Validación de integridad por CRC16 | 🔴 |
| 118 | Resincronización tras trama corrupta | 🔴 |
| 119 | Puente Python: UART → decodificación → JSON | 🔴 |
| 120 | Grabación local de trazas a archivo (modo offline) | 🔴 |
| 121 | Canal descendente: comandos del host → UART → kernel | 🟡 |
| 122 | Publicación MQTT a AWS IoT Core con TLS y X.509 | 🟡 |
| 123 | Buffer de reintento si se cae la red | 🟡 |
| 124 | Reproductor de trazas (replay de sesión grabada) | 🟢 |

### 9.9 Inyección de fallos `[E09]`

| # | Funcionalidad | Pri |
|---|---|---|
| 125 | `inject deadlock` — dos tareas, dos mutex, orden invertido | 🔴 |
| 126 | `inject leak <bytes>` — malloc periódico sin free | 🔴 |
| 127 | `inject frag` — patrón alloc/free intercalado | 🔴 |
| 128 | `inject hog` — tarea que monopoliza la CPU | 🔴 |
| 129 | Generador de carga sintética configurable | 🔴 |
| 130 | `inject prio-inv` — escenario Mars Pathfinder | 🟡 |
| 131 | `inject stack-overflow` — desbordamiento controlado | 🟡 |
| 132 | Escenarios parametrizables (intensidad, duración) | 🟡 |

### 9.10 Detección y diagnóstico `[E10, E15]`

| # | Funcionalidad | Pri |
|---|---|---|
| 133 | **Grafo de espera + detección de ciclos** (deadlock exacto) | 🔴 |
| 134 | Cálculo continuo del índice de fragmentación | 🔴 |
| 135 | Detección de inanición por umbral de tiempo en cola | 🔴 |
| 136 | Jitter del scheduler (desviación estándar de latencia) | 🔴 |
| 137 | Detección de inversión de prioridad | 🟡 |
| 138 | Regresión sobre heap libre → predicción de OOM con ETA | 🟡 |
| 139 | EWMA / detección de deriva sobre latencias | 🟡 |
| 140 | Detección de anomalías multivariada (Isolation Forest) | 🟢 |
| 141 | Motor de reglas de alerta configurable | 🟢 |

### 9.11 Nube, visualización y calidad `[E14, E16, E17]`

| # | Funcionalidad | Pri |
|---|---|---|
| 142 | Modo offline: persistencia local en SQLite | 🔴 |
| 143 | Banco de pruebas automatizado + tabla de resultados | 🔴 |
| 144 | Tests unitarios del allocator **en el host** (gcc, sin placa) | 🟡 |
| 145 | Ingesta AWS IoT Core + Lambda + modelo relacional | 🟡 |
| 146 | Dashboard (Grafana o HTML+Chart.js) | 🟡 |
| 147 | CI en GitHub Actions | 🟡 |
| 148 | Infraestructura como código (Terraform/CDK) | 🟢 |

---

## 10. Mapa de épicas

> Detalle completo de cada una en `tasks/EXX-*.md`

### 🔴 MVP — OBLIGATORIAS (E01–E12)

| ID | Épica | Depende de | Est. (h) |
|---|---|---|---|
| E01 | Fundación bare-metal y toolchain | — | 16 |
| E02 | Núcleo de planificación (scheduler) | E01 | 19,5 |
| E03 | Gestión dinámica de memoria | E01 | 9 |
| E04 | Sincronización y concurrencia | E02 | 6 |
| E05 | Sistema de archivos mínimo (VFS) | E03 | 9 |
| E06 | Shell e intérprete de comandos | E02,E03,E05 | 10 |
| E07 | Capa de instrumentación | E02,E03,E04 | 8 |
| E08 | Protocolo de transporte y puente | E07 | 7,5 |
| E09 | Inyección de fallos | E04,E06 | 4 |
| E10 | Detección determinista | E04,E07 | 6 |
| E11 | Banco de pruebas y validación experimental | E07,E09 | 6 |
| E12 | Documentación, portafolio y sustentación | transversal | 8,5 |
| | **TOTAL MVP** | | **109,5 h** |

### 🟡 ALTO VALOR — si hay tiempo (E13–E17)

| ID | Épica | Depende de | Est. (h) |
|---|---|---|---|
| E13 | Arquitectura dual-core AMP | E07,E08 | 12 |
| E14 | Pipeline en la nube (AWS) | E08 | 10 |
| E15 | Análisis predictivo | E14 | 5 |
| E16 | Dashboard y visualización | E14 | 8 |
| E17 | Ingeniería de software y CI | E01 | 5 |
| | **TOTAL** | | **40 h** |

### 🟢 LUJO — backlog / trabajo futuro (E18–E19)

| ID | Épica | Depende de | Est. (h) |
|---|---|---|---|
| E18 | Políticas avanzadas de planificación y memoria | E02,E03 | 14 |
| E19 | Persistencia en flash y FS avanzado | E05 | 12 |
| | **TOTAL** | | **26 h** |

**TOTAL GLOBAL: 175,5 horas.**

---

## 11. Criterios de éxito

No "funciona / no funciona". Números verificables.

### 11.1 Criterios del profesor (obligatorios)

| # | Criterio | Meta |
|---|---|---|
| P1 | El sistema arranca en la ESP32 sin ningún RTOS/SDK | ✅ binario |
| P2 | Multitarea apropiativa con ≥ 5 tareas concurrentes | 1 h sin fallar |
| P3 | Gestión dinámica de memoria funcional | `free` reporta correctamente |
| P4 | Shell interactivo por serial | prompt responde |
| P5 | Comandos `ls`, `cd`, `pwd` operativos | + `cat`, `mkdir`, `rm`, `ps`, `free` |
| P6 | Sistema de archivos jerárquico navegable | ≥ 3 niveles de profundidad |

### 11.2 Criterios de diferenciación (Sentinel)

| # | Criterio | Meta |
|---|---|---|
| S1 | Latencia de cambio de contexto medida | < 10 µs, con desviación conocida |
| S2 | Overhead de instrumentación cuantificado | < 5% CPU, documentado |
| S3 | Deadlock inyectado → detectado | < 1 segundo |
| S4 | Fuga inyectada → predicha antes del agotamiento | ≥ 60% de anticipación |
| S5 | Eventos perdidos bajo carga nominal | < 1%, siempre reportados |
| S6 | Comparativa monitor core 0 vs core 1 | diferencia medida y explicada |
| S7 | Reproducibilidad: clonar y compilar | 1 comando |
| S8 | Los 5 escenarios de fallo demostrables en vivo | 100% |

> **El criterio S5 es el más importante y el menos obvio.** Un sistema que pierde datos y
> lo admite es infinitamente más confiable que uno que pierde datos y calla.

---

## 12. Entregables finales

1. **Repositorio Git** con historial limpio y commits atómicos por HU.
2. **Binario del kernel** + instrucciones de flasheo en un comando.
3. **El puente** (Python) + modo offline funcional.
4. **Dashboard** (si E16 se completa).
5. **Informe técnico** con arquitectura, ADRs, resultados numéricos y análisis de error.
6. **Presentación** de sustentación.
7. **Video demo de 3 minutos** (para LinkedIn).
8. **README de portafolio** con diagramas y GIFs.

---

## 13. Beneficios y público objetivo

### 13.1 Para el autor

| Dimensión | Qué se obtiene |
|---|---|
| **Técnica** | Haber escrito un cambio de contexto en assembly y poder explicarlo registro por registro. No se improvisa en una entrevista. |
| **Conceptual** | *Sentir* lo que la mayoría memoriza: por qué un deadlock es un ciclo, por qué fragmentación ≠ memoria llena, por qué medir cuesta. |
| **Método** | Diseñar un experimento: hipótesis, variable de control, medición, reporte honesto de error. |
| **Portafolio** | Un repositorio que un ingeniero senior respeta en 30 segundos. |
| **Narrativa** | Una historia de 3 minutos contable en cualquier entrevista. |

### 13.2 Perfiles laborales a los que apunta

| Perfil | Por qué le importa |
|---|---|
| Firmware / Embedded Engineer | Es literalmente el trabajo: bare-metal, RTOS, drivers, protocolos serie |
| Systems / Kernel Engineer | Scheduler, memoria, concurrencia sin locks, barreras de memoria |
| SRE / Observability Engineer | Instrumentación, telemetría, detección de anomalías, costo de observar |
| Platform / DevOps | Pipeline de datos, IaC, IoT, series temporales |
| Automotriz / Aeroespacial / Médico | Tiempo real donde jitter y determinismo son requisitos regulados |
| Robótica | Control en lazo cerrado con garantías temporales |

### 13.3 A quién le sirve el sistema en el mundo real

- **Equipos de firmware** que necesitan entender por qué su RTOS pierde deadlines y no
  tienen presupuesto para herramientas comerciales.
- **Flotas IoT desplegadas** donde no se puede conectar un debugger a un dispositivo remoto.
- **Educación**: un kernel de 5.000 líneas que se explica a sí mismo es mejor instrumento
  pedagógico que Linux, precisamente por ser pequeño.
- **Investigación en tiempo real**: banco de pruebas para comparar políticas de
  planificación con datos reales, no simulados.

---

## 14. Estado del arte y diferenciación

> ⚠️ **Mencionar esto proactivamente en la sustentación.** Si el profesor lo saca primero,
> queda mal; si lo saca el autor, demuestra que investigó el estado del arte.

| Producto | Qué hace | Dónde Sentinel es distinto |
|---|---|---|
| **Percepio Tracealyzer** | Traza visual de RTOS comerciales, líder del mercado | Comercial, caro, y **traza kernels ajenos**. Sentinel traza uno propio: puede instrumentar donde quiera. |
| **SEGGER SystemView** | Traza en tiempo real por JTAG/RTT | Requiere hardware J-Link. Sentinel funciona con un cable USB de 2 dólares. |
| **Linux ftrace / eBPF** | Estado del arte en instrumentación de kernel | Requiere Linux completo con MMU. Sentinel cabe en 300 KB de RAM. |
| **FreeRTOS + trace hooks** | Ganchos de traza en un RTOS estándar | FreeRTOS no permite medir su propio scheduler desde adentro con precisión de ciclo. Y aquí el scheduler es propio. |
| **xv6 (MIT)** | Kernel didáctico Unix-like | Corre en QEMU/RISC-V, sin instrumentación ni hardware real. Sentinel corre en silicio y se mide. |

### Diferenciación real, en tres puntos

1. **Nadie combina las tres cosas**: kernel propio + observador en núcleo dedicado +
   analítica externa.
2. **La inyección de fallos integrada** convierte la demo en un experimento reproducible.
   Tracealyzer no trae un botón de "provocar deadlock".
3. **La medición del propio costo de observación** es un ángulo que las herramientas
   comerciales evitan por conveniencia comercial. Aquí es honestidad científica.

### Posicionamiento honesto

> "Sentinel es a Tracealyzer lo que xv6 es a Linux: no compite en funcionalidad, compite en
> **comprensibilidad**. Cabe entero en la cabeza de una persona, y por eso puede enseñar
> cosas que un sistema de producción no puede."

---

## 15. Riesgos

| # | Riesgo | Prob. | Impacto | Mitigación |
|---|---|---|---|---|
| R1 | **El *window spill* de Xtensa bloquea el proyecto** | Alta | Crítico | Estudiarlo en E01, no en E02. Leer (no copiar) `portasm.S` de FreeRTOS como referencia. Timebox de 12 h; si se excede → plan B. |
| R2 | **El bring-up bare-metal no arranca** | Media | Crítico | E01 completa dedicada. "Hola mundo" mínimo antes de cualquier ambición. Plan B: usar ESP-IDF solo como bootloader/toolchain (ver ADR-002). |
| R3 | Saturación del UART bajo carga | Media | Medio | Presupuesto de ancho de banda calculado en diseño + filtros + muestreo (E07). |
| R4 | AWS consume tiempo desproporcionado | Media | Medio | **Modo offline (SQLite) desde el inicio.** La nube es capa intercambiable, no requisito. |
| R5 | **Alcance excesivo vs horas disponibles** | **Muy alta** | Alto | Backlog estrictamente ordenado por prioridad + checkpoints demostrables (ver `02-ROADMAP.md`). |
| R6 | Hardware quemado / placa muerta | Baja | Medio | Hay varias unidades disponibles. |
| R7 | El profesor cambia los requisitos al asignar el proyecto | Media | Alto | E01–E06 cubren cualquier definición razonable de "SO minimalista". Revalidar en cuanto asigne. |

---

## 16. Restricciones y contexto del autor

| Campo | Valor |
|---|---|
| **Disponibilidad** | ~1 hora diaria → **5–8 h/semana** |
| **Horizonte** | Semestre 2026-2; el profesor **aún no ha asignado formalmente el proyecto** (ventaja: se empieza antes) |
| **Estrategia acordada** | **Opción A extendida:** sacar el MVP completo primero, en orden estricto de prioridad; el tiempo sobrante se invierte de mayor a menor importancia. El día de la entrega se presenta lo que exista. |
| **Compromiso** | El proyecto se termina pase lo que pase y se publica en GitHub y LinkedIn. |
| **Nivel** | 2º semestre — primera experiencia con bare-metal y assembly. El plan lo contempla. |

### ⚠️ Realidad del presupuesto de horas

- Horas disponibles en 8 semanas: **40–64 h**
- Estimación del MVP: **109,5 h**
- **El MVP NO cabe en 8 semanas a 6 h/semana.** Necesita ~17 semanas a ese ritmo.
- **CP4** (el mínimo que satisface al profesor) necesita **~70 h ≈ 11 semanas** a 6 h/semana.

**Esto no es un problema si se gestiona bien.** El `02-ROADMAP.md` define **checkpoints
demostrables**: puntos de corte en los que el sistema, aunque incompleto, arranca y hace
algo que se puede mostrar. La estrategia es avanzar por checkpoints, no por épicas
completas, para que **cualquier fecha de corte encuentre un sistema presentable**.

---

## 17. Preguntas abiertas

| # | Pregunta | Estado | Impacto |
|---|---|---|---|
| Q1 | ¿Cuál es la fecha exacta de entrega del proyecto? | ❓ Abierta | Define el punto de corte del roadmap |
| Q2 | ¿El proyecto es individual o en grupo? | ❓ Abierta | Duplica o triplica las horas disponibles |
| Q3 | ¿El profesor exige un lenguaje o plataforma específicos? | ❓ Abierta | Podría invalidar ESP32/bare-metal |
| Q4 | ¿Qué peso tiene la rúbrica en documentación vs. funcionalidad? | ❓ Abierta | Reordena E12 |
| Q5 | ¿Hay un formato de informe obligatorio de la universidad? | ❓ Abierta | Afecta E12 |
| Q6 | ¿El FS debe persistir tras reinicio o basta RAMFS? | 🟡 Asumido: RAMFS | Si debe persistir, E19 sube a MVP |
| Q7 | ¿Se dispone de analizador lógico u osciloscopio? | ❓ Abierta | Afecta la validación cruzada de E11 |

---

## Apéndice A — Respuestas a preguntas de sustentación

Preparar estas respuestas. Son las que va a hacer un profesor exigente.

**"¿Tu SO tiene memoria virtual?"**
> No. El ESP32 no tiene MMU, así que no hay paginación ni espacios de direcciones
> separados. Lo que sí tengo es un heap con metadatos por bloque y detección de corrupción
> por canarios. Para aislamiento real necesitaría un MPU, que es protección por regiones,
> no paginación — y son cosas distintas que no voy a confundir.

**"¿Cómo sabes que tus mediciones son correctas?"**
> Los UART crudos se reprodujeron y contrastaron con los CSV; cinco corridas nominales
> produjeron 57 940 cambios emparejados, con sus errores de enlace conservados.
> `CCOUNT` no se contrastó con un pulso GPIO porque no hubo analizador lógico ni
> osciloscopio. El cronómetro de Windows solo comprueba una escala agregada: incluye
> trabajo de tareas y comunicación, así que no prueba discrepancia menor de 5 %.

**"Tu detector de deadlocks corre dentro del sistema. ¿Qué pasa si se bloquea él mismo?"**
> El detector **nunca toma mutexes**. Lee el grafo desde una sección crítica muy corta con
> interrupciones deshabilitadas, o desde un ISR de timer que por definición no puede ser
> bloqueado por una tarea. El vigilante debe vivir en un plano donde la enfermedad no lo
> alcanza.

**"¿Y si el sistema se cuelga del todo? Tu observador solo vería silencio."**
> Por eso existe el **heartbeat**: core 0 emite un latido incondicional desde el ISR del
> timer. Si los latidos paran, el sistema no está tranquilo — está muerto. **La ausencia de
> datos también es un dato.** Es el mismo patrón de todos los sistemas distribuidos.

**"¿Cuánto cuesta tu instrumentación?"**
> En la carga nominal, TRACE ON frente a OFF añadió 1,285 ± 0,009 puntos de CPU,
> 33 320 B de RAM estática y 3296 B de imagen. El coste integral fue
> 109,111 ± 0,849 ciclos por evento, que incluye drenaje; la sonda aislada de E07
> midió 48 ciclos y no es directamente comparable. La latencia observada no cumplió
> la meta de 10 µs.

**"¿Por qué no usaste FreeRTOS y ya?"**
> Porque entonces habría medido el scheduler de otro. El proyecto no es tener tareas
> concurrentes; es entender **qué cuesta cambiar entre ellas**.

**"¿Por qué no envías la telemetría por WiFi directamente?"**
> Dos razones. Arquitectónica: la red no es responsabilidad de un microkernel, es un
> detalle de transporte. Y técnica: el driver WiFi de Espressif es un blob propietario que
> depende de FreeRTOS. Meterlo significaría que mi SO corre *sobre* otro SO, y mis
> mediciones de cambio de contexto medirían el scheduler de FreeRTOS, no el mío.
> Sacrificaría la validez de todo el experimento por conveniencia.
