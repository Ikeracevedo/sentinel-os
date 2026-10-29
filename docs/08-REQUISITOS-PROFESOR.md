# Sentinel OS — Registro de Requisitos del Profesor

> 📌 **DOCUMENTO VIVO Y DE MÁXIMA PRIORIDAD.**
>
> Cada vez que el profesor pida, aclare o cambie algo, se registra aquí **textualmente** y
> se audita contra las épicas existentes.
>
> **Regla absoluta:** cualquier requisito de este documento tiene prioridad sobre cualquier
> funcionalidad de Sentinel. Si hay conflicto de tiempo, se sacrifica Sentinel, nunca esto.
>
> **Última actualización:** 2026-10-21

---

## 1. Cómo usar este documento

### Flujo cuando el profesor pide algo nuevo

```
1. Anotar la cita TEXTUAL en §3 (lo que dijo, no lo que interpretaste)
2. Traducir a requisitos técnicos en §4
3. AUDITAR: ¿está cubierto por alguna épica existente?
     ├─ SÍ  → marcar la épica como "exigida por el profesor" (sube a prioridad 🔴)
     └─ NO  → crear épica o HU nueva; recalcular el roadmap
4. Actualizar 00-MASTER.md §6 y 02-ROADMAP.md
5. Registrar el impacto en horas en §5
```

### Regla de captura

> **Escribe lo que el profesor dijo, no lo que crees que quiso decir.**
> La interpretación va en §4, separada de la cita. Así, si la interpretación fue errónea,
> la cita original sigue intacta para reinterpretarla.

---

## 2. Estado de la asignación formal

| Campo | Estado |
|---|---|
| ¿El proyecto fue asignado formalmente? | ✅ Hay requisitos escritos de entrega final, registrados como R-002. |
| ¿Hay rúbrica publicada? | ✅ Nueve criterios, detallados en `09-RUBRICA-PROFESOR.md`. |
| ¿Hay fecha de entrega? | ❓ Desconocida |
| ¿Individual o grupal? | ❓ Desconocido |
| ¿Formato de informe obligatorio? | ❓ Desconocido |

> R-001 fue verbal; R-002 es formal y prevalece. Aún deben confirmarse la fecha,
> el formato del informe y los datos de portada. E12 prepara un informe editable
> mientras llega esa respuesta.

---

## 3. Registro de requisitos (citas textuales)

### R-001 — Comentario en clase · 2026-09 (fecha exacta desconocida)

**Cita (según recuerdo del estudiante):**
> "El proyecto final va a ser un sistema operativo minimalista, que pudiera hacer las cosas
> básicas de un sistema operativo. Programas como `ls`, `cd`, `pwd`, comandos como de Linux
> que pudiesen hacerse en el sistema operativo."

**Fuente:** comentario verbal en clase, no oficial
**Confianza:** 🟡 Media (de memoria, sin asignación formal)
**Estado:** ✅ Incorporado al plan

---

### R-002 — 🔴 RÚBRICA FORMAL DE LA ENTREGA FINAL · 2026-10

**Cita textual (requisitos entregados por el profesor):**
> **Entregables:**
> - Link al fork público en GitHub (fork de `fguzman82/minios-lab`)
> - Link al video en YouTube (máx. 10 min, con audio explicando)
> - 10 capturas de pantalla, una por criterio de la rúbrica
> - Portada con nombre, código del curso y fecha
>
> **Video — 9 escenas obligatorias:**
> 1. `make clean && make` sin errores
> 2. Arrancar los 3 componentes: `./minios`, bridge y dashboard
> 3. Lanzar mínimo 5 procesos con `run`
> 4. Gantt chart con los 5 procesos y sus colores
> 5. `ps` en miniOS explicando cada columna
> 6. `slice 200` y `slice 1500` — efecto visible en el Gantt
> 7. `kill <pid>` y verificar en `ps` y en el dashboard
> 8. `stats` con métricas agregadas
> 9. `exit` y `ps aux | grep defunct` vacío en otra terminal

**Aclaración adicional del profesor:**
> No es obligatorio usar su repositorio de GitHub. Lo que pide es que **el sistema operativo
> propio se acomode a los criterios** que él define.

