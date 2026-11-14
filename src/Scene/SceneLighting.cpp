#include "Scene/SceneLighting.hpp"
#include "Scene/SceneMenuSDL.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Core/StackString.hpp"
#include <cmath>

namespace WiiU {

SceneLighting::SceneLighting()
    : m_backButton([]() {
          SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuSDL>());
      }) {}

void SceneLighting::Update(float dt) {
    m_cube.Update(dt);

    // Orbitar la dirección de la luz 3D con el tiempo
    m_lightOrbitAngle += 1.5f * dt;
    m_light.direction.x = std::cos(m_lightOrbitAngle);
    m_light.direction.z = std::sin(m_lightOrbitAngle);

    if (dt > 0.0001f) {
        float instFps = 1.0f / dt;
        m_smoothedFps = m_smoothedFps * 0.90f + instFps * 0.10f;
    }

    const auto& touch = InputManager::Instance().GetTouchState();

    if (m_backButton.UpdateAndHandle(touch)) {
        return;
    }

    if (touch.active && touch.justPressed) {
        m_cube.ScrambleFaceColors();
    }
}

void SceneLighting::Render(SDL_Renderer* renderer) {
    // 1. Dibujar Cubo 3D con Iluminación Dinámica Nativa GX2 (Single Draw Call + Back-Face Culling)
    m_cube.Render(renderer, 1280, 720, m_light);

    // 2. Botón Volver al Menú Principal
    m_backButton.Render(renderer);

    // 3. HUD Informativo de la Demo de Iluminación Híbrida GX2 con Contador de FPS
    constexpr SDL_Rect hudBox = { 40, 580, 1200, 100 };
    Render2D::drawBox(renderer, hudBox.x, hudBox.y, hudBox.w, hudBox.h, ColorRGBA{10, 20, 35, 230}, ColorRGBA::Cyan());

    const bool fpsLimited = SceneManager::Instance().IsFPSLimitEnabled();
    StackString<128> titleStr;
    titleStr.Format("DEMO 1: ILUMINACION 3D HIBRIDA - FPS: %d FPS (%s)",
                    static_cast<int>(m_smoothedFps + 0.5f),
                    fpsLimited ? "LIMITADO A 60 FPS" : "DESBLOQUEADO / UNCAPPED");
    Render2D::drawPixelString(renderer, titleStr.string_view(), 60, 600, 2, ColorRGBA::Yellow());
    Render2D::drawPixelString(renderer, "LUZ ORBITAL EN TIEMPO REAL - ILUMINACION DINAMICA PHONG / GOURAUD", 60, 628, 2, ColorRGBA::White());
    Render2D::drawPixelString(renderer, "TOCA CUALQUIER PUNTO DE LA PANTALLA PARA MEZCLAR LOS COLORES BASE", 60, 655, 2, ColorRGBA{180, 180, 180, 255});
}

} // namespace WiiU
