# Video de portafolio E12

**Duración objetivo:** 180 s (máximo 3 min). **Estado:** guion, storyboard,
subtítulos y material de reproducción preparados; no existe todavía un MP4
final grabado, narrado, revisado ni subido a YouTube.

Este video es distinto del video **calificado** de hasta 10 min y nueve escenas
de la [rúbrica R-002](../../09-RUBRICA-PROFESOR.md). No utilizar este guion como
prueba de cumplimiento de C1–C9.

## Material auténtico ya disponible

| Toma | Archivo | Naturaleza |
|---|---|
| Deadlock y limpieza | `experiments/e11/detector_20261021T042124Z_r01.uart` | Bytes capturados de ESP32; `bridge.py --replay` muestra el shell y alerta. |
| GIF de portada | `docs/e12/media/deadlock-replay.gif` | Extracto visual generado solo de líneas presentes en ese UART; rotulado como replay. |
| Latencia | `experiments/e11/switch_hist_20261021T012850Z.png` | Gráfica de la batería nominal. |
| Coste de TRACE | `experiments/e11/nominal_overhead_comparison.png` | Cinco corridas ON y cinco OFF. |
| Fuga y fragmentación | `experiments/e11/memory_leak_20261021T011525Z.png`, `memory_frag_20261021T011525Z.png` | Gráficas de placa analizadas en PC. |

## Storyboard, narración y montaje

| Tiempo | Imagen/acción | Narración sugerida | Texto visible |
|---|---|---|---|
| 00:00–00:20 | Título y diagrama simple. | “Un sistema operativo puede parecer sano mientras sus tareas se bloquean. Sentinel OS expone ese estado y mide cuánto cuesta observarlo.” | ESP32; kernel propio; observabilidad medida. |
| 00:20–00:50 | Capas: ESP32, buffer, UART, puente PC. | “El kernel planifica tareas, gestiona memoria y un RAMFS. Las sondas registran eventos con CCOUNT. El PC recibe tramas UART con secuencia y CRC.” | Core 0 → traza → UART → PC. |
| 00:50–01:50 | **Primera opción:** grabación nueva de la placa con letra grande: `ls`, `cd`, `pwd`, `ps`, `free`. **Plan B:** reproducción identificada como tal y salidas reales disponibles; los UART E11 no prueban todos esos comandos en una sola toma. | Explicar una columna de `ps`, el heap y que RAMFS se reinicia. | “Live board” solo si se graba; de otro modo “Recorded UART replay”. |
| 01:50–02:40 | Reproducir el UART de E11: `inject deadlock`, alerta, `graph`, `inject stop deadlock`. | “Dos tareas forman un ciclo de espera. El detector encuentra el ciclo y la alerta nombra tareas y mutexes. En 20 inyecciones se detectaron 20.” | Fuente `detector_20261021T042124Z_r01.uart`. |
| 02:40–03:00 | Comparativa TRACE y tabla de tres cifras. | “La traza costó 1,285 puntos de CPU. El cambio observado fue 22,355 microsegundos y no cumplió la meta de 10. Medir también revela límites.” | S2 cumple; S1 falla; GPIO no comprobado. |

El esquema suma exactamente 20 + 30 + 60 + 50 + 20 = **180 s**. Cortar esperas entre comandos. Usar terminal con fuente monoespaciada de al menos 24 px, fondo oscuro y texto claro. La narración puede sustituirse por [`subtitulos.srt`](subtitulos.srt); hay que ajustar sus tiempos al montaje real y revisar que no tapen alertas.

## Comandos de demostración

Desde PowerShell en la raíz del repositorio, **sin placa**:

```powershell
py tools/bridge.py --replay experiments/e11/detector_20261021T042124Z_r01.uart --sink console
```

Salida esperada: comandos `free`, `graph`, `inject deadlock`, alerta de ciclo,
`inject stop deadlock` y resumen final `crc_errors=0 huecos=0` de esa captura.
La salida reproduce bytes pasados; no se deben teclear comandos sobre ese replay.

Para grabar una **nueva demostración en placa**, primero verificar el puerto:

```powershell
py -m serial.tools.list_ports -v
py tools/bridge.py --port COM8 --baud 921600 --sink console
```

`COM8` fue el puerto E11 y puede cambiar. Si el shell está en modo texto directo
en lugar de puente binario, usar el monitor apropiado para la imagen flasheada;
no abrir dos programas en el mismo puerto a la vez. En el shell, mostrar:

```text
pwd
ls
cd /home
pwd
ps
free
inject deadlock
graph
inject stop deadlock
```

Confirmar que la imagen de firmware acepta esos comandos antes de filmar.
Registrar una toma completa de respaldo, sin cortes, además de la edición corta.
No consta ninguna de esas dos grabaciones aún.

## Montaje local reproducible

1. Capturar pantalla y voz con una herramienta local disponible en el equipo;
   guardar la toma original fuera de Git si es grande.
2. Armar la línea de tiempo con los cinco bloques de arriba. Para la sección
   de deadlock se puede capturar el replay real. Importar las gráficas E11
   directamente, sin redibujar sus valores.
3. Exportar MP4 H.264, 1920×1080 o 1280×720, duración **≤180 s**, audio
   inteligible o subtítulos incrustados. Exportar también la toma completa de
   respaldo para el plan B de sustentación.
4. Comprobar visualmente `ls`/`cd`/`pwd`, alerta y unidades de la tabla. Verificar
   la duración con las propiedades del archivo y que los subtítulos coincidan.
5. Solo tras revisión humana, subir el video como **no listado** a YouTube y
   agregar el enlace al README. Publicar requiere la cuenta de Iker; no se
   afirma que se haya hecho.

**Acción humana pendiente concreta:** grabar la sección de shell que falta,
aportar narración o aceptar subtítulos, revisar el MP4 y publicarlo. No se
inventará una sesión en vivo con el UART de E11.