**Aclaración de Iker:** las 9 escenas **son** los 9 criterios de la rúbrica.
**Hay que cumplir las 9, o su equivalencia justificada.**

**Fuente:** requisitos formales de la entrega final · referencia `fguzman82/minios-lab`
**Confianza:** 🟢 Alta (requisitos escritos)
**Estado:** ✅ Incorporado al plan → **`09-RUBRICA-PROFESOR.md`**

> 📌 **Esta rúbrica tiene prioridad absoluta sobre cualquier otra funcionalidad.**
> El desglose completo, con criterios de aceptación verificables por criterio y la matriz de
> equivalencias para bare-metal, está en **`09-RUBRICA-PROFESOR.md`**.

---

## 4. Traducción a requisitos técnicos

| ID | Requisito derivado | De | Épica que lo cubre | Estado |
|---|---|---|---|---|
| RT-01 | Debe ser un **sistema operativo**, no una aplicación | R-001 | E02 (scheduler) + E03 (memoria) | ✅ Cubierto |
| RT-02 | Debe ser **minimalista** | R-001 | E01 (bare-metal, sin SDK) | ✅ Cubierto |
| RT-03 | Debe hacer las **cosas básicas de un SO** | R-001 | E02, E03, E04 | ✅ Cubierto |
| RT-04 | Debe tener **`ls`** | R-001 | E06 (shell) + E05 (VFS) | ✅ Cubierto |
| RT-05 | Debe tener **`cd`** | R-001 | E06 + E05 | ✅ Cubierto |
| RT-06 | Debe tener **`pwd`** | R-001 | E06 + E05 | ✅ Cubierto |
| RT-07 | Debe tener **otros comandos tipo Linux** | R-001 | E06 (`cat`, `mkdir`, `rm`, `echo`, `ps`, `free`, `uptime`, `clear`, `uname`, `help`) | ✅ Cubierto |
| RT-08 | Los comandos deben ser **ejecutables dentro del SO** | R-001 | E06 (shell corre como tarea del scheduler) | ✅ Cubierto |
| RT-09 | Implícito: hace falta un **sistema de archivos** jerárquico | R-001 | E05 (VFS + RAMFS) | ✅ Cubierto |
| RT-10 | Implícito: hace falta un **intérprete de comandos** | R-001 | E06 | ✅ Cubierto |

---

## 5. Impacto acumulado en el plan

| Fecha | Requisito | Épicas añadidas | Horas añadidas | Horas totales MVP |
|---|---|---|---|---|
| 2026-09-20 | R-001 (`ls`/`cd`/`pwd`) | **E05 (VFS), E06 (Shell)** | **+19 h** | 90,5 → **109,5 h** |
| 2026-10-21 | **R-002 (rúbrica formal)** | **E20, E21, E22, E23** | **+30,5 h** | 109,5 → **140 h** |

> 📌 **Nota histórica 1:** la planificación original de Sentinel (17 épicas, centrada en
> instrumentación) **no contemplaba sistema de archivos ni shell.** Al detectar R-001 se
> reestructuró el MVP completo, subiendo E05 y E06 a prioridad innegociable y
> reordenando el roadmap para que CP4 (shell funcional) sea el mínimo entregable.

> 📌 **Nota histórica 2:** al llegar R-002 (la rúbrica formal), el proyecto ya tenía E01–E10
> completas y validadas en placa. La rúbrica resultó **compatible en su mayor parte**: de los
> 9 criterios, 3 ya se cumplían o estaban al 80%. Lo que faltaba se agrupó en 4 épicas
> nuevas (E20–E23), que **tienen prioridad sobre E13–E19** (dual-core, nube, dashboard
> avanzado, políticas y flash), las cuales pasan a backlog diferido.
>
> **Dos criterios exigieron una decisión de arquitectura:** C2 (`./minios`) y C9
> (`ps aux | grep defunct`) asumen un programa de Linux. Se resolvieron con un lanzador real
> y una doble demostración, sin portar el kernel. Ver `09-RUBRICA-PROFESOR.md` §4.

---

## 6. Preguntas pendientes para el profesor

> Llevarlas a clase. **Preguntar demuestra iniciativa, no ignorancia.**

