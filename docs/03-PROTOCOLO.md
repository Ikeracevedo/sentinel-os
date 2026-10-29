# Sentinel OS — Especificación del Protocolo de Telemetría

> Versión del protocolo: **1**
> Relevante para: E07 (instrumentación), E08 (transporte y puente), E13 (monitor core 1)
> **Última actualización:** 2026-10-17

---

## 1. Posición en el modelo de capas

> **Pregunta frecuente de sustentación: "¿Qué capa de transporte usas?"**
> **Respuesta: desde el ESP32, NINGUNA. Y es deliberado.**

El UART es **capa física y de enlace, nada más**: bytes crudos por un cable. No hay
direccionamiento, ni puertos, ni retransmisión, ni control de flujo, ni conexión. No hay
TCP ni UDP porque no hay IP porque no hay red.

**Por lo tanto, la capa de transporte hay que inventarla.** Eso es lo que especifica este
documento, y es contenido técnico real del proyecto.

```
ESP32 ──UART──► PUENTE (PC) ──────────────────► AWS IoT Core
       L1/L2                       MQTT          (L7 aplicación)
       protocolo propio            TLS 1.2       (L5/6 seguridad)
       (este documento)            TCP :8883     (L4 TRANSPORTE real)
                                   IP            (L3 red)
                                   WiFi/Ethernet (L2/L1)
```

### Justificación de diseño (para el informe)

> "No uso una capa de transporte estándar. Diseñé un protocolo de trama propio sobre UART
> con sincronización, secuenciación y CRC. Elegí **no retransmitir** deliberadamente: en
> telemetría de kernel, un dato viejo retransmitido vale menos que un dato nuevo a tiempo,
> y el mecanismo de retransmisión introduciría estado y latencia dentro del sistema que
> estoy midiendo. **Prefiero perder eventos y saber exactamente cuántos perdí.** Es la
> misma decisión de diseño que toma `ftrace` con su buffer circular."

---

## 2. Formato de trama

```
┌──────┬──────┬──────┬──────┬──────┬──────┬───────────┬───────┐
│ 0xAA │ 0x55 │ VER  │ TIPO │ SEQ  │ LEN  │  PAYLOAD  │ CRC16 │
│  1B  │  1B  │  1B  │  1B  │  2B  │  2B  │    N B    │  2B   │
└──────┴──────┴──────┴──────┴──────┴──────┴───────────┴───────┘
 └──── sync ────┘                                       └─ sobre todo
                                                           lo anterior
                                                           menos sync
Overhead: 10 bytes por trama
```

| Campo | Tam | Descripción |
|---|---|---|
| `SYNC` | 2 B | `0xAA 0x55`. Permite resincronizar tras corrupción. |
| `VER` | 1 B | Versión del protocolo. Actualmente `0x01`. |
| `TIPO` | 1 B | Tipo de trama (ver §3). |
| `SEQ` | 2 B | Contador incremental, little-endian, con wrap. **Detección de pérdida.** |
| `LEN` | 2 B | Longitud del payload en bytes, little-endian. Máx. 512. |
| `PAYLOAD` | N B | Contenido según `TIPO`. |
| `CRC16` | 2 B | CRC-16/CCITT-FALSE sobre `VER..PAYLOAD`. Polinomio `0x1021`, init `0xFFFF`. |

> **Por qué cada campo existe** — esto va en el informe:
>
> | Campo | Problema que resuelve (que TCP resolvería gratis) |
> |---|---|
> | `SYNC` | Si el puente arranca a mitad de un mensaje, ¿dónde empieza el siguiente? |
> | `SEQ` | Si recibes 41, 42, 44 → perdiste una. TCP retransmitiría; aquí solo se cuenta. |
> | `CRC16` | El UART corrompe bits por ruido eléctrico. Sin CRC, se procesaría basura como dato real. |
> | `VER` | Cuando el formato cambie, el puente viejo no explota. |

---

## 3. Tipos de trama

### Ascendentes (ESP32 → host)

| ID | Nombre | Payload |
|---|---|---|
| `0x01` | `FRAME_EVENTS` | 1..N eventos de traza de 16 B cada uno (`LEN` múltiplo de 16) |
| `0x02` | `FRAME_STATS` | Snapshot de estadísticas del sistema (ver §5) |
| `0x03` | `FRAME_LOG` | Texto ASCII (salida de `kprintf`, panic, mensajes del shell) |
| `0x04` | `FRAME_ALERT` | Alerta generada en el dispositivo (ej. deadlock detectado) |
| `0x05` | `FRAME_HELLO` | Identificación al arrancar: versión, build, config |
| `0x06` | `FRAME_HEARTBEAT` | Latido vacío. Presencia de vida. |
| `0x07` | `FRAME_TASKS` | Foto de las 16 ranuras de tareas para el dashboard. |

