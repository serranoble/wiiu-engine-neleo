# Hoja de Ruta — Evolución del Motor y Modernización del Cooker (Wii U C++20 / GX2)

**Fecha:** 23 de Noviembre de 2026  
**Estado base:** Hito 146 — 8 librerías estáticas (DAG acíclico), **8/8 Suites Gráficas (G1–G8)** validadas en Cemu 2.6 y **Nintendo Wii U real**, **43/43 pruebas** en `EngineSelfTestSuite`, networking Wi-Fi validado (`RTT 34.6 ms`), teclado táctil calibrado (`nn::swkbd`) y guardado asíncrono (`EngineConfig::SaveToFileAsync()`).

---

## Referencias Bibliográficas Base

1. **Jason Gregory — *Game Engine Architecture* (3.ª Ed.):** Cap. 7 (*Resources and the File System* / *Asset Conditioning Pipeline*), Cap. 11 (*Rendering Engine*) y Cap. 12 (*Animation Systems*).
2. **Gabor Szauer — *Hands-On C++ Game Animation Programming* (2020):** Carga glTF, jerarquías de esqueletos, *Inverse Bind Pose*, LBS vs *Dual Quaternion Skinning* y empaquetado de paletas de huesos hacia GPU.
3. **Sergey Kosarevsky & Viktor Latypov — *3D Graphics Rendering Cookbook* (2021):** Optimización de mallas (`meshoptimizer`), caché de vértices, *overdraw*, LODs y dibujo instanciado/indirecto.
4. **Tomas Akenine-Möller et al. — *Real-Time Rendering* (4.ª Ed.):** Cap. 4.4 (*Skinning*) y Cap. 16 (*Polygonal Techniques*, cuantización, MikkTSpace).
5. **Eric Lengyel — *Foundations of Game Engine Development, Vol. 2: Rendering* (2019)** + **Especificación glTF 2.0 (Khronos)** + **`meshoptimizer` (Arseny Kapoulkine)**.

---

## Fase A — Crecimiento y Madurez del Motor (Prioridad 1)

Objetivo: pasar de 8 librerías validadas en suites aisladas a un motor listo para producir juegos completos a **60 FPS sostenidos con VSync ON** en Nintendo Wii U real.

### A1. Especialización de Shaders PBR (60 FPS reales con VSync)
- **Situación actual medida en Wii U real (VSync OFF):** G2 rinde **32 FPS** y G5/G7 rondan **50 FPS** porque comparten el uber-shader `pbr_mesh_gx2.frag` (`33.5 KB`, `36 GPRs`, 10 modos). Al superar `16.67 ms`, VSync los clava en 30 FPS.
- **Acción:** Crear variante liviana `pbr_fast_gx2.gsh` (`< 20 GPRs`, sin agua ni ruido 3D) para mallas PBR estándar, manteniendo variantes especializadas para Agua/IBL y Terreno/Toon.
- **Meta:** Bajar G2, G3, G5 y G7 por debajo de `16.67 ms` en AMD Latte R700.

### A2. Carga de Escenas y Materiales *Data-Driven*
- **Acción:** Definir formato ligero de escena/prefabs (INI/JSON sobre `VirtualFileSystem` + `StorageManager`) para declarar entidades, transformaciones, materiales PBR, luces y parámetros de post-proceso sin recompilar C++.
- **Integración:** Instanciación directa sobre `ECSWorld`.

### A3. *Vertical Slice* Jugable (Integración Total de las 8 Librerías)
- **Acción:** Escena jugable de referencia uniendo `engine_core` (ECS/Frustum/BVH/Config Async), `engine_physics` (KCC + colisiones), `engine_audio` (audio 3D + reverb), `engine_ai` (A* + Steering), `engine_graphics` (PBR rápido + sombras + HDR/Bloom) y `engine_net` (sincronización opcional en red).

