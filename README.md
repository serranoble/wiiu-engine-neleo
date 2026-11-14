# 🎮 Wii U 3D Graphics & Game Engine (C++20)

Motor de videojuegos modular de alto rendimiento y suite de demos gráficas para **Nintendo Wii U**, desarrollado en **C++20** y orientado al entorno **Aroma (Homebrew)**. El motor aprovecha la arquitectura heterogénea de la consola (CPU tri-núcleo IBM Espresso PowerPC 750CL y GPU AMD Latte GX2 con memoria eDRAM de 560 GB/s), combinando un framework ECS orientado a datos (DOD) con renderizado GX2 nativo en silicio, audio posicional 3D, cámara orbital anti-colisiones y animación esquelética con *Linear Blend Skinning* (LBS).

---

> [!TIP]
> **📘 MOTOR MODULAR OPEN SOURCE PARA NINTENDO WII U (C++20 / GX2):**
> - **Umbrella Header Público:** [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp) expone los 7 módulos estáticos desacoplados del motor (`engine_core`, `engine_physics`, `engine_audio`, `engine_input`, `engine_ai`, `engine_graphics`, `engine_framework`).
> - **Guía de Framework y Contribución:** Lee [`docs/FRAMEWORK_ENGINE_GUIDE.md`](docs/FRAMEWORK_ENGINE_GUIDE.md), [`CONTRIBUTING.md`](CONTRIBUTING.md) y la licencia [`LICENSE`](LICENSE) (Apache 2.0).
> - **Auditoría Jason Gregory (*Game Engine Architecture*, 3ª Ed.):** **98.0% Core Engine / 93.0% Libro Completo** — ver [`docs/JASON_GREGORY_COMPLETION_AUDIT.md`](docs/JASON_GREGORY_COMPLETION_AUDIT.md).

---

## 📋 Tabla de Contenidos