### Descendentes (host → ESP32)

| ID | Nombre | Payload |
|---|---|---|
| `0x81` | `FRAME_CMD` | Cadena ASCII de comando (ej. `inject deadlock`) |
| `0x82` | `FRAME_TRACE_CFG` | Configuración de filtros y muestreo |
| `0x83` | `FRAME_ACK` | Confirmación (opcional) |

---

## 4. Formato de evento de traza (16 bytes)

```c
typedef struct __attribute__((packed)) {
    uint32_t cycles;   // CCOUNT truncado a 32 bits (wrap cada ~26 s @160 MHz)
    uint8_t  type;     // ver tabla de tipos de evento
    uint8_t  tid;      // tarea implicada
    uint16_t seq;      // secuencia local del evento
    uint32_t arg0;     // depende del tipo
    uint32_t arg1;     // depende del tipo
} trace_event_t;
```

> ⚠️ **`cycles` es de 32 bits y hace wrap cada ~107,4 s a los 40 MHz actuales**
> (~26,8 s si se cambia el reloj a 160 MHz). El puente debe
> reconstruir el tiempo absoluto detectando el wrap (si `cycles_nuevo < cycles_anterior`,
> sumar 2³²). Esto es un detalle real que hay que implementar y documentar.

### Tipos de evento

| ID | Nombre | `tid` | `arg0` | `arg1` |
|---|---|---|---|---|
| `0x01` | `EV_SWITCH_OUT` | tarea saliente | tarea entrante | motivo (quantum/block/yield) |
| `0x02` | `EV_SWITCH_IN` | tarea entrante | ciclos del switch | tiempo en cola |
| `0x10` | `EV_ALLOC` | solicitante | tamaño pedido | dirección devuelta (0 = fallo) |
| `0x11` | `EV_FREE` | solicitante | dirección | tamaño liberado |
| `0x12` | `EV_HEAP_STATS` | — | bytes libres | mayor bloque libre |
| `0x20` | `EV_LOCK_TRY` | solicitante | id del mutex | — |
| `0x21` | `EV_LOCK_ACQ` | solicitante | id del mutex | ciclos esperando |
| `0x22` | `EV_LOCK_REL` | solicitante | id del mutex | ciclos poseído |
| `0x23` | `EV_BLOCK` | tarea | id del mutex | propietario actual |
| `0x30` | `EV_TASK_CREATE` | nueva tarea | prioridad | tamaño de pila |
| `0x31` | `EV_TASK_EXIT` | tarea | código de salida | ciclos CPU totales |
| `0x40` | `EV_HEARTBEAT` | — | ticks del sistema | eventos perdidos acumulados |
| `0x50` | `EV_ALERT` | tarea | código de alerta | dato asociado |
| `0x60` | `EV_INJECT` | — | escenario | parámetro |
| `0x61` | `EV_SLICE_CHANGE` | tarea que ejecutó `slice` | quantum anterior (ms) | quantum nuevo (ms) |

---

## 5. Payload de `FRAME_STATS`

```c
typedef struct __attribute__((packed)) {
    uint64_t uptime_ticks;
    uint32_t heap_total, heap_free, heap_largest_free, heap_peak;
    uint16_t heap_blocks, heap_free_blocks;
    uint32_t events_emitted, events_dropped;
    uint32_t switch_count;
    uint32_t switch_cycles_min, switch_cycles_max, switch_cycles_avg;
    uint8_t  task_count, trace_enabled;
    uint8_t  cpu_idle_pct;
    uint8_t  reserved;
} sys_stats_t;
```

Se emite cada 1 s. Es el latido "rico" del sistema: aunque se pierdan eventos individuales,
las estadísticas agregadas siempre llegan.

### `FRAME_TASKS` (v1)

Se emite junto a `FRAME_STATS`, una vez por segundo. Contiene 16 entradas de 32 B,
una por ranura del scheduler: `tid`, estado, prioridad, quantum, CPU acumulada,
heap atribuido y nombre. Una entrada con `tid = 0xff` está libre. Esta foto permite
que el dashboard rotule el Gantt con nombres reales sin inferirlos de texto del shell.

---

## 6. Algoritmo del decodificador (puente)

