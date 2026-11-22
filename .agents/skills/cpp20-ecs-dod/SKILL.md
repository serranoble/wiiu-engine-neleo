---
name: cpp20-ecs-dod
description: C++20 Clean Code, Data-Oriented Design (DOD/ECS), 7-library DAG boundaries, and Zero-Alloc 60 FPS rules for the Wii U multi-console engine. Load before designing or refactoring engine modules.
---

# C++20 Engine Architecture & DOD/ECS Conventions

## 1. Acyclic 7-Library DAG (`CMakeLists.txt`)
1. `engine_core` — **0** includes of `<gx2/`, `<gx2r/`, `<SDL2/`, `Audio/`, `Input/`, or `Scene/`. Uses forward declarations (`struct GX2Texture;`) and DIP callbacks (`ObserverSlot[16]` in `EngineConfig`).
2. `engine_physics` -> `engine_core`
3. `engine_audio` -> `engine_core`
4. `engine_input` -> `engine_core`
5. `engine_graphics` -> `engine_core` (keep UI/rendering decoupled from scene flow)
6. `engine_ai` -> `engine_core`, `engine_physics`
7. `engine_framework` -> orchestrates all subsystems (`Engine`, `Window`, `SceneManager`, `GameScene3D`, `ModalStack`, `ResourceMonitor`).

## 2. Multi-Console Reusability Gate
- Build reusable engine components/systems, never single-scene demo hacks.
- Scenes (`GameScene3D` / `IScene`) only orchestrate data and input mappings — no raw `VPAD_*`/`WPAD_*` masks (use `VirtualButton` and `InputManager::GetGamepad(p)` across `0..MAX_PLAYERS-1`).
- Keep public headers clean: no `using namespace`, prefer forward declarations, mark non-mutating queries `[[nodiscard]] ... const noexcept`.

## 3. Zero Hot Allocations (60 FPS Budget)
- **Forbidden inside `Update()` / `Render()`:** `new`, `malloc`, `std::string` construction/concatenation beyond stack buffers, `std::vector::push_back` beyond pre-allocated capacity.
- **Use instead:** `LinearFrameArena` (reset in `O(1)` per frame), `StackString<N>`, `std::string_view`, fixed `char buf[N]` + `std::snprintf`, `std::array` pools.

## 4. Memory Safety & Rule of 5
- Any class owning hardware handles, threads, atomics, or arenas must declare `= delete` for copy/move (or explicit `noexcept` move).
- Deallocation helpers (`MemoryManager::Free`, `FreeAligned`) take `T*&` and nullify pointers immediately.
- Never destroy static singletons that call hardware/OS APIs after `Engine::Shutdown()`; provide explicit `Shutdown()` methods and `= default` destructors (avoid Static Destruction Order Fiasco).
- Audio/Worker thread safety: scalar flags/volumes use `std::atomic<T>`; swap pre-allocated buffers in `O(1)` inside critical sections so heap frees happen outside locks.

## 5. Line Endings & Verification
- Strictly **LF (`0 CRLF`)** on all source, script, config, and markdown files.
- Before requesting reviewer audits, run `python scripts/agent_harness.py check` and `python scripts/agent_harness.py build`.
