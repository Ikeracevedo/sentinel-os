# Sentinel OS — Registro de Decisiones de Arquitectura (ADR)

> Cada decisión importante se registra aquí: **qué se decidió, qué alternativas había, por
> qué se eligió esta, y qué consecuencias acepta.**
>
> Este archivo es oro para la sustentación: un profesor que pregunta "¿por qué hiciste X?"
> recibe una respuesta razonada, no una improvisación.
>
> **Última actualización:** 2026-09-20

---

## Formato

```
## ADR-NNN — Título
Estado: PROPUESTA | ACEPTADA | SUPERADA por ADR-XXX
Fecha:
### Contexto
### Decisión
### Alternativas consideradas
### Consecuencias (positivas y negativas)
```

---

## ADR-001 — Plataforma: ESP32-WROOM-32 (Xtensa LX6, doble núcleo)

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
Hay placas disponibles. Hay que elegir sobre qué silicio construir el kernel.

### Decisión
Usar la **ESP32 clásica (WROOM-32)**, con arquitectura Xtensa LX6 de doble núcleo.

### Alternativas consideradas

| Alternativa | Pros | Contras |
|---|---|---|
| **ESP32-C3 (RISC-V)** | 32 registros planos → cambio de contexto mucho más simple; ISA abierta y bien documentada; tiene PMP para protección de memoria | **Un solo núcleo** → mata la arquitectura AMP, que es el segundo diferenciador del proyecto |
| **ESP32-S3** | Doble núcleo + vectorial | Sigue siendo Xtensa; no aporta nada relevante para este proyecto |
| **QEMU / emulador** | Cero riesgo de hardware | Pierde toda la credibilidad: no hay silicio real, ni temporización real, ni demo física |
| **ESP32-WROOM-32** ✅ | **Doble núcleo** (habilita AMP); hardware ya disponible; enorme cantidad de documentación comunitaria | Xtensa usa **ventanas de registros** → *window spill* obligatorio en el cambio de contexto |

### Consecuencias
- ✅ La épica E13 (dual-core AMP) es viable. Es el segundo gran diferenciador.
- ✅ No hay que comprar nada.
- ⚠️ **El *window spill* de Xtensa es el punto técnico más difícil del proyecto.** Se le
  asigna timebox explícito (12 h) y estudio anticipado en E01.
- ⚠️ Sin MMU → el proyecto no puede ofrecer paginación ni memoria virtual. Se declara.
- 💡 **Ángulo positivo:** "implementé el window spill de Xtensa a mano" es una frase que
  ningún compañero podrá decir. La dificultad se convierte en diferenciación.

---

## ADR-002 — Estrategia de bring-up: bare-metal puro con plan de rescate

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
Arrancar código propio en ESP32 sin ESP-IDF exige generar una imagen con cabecera válida
para el ROM bootloader, un linker script correcto y código de arranque propio. Es la fuente
de riesgo #2 del proyecto (R2).

### Decisión
**Intentar el Camino A (bare-metal puro) con timebox estricto de 12 h.** Si se excede,
migrar al Camino B (híbrido) sin culpa y documentarlo.

| | Camino A — Bare-metal puro | Camino B — Híbrido |
|---|---|---|
| Qué | Linker script y `start.S` propios, sin ESP-IDF | ESP-IDF solo como bootloader/toolchain; el kernel toma control y desactiva FreeRTOS |
| Pureza del discurso | ✅ "Sin ningún SO por debajo" | ⚠️ Requiere explicación honesta |
| Riesgo | 🔴 Alto | 🟢 Bajo |
| Tiempo | 10–16 h | 3–5 h |

### Alternativas consideradas
- **Empezar directamente en híbrido:** menos riesgo, pero renuncia de entrada al argumento
  más fuerte del proyecto. Rechazado como punto de partida, aceptado como rescate.
- **Usar FreeRTOS y solo instrumentarlo:** invalida la tesis (se estaría midiendo el
  scheduler de otro). Rechazado.

### Consecuencias
- ✅ Se persigue la versión más fuerte del proyecto sin apostar el semestre a ella.
- ✅ El timebox convierte un riesgo abierto en una decisión acotada.
- ⚠️ Si se cae al Camino B, hay que ajustar el discurso: "mi kernel no usa FreeRTOS para
  planificar ni gestionar memoria; uso la infraestructura de arranque de ESP-IDF y desde
  ahí tomo el control completo del procesador."

