#include "Scene/SceneMenuSDL.hpp"
#include "Scene/SceneLighting.hpp"
#include "Scene/SceneParticles.hpp"
#include "Scene/SceneInteractive.hpp"
#include "Scene/SceneRaytracing.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"

namespace WiiU {

SceneMenuSDL::SceneMenuSDL()
    : m_starfield(120),
      m_backButton([]() {
          SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
      }) {
    InitMenuItems();
}

void SceneMenuSDL::InitMenuItems() {
    m_menuItems.reserve(4);

    auto addMenuItem = [this](int y, std::string label, std::string desc, const char* action, std::function<void()> onSelect) {
        UIButton btn(280, y, 720, 52, std::move(label), 3);
        btn.SetColors(
            ColorRGBA{15, 30, 55, 230},
            ColorRGBA{0, 180, 240, 240},
            ColorRGBA::Cyan(),
            ColorRGBA::Yellow(),
            ColorRGBA::White()
        );
        m_menuItems.push_back(SDLMenuItem{ std::move(btn), std::move(desc), action, std::move(onSelect) });
    };

    addMenuItem(155, "1. DEMO ILUMINACION 3D", "SOMBREADO LAMBERTIANO 3D, ROTACION ORBITAL Y MATRICES", "SELECCION DEMO 1", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneLighting>());
    });

    addMenuItem(245, "2. DEMO PARTICULAS 3D", "SISTEMA FISICO EULERIANO, ALPHA BLENDING Y EMISION DINAMICA", "SELECCION DEMO 2", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneParticles>());
    });

    addMenuItem(335, "3. SANDBOX INTERACTIVO", "CANVAS TACTIL CON LAPIZ, MANIPULACION DE CUBO 3D Y HUD", "SELECCION DEMO 3", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneInteractive>());
    });

    addMenuItem(425, "4. RAY TRACING SOFTWARE CPU", "TRAZADO DE RAYOS MULTIHILO SIMETRICO EN CORES 1 Y 2 ESPRESSO", "SELECCION DEMO 4", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneRaytracing>());
    });
}

void SceneMenuSDL::OnEnter() {
    AudioManager::Instance().PlayBGM();
}

void SceneMenuSDL::Update(float dt) {
    m_starfield.Update(dt);

    const auto& touch = InputManager::Instance().GetTouchState();

    if (m_backButton.UpdateAndHandle(touch)) {
        return;
    }

    for (auto& item : m_menuItems) {
        if (item.button.Update(touch, item.actionName)) {
            item.onSelect();
            return;
        }
    }
}

void SceneMenuSDL::Render(SDL_Renderer* renderer) {
    // 1. Fondo Estelar
    m_starfield.Render(renderer, 1280, 720);

    // 2. Botón Volver
    m_backButton.Render(renderer);

    // 3. Título y Subtítulo
    Render2D::drawPixelString(renderer, "DEMOS GRAFICAS SDL", 412, 35, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "PRUEBAS BASADAS EN RASTERIZACION Y ACELERACION SDL2", 330, 90, 2, ColorRGBA::Yellow());

    // 4. Botones y Descripciones
    for (const auto& item : m_menuItems) {
        item.button.Render(renderer);
        const int descY = item.button.GetRect().y + item.button.GetRect().h + 7;
        Render2D::drawPixelString(renderer, item.description, 290, descY, 2, ColorRGBA{170, 210, 240, 255});
    }

    // 5. Pie de página
    Render2D::drawPixelString(renderer, "TOCA UNA DEMO CON EL LAPIZ DEL GAMEPAD O EL RATON", 330, 560, 2, ColorRGBA{180, 180, 180, 255});
}

} // namespace WiiU
