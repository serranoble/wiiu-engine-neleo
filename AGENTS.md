# Protocolo de Trabajo Obligatorio - Proyecto Wii U Framework & Demos

Este documento establece las reglas obligatorias e inviolables de desarrollo, conducta y arquitectura para cualquier agente o asistente que trabaje en este repositorio.

---

## 1. Plan de Trabajo Previo y Aprobación Obligatoria
- **NUNCA** modificar o crear código sin haber presentado previamente al usuario un plan de trabajo detallado.
- El plan debe contener:
  1. Diagnóstico del problema, síntoma o requerimiento.
  2. Causa raíz identificada a nivel de código o silicio.
  3. Propuesta de solución técnica paso a paso.
  4. Lista explícita de archivos a crear, editar o eliminar.
- **Detenerse y solicitar visto bueno:** El agente debe esperar la confirmación o retroalimentación del usuario antes de proceder a la edición de archivos.

---

## 2. Auditoría Obligatoria con Subagentes Revisores
- Antes de dar por finalizada cualquier implementación, corrección de bugs o refactorización significativa, es **ESTRICTAMENTE OBLIGATORIO** invocar a los subagentes revisores configurados en `.agents/agents/`:
  - `engine_clean_code_reviewer`:
    - Valida arquitectura limpia C++20, principios de ingeniería orientada a datos (DOD/ECS).
    - Higiene de dependencias, diseño modular y preservación de convenciones de nombrado.
    - Cero regresiones y validación de seguridad de memoria.
  - `graphics_code_reviewer`:
    - Valida silicio Wii U (CPU Espresso Tri-Core 750CL y GPU AMD Latte R700 / GX2).
    - Higiene de GX2ContextState, Render Targets, Vertex Fetch Shaders y coherencia de cachés (GX2Invalidate / DCFlushRange).
    - Descarte de deadlocks multihilo en Cafe OS y uso adecuado de MEM1/MEM2.
- Los reportes y observaciones de ambos revisores deben integrarse en la respuesta al usuario.

---

## 3. Prohibición Absoluta de Despliegues FTP Autónomos
- El agente tiene **ESTRICTAMENTE PROHIBIDO** subir archivos o binarios por FTP a la consola Wii U sin una orden explícita y directa del usuario en el turno actual.
- Aunque el binario (`.wuhb`, `.rpx` o `.elf`) se compile con éxito:
  1. Se informa que la compilación local concluyó exitosamente.
  2. Se indica el tamaño del archivo generado en `build/`.
  3. Se pregunta al usuario si desea que se proceda con la transferencia FTP a la consola.
- Solamente si el usuario responde afirmativamente (ej. "sube el binario", "transfiere a la consola", "sí, súbelo"), se ejecuta el comando de despliegue FTP.

---

## 4. Transparencia y Explicación Post-Ejecución
- Cada vez que se realicen modificaciones aprobadas, el agente debe:
  - Explicar detalladamente qué cambios se realizaron en cada componente y por qué.
  - Incluir enlaces markdown clickeables a los archivos y líneas relevantes (`file:///...`).
  - Resumir el impacto técnico de la solución aplicada.

---

## 5. Normas Técnicas y de Estilo de Comunicación
- **Terminaciones de Línea:** Todos los archivos de código fuente (`.cpp`, `.hpp`, `.h`, `.c`), scripts (`.py`, `.sh`), configuraciones (`CMakeLists.txt`) y documentación (`.md`) deben mantener **estrictamente finales de línea LF (0 CRLF)**.
- **Idioma y Tono:** Responder siempre en **español neutro o chileno** (tuteo estándar: *tú*, *puedes*, *ejecuta*, *prueba*). Prohibido el voseo rioplatense (*podés*, *tenés*, *probá*, *ejecutá*).
- **Rendimiento y Silicio:**
  - Cero asignaciones dinámicas en el bucle caliente (60 FPS).
  - Manejo de memoria consciente de MEM1 (16 MB rápidos) y MEM2 (2 GB principales).
  - Limpieza y sincronización de contextos gráficos al alternar entre SDL2 y GX2.

---

## 6. Cobertura Obligatoria de Pruebas y Validaciones del Motor
- **Todo lo desarrollado debe contar con una prueba automatizada y verificable:**
  - Cada nuevo subsistema, funcionalidad gráfica o corrección de bug crítico debe incorporar su validación correspondiente en el orquestador unificado `EngineSelfTestSuite` (Zero-Alloc en consulta, reporte estructurado por subsistema).
  - Si la funcionalidad o corrección protege un invariante de silicio, memoria o arquitectura (p. ej. `GX2DepthBuffer`, orden de registros HiZ, guardas ProcUI, DAG), debe añadirse además un chequeo estático en `scripts/agent_harness.py check`.
  - El avance y las brechas pendientes por cada una de las 8 librerías del motor se rastrean en [`docs/ENGINE_VALIDATION_PLAN.md`](docs/ENGINE_VALIDATION_PLAN.md).
