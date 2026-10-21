# E12 — Documentación, Portafolio y Sustentación

| Campo | Valor |
|---|---|
| **Estado** | 🟡 En preparación; 1/6 HU cerrada, CP8 pendiente |
| **Prioridad** | 🔴 MVP — innegociable |
| **Estimación** | 8,5 h (3 h distribuidas + 5,5 h finales) |
| **Depende de** | transversal (se alimenta de todas) |
| **Habilita** | la nota y el portafolio |
| **Checkpoint** | **CP8 — "Se demuestra"** |
| **Riesgo** | 🟡 Medio (se posterga fácilmente) |

---

## 1. Objetivo

Que el trabajo **se pueda contar y se pueda revisar**: informe técnico, README de
portafolio, video de demostración y presentación de sustentación.

---

## 2. Por qué existe

> **Un proyecto excelente que nadie entiende vale cero**, académica y profesionalmente.

Hay dos audiencias distintas y hay que servir a ambas:

| Audiencia | Qué quiere | Entregable |
|---|---|---|
| **El profesor** | Verificar que dominas la materia y cumpliste los requisitos | Informe técnico + sustentación + demo |
| **Un reclutador / ingeniero senior** | Entender en 30 segundos qué hiciste y si vale la pena | README + video de 3 min |

Son documentos distintos. **Escribir uno solo y usarlo para ambos es un error.**

### Advertencia sobre el momento

Esta épica **no se hace al final**. Se hace **30 minutos cada semana** más 3 horas de
consolidación. Si se deja entera para el final:

- Se te olvidarán las decisiones y su porqué (por eso existen los ADRs)
- Se te olvidarán los problemas que superaste (que son lo más interesante)
- Llegarás con el tiempo justo y el informe saldrá pobre

---

## 3. Alcance

### Incluye
- ADRs mantenidos a lo largo del proyecto
- Bitácora de sesiones (`05-ESTADO.md`)
- README de portafolio con diagramas y GIFs
- Informe técnico con resultados
- Presentación de sustentación
- Video demo de 3 minutos
- Preparación de preguntas difíciles

### NO incluye
- ❌ La generación de los datos experimentales → **E11**
- ❌ Documentación de API en el código (eso es parte de cada épica)

---

## 4. Historias de Usuario

### HU-E12-01 — Documentación continua (transversal)
**Como** autor, **quiero** documentar mientras construyo **para** no perder el contexto ni
las decisiones.

**Criterios de aceptación:**
- [x] La sesión E12 tiene entrada en `05-ESTADO.md` §5; la revisión histórica de todas las sesiones queda abierta
- [x] Las decisiones nuevas ADR-018 y ADR-019 están en `04-DECISIONES-ADR.md`
- [x] Las sorpresas de rúbrica y renderizado se registran en `05-ESTADO.md` §7
- [x] E12 toma métricas E11 ya registradas en `05-ESTADO.md` §4; no genera mediciones de placa nuevas
- [x] El requisito formal R-002 está en `08-REQUISITOS-PROFESOR.md`
- [ ] **Se dedican 30 min al final de cada semana a esto**

**Estimación:** 0,5 h/semana (≈ 3 h en total)

> 💡 **Los ADRs son lo más subestimado del proyecto.** Cuando el profesor pregunte "¿por qué
> no usaste WiFi?" o "¿por qué RAMFS?", tendrás una respuesta razonada escrita hace meses,
> no una improvisación. **Eso se nota.**

---

### HU-E12-02 — README de portafolio ⭐
**Como** ingeniero construyendo su carrera, **quiero** que un senior entienda el proyecto en
30 segundos **para** que valga como credencial profesional.

**Criterios de aceptación:**
- [x] Título + una línea que explique qué es
- [x] GIF de reproducción de demo real al principio, etiquetado como UART grabado
- [x] Sección "Qué hace" con bullets concretos
- [x] Sección "Arquitectura" con el diagrama de capas
- [x] Sección "Resultados" con tabla de métricas E11 y S1 fallida
- [x] Sección "Cómo ejecutarlo" con pasos reproducibles
- [x] Sección "Decisiones técnicas" enlazando a los ADRs
- [x] Sección "Qué aprendí" con dificultad real de window spill y medición
- [x] Sección "Limitaciones conocidas y trabajo futuro"
- [x] Badges: lenguaje, estado local de build y licencia aún no especificada
- [x] Bilingüe; mensaje principal en inglés

**Estimación:** 1 h

> 📌 **Regla del README de portafolio:** si alguien solo lee el título, el GIF y la tabla de
> resultados, **ya debe querer hablar contigo.** Todo lo demás es para quien ya se interesó.

---

### HU-E12-03 — Informe técnico
**Como** estudiante, **quiero** un documento académico completo **para** la entrega formal.