---

## ADR-003 — La red NO vive en el kernel

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
El proyecto necesita sacar telemetría del chip. La opción obvia es WiFi, que la ESP32 tiene.
Pero el driver WiFi de Espressif es un **blob binario propietario que depende de las APIs de
FreeRTOS**. Incluirlo obligaría a tener FreeRTOS corriendo.

### Decisión
**El kernel no tiene stack de red.** La telemetría sale por **UART** hacia un puente
ejecutándose en un PC, que se encarga de TLS, MQTT y la nube.

### Alternativas consideradas

| Alternativa | Por qué se rechazó |
|---|---|
| WiFi directo desde el kernel | Obliga a FreeRTOS → el kernel dejaría de ser propio → **invalida toda la tesis del proyecto** |
| Escribir un stack TCP/IP propio | Semanas de trabajo, sin relación con la materia |
| AMP heterogéneo: core 1 con FreeRTOS+WiFi | Arquitectura legítima y real, pero es un proyecto entero por sí sola. → **Trabajo futuro** |

### Consecuencias
- ✅ El kernel permanece puro y pequeño.
- ✅ Argumento arquitectónico fuerte: *"la red no es responsabilidad de un microkernel; es
  un detalle de transporte"* — mismo principio de separación que usa `ftrace`.
- ✅ Argumento de validez experimental: *"si metiera FreeRTOS, mis mediciones de cambio de
  contexto medirían el scheduler de FreeRTOS, no el mío. Sacrificaría la validez de todo
  el experimento por conveniencia."*
- ⚠️ La demo requiere un PC conectado por USB. Aceptable.
- ⚠️ Hay que diseñar un protocolo de transporte propio (ver `03-PROTOCOLO.md`). **Esto no
  es un costo, es contenido técnico valioso.**

---

## ADR-004 — Sin retransmisión: se pierde y se reporta

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
El ring buffer de traza puede llenarse bajo carga. El UART puede saturarse. ¿Qué hacer con
los eventos que no caben?

### Decisión
**Descartar y contar.** Nunca bloquear al productor, nunca retransmitir. El contador
`events_dropped` viaja en cada heartbeat y en cada snapshot de estadísticas.

### Alternativas consideradas
- **Bloquear al productor hasta que haya espacio:** ❌ catastrófico. Una sonda que bloquea
  puede provocar el mismo deadlock que intenta detectar, y destruye la validez temporal.
- **Retransmitir tramas perdidas:** ❌ introduce estado y latencia dentro del sistema medido.
  Un dato viejo retransmitido vale menos que un dato nuevo a tiempo.
- **Ring buffer más grande:** ✅ ayuda, pero no elimina el problema; solo lo retrasa.

### Consecuencias
- ✅ La instrumentación nunca puede causar un fallo en el sistema observado.
- ✅ **Criterio de éxito S5:** un sistema que pierde datos y lo admite es más confiable que
  uno que pierde datos y calla.
- ⚠️ Los análisis deben tolerar huecos en la traza.

---

## ADR-005 — Ring buffer SPSC lock-free, sin atomics

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
Core 0 produce eventos, el monitor los consume. Hay que comunicarlos sin locks (ADR-004) y
sin corrupción.

### Decisión
**Ring buffer de un productor y un consumidor (SPSC)**, con `head` escrito solo por el
productor y `tail` solo por el consumidor. Sincronización mediante **barreras de memoria
release/acquire** y `volatile`. **Sin compare-and-swap, sin spinlocks.**

### Análisis técnico

| Problema | ¿Aplica? | Resolución |
|---|---|---|
| Coherencia de caché entre núcleos | ❌ No | La SRAM interna del ESP32 se accede **sin caché de datos**; la caché solo cubre flash/PSRAM. El buffer vive en SRAM interna. |
| Reordenamiento (compilador/CPU) | ✅ Sí | Barrera `release` entre escribir el dato y publicar el índice. Variables `volatile`. |
| Carrera sobre los índices | ❌ No | Cada índice tiene **un único escritor**. No hay carrera que resolver. |

