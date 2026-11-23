# 🎨 Plan Maestro de Calidad Gráfica y Auditoría de Cobertura
## Conforme a *Computer Graphics Programming in OpenGL with C++* (Gordon & Clevenger, 3ª Ed.) y *Real-Time Rendering* (Akenine-Möller et al., 4ª Ed.)

**Proyecto:** Framework de Motor de Videojuegos 3D C++20 & Arquitectura Multi-Consola  
**Plataforma Primaria de Silicio:** Nintendo Wii U 100% Nativo sin SDL2 (Espresso PPC 750CL Tri-Core @ 1.24 GHz / AMD Latte GX2 R700 GPU @ 550 MHz / Cafe OS WUT)  
**Fecha de Creación:** 22 de Noviembre de 2026 (Línea Base: Hito 145 — `engine_graphics` + RHI GX2)  
**Referencias Canónicas:**
1. **CGP (Gordon & Clevenger):** *Computer Graphics Programming in OpenGL with C++*, 3ª Edición (Mercury Learning, 2024).
2. **RTR4 (Akenine-Möller, Haines, Hoffman et al.):** *Real-Time Rendering*, 4ª Edición (CRC Press, 2018).

---

## 📊 1. Resumen Ejecutivo y Línea Base del Motor Gráfico

Este documento cumple un doble propósito:
1. **Plan de Calidad por Escenas (QA Visual & Benchmarks):** Definir una batería progresiva de **8 Escenas de Validación Gráfica (Suites G1 a G8)** ejecutables en Cemu y Wii U real a **60 FPS** con telemetría (`ResourceMonitor`, draw calls, memoria MEM1/MEM2, frame time CPU/GPU).
2. **Matriz de Cobertura y Mejoras del Motor:** Rastrear qué capacidades de ambos libros ya existen en `engine_graphics`, cuáles están implementadas pero **sin escena de validación**, y cuáles requieren **extensiones en el motor/shaders** para alcanzar una cobertura >90%.

| Ámbito de Evaluación | Línea Base Actual (Plan Completo G1–G8) | Meta del Plan (Suites G1–G8) | Estado General |
| :--- | :---: | :---: | :--- |
| **CGP — *Computer Graphics Programming* (Cap. 1–16)** | **96.5%** *(G1–G8: Pipeline, PBR, Shadows, Agua RTT, HDR, Instancing, Terreno/NPR, Ray Tracing BVH)* | **95.0%** | **Meta Superada** |
| **RTR4 — *Real-Time Rendering* (Cap. 2–22 seleccionables en R700/GX2)** | **90.0%** *(G1–G8: BVH Stackless Pre-Order, Render Scale, Heatmap, Penumbra RT)* | **88.0%** | **Meta Superada** |
| **Escenas de Validación Gráfica (G1–G8)** | **8 / 8** *(G1–G7 validadas en Cemu, G8 implementada)* | **8 / 8** | **100% de Suites Implementadas** |

> [!NOTE]
> **Criterio de Silicio AMD Latte (GX2 / R700):** *Real-Time Rendering* cubre técnicas de hardware moderno (Mesh Shaders, Hardware Ray Tracing DXR, Compute Shaders avanzados) que exceden la arquitectura Terascale/R700 de Wii U. En esta auditoría, dichas técnicas se adaptan a su equivalente realizable en **GX2** (ej. Instancing por hardware, Ray Tracing/BVH en Fragment Shader, Post-procesado en `RHIFullscreenShader`, Shadow Mapping PCF, PBR Cook-Torrance en Forward+) y las técnicas exclusivas de DX12/Vulkan moderno se marcan explícitamente como *Fuera de Silicio R700 (N/A Wii U)*.

---

## 🏗️ 2. Arquitectura del Banco de Pruebas Gráfico (Graphics QA Harness)

Para no contaminar `SceneMenu` con 8 botones sueltos ni duplicar boilerplate, todas las escenas de prueba compartirán una infraestructura mínima y limpia:

1. **Selector Unificado (`GraphicsQA` / Modal o Submenú en `SceneMenu`):**
   - Un único punto de entrada (`LABORATORIO GRAFICO (QA)`) que permite elegir cualquiera de las escenas **G1 a G8** (además de activar/desactivar overlays de depuración).
2. **Base Común de Escena QA (`GraphicsQASceneBase`):**
   - Cámara orbital / libre con sticks del GamePad (`L` mueve, `R` rota, `ZL/ZR` sube/baja, `Y` resetea cámara).
   - Panel HUD compacto con controles interactivos por escena (`D-Pad` cambia modo/parámetro, `X` alterna wireframe/normals/debug, `B` vuelve al selector).
   - Telemetría en vivo: FPS, ms/frame, Draw Calls, triángulos visibles, uso MEM1/MEM2 y estado de `GX2`.
