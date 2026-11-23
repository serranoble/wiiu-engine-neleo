# 🧪 Plan Maestro de Validaciones y Self-Tests del Motor (8 Librerías)

**Proyecto:** Framework de Motor de Videojuegos 3D C++20 & Arquitectura Multi-Consola  
**Plataforma Base:** Nintendo Wii U 100% Nativo sin SDL2 (Espresso PPC 750CL / AMD Latte GX2 / Cafe OS WUT)  
**Última Actualización:** 22 de Noviembre de 2026 (Fase V2 completada — **8/8 Librerías al 100%**)  
**Regla Canónica:** `AGENTS.md` §6 — *Todo lo desarrollado debe contar con una prueba automatizada verificable en `EngineSelfTestSuite` y, cuando aplique a invariantes de silicio/arquitectura, en `scripts/agent_harness.py check`.*

---

## 📊 1. Resumen Ejecutivo de Cobertura de Validaciones

| Ámbito | Fase V1 | Fase V2 (Actual) | Estado |
| :--- | :---: | :---: | :--- |
| **Pruebas Activas en `EngineSelfTestSuite`** | 14 / 14 | **43 / 43** *(8/8 librerías + G1–G8 completos: PBR, Shadows, IBL/RTT, HDR/Bloom, Frustum/LOD, Terreno/Partículas, RT BVH)* | **100% Operativo** |
| **Chequeos Estáticos en `scripts/agent_harness.py check`** | 4 / 4 | **4 / 4** (`CRLF`, `CORE_HEADER_LEAKS`, `GX2_INVARIANTS` ampliado a `src/Graphics/`, `ELF_SDL_SYMBOLS`) | **100% Operativo** |
| **Cobertura Global de Validación de Subsistemas** | 51.0% | **100.0%** *(línea base del motor completo)* | **Completada** |

---

## 📑 2. Matriz de Validaciones por Librería (DAG de 8 Librerías)

| # | Librería (DAG) | Avance | Pruebas Activas en `EngineSelfTestSuite` | Extensión Continua (Regla 6) |
| :-: | :--- | :---: | :--- | :--- |
| **1** | `engine_core` | **100%** | **9 pruebas:** `MemoryManager::VerifyMemoryIntegrity`, `ECSWorld::VerifyECSIntegrity`, `JobSystem::VerifyJobSystemIntegrity` + `CoreSelfTest` (`PRNG_PCG32`, `CRYPTO_HMAC_SHA256` FIPS 180-4, `SPATIAL_AABB_BVH`, `ENGINE_CONFIG_CVAR`, `FRUSTUM_CULLING_MATH`, `STACKLESS_BVH_BUILDER`). | Completado (100%). |
| **2** | `engine_physics` | **100%** | **2 pruebas:** `PhysicsSelfTest` (`SPATIAL_HASH_GRID` broadphase O(1) sin duplicados + `PHYSICS_AABB_BOUNDS` colisión e integración finita). | Añadir pruebas de constraints/joints si se extienden. |
| **3** | `engine_audio` | **100%** | **2 pruebas:** `AudioSelfTest` (`AUDIO_3D_TELEMETRY` límites de Voice Pool `<=32` y buses + `AUDIO_3D_LISTENER_STATE` parámetros DSP/reverb/doppler/handle inválido y round-trip thread-safe del listener sin perturbar el hilo AX). | Añadir validación de DSP/filtros adicionales si se agregan. |
| **4** | `engine_input` | **100%** | **2 pruebas:** `InputSelfTest` (`RADIAL_DEADZONE` 0 en centro / remapeo / diagonal `<=1` + `INPUT_CONTEXT_STACK` prioridad, bloqueo y consumo). | Añadir pruebas de remapeo dinámico si se agrega. |
| **5** | `engine_graphics` | **100%** | **23 pruebas:** `GraphicsSelfTest` (`GX2_COLOR_BUFFER`, `GX2_DEPTH_BUFFER`, `GX2_CONTEXT_STATE`, `FORWARD_RENDERER`, `MATH3D_PROJ_QUAT`, `MESH_BUILDER_CCW`, `MIPMAP_CHAIN_GX2`, `RHI_PIPELINE_HASH`, `PBR_SHADER_PIPELINE`, `RELIEF_TEX_MIPMAPS`, `SHADOW_MAP_GX2`, `SHADOW_CSM_MATH`, `CUBEMAP_SKY_GX2`, `PBR_IBL_SLOTS`, `RENDER_TARGET_GX2`, `HDR_POSTPROCESS_GX2`, `ALPHA_CUTOUT_TEX`, `MESH_LOD_INSTANCING`, `HEIGHTMAP_TERRAIN_MESH`, `PARTICLE_POOL_ZERO_ALLOC`, `RAYTRACING_BVH_PIPELINE`) + `StaticMesh::VerifyMeshIntegrity` + `UI::UIToolkit::VerifyUIToolkitIntegrity`. | Completado (8/8 Suites QA cubiertas). |
| **6** | `engine_ai` | **100%** | **2 pruebas:** `AISelfTest` (`NAVGRAPH_ASTAR` ruta óptima determinista + `STEERING_BEHAVIORS` Seek/Flee/Arrive finitos). | Añadir pruebas de árboles de comportamiento compuestos si se amplían. |
| **7** | `engine_net` | **100%** | **1 suite compuesta:** `NetPlatform::VerifyNetIntegrity` (cubre HAL, `NetBitStream`, Endianness, aritmética de secuencias, `ParseIPv4` y `SnapshotInterpolator`). | Completado (100%). |
| **8** | `engine_framework` | **100%** | **2 pruebas:** `MODAL_STACK_LIFECYCLE` (push/bloqueo/pop/vacío) + `RESOURCE_MONITOR_FINITE` (FPS y MEM1/MEM2 finitos en `[0..100]`) + orquestador `EngineSelfTestSuite`. | Completado (100%). |