### Alternativas consideradas
- **Mutex sobre el buffer:** ❌ viola el principio P2 ("observar nunca debe bloquear").
- **Compare-and-swap (`S32C1I`):** necesario solo si hubiera múltiples productores. La
  restricción SPSC **evita el problema en lugar de resolverlo** — que es mejor ingeniería.

### Consecuencias
- ✅ Coste por evento ~20 ciclos. Mínimo.
- ✅ Imposible que la instrumentación bloquee el kernel.
- ⚠️ **La barrera es obligatoria y su omisión produce bugs intermitentes casi imposibles de
  depurar.** Es el punto más sutil del código del proyecto.
- ⚠️ La restricción SPSC debe respetarse: si algún día dos fuentes escriben, el diseño
  rompe silenciosamente.

---

## ADR-006 — Todos los timestamps se toman en core 0

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
`CCOUNT` (contador de ciclos) es **un registro por núcleo**. Core 0 y core 1 tienen
contadores independientes que arrancan en momentos distintos y pueden divergir.

### Decisión
**Todos los timestamps se toman en core 0, en el instante del evento.** Core 1 nunca
timestampea: solo transporta lo que ya viene fechado.

### Alternativas consideradas
- **Timer global de periférico (TIMG/systimer) como reloj compartido:** más robusto y
  permitiría fechar en ambos núcleos, pero leer un periférico es más lento que leer un
  registro especial de la CPU — y la sonda debe ser lo más barata posible.
- **Sincronizar los `CCOUNT` al arrancar:** frágil, derivan.

### Consecuencias
- ✅ Los tiempos son coherentes y comparables por construcción.
- ✅ La sonda es máximamente barata (`RSR CCOUNT`, ~1 ciclo).
- ⚠️ Si en el futuro core 1 necesitara fechar eventos propios, habría que migrar al timer
  global. Documentado como limitación conocida.

---

## ADR-007 — El detector de deadlock nunca toma mutexes

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
El detector de deadlock corre dentro del mismo sistema que puede estar bloqueado.
**¿Qué pasa si el vigilante se convierte en víctima?**

### Decisión
El detector **nunca adquiere un mutex**. Lee el grafo de espera desde:
- **MVP (E10):** el ISR del timer, que por definición no puede ser bloqueado por una tarea.
- **E13:** core 1, sobre el estado reconstruido desde el ring buffer.

Para leer estructuras compartidas usa **secciones críticas muy cortas** (interrupciones
deshabilitadas por decenas de ciclos, no más).

### Consecuencias
- ✅ El vigilante vive en un plano donde la enfermedad no lo alcanza.
- ⚠️ El detector debe ser rápido: corre en contexto de ISR. DFS sobre ≤16 nodos es
  instantáneo, pero el límite hay que respetarlo.

---

## ADR-008 — Heartbeat incondicional: la ausencia de datos es un dato

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
Si core 0 se cuelga por completo, deja de generar eventos. El observador ve el buffer vacío.
**Pero "sistema bloqueado" y "sistema tranquilo sin actividad" producen exactamente la misma
señal: silencio.**

### Decisión
El ISR del timer emite `EV_HEARTBEAT` cada N ticks **incondicionalmente**, ocurra lo que
ocurra en las tareas. El consumidor declara el sistema muerto si transcurren `3×N` períodos
sin latido.

### Consecuencias
- ✅ Se distingue silencio sano de silencio patológico.
- ✅ El heartbeat transporta gratis datos útiles: ticks del sistema y `events_dropped`.
- ✅ Es el patrón estándar de todos los sistemas distribuidos — cita directa a Kubernetes
  liveness probes, Raft heartbeats, etc.
- ⚠️ Consume ancho de banda constante (despreciable: 10 eventos/s).

---

## ADR-009 — El sistema de archivos vive en RAM (RAMFS)

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
El profesor exige `ls`, `cd`, `pwd` → hace falta un sistema de archivos. ¿Debe persistir
tras reiniciar?

### Decisión
**RAMFS para el MVP.** El árbol vive en el heap y se pierde al reiniciar. Al arrancar se
precarga un árbol de demostración. La persistencia en flash SPI queda en E19 (backlog).

### Alternativas consideradas
- **Persistencia en flash desde el inicio:** exige driver SPI flash, gestión de sectores,
  wear leveling, journaling. +12 h y alto riesgo, sin aportar nada a la materia de SO que
  RAMFS no aporte.
