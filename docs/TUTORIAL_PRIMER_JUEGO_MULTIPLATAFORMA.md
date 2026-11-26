# Tutorial: Tu Primer Juego 3D Multiplataforma (Wii U + Windows x64 / ROG Ally)

Este tutorial te guía paso a paso para crear una escena 3D completa usando la API pública del motor ([`include/WiiUEngine.hpp`](../include/WiiUEngine.hpp)), aprovechando exactamente el mismo código C++20 tanto en **Nintendo Wii U (AMD Latte GX2)** como en **Windows x64 / ASUS ROG Ally Z1 (OpenGL 3.3 Core, 0 SDL2, 0 DLLs externas)**.

---

## 1. Preparar un Proyecto Externo con CMake

Para usar el motor como librería estática sin compilar las demos de prueba:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MiJuego3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Desactiva el ejecutable de demos y construye únicamente las 8 librerías estáticas
set(WIIU_ENGINE_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/wiiu_engine)

add_executable(mi_juego
    src/main.cpp
    src/SceneMiJuego.cpp
)

# engine_framework enlaza automáticamente engine_graphics, engine_physics,
# engine_audio, engine_input, engine_ai, engine_net y engine_core
# y selecciona el backend GX2 (WUT) u OpenGL 3.3 Core (Windows).
target_link_libraries(mi_juego PRIVATE engine_framework)
```

> **Inmutabilidad del DAG:** En tus headers públicos evita `#include <gx2/...>`, `<windows.h>` o `<GL/gl.h>`; trabaja siempre sobre `#include "WiiUEngine.hpp"`.

---

## 2. Preparar Activos 3D y Texturas con el Cooker

Antes de cargar geometría en tiempo de ejecución, convierte tus archivos `.fbx` u `.obj` y texturas `.png` a formatos nativos cocinados (`.wmesh`, `.wskel`, `.wanim`, `.wtex`) mediante [`tools/cooker/cook_model.py`](../tools/cooker/cook_model.py):

```powershell
# Modelo estático con escala personalizada
python tools/cooker/cook_model.py ruta/a/mi_modelo.fbx --name mi_modelo --scale 0.05 --output-dir assets/models

# Modelo con textura específica
python tools/cooker/cook_model.py ruta/a/personaje.fbx --tex ruta/a/albedo.png --output-dir assets/models
```

El motor resuelve rutas relativas como `"models/mi_modelo.wmesh"` automáticamente en ambas plataformas (`content:/` en Wii U y `assets/` en Windows) y convierte el formato Big-Endian de Wii U a Little-Endian al subirlo a OpenGL 3.3 Core.

---

## 3. Crear tu Escena 3D (`GameScene3D`)

[`GameScene3D`](../include/Scene/GameScene3D.hpp) sigue el patrón *Template Method*: gestiona el Z-Buffer, el limpiado de frame, la cámara orbital (`OrbitCameraSystem`), el mundo ECS (`ECSWorld`), los pases 3D/2D y la liberación automática de texturas (`ResourcePool`).

### `SceneMiJuego.hpp`

```cpp
#pragma once

#include "WiiUEngine.hpp"
#include <vector>

class SceneMiJuego final : public Neleo::GameScene3D {
public:
    void LoadAsync() override;
    void OnGameInit() override;
    void OnGameExit() override;
    void OnGameUpdate(float dt) override;
    void OnGameRender3D(Neleo::ForwardRendererGX2& renderer) override;
    void OnGameRenderDebug3D() override;
    void OnGameRenderHUD() override;

    [[nodiscard]] std::string GetSceneName() const override { return "Mi Juego 3D"; }

private:
    Neleo::StaticMesh m_mesh;
    Neleo::MaterialPropertyBlock m_material{};
    Neleo::ResourceHandle<GX2Texture> m_albedoHandle{};
    Neleo::GameClock m_clock{};

    Neleo::Point3D m_playerPos{0.0f, 0.5f, 0.0f};
    float m_yawRad{0.0f};
    float m_elapsedSec{0.0f};
    bool m_showDebug{false};

    // Buffers temporales usados únicamente entre LoadAsync() y OnGameInit()
    std::vector<uint8_t> m_stagedMesh;
    std::vector<uint8_t> m_stagedTex;
};
```

### `SceneMiJuego.cpp` — Carga Asíncrona y Configuración Inicial

1. **`LoadAsync()`** se ejecuta en hilo secundario (Core 2 en Wii U): **nunca** llames aquí a GX2 ni a OpenGL; limita este paso a leer bytes con `AssetManager::LoadBinaryFile`.
2. **`OnGameInit()`** corre en el hilo principal con GPU lista: sube mallas/texturas, libera buffers temporales y configura iluminación y cámara.

