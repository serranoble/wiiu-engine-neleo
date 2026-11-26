# Estado Vivo del Proyecto — Neleo 3D Engine C++20

**Última actualización:** 26 de Noviembre de 2026 (Hito 153: Fase 2 Rendimiento Silicio Wii U [`OPT-453`: Shader 2D `ui2d_gx2` + Atlas de Fuente 5×7 `128×64` con 1 quad/glifo, Tiling 2D Hardware `GX2_TILE_MODE_DEFAULT` en `TextureLoaderGX2` y ruta rápida direccional en `ForwardRendererGX2`] + Teclado Virtual Modal PC/ROG Ally + `namespace Neleo`)
**Rama activa:** `main`
**Plataformas activas:** Nintendo Wii U (Espresso PPC 750CL / AMD Latte GX2 / Cafe OS) + **Windows x64 Nativo (`build-win/wiiu_demos.exe` — 0 SDL2, 0 DLLs externas, 2D + 3D + FBOs + Sombras + HDR OpenGL 3.3 Core)** — diseño multi-consola (Wii U / Windows-ROG Ally / Linux / Switch).

---

## Arquitectura Vigente (Hito 153 — `namespace Neleo` + 8 Librerías Estáticas + PAL Multiplataforma + Optimización 60 FPS Wii U Fases 1–2)

- **8 librerías estáticas (DAG acíclico en `CMakeLists.txt`, `namespace Neleo`):**
  1. `engine_core` (+ `PlatformClock`, `WindowWiiU` / `WindowWin32` WGL OpenGL 3.3 Core, `Win32InputBridge`, `ViewFrustum`, `StacklessBVH`, `SaveToFileAsync`), 2. `engine_physics`, 3. `engine_audio` (+ `AudioDeviceWin32` `waveOut` 48 kHz), 4. `engine_input` (+ `InputWin32` XInput + táctil, `RumbleWin32`), 5. `engine_graphics` (+ GX2 con `ui2d_gx2` + Atlas 5×7, `pbr_lite_gx2` [9.6 KB] + `pbr_mesh_gx2`, Tiling 2D hardware y caché de pipeline + OpenGL 3.3 Core completo: `GLFunctions`, `Render2DGL`, `ForwardRendererGL` [modos `0..9`, PCF 3×3/5×5, Reflejo planar, Terreno 4 biomas, NPR], `TextureLoaderGL`, `RenderTargetGL`, `ShadowMapRendererGL`, `PostProcessHDRGL`, `RHIFullscreenShaderGL`), 6. `engine_ai`, 7. `engine_framework` (+ `GameScene3D` con Z-Buffer único en MEM1/eDRAM en Wii U, `SoftwareKeyboard` modal multiplataforma), 8. `engine_net` (Wii U `nsysnet` + Windows `ws2_32`).
- **Cero SDL2 en todo el motor (`ZERO_SDL_SOURCE_REFS: PASS (0)` + `ELF_SDL_SYMBOLS: PASS (0)`):** Ningún `#include <SDL2/` ni símbolo `SDL_*` en `include/` o `src/`.
- **Cobertura de Calidad Gráfica y Validación:** **8/8 Suites QA (G1–G8)** + `SceneModelHouse` + `SceneModelBird` + **45/45 pruebas** en `EngineSelfTestSuite` + **8/8 gates** en `scripts/agent_harness.py check` (`OPT-444`..`OPT-453` en `ANEXO_OPTIMIZACIONES_PENDIENTES.md`).
- **Herramienta de Diagnóstico:** `tools/net_test_server.py` (servidor local dual TCP 7777 + UDP 7778).
- **Umbrella header público:** `include/NeleoEngine.hpp` (con redirección en `include/WiiUEngine.hpp`).

---

## Invariantes Críticos del Motor

1. **Cero SDL2 en Wii U:** `0` símbolos `SDL_` en `build/wiiu_demos.elf`.
2. **Guardas de primer plano GX2:** Toda llamada a `GX2DrawDone()` debe protegerse con `Window::HasForeground() && !Window::IsExitingProcUI()`.
3. **Zero Hot Allocations:** Cero alocaciones en el heap a 60 FPS en bucles críticos.
4. **Terminaciones de línea:** Estrictamente LF (`0 CRLF`).
5. **Prohibición FTP:** Nunca subir binarios a la consola sin orden explícita.
