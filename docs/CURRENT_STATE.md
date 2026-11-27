# Estado Vivo del Proyecto — Neleo 3D Engine C++20

**Última actualización:** 26 de Noviembre de 2026 (Hito 161 — Oleada 2: `OPT-462` Depth Pre-Pass integrado en `RenderQueuePBR` con Dummy PS seguro + `OPT-463` Cadena completa de Mipmaps en `TextureLoaderGX2` — 51/51 pruebas)
**Rama activa:** `main`
**Plataformas activas:** Nintendo Wii U (Espresso PPC 750CL / AMD Latte GX2 / Cafe OS) + **Windows x64 Nativo (`build-win/wiiu_demos.exe` — 0 SDL2, 0 DLLs externas, 2D + 3D + FBOs + Sombras + HDR OpenGL 3.3 Core)** — diseño multi-consola (Wii U / Windows-ROG Ally / Linux / Switch).

---

## Arquitectura Vigente (Hito 161 — `namespace Neleo` + 8 Librerías Estáticas + PAL Multiplataforma + `RenderQueuePBR` + Z-Prepass + Mipmaps GX2)

- **8 librerías estáticas (DAG acíclico en `CMakeLists.txt`, `namespace Neleo`):**
  1. `engine_core` (+ `PlatformClock`, `EngineProfiler`, `WindowWiiU` / `WindowWin32` WGL OpenGL 3.3 Core, `Win32InputBridge`, `ViewFrustum`, `StacklessBVH`, `SaveToFileAsync`), 2. `engine_physics`, 3. `engine_audio` (+ `AudioDeviceWin32` `waveOut` 48 kHz), 4. `engine_input` (+ `InputWin32` XInput + táctil, `RumbleWin32`), 5. `engine_graphics` (+ GX2 con `ui2d_gx2` + Atlas 5×7, **5 permutaciones PBR cocinadas** [`pbr_dir` 5.6 KB/12 GPR, `pbr_dir_bump` 6.6 KB/12 GPR, `pbr_multi` 9.5 KB/19 GPR, `pbr_multi_bump` 10.5 KB/16 GPR, `pbr_shadow` 13.0 KB/31 GPR], `water_glass_gx2` [8.0 KB/12 GPR], `hdr_fog_gx2` [4.0 KB/7 GPR], `RenderQueuePBR` con `SortKey` 64-bit + Frustum Culling + Z-Prepass opt-in, `ShaderVariantKey` + Light-Culling por *Bounding Sphere* compartido Wii U/PC, Tiling 2D hardware Full-Res `2048×2048` con cadena completa de Mipmaps GX2 + OpenGL 3.3 Core con paridad ACES Filmic + IBL físico real), 6. `engine_ai`, 7. `engine_framework` (+ `GameScene3D` con Z-Buffer único en MEM1/eDRAM en Wii U, `SoftwareKeyboard` modal multiplataforma), 8. `engine_net` (Wii U `nsysnet` + Windows `ws2_32`).
- **Cero SDL2 en todo el motor (`ZERO_SDL_SOURCE_REFS: PASS (0)` + `ELF_SDL_SYMBOLS: PASS (0)`):** Ningún `#include <SDL2/` ni símbolo `SDL_*` en `include/` o `src/`.
- **Cobertura de Calidad Gráfica y Validación:** **8/8 Suites QA (G1–G8)** + `SceneModelHouse` + `SceneModelBird` + **51/51 pruebas** en `EngineSelfTestSuite` + **9/9 gates** en `scripts/agent_harness.py check` (incluyendo `SHADER_BUDGET_CHECK` GFD/GPR sobre 26 `.gsh` y guarda VS↔PS `SPI_PS_INPUT_CNTL`) (`OPT-444`..`OPT-463` en `ANEXO_OPTIMIZACIONES_PENDIENTES.md`).
- **Herramienta de Diagnóstico:** `tools/net_test_server.py` (servidor local dual TCP 7777 + UDP 7778).
- **Umbrella header público:** `include/NeleoEngine.hpp` (con redirección en `include/WiiUEngine.hpp`).

---

## Invariantes Críticos del Motor

1. **Cero SDL2 en Wii U:** `0` símbolos `SDL_` en `build/wiiu_demos.elf`.
2. **Guardas de primer plano GX2:** Toda llamada a `GX2DrawDone()` debe protegerse con `Window::HasForeground() && !Window::IsExitingProcUI()`.
3. **Zero Hot Allocations:** Cero alocaciones en el heap a 60 FPS en bucles críticos.
4. **Terminaciones de línea:** Estrictamente LF (`0 CRLF`).
5. **Prohibición FTP:** Nunca subir binarios a la consola sin orden explícita.