### A4. Perfilador por Pase en `ResourceMonitor`
- **Acción:** Mostrar en el OSD el desglose de milisegundos por pase (**Sombras**, **Geometría 3D**, **Post-Proceso**, **HUD/2D**) en hardware real.

---

## Fase B — Auditoría y Estado del Arte del *Cooker* y Pipeline de Modelos (Prioridad 2)

Objetivo: erradicar la brecha entre Cemu y Wii U real en modelos cocinados (`.wmesh` / `.wskel` / `.wanim`) y elevar `tools/cooker` al estándar moderno de la industria.

### B1. Diagnóstico en Silicio Real del Cuelgue de Skinning (`bird_orange`)
- Auditar `tools/cooker/` (`cook_bird.py`, `mesh_cooker.py`, `fbx_parser.py`, `generate_skinned_character.py`, `cook_model.py`), `StaticMesh.cpp`, `ForwardRendererGX2::InitSkinningShader()` y `RenderSkinnedMeshInstanced()`:
  - Verificar *endianness* y `GX2EndianSwapMode` en `boneIndices` (`UINT_8_8_8_8`), `boneWeights` e IBO.
  - Verificar correspondencia exacta entre `GX2AttribStream` y los `attribVars` reales de `assets/shaders/advanced_skel_gx2.gsh`.
  - Evaluar migración de la paleta de huesos (`uBones[24]` / `GX2SetVertexUniformReg(0, 416, ...)`) a un **Uniform Block (`UBO`)** alineado a `256 B` o registros verificados en CafeGLSL.
  - Validar modelos animados corriendo estables en Wii U real sin congelamientos.

### B2. Modernización del *Cooker* (`tools/cooker`)
- Adoptar **glTF 2.0** como entrada canónica y añadir al *cooker*:
  - Optimización de caché de vértices post-transformación y *overdraw* (estilo `meshoptimizer`).
  - Precálculo de **AABB y Bounding Sphere** por sub-malla en la cabecera `.wmesh` (para Frustum Culling automático).
  - Generación de tangentes **MikkTSpace** y validación estricta de `stride`, alineación `256 B` y Big-Endian PowerPC en `scripts/agent_harness.py`.

### B3. Animación Esquelética Robusta en `engine_graphics`
- Soporte de *cross-fade blending* entre clips (`Idle ↔ Walk ↔ Run`), múltiples sub-mallas/materiales por modelo y cobertura en `GraphicsSelfTest`.

---

## Benchmarks de Referencia en Wii U Real (Línea Base — 2026-11-23)

| Suite / Subsistema | Cemu 2.6 | Wii U Real (VSync OFF) | Notas |
| :--- | :---: | :---: | :--- |
| **G1** (Pipeline / Mipmaps) | 60.1 FPS | **87 FPS** | 60 FPS sólidos con VSync |
| **G2** (PBR Cook-Torrance) | 60.2 FPS | **32 FPS** | Cuello de botella en fillrate (36 GPRs) |
| **G3** (Shadow Maps 2-Pass) | 60.0 FPS | **56 FPS** | Muy cerca de 60 FPS |
| **G4** (Agua Planar RTT + Skybox) | 60.0 FPS | **62 FPS** | 100% fluido |
| **G5** (HDR `RGBA16F` + Bloom) | 59.7 FPS | **50 FPS** | 40 FPS con overdraw de cerca |
| **G6** (Instancing 1,000 + LOD) | 60.0 FPS | **55 FPS** | Sin pájaro skinned (B1 pendiente) |
| **G7** (Terreno + Ruido 3D + Toon) | 58–60 FPS | **50 FPS** | Fluido |
| **G8** (Ray Tracing BVH) | 57–60 FPS | **25–50 FPS** | 30 FPS ≤ 540p con VSync |
| **`engine_net` (Wi-Fi)** | — | **RTT 34.6 ms** | SRTT 46 ms, Jitter 19 ms |