1. [Arquitectura Modular del Motor y Características](#-arquitectura-modular-del-motor-y-características)
2. [Estructura del Repositorio](#-estructura-del-repositorio)
3. [Requisitos del Entorno de Desarrollo](#-requisitos-del-entorno-de-desarrollo)
4. [Compilación y Generación del Paquete (.wuhb)](#-compilación-y-generación-del-paquete-wuhb)
5. [Pipeline Universal de Modelos 3D (Cooker)](#-pipeline-universal-de-modelos-3d-cooker)
6. [Pipeline de Shaders CafeGLSL](#-pipeline-de-shaders-cafeglsl)
7. [Despliegue en Consola Física vía FTP](#-despliegue-en-consola-física-vía-ftp)
8. [Pruebas y Depuración en Emulador (Cemu)](#-pruebas-y-depuración-en-emulador-cemu)
9. [Cómo Crear una Nueva Escena en el Motor](#-cómo-crear-una-nueva-escena-en-el-motor)
10. [Reglas y Directivas de Desarrollo](#-reglas-y-directivas-de-desarrollo)

---

## 🏛️ Arquitectura Modular del Motor y Características

El motor está desacoplado en **7 librerías estáticas CMake** independientes (ninguna depende de escenas demo ni menús; todo consumidor externo enlaza `engine_framework` e incluye [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp)):

| Librería CMake | Namespace / Subsistema | Responsabilidad Principal |
| :--- | :--- | :--- |
| **`engine_core`** | `WiiU` (`Core/`, `ECS/`, `Spatial/`, `AI` types, `Time/`, `Events/`, `Storage/`, `Config/`) | Loop base, `ECSWorld` DOD, `MemoryManager`/`ResourcePool`, `JobSystem` SMP, `SpatialBVH`, `GameClock`, `GameplayEventBus`, `StorageManager` SAL/VFS, `EngineConfig`. Cero cabeceras `<gx2/*.h>`. |
| **`engine_physics`** | `WiiU` (`CollisionSystem`, `KinematicCharacterSystem`, `RigidBodyPhysicsSystem`) | Barrido cinemático KCC, detección continua, cuerpos rígidos SO(3), impulsos secuenciales + fricción de Coulomb y restricciones XPBD. |
| **`engine_audio`** | `WiiU` (`AudioManager`, `AudioEngine3D`, `AudioSpatialSystem`) | Mixer 32 voces, paneo azimutal, LPF oclusión, efecto Doppler físico, *sidechain ducking* por buses y reverberación estéreo Schroeder. |
| **`engine_input`** | `WiiU` (`InputManager`, `RumbleManager`) | HAL unificado VPAD/WPAD/Pro Controller, curvas de respuesta no lineales, contexto jerárquico de acciones y háptica ADSR. |
| **`engine_ai`** | `WiiU::AI` (`NavGraph`, `AStarPathfinder`, `SteeringBehaviors`, `AISteeringSystem`) | Navegación A* en memoria de pila, comportamientos de Craig Reynolds y máquina de estados cognitiva FSM. |
| **`engine_graphics`** | `WiiU` / `WiiU::Graphics` (`RHI/`, `ForwardRendererGX2`, `TextureLoaderGX2`, `ShadowSystem`, `StaticMesh`, `Render2DGX2`, `DebugDraw3D`, `UIToolkit`) | Renderizado hardware AMD Latte GX2, Cascaded Shadow Maps (CSM), cargas `.wmesh`/`.wtex`/`.wcube`, LBS *skinning* en GPU y primitivas 2D/UI. |
| **`engine_framework`** | `WiiU` (`Engine`, `SceneManager`, `GameScene3D`, `HUDOverlay`, `ResourceMonitor`, `SceneLoading`, `SceneAssetError`) | Orquestación de arranque con escena inyectable `Engine::Init(std::unique_ptr<IScene>)`, navegación desacoplada por fábricas (`SetHomeMenuFactory` / `SetOptionsSceneFactory`) y clase base `GameScene3D`. |

* **Renderizado Dual:**
  - **GX2 Nativo en Silicio:** Control directo del procesador de comandos de la GPU AMD Latte GX2, pases de render en eDRAM (MEM1 32 MB), z-buffer jerárquico HiZ y shaders CafeGLSL directos.
  - **SDL2 Fallback:** Ruta de renderizado 2D/3D por software para pruebas y menús clásicos.
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
| `include/Graphics/`, `src/Graphics/` | RHI de GX2, `ForwardRendererGX2`, `TextureLoaderGX2` (`.wtex`/`.wcube`), `StaticMesh`, `ShadowSystem`, `Render2DGX2` y `UIToolkit`. |
| `include/Graphics/Animation/` | Estructuras de `Skeleton`, `AnimationClip`, `JointTrack` y `AnimationLoader` binario. |
| `include/Scene/`, `src/Scene/` | `IScene`, `GameScene3D`, `SceneManager`, `HUDOverlay` + escenas showcase opcionales (`WIIU_ENGINE_BUILD_DEMOS`). |
| `include/Input/`, `src/Input/` | Abstracción de mandos Wii U GamePad (VPAD), Pro Controller y Wiimote (WPAD). |
| `include/Audio/`, `src/Audio/` | `AudioEngine3D`: Doppler físico, buses con *ducking*, reverberación Schroeder y streaming BGM. |
| `assets/models/` | Activos 3D cocinados listos para distribución (`.wmesh`, `.wskel`, `.wanim`, `.wtex`, `.wcube`). |
| `assets/shaders/` | Código fuente CafeGLSL (`.vert`, `.frag`) y contenedores compilados GFD (`.gsh`). |
| `tools/cooker/` | Pipeline universal de activos (`cook_model.py`, `fbx_parser.py`, `mesh_cooker.py`). |
| `tools/cafeglsl/` | Compilador oficial de shaders CafeGLSL de Nintendo para Windows. |
| `tools/EmbedGsh.ps1` | Script para embeber contenedores `.gsh` como arreglos C++ de fallback. |
| `CONTRIBUTING.md`, `LICENSE` | Guía de contribución open source y licencia Apache 2.0. |
| `HANDOFF_ANTIGRAVITY.md` | Bitácora técnica completa de hitos, arquitectura y estado del proyecto. |
| `ANEXO_OPTIMIZACIONES_PENDIENTES.md` | Registro formal de optimizaciones implementadas, pendientes y descartadas. |

---

## 🛠️ Requisitos del Entorno de Desarrollo

### 1. Toolchain de Consola (devkitPro para Wii U)
Instalar [devkitPro Updater](https://devkitpro.org/wiki/Getting_Started) en Windows (ruta recomendada: `C:\devkitPro`). Desde la terminal de **devkitPro MSYS2**, instalar los paquetes necesarios:

```bash
pacman -Syu
pacman -S wut-tools wut-developer wiiu-sdl2
```

Variables de entorno esperadas:
* `DEVKITPRO=/opt/devkitpro` (dentro de MSYS2) o `C:\devkitPro` (en Windows).
* `DEVKITPPC=/opt/devkitpro/devkitPPC`.

### 2. Herramientas de Compilación en Host
* **CMake** $\ge 3.24$
* **Python** $\ge 3.10$ con biblioteca `Pillow` (`pip install Pillow`) para el procesamiento de texturas en el cooker.
* **Make** o **Ninja**

---

## 🔨 Compilación y Generación del Paquete (.wuhb)

Toda la compilación del motor se realiza mediante CMake configurado con el toolchain de Nintendo Wii U provisto por devkitPro.

### Paso 1: Configurar el Directorio de Compilación (CMake)
Abrir la terminal (MSYS2 o PowerShell con toolchain en PATH) en la raíz del proyecto:

```bash
# Configuración inicial o cambio de workspace
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=Release
```

> **Nota:** Si estás en PowerShell en Windows usando rutas de Windows, la ruta al toolchain suele ser `C:/devkitPro/cmake/WiiU.cmake`.

### Paso 2: Compilar el Ejecutable y Empaquetar WUHB
Ejecutar la compilación incremental:

```bash
cmake --build build
```

El proceso compila los objetos C++ con optimizaciones `powerpc-eabi-g++ -O3 -ffast-math`, enlaza el ejecutable `wiiu_demos.elf`, lo convierte en ejecutable de Wii U `wiiu_demos.rpx`, sincroniza los activos hacia `build/assets/` y `build/content/`, y genera finalmente el paquete listo para Aroma:
* **`build/wiiu_demos.wuhb`** (Paquete distribuible de Wii U)
* **`build/wiiu_demos.rpx`** (Binario ejecutable)

### Forzar Re-empaquetado tras Modificar Activos
Si modificas texturas, mallas o shaders y deseas regenerar el `.wuhb` sin recompilar todo el código C++:

```bash
# Eliminar el paquete anterior y forzar re-empaquetado
rm -f build/wiiu_demos.wuhb
cmake --build build --target wiiu_demos_wuhb
```

### Integración en Otros Proyectos (Modo Librería `-DWIIU_ENGINE_BUILD_DEMOS=OFF`)
Si deseas reutilizar el motor como librería estática en tu propio videojuego Wii U sin compilar el ejecutable showcase `wiiu_demos`:

```cmake
# En el CMakeLists.txt de tu proyecto externo:
set(WIIU_ENGINE_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/wiiu_engine) # ruta a este repositorio

add_executable(mi_juego.elf src/main.cpp src/Scene/SceneMiJuego.cpp)
target_link_libraries(mi_juego.elf PRIVATE engine_framework)
```

> `engine_framework` propaga transitivamente los directorios de cabeceras públicos (`include/`), la definición `-D__WIIU__` y las 6 librerías subyacentes (`engine_graphics`, `engine_physics`, `engine_audio`, `engine_input`, `engine_ai`, `engine_core`), de modo que tu proyecto solo necesita `#include "WiiUEngine.hpp"`.

También puedes compilar únicamente las 7 librerías estáticas desde línea de comandos:
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

La forma más rápida de construir un juego 3D reutilizando el pipeline GX2, cámara orbital anti-colisiones, luces, sombras y texturas es heredar de [`GameScene3D`](include/Scene/GameScene3D.hpp) usando [`include/WiiUEngine.hpp`](include/WiiUEngine.hpp):

1. **Crear tu Escena heredando de `GameScene3D` (`SceneMiJuego.hpp`):**
   ```cpp
   #pragma once
   #include "WiiUEngine.hpp"

   class SceneMiJuego : public WiiU::GameScene3D {
   public:
       void LoadAsync() override {
           m_mesh.LoadFromFile("content:/models/dutch_house_complete.wmesh");
       }

       void OnGameInit() override {
           m_albedo = LoadSceneTexture("content:/models/dutch_house.wtex");
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
           WiiU::Engine engine; // Ámbito RAII para apagar GX2/SDL/Audio antes de salir
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
   - **Fase 1 (`graphics_code_reviewer`):** Silicio Latte GX2, eDRAM, shaders, VBO/IBO, texturas y VSync.
   - **Fase 2 (`engine_clean_code_reviewer`):** Arquitectura de motor, Clean Code C++20, RAII, DOD/ECS y cero asignaciones en caliente.
3. **Regla 3 (Actualización de Handoff):** Cada hito completado debe documentarse en [`HANDOFF_ANTIGRAVITY.md`](HANDOFF_ANTIGRAVITY.md) con saltos de línea LF puros (`\n`).
4. **Regla 4 (Autonomía de Workspace):** Plena autonomía para ejecutar CMake, compilaciones y manipulaciones de archivos dentro del repositorio; autorización previa obligatoria para comandos a nivel de sistema operativo fuera del workspace.
