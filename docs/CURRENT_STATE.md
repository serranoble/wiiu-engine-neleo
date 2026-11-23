# Estado Vivo del Proyecto — Wii U 3D Engine C++20

**Última actualización:** 23 de Noviembre de 2026 (Hito 146: Plan Maestro de Calidad Gráfica 8/8 Suites G1–G8 validadas en Wii U real + 43/43 pruebas)
**Rama activa:** `refactor/engine-stability-and-scene-archive`
**Plataforma base:** Nintendo Wii U (Espresso PPC 750CL Tri-Core @ 1.24 GHz, AMD Latte GX2 @ 550 MHz, Cafe OS) — diseño multi-consola (Wii U / Switch / PC).

---

## Arquitectura Vigente (Hito 146 — 8 Librerías Estáticas + Plan Gráfico 8/8 Completo)

- **8 librerías estáticas (DAG acíclico en `CMakeLists.txt`):**
  1. `engine_core` (+ `ViewFrustum`, `StacklessBVH`, `EngineConfig::SaveToFileAsync()` sobre `StorageManager::SaveFileAsync`), 2. `engine_physics`, 3. `engine_audio`, 4. `engine_input` (+ `SoftwareKeyboard` táctil calibrado `VPADGetTPCalibratedPoint`), 5. `engine_graphics` (+ `RenderTargetGX2`, `PostProcessHDRGX2` Kawase 5-Tap, `MeshLODGroup`, `ParticleSystem3D`, Instancing Ring Buffer lock-free), 6. `engine_ai`, 7. `engine_framework`, 8. `engine_net` (validado sobre Wi-Fi Wii U real: RTT 34.6 ms / SRTT 46 ms / Jitter 19 ms).
- **Cobertura de Calidad Gráfica:** **8/8 Suites QA (G1–G8)** cerradas y validadas en **Cemu 2.6** y **Wii U real** (G1: 87 FPS, G2: 32 FPS, G3: 56 FPS, G4: 62 FPS, G5: 50 FPS, G6: 55 FPS, G7: 50 FPS, G8: 25–50 FPS con VSync OFF) + **43/43 pruebas** en `EngineSelfTestSuite`.
- **Herramienta de Diagnóstico:** `tools/net_test_server.py` (servidor local dual TCP 7777 + UDP 7778).
- **Umbrella header público:** `include/WiiUEngine.hpp`.

---

## Invariantes Críticos del Motor

1. **Cero SDL2 en Wii U:** `0` símbolos `SDL_` en `build/wiiu_demos.elf`.
2. **Guardas de primer plano GX2:** Toda llamada a `GX2DrawDone()` debe protegerse con `Window::HasForeground() && !Window::IsExitingProcUI()`.
3. **Zero Hot Allocations:** Cero alocaciones en el heap a 60 FPS en bucles críticos.
4. **Terminaciones de línea:** Estrictamente LF (`0 CRLF`).
5. **Prohibición FTP:** Nunca subir binarios a la consola sin orden explícita.