---

## 🔒 3. Invariantes Estáticos en `scripts/agent_harness.py check`

| Chequeo | Qué protege | Estado |
| :--- | :--- | :---: |
| `CRLF_CHECK` | Cero finales de línea CRLF en fuentes, scripts y docs. | Activo |
| `CORE_HEADER_LEAKS` | Cero includes `<gx2/*>`, `<gx2r/*>`, `<SDL2/*>` en `include/Core/*.hpp`. | Activo |
| `ELF_SDL_SYMBOLS` | Cero símbolos `SDL_` en `build/wiiu_demos.elf`. | Activo |
| `GX2_INVARIANTS` | En `Window.cpp`: presencia obligatoria de `GX2SetColorBuffer`, `GX2SetDepthBuffer`, `GX2ClearDepthStencilEx`, `GX2InitDepthBufferRegs` y orden `GX2InitDepthBufferRegs` antes de `GX2InitDepthBufferHiZEnable`. En `src/Scene/` y `src/Graphics/`: cero `GX2DrawDone()` sin guarda de foreground. | Activo |

---

## 📝 4. Registro de Ejecuciones

| Fecha | Fase | Pruebas Runtime | Harness Estático | Notas |
| :--- | :---: | :---: | :---: | :--- |
| 2026-11-22 | **V1** | 14 pruebas (`Core` + `Graphics` + `Net`) | 4/4 PASS | Orquestador `EngineSelfTestSuite` + semáforo en `GraphicsQAModal` + `GX2_INVARIANTS`. |
| 2026-11-22 | **V2** | **28 pruebas (8/8 librerías al 100%)** | **4/4 PASS** | `CoreSelfTest`, `PhysicsSelfTest`, `AudioSelfTest`, `InputSelfTest`, `AISelfTest`, `ModalStack`/`ResourceMonitor`, `EngineSelfTestReport<64u>`, check `hiZPtr` 256B y escaneo `src/Graphics/`. |
| 2026-11-22 | **G2** | **30 pruebas (8/8 librerías al 100%)** | **4/4 PASS** | Añadidas `PBR_SHADER_PIPELINE` (T9) y `RELIEF_TEX_MIPMAPS` (T10) para validar el shader nativo PBR Cook-Torrance + Spot Light + textura de relieve. |
| 2026-11-22 | **G3** | **32 pruebas (8/8 librerías al 100%)** | **4/4 PASS** | Añadidas `SHADOW_MAP_GX2` (T11, 1024×1024 D32F alineado ≥256B) y `SHADOW_CSM_MATH` (T12, splits y matrices ortográficas CSM). |
| 2026-11-23 | **G4** | **34 pruebas (8/8 librerías al 100%)** | **4/4 PASS** | Añadidas `CUBEMAP_SKY_GX2` (T13, 32×32×6 alineado ≥256B) y `PBR_IBL_SLOTS` (T14, reflexión completa de los 3 TMUs: `uTexture`, `uSkybox`, `uShadowMap`). |
