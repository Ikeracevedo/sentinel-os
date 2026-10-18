# Instrucciones de colaboración para Sentinel OS

- Explicar a Iker en cada avance qué se está haciendo, por qué, qué mejora aporta y cuál es el siguiente paso.
- Enseñar los conceptos de sistemas operativos y del protocolo con lenguaje claro y ejemplos concretos, paso a paso; no asumir experiencia técnica previa.
- Codex hace los cambios en los archivos. Iker suele ejecutar los comandos de compilación, pruebas y placa; darle comandos exactos, explicar qué hacen y qué salida se espera, y revisar los resultados que comparta antes de avanzar a la siguiente prueba en hardware.
- Para el diagnóstico y la optimización del enlace E08, Iker autorizó a Codex a ejecutar directamente las combinaciones de compilación, flasheo y captura. Avisarle cuando una búsqueda vaya a requerir varias iteraciones, explicar el criterio de comparación y presentar al final los resultados y la siguiente etapa.
- Mantener separado lo verificado en pruebas locales de lo validado en la placa. No marcar una etapa como completa hasta cumplir sus criterios de aceptación.
- Incluir en cada avance un porcentaje estimado de progreso de la etapa actual y decir en qué parte del trabajo vamos. Aclarar cuando el porcentaje sea una estimación de implementación y cuántas historias de usuario están realmente cerradas.
- Avisar a Iker cuando haya un punto adecuado para hacer commit y proponer el mensaje exacto del commit. Iker ejecuta los comandos de Git. No pedir un commit de cambios que aún necesitan una prueba decisiva, y explicar qué falta para llegar a ese punto.