```cpp
#include "SceneMiJuego.hpp"
#include <cstdio>
#include <cmath>

void SceneMiJuego::LoadAsync() {
    Neleo::AssetManager::LoadBinaryFile("models/dutch_house.wmesh", m_stagedMesh);
    Neleo::AssetManager::LoadBinaryFile("models/dutch_house.wtex", m_stagedTex);
}

void SceneMiJuego::OnGameInit() {
    // 1. Subir geometría y textura a VRAM (GX2 en Wii U / GL 3.3 VBO+Tex2D en Windows)
    if (!m_stagedMesh.empty()) {
        m_mesh.LoadFromMemory(m_stagedMesh.data(), m_stagedMesh.size());
    }
    if (!m_stagedTex.empty()) {
        m_albedoHandle = LoadSceneTextureFromMemory(m_stagedTex.data(), m_stagedTex.size(), "Albedo");
    }
    // Liberar memoria staging de CPU de inmediato
    std::vector<uint8_t>{}.swap(m_stagedMesh);
    std::vector<uint8_t>{}.swap(m_stagedTex);

    // 2. Configurar cielo, cámara y material PBR (Zero-Alloc)
    SetClearColor({118, 178, 224, 255});
    SetCameraProjection(55.0f, 0.25f, 500.0f);
    m_orbitCamera.Configure(6.0f, 3.0f, 18.0f, 1.2f);
    m_orbitCamera.SetTargetPosition(m_playerPos);

    m_material.diffuseTexture = GetSceneTexture(m_albedoHandle);
    m_material.roughness = 0.55f;
    m_material.metallic = 0.08f;
    m_material.ambientIntensity = 0.35f;
    m_material.normalMapEnabled = true;

    // 3. Registrar música o audio 3D si dispones de un archivo WAV en assets/audio/
    Neleo::AudioEngine3D::Instance().SetListenerTransform(m_playerPos, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
}

void SceneMiJuego::OnGameExit() {
    // Las texturas registradas en m_albedoHandle se liberan automáticamente por RAII en GameScene3D::OnExit()
    m_mesh.Unload();
}
```

---

## 4. Entrada Unificada, Háptica y Lógica a 60 FPS (`OnGameUpdate`)

Usa siempre [`InputManager::Instance().GetGamepad(0)`](../include/Input/InputManager.hpp) y [`VirtualButton`](../include/Input/VirtualGamepad.hpp). Así tu código funciona igual con:
- **Wii U GamePad / Pro Controller / Wiimote**
- **Mandos XInput / ASUS ROG Ally**, teclado/ratón y pantalla táctil en **Windows x64**

```cpp
void SceneMiJuego::OnGameUpdate(float dt) {
    m_clock.Tick(dt);
    m_elapsedSec += dt;

    const auto& pad = Neleo::InputManager::Instance().GetGamepad(0);

    // 1. Movimiento con Stick Izquierdo (zona muerta radial aplicada por el motor)
    const float moveSpeed = 4.5f;
    m_playerPos.x += pad.leftStick.x * moveSpeed * dt;
    m_playerPos.z -= pad.leftStick.y * moveSpeed * dt;

    // 2. Cámara orbital con Stick Derecho
    m_orbitCamera.AddInput(pad.rightStick.x * 2.2f * dt, pad.rightStick.y * 1.6f * dt);
    m_orbitCamera.SetTargetPosition(m_playerPos);
    m_orbitCamera.Update(dt);

    // 3. Botón A (o clic/táctil): activar vibración háptica multiplataforma
    if (pad.WasJustPressed(Neleo::VirtualButton::A)) {
        Neleo::RumbleManager::Instance().PlayPulse(0, 0.7f, 0.7f, 0.18f);
    }

    // 4. Botón Y: alternar wireframes 3D de depuración
    if (pad.WasJustPressed(Neleo::VirtualButton::Y)) {
        m_showDebug = !m_showDebug;
    }

    // 5. Actualizar listener espacial y sistema físico/ECS (0 heap allocations)
    Neleo::AudioEngine3D::Instance().SetListenerTransform(m_playerPos, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    m_ecsWorld.UpdateTransforms();
}
```

> **Regla de Oro 60 FPS:** No hagas `new`, `std::string` dinámicos ni `push_back` sin reserva previa dentro de `OnGameUpdate`, `OnGameRender3D` ni `OnGameRenderHUD`. Usa buffers en pila (`char buf[96]` + `std::snprintf`) o `StackString<N>`.

---

## 5. Renderizado 3D PBR, Sombras y HUD (`OnGameRender3D` / `DebugDraw3D` / `Render2D`)

`ForwardRendererGX2` ejecuta automáticamente **AMD Latte GX2** en Wii U u **OpenGL 3.3 Core** en Windows/ROG Ally con las mismas llamadas:

