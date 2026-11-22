# Estado Vivo del Proyecto — Wii U 3D Engine C++20

**Última actualización:** 21 de Noviembre de 2026 (Hito 144 + Optimización de Harness/Contexto)
**Rama activa:** `refactor/engine-stability-and-scene-archive`
**Plataforma base:** Nintendo Wii U (Espresso PPC 750CL Tri-Core @ 1.24 GHz, AMD Latte GX2 @ 550 MHz, Cafe OS) — diseño multi-consola (Wii U / Switch / PC).

---

## Arquitectura Vigente (Hito 144 — 100% Nativo sin SDL2 en Wii U)

- **7 librerías estáticas (DAG acíclico en `CMakeLists.txt`):**
  1. `engine_core`: memoria determinista (`MemoryManager`, `LinearFrameArena`), ECS C++20, `JobSystem` (cola MPMC Vyukov en Core 2), `VirtualFileSystem`, `CVarRegistry`, `StorageManager` + `CryptoHMAC`, `ApplicationLifecycle`, `EngineConfig` (DIP, 16 `ObserverSlot` sin heap). **0 inclusiones `<gx2/` ni `<SDL2/` en `include/Core/`.**
  2. `engine_physics`: `SpatialHashGrid` (bitset 32B en pila), `CollisionSystem`, `RigidBodyPhysicsSystem`, `KinematicCharacterSystem` + `CameraRelativeMovementSystem`.
  3. `engine_audio`: `AudioManager` (`sndcore2`/AX estéreo 48 kHz ping-pong en MEM2 de 16,384 B, `DCStoreRange`, hilo de mezcla dedicado `prio - 1`, arranque L/R sincronizado, `std::atomic` lock-free) y `AudioEngine3D` (128 voces virtuales / 32 físicas).
  4. `engine_input`: `InputManager` (`VPAD`/`KPAD` nativo en Wii U; `SDL2` aislado en `#if !defined(__WIIU__)`), `InputContextStack`, `RumbleManager` (PWM 32 bits paced 160 ms).
  5. `engine_graphics`: `ForwardRendererGX2` (3-frame ring buffer, cubemap/textura neutros, Direct GFD sin WHB), `ShadowMapRendererGX2` (PCF 2x2, Hi-Z), `Render2DGX2` (triple-buffer `BUFFER_SLOTS = 3`), `Render2D`/`UIComponents` sin `SDL_Renderer*`, `MaterialSystem`, `RenderQueue` (Early-Z -> Opacos -> Skybox -> Transparentes), `TextureLoaderGX2`.
  6. `engine_ai`: `AStarPathfinder`, `BehaviorTree`, `AIPerceptionSystem`, `SensoryBlackboard`.
  7. `engine_framework`: `Engine` (reloj `OSGetSystemTime()`, `ProcUIInitEx`), `Window` (swapchain GX2 nativo, heaps MEM1/FG, TV/DRC activos en Menú HOME), `SceneManager`, `GameScene3D`, `ModalStack`, `ResourceMonitor`.
- **Escenas activas en `wiiu_demos`:** `SceneLoading` -> `SceneMenu` -> `SceneOptions` (escenas previas archivadas en `archive/scenes/`).
- **Umbrella header público:** `include/WiiUEngine.hpp`.

---

## Invariantes Críticos del Motor

1. **Cero SDL2 en Wii U:** `0` símbolos `SDL_` en `build/wiiu_demos.elf`. Todo código SDL2 debe vivir bajo `#if !defined(__WIIU__) && !defined(__wiiu__)`.
2. **Guardas de primer plano GX2:** Toda llamada a `GX2DrawDone()` y teardown GPU debe protegerse con `Window::HasForeground() && !Window::IsExitingProcUI()`.
3. **Zero Hot Allocations:** Cero `new`/`malloc`/`std::string` en bucles a 60 FPS (`Update`/`Render`). Usar `LinearFrameArena` o `StackString<N>`.
4. **Terminaciones de línea:** Estrictamente LF (`0 CRLF`).
5. **Prohibición FTP:** Nunca subir binarios a la consola sin orden explícita del usuario en el turno actual.
6. **Auditoría obligatoria:** Invocar `graphics_code_reviewer` y `engine_clean_code_reviewer` antes de cerrar cualquier implementación relevante.

---

## Comandos Rápidos (Harness y Build)

```powershell
# Resumen compacto de contexto y git
python scripts/agent_harness.py context

# Verificaciones estáticas (CRLF, fugas GX2/SDL2 en Core, símbolos SDL en ELF)
python scripts/agent_harness.py check

# Compilación filtrada (sin spam de empaquetado de assets)
python scripts/agent_harness.py build
python scripts/agent_harness.py build --lib

# Consultar último OPT o uno específico sin leer ANEXO entero (733 KB)
python scripts/agent_harness.py opt tail
python scripts/agent_harness.py opt OPT-442
```

---

## Bitácoras Históricas (Solo Consulta Puntual / Append-Only)
- `HANDOFF_ANTIGRAVITY.md` (529 KB): historial completo Hitos 1–144. **No leer completo**; consultar solo el final o usar `context_scribe` para anexar nuevos hitos.
- `ANEXO_OPTIMIZACIONES_PENDIENTES.md` (733 KB): catálogo `OPT-01` a `OPT-442`. **No leer completo**; usar `scripts/agent_harness.py opt <ID|tail>`.
