# 🎮 Multi-Platform 3D Graphics & Game Engine (C++20 — Wii U & Windows OpenGL 3.3 Core)

Motor de videojuegos modular de alto rendimiento y suite de demos gráficas en **C++20**, con soporte nativo para **Nintendo Wii U** (entorno **Aroma**, CPU tri-núcleo IBM Espresso PowerPC 750CL y GPU AMD Latte GX2 con eDRAM de 560 GB/s) y **Windows x64 / ASUS ROG Ally Z1** (**Win32 WGL OpenGL 3.3 Core**, **0 SDL2** y **0 DLLs de terceros**), manteniendo arquitectura desacoplada orientada a consola/PC (Wii U / Windows-ROG Ally / Linux).

---

> [!TIP]
> **📘 MOTOR MODULAR OPEN SOURCE MULTIPLATAFORMA (C++20 / GX2 + OPENGL 3.3 CORE):**
> - **Tutorial Práctico Multiplataforma:** [`docs/TUTORIAL_PRIMER_JUEGO_MULTIPLATAFORMA.md`](docs/TUTORIAL_PRIMER_JUEGO_MULTIPLATAFORMA.md) (crear tu escena `GameScene3D`, ECS, física, PBR/sombras, cooker y compilar en Wii U + Windows).
> - **Sitio Web de Documentación (GitHub Pages):** [`docs/index.html`](docs/index.html) (activar en *Settings → Pages → Branch → `/docs`*).
> - **Umbrella Header Público:** [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp) expone los **8 módulos estáticos** desacoplados del motor (`engine_core`, `engine_physics`, `engine_audio`, `engine_input`, `engine_ai`, `engine_graphics`, `engine_framework`, `engine_net`).
> - **Plan Maestro de Calidad Gráfica (CGP + RTR4):** **8/8 Suites (G1–G8)** validadas en **Nintendo Wii U real**, Cemu 2.6 y **Windows x64 OpenGL 3.3 Core** + **44/44 pruebas** en `EngineSelfTestSuite` — ver [`docs/GRAPHICS_QUALITY_PLAN_AUDIT.md`](docs/GRAPHICS_QUALITY_PLAN_AUDIT.md).
> - **Guía de Framework y Contribución:** Lee [`docs/FRAMEWORK_ENGINE_GUIDE.md`](docs/FRAMEWORK_ENGINE_GUIDE.md), [`CONTRIBUTING.md`](CONTRIBUTING.md) y la licencia [`LICENSE`](LICENSE) (Apache 2.0).
> - **Auditoría Jason Gregory (*Game Engine Architecture*, 3ª Ed.):** **98.0% Core Engine / 93.0% Libro Completo** — ver [`docs/JASON_GREGORY_COMPLETION_AUDIT.md`](docs/JASON_GREGORY_COMPLETION_AUDIT.md).

---

## 📋 Tabla de Contenidos