- **Usar SPIFFS/LittleFS de Espressif:** dependencias externas, contradice el bare-metal.

### Consecuencias
- ✅ Cumple el requisito del profesor con coste mínimo.
- ✅ **Beneficio inesperado:** como el FS vive en el heap, crear archivos consume memoria
  **visible en `free`**. Permite demostraciones honestas de gestión de memoria.
- ⚠️ Si el profesor exige persistencia, E19 sube a MVP. **Pregunta abierta Q6.**

---

## ADR-010 — Política de planificación: prioridades estáticas + RR por nivel

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
Hay que elegir una política de planificación para el MVP.

### Decisión
**8 niveles de prioridad estática, con Round-Robin dentro de cada nivel.** Quantum
configurable, por defecto 10 ms.

### Alternativas consideradas

| Alternativa | Veredicto |
|---|---|
| Solo Round-Robin | Demasiado simple; no permite demostrar inanición ni inversión de prioridad |
| Colas multinivel con retroalimentación (MLFQ) | Más rico académicamente, pero complejo para el MVP → **E18** |
| EDF (Earliest Deadline First) | Excelente para tiempo real, pero exige modelo de deadlines → **E18** |
| **Prioridades + RR** ✅ | Suficiente para demostrar apropiación, inanición e inversión de prioridad. Base para comparar con E18. |

### Consecuencias
- ✅ Permite los escenarios de inyección `hog` (inanición) y `prio-inv`.
- ✅ Base de comparación cuando E18 añada políticas alternativas.
- ⚠️ Sin *aging*, la inanición es posible — lo cual es **intencional**: hay que poder
  provocarla para demostrarla. El *aging* llega en E18 como solución.

---

## ADR-011 — Modo offline (SQLite) antes que la nube

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
La nube AWS es atractiva para el portafolio, pero no es contenido de Sistemas Operativos y
puede consumir horas desproporcionadas (riesgo R4).

### Decisión
El puente escribe **siempre** a SQLite local. La publicación a AWS es un *sink* adicional y
opcional, enchufable. El análisis funciona idéntico sobre ambos.

### Consecuencias
- ✅ El proyecto es demostrable **sin internet** — crítico para una sustentación en aula.
- ✅ Si E14 se corta, no se pierde ninguna funcionalidad de análisis.
- ✅ El modelo relacional (requisito potencial del profesor) se satisface con SQLite.
- ⚠️ Hay que mantener dos *sinks*. Coste bajo si se diseña la interfaz desde el principio.

---

## ADR-012 — Frecuencia de CPU: 40 MHz (sin PLL, bare-metal puro)

**Estado:** ACEPTADA · **Fecha:** 2026-09-20

### Contexto
Todo cálculo de ciclos→tiempo (`cycles_to_us`, mediciones de E07) necesita saber la
frecuencia real de la CPU. La documentación de Espressif y casi todos los tutoriales
asumen 80/160/240 MHz — pero esos valores solo son ciertos **después** de que ESP-IDF
configura el PLL de reloj al arrancar. En Sentinel (Camino A, bare-metal puro) esa
inicialización nunca ocurre.

### Decisión
**CPU_FREQ_HZ = 40.000.000 (40 MHz)**, medido empíricamente en hardware real (HU-E01-06):
se transmitieron 10 bytes por UART a 115200 baudios (duración conocida: 100 bits ÷ 115200
= 868,1 µs) y se contaron los ciclos de `CCOUNT` transcurridos (34.771, repetible en dos
corridas). `34771 ÷ 0,0008681 ≈ 40,06 MHz` → confirma que la CPU corre directo del cristal
de 40 MHz, igual que el reloj APB (también medido en HU-E01-05).

### Alternativas consideradas
| Alternativa | Veredicto |
|---|---|
| Asumir 80 MHz (valor "de manual") | ❌ Ya causó un bug real de baudrate corrupto en HU-E01-05. Rechazado — no confiar en supuestos sin medir. |
| Configurar el PLL a 80/160/240 MHz nosotros mismos | Válido a futuro (más rendimiento), pero fuera de alcance de E01. Si se hace luego, este ADR se marca SUPERADA. |
| Medir con instrumento externo (osciloscopio/analizador lógico) | Más preciso, pero no disponible. La medición por UART tiene ~0,15% de error, suficiente para el proyecto. |

