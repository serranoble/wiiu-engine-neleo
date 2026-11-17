#include "Scene/SceneMenu.hpp"
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
    m_menuItems.clear();
    m_menuItems.reserve(1);

    auto addMenuItem = [this](int y, std::string label, std::string subtitle, const char* action, std::function<void()> onSelect,
                              ColorRGBA normalFill, ColorRGBA hoverFill,
                              ColorRGBA normalBorder, ColorRGBA hoverBorder, ColorRGBA textColor) {
        UIButton btn(300, y, 680, 60, std::move(label), 3);
        btn.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
        m_menuItems.push_back(MainMenuItem{ std::move(btn), std::move(subtitle), action, std::move(onSelect) });
    };

    // Botón Único: Opciones y Configuración Global del Motor
    addMenuItem(280, "OPCIONES DEL MOTOR",
                "RESOLUCION, FILTRADO, AUDIO, MONITORES Y DEPURACION",
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

    // Entrada física por mando: Botón A o START/PLUS entra directo a Opciones
    if (InputManager::Instance().IsButtonTriggered(VPAD_BUTTON_A) ||
        InputManager::Instance().IsButtonTriggered(WPAD_PRO_BUTTON_A) ||
        InputManager::Instance().IsButtonTriggered(VPAD_BUTTON_PLUS) ||
        InputManager::Instance().IsButtonTriggered(WPAD_PRO_BUTTON_PLUS)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneOptions>());
    }
}

void SceneMenu::Render(SDL_Renderer* renderer) {
    // 1. Fondo Estelar Animado
    m_starfield.Render(renderer, 1280, 720);

    // 2. Título y Subtítulo Centrales
    Render2D::drawPixelString(renderer, "WIIU Demos by Neleo", 412, 100, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "FRAMEWORK Y MOTOR GRAFICO C++20", 446, 160, 2, ColorRGBA::Yellow());
    Render2D::drawPixelString(renderer, "FASE DE ARQUITECTURA & BASE TECNICA LIMPIA", 380, 200, 2, ColorRGBA{160, 210, 255, 255});

    // 3. Botón de Opciones y Subtítulo
    for (const auto& item : m_menuItems) {
        item.button.Render(renderer);
        const int descY = item.button.GetRect().y + item.button.GetRect().h + 12;
        Render2D::drawPixelString(renderer, item.subtitle, 310, descY, 2, ColorRGBA{180, 210, 240, 255});
    }

    // 4. Panel de Estado Informativo en la parte inferior
    Render2D::drawBox(renderer, 240, 460, 800, 130, ColorRGBA{10, 20, 35, 200}, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "ESTADO: ESCENAS ANTERIORES ARCHIVADAS COMO REFERENCIA", 270, 485, 2, ColorRGBA{120, 255, 160, 255});
    Render2D::drawPixelString(renderer, "LISTO PARA REPLANIFICAR Y CONSTRUIR NUEVOS HITOS PASO A PASO", 270, 515, 2, ColorRGBA{200, 225, 255, 255});
    Render2D::drawPixelString(renderer, "PULSA (A) EN GAMEPAD O TOCA LA PANTALLA PARA ENTRAR A OPCIONES", 270, 545, 2, ColorRGBA::Yellow());
}

} // namespace WiiU
