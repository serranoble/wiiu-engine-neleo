#include "Scene/SceneInteractive.hpp"
#include "Scene/SceneMenuSDL.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Color.hpp"

namespace WiiU {

SceneInteractive::SceneInteractive()
    : m_square((1280.0f - 90.0f) / 2.0f, (720.0f - 90.0f) / 2.0f, 90.0f),
      m_backButton([]() {
          SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuSDL>());
      }) {}

void SceneInteractive::Update(float dt) {
    auto& input = InputManager::Instance();
    const auto& touch = input.GetTouchState();

    if (m_backButton.UpdateAndHandle(touch)) {
        return;
    }

    if (input.WasActionTriggeredThisFrame()) {
        m_cube.ScrambleFaceColors();
        m_ring.Trigger();
    }

    if (dt > 0.0001f) {
        float instFps = 1.0f / dt;
        m_smoothedFps = m_smoothedFps * 0.90f + instFps * 0.10f;
    }

    m_cube.Update(dt);
    m_square.Update(dt, touch, input);
    m_ring.Update(dt);
}

void SceneInteractive::Render(SDL_Renderer* renderer) {
    // 1. Renderizar Cubo 3D Nativo GX2 (Single Geometry Draw Call)
    m_cube.Render(renderer, 1280, 720, m_light);

    // 2. Renderizar Anillo Cyan con primitivas de hardware
    m_ring.Render(renderer, 640, 360);

    // 3. Renderizar Cuadrado Blanco Arrastrable con geometría batcheada
    m_square.Render(renderer);

    // 4. Renderizar HUD Overlay tradicional con contador de FPS
    m_hud.Render(renderer, InputManager::Instance(), m_smoothedFps);

    // 5. Botón Volver al Menú
    m_backButton.Render(renderer);
}

} // namespace WiiU
