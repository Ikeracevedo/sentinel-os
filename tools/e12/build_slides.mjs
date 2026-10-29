import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { Presentation, PresentationFile } from 'file:///C:/Users/Iker/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';

const root = path.resolve(import.meta.dirname, '../..');
const build = path.join(root, 'docs/e12/build');
const output = path.join(root, 'docs/e12/deliverables/presentacion-sentinel.pptx');
const skill = 'C:/Users/Iker/.codex/plugins/cache/openai-primary-runtime/presentations/26.1015.12040/skills/presentations';
const runtimePython = 'C:/Users/Iker/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const presentation = Presentation.create({slideSize:{width:1280,height:720}});
const C = {bg:'#0e1726', ink:'#f1f5f9', soft:'#becbd8', accent:'#57d3c2', warning:'#ffb26e', line:'#456173'};

function box(slide, text, x,y,w,h, size=26, color=C.ink, bold=false) {
  const shape=slide.shapes.add({geometry:'textbox', position:{left:x,top:y,width:w,height:h}, fill:'none', line:{style:'solid',fill:'none',width:0}});
  shape.text=text;
  shape.text.style={fontSize:size,color,bold};
  return shape;
}
function base(title, num, kicker='SENTINEL OS · SUSTENTACIÓN') {
  const slide=presentation.slides.add();
  slide.background.fill=C.bg;
  box(slide,kicker,68,28,1000,28,15,C.accent,true);
  box(slide,title,68,78,1140,82,38,C.ink,true);
  box(slide,`${String(num).padStart(2,'0')} / 12`,1110,670,100,28,16,C.soft);
  return slide;
}
function lines(slide, arr, x=70,y=190,w=1120,size=25,gap=71) {
  arr.forEach((line,i)=>box(slide,line,x,y+i*gap,w,gap-8,size,C.ink));
}
async function picture(slide, rel, alt, x,y,w,h) {
  const bytes=await fs.readFile(path.join(root,rel));
  slide.images.add({blob:bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),contentType:'image/png',alt,fit:'contain',position:{left:x,top:y,width:w,height:h}});
}

let s=base('Sentinel OS',1,'SISTEMAS OPERATIVOS · ESP32');
box(s,'Un kernel pequeño que mide el costo de observarse',70,230,1060,95,36,C.accent);
box(s,'Iker  ·  Universidad Pontificia Bolivariana',70,520,1020,40,22,C.soft);
s.speakerNotes.text='Apertura 30 s. Presentar E12 como informe del proyecto, no como cumplimiento completo de la rúbrica C1–C9.';

s=base('El problema de la observabilidad',2);
lines(s,['Una salida de shell no explica por qué una tarea deja de avanzar.','Un detector interno también consume CPU y memoria.','Pregunta: ¿qué ve Sentinel y cuánto cuesta verlo?'],70,205,1110,30,100);
s.speakerNotes.text='2 min. Ejemplo: dos tareas toman mutexes en orden opuesto y quedan esperando. Therac-25 y Pathfinder son motivación histórica, no pruebas de Sentinel. Fuentes: informe §1.';

s=base('Tesis y arquitectura',3);
box(s,'Core 0: tareas + scheduler + heap + RAMFS + detector',70,220,1110,62,28,C.ink);
box(s,'↓  sondas CCOUNT  ·  buffer circular',70,320,1110,62,29,C.accent,true);
box(s,'UART con secuencia y CRC  →  puente Python en PC',70,420,1110,62,28,C.ink);
s.speakerNotes.text='1–2 min. Core 1 es futuro E13. Fuente: docs/01-ARQUITECTURA.md y docs/03-PROTOCOLO.md.';

s=base('El kernel que se puede demostrar',4);
lines(s,['Arranque bare-metal sin FreeRTOS','Tareas y prioridades; mutexes y detección de ciclos','Heap con métricas; VFS + RAMFS; shell propio'],70,200,1100,29,105);
s.speakerNotes.text='1 min. No atribuir al proyecto memoria virtual, persistencia o herencia de prioridad. Fuente: kernel/ y ADR.';

s=base('Demo: shell y estado',5);
lines(s,['pwd  →  ruta actual     ls  →  archivos     cd  →  directorio','ps  →  tareas y estados     free  →  heap','Plan B: reproducción de UART etiquetada como grabación'],70,205,1120,27,105);
s.speakerNotes.text='2 min. La toma en vivo debe hacerse en la placa. El UART E11 disponible muestra free y graph, pero no una sola toma con ls/cd/pwd/ps. No presentar replay como vivo.';