| # | Pregunta | Por qué importa | Prioridad |
|---|---|---|---|
| P1 | ¿Cuál es la fecha de entrega del proyecto? | Define el punto de corte del roadmap | 🔴 Alta |
| P2 | ¿Es individual o en grupo? | Duplica o triplica las horas disponibles | 🔴 Alta |
| P3 | ¿Hay restricción de lenguaje o plataforma? | Podría invalidar ESP32/bare-metal | 🔴 Alta |
| P4 | ¿Qué comandos exactos espera ver funcionando? | Define el alcance mínimo de E06 | 🔴 Alta |
| P5 | ¿El sistema de archivos debe persistir tras reiniciar? | Si sí, E19 sube a MVP (+12 h) | 🟡 Media |
| P6 | ¿Debe correr en hardware real o acepta emulador? | Valida la decisión del ESP32 | 🟡 Media |
| P7 | ¿Qué peso tiene documentación vs. funcionalidad en la rúbrica? | Reordena E12 | 🟡 Media |
| P8 | ¿Hay formato de informe obligatorio de la universidad? | Afecta E12 | 🟡 Media |
| P9 | ¿Espera multiusuario o permisos reales? | Si sí, es una épica nueva | 🟢 Baja |
| P10 | ¿Espera gestión de procesos (fork/exec) o basta con tareas? | Sin MMU, `fork` real es imposible. Hay que aclararlo | 🟡 Media |

### Pregunta estratégica (la más valiosa)

> "Profe, tengo diseñado un microkernel bare-metal para ESP32 con planificador apropiativo,
> gestor de memoria, sistema de archivos y shell con comandos tipo Unix. Encima de eso
> quiero añadir instrumentación del kernel para detectar deadlocks y fugas en tiempo real.
> **¿Qué tan profundo quiere que llegue en la parte de instrumentación versus la parte de
> kernel básico?** Prefiero ajustar el alcance a lo que usted considere más valioso."

Esta pregunta hace tres cosas: demuestra que ya pensaste el alcance, le da control sobre la
dirección, y **te da información gratis sobre cómo va a calificar.**

---

## 7. Cobertura: requisitos vs. épicas

```
R-001 "SO minimalista con comandos tipo Linux"
  │
  ├─ RT-01 Sistema operativo ──────────► E02 Scheduler     ✅
  │                                      E03 Memoria       ✅
  ├─ RT-02 Minimalista ────────────────► E01 Bare-metal    ✅
  ├─ RT-03 Cosas básicas ──────────────► E04 Sincroniz.    ✅
  ├─ RT-09 Sistema de archivos ────────► E05 VFS           ✅
  └─ RT-04..08, RT-10 Comandos ────────► E06 Shell         ✅

  COBERTURA: 10/10 requisitos derivados  →  100%
  CHECKPOINT QUE LO SATISFACE:  CP4 "Obedece"  (~70 h de trabajo)
```

> **Estado real (2026-10-13):** E01–E06 implementadas y probadas en la placa.
> `ls`, `cd`, `pwd` (RT-04..06), `cat`, `mkdir`, `rm`, `touch`, `write`, `echo`, `ps`, `free`, `uptime`,
> `uname`, `help`, `clear`, `history`, `run`, `kill`, `reboot` funcionan: 67/67 comprobaciones automáticas
> (`tools/shell_test.py`). Pendiente de tu parte: grabar el video de 60 s y mostrárselo al profesor.
> ✅ **Con CP4 completado, todos los requisitos conocidos del profesor están cubiertos.**
> Todo lo posterior (E07–E19) es diferenciación voluntaria para el portafolio.

---

## 8. Plantilla para nuevos requisitos

Copiar y rellenar cuando el profesor pida algo:

```markdown
### R-00X — [Contexto: clase / correo / asesoría] · [fecha]

**Cita textual:**
> "..."

**Fuente:**
**Confianza:** 🟢 Alta (escrito/oficial) | 🟡 Media (verbal) | 🔴 Baja (de memoria)
**Estado:** ⬜ Sin auditar | 🟡 En análisis | ✅ Incorporado | ⏭️ Descartado

**Traducción técnica:**
| ID | Requisito | Épica que lo cubre | ¿Cubierto? |
|---|---|---|---|
| RT-XX | | | |

**Impacto:**
- Épicas nuevas:
- Épicas que suben de prioridad:
- Horas añadidas:
- Checkpoint afectado:
```