**Criterios de aceptación:**
- [x] **Resumen**: problema, solución y resultados principales en menos de una página
- [x] **Introducción y motivación**: el problema de la observabilidad en kernels + casos
      históricos (Pathfinder, Therac-25)
- [x] **Marco teórico**: planificación, gestión de memoria, sincronización, deadlocks
- [x] **Estado del arte**: ftrace, eBPF, Tracealyzer, SystemView y posicionamiento
- [x] **Arquitectura**: capas y decisiones
- [x] **Implementación**: subsistemas y window spill
- [x] **Metodología experimental**: repeticiones, crudos, incertidumbre y límites
- [x] **Resultados**: tablas y cuatro gráficas de E11
- [x] **Discusión**: resultados, S1 y efecto observador
- [x] **Limitaciones**: sin MMU, RAMFS, sin herencia de prioridad y enlace
- [x] **Trabajo futuro**: E13–E19 como roadmap
- [x] **Conclusiones** y **bibliografía**
- [ ] Formato según exija la universidad (❓ pregunta P8)

**Estimación:** 2 h (si los ADRs y E11 están al día)

> **Sección más valiosa e ignorada: "Limitaciones".** Declarar con precisión qué NO hace tu
> sistema y por qué demuestra que entiendes sus fronteras. Un sistema sin limitaciones
> declaradas es un sistema que no se entiende.

---

### HU-E12-04 — Video demo de 3 minutos ⭐
**Como** profesional, **quiero** una pieza compartible **para** LinkedIn y para respaldar la
sustentación.

**Criterios de aceptación:**
- [ ] Duración: **3 minutos máximo**; línea de tiempo de 180 s preparada, MP4 pendiente
- [x] Guion: problema (20 s) → arquitectura (30 s) → demo del shell (60 s) → demo de
      inyección y detección (50 s) → resultados numéricos (20 s)
- [ ] Terminal legible en el MP4 final; especificación 24 px y contraste preparada
- [x] Subtítulos `.srt` preparados, sujetos a ajuste con el montaje final
- [ ] Sin tiempos muertos (cortar las esperas)
- [ ] Grabación de respaldo de la demo completa por si el hardware falla en vivo
- [ ] Subido a YouTube (no listado) y enlazado desde el README

**Estimación:** 1 h

> 🎬 **El video es tu seguro de vida.** Si la placa falla el día de la sustentación —y a
> veces falla— reproduces el video y la demostración continúa. También es lo que compartes
> en LinkedIn, donde nadie va a clonar tu repositorio pero mucha gente ve 3 minutos.

---

### HU-E12-05 — Presentación de sustentación
**Como** estudiante, **quiero** una presentación estructurada **para** sustentar con
seguridad.

**Criterios de aceptación:**
- [x] 12 diapositivas editables, dentro del máximo de 15
- [x] Estructura:
  1. El problema (asimetría de la observabilidad) — 2 min
  2. La tesis de Sentinel — 1 min
  3. Arquitectura (diagrama de capas) — 2 min
  4. **Demo en vivo: shell, `ls`/`cd`/`pwd`, `ps`, `free`** — 2 min
  5. **Demo en vivo: `inject deadlock` → detección** — 2 min
  6. Los números (tabla de resultados) — 3 min
  7. El efecto observador — 2 min
  8. Limitaciones y trabajo futuro — 1 min
- [ ] Ensayada al menos 2 veces con cronómetro
- [ ] Plan B: video grabado si el hardware falla
- [x] Última diapositiva: la frase de cierre

**Estimación:** 1 h

**Frase de cierre:**
> "Un proyecto que funciona demuestra que sabes programar.
> Un proyecto que se mide a sí mismo demuestra que sabes de sistemas."

---

### HU-E12-06 — Preparación de preguntas difíciles
**Como** estudiante, **quiero** anticipar las preguntas duras **para** responder con
seguridad en lugar de improvisar.

**Criterios de aceptación:**
- [ ] Respuestas preparadas para todas las preguntas del Apéndice A; ensayo con Iker pendiente
      `00-MASTER.md`
- [x] Tres preguntas nuevas anticipadas según la rúbrica formal del profesor
- [ ] **Simulacro realizado**: alguien (o una IA) hace de profesor exigente
- [x] Tres debilidades identificadas y respuesta honesta preparada
      honesta para cada una

**Estimación:** 0,5 h

**Las preguntas obligatorias:**