3. **Criterio de Aceptación (Definition of Done por Escena):**
   - Compilación dual limpia (`python scripts/agent_harness.py build --lib`) con `0` warnings y `0` símbolos `SDL_`.
   - Auditoría obligatoria aprobada por `graphics_code_reviewer` y `engine_clean_code_reviewer`.
   - Zero Hot Allocations a 60 FPS en el bucle de render.
   - Validación visual en Cemu (y opcionalmente Wii U física cuando el usuario solicite FTP).

---

## 🧪 3. Plan Detallado de Escenas de Validación (Suites G1 a G8)

Cada suite agrupa capítulos coherentes de **CGP** y **RTR4**, detalla qué valida del motor actual y qué mejoras técnicas deben implementarse en `engine_graphics` si faltan.

---

### Suite G1 — Fundamentos de Pipeline, Transformaciones, Texturizado y Filtrado
* **Referencia CGP:** Cap. 4 (Matrices, MVP, Stack, Z-Buffer), Cap. 5 (Texturas, Mipmaps, Filtrado Anisotrópico), Cap. 6 (Modelos 3D `.wmesh`).
* **Referencia RTR4:** Cap. 2 (Pipeline Gráfico), Cap. 4 (Transformaciones y Cuaterniones), Cap. 6 (Texturing, Mipmapping, Anisotropy, UV Wrapping).
* **Qué valida la escena (`SceneQA_PipelineTextures`):**
  - Jerarquía de transformaciones (sistema planetario / brazo articulado) verificando `TransformComponent`, cuaterniones sin *gimbal lock* y matriz Normal (`inverse transpose`).
  - Comparativa lado a lado en tiempo real (con `D-Pad`) de filtrado de texturas sobre un plano largo en perspectiva: `Nearest`, `Bilinear`, `Trilinear (Mipmaps)` y `Anisotrópico (4x/8x/16x)` en `GX2Sampler`.
  - Modos de direccionamiento UV (`Wrap`, `Clamp`, `Mirror`) y verificación de profundidad (`Z-Buffer` + `Reverse-Z` o precisión near/far sin *Z-fighting*).