```cpp
void SceneMiJuego::OnGameRender3D(Neleo::ForwardRendererGX2& renderer) {
    renderer.SetShaderTime(m_elapsedSec);

    if (m_mesh.IsLoaded()) {
        const Neleo::Matrix4x4 model =
            Neleo::Matrix4x4::Translation(m_playerPos.x, 0.0f, m_playerPos.z) *
            Neleo::Matrix4x4::RotationY(m_yawRad);

        // shadingMode: 0u = PBR Cook-Torrance (GGX + luces), 1u = Blinn-Phong, 8u = Terreno/Procedural, 9u = Toon NPR
        renderer.RenderStaticMeshPBR(m_mesh, model, m_material, 0u);
    }
}

void SceneMiJuego::OnGameRenderDebug3D() {
    if (!m_showDebug) return;

    Neleo::DebugDraw3D::SetMatrices(GetViewMatrix(), GetProjectionMatrix());
    Neleo::DebugDraw3D::DrawAxes(m_playerPos, 1.2f);
    Neleo::DebugDraw3D::DrawAABB(
        {m_playerPos.x - 1.0f, 0.0f, m_playerPos.z - 1.0f},
        {m_playerPos.x + 1.0f, 2.0f, m_playerPos.z + 1.0f},
        {80, 255, 120, 255}
    );
}

void SceneMiJuego::OnGameRenderHUD() {
    // Coordenadas virtuales unificadas 1280x720 (escaladas automáticamente en TV, DRC y 16:9 Windows)
    Neleo::Render2D::DrawBox(24, 20, 410, 84, {12, 18, 28, 190}, {90, 175, 255, 255});

    char line[96];
    std::snprintf(line, sizeof(line), "MI JUEGO 3D | %.1f FPS (%.2f ms)", GetFPS(), GetFrameTimeMs());
    Neleo::Render2D::DrawPixelString(line, 40, 36, 2, {255, 255, 255, 255});
    Neleo::Render2D::DrawPixelString("[A] Vibracion  [Y] Wireframe  [L/R] Camara", 40, 68, 2, {185, 220, 255, 255});
}
```

### Funciones Gráficas Avanzadas Disponibles sin Cambiar de Código
- **Shadow Mapping 2-Pass (`ShadowMapRendererGX2`):** Enciende `BeginShadowPass()` → `RenderCaster()` → `EndShadowPass()` + `BindShadowMap()` y activa `renderer.SetDirectionalShadowState(lightVP, 2u, 0.002f, 1.0f / 1024.0f)` para PCF 3×3/5×5 hardware (ver [`src/Scene/SceneQA_Shadows.cpp`](../src/Scene/SceneQA_Shadows.cpp)).
- **Post-Procesado HDR + Kawase Bloom (`PostProcessHDRGX2`):** Envuelve tu escena con `BeginHDRScenePass()` y `EndHDRScenePassAndCompose(settings, ...)` para ACES Filmic, vignette y bloom 16F (ver [`src/Scene/SceneQA_PostProcessAlpha.cpp`](../src/Scene/SceneQA_PostProcessAlpha.cpp)).
- **ECS + Física de Cuerpos Rígidos (`engine_physics`):** Agrega `TransformComponent`, `ColliderComponent` y `RigidBodyComponent` a tus entidades en `m_ecsWorld` y avanza con `RigidBodyPhysicsSystem`.
- **Modo Offline/Online (`engine_net`):** Usa `Neleo::Net::NetSession` para UDP/TCP sobre `nsysnet` (Wii U) o `ws2_32` (Windows).

---

## 6. Punto de Entrada (`main.cpp`)

```cpp
#include "WiiUEngine.hpp"
#include "SceneMiJuego.hpp"
#include <memory>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    {
        Neleo::Engine engine; // Gestiona init y shutdown ordenados (GX2/OpenGL, Audio, Input, Window)
        if (engine.Init(std::make_unique<SceneMiJuego>())) {
            engine.Run();
        }
    }
    return 0;
}
```

---

## 7. Compilar, Verificar y Probar en Ambas Plataformas

### En Windows x64 / ASUS ROG Ally
```powershell
# Compilación dual (Wii U + Windows) desde la raíz del motor:
python scripts/agent_harness.py build --all

# Chequeos estáticos (0 CRLF, 0 SDL2, DAG, invariantes GX2/GL, 0 DLLs de terceros):
python scripts/agent_harness.py check
```
Ejecuta **`build-win/wiiu_demos.exe`** (o tu binario `mi_juego.exe`) manteniendo la carpeta **`assets/`** en el directorio de trabajo:
- Pulsa **`F11`** para alternar pantalla completa sin bordes (ideal en ROG Ally 1080p con letterbox 16:9 automático).
- Controles compatibles sin configuración extra: mando **XInput** integrado, teclado/ratón y pantalla táctil Windows.

### En Emulador Cemu o Nintendo Wii U Real
- Arrastra **`build/wiiu_demos.wuhb`** a **Cemu $\ge 2.0$** para validación en emulador.
- Para consola Wii U física con Aroma + FTP activo, realiza la transferencia **solo cuando lo decidas explícitamente**:
  ```powershell
  curl.exe -v -T build/wiiu_demos.wuhb ftp://192.168.0.141:21/fs/vol/external01/wiiu/apps/wiiu_demos.wuhb
  ```
