#include "Scene/SceneMenu.hpp"
#include "Scene/SceneMenuSDL.hpp"
#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneOptions.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"

namespace WiiU {

SceneMenu::SceneMenu()
    : m_starfield(120) {
    InitMenuItems();
}

void SceneMenu::InitMenuItems() {
    m_menuItems.reserve(4);

    auto addMenuItem = [this](int y, std::string label, std::string subtitle, const char* action, std::function<void()> onSelect,
                              ColorRGBA normalFill, ColorRGBA hoverFill,
                              ColorRGBA normalBorder, ColorRGBA hoverBorder, ColorRGBA textColor) {
        UIButton btn(300, y, 680, 52, std::move(label), 3);
        btn.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
        m_menuItems.push_back(MainMenuItem{ std::move(btn), std::move(subtitle), action, std::move(onSelect) });
    };

    // 1. Demos SDL
    addMenuItem(140, "1. DEMOS SDL", 
                "PRUEBAS 3D & 2D POR SOFTWARE Y ACELERACION SDL2", 
                "SELECCION CATEGORIA SDL", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuSDL>());
    }, ColorRGBA{15, 30, 55, 230}, ColorRGBA{0, 180, 240, 240}, ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White());

    // 2. Demos GX2
    addMenuItem(235, "2. DEMOS GX2", 
                "SHADERS HARDWARE CAFEGLSL, EDRAM Y SILICIO LATTE", 
                "SELECCION CATEGORIA GX2", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuGX2>());
    }, ColorRGBA{35, 15, 55, 230}, ColorRGBA{160, 50, 240, 240}, ColorRGBA{200, 80, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    // 3. Demos Modelos
    addMenuItem(330, "3. DEMOS MODELOS", 
                "MODELOS 3D COCINADOS (.WMESH), TEXTURAS Y CACHES PTVC", 
                "SELECCION CATEGORIA MODELOS", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
    }, ColorRGBA{50, 30, 12, 230}, ColorRGBA{230, 130, 20, 240}, ColorRGBA{255, 180, 40, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    // 4. Opciones
    addMenuItem(425, "4. OPCIONES", 
                "LIMITE DE FPS, MUSICA Y VOLUMEN GENERAL", 
                "SELECCION CATEGORIA OPCIONES", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneOptions>());
    }, ColorRGBA{15, 45, 30, 230}, ColorRGBA{0, 210, 130, 240}, ColorRGBA::Green(), ColorRGBA::Yellow(), ColorRGBA::White());
}

void SceneMenu::OnEnter() {
    AudioManager::Instance().PlayBGM();
}

void SceneMenu::Update(float dt) {
    m_starfield.Update(dt);

    const auto& touch = InputManager::Instance().GetTouchState();

    for (auto& item : m_menuItems) {
        if (item.button.Update(touch, item.actionName)) {
            item.onSelect();
            return;
        }
    }
}

void SceneMenu::Render(SDL_Renderer* renderer) {
    // 1. Fondo Estelar
    m_starfield.Render(renderer, 1280, 720);

    // 2. Título y Subtítulo
    Render2D::drawPixelString(renderer, "WIIU Demos by Neleo", 412, 40, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "SELECCIONA UNA CATEGORIA DEL MOTOR", 436, 100, 2, ColorRGBA::Yellow());

    // 3. Botones Principales y Subtítulos Explicativos
    for (const auto& item : m_menuItems) {
        item.button.Render(renderer);
        const int descY = item.button.GetRect().y + item.button.GetRect().h + 8;
        Render2D::drawPixelString(renderer, item.subtitle, 310, descY, 2, ColorRGBA{180, 210, 240, 255});
    }

    // 4. Pie de página
    Render2D::drawPixelString(renderer, "TOCA UNA OPCION CON EL LAPIZ DEL GAMEPAD O EL RATON", 328, 555, 2, ColorRGBA{180, 180, 180, 255});
}

} // namespace WiiU
