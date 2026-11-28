#include "Scene/SceneMenu.hpp"
#include "Scene/SceneOptions.hpp"
#include "Scene/NetTestModal.hpp"
#include "Scene/GraphicsQAModal.hpp"
#include "Scene/SceneModelHouse.hpp"
#include "Scene/SceneModelBird.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"

namespace Neleo {

SceneMenu::SceneMenu()
    : m_starfield(120) {
    InitMenuItems();
}

void SceneMenu::InitMenuItems() {
    m_menuItems.clear();
    m_menuItems.reserve(5);

    auto addMenuItem = [this](int y, std::string_view label, std::string subtitle, const char* action, std::function<void()> onSelect,
                              ColorRGBA normalFill, ColorRGBA hoverFill,
                              ColorRGBA normalBorder, ColorRGBA hoverBorder, ColorRGBA textColor) {
        UIButton btn(290, y, 700, 38, label, 2);
        btn.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
        m_menuItems.push_back(MainMenuItem{ std::move(btn), std::move(subtitle), action, std::move(onSelect) });
    };

    // Botón 1: Laboratorio de Calidad Gráfica (Suites G1 a G8 — CGP + RTR4)
    addMenuItem(176, NELEO_LOC("menu.item.qa.label"),
                std::string(NELEO_LOC("menu.item.qa.desc")),
                "ABRIR MODAL LABORATORIO GRAFICO", []() {
        SceneManager::Instance().GetModalStack().Push(std::make_unique<GraphicsQAModal>());
    }, ColorRGBA{38, 22, 62, 230}, ColorRGBA{155, 70, 230, 240}, ColorRGBA{195, 120, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    // Botón 2: Validación Cooker — Modelo Estático (Casa)
    addMenuItem(244, NELEO_LOC("menu.item.house.label"),
                std::string(NELEO_LOC("menu.item.house.desc")),
                "CARGAR CASA ESTATICA", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneModelHouse>());
    }, ColorRGBA{48, 32, 16, 230}, ColorRGBA{215, 140, 45, 240}, ColorRGBA{255, 185, 80, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    // Botón 3: Validación Cooker — Modelo Animado (Pájaro Skinned)
    addMenuItem(312, NELEO_LOC("menu.item.bird.label"),
                std::string(NELEO_LOC("menu.item.bird.desc")),
                "CARGAR PAJARO ANIMADO", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneModelBird>());
    }, ColorRGBA{48, 18, 36, 230}, ColorRGBA{210, 65, 140, 240}, ColorRGBA{255, 110, 180, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    // Botón 4: Opciones y Configuración Global del Motor
    addMenuItem(380, NELEO_LOC("menu.item.options.label"),
                std::string(NELEO_LOC("menu.item.options.desc")),
                "SELECCION CATEGORIA OPCIONES", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneOptions>());
    }, ColorRGBA{15, 45, 30, 230}, ColorRGBA{0, 210, 130, 240}, ColorRGBA::Green(), ColorRGBA::Yellow(), ColorRGBA::White());

    // Botón 5: Modal de Prueba de Red (UDP + TCP) y Teclado Nativo Wii U
    addMenuItem(448, NELEO_LOC("menu.item.net.label"),
                std::string(NELEO_LOC("menu.item.net.desc")),
                "ABRIR MODAL PRUEBA RED", []() {
        SceneManager::Instance().GetModalStack().Push(std::make_unique<NetTestModal>());
    }, ColorRGBA{15, 35, 58, 230}, ColorRGBA{0, 175, 230, 240}, ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White());
}

void SceneMenu::OnEnter() {
    InitMenuItems();
    AudioManager::Instance().PlayBGM();
}

void SceneMenu::Update(float dt) {
    m_starfield.Update(dt);

    const auto& touch = InputManager::Instance().GetTouchState();

    for (size_t i = 0; i < m_menuItems.size(); ++i) {
        if (m_menuItems[i].button.Update(touch, m_menuItems[i].actionName)) {
            m_focusedIndex = i;
            m_menuItems[i].onSelect();
            return;
        }
    }

    // Navegación física multi-mando (D-Pad Arriba/Abajo + A/Plus)
    bool navUp = false;
    bool navDown = false;
    bool confirm = false;
    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadUp)) navUp = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown)) navDown = true;
        if (pad.WasJustPressed(VirtualButton::A) || pad.WasJustPressed(VirtualButton::Plus)) confirm = true;
    }

    if (!m_menuItems.empty()) {
        if (navUp) {
            AudioManager::Instance().PlayClick();
            m_focusedIndex = (m_focusedIndex + m_menuItems.size() - 1u) % m_menuItems.size();
        } else if (navDown) {
            AudioManager::Instance().PlayClick();
            m_focusedIndex = (m_focusedIndex + 1u) % m_menuItems.size();
        } else if (confirm) {
            AudioManager::Instance().PlayClick();
            m_menuItems[m_focusedIndex].onSelect();
        }
    }
}

void SceneMenu::Render() {
    Render2D::Begin();

    // 1. Fondo Estelar Animado
    m_starfield.Render(1280, 720);

    // 2. Título y Subtítulo Centrales
    Render2D::DrawPixelString("WIIU Demos by Neleo", 412, 56, 4, ColorRGBA::Cyan());
    Render2D::DrawPixelString(NELEO_LOC("menu.subtitle1"), 446, 106, 2, ColorRGBA::Yellow());
    Render2D::DrawPixelString(NELEO_LOC("menu.subtitle2"), 352, 134, 2, ColorRGBA{160, 210, 255, 255});

    // 3. Botones del Menú y Subtítulos
    for (size_t i = 0; i < m_menuItems.size(); ++i) {
        const auto& item = m_menuItems[i];
        item.button.Render();
        if (i == m_focusedIndex) {
            const auto& r = item.button.GetRect();
            Render2D::DrawBox(r.x - 3, r.y - 3, r.w + 6, r.h + 6, ColorRGBA{0, 0, 0, 0}, ColorRGBA::Yellow());
        }
        const int descY = item.button.GetRect().y + item.button.GetRect().h + 6;
        const int subX = 640 - static_cast<int>(item.subtitle.size()) * 6;
        Render2D::DrawPixelString(item.subtitle, subX, descY, 2, ColorRGBA{180, 210, 240, 255});
    }

    // 4. Panel de Estado Informativo en la parte inferior
    Render2D::DrawBox(240, 524, 800, 100, ColorRGBA{10, 20, 35, 200}, ColorRGBA::Cyan());
    Render2D::DrawPixelString(NELEO_LOC("menu.footer.status"), 250, 538, 2, ColorRGBA{120, 255, 160, 255});
    Render2D::DrawPixelString(NELEO_LOC("menu.footer.controls"), 275, 566, 2, ColorRGBA::Yellow());
    Render2D::DrawPixelString(NELEO_LOC("menu.footer.summary"), 275, 594, 2, ColorRGBA{200, 225, 255, 255});

    Render2D::End();
}

} // namespace Neleo