### Consecuencias
- ✅ `cycles_to_us()` da conversiones correctas mientras el reloj no se reconfigure.
- ✅ El *wrap* de `CCOUNT` (contador de 32 bits) ocurre cada `2³² ÷ 40.000.000 ≈ 107,37 s`
  — **no** a los ~26,8 s que se había estimado antes de medir (esa cifra asumía 160 MHz).
  Corregido en `07-GLOSARIO.md` y en la bitácora.
- ⚠️ Si en una épica futura (E13, dual-core) se configura el PLL para subir la frecuencia,
  **todas** las constantes de tiempo (baudrate, delays, `CPU_FREQ_HZ`) deben recalcularse.
  Documentar ese momento como una nueva ADR, no como un parche silencioso.

---

## ADR-016 — Diseño del heap: offsets, canario dentro del tamaño y recorridos por tandas

**Estado:** ACEPTADA · **Fecha:** 2026-10-12 · **Épica:** E03

### Contexto
El heap (96 KB, E03) debe servir `kmalloc`/`kfree` con first-fit, detectar corrupción y
exponer métricas sin degradar la latencia de interrupciones. Tres decisiones lo definen.

### Decisión
1. **Los enlaces `next`/`prev` son offsets de 32 bits dentro de la arena, no punteros.**
   La cabecera mide 20 B igual en la placa (32 bits) y en el PC (64 bits), así `heap.c`
   compila sin cambios con `gcc` nativo y se prueba en el host (`make test`, 51 asserts).
2. **El canario (4 B) va al final del payload y `size` lo incluye.** Un bloque que no se
   parte por tener un sobrante diminuto conserva el canario al final. Costo medido en la
   placa: ~6 ciclos frente a 283 del par `kmalloc+kfree` (≈2%), más 4 B por bloque.
3. **Los recorridos completos (`heap_stats`, `heap_usage_by_task`, `heap_dump`) se hacen en
   tandas de 32 bloques** con interrupciones apagadas solo dentro de cada tanda. Una
   *generación* (`g_gen`, sube en cada `kmalloc`/`kfree`) detecta que la lista cambió entre
   tandas y reinicia el recorrido (lectura optimista, como un *seqlock*). Tras 3 reinicios
   se hace una pasada completa con interrupciones apagadas para garantizar que termina.

### Alternativas consideradas
| Alternativa | Veredicto |
|---|---|
| Punteros reales en la cabecera | ❌ La cabecera cambia de tamaño entre placa y PC; los tests de host dejarían de ser fieles. |
| Recorrido único con interrupciones apagadas | ❌ Medido: ~64 ciclos/bloque; con 793 bloques son ~1,3 ms sin interrupciones. Incumple la meta de 100 µs. |
| Contadores incrementales (O(1)) para todo | ⚠️ Sirve para `used`/`free`/pico, pero `largest_free_block` exige recorrer. Se usa donde se puede (pico, contadores) y el recorrido solo para el resto. |
| Candado por bloque (soltar y retomar en cada bloque) | ❌ Otra tarea puede fusionar el bloque donde estamos parados; el offset guardado quedaría apuntando a una cabecera absorbida. |

### Consecuencias
- ✅ Latencia de interrupción acotada: una tanda de 32 bloques ≈ 51 µs (medido, meta < 100).
- ✅ Doble `kfree` y cabeceras absorbidas se detectan sin corromper (`HEAP_MAGIC_DEAD`).
- ⚠️ **`kmalloc` sigue apagando interrupciones durante todo su first-fit** (~200 µs con
  ~790 bloques). Reducirlo exige una lista de bloques libres o next-fit → **E18**.
- ⚠️ Un recorrido puede reiniciarse bajo alloc/free constante (visible en `walk_restarts`).
- ⚠️ `heap_dump` necesita ~520 B de pila.

---

## ADR-017 — La arena del heap vive en SRAM1 (`.dram1`)

**Estado:** ACEPTADA · **Fecha:** 2026-10-12 · **Épica:** E05 (decisión forzada)