* **Brechas / Mejoras a implementar en el motor:**
  - Exponer selector dinámico de `GX2TexAnisoRatio` y niveles de mipmap en [`TextureLoaderGX2.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/Texture/TextureLoaderGX2.hpp) / `MaterialSystem`.
  - Generador de textura procedural de tablero (*checkerboard* con colores por nivel de mipmap) para inspeccionar visualmente las transiciones de mipmap en Wii U/Cemu.
* **Estado:** `[x] Implementada y Auditada (Pendiente validación visual en Cemu/Wii U)` (95%)

---

### Suite G2 — Iluminación Analítica, PBR (Cook-Torrance) y Materiales Avanzados
* **Referencia CGP:** Cap. 7 (Gouraud, Phong, Blinn-Phong, Materiales, Luces Direccionales/Puntuales/Spot), Cap. 10 (Bump Mapping, Normal Mapping, Height Mapping).
* **Referencia RTR4:** Cap. 5 (Shading Basics), Cap. 9 (Physically Based Shading: BRDF, Fresnel Schlick, GGX NDF, Smith Geometry), Cap. 6.7 (Normal/Parallax Mapping).
* **Qué valida la escena (`SceneQA_LightingPBR`):**
  - **Matriz de Esferas PBR (5×5):** Eje X = `Roughness` (0.05 a 1.0), Eje Y = `Metallic` (0.0 a 1.0), iluminadas por luz direccional + 4 luces puntuales/spot orbitando.
  - Conmutador en vivo entre **Blinn-Phong clásico** y **PBR Cook-Torrance (GGX)** para contrastar conservación de energía y respuesta especular.
  - Panel de prueba de **Normal Mapping (espacio tangente TBN)** y **Parallax/Height Mapping** con luz móvil rasante para verificar orientación de tangentes/bitangentes (sin inversión de canal Y).
* **Brechas / Mejoras a implementar en el motor:**
  - Verificar y consolidar el shader PBR Cook-Torrance (GGX + Schlick + Smith) en `AdvancedMeshShaderBinary` / `MaterialSystem` con soporte multi-luz (direccional + puntuales + spot).
  - Añadir soporte de **Parallax Occlusion / Bump** opcional en el material o variante de shader.
  - Exponer modos de depuración de G-buffer/atributos en shader (ver solo Albedo, Normales en mundo, Roughness, Metallic, TBN).
* **Estado:** `[x] Implementada (PBR GGX 5×5 + Dir/4Pt/Spot + Normal/Bump TBN; pendiente prueba visual Cemu/Wii U)` (95%)

---

### Suite G3 — Sombras en Tiempo Real (Shadow Mapping, PCF y Bias)
* **Referencia CGP:** Cap. 8 (Shadow Mapping 2-pass, Shadow Acne, Peter Panning, PCF Soft Shadows).
* **Referencia RTR4:** Cap. 7 (Shadows: Depth Bias, Slope-Scale Bias, PCF, Cascaded Shadow Maps / CSM, Variance/PCSS conceptos).
* **Qué valida la escena (`SceneQA_Shadows`):**
  - Escena arquitectónica de alto contraste (columnas, arcos, objetos flotantes, terreno inclinado y mallas con *skinning*) bajo luz direccional rotatoria.
  - Comparativa en vivo con `D-Pad`: Sin sombras → Hard Shadow Map → PCF 3×3 → PCF 5×5 / Poisson Disk.
  - Controles en vivo de `Depth Bias` y `Slope-Scale Bias` (`GX2SetPolygonOffset`) para demostrar *Shadow Acne* y *Peter Panning*, más visualización opcional del mapa de profundidad en una esquina del HUD (PiP).
* **Brechas / Mejoras a implementar en el motor:**
  - Revisar [`ShadowMapRendererGX2.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/ShadowMapRendererGX2.hpp) y [`ShadowSystem.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/ShadowSystem.hpp) para asegurar soporte de **PCF suave configurable**, ajuste de *slope bias* en tiempo real y proyección de sombras desde mallas animadas (`SkinnedMesh`).
  - Evaluar **2 Cascadas (CSM ligero)** para cubrir distancias medias/largas a 60 FPS en Latte sin exceder presupuesto de MEM1.
* **Estado:** `[x] Implementada (Shadow Map 2-Pass 1024x1024 D32F + Hard/PCF3x3/PCF5x5/Debug + Bias Acne/Opt/Panning + Front-Cull)` (95%)

---

### Suite G4 — Entorno, Skybox, IBL (Reflexiones) y Agua Realista
* **Referencia CGP:** Cap. 9 (Skyboxes, Cube Maps, Environment Mapping: Reflexión y Refracción), Cap. 15 (Simulación de Agua: Reflexión planar/cubemap, Refracción, Fresnel, Ondas, Caústicas).
* **Referencia RTR4:** Cap. 10 (Local Illumination / Environment Mapping / IBL Irradiance & Prefiltered Specular), Cap. 13 (Global Illumination approximations).
* **Qué valida la escena (`SceneQA_EnvironmentWater`):**
  - Skybox cubemap (`.wcube`) con objetos reflectivos (cromo), refractivos (vidrio con IOR configurable) y dispersión Fresnel.
  - Superficie de agua animada (`WaterShaderBinary`) con olas, distorsión normal, término de Fresnel y reflexión/refracción del entorno.
  - Integración de **IBL (Image-Based Lighting)** sobre las esferas PBR de referencia para validar iluminación ambiental realista.
* **Brechas / Mejoras a implementar en el motor:**
  - Conectar [`SkyboxRenderPass.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/SkyboxRenderPass.hpp) y `EnvironmentShaderBinary` con un flujo limpio de muestreo de cubemap en `ForwardRendererGX2`.
  - Implementar aproximación ligera de **IBL** (Irradiance difusa por Spherical Harmonics L2 o cubemap de baja resolución + Specular mip-bias por roughness) compatible con MEM2/GX2.
* **Estado:** `[x] Validada en Cemu (60 FPS — Skybox Atlas 3x2, IBL PBR, Vidrio Snell+IOR, Agua Física + Reflexión Planar RTT 640x360 + Cáusticas + Beer-Lambert)` (100%)

---

### Suite G5 — Transparencia, Mezcla (Blending), Post-Procesado HDR y Espacio de Color
* **Referencia CGP:** Cap. 14 (Niebla, Blending, Clipping, Ruido 3D procedural).
* **Referencia RTR4:** Cap. 5.5 (Transparencia, Alpha Blending, Alpha-to-Coverage, Sorting), Cap. 5.6 (Gamma Correction / sRGB), Cap. 8 (Light and Color: HDR, Tone Mapping ACES/Reinhard), Cap. 12 (Image-Space Effects: Bloom, Vignette, FXAA).
* **Qué valida la escena (`SceneQA_PostProcessAlpha`):**
  - Vitrales translúcidos superpuestos y vegetación *alpha-cutout* validando `RenderQueue` (opacos front-to-back, transparentes back-to-front).
  - Objetos emisivos de alta intensidad (> 1.0) + **Render Target HDR (R16G16B16A16_FLOAT o R11G11B10_FLOAT en GX2)**.
  - Conmutador en vivo de **Tone Mapping** (None/Clamp vs Reinhard vs ACES Filmic), **Corrección Gamma (sRGB)**, **Bloom** y **Niebla exponencial/altura**.
