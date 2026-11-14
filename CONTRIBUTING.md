# Guía de Contribución (`CONTRIBUTING.md`)

¡Gracias por tu interés en contribuir al **Wii U GX2 3D Game Engine & Framework (C++20)**! Este repositorio sigue los principios de arquitectura descritos en *Game Engine Architecture* (Jason Gregory, 3ª Ed.) adaptados al hardware real de Nintendo Wii U (IBM PowerPC 750CL Espresso + GPU AMD Latte GX2).

---

## 1. Arquitectura Modular en 7 Librerías Estáticas

El motor en [`CMakeLists.txt`](CMakeLists.txt) está estrictamente estratificado en 7 librerías estáticas desacopladas de las escenas demo (`DEMO_SOURCES`):

1. **`engine_core`**: Memoria determinista (`MemoryManager`, `FrameScratchArena`), `JobSystem` (Core 2), `AssetManager`, `StorageManager`, `EngineConfig`, `GameClock`, `GameplayEventBus`, `SpatialBVH` y `ECSWorld` + componentes/sistemas base. (**Regla estricta: `0` cabeceras `<gx2/*.h>` dentro de `include/Core/`**).
2. **`engine_physics`**: Sistemas analíticos de colisión (`CollisionSystem`), cuerpos rígidos y restricciones XPBD (`RigidBodyPhysicsSystem`) y *Kinematic Character Controller* (`KinematicCharacterSystem`).
3. **`engine_ai`**: Navegación espacial (`NavGraph`), *pathfinding* sin asignaciones en heap (`AStarPathfinder`), dirección cinemática (`SteeringBehaviors`) y HFSM (`AISteeringSystem`).
4. **`engine_audio`**: `AudioManager` y `AudioEngine3D` (efectos Doppler, Sidechain Ducking, buses de mezcla y reverberación Schroeder).
5. **`engine_input`**: `InputManager` (GamePad DRC VPAD, Pro Controller WPAD, Wii Remote, Touch) y `RumbleManager`.
6. **`engine_graphics`**: `ForwardRendererGX2`, `ShadowSystem`, `StaticMesh`, `TextureLoaderGX2`, `Render2DGX2`, `DebugDraw3D`, `UIToolkit` y RHI (`RHIPipelineCache`, `DisplayCompositor`, `RHIRenderPass`, `RHIFullscreenShader`).
7. **`engine_framework`**: `Engine`, `SceneManager`, `GameScene3D`, `HUDOverlay`, `ResourceMonitor`, `SceneLoading`, `SceneAssetError`.

### Regla Arquitectónica de Oro (Cero Dependencias Hacia Arriba)
Ningún archivo dentro de `engine_*` puede hacer `#include` de encabezados de `DEMO_SOURCES` (`Scene/SceneMenu*.hpp`, `Scene/SceneOptions.hpp`, o escenas showcase específicas).
La navegación entre menús externos se configura desde la aplicación ejecutable mediante fábricas en `SceneManager`:
- `SetHomeMenuFactory(...)`
- `SetModelsMenuFactory(...)`
- `SetOptionsSceneFactory(...)`

---

## 2. Reglas Estrictas de Silicio Wii U (Espresso & Latte)

Toda contribución al código del motor debe respetar estas invariantes de hardware:

- **Big-Endian Nativo (PowerPC 750CL):** Todos los formatos cocinados (`.wmesh`, `.wtex`, `.wcube`, `.wskel`, `.wanim`) utilizan orden de bytes Big-Endian en consola. Usa los *cookers* offline en [`tools/cooker/`](tools/cooker/) en lugar de parsear `.glb`/`.png` en caliente.
- **Cero Alocaciones Dinámicas a 60 FPS:** Está prohibido usar `new`, `malloc`, `std::vector` sin reserva o `std::string` temporales dentro de `Update(dt)` o `Render(...)`. Utiliza `FrameScratchArena`, `StackString`, `ArenaVector`, `ResourcePool` y arreglos estáticos de tamaño fijo.
- **Alineación DMA y Caché L1D/L2:** Todo búfer subido a la GPU Latte GX2 (`GX2Texture`, uniformes, VBO/IBO, shaders) debe alinearse a 256 bytes (o `surface.alignment`) y vaciar la caché de CPU mediante `GX2Invalidate(GX2_INVALIDATE_MODE_CPU_..., ...)`. Usa `WiiU::Graphics::TextureLoaderGX2` o los helpers de `GameScene3D` en lugar de escribir código GX2 manual en tus escenas.
- **Drenado Seguro de GPU y RAII:** Antes de destruir texturas o superficies en VRAM, sincroniza con `GX2DrawDone()` protegido por `ProcUIIsRunning() && ProcUIInForeground()` (ya automatizado en `GameScene3D::DestroyGX2Pipeline()`).

---

## 3. Cómo Empezar tu Propio Proyecto con el Motor

Puedes enlazar el motor directamente desde un proyecto CMake externo con `-DWIIU_ENGINE_BUILD_DEMOS=OFF` y usar el *Umbrella Header* [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp):

```cmake
add_subdirectory(wiiu_engine)
target_link_libraries(mi_juego PRIVATE engine_framework)
```

En tu código fuente, incluye `#include "WiiUEngine.hpp"`, hereda tu escena de `WiiU::GameScene3D` y pásala a `engine.Init(std::make_unique<MiJuegoScene>())`.

---

## 4. Pipeline de Activos Offline (`tools/cooker/`)

Nunca cargues formatos pesados de authoring directamente en la Wii U. Convierte tus modelos y texturas con [`tools/cooker/asset_cooker.py`](tools/cooker/asset_cooker.py) hacia archivos binarios alineados (`.wmesh`, `.wtex`, `.wcube`, `.wskel`, `.wanim`) antes de empaquetar el `.wuhb`.

---

## 5. Flujo de Trabajo para Pull Requests

1. **Compila sin errores** bajo devkitPPC + WUT:
   ```bash
   cmake -B build && cmake --build build -j4
   ```
2. **Verifica en emulador (Cemu >= 2.0)** cargando `build/wiiu_demos.wuhb` y comprobando estabilidad visual y de memoria (sin cuelgues ni fugas en `MemoryManager`).
3. **Finales de línea Unix (`LF` / `\n`):** Mantén todo archivo `.md`, `.hpp` y `.cpp` con finales de línea LF puros (sin `CRLF`).