### Contexto
Al añadir al TCB los descriptores de archivo y el cwd (E05), el enlazado falló:
`region 'dram' overflowed by 2880 bytes`. El presupuesto de DRAM (176 KB: heap 96 KB +
16 pilas de 4 KB + resto) estaba agotado, y E07 necesita además un ring buffer.

### Decisión
Nueva región `dram1` en `linker.ld` (`0x3FFE5000`, 108 KB, SRAM1) y sección `.dram1`
(NOLOAD). La arena del heap (96 KB) va ahí. `heap_probe_region()` escribe y verifica dos
patrones por palabra en toda la arena al arrancar; si falla, `panic`.

### Por qué esa dirección
Según `heap/port/esp32/memory_layout.c` de ESP-IDF, el ROM reserva `0x3FFE0000-0x3FFE0440`
y `0x3FFE3F20-0x3FFE4350`; la SRAM1 llega hasta `0x40000000` y es accesible por bytes
(`soc.h`). Se empieza en `0x3FFE5000`, por encima de ambas.

### Alternativas consideradas
| Alternativa | Veredicto |
|---|---|
| Reducir `MAX_TASKS` o las pilas | ❌ Gana ~2 KB y limita la demo; no resuelve E07. |
| Usar el IRAM sobrante | ⚠️ Solo admite accesos de 32 bits: apto para el ring de E07, no para el heap (strings). |
| PSRAM externa | ❌ Requiere WROVER e inicialización larga; fuera del MVP. |

### Consecuencias
- ✅ La DRAM principal queda con ≈ 100 KB libres (el ring de 32 KB de E07 cabe).
- ✅ Verificado en hardware: `0 palabras con error`.
- ⚠️ Depende del mapa de memoria del ESP32 clásico; en otro chip habría que revisarlo.
- ⚠️ La arena no se pone a cero: `heap_init` solo inicializa el primer bloque.

---

## ADR-018 — La tabla E11 conserva la condición y el límite de cada cifra

### Contexto
La misma unidad puede describir experimentos distintos: E07 midió 48 ciclos por
sonda aislada y E11 obtuvo 109,111 ± 0,849 ciclos por evento incluyendo drenaje.
La latencia nominal observada incumplió la meta S1, y la recepción UART tuvo
errores aunque la placa no descartó eventos del buffer.

### Decisión
El README, el informe y las diapositivas toman sus cifras de
`experiments/e11/master_results.csv`. Cada afirmación indica carga, variante,
unidad, incertidumbre y limitación. Las metas fallidas y los criterios no
comprobables se muestran como tales. El crudo UART y los CSV se conservan.

### Alternativas consideradas
- Usar la cifra aislada más favorable sin aclarar el perímetro de medición:
  rechazada por confundir dos experimentos.
- Eliminar atípicos o fallos de enlace: rechazado porque ocultaría el coste y
  la incertidumbre de observar el kernel.

### Consecuencias
- Un lector puede reproducir y cuestionar cada cifra.
- El material de portafolio debe dedicar espacio a límites y resultados adversos.

---

## ADR-019 — Versionar documentación de entrega de forma selectiva

### Contexto
`docs/` estaba excluido por completo en `.gitignore`. E12 requiere que el
informe, las decisiones y la evidencia contextual sobrevivan a un clon limpio.
La misma carpeta conserva borradores y notas que no son entregables.

### Decisión
Permitir en Git solo los documentos de referencia necesarios, E11/E12, la
rúbrica formal y `docs/e12/`. Excluir `docs/e12/build/`, usado para revisión
visual. Revisar el índice antes de confirmar para no añadir capturas temporales.

### Consecuencias
- Los enlaces del README a fuentes esenciales funcionarán tras clonar.
- Otros documentos de trabajo siguen locales hasta seleccionarlos expresamente.

---

## Decisiones pendientes

| ID | Pregunta | Bloquea | Cuándo decidir |
|---|---|---|---|
| ADR-013 | ¿Tamaño del ring buffer? (1024/2048/4096) — **ver presupuesto de DRAM en `05-ESTADO.md` §7** | Presupuesto de RAM | E07 |
| ADR-014 | ¿Grafana o HTML propio para el dashboard? | E16 | Al llegar a E16 |
| ADR-015 | ¿Persistencia en flash requerida? | E19 | Cuando el profesor asigne el proyecto |