```
estado = BUSCANDO_SYNC
bucle:
    BUSCANDO_SYNC:
        leer byte; si == 0xAA → leer siguiente; si == 0x55 → estado = CABECERA
    CABECERA:
        leer 6 bytes (VER, TIPO, SEQ, LEN)
        si VER no soportada → descartar, volver a BUSCANDO_SYNC
        si LEN > 512 → trama corrupta, volver a BUSCANDO_SYNC
        estado = PAYLOAD
    PAYLOAD:
        leer LEN bytes + 2 de CRC
        calcular CRC sobre VER..PAYLOAD
        si CRC no coincide:
            crc_errors++ ; volver a BUSCANDO_SYNC
        si SEQ != ultimo_seq + 1:
            huecos += (SEQ - ultimo_seq - 1)     ← PÉRDIDA DETECTADA
        ultimo_seq = SEQ
        procesar según TIPO
        volver a BUSCANDO_SYNC
```

**Métricas que el puente debe reportar siempre:**

| Métrica | Significado |
|---|---|
| `tramas_ok` | Tramas válidas procesadas |
| `crc_errors` | Tramas que llegaron al decodificador con CRC inválido; la pérdida puede estar en UART, USB o el controlador del host |
| `huecos` | Tramas ausentes en el receptor (detectadas por `SEQ`) |
| `resyncs` | Veces que hubo que buscar sync de nuevo |
| `dropped_device` | Eventos que el **dispositivo** descartó (viene en el heartbeat) |

> Distinguir `huecos` (pérdida en el cable) de `dropped_device` (pérdida por saturación del
> ring buffer) es importante: son problemas distintos con soluciones distintas.

---

## 7. Presupuesto de ancho de banda

```
921600 baudios ÷ 10 bits/byte (8N1)          = 92.160 B/s (validado por COM9 directo)
Trama con 1 evento:  10 + 16 = 26 B          → ~3.540 eventos/s
Trama con 16 eventos: 10 + 256 = 266 B       → ~5.540 eventos/s  ← agrupar
```

**Conclusión de diseño: agrupar eventos por trama es obligatorio.** Amortiza los 10 bytes
de overhead y sube el techo un 56%.

En esta placa, la recepción directa por COM9 de Windows fue íntegra a 921600 baudios:
108220 eventos en 20 s de prueba determinista y 41329 eventos en 90 s con Sentinel
completo, sin errores de CRC ni huecos. En cambio, la recepción del mismo dispositivo
mediante `usbipd` hacia WSL perdió bytes. Para la telemetría E08 se usa COM9 en Windows;
WSL queda para compilar y grabar la imagen cuando haga falta.

### Mitigaciones ante saturación (E07 y E08)

1. **Agrupación** de hasta 16 eventos por trama.
2. **Filtros por tipo**, activables en caliente vía `FRAME_TRACE_CFG`.
3. **Muestreo 1-de-N** para eventos de alta frecuencia.
4. **Reporte honesto**: `events_dropped` viaja en cada heartbeat y stats.

---

## 8. Configuración de traza (`FRAME_TRACE_CFG`)

El primer byte del payload elige una operación. Este formato refleja las funciones
de configuración ya existentes en E07 y permite usar cualquiera de los 256 valores
de tipo de evento. El borrador anterior usaba una máscara de 32 bits que no podía
representar tipos como `EV_HEARTBEAT=0x40`.

| Operación | Payload | Efecto |
|---|---|---|
| `0x01` ENABLE | `01 enabled(1)` | `enabled` vale 0 o 1 |
| `0x02` FILTER | `02 tipo(1) enabled(1)` | Activa/desactiva un tipo (0..255); el heartbeat no se apaga |
| `0x03` SAMPLE | `03 rate_lo(1) rate_hi(1)` | Muestreo 1 de N, con N entre 1 y 65535 |

El receptor solo aplica una configuración después de validar el CRC. Devuelve el
resultado como texto `FRAME_LOG` (`trace cfg: ok` o `trace cfg: invalida`).

Comandos de shell equivalentes (E06/E07):

```
trace on | off
trace filter <tipo> on|off
trace sample <N>
trace stats
```

---

## 9. Versionado

| Versión | Cambios |
|---|---|
| `0x01` | Versión inicial. Evento de 16 B. Tramas 0x01–0x06 y 0x81–0x83. |

**Regla:** si cambia el tamaño o semántica de `trace_event_t`, se incrementa `VER`. El
puente debe soportar al menos la versión anterior durante una transición.