s=base('Demo: inyectar y detectar un deadlock',6);
lines(s,['inject deadlock  →  dos tareas esperan mutexes','graph  →  ciclo 5 → 6 → 5','inject stop deadlock  →  escenario limpiado'],70,210,1100,28,100);
box(s,'20 alertas en 20 inyecciones E11',70,550,1100,54,30,C.accent,true);
s.speakerNotes.text='2 min. Puede usarse el UART real detector_20261021T042124Z_r01.uart como plan B. Tiempo medio de detección 320,862 ± 158,026 ms; no prometer generalidad absoluta.';

s=base('Cómo se midió',7);
lines(s,['5 corridas por condición comparable; UART crudo + CSV','57 940 switches nominales emparejados','20 inyecciones; una hora continua sin inyección','σ entre corridas; atípicos y fallos de enlace conservados'],70,183,1120,26,91);
s.speakerNotes.text='1 min. Explicar que ± es desviación entre corridas. La hora sin alertas es una ventana continua, no cinco horas. Fuente: experiments/e11/README.md y master_results.csv.';

s=base('Resultados principales',8);
lines(s,['Switch observado: 22,355 ± 0,223 µs  →  S1 falla','TRACE: +1,285 ± 0,009 puntos de CPU  →  S2 cumple','Deadlock: 20/20; 320,862 ± 158,026 ms','Falsas alertas: 0 en 3608,872 s'],70,182,1120,27,91);
s.speakerNotes.text='3 min. Valores de experiments/e11/master_results.csv. Switch con TRACE ON y sonda; detector de dos tareas; cero alertas en una sola hora.';

s=base('Distribución de cambio de contexto',9);
await picture(s,'experiments/e11/switch_hist_20261021T012850Z.png','Histograma E11 de 57 940 switches',110,175,1060,420);
box(s,'Meta <10 µs: no cumplida. Atípicos conservados.',105,605,1070,40,23,C.warning,true);
s.speakerNotes.text='La media nominal fue 22,355 ± 0,223 µs, la mediana 15,925, p99 42,425. Los 301 casos ≥80 µs coinciden con la activación periódica del detector; causalidad exacta no aislada. Fuente: E11.';

s=base('El efecto observador',10);
await picture(s,'experiments/e11/nominal_overhead_comparison.png','Comparativa TRACE ON OFF E11',100,170,1080,390);
box(s,'109,111 ± 0,849 ciclos/evento incluyendo drenaje',95,585,1100,52,24,C.accent,true);
s.speakerNotes.text='2 min. El coste integral E11 no es comparable directamente con 48 ciclos de sonda aislada E07. RAM +33 320 B, binario +3296 B. Fuente: nominal_overhead_summary.csv.';

s=base('Límites y próximos experimentos',11);
lines(s,['Sin MMU, RAMFS persistente ni herencia de prioridad','GPIO–CCOUNT <5%: no comprobable sin instrumento','Windows: 14 CRC y 901 huecos en la hora nominal','E13: medir un observador en core 1'],70,188,1110,25,87);
s.speakerNotes.text='1 min. Cero drops en placa y pérdida de enlace son contadores distintos. La rúbrica formal C2–C9 y el video de 10 min pertenecen a E20–E23; todavía no están completos.';

s=base('Cierre',12);
box(s,'Un proyecto que funciona demuestra que sabes programar.',70,240,1100,90,32,C.ink);
box(s,'Un proyecto que se mide a sí mismo demuestra que sabes de sistemas.',70,360,1110,155,34,C.accent,true);
s.speakerNotes.text='Cierre y preguntas. No declarar CP8 alcanzado: faltan video publicado, dos ensayos cronometrados y simulacro, entre otros criterios.';

await fs.mkdir(build,{recursive:true});
await fs.mkdir(path.dirname(output),{recursive:true});
for (let i=0;i<presentation.slides.items.length;i++) {
  const png=await presentation.export({slide:presentation.slides.items[i],format:'png',scale:1});
  await fs.writeFile(path.join(build,`slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await png.arrayBuffer()));
}
const candidate=path.join(build,'candidate.pptx');
await (await PresentationFile.exportPptx(presentation)).save(candidate);
const {finalizePresentation}=await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')).href);
const result=await finalizePresentation({
  explicitTotalSlideCount:12,
  workspaceDir:root,
  candidatePath:candidate,
  finalPath:output,
  pythonExecutable:runtimePython,
  integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),
  layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
  layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-heading-fit'],
  fontPolicy:{basis:'design',families:['Aptos']},
  verifyArtifactToolImport:true,
  receiptPath:path.join(build,'validation.json'),
});
console.log(JSON.stringify({output,slides:presentation.slides.items.length,result}));
