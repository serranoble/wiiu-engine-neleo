#include "Scene/SceneMenuGX2.hpp"
#include "Scene/SceneGX2Native.hpp"
#include "Scene/SceneRaytracingGPU.hpp"
#include "Scene/SceneWater.hpp"
#include "Scene/SceneInteractiveGX2.hpp"
#include "Scene/SceneShadowsGX2.hpp"
#include "Scene/SceneEnvironmentGX2.hpp"
#include "Scene/SceneRaytracingBVHGX2.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"

namespace WiiU {

SceneMenuGX2::SceneMenuGX2()
    : m_starfield(120),
      m_backButton([]() {
          SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
      }) {
    InitMenuItems();
}

void SceneMenuGX2::InitMenuItems() {
    m_menuItems.reserve(7);

    auto addMenuItem = [this](int y, std::string label, std::string desc, const char* action, std::function<void()> onSelect,
                              ColorRGBA normalFill, ColorRGBA hoverFill,
                              ColorRGBA normalBorder, ColorRGBA hoverBorder, ColorRGBA textColor) {
        UIButton btn(280, y, 720, 36, std::move(label), 3);
        btn.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
        m_menuItems.push_back(GX2MenuItem{ std::move(btn), std::move(desc), action, std::move(onSelect) });
    };

    addMenuItem(105, "1. DEMO 100% GX2 NATIVO (SHADERS + Z-BUFFER HW)", 
                "ESCENA MULTI-CUBO DIRECTA EN SILICIO CON Z-BUFFER EN EDRAM (560 GB/S)", 
                "SELECCION DEMO GX2 NATIVO", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneGX2Native>());
    }, ColorRGBA{35, 15, 55, 230}, ColorRGBA{160, 50, 240, 240}, ColorRGBA{200, 80, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(171, "2. DEMO RAY TRACING (GPU GX2 SHADERS)", 
                "TRAZADO DE RAYOS POR HARDWARE EN SHADERS DE FRAGMENTOS CAFEGLSL", 
                "SELECCION DEMO RAYTRACING GPU", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneRaytracingGPU>());
    }, ColorRGBA{10, 45, 35, 230}, ColorRGBA{0, 220, 140, 240}, ColorRGBA::Green(), ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(237, "3. DEMO AGUA 3D (ZELDA BOTW / GERSTNER)", 
                "DEFORMACION HW DE 4 OLAS EN VERTEX SHADER Y SOMBREADO CEL EN LATTE", 
                "SELECCION DEMO AGUA BOTW", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneWater>());
    }, ColorRGBA{10, 35, 65, 230}, ColorRGBA{0, 190, 255, 240}, ColorRGBA{0, 230, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(303, "4. SANDBOX INTERACTIVO (100% GX2 NATIVO)", 
                "CANVAS TACTIL, WIDGET CON INERCIA, ONDAS DE CHOQUE Y CUBO 3D HW", 
                "SELECCION DEMO SANDBOX GX2", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneInteractiveGX2>());
    }, ColorRGBA{45, 20, 20, 230}, ColorRGBA{240, 80, 40, 240}, ColorRGBA{255, 120, 0, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(369, "5. LUZ Y TERRENO 3D (100% GX2 NATIVO)", 
                "LUZ CENTRAL BASICA, EXPANSION RADIAL EN TERRENO 24X24 Y SLIDER", 
                "SELECCION DEMO LUZ GX2", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneShadowsGX2>());
    }, ColorRGBA{20, 35, 60, 230}, ColorRGBA{50, 120, 230, 240}, ColorRGBA{100, 180, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(435, "6. MUNDO TEXTURIZADO Y ENTORNO (100% GX2)", 
                "TEXTURAS EN SILICIO (PASTO/LADRILLO/ROCA), AGUA, NUBES Y GLOW", 
                "SELECCION DEMO ENTORNO GX2", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneEnvironmentGX2>());
    }, ColorRGBA{25, 45, 20, 230}, ColorRGBA{60, 160, 50, 240}, ColorRGBA{120, 255, 100, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(501, "7. RAY TRACING BVH & SOMBRAS CSM (CAP. 11)", 
                "BVH SAH GPU + HEATMAP + PENUMBRA ANALITICA + WIREFRAME 3D (GX2)", 
                "SELECCION DEMO RAYTRACING BVH", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneRaytracingBVHGX2>());
    }, ColorRGBA{15, 45, 65, 230}, ColorRGBA{40, 185, 255, 240}, ColorRGBA{100, 230, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White());
}

void SceneMenuGX2::OnEnter() {
    AudioManager::Instance().PlayBGM();
}

void SceneMenuGX2::Update(float dt) {
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

void SceneMenuGX2::Render(SDL_Renderer* renderer) {
    // 1. Fondo Estelar
    m_starfield.Render(renderer, 1280, 720);

    // 2. Botón Volver
    m_backButton.Render(renderer);

    // 3. Título y Subtítulo
    Render2D::drawPixelString(renderer, "DEMOS GRAFICAS GX2 NATIVAS", 328, 35, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "PIPELINE DE SILICIO, SHADERS CAFEGLSL Y EDRAM EN GPU LATTE", 270, 90, 2, ColorRGBA::Yellow());

    // 4. Botones y Descripciones
    for (const auto& item : m_menuItems) {
        item.button.Render(renderer);
        const int descY = item.button.GetRect().y + item.button.GetRect().h + 7;
        Render2D::drawPixelString(renderer, item.description, 290, descY, 2, ColorRGBA{220, 200, 255, 255});
    }

    // 5. Pie de página
    Render2D::drawPixelString(renderer, "TOCA UNA DEMO CON EL LAPIZ DEL GAMEPAD O EL RATON", 330, 580, 2, ColorRGBA{180, 180, 180, 255});
}

} // namespace WiiU
