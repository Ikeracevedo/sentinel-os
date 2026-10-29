# Preparación de preguntas y simulacro de sustentación

**Estado:** respuestas escritas y simulacro guiado preparados. Iker no ha ensayado
dos veces ni realizado un simulacro en este chat; no se atribuye esa experiencia.

## Respuestas obligatorias del Apéndice A y de E12

**¿Hay memoria virtual?** No. El ESP32 medido no ofrece una MMU para páginas y
espacios separados. Hay heap con canarios y métricas, pero eso no aísla tareas.
Un MPU protegería regiones; no sería paginación.

**¿Cómo sabes que la medición es correcta?** Los CSV se reconstruyeron desde
UART crudo y la auditoría revisó 115 archivos en Git. CCOUNT midió ciclos de
la placa; se conservaron muestras atípicas y errores de enlace. Sin osciloscopio
no se comprobó la discrepancia GPIO–CCOUNT menor de 5%. El cronómetro de
Windows no aísla un switch. Esta respuesta corrige el texto histórico del
Apéndice A, que afirmaba una validación GPIO inexistente.

**¿Puede bloquearse el detector?** No toma mutexes (ADR-007) y por ello no
entra en el mismo ciclo de espera que inspecciona. Eso reduce un riesgo;
un fallo del kernel o interrupciones deshabilitadas aún pueden detenerlo.

**¿Cómo distingues silencio de cuelgue?** El heartbeat periódico es una señal
de vida (ADR-008). Si desaparece, el PC puede marcar pérdida de observación.
Un error UART también puede ocultarlo, por lo que hay que revisar CRC y huecos.

**¿Cuánto cuesta la instrumentación?** Bajo carga nominal, TRACE ON añadió
1,285 ± 0,009 puntos de CPU, 33 320 B de RAM estática y 3296 B al binario.
El coste integral fue 109,111 ± 0,849 ciclos/evento; los 48 ciclos de E07
eran solo la sonda aislada. La latencia observada no cumplió S1.

**¿Por qué no FreeRTOS?** La meta didáctica era implementar y medir el
planificador propio. Usar otro kernel cambiaría el objeto del experimento.
No se afirma que Sentinel sea una alternativa general a FreeRTOS.

**¿Por qué no WiFi dentro del kernel?** UART mantiene pequeño el sistema
medido y el puente asume la conexión externa (ADR-003). Es una elección de
alcance y validez experimental, con el coste de necesitar PC y cable.

**¿Qué transporte usas?** En el ESP32 no hay TCP/IP. Sobre UART se definió
framing con sincronización, secuencia y CRC16; no hay retransmisión (ADR-004).
El puente del PC puede usar otras capas fuera del kernel medido.

**¿Ya existen herramientas similares?** Sí: ftrace y eBPF en Linux, y
Tracealyzer/SystemView en embebidos. Sentinel no supera esas herramientas.
Su aporte es integrar un kernel docente propio, detector y coste ON/OFF con
crudos auditables. Ver las fuentes del [informe](informe-tecnico.md#bibliografía).

**¿Por qué no IA para detectar deadlocks?** Un ciclo en el grafo de espera es
un criterio determinista, explicable y de coste acotado para ≤16 tareas. Un
modelo de IA añadiría datos de entrenamiento, memoria y errores de clasificación
sin mejorar esa prueba concreta. IA predictiva es trabajo futuro, no E10.

## Tres preguntas nuevas por el énfasis de la rúbrica formal

**¿Dónde están `slice 200` y `slice 1500` en el Gantt?** Todavía no hay una
demostración verificada de C6. E20 implementará el comando y E21 el dashboard;
E22 deberá mostrar el cambio visual con cinco tareas. Los resultados E11 no
prueban ese criterio.

**¿Cómo funciona `exit` si la placa no es un proceso Linux?** El kernel de
ESP32 no tiene zombies de host; la equivalencia de C9 exige terminar sus tareas
y que el lanzador `./minios` del PC cierre y recoja sus hijos. E20/E22 deben
mostrar ambos lados. No se debe simular `ps aux | grep defunct` dentro del shell.

**¿Por qué el video de tres minutos no sigue las nueve escenas?** El video E12
es de portafolio. La entrega calificada R-002 pide otro video, con audio, hasta
diez minutos y nueve escenas en orden. E23 lo preparará una vez C1–C9 estén
verificados. No se sustituirán entre sí.

## Las tres debilidades más probables

1. **S1 falló:** la media observada fue 22,355 ± 0,223 µs contra <10 µs.
   Mostrar histograma, condición y comparación ON/OFF; explicar que optimizar
   requiere primero aislar mejor el camino de medida.
2. **No hubo instrumento GPIO:** decir “no comprobable”, enseñar el método
   alternativo y explicar exactamente qué incluye el reloj de Windows.
3. **Rúbrica C2–C9 incompleta:** presentar el plan E20–E23 y demostrar solo lo
   que ya existe. No llamar “cumplido” al Gantt, `slice`, `stats` o video final.

## Simulacro guiado pendiente

Una persona o IA debe hacer diez preguntas sin enseñar las respuestas al
estudiante. Duración sugerida: 12 minutos. Orden: memoria virtual, método de
medición, S1, overhead, detector, heartbeat, UART, estado del arte, C6 y C9.
Cronometrar cada respuesta (máximo 60 s), pedir la fuente del número y una
limitación. Registrar fecha, interlocutor, tiempos, tres respuestas débiles y
correcciones en la tabla siguiente. Repetir la presentación completa dos veces
con cronómetro y anotar ambas duraciones antes de marcar HU-E12-05/06.

| Fecha | Ensayo o simulacro | Duración | Preguntas débiles | Corrección | Responsable |
|---|---|---:|---|---|---|
| Pendiente | Ensayo 1 | — | — | — | Iker |
| Pendiente | Ensayo 2 | — | — | — | Iker |
| Pendiente | Simulacro exigente | — | — | — | Iker + interlocutor |
