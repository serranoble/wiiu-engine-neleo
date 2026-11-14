#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneStaticModelsGX2.hpp"
#include "Scene/SceneSkeletalAnimationGX2.hpp"
#include "Scene/SceneBirdAnimationGX2.hpp"
#include "Scene/SceneShowcaseGX2.hpp"
#include "Scene/SceneCollisionTriggerDemoGX2.hpp"
#include "Scene/SceneAIDemoGX2.hpp"
#include "Scene/SceneInputHIDDemoGX2.hpp"
#include "Scene/SceneRigidBodyPhysicsGX2.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneManager.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"

namespace WiiU {

SceneMenuModels::SceneMenuModels()
    : m_starfield(120),
      m_backButton([]() {
          SceneManager::Instance().ChangeScene(std::make_unique<SceneMenu>());
      }) {
    InitMenuItems();
}

void SceneMenuModels::InitMenuItems() {
    m_menuItems.reserve(8);

    auto addMenuItem = [this](int y, std::string label, std::string desc, const char* action, std::function<void()> onSelect,
                              ColorRGBA normalFill, ColorRGBA hoverFill,
                              ColorRGBA normalBorder, ColorRGBA hoverBorder, ColorRGBA textColor) {
        UIButton btn(280, y, 720, 32, std::move(label), 3);
        btn.SetColors(normalFill, hoverFill, normalBorder, hoverBorder, textColor);
        m_menuItems.push_back(ModelMenuItem{ std::move(btn), std::move(desc), action, std::move(onSelect) });
    };

    addMenuItem(92, "1. MODELOS ESTATICOS (100% GX2 NATIVO)",
                "CASA HOLANDESA (.WMESH) EN EL CENTRO CON FONDO NEGRO Y TEXTURAS HW",
                "SELECCION DEMO MODELOS ESTATICOS", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneStaticModelsGX2>());
    }, ColorRGBA{50, 30, 12, 230}, ColorRGBA{230, 130, 20, 240}, ColorRGBA{255, 180, 40, 255}, ColorRGBA::Yellow(), ColorRGBA::White());

    addMenuItem(158, "2. ANIMACION ESQUELETICA (100% GX2 NATIVO)",
                "MODELO ARTICULADO CON JERARQUIA DE HUESOS Y LINEAR BLEND SKINNING EN LATTE",
                "SELECCION DEMO ANIMACION ESQUELETICA", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneSkeletalAnimationGX2>());
    }, ColorRGBA{15, 35, 55, 230}, ColorRGBA{30, 100, 180, 240}, ColorRGBA{0, 200, 255, 255}, ColorRGBA::Cyan(), ColorRGBA::White());

    addMenuItem(224, "3. PAJARO ANIMADO (BIRD ORANGE FBX)",
                "PAJARO ARTICULADO (21 HUESOS, TEXTURA BASECOLOR Y ANIMACION HOP/CHIRP)",
                "SELECCION DEMO PAJARO ANIMADO", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneBirdAnimationGX2>());
    }, ColorRGBA{45, 25, 10, 230}, ColorRGBA{200, 90, 20, 240}, ColorRGBA{255, 140, 30, 255}, ColorRGBA::Orange(), ColorRGBA::White());

    addMenuItem(290, "4. MATERIALES AVANZADOS (SKYBOX + NORMAL MAP + REFLEXION)",
                "GORDON & CLEVENGER CAP. 9 Y 10: CUBEMAP ATMOSFERICO, DERIVADAS TBN Y FRESNEL",
                "SELECCION DEMO MATERIALES AVANZADOS", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneShowcaseGX2>());
    }, ColorRGBA{45, 15, 55, 230}, ColorRGBA{180, 40, 200, 240}, ColorRGBA{240, 80, 255, 255}, ColorRGBA::Magenta(), ColorRGBA::White());

    addMenuItem(356, "5. COLISIONES Y TRIGGERS ECS (JUEGO INTERACTIVO)",
                "CONTROL 3A PERSONA, SLIDE RESPONSE (AABB/ESFERA), BALIZAS Y ZONA META",
                "SELECCION DEMO COLISIONES Y TRIGGERS", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneCollisionTriggerDemoGX2>());
    }, ColorRGBA{12, 48, 28, 230}, ColorRGBA{25, 165, 85, 240}, ColorRGBA{60, 255, 140, 255}, ColorRGBA::Green(), ColorRGBA::White());

    addMenuItem(422, "6. INTELIGENCIA ARTIFICIAL Y NAVEGACION (CAP. 19)",
                "NAVGRAPH, ALGORITMO A*, STEERING (REYNOLDS), MAQUINAS HFSM Y VISION",
                "SELECCION DEMO IA Y NAVEGACION", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneAIDemoGX2>());
    }, ColorRGBA{20, 35, 55, 230}, ColorRGBA{30, 90, 180, 240}, ColorRGBA{80, 190, 255, 255}, ColorRGBA::Cyan(), ColorRGBA::White());

    addMenuItem(488, "7. CONTROLES Y DISPOSITIVOS HID (CAP. 8)",
                "DIAGNOSTICO MULTI-MANDO, ZONA MUERTA RADIAL, MATRIZ DE BOTONES Y HAPTICOS",
                "SELECCION DEMO CONTROLES HID", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneInputHIDDemoGX2>());
    }, ColorRGBA{38, 20, 52, 230}, ColorRGBA{140, 50, 180, 240}, ColorRGBA{200, 100, 255, 255}, ColorRGBA::Magenta(), ColorRGBA::White());

    addMenuItem(554, "8. DINAMICA RIGIDA Y CONSTRAINTS (CAP. 14)",
                "IMPULSOS SECUENCIALES, FRICCION COULOMB, MUELLES XPBD, SLEEPING Y KCC",
                "SELECCION DEMO DINAMICA RIGIDA", []() {
        SceneManager::Instance().ChangeScene(std::make_unique<SceneRigidBodyPhysicsGX2>());
    }, ColorRGBA{52, 24, 14, 230}, ColorRGBA{195, 75, 25, 240}, ColorRGBA{255, 145, 55, 255}, ColorRGBA::Yellow(), ColorRGBA::White());
}

void SceneMenuModels::OnEnter() {
    AudioManager::Instance().PlayBGM();
}

void SceneMenuModels::Update(float dt) {
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

void SceneMenuModels::Render(SDL_Renderer* renderer) {
    // 1. Fondo Estelar
    m_starfield.Render(renderer, 1280, 720);

    // 2. Botón Volver
    m_backButton.Render(renderer);

    // 3. Título y Subtítulo
    Render2D::drawPixelString(renderer, "DEMOS DE MODELOS 3D", 392, 24, 4, ColorRGBA::Cyan());
    Render2D::drawPixelString(renderer, "PIPELINE BINARIO .WMESH BIG-ENDIAN, CACHES PTVC Y SILICIO GX2", 268, 66, 2, ColorRGBA::Yellow());

    // 4. Botones y Descripciones
    for (const auto& item : m_menuItems) {
        item.button.Render(renderer);
        const int descY = item.button.GetRect().y + item.button.GetRect().h + 4;
        Render2D::drawPixelString(renderer, item.description, 285, descY, 2, ColorRGBA{255, 225, 180, 255});
    }

    // 5. Pie de página
    Render2D::drawPixelString(renderer, "TOCA UNA DEMO CON EL LAPIZ DEL GAMEPAD O EL RATON", 330, 645, 2, ColorRGBA{180, 180, 180, 255});
}

} // namespace WiiU