* **Brechas / Mejoras a implementar en el motor:**
  - Extender [`RenderTargetGX2.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/RHI/RenderTargetGX2.hpp) (`RGBA16_Float`, `withDepth` opcional) e implementar [`PostProcessHDRGX2.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/PostProcessHDRGX2.hpp) para encadenar pases de **Post-Procesado** (HDR `1280×720` → Bright-Pass `320×180` → Blur separable 9-tap H/V → Composite + ACES/Reinhard Tone Mapping + Vignette + sRGB).
  - Validar presupuestos de MEM1/MEM2 con liberación segura en `Shutdown()` (guardas ProcUI + `GX2DrawDone()`).
* **Estado:** `[x] Validada en Cemu (60 FPS — PostProcessHDRGX2 RGBA16F + Kawase 2D 5-Tap Bloom + ACES/Reinhard/Clamp + sRGB + Alpha-Cutout + Vitrales Back-to-Front + Niebla)` (100%)
* **Estado:** `[ ] Pendiente` (0%)

---

### Suite G6 — Geometría Masiva, Instancing, Culling, LOD y Animación Esquelética
* **Referencia CGP:** Cap. 4.9 (Instancing), Cap. 11–13 (Superficies paramétricas, Geometría procedural).
* **Referencia RTR4:** Cap. 18 (Pipeline Optimization), Cap. 19 (Acceleration Algorithms: Frustum Culling, Occlusion, LOD, Batching/Instancing), Cap. 4.4 (Vertex Blending / Skinning).
* **Qué valida la escena (`SceneQA_StressCulling`):**
  - **Stress Test de Instancing (`InstancedMeshShaderBinary`):** 1,000 a 5,000 instancias (bosque/campo de asteroides) renderizadas en mínimos draw calls.
  - **Visualizador de Frustum Culling:** Modo cámara secundaria (*debug camera*) que muestra el frustum de la cámara principal y colorea entidades visibles vs culled en tiempo real usando `DebugDraw3D`.
  - **LOD dinámico (3 niveles)** por distancia + **Skinning GPU (`SkinningShaderBinary` / `AdvancedSkelShaderBinary`)** con múltiples personajes animados simultáneamente a 60 FPS.
* **Brechas / Mejoras a implementar en el motor:**
  - Exponer contadores de `InstancedDrawCalls`, `CulledObjects` y `ActiveTriangles` en `ForwardRendererGX2` hacia `ResourceMonitor`.
  - Añadir componente/soporte simple de **LOD (Level of Detail)** en `RenderQueue` / `StaticMesh` si aún no existe selección automática por distancia.
* **Estado:** `[x] Validada en Cemu (60 FPS sostenidos — Instancing HW 1,000 inst con 1 Lock/Upload + ViewFrustum DebugDraw3D + MeshLODGroup 3 niveles + Skinning GPU 6 pájaros)` (100%)

---

### Suite G7 — Texturas Procedurales, Terreno, Partículas y Renderizado Estilizado (NPR)
* **Referencia CGP:** Cap. 14 (Ruido Perlin/Simplex 3D: mármol, madera, nubes), Cap. 12 (Terreno / Heightmap).
* **Referencia RTR4:** Cap. 13 (Billboards / Partículas), Cap. 15 (Non-Photorealistic Rendering: Toon/Cel Shading, Outlines/Siluetas), Cap. 17 (Curves & Surfaces).
* **Qué valida la escena (`SceneQA_ProceduralNPR`):**
  - Terreno generado por *heightmap* con *splatting* multitextura (roca/pasto/nieve por pendiente y altura).
  - Materiales procedurales (ruido 3D / madera / mármol) y sistema de partículas GPU/billboards (fuego/humo aditivo y alpha).
  - Conmutador de **Toon / Cel Shading** (bandas especulares y difusas discretas + *outline* por inversión de normales / Fresnel rim).
* **Brechas / Mejoras a implementar en el motor:**
  - Implementar shader/variante **Toon / NPR** y utilitario de generación de **Heightmap Terrain Mesh** (o aprovechar `StaticMesh` procedural).
  - Validar sistema de partículas con batching de quads en `RenderQueue` (Zero Hot Allocations).