| Pregunta | Dónde está la respuesta |
|---|---|
| ¿Tu SO tiene memoria virtual? | `00-MASTER.md` Apéndice A |
| ¿Cómo sabes que tus mediciones son correctas? | E11-HU-05 (validación cruzada) |
| Tu detector corre dentro del sistema. ¿Y si se bloquea él mismo? | ADR-007 |
| ¿Y si el sistema se cuelga del todo? Solo verías silencio. | ADR-008 (heartbeat) |
| ¿Cuánto cuesta tu instrumentación? | E11-HU-03 (número duro) |
| ¿Por qué no usaste FreeRTOS? | ADR-003 |
| ¿Por qué no envías telemetría por WiFi? | ADR-003 |
| ¿Qué capa de transporte usas? | `03-PROTOCOLO.md` §1 |
| ¿Ya existen herramientas que hacen esto? | `00-MASTER.md` §14 |
| ¿Por qué no usaste IA para detectar deadlocks? | E10 §2 |

> ⚠️ **La pregunta más peligrosa es la del estado del arte** (Tracealyzer, SystemView).
> **Sácala tú primero.** Si el profesor la saca y no la conocías, pareces no haber
> investigado. Si la sacas tú, pareces alguien que estudió el campo.

---

## 5. Notas técnicas críticas

### Dos documentos, dos audiencias

| | Informe técnico | README de portafolio |
|---|---|---|
| Audiencia | Profesor | Ingeniero / reclutador |
| Objetivo | Demostrar dominio de la materia | Generar interés en 30 s |
| Tono | Académico, exhaustivo | Directo, visual |
| Longitud | 20–40 páginas | 1 pantalla de scroll + detalle |
| Lo primero que se ve | Resumen | **GIF de la demo** |
| Idioma | Español | Inglés |

### La honestidad vende más que la perfección

Secciones que parecen debilidades y son fortalezas:

- **"Limitaciones conocidas"** → demuestra que entiendes las fronteras de tu sistema
- **"Qué aprendí"** con los problemas reales → demuestra capacidad de superar obstáculos
- **"Intenté X, costó Y, decidí Z"** → demuestra criterio de ingeniería

> Un informe donde todo salió perfecto a la primera es un informe que nadie cree. **El
> window spill que te costó 12 horas es más interesante que el shell que salió bien.**

### Qué hacer con las épicas no completadas

Convertirlas en una sección de **"Trabajo futuro"** *diseñada*, no en una disculpa:

> "El siguiente paso natural es mover el observador al segundo núcleo (arquitectura AMP).
> El diseño está completo: core 1 drenaría el ring buffer sin competir por CPU con el
> kernel. El experimento clave sería comparar el overhead en ambas configuraciones para
> aislar la contención de bus de memoria del costo de cómputo. No cupo en el alcance de
> este semestre por [razón concreta]."

**Eso no suena a algo que no hiciste. Suena a algo que ya pensaste.**

---

## 6. Riesgos

| # | Riesgo | Mitigación |
|---|---|---|
| Doc1 | **Se posterga toda la documentación al final** | 30 min fijos cada semana, no negociables |
| Doc2 | Se olvidan las decisiones y su porqué | ADRs escritos en el momento |
| Doc3 | El hardware falla en la sustentación | Video de respaldo (HU-04) + modo `--replay` de E08 |
| Doc4 | La demo en vivo se alarga y no da tiempo | Ensayo cronometrado × 2 |
| Doc5 | Pregunta imprevista del profesor | Simulacro previo (HU-06) |

---

## 7. Definición de Hecho

- [ ] Las 6 HUs cumplen sus criterios
- [x] `04-DECISIONES-ADR.md` tiene ≥ 12 ADRs (16 registros numerados)
- [ ] `05-ESTADO.md` tiene una entrada de bitácora por cada sesión
- [x] README con GIF auténtico rotulado, tabla de resultados y pasos reproducibles
- [x] Informe técnico con todos los apartados y copia DOCX; formato P8 por confirmar
- [ ] Video de 3 min subido y enlazado
- [ ] Presentación ensayada 2 veces con cronómetro
- [ ] Simulacro de preguntas realizado
- [ ] Repositorio público en GitHub, limpio y con tags de checkpoint
- [x] Borrador de LinkedIn preparado en `docs/e12/linkedin-borrador.md`; no publicado
- [ ] `05-ESTADO.md` actualizado, **CP8 marcado**
- [ ] Tag `git tag v1.0`

---

## 8. Referencias

- Nygard, *Documenting Architecture Decisions* — el formato ADR original
- *The Documentation System* (Divio) — tutorial/how-to/reference/explanation
- Heilmeier Catechism — las preguntas que todo proyecto debe responder
- Gregg, *Systems Performance* — cómo presentar resultados de rendimiento

---

## 9. Bitácora

| Fecha | Horas | Qué se hizo | Aprendizaje |
|---|---|---|---|
| 2026-10-21 | — | Fuentes E11 auditadas; README, informe, GIF de UART real, PPTX, guion, subtítulos y preguntas; QA local | S1 fallida y validación GPIO ausente deben aparecer en todo el material; rúbrica formal R-002 tiene video distinto |