1. [Arquitectura Modular del Motor y Características](#-arquitectura-modular-del-motor-y-características)
2. [Estructura del Repositorio](#-estructura-del-repositorio)
3. [Requisitos del Entorno de Desarrollo](#-requisitos-del-entorno-de-desarrollo)
4. [Compilación Dual: Nintendo Wii U (.wuhb) y Windows x64 (.exe)](#-compilación-dual-nintendo-wii-u-wuhb-y-windows-x64-exe)
5. [Pipeline Universal de Modelos 3D (Cooker)](#-pipeline-universal-de-modelos-3d-cooker)
6. [Pipeline de Shaders CafeGLSL](#-pipeline-de-shaders-cafeglsl)
7. [Despliegue en Consola Física vía FTP](#-despliegue-en-consola-física-vía-ftp)
8. [Pruebas y Depuración en Emulador (Cemu)](#-pruebas-y-depuración-en-emulador-cemu)
9. [Cómo Crear una Nueva Escena y Tutorial](#-cómo-crear-una-nueva-escena-y-arrancar-un-proyecto-externo)
10. [Reglas y Directivas de Desarrollo](#-reglas-y-directivas-de-desarrollo)

---

## 🏛️ Arquitectura Modular del Motor y Características

El motor está desacoplado en **8 librerías estáticas CMake** independientes (DAG acíclico; ninguna depende de escenas demo ni menús; todo consumidor externo enlaza `engine_framework` e incluye [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp)):

| Librería CMake | Namespace / Subsistema | Responsabilidad Principal |
| :--- | :--- | :--- |
| **`engine_core`** | `WiiU` (`Core/`, `ECS/`, `Spatial/`, `Time/`, `Events/`, `Storage/`, `Config/`, `Platform/`) | Loop base, PAL (`WindowWiiU` / `WindowWin32` WGL, `PlatformClock`), `ECSWorld` DOD, `MemoryManager`, `JobSystem` SMP, `ViewFrustum`, `StacklessBVH`, `GameClock`, `GameplayEventBus`, `StorageManager` + `SaveFileAsync()`. Cero cabeceras `<gx2/*.h>`, `<windows.h>` o `<GL/`. |
| **`engine_physics`** | `WiiU` (`CollisionSystem`, `KinematicCharacterSystem`, `RigidBodyPhysicsSystem`) | Barrido cinemático KCC, detección continua, cuerpos rígidos SO(3), impulsos secuenciales + fricción de Coulomb y restricciones XPBD. |
| **`engine_audio`** | `WiiU` (`AudioManager`, `AudioEngine3D`, `AudioSpatialSystem`, `AudioDeviceWin32`) | Mixer 32 voces (`sndcore2` AX en Wii U / `waveOut` 48 kHz nativo en Windows), paneo azimutal, LPF oclusión, efecto Doppler físico, *sidechain ducking* y reverberación Schroeder. |
| **`engine_input`** | `WiiU` (`InputManager`, `RumbleManager`, `SoftwareKeyboard`, `InputWin32`, `RumbleWin32`) | HAL unificado: VPAD/WPAD/Pro Controller en Wii U + XInput, teclado, ratón y táctil en Windows/ROG Ally; curvas radiales, contexto jerárquico, háptica ADSR y teclado táctil/nativo. |
| **`engine_ai`** | `WiiU::AI` (`NavGraph`, `AStarPathfinder`, `SteeringBehaviors`, `AISteeringSystem`) | Navegación A* en memoria de pila, comportamientos de Craig Reynolds y máquina de estados cognitiva FSM / Behavior Trees. |
| **`engine_graphics`** | `WiiU` / `WiiU::Graphics` (`RHI/`, `ForwardRenderer*`, `RenderTarget*`, `PostProcessHDR*`, `ShadowMapRenderer*`, `TextureLoader*`, `Render2D*`, `DebugDraw3D`, `UIToolkit`) | Renderizado dual nativo por tiempo de enlace (**AMD Latte GX2** + **OpenGL 3.3 Core**): PBR Cook-Torrance, Shadow Maps 2-Pass PCF 3×3/5×5, Skybox Atlas 2D 3×2 + Agua Planar RTT, HDR `RGBA16F` + Bloom Kawase, Instancing HW, Terreno 4 biomas, Toon NPR y Ray Tracing BVH. |
| **`engine_net`** | `WiiU::Net` (`NetSocket`, `NetBitStream`, `NetSession`, `NetReplication`) | Sockets no bloqueantes `nsysnet` (Wii U) / `ws2_32` (Windows), serialización endian-safe, framing TCP + datagramas UDP `WUN1`, replicación de snapshots 50B con LERP/SLERP y telemetría RTT/SRTT/Jitter. |
| **`engine_framework`** | `WiiU` (`Engine`, `SceneManager`, `GameScene3D`, `HUDOverlay`, `ResourceMonitor`, `EngineSelfTestSuite`) | Orquestación de arranque con escena inyectable `Engine::Init(std::unique_ptr<IScene>)`, navegación desacoplada por fábricas, suite de **44/44 pruebas automatizadas** y sincronización de VSync/ProcUI. |

### Suites de Validación Gráfica (G1–G8) — Benchmarks Reales en Wii U

| Suite | Enfoque Técnico | FPS Cemu 2.6 | FPS Wii U Real (VSync OFF) |
| :---: | :--- | :---: | :---: |
| **G1** | Pipeline, Mipmaps (9 niveles), Z-Buffer 32F+HiZ | 60.1 FPS | **87 FPS** |
| **G2** | PBR Cook-Torrance GGX + Normal Mapping TBN | 60.2 FPS | **32 FPS** *(fillrate PBR)* |
| **G3** | Shadow Map 2-Pass (`1024×1024 D32F`) + PCF 3×3/5×5 | 60.0 FPS | **56 FPS** |
| **G4** | Skybox Atlas 2D `3×2` + Agua Física + Reflexión Planar RTT (`640×360`) | 60.0 FPS | **62 FPS** |
| **G5** | HDR `1280×720 RGBA16F` + Bloom Kawase 5-Tap (`320×192`) + Alpha Blend | 59.7 FPS | **50 FPS** *(40 cerca)* |
| **G6** | Instancing HW (1,000 inst, Ring Buffer lock-free) + Frustum Culling + LOD | 60.0 FPS | **55 FPS** |
| **G7** | Terreno Heightmap `48×48` + Ruido 3D + Partículas (128) + Toon NPR | 58.1–60 FPS | **50 FPS** |
| **G8** | Ray Tracing GPU (BVH Stackless 12 esferas) + Render Scale `360p..720p` | 57.0–60 FPS | **25–50 FPS** *(30 FPS ≤540p VSync)* |

> **Networking en Wii U real (Wi-Fi):** `engine_net` validado contra `tools/net_test_server.py` con **RTT 34.6 ms | SRTT 46 ms | Jitter 19 ms**.

### Errores Conocidos y Limitaciones de Silicio

- **`RenderSkinnedMeshInstanced` en Wii U real:** Pendiente de auditoría en silicio real (provocaba congelamiento en el primer frame en Suite G6; reemplazado temporalmente por bloques PBR en G6 mientras se aísla el microcódigo de skinning multi-instancia).
- **Instrucción R600 `CUBE` (`samplerCube`) en CafeGLSL / Cemu 2.6:** Produce artefactos radiales en Vulkan y fallos en cubemaps lineales. **Solución canónica adoptada:** Atlas 2D `3×2` (`SampleCubeAtlas()`) en `pbr_mesh_gx2.frag` y reflexión hemisférica analítica en `instanced_mesh_gx2.frag`.
- **Escalón VSync 60→30 Hz y GPRs en `pbr_mesh_gx2.frag`:** Con VSync ON, cualquier frame que supere `16.67 ms` cae a 30 FPS exactos; el uber-shader PBR (36 GPRs) limita G2 a ~32 FPS en fillrate puro en AMD Latte R700. Especializar variantes livianas del shader es una mejora planificada.
- **Ray Tracing GPU (G8):** La GPU AMD Latte (~0.35 TFLOPS) carece de RT Cores; a 720p cae a ~20–25 FPS, mientras que con Render Scale `≤ 540p` sostiene 30 FPS.

* **Renderizado Multiplataforma Nativo (0 SDL2 / 0 DLLs Externas):**
  - **Wii U (`__WIIU__` — AMD Latte GX2 Nativo):** Control directo del procesador de comandos GX2, pases en eDRAM (MEM1 32 MB), Z-buffer jerárquico HiZ y shaders CafeGLSL directos (`.gsh`).
  - **Windows x64 / ASUS ROG Ally (`GL` — OpenGL 3.3 Core + Win32 PAL):** Contexto WGL OpenGL 3.3 Core con carga dinámica propia (`GLFunctions`), `ForwardRendererGL` (modos `0..9`), `RenderTargetGL`, `ShadowMapRendererGL` PCF, `PostProcessHDRGL` (HDR + Bloom Kawase), `RHIFullscreenShaderGL` (BVH) y `Render2DGL` en un binario único sin DLLs de terceros.
* **Arquitectura ECS (Entity Component System):**
  - Entidades y componentes orientados a datos (`TransformComponent`, `MeshComponent`, `SkeletalMeshComponent`, `AnimatorComponent`, `IKRigComponent`, `CameraComponent`, `SpringArmComponent`, `StudioTurntableComponent`, `MaterialComponent`, `BoundingVolumeComponent`, `RigidBodyComponent`, `AudioEmitterComponent`).
  - Sistemas desacoplados ejecutados por frame (`AnimationSystem`, `IKSystem`, `TransformSystem`, `CameraSystem`, `SpringArmSystem`, `OrbitCameraSystem`, `StudioTurntableSystem`, `CullingSystem`, `MeshRenderSystem`).
* **Concurrencia SMP en CPU Espresso (3 Cores):**
  - **Core 0:** Bucle de eventos, entrada VPAD/WPAD, lógica de escenas y orquestación.
  - **Core 1:** Procesamiento de audio y sumisión de comandos GPU GX2.
  - **Core 2:** Carga asíncrona de archivos binarios y culling paralelo mediante `JobSystem`.
* **Animación Esquelética en GPU e IK:**
  - *Linear Blend Skinning* (LBS) de 4 influencias por vértice en Vertex Shader CafeGLSL (`KC2`), *cross-fading* SQT Zero-Alloc, máscaras de capas de 32 bits y *solvers* IK analítico / FABRIK.
* **Cero Asignaciones en Caliente (Zero Hot Allocations):**
  - Todas las estructuras por frame (`StackString<N>`, `GameplayEventBus`, arreglos de uniformes `alignas(256)`) residen en la pila o arenas de memoria pre-reservadas (`0` `malloc`/`new` a 60 FPS).

---

## 📂 Estructura del Repositorio

| Directorio / Archivo | Responsabilidad Arquitectónica |
| :--- | :--- |
| `include/WiiUEngine.hpp` | **Umbrella Header Público**: único punto de entrada recomendado para proyectos y contribuidores. |
| `include/Core/`, `src/Core/` | Bucle principal (`Engine`), `ECSWorld`, componentes, sistemas, reloj (`GameClock`), `GameplayEventBus`, `StorageManager` y `EngineConfig`. |
| `include/Core/AI/`, `src/Core/AI/` | Inteligencia Artificial: grafos espaciales (`NavGraph`), A* en stack, Craig Reynolds y FSM. |
| `include/Platform/`, `src/Platform/` | Capa PAL por plataforma (`WiiU/WindowWiiU`, `WiiU/PlatformClockWiiU`, `Windows/WindowWin32`, `InputWin32`, `RumbleWin32`, `AudioDeviceWin32`, `PlatformClockWin32`). |
| `include/Graphics/`, `src/Graphics/` | RHI compartido, `ForwardRendererGX2`, `TextureLoaderGX2`, `StaticMesh` (BE + BE→LE), `ShadowSystem`, `Render2D*` y `UIToolkit`. |
| `include/Graphics/GL/`, `src/Graphics/GL/` | Backend completo **OpenGL 3.3 Core** para Windows / escritorio (`GLFunctions`, `Render2DGL`, `ForwardRendererGL`, `TextureLoaderGL`, `RenderTargetGL`, `ShadowMapRendererGL`, `PostProcessHDRGL`, `RHIFullscreenShaderGL`). |
| `include/Graphics/Animation/` | Estructuras de `Skeleton`, `AnimationClip`, `JointTrack` y `AnimationLoader` binario. |
| `include/Scene/`, `src/Scene/` | `IScene`, `GameScene3D`, `SceneManager`, `HUDOverlay` + escenas showcase opcionales (`WIIU_ENGINE_BUILD_DEMOS`). |
| `include/Input/`, `src/Input/` | Abstracción unificada de mandos Wii U GamePad (VPAD), Pro Controller / Wiimote (WPAD) y Windows XInput / teclado / táctil. |
| `include/Audio/`, `src/Audio/` | `AudioEngine3D`: Doppler físico, buses con *ducking*, reverberación Schroeder y streaming BGM (`sndcore2` AX / `waveOut`). |
| `assets/models/` | Activos 3D cocinados listos para distribución (`.wmesh`, `.wskel`, `.wanim`, `.wtex`, `.wcube`). |
| `assets/shaders/` | Código fuente CafeGLSL / GLSL (`.vert`, `.frag`) y contenedores compilados GFD (`.gsh`). |
| `scripts/agent_harness.py` | Harness determinista: compilación dual (`build --all`) y 8 gates estáticos (`CRLF_CHECK`, `CORE_HEADER_LEAKS`, `ZERO_SDL_SOURCE_REFS`, `GX2_INVARIANTS`, `GL_INVARIANTS`, `COOKED_WMESH_CHECK`, `ELF_SDL_SYMBOLS`, `WIN_DLL_CHECK`). |
| `docs/TUTORIAL_PRIMER_JUEGO_MULTIPLATAFORMA.md` | Tutorial completo paso a paso para crear un juego 3D multiplataforma con el motor. |
| `tools/cooker/` | Pipeline universal de activos (`cook_model.py`, `fbx_parser.py`, `mesh_cooker.py`). |
| `tools/cafeglsl/` | Compilador oficial de shaders CafeGLSL de Nintendo para Windows. |
| `tools/EmbedGsh.ps1` | Script para embeber contenedores `.gsh` como arreglos C++ de fallback. |
| `CONTRIBUTING.md`, `LICENSE` | Guía de contribución open source y licencia Apache 2.0. |
| `HANDOFF_ANTIGRAVITY.md` | Bitácora técnica completa de hitos, arquitectura y estado del proyecto. |
| `ANEXO_OPTIMIZACIONES_PENDIENTES.md` | Registro formal de optimizaciones implementadas, pendientes y descartadas. |

---

## 🛠️ Requisitos del Entorno de Desarrollo

### 1. Toolchain de Consola (devkitPro para Wii U — 0 SDL2)
Instalar [devkitPro Updater](https://devkitpro.org/wiki/Getting_Started) en Windows (ruta recomendada: `C:\devkitPro`). Desde la terminal de **devkitPro MSYS2**, instalar los paquetes nativos (ya **no** se utiliza `wiiu-sdl2`):

```bash
pacman -Syu
pacman -S wut-tools wut-developer
```

Variables de entorno esperadas:
* `DEVKITPRO=/opt/devkitpro` (dentro de MSYS2) o `C:\devkitPro` (en Windows).
* `DEVKITPPC=/opt/devkitpro/devkitPPC`.

### 2. Toolchain Nativo Windows x64 / ASUS ROG Ally (0 DLLs Externas)
* **MinGW-w64 UCRT64 (`g++` C++20)** — probado con **WinLibs POSIX UCRT** o **MSYS2 UCRT64** + **Ninja** / **MinGW Makefiles**.
* Solo enlaza librerías estándar de Windows (`opengl32`, `gdi32`, `user32`, `winmm`, `ws2_32`, `xinput`) de forma estática (`-static`). El binario resultante `build-win/wiiu_demos.exe` **no requiere SDL2 ni ninguna DLL de terceros**.

### 3. Herramientas de Compilación en Host
* **CMake** $\ge 3.24$
* **Python** $\ge 3.10$ con biblioteca `Pillow` (`pip install Pillow`) para el procesamiento de texturas en el cooker.
* **Make** o **Ninja**

---

## 🔨 Compilación Dual: Nintendo Wii U (.wuhb) y Windows x64 (.exe)

Puedes compilar ambos destinos de forma directa con CMake o en un único comando mediante el harness del proyecto:

```powershell
# Compilar ambas plataformas (Wii U + Windows x64) y verificar binarios
python scripts/agent_harness.py build --all

# Verificar los 8 gates estáticos (LF, DAG, 0 SDL2, GX2/GL Invariants, COOKED_WMESH, WIN_DLL_CHECK)
python scripts/agent_harness.py check
```

### Compilación Manual para Nintendo Wii U
Abrir la terminal (MSYS2 o PowerShell con toolchain en PATH) en la raíz del proyecto:

```bash
# Configuración inicial o cambio de workspace
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=Release
```

> **Nota:** Si estás en PowerShell en Windows usando rutas de Windows, la ruta al toolchain suele ser `C:/devkitPro/cmake/WiiU.cmake`.

```bash
cmake --build build
```

El proceso compila los objetos C++ con optimizaciones `powerpc-eabi-g++ -O3 -ffast-math`, enlaza `wiiu_demos.elf`, genera `build/wiiu_demos.rpx`, sincroniza `assets/` hacia `build/assets/` y `build/content/`, y empaqueta **`build/wiiu_demos.wuhb`**.

### Compilación Manual para Windows x64 / ASUS ROG Ally
Desde PowerShell en la raíz del repositorio:

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j4
```

Genera **`build-win/wiiu_demos.exe`** (binario autocontenido con **0 DLLs externas**). Para ejecutarlo en otra PC o en tu **ASUS ROG Ally Z1**, copia `build-win/wiiu_demos.exe` junto con la carpeta `assets/` en el mismo directorio (`F11` alterna pantalla completa sin bordes; soporta XInput, ratón/táctil y teclado).

### Forzar Re-empaquetado tras Modificar Activos
Si modificas texturas, mallas o shaders y deseas regenerar el `.wuhb` sin recompilar todo el código C++:

```bash
# Eliminar el paquete anterior y forzar re-empaquetado
rm -f build/wiiu_demos.wuhb
cmake --build build --target wiiu_demos_wuhb
```

### Integración en Otros Proyectos (Modo Librería `-DWIIU_ENGINE_BUILD_DEMOS=OFF`)
Si deseas reutilizar el motor como librería estática en tu propio videojuego multiplataforma (Wii U o Windows x64) sin compilar el ejecutable showcase `wiiu_demos`:

```cmake
# En el CMakeLists.txt de tu proyecto externo:
set(WIIU_ENGINE_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/wiiu_engine) # ruta a este repositorio

add_executable(mi_juego src/main.cpp src/Scene/SceneMiJuego.cpp)
target_link_libraries(mi_juego PRIVATE engine_framework)
```

> `engine_framework` propaga transitivamente los directorios de cabeceras públicos (`include/`) y las **8 librerías estáticas** (`engine_graphics`, `engine_physics`, `engine_audio`, `engine_input`, `engine_ai`, `engine_net`, `engine_core`), seleccionando automáticamente Wii U GX2 (`if(WUT)`) o Windows OpenGL 3.3 Core (`else()`). Tu proyecto solo necesita `#include "WiiUEngine.hpp"`.

También puedes compilar únicamente las 8 librerías estáticas desde línea de comandos:
```bash
cmake -S . -B build_lib -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake -DWIIU_ENGINE_BUILD_DEMOS=OFF
cmake --build build_lib -j4
```

---

## 🥩 Pipeline Universal de Modelos 3D (Cooker)

El motor cuenta con una herramienta universal unificada: [`tools/cooker/cook_model.py`](tools/cooker/cook_model.py), capaz de ingerir cualquier modelo estático o esquelético desde **Autodesk FBX (binario 7.x)** o **Wavefront OBJ**, optimizarlo para el hardware de Wii U y sincronizarlo automáticamente.

### Formatos Binarios Propietarios de Wii U Generados

1. **`.wmesh` (Malla de Vértices):**
   * **Malla Estática (48 bytes/vértice):** `pos:vec3` (12B), `normal:vec3` (12B), `uv:vec2` (8B), `color:vec4` (16B).
   * **Malla Esquelética (80 bytes/vértice):** `pos:vec3` (12B), `normal:vec3` (12B), `uv:vec2` (8B), `color:vec4` (16B), `boneIndices:uvec4` (4B uint8), `boneWeights:vec4` (16B float), `padding:vec3` (12B para alineación a 16B).
   * VBO alineado a 256 bytes para DMA directo de GX2; IBO alineado a 32 bytes para caché L1D.
   * Optimización de caché de vértices post-transformación (PTVC Tipsify).
2. **`.wskel` (Jerarquía de Esqueleto):**
   * Cabecera de 32 bytes (`WSKL`, versión 1, número de articulaciones).
   * Articulaciones ordenadas topológicamente ($padre < hijo$) con matrices `invBindPose` y `localBindPose`.
3. **`.wanim` (Animación Multiclip):**
   * Cabecera de 32 bytes (`WANM`, versión 1, número de clips).
   * Múltiples pistas de animación muestreadas con interpolación TRS en cuaterniones $SO(3)$ ($\le 64$ keyframes por pista).
4. **`.wtex` (Textura Nativa de GPU):**
   * Cabecera `WTEX` con formato RGBA8 y pirámide completa de mipmaps con remuestreo Lanczos.

### Modo de Uso del Cooker Universal

```powershell
# 1. Ingesta Automática de Modelo Esquelético Animado (FBX)
python tools/cooker/cook_model.py models/bird-orange/source/BirdOrange.fbx

# 2. Ingesta de Malla Estática con Escala Personalizada (FBX u OBJ)
python tools/cooker/cook_model.py models/dutch-house/source/soloport_props.fbx --name dutch_house_props --scale 0.05

# 3. Especificar Directorio de Salida y Textura Manualmente
python tools/cooker/cook_model.py ruta/a/modelo.fbx --tex ruta/a/textura.png --output-dir assets/models
```

### Configuración Opcional JSON para Modelos (`model_config.json`)
Puedes colocar un archivo JSON junto al modelo para definir nombres de clips, poda de huesos o texturas personalizadas sin modificar código:

```json
{
  "name": "bird_orange",
  "texture": "models/bird-orange/textures/BirdOrange_BaseColor.png",
  "skeleton": {
    "max_joints": 24,
    "remap": {
      "ToesA_L": "Ankle_L",
      "ToesB_L": "Ankle_L",
      "Tail3End_M": "Tail3_M"
    }
  },
  "clips": [
    { "name": "Salto",    "start": 0.00, "end": 0.65, "kfs": 21, "loop": true },
    { "name": "Piar",     "start": 0.65, "end": 2.65, "kfs": 41, "loop": true },
    { "name": "Mirar",    "start": 2.65, "end": 5.00, "kfs": 48, "loop": true },
    { "name": "Completa", "start": 0.00, "end": 5.00, "kfs": 61, "loop": true }
  ]
}
```

---

## 🎨 Pipeline de Shaders CafeGLSL

CMake **no compila los shaders automáticamente**. Si modificas un archivo `.vert` o `.frag` en `assets/shaders/`, debes compilarlo a contenedor `.gsh` con el compilador oficial de Nintendo incluido en el repositorio.

### Paso 1: Compilar Shaders a Contenedores GFD (`.gsh`)
Desde la raíz del proyecto en PowerShell:

```powershell
$compiler = ".\tools\cafeglsl\cafeglsl-1.0.0-rc1-windows-x86_64\bin\glslcompiler.exe"

# Shaders de Animación Esquelética en GPU (Skinning LBS)
& $compiler -vs assets/shaders/skinning_gx2.vert -ps assets/shaders/skinning_gx2.frag -o assets/shaders/skinning_gx2.gsh

# Shaders de Entorno y Casas
& $compiler -vs assets/shaders/environment_gx2.vert -ps assets/shaders/environment_gx2.frag -o assets/shaders/environment_gx2.gsh

# Shaders de Sombras y Luz Dinámica
& $compiler -vs assets/shaders/shadows_gx2.vert -ps assets/shaders/shadows_gx2.frag -o assets/shaders/shadows_gx2.gsh

# Shaders de Agua Gerstner
& $compiler -vs assets/shaders/water.vert -ps assets/shaders/water.frag -o assets/shaders/water.gsh

# Shaders de Ray Tracing en GPU
& $compiler -vs assets/shaders/raytracing.vert -ps assets/shaders/raytracing.frag -o assets/shaders/raytracing.gsh
```

### Paso 2: Actualizar Fallbacks Embebidos en C++
Para garantizar que el motor funcione incluso si el archivo `.gsh` no se encuentra en el disco, actualiza la copia binaria embebida:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/EmbedGsh.ps1 -Shader skinning_gx2
powershell -NoProfile -ExecutionPolicy Bypass -File tools/EmbedGsh.ps1 -Shader environment_gx2
powershell -NoProfile -ExecutionPolicy Bypass -File tools/EmbedGsh.ps1 -Shader shadows_gx2
powershell -NoProfile -ExecutionPolicy Bypass -File tools/EmbedGsh.ps1 -Shader water
```

---

## 📡 Despliegue en Consola Física vía FTP

> [!CAUTION]
> **REGLA ESTRICTA:** No ejecutes transferencias FTP de forma automática ni desatendida. Despliega en la consola únicamente cuando lo decidas explícitamente tras validar la compilación local.

### Requisitos en la Consola Wii U
1. Tener la consola encendida ejecutando el entorno **Aroma**.
2. Tener un servidor FTP activo (ej. plugin **FTii** de Aroma o aplicación Homebrew FTP).
3. Conectar la consola a la misma red local que la PC de desarrollo.

### Comando de Subida por FTP
Ejecutar desde la terminal de Windows o PowerShell:

```powershell
# Subida directa mediante curl al almacenamiento SD de la consola
curl.exe -v -T build/wiiu_demos.wuhb ftp://192.168.0.141:21/fs/vol/external01/wiiu/apps/wiiu_demos.wuhb
```

> **Nota:** Si la IP de tu consola cambia, actualízala en el comando (ej. `192.168.0.xxx`). Una vez finalizada la subida, la aplicación aparecerá en el Menú de Wii U lista para iniciarse.

---

## 🕹️ Pruebas y Depuración en Emulador (Cemu)

Para iterar rápidamente sin encender la consola física:

1. **Abrir Cemu:** Abre el emulador **Cemu** ($\ge 2.0$).
2. **Cargar el Paquete Directamente:**
   * Menú `File` $\to$ `Install game title, update or DLC...` $\to$ Seleccionar `build/wiiu_demos.wuhb`.
   * O simplemente arrastra el archivo `build/wiiu_demos.wuhb` dentro de la ventana de Cemu.
3. **Ejecución Rápida por Línea de Comandos:**
   ```powershell
   & "C:\Ruta\A\Cemu.exe" -g "g:\Mi unidad\wiiu_demo\build\wiiu_demos.wuhb"
   ```
4. **Depuración de Cuelgues:**
   * Si la aplicación se cierra inesperadamente en Cemu, abre el archivo `log.txt` en la carpeta de instalación de Cemu.
   * Si se produce una violación de acceso `0xC0000005`, verifica que el tamaño de los componentes ECS no exceda los límites de memoria de la arena y que los registros de uniformes de GX2 coincidan exactamente con la cantidad de floats declarados en C++.

---

## 🏗️ Cómo Crear una Nueva Escena y Arrancar un Proyecto Externo

> **Tutorial Completo Paso a Paso:** Consulta [`docs/TUTORIAL_PRIMER_JUEGO_MULTIPLATAFORMA.md`](docs/TUTORIAL_PRIMER_JUEGO_MULTIPLATAFORMA.md) para ver un ejemplo completo con carga asíncrona, PBR/sombras, cámara orbital, entrada unificada (GamePad + XInput + táctil), ECS/física, audio 3D y compilación dual.

La forma más rápida de construir un juego 3D multiplataforma (idéntico código para **Wii U GX2** y **Windows x64 OpenGL 3.3 Core**) reutilizando cámara orbital anti-colisiones, luces, sombras y texturas es heredar de [`GameScene3D`](include/Scene/GameScene3D.hpp) usando [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp):

1. **Crear tu Escena heredando de `GameScene3D` (`SceneMiJuego.hpp`):**
   ```cpp
   #pragma once
   #include "WiiUEngine.hpp"
   #include <vector>

   class SceneMiJuego : public WiiU::GameScene3D {
   public:
       void LoadAsync() override {
           // Lectura segura en Core 2 (Wii U) o hilo trabajador (Windows)
           WiiU::AssetManager::LoadBinaryFile("models/dutch_house.wmesh", m_stagedMesh);
           WiiU::AssetManager::LoadBinaryFile("models/dutch_house.wtex", m_stagedTex);
       }

       void OnGameInit() override {
           // Carga a VRAM en hilo principal (GX2 en Wii U / OpenGL 3.3 VBO-Texture en Windows)
           m_mesh.LoadFromMemory(m_stagedMesh.data(), m_stagedMesh.size());
           m_albedo = LoadSceneTextureFromMemory(m_stagedTex.data(), m_stagedTex.size(), "HouseTex");
           std::vector<uint8_t>{}.swap(m_stagedMesh);
           std::vector<uint8_t>{}.swap(m_stagedTex);
       }

       void OnGameUpdate(float dt) override {
           m_clock.Tick(dt);
       }

       void OnGameRender3D(WiiU::ForwardRendererGX2& renderer) override {
           renderer.RenderStaticMesh(
               m_mesh,
               WiiU::Matrix4x4::Identity(),
               GetSceneTexture(m_albedo)
           );
       }

       [[nodiscard]] std::string GetSceneName() const override { return "Mi Juego 3D"; }

   private:
       WiiU::StaticMesh m_mesh;
       WiiU::ResourceHandle<GX2Texture> m_albedo; // Liberación RAII automática en OnExit()
       WiiU::GameClock m_clock;
       std::vector<uint8_t> m_stagedMesh;
       std::vector<uint8_t> m_stagedTex;
   };
   ```

2. **Arrancar directamente sin depender de los menús showcase (`main.cpp`):**
   ```cpp
   #include "WiiUEngine.hpp"
   #include "SceneMiJuego.hpp"
   #include <memory>

   int main(int argc, char* argv[]) {
       (void)argc; (void)argv;
       {
           WiiU::Engine engine; // Ámbito RAII para apagar GX2/OpenGL/Audio antes de salir
           if (engine.Init(std::make_unique<SceneMiJuego>())) {
               engine.Run();
           }
       }
       return 0;
   }
   ```

3. **Si deseas integrarla además en el Showcase de `wiiu_demos`:**
   * Agrega `src/Scene/SceneMiDemo.cpp` dentro del bloque `WIIU_ENGINE_BUILD_DEMOS` en [`CMakeLists.txt`](CMakeLists.txt).
   * Registra su opción en [`src/Scene/SceneMenuModels.cpp`](src/Scene/SceneMenuModels.cpp) y las fábricas opcionales de retorno en [`src/main.cpp`](src/main.cpp).

---

## 📜 Reglas y Directivas de Desarrollo

El desarrollo de este motor se rige por directivas de ingeniería de software obligatorias:

1. **Regla 1 (FTP Controlado):** Jamás desplegar automáticamente a la consola física vía FTP sin orden explícita.
2. **Regla 2 (Revisión en Cascada de 2 Fases):** Todo cambio en el código o pipeline de activos debe someterse a revisión por los subagentes especializados:
   - **Fase 1 (`graphics_code_reviewer`):** Silicio Latte GX2, eDRAM, shaders, VBO/IBO, texturas, VSync **y OpenGL 3.3 Core / Win32 PAL**.
   - **Fase 2 (`engine_clean_code_reviewer`):** Arquitectura de motor (DAG de 8 librerías), Clean Code C++20, RAII, DOD/ECS, 0 SDL2 y cero asignaciones en caliente.
3. **Regla 3 (Actualización de Handoff):** Cada hito completado debe documentarse en [`HANDOFF_ANTIGRAVITY.md`](HANDOFF_ANTIGRAVITY.md) y [`ANEXO_OPTIMIZACIONES_PENDIENTES.md`](ANEXO_OPTIMIZACIONES_PENDIENTES.md) con saltos de línea LF puros (`\n`).
4. **Regla 4 (Autonomía de Workspace):** Plena autonomía para ejecutar `scripts/agent_harness.py` (`check`, `build --all`), CMake y manipulaciones dentro del repositorio; autorización previa obligatoria para despliegues FTP o comandos fuera del workspace.