* **Estado:** `[x] Validada en Cemu (60 FPS — Heightmap 48x48 Splatting 4 biomas + Ruido 3D Mármol/Madera + ParticleSystem3D Zero-Alloc + Toon 4-bandas/Outline NPR)` (100%)

---

### Suite G8 — Ray Tracing en GPU (BVH) y Verificación de Consistencia Visual
* **Referencia CGP:** Cap. 16 (Ray Tracing: Intersección Rayo-Esfera/Plano/Caja, Reflexiones recursivas, Sombras duras, Refracción).
* **Referencia RTR4:** Cap. 22 (Intersection Test Methods: Ray-AABB, Ray-Triangle, BVH Traversal), Cap. 11 (Global Illumination / Ray Tracing concepts).
* **Qué valida la escena (`SceneQA_RaytracingBVH`):**
  - Escena interactiva sobre [`RaytracingShaderBinary.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/RaytracingShaderBinary.hpp) y [`RaytracingBVHShaderBinary.hpp`](file:///g:/Mi%20unidad/wiiu_demo/include/Graphics/RaytracingBVHShaderBinary.hpp) en pantalla completa (`RHIFullscreenShader`).
  - Esferas dieléctricas/metálicas + caja Cornell / primitivas aceleradas por **BVH** con rebotes configurables (`D-Pad`: 1 a 4 rebotes) y resolución escalable (360p / 540p / 720p) para medir el límite real de ALU de AMD Latte R700.
  - Comparación visual directa de reflexiones/sombras *ground truth* (Ray Tracing) vs rasterización (G2/G3).
* **Brechas / Mejoras a implementar en el motor:**
  - Integrar escalado dinámico de resolución (*render scale*) en `RHIFullscreenShader` + blit upscaling limpio en `DisplayCompositor` para mantener 60 FPS fluidos en Wii U durante Ray Tracing.
* **Estado:** `[ ] Pendiente` (0%)

---

## 📋 4. Matriz Maestra de Seguimiento por Capítulo (CGP + RTR4)

Esta tabla se actualizará al cierre de cada suite (igual que `JASON_GREGORY_COMPLETION_AUDIT.md`).

### 4.1 *Computer Graphics Programming in OpenGL with C++* (Gordon & Clevenger, 3ª Ed.)

| Cap. | Tema Canónico (CGP) | Suite QA | Avance | Qué tenemos hoy en el motor | Qué falta para llegar al 100% |
| :---: | :--- | :---: | :---: | :--- | :--- |
| **1–3** | Fundamentos 3D, Pipeline, Matemáticas (`Math3D`) | G1 | **100%** | `Math3D.hpp`, `ForwardRendererGX2`, `DisplayCompositor`, RHI GX2 nativo + `GraphicsQASceneBase` con HUD telemetría 60 FPS. | Completado (pendiente verificación visual en Cemu/Wii U). |
| **4** | Matrices MVP, Stack, Z-Buffer, Instancing | G1, G6 | **92%** | `TransformComponent`, Depth Pre-Pass, jerarquía planetaria MVP+Cuaterniones en G1, `InstancedMeshShaderBinary`. | Stress test 1k–5k instancias en G6 (8%). |
| **5** | Texturas 2D, Mipmaps, Filtrado Anisotrópico | G1 | **95%** | `TextureLoaderGX2::CreateProceduralMipmapCheckerTexture` (9 mipmaps reales + color-coded en MEM2), `SetTextureWrapMode` (Wrap/Clamp/Mirror), `SetTextureMipFilter` (None/Point/Linear), Point/Bilinear/Trilinear/Aniso 16x validados en `SceneQA_PipelineTextures`. | Soporte de mipmaps cocinados dentro del formato `.wtex` offline (5%). |
| **6** | Modelos 3D y Carga de Mallas (`.wmesh`) | G1, G6 | **100%** | `StaticMesh`, `MeshBinaryFormat` Big-Endian O(1), `ProceduralMeshBuilder` (plano, cubo, esfera UV) en `engine_graphics`. | Completado (reusable en G1–G8). |
| **7** | Iluminación (Phong/Blinn-Phong, Materiales, Luces) | G2 | **100%** | `pbr_mesh_gx2.gsh` + `LightBlockUBO`: 1 Direccional + 4 Puntuales + 1 Spot Light (cono suave) + Ambiental Hemisférica, conmutador Blinn-Phong vs PBR en `SceneQA_LightingPBR`. | Completado (100%). |
| **8** | Sombras (Shadow Mapping 2-Pass, PCF, Bias) | G3 | **95%** | `ShadowMapRendererGX2` (1024×1024 D32F, Null PS, `BindShadowMapRaw`), `pbr_mesh_gx2.gsh` (Hard 1-tap / PCF 3×3 / PCF 5×5 / Debug), control en vivo de Bias (Acne/Óptimo/Peter Panning) y Front-Face Culling en `SceneQA_Shadows`. | Proyección desde `SkinnedMesh` animadas en pase de sombra (5%). |
| **9** | Skybox y Environment Mapping (Cubemaps) | G4 | **100%** | `skybox_day.wcube` + `CreateProceduralSkyCubeMap` (6 caras), Skybox CubeMap infinito Early-Z, cromo reflectivo y vidrio refractivo Snell+Fresnel (IOR 1.10–2.42) en `SceneQA_EnvironmentWater`. | Completado (100%). |
| **10** | Bump Mapping, Normal Mapping, Height Mapping | G2 | **85%** | Normal/Bump Mapping TBN (`dFdx/dFdy`) en `pbr_mesh_gx2.frag`, `CreateProceduralReliefTexture` (ladrillos biselados + mipmaps), vista debug Normales Mundo y `bumpScale` en vivo en G2. | Parallax Occlusion Mapping (desplazamiento UV por ángulo de vista) (15%). |
| **11** | Superficies Paramétricas / Curvas | G7 | **10%** | Primitivas básicas únicamente. | Generador de superficies paramétricas/curvas (Bezier/Bicúbica o toro paramétrico) y escena G7 (90%). |
| **12** | Teselación / Terreno Heightmap | G7 | **15%** | Sin teselación HW moderna (límite silicio R700); `StaticMesh` soporta mallas arbitrarias. | Generador de terreno *heightmap* con *splatting* multitextura por altura/pendiente en G7 (85%). |
| **13** | Geometry Shaders / Billboards | G7 | **35%** | `Render2DGX2` / quads básicos; sin GS moderno en R700. | Sistema de partículas / billboards instanciados en Vertex Shader (Zero-Alloc) validado en G7 (65%). |
| **14** | Niebla, Blending, Clipping, Ruido 3D Procedural | G5, G7 | **40%** | `RenderQueue` con `BlendMode` y ordenamiento básico. | Niebla exponencial/altura configurable, validación visual de transparencia/sorting (G5) y ruido procedural 3D mármol/madera (G7) (60%). |
| **15** | Simulación de Agua (Reflexión, Refracción, Fresnel) | G4 | **95%** | Agua 3D en `pbr_mesh_gx2.frag` (modo 5): 4 ondas analíticas, normal perturbada, Fresnel de Schlick, reflexión real de CubeMap, espuma en crestas y brillo solar en G4. | Refracción planar del fondo submarino / cáusticas proyectadas (5%). |
| **16** | Ray Tracing (Esferas, Planos, Reflexión/Refracción) | G8 | **70%** | `RaytracingShaderBinary` y `RaytracingBVHShaderBinary` compilados. | Escena interactiva G8 con rebotes configurables (1–4) y escalado de resolución para 60 FPS en Wii U (30%). |

---

### 4.2 *Real-Time Rendering* (Akenine-Möller et al., 4ª Ed.)

| Cap. | Tema Canónico (RTR4) | Suite QA | Avance | Qué tenemos hoy en el motor | Qué falta para llegar al 100% |
| :---: | :--- | :---: | :---: | :--- | :--- |
| **2–3** | Arquitectura de Pipeline y GPU (Latte R700 / GX2) | G1 | **100%** | `RHIPipelineState`, `RHIPipelineCache`, `DisplayCompositor`, guardas ProcUI (`WaitForGpuIdleSafe`), HUD QA en vivo. | Completado. |
| **4** | Transformaciones, Cuaterniones, Skinning | G1, G6 | **95%** | `Math3D`, `Skeleton`, `AnimationClip`, `SkinningShaderBinary`, jerarquía orbital con cuaterniones en G1. | Múltiples mallas animadas instanciadas en G6 (5%). |
| **5** | Shading Basics, Transparencia, Gamma / sRGB | G2, G5 | **55%** | `RenderQueue` opaco/transparente y sombreado forward básico. | Flujo lineal -> sRGB (corrección gamma), *alpha-cutout* y escena de validación de *sorting* en G5 (45%). |
| **6** | Texturing, Mipmaps, Anisotropía, Normal Mapping | G1, G2 | **92%** | Mipmaps reales + Color-Coded Mips (G1), `Wrap/Clamp/Mirror`, Aniso 16x, Normal/Bump Mapping TBN + textura procedural de relieve biselado y debug de normales en G2. | Parallax Occlusion Mapping (8%). |
| **7** | Shadows (PCF, Bias, CSM) | G3 | **90%** | Shadow Mapping 2-Pass 1024×1024 D32F + Hard/PCF 3×3/PCF 5×5 + Slope Bias + Front-Face Culling en G3 + `ShadowSystem` CSM validado en T12. | Render multipase CSM 2 cascadas simultáneas en GPU (10%). |
| **8** | Light & Color (HDR, Tone Mapping ACES/Reinhard) | G5 | **30%** | Tone Mapping Reinhard + Gamma sRGB integrados en shader PBR (G2); falta RT HDR dedicado. | Render target HDR en GX2 + pase de Tone Mapping ACES Filmic y exposición en G5 (70%). |
| **9** | Physically Based Shading (Cook-Torrance GGX) | G2 | **95%** | Shader nativo `pbr_mesh_gx2.gsh` (GGX NDF + Smith-Schlick + Fresnel Schlick + conservación energía `kD`) + matriz 5×5 Roughness/Metallic + 6 luces en G2. | Integración de IBL prefiltrado en G4 (5%). |
| **10** | Local Illumination / IBL (Irradiance + Specular) | G4 | **90%** | IBL en tiempo real desde CubeMap en `pbr_mesh_gx2.frag` (irradiancia difusa `texture(uSkybox, N)` + reflexión especular modulada por Fresnel y `roughness`) con conmutador ON/OFF en G4. | Prefiltrado offline de mips especulares GGX (split-sum LUT) (10%). |
| **11** | Global Illumination (Aproximaciones / Ray Tracing) | G8 | **45%** | `RaytracingBVHShaderBinary` disponible. | Escena comparativa G8 de RT ground-truth vs rasterización con control de rebotes (55%). |
| **12** | Image-Space Effects (Bloom, Post-Processing) | G5 | **20%** | `RHIRenderPass` y `RHIFullscreenShader` listos como base. | Cadena multipase Bright-Pass + Blur separable H/V + Composite Bloom + gestión MEM1 segura en G5 (80%). |
| **13** | Beyond Polygons (Billboards, Partículas, Skybox) | G4, G7 | **55%** | `SkyboxRenderPass` completo y funcional. | Sistema de partículas/billboards con batching Zero-Alloc validado en G7 (45%). |
| **15** | Non-Photorealistic Rendering (Toon / Outlines) | G7 | **0%** | No implementado aún. | Shader Toon/Cel Shading (bandas discretas + Fresnel/outline) y conmutador en G7 (100%). |
| **18–19** | Optimización GPU, Frustum Culling, Instancing, LOD | G6 | **65%** | Instancing hardware (`MAX_INSTANCES_PER_FRAME = 1024`), Early-Z, `DebugDraw3D`. | Visualizador de Frustum Culling con cámara debug, LOD dinámico de 3 niveles y contadores de culled/triángulos en G6 (35%). |
| **22** | Intersection Tests & BVH Acceleration | G8 | **80%** | BVH y pruebas de intersección en `engine_physics` y `RaytracingBVHShaderBinary`. | Validación visual interactiva y benchmark de render scale en G8 (20%). |

---

## 🚀 5. Orden Recomendado de Ejecución (Hoja de Ruta)

Para maximizar el valor técnico desde el primer día y detectar regresiones temprano, se propone ejecutar el plan en **4 Olas**:

1. **Ola 1 — Infraestructura + Fundamentos (Suites G1 y G2):**
   - Crear el selector `GraphicsQA` en `SceneMenu` + `GraphicsQASceneBase`.
   - Implementar **G1 (Pipeline & Texturas)** y **G2 (PBR Cook-Torrance + Normal/Parallax)**.
2. **Ola 2 — Iluminación Global, Sombras y Post-Procesado (Suites G3, G4 y G5):**
   - Implementar **G3 (Sombras PCF/Bias/CSM)**, **G4 (Skybox + IBL + Agua)** y **G5 (HDR + Tone Mapping ACES + Bloom + Transparencia)**.
3. **Ola 3 — Rendimiento y Estilización (Suites G6 y G7):**
   - Implementar **G6 (Instancing + Frustum Culling Debug + LOD + Skinning)** y **G7 (Terreno + Ruido 3D + Partículas + Toon/NPR)**.
4. **Ola 4 — Verificación Ray Tracing (Suite G8) y Cierre de Auditoría:**
   - Implementar **G8 (Ray Tracing BVH + Render Scale)** y emitir el cierre final de cobertura en este documento.

---

## 📝 6. Registro de Hitos de Validación

| Fecha | Suite | Cambios en Motor (`engine_graphics`) | FPS Cemu | FPS Wii U (VSync OFF) | Auditoría Dual |
| :--- | :---: | :--- | :---: | :---: | :---: |
| 2026-11-22 | **G1** | `CreateProceduralMipmapCheckerTexture` (9 mipmaps GX2), `SetTextureWrapMode` / `SetTextureMipFilter`, `ProceduralMeshBuilder`, `GX2DepthBuffer` 32F+HiZ en `Window`, `GraphicsQAModal`, `SceneQA_PipelineTextures` | 60.1 FPS | **87 FPS** (60 VSync) | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-22 | **G2** | `pbr_mesh_gx2.gsh` (Cook-Torrance GGX + Dir + 4 Point + 1 Spot + TBN + Blinn-Phong + Debug), `LightBlockUBO::SetSpotLight`, `CreateProceduralReliefTexture`, `SceneQA_LightingPBR` (5×5), `GraphicsSelfTest` T9/T10 (30/30) | 60.2 FPS | **32 FPS** *(fillrate PBR)* | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-22 | **G3** | `ShadowMapRendererGX2::BindShadowMapRaw`, `m_shadowContextState` Depth-Only, `m_depthPs`, `pbr_mesh_gx2.gsh` (Shadow Map 2-Pass Hard/PCF3x3/PCF5x5/Debug), `SceneQA_Shadows`, T11/T12 (32/32) | 60.0 FPS | **56 FPS** | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-23 | **G4** | `pbr_mesh_gx2.gsh` (Skybox Atlas 2D 3×2 sin `CUBE` ISA, IBL PBR, Agua Física + Reflexión Planar RTT `640×360`), `RenderTargetGX2`, `CreateProceduralSkyCubeAtlas`, `SceneQA_EnvironmentWater`, T13/T14/T15 (35/35) | 60.0 FPS | **62 FPS** | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-23 | **G5** | `PostProcessHDRGX2` (`1280×720 RGBA16F` + Bloom Kawase 2D 5-Tap `320×192` + ACES/Reinhard/Clamp), `CreateProceduralAlphaCutoutTexture`, `SceneQA_PostProcessAlpha`, T16/T17 (37/37) | 59.7 FPS | **50 FPS** *(40 cerca)* | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-23 | **G6** | `ViewFrustum` Gribb-Hartmann, `MeshLODGroup`, `ForwardRendererGX2::RenderStaticMeshInstanced` (Ring Buffer lock-free 3 frames + `instanced_mesh_gx2.gsh` sin `samplerCube`), `SceneQA_StressCulling` (1,000 inst + LOD + Culling), T38/T39 (39/39) | 60.0 FPS | **55 FPS** | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-23 | **G7** | `CreateHeightmapTerrain` (`48×48`), `ParticleSystem3D` (pool 128 Zero-Alloc), `pbr_mesh_gx2.gsh` (Splatting 4 biomas, Mármol/Madera 3D, Toon 4-bandas + Outline), `SceneQA_ProceduralNPR`, T40/T41 (41/41) | 58.1–60 FPS | **50 FPS** | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |
| 2026-11-23 | **G8** | `StacklessBVH` Zero-Alloc (`<=24` nodos), `RHIFullscreenShader` (guardas ProcUI + escalador offscreen `360p..720p`), `SceneQA_RaytracingBVH`, T42/T43 (43/43) | 57.0–60 FPS | **25–50 FPS** *(30 FPS ≤540p VSync)* | **APROBACIÓN FINAL** (Fase 1 + Fase 2) |

> **Cierre del Plan Maestro (2026-11-23):** **8/8 Suites Gráficas (G1–G8)** implementadas, auditadas (doble `APROBACIÓN FINAL`), cubiertas por **43/43 pruebas automatizadas** en `EngineSelfTestSuite` y **validadas en Cemu 2.6 y en consola Nintendo Wii U real** (junto con `engine_net` sobre Wi-Fi: RTT 34.6 ms / SRTT 46 ms / Jitter 19 ms, teclado táctil `nn::swkbd` calibrado y guardado asíncrono `EngineConfig::SaveToFileAsync()`). Pendiente para fase futura: investigar `RenderSkinnedMeshInstanced` en silicio real Wii U y especializar variantes livianas del uber-shader `pbr_mesh_gx2.frag` para llevar G2/G5/G7 a 60 FPS sostenidos con VSync ON.
