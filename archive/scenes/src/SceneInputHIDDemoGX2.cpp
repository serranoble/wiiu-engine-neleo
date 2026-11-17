#include "Scene/SceneInputHIDDemoGX2.hpp"
#include "Scene/SceneMenuModels.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/AssetManager.hpp"
#include "Core/StackString.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Input/InputManager.hpp"
#include "Input/RumbleManager.hpp"
#include "Input/ActionMapping.hpp"
#include "Audio/AudioManager.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Graphics/Render2DGX2.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

#if defined(__WIIU__) || defined(__wiiu__)
#include <vpad/input.h>
#include <padscore/wpad.h>
#include <padscore/kpad.h>
#include <coreinit/core.h>
#endif

namespace WiiU {

namespace {

[[nodiscard]] const char* GetControllerTypeName(ControllerType type) noexcept {
    switch (type) {
        case ControllerType::GamePadDRC:        return "GamePad DRC (VPAD)";
        case ControllerType::ProController:     return "Wii U Pro Controller";
        case ControllerType::WiiRemote:         return "Wii Remote (Solo)";
        case ControllerType::WiiRemoteNunchuk:  return "Wii Remote + Nunchuk";
        case ControllerType::ClassicController: return "Classic Controller / Pro";
        case ControllerType::KeyboardMouse:     return "PC Teclado & Raton (SDL2)";
        case ControllerType::GenericSDL:        return "Mando Generico PC (SDL2)";
        default:                                return "Desconectado";
    }
}

[[nodiscard]] PlayerIndex SlotToPlayerIndex(size_t slot) noexcept {
    switch (slot) {
        case 0:  return PlayerIndex::Player1;
        case 1:  return PlayerIndex::Player2;
        case 2:  return PlayerIndex::Player3;
        case 3:  return PlayerIndex::Player4;
        default: return PlayerIndex::Player1;
    }
}

// =============================================================================
// RECTÁNGULOS ESTÁTICOS DE INTERFAZ DE USUARIO (0 ALOCACIONES EN EL BUCLE)
// =============================================================================
static const UI::UIRect s_topBarPanel{ 15.0f, 10.0f, 1250.0f, 44.0f };
static const UI::UIRect s_btnVolverTop{ 25.0f, 16.0f, 100.0f, 32.0f };

// Pestañas de Ranuras de Jugador
static const UI::UIRect s_tabPlayer[4] = {
    UI::UIRect{ 740.0f, 16.0f, 120.0f, 32.0f },
    UI::UIRect{ 868.0f, 16.0f, 120.0f, 32.0f },
    UI::UIRect{ 996.0f, 16.0f, 120.0f, 32.0f },
    UI::UIRect{ 1124.0f, 16.0f, 120.0f, 32.0f }
};

// Panel Izquierdo: Diagnóstico Hardware & Sticks
static const UI::UIRect s_panelLeft{ 15.0f, 62.0f, 380.0f, 575.0f };

// Panel Derecho: Matriz de Botones y Hápticos
static const UI::UIRect s_panelRight{ 885.0f, 62.0f, 380.0f, 575.0f };

// Panel Central-Inferior: Action Mapping
static const UI::UIRect s_panelCenterBottom{ 405.0f, 545.0f, 470.0f, 92.0f };

// Barra Guía Inferior
static const UI::UIRect s_panelBottomBar{ 15.0f, 645.0f, 1250.0f, 65.0f };

// Botones de la Suite de Hápticos
static const UI::UIRect s_btnPresetTick{ 900.0f, 350.0f, 350.0f, 26.0f };
static const UI::UIRect s_btnPresetJump{ 900.0f, 382.0f, 350.0f, 26.0f };
static const UI::UIRect s_btnPresetDamage{ 900.0f, 414.0f, 350.0f, 26.0f };
static const UI::UIRect s_btnPresetExplosion{ 900.0f, 446.0f, 350.0f, 26.0f };
static const UI::UIRect s_btnStopAllRumble{ 900.0f, 478.0f, 350.0f, 26.0f };

// Botones de Configuración en Caliente
static const UI::UIRect s_btnToggleRumble{ 900.0f, 538.0f, 350.0f, 28.0f };
static const UI::UIRect s_btnCycleDeadzone{ 900.0f, 574.0f, 350.0f, 28.0f };

} // namespace

SceneInputHIDDemoGX2::SceneInputHIDDemoGX2()
    : GameScene3D() {
}

SceneInputHIDDemoGX2::~SceneInputHIDDemoGX2() {
    OnGameExit();
}

void SceneInputHIDDemoGX2::LoadAsync() {
    AssetManager::LoadBinaryFile("models/skydome.wmesh", m_stagedSkydomeMesh);
    AssetManager::LoadBinaryFile("models/ground_plane.wmesh", m_stagedGroundMesh);
    AssetManager::LoadBinaryFile("models/beacon.wmesh", m_stagedActorMesh);
    AssetManager::LoadBinaryFile("models/pillar.wmesh", m_stagedPillarMesh);
    AssetManager::LoadBinaryFile("models/skydome.wtex", m_stagedSkydomeTex);
    AssetManager::LoadBinaryFile("models/ground_grass.wtex", m_stagedGrassTex);
}

bool SceneInputHIDDemoGX2::CreateTextures() {
    if (m_texturesCreated) return true;

    if (!m_stagedSkydomeTex.empty()) {
        m_hSkydomeTex = LoadSceneTextureFromMemory(m_stagedSkydomeTex.data(), m_stagedSkydomeTex.size(), "HIDDemo_SkydomeTex");
        m_stagedSkydomeTex.clear();
        m_stagedSkydomeTex.shrink_to_fit();
    } else {
        m_hSkydomeTex = LoadSceneTexture("models/skydome.wtex", "HIDDemo_SkydomeTex");
    }

    if (!m_stagedGrassTex.empty()) {
        m_hGrassTex = LoadSceneTextureFromMemory(m_stagedGrassTex.data(), m_stagedGrassTex.size(), "HIDDemo_GrassTex");
        m_stagedGrassTex.clear();
        m_stagedGrassTex.shrink_to_fit();
    } else {
        m_hGrassTex = LoadSceneTexture("models/ground_grass.wtex", "HIDDemo_GrassTex");
    }

    m_hWhiteTex = CreateSceneSolidTexture(0xFFFFFFFFu, "HIDDemo_WhiteTex");
    m_texturesCreated = true;
    return true;
}

void SceneInputHIDDemoGX2::OnGameInit() {
    AudioManager::Instance().PlayBGM();

    auto loadMeshHelper = [](StaticMesh& mesh, std::vector<uint8_t>& staged, const char* path) {
        if (!staged.empty()) {
            mesh.LoadFromMemory(staged.data(), staged.size());
            staged.clear();
            staged.shrink_to_fit();
        } else {
            mesh.LoadFromFile(path);
        }
    };

    loadMeshHelper(m_skydomeMesh, m_stagedSkydomeMesh, "models/skydome.wmesh");
    loadMeshHelper(m_groundMesh, m_stagedGroundMesh, "models/ground_plane.wmesh");
    loadMeshHelper(m_actorMesh, m_stagedActorMesh, "models/beacon.wmesh");
    loadMeshHelper(m_pillarMesh, m_stagedPillarMesh, "models/pillar.wmesh");

    CreateTextures();

    // 1. Entidad Skydome
    m_skydomeEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_skydomeEntity, Point3D{0, 0, 0}, Point3D{0, 0, 0}, Point3D{25, 25, 25});

    // 2. Entidad Terreno Suelo
    m_groundEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_groundEntity, Point3D{0, -0.05f, 0}, Point3D{0, 0, 0}, Point3D{1.4f, 1.0f, 1.4f});

    // 3. Entidad Pedestal Central
    m_pillarEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_pillarEntity, Point3D{0.0f, -0.4f, 0.0f}, Point3D{0, 0, 0}, Point3D{1.0f, 0.5f, 1.0f});

    // 4. Entidad Actor Interactivo
    m_actorEntity = m_ecsWorld.CreateEntity();
    m_ecsWorld.EmplaceComponent<TransformComponent>(m_actorEntity, m_actorPosition, Point3D{0, 0, 0}, Point3D{0.7f, 0.7f, 0.7f});

    // 5. Configuración de Cámara Orbital
    m_orbitCamera.SetTarget(Point3D{0.0f, 0.8f, 0.0f});
    m_orbitCamera.SetAngles(0.0f, 30.0f);
    m_orbitCamera.SetDistance(12.0f);

    m_selectedPlayerIndex = 0;
}

void SceneInputHIDDemoGX2::OnGameExit() {
    // 1. Limpieza determinista de entidades ECS y mallas en VRAM
    m_ecsWorld.Clear();
    m_skydomeMesh.Destroy();
    m_groundMesh.Destroy();
    m_actorMesh.Destroy();
    m_pillarMesh.Destroy();

    m_stagedSkydomeMesh.clear();
    m_stagedGroundMesh.clear();
    m_stagedActorMesh.clear();
    m_stagedPillarMesh.clear();
    m_stagedSkydomeTex.clear();
    m_stagedGrassTex.clear();

    m_hSkydomeTex = {};
    m_hGrassTex = {};
    m_hWhiteTex = {};
    m_texturesCreated = false;

    // 2. Detención de seguridad de actuadores hápticos
    RumbleManager::Instance().StopAll();
}

void SceneInputHIDDemoGX2::OnGameUpdate(float dt) {
    auto& input = InputManager::Instance();
    auto& rumble = RumbleManager::Instance();
    auto& config = EngineConfig::Get();

    // Actualizar indicador de vibración
    if (rumble.IsRumbling(SlotToPlayerIndex(m_selectedPlayerIndex))) {
        m_rumbleIndicatorTimer += dt * 15.0f;
    } else {
        m_rumbleIndicatorTimer = 0.0f;
    }

    const auto& touch = input.GetTouchState();
    const auto& currentPad = input.GetGamepad(m_selectedPlayerIndex);

    // 1. Salida de la escena: Toque en "< VOLVER" o Combo seguro [PLUS + MINUS]
    // [DESACOPLADO DE BOTÓN B]: Permite probar el botón B de forma independiente en la matriz
    if (touch.justPressed && s_btnVolverTop.Contains(touch.x, touch.y)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
        return;
    }

    if (currentPad.IsHeld(VirtualButton::Plus) && currentPad.IsHeld(VirtualButton::Minus)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().ChangeScene(std::make_unique<SceneMenuModels>());
        return;
    }

    // 2. Selección de Ranura de Jugador exclusivamente por Toque en Pestaña
    // [DESACOPLADO DE BUMPERS L / R]: Permite probar L y R de forma independiente
    if (touch.justPressed) {
        for (size_t i = 0; i < 4; ++i) {
            if (s_tabPlayer[i].Contains(touch.x, touch.y)) {
                m_selectedPlayerIndex = i;
                AudioManager::Instance().PlayClick();
                break;
            }
        }
    }

    PlayerIndex rPlayer = SlotToPlayerIndex(m_selectedPlayerIndex);

    // 3. Suite de Disparo de Presets Hápticos exclusivamente por Toque
    // [DESACOPLADO DE BOTONES X / Y / ZR]: No interfieren con el testeo de botones
    if (touch.justPressed) {
        if (s_btnPresetTick.Contains(touch.x, touch.y)) {
            rumble.PlayPreset(rPlayer, RumblePreset::SubtleTick);
            m_lastRumblePreset = "SUBTLE TICK";
            AudioManager::Instance().PlayClick();
        } else if (s_btnPresetJump.Contains(touch.x, touch.y)) {
            rumble.PlayPreset(rPlayer, RumblePreset::JumpLand);
            m_lastRumblePreset = "JUMP LAND";
            AudioManager::Instance().PlayClick();
        } else if (s_btnPresetDamage.Contains(touch.x, touch.y)) {
            rumble.PlayPreset(rPlayer, RumblePreset::DamageImpact);
            m_lastRumblePreset = "DAMAGE IMPACT";
            AudioManager::Instance().PlayClick();
        } else if (s_btnPresetExplosion.Contains(touch.x, touch.y)) {
            rumble.PlayPreset(rPlayer, RumblePreset::HeavyExplosion);
            m_lastRumblePreset = "HEAVY EXPLOSION";
            AudioManager::Instance().PlayClick();
        } else if (s_btnStopAllRumble.Contains(touch.x, touch.y)) {
            rumble.StopAll();
            m_lastRumblePreset = "DETENIDO";
            AudioManager::Instance().PlayClick();
        } else if (s_btnToggleRumble.Contains(touch.x, touch.y)) {
            config.controls.rumbleEnabled = !config.controls.rumbleEnabled;
            AudioManager::Instance().PlayClick();
        } else if (s_btnCycleDeadzone.Contains(touch.x, touch.y)) {
            // Ciclo de zona muerta: 0.10 -> 0.18 -> 0.25 -> 0.35 -> 0.10
            float cur = config.controls.deadzoneRadius;
            if (cur < 0.14f) config.controls.deadzoneRadius = 0.18f;
            else if (cur < 0.21f) config.controls.deadzoneRadius = 0.25f;
            else if (cur < 0.30f) config.controls.deadzoneRadius = 0.35f;
            else config.controls.deadzoneRadius = 0.10f;
            AudioManager::Instance().PlayClick();
        }
    }

    // 4. Control Continuo del Actor 3D mediante Stick Izquierdo
    // [DESACOPLADO DE BOTÓN A Y D-PAD]: No hay saltos ni movimiento por cruceta indeseados
    float moveX = currentPad.leftStick.x;
    float moveZ = -currentPad.leftStick.y; // Hacia arriba en stick = avanzar hacia el fondo (-Z)

    float moveSpeed = 5.0f;
    m_actorPosition.x += moveX * moveSpeed * dt;
    m_actorPosition.z += moveZ * moveSpeed * dt;

    // Confinar en el área visible del suelo
    m_actorPosition.x = std::clamp(m_actorPosition.x, -6.0f, 6.0f);
    m_actorPosition.z = std::clamp(m_actorPosition.z, -6.0f, 6.0f);

    // Orientación del actor en base al avance del stick
    if (std::abs(moveX) > 0.05f || std::abs(moveZ) > 0.05f) {
        m_actorYaw = std::atan2(moveX, -moveZ) * (180.0f / 3.14159265f);
    }

    // Actualizar TransformComponent del actor
    if (auto* aTrans = m_ecsWorld.GetComponent<TransformComponent>(m_actorEntity)) {
        aTrans->position = m_actorPosition;
        aTrans->rotation = Point3D{0.0f, m_actorYaw, 0.0f};
    }

    // 5. Control de la Cámara Orbital con Stick Derecho
    // [DESACOPLADO DE GATILLOS ZL / ZR]: ZL y ZR quedan libres para probarse en el HUD
    float rotSpeed = 75.0f * dt;
    if (std::abs(currentPad.rightStick.x) > 0.05f) {
        m_orbitCamera.SetAngles(m_orbitCamera.GetYaw() - currentPad.rightStick.x * rotSpeed, m_orbitCamera.GetPitch());
    }
    if (std::abs(currentPad.rightStick.y) > 0.05f) {
        float newPitch = std::clamp(m_orbitCamera.GetPitch() - currentPad.rightStick.y * rotSpeed, 10.0f, 80.0f);
        m_orbitCamera.SetAngles(m_orbitCamera.GetYaw(), newPitch);
    }

    m_orbitCamera.Update(m_ecsWorld, dt);
    TransformSystem::Update(m_ecsWorld);
}

void SceneInputHIDDemoGX2::OnGameRender3D(ForwardRendererGX2& renderer) {
    GX2Texture* skyTex   = GetSceneTexture(m_hSkydomeTex);
    GX2Texture* grassTex = GetSceneTexture(m_hGrassTex);
    GX2Texture* whiteTex = GetSceneTexture(m_hWhiteTex);

    // 1. Skydome
    renderer.RenderSkydome(m_skydomeMesh, skyTex);

    // 2. Terreno
    if (const auto* gTrans = m_ecsWorld.GetComponent<TransformComponent>(m_groundEntity)) {
        renderer.RenderStaticMesh(m_groundMesh, gTrans->worldMatrix, grassTex, nullptr, 0.0f, true, 0.8f, 0.42f);
    }

    // 3. Pedestal Central
    if (const auto* pTrans = m_ecsWorld.GetComponent<TransformComponent>(m_pillarEntity)) {
        renderer.RenderStaticMesh(m_pillarMesh, pTrans->worldMatrix, whiteTex, nullptr, 0.1f, true, 1.2f, 0.40f);
    }

    // 4. Actor 3D Interactivo
    if (const auto* aTrans = m_ecsWorld.GetComponent<TransformComponent>(m_actorEntity)) {
        renderer.RenderStaticMesh(m_actorMesh, aTrans->worldMatrix, whiteTex, nullptr, 0.35f, true, 1.8f, 0.65f);
    }
}

void SceneInputHIDDemoGX2::OnGameRenderDebug3D() {
    // Vector de orientación y posición de contacto del actor 3D
    Point3D topPt = m_actorPosition;
    topPt.y += 1.2f;
    DebugDraw3D::DrawLine(m_actorPosition, topPt, 2.0f, ColorRGBA::Yellow());

    // Círculo de sombra/base sobre el suelo (yOffset = 0.0f relativo a groundPt.y, 24 segmentos)
    Point3D groundPt = m_actorPosition;
    groundPt.y = 0.02f;
    DebugDraw3D::DrawCircleXZ(groundPt, 0.5f, 0.0f, 2.0f, ColorRGBA{0, 220, 255, 180}, 24);
}

void SceneInputHIDDemoGX2::OnGameRenderHUD() {
    auto& input = InputManager::Instance();
    auto& rumble = RumbleManager::Instance();
    const auto& config = EngineConfig::Get();
    auto& render2D = Render2DGX2::Instance();

    const auto& pad = input.GetGamepad(m_selectedPlayerIndex);
    PlayerIndex rPlayer = SlotToPlayerIndex(m_selectedPlayerIndex);

    // =========================================================================
    // 1. BARRA SUPERIOR Y PESTAÑAS DE JUGADOR
    // =========================================================================
    UI::UIToolkit::DrawPanel(s_topBarPanel);
    UI::UIToolkit::DrawButton(s_btnVolverTop, "< VOLVER", false, UI::TextScale::Normal);
    UI::UIToolkit::DrawLabel(140.0f, 24.0f, "TEST DE DISPOSITIVOS DE ENTRADA Y HAPTICOS (CAP. 8)", UI::TextScale::Normal, ColorRGBA::Cyan());

    for (size_t i = 0; i < 4; ++i) {
        bool isSelected = (i == m_selectedPlayerIndex);
        const auto& pPad = input.GetGamepad(i);
        StackString<32> tabLabel;
        if (i == 0) {
            tabLabel.Format("DRC/P1%s", pPad.isConnected ? "*" : "");
        } else {
            tabLabel.Format("P%zu%s", i + 1, pPad.isConnected ? "*" : "");
        }

        ColorRGBA fill = isSelected ? ColorRGBA{30, 110, 70, 240} : ColorRGBA{20, 32, 52, 235};
        ColorRGBA border = isSelected ? ColorRGBA{100, 255, 150, 255} : ColorRGBA{0, 180, 220, 200};
        UI::UIToolkit::DrawButton(s_tabPlayer[i], tabLabel.c_str(), false, UI::TextScale::Normal, &fill, &border);
    }

    // =========================================================================
    // 2. PANEL IZQUIERDO: DIAGNÓSTICO DE HARDWARE Y STICKS CON ZONA MUERTA RADIAL
    // =========================================================================
    UI::UIToolkit::DrawPanel(s_panelLeft);
    UI::UIToolkit::DrawLabel(30.0f, 74.0f, "HARDWARE & STICKS ANALOGICOS", UI::TextScale::Normal, ColorRGBA::Yellow());

    // Fila 1: Ranura y Tipo detectado
    StackString<64> strSlot;
    strSlot.Format("RANURA: JUGADOR %zu", m_selectedPlayerIndex + 1);
    UI::UIToolkit::DrawLabel(30.0f, 100.0f, strSlot.c_str(), UI::TextScale::Small, ColorRGBA::White());

    StackString<64> strType;
    strType.Format("TIPO: %s", GetControllerTypeName(pad.type));
    UI::UIToolkit::DrawLabel(30.0f, 116.0f, strType.c_str(), UI::TextScale::Small, ColorRGBA{0, 220, 255, 255});

    // Fila 2: Estado de Conexión y Batería
    ColorRGBA connColor = pad.isConnected ? ColorRGBA{50, 255, 100, 255} : ColorRGBA{160, 160, 160, 255};
    UI::UIToolkit::DrawLabel(30.0f, 132.0f, pad.isConnected ? "ESTADO: CONECTADO (ONLINE)" : "ESTADO: DESCONECTADO (OFFLINE)", UI::TextScale::Small, connColor);

    StackString<32> strBattery;
    strBattery.Format("BATERIA: %u%%", pad.batteryLevel);
    UI::UIToolkit::DrawProgressBar(UI::UIRect{ 30.0f, 148.0f, 350.0f, 16.0f }, pad.batteryLevel / 100.0f, strBattery.c_str(), ColorRGBA{60, 200, 120, 255});

    UI::UIToolkit::DrawSeparator(30.0f, 172.0f, 350.0f);

    // Subtítulo Zona Muerta Radial
    UI::UIToolkit::DrawLabel(30.0f, 180.0f, "ZONA MUERTA RADIAL CONTINUA (CAP. 8.2)", UI::TextScale::Small, ColorRGBA::Cyan());

    // -------------------------------------------------------------------------
    // WIDGET STICK IZQUIERDO (Centro cx = 115, cy = 250, Radio = 50)
    // -------------------------------------------------------------------------
    const float lcx = 115.0f;
    const float lcy = 250.0f;
    const float stickR = 50.0f;
    const float deadzoneR = stickR * config.controls.deadzoneRadius;

    // Círculo exterior (rango total)
    render2D.DrawCircle(lcx, lcy, stickR, ColorRGBA{16, 24, 40, 220}, ColorRGBA{0, 180, 240, 240}, 24);
    // Círculo interior (umbral de zona muerta)
    render2D.DrawCircle(lcx, lcy, deadzoneR, ColorRGBA{45, 25, 20, 200}, ColorRGBA{255, 120, 0, 220}, 20);
    // Cruz de ejes
    render2D.DrawLine(lcx - stickR, lcy, lcx + stickR, lcy, 1.0f, ColorRGBA{80, 110, 150, 160});
    render2D.DrawLine(lcx, lcy - stickR, lcx, lcy + stickR, 1.0f, ColorRGBA{80, 110, 150, 160});

    // Posición del Stick Izquierdo
    float lxPos = lcx + pad.leftStick.x * stickR;
    float lyPos = lcy - pad.leftStick.y * stickR; // Invertir Y para pantalla
    render2D.DrawLine(lcx, lcy, lxPos, lyPos, 2.0f, ColorRGBA::Yellow());
    render2D.DrawCircle(lxPos, lyPos, 6.0f, ColorRGBA::Yellow(), ColorRGBA::White(), 12);

    UI::UIToolkit::DrawLabel(65.0f, 308.0f, "STICK IZQUIERDO", UI::TextScale::Small, ColorRGBA::Yellow());

    // -------------------------------------------------------------------------
    // WIDGET STICK DERECHO (Centro cx = 295, cy = 250, Radio = 50)
    // -------------------------------------------------------------------------
    const float rcx = 295.0f;
    const float rcy = 250.0f;

    render2D.DrawCircle(rcx, rcy, stickR, ColorRGBA{16, 24, 40, 220}, ColorRGBA{0, 180, 240, 240}, 24);
    render2D.DrawCircle(rcx, rcy, deadzoneR, ColorRGBA{45, 25, 20, 200}, ColorRGBA{255, 120, 0, 220}, 20);
    render2D.DrawLine(rcx - stickR, rcy, rcx + stickR, rcy, 1.0f, ColorRGBA{80, 110, 150, 160});
    render2D.DrawLine(rcx, rcy - stickR, rcx, rcy + stickR, 1.0f, ColorRGBA{80, 110, 150, 160});

    float rxPos = rcx + pad.rightStick.x * stickR;
    float ryPos = rcy - pad.rightStick.y * stickR;
    render2D.DrawLine(rcx, rcy, rxPos, ryPos, 2.0f, ColorRGBA::Cyan());
    render2D.DrawCircle(rxPos, ryPos, 6.0f, ColorRGBA::Cyan(), ColorRGBA::White(), 12);

    UI::UIToolkit::DrawLabel(245.0f, 308.0f, "STICK DERECHO", UI::TextScale::Small, ColorRGBA::Cyan());

    // Telemetría Numérica de Sticks
    StackString<64> strLStick;
    strLStick.Format("LX:%+.2f LY:%+.2f MAG:%.2f %s", pad.leftStick.x, pad.leftStick.y, pad.leftStick.magnitude,
                     (pad.leftStick.magnitude > 0.001f) ? "[ACT]" : "[MUERTO]");
    UI::UIToolkit::DrawLabel(30.0f, 330.0f, strLStick.c_str(), UI::TextScale::Small,
                             (pad.leftStick.magnitude > 0.001f) ? ColorRGBA{100, 255, 140, 255} : ColorRGBA{160, 160, 160, 255});

    StackString<64> strRStick;
    strRStick.Format("RX:%+.2f RY:%+.2f MAG:%.2f %s", pad.rightStick.x, pad.rightStick.y, pad.rightStick.magnitude,
                     (pad.rightStick.magnitude > 0.001f) ? "[ACT]" : "[MUERTO]");
    UI::UIToolkit::DrawLabel(30.0f, 346.0f, strRStick.c_str(), UI::TextScale::Small,
                             (pad.rightStick.magnitude > 0.001f) ? ColorRGBA{100, 255, 140, 255} : ColorRGBA{160, 160, 160, 255});

    UI::UIToolkit::DrawSeparator(30.0f, 368.0f, 350.0f);

    // Gatillos Analógicos
    UI::UIToolkit::DrawLabel(30.0f, 376.0f, "GATILLOS ANALOGICOS", UI::TextScale::Small, ColorRGBA::Cyan());

    StackString<32> strTrigL;
    strTrigL.Format("GATILLO L: %.0f%%", pad.triggers.left * 100.0f);
    UI::UIToolkit::DrawProgressBar(UI::UIRect{ 30.0f, 394.0f, 350.0f, 16.0f }, pad.triggers.left, strTrigL.c_str(), ColorRGBA::Orange());

    StackString<32> strTrigR;
    strTrigR.Format("GATILLO R: %.0f%%", pad.triggers.right * 100.0f);
    UI::UIToolkit::DrawProgressBar(UI::UIRect{ 30.0f, 416.0f, 350.0f, 16.0f }, pad.triggers.right, strTrigR.c_str(), ColorRGBA::Orange());

    UI::UIToolkit::DrawSeparator(30.0f, 442.0f, 350.0f);

    // Pantalla Táctil y Sensores Inerciales (IMU)
    UI::UIToolkit::DrawLabel(30.0f, 450.0f, "TOUCH & SENSORES INERCIALES", UI::TextScale::Small, ColorRGBA::Cyan());

    StackString<64> strTouch;
    strTouch.Format("TOUCH: %s (X:%.0f, Y:%.0f)", pad.touch.active ? "ACTIVO" : "INACTIVO", pad.touch.x, pad.touch.y);
    UI::UIToolkit::DrawLabel(30.0f, 468.0f, strTouch.c_str(), UI::TextScale::Small,
                             pad.touch.active ? ColorRGBA{80, 255, 150, 255} : ColorRGBA{160, 160, 160, 255});

    StackString<64> strIMU1;
    strIMU1.Format("ACC: X:%+.2f Y:%+.2f Z:%+.2f", pad.motion.accX, pad.motion.accY, pad.motion.accZ);
    UI::UIToolkit::DrawLabel(30.0f, 486.0f, strIMU1.c_str(), UI::TextScale::Small, ColorRGBA{180, 210, 240, 255});

    StackString<64> strIMU2;
    strIMU2.Format("GIRO: P:%+.1f R:%+.1f Y:%+.1f", pad.motion.pitch, pad.motion.roll, pad.motion.yaw);
    UI::UIToolkit::DrawLabel(30.0f, 504.0f, strIMU2.c_str(), UI::TextScale::Small, ColorRGBA{180, 210, 240, 255});

    // =========================================================================
    // 3. PANEL DERECHO: MATRIZ DE BOTONES DIGITALES & SUITE DE HÁPTICOS
    // =========================================================================
    UI::UIToolkit::DrawPanel(s_panelRight);
    UI::UIToolkit::DrawLabel(900.0f, 74.0f, "BOTONES DIGITALES & HAPTICOS", UI::TextScale::Normal, ColorRGBA::Yellow());

    // -------------------------------------------------------------------------
    // MATRIZ DE BOTONES DIGITALES CON FLANCOS
    // -------------------------------------------------------------------------
    struct BtnWidget {
        const char* name;
        VirtualButton btn;
        float x;
        float y;
        float w;
        float h;
    };

    const BtnWidget btnList[] = {
        { "A",    VirtualButton::A,        1140.0f, 130.0f, 42.0f, 26.0f },
        { "B",    VirtualButton::B,        1090.0f, 160.0f, 42.0f, 26.0f },
        { "X",    VirtualButton::X,        1090.0f, 100.0f, 42.0f, 26.0f },
        { "Y",    VirtualButton::Y,        1040.0f, 130.0f, 42.0f, 26.0f },
        { "L",    VirtualButton::L,         910.0f, 100.0f, 50.0f, 24.0f },
        { "R",    VirtualButton::R,        1190.0f, 100.0f, 50.0f, 24.0f },
        { "ZL",   VirtualButton::ZL,        910.0f, 130.0f, 50.0f, 24.0f },
        { "ZR",   VirtualButton::ZR,       1190.0f, 130.0f, 50.0f, 24.0f },
        { "UP",   VirtualButton::DPadUp,    950.0f, 170.0f, 40.0f, 22.0f },
        { "DOWN", VirtualButton::DPadDown,  950.0f, 220.0f, 40.0f, 22.0f },
        { "LEFT", VirtualButton::DPadLeft,  905.0f, 195.0f, 40.0f, 22.0f },
        { "RGHT", VirtualButton::DPadRight, 995.0f, 195.0f, 40.0f, 22.0f },
        { "+",    VirtualButton::Plus,     1080.0f, 205.0f, 32.0f, 22.0f },
        { "-",    VirtualButton::Minus,    1040.0f, 205.0f, 32.0f, 22.0f },
        { "HOME", VirtualButton::Home,     1060.0f, 235.0f, 55.0f, 22.0f },
        { "L3",   VirtualButton::StickL,    910.0f, 235.0f, 38.0f, 22.0f },
        { "R3",   VirtualButton::StickR,   1190.0f, 235.0f, 38.0f, 22.0f }
    };

    for (const auto& bw : btnList) {
        bool isHeld = pad.IsHeld(bw.btn);
        bool justPressed = pad.WasJustPressed(bw.btn);

        ColorRGBA fill = ColorRGBA{20, 28, 44, 220};
        ColorRGBA border = ColorRGBA{70, 95, 130, 200};
        ColorRGBA textCol = ColorRGBA{160, 180, 205, 255};

        if (justPressed) {
            fill = ColorRGBA{40, 200, 100, 255};
            border = ColorRGBA::White();
            textCol = ColorRGBA::White();
        } else if (isHeld) {
            fill = ColorRGBA{230, 180, 20, 240};
            border = ColorRGBA::Yellow();
            textCol = ColorRGBA::Black();
        }

        UI::UIToolkit::DrawButton(UI::UIRect{bw.x, bw.y, bw.w, bw.h}, bw.name, false, UI::TextScale::Small, &fill, &border, &textCol);
    }

    UI::UIToolkit::DrawSeparator(900.0f, 270.0f, 350.0f);

    // -------------------------------------------------------------------------
    // SUITE DE PRUEBAS DE VIBRACIÓN Y HÁPTICOS (JASON GREGORY CAP. 8.2)
    // -------------------------------------------------------------------------
    UI::UIToolkit::DrawLabel(900.0f, 280.0f, "SUITE DE PRUEBAS DE HAPTICOS", UI::TextScale::Small, ColorRGBA::Cyan());

    bool isRumbling = rumble.IsRumbling(rPlayer);
    ColorRGBA motorBg = isRumbling ? ColorRGBA{60, 180, 40, 240} : ColorRGBA{35, 45, 60, 220};
    ColorRGBA motorBorder = isRumbling ? ColorRGBA{120, 255, 100, 255} : ColorRGBA{80, 110, 140, 200};

    StackString<64> strMotor;
    if (isRumbling) {
        float pulse = std::sin(m_rumbleIndicatorTimer);
        strMotor.Format("ACTUADOR MOTOR: VIBRANDO [%s]", (pulse > 0.0f) ? "~~~" : "---");
    } else {
        strMotor.Format("ACTUADOR MOTOR: EN REPOSO (%s)", m_lastRumblePreset);
    }
    UI::UIToolkit::DrawButton(UI::UIRect{900.0f, 302.0f, 350.0f, 32.0f}, strMotor.c_str(), false, UI::TextScale::Small, &motorBg, &motorBorder);

    // Botones de Presets Hápticos
    UI::UIToolkit::DrawButton(s_btnPresetTick, "[1] SUTIL (0.08s, 25%)", false, UI::TextScale::Small);
    UI::UIToolkit::DrawButton(s_btnPresetJump, "[2] SALTO (0.15s, 50%)", false, UI::TextScale::Small);
    UI::UIToolkit::DrawButton(s_btnPresetDamage, "[3] IMPACTO (0.35s, 85%)", false, UI::TextScale::Small);
    UI::UIToolkit::DrawButton(s_btnPresetExplosion, "[4] EXPLOSION (0.60s, 100%)", false, UI::TextScale::Small);

    ColorRGBA stopBg{180, 40, 40, 240};
    ColorRGBA stopBorder{255, 100, 100, 255};
    UI::UIToolkit::DrawButton(s_btnStopAllRumble, "DETENER TODA LA VIBRACION", false, UI::TextScale::Small, &stopBg, &stopBorder);

    UI::UIToolkit::DrawSeparator(900.0f, 518.0f, 350.0f);

    // -------------------------------------------------------------------------
    // AJUSTES EN CALIENTE DE ENGINECONFIG
    // -------------------------------------------------------------------------
    UI::UIToolkit::DrawLabel(900.0f, 524.0f, "OPCIONES GLOBALES (ENGINECONFIG)", UI::TextScale::Small, ColorRGBA::Yellow());

    UI::UIToolkit::DrawToggleButton(s_btnToggleRumble, "VIBRACION GLOBAL", config.controls.rumbleEnabled, false, UI::TextScale::Small);

    StackString<32> strDeadVal;
    strDeadVal.Format("%.2f", config.controls.deadzoneRadius);
    UI::UIToolkit::DrawValueButton(s_btnCycleDeadzone, "ZONA MUERTA RADIAL", strDeadVal.c_str(), false, UI::TextScale::Small);

    // =========================================================================
    // 4. PANEL CENTRAL-INFERIOR: ACTION MAPPING EVALUADO EN VIVO
    // =========================================================================
    UI::UIToolkit::DrawPanel(s_panelCenterBottom);
    UI::UIToolkit::DrawLabel(415.0f, 552.0f, "ACTION MAPPING (DESACOPLADO DE HARDWARE)", UI::TextScale::Small, ColorRGBA::Yellow());

    StackString<80> strActionAxes;
    strActionAxes.Format("AXIS: MoveX:%+.2f  MoveY:%+.2f  LookX:%+.2f  LookY:%+.2f",
                         input.GetActionAxis(InputAction::MoveX, m_selectedPlayerIndex),
                         input.GetActionAxis(InputAction::MoveY, m_selectedPlayerIndex),
                         input.GetActionAxis(InputAction::LookX, m_selectedPlayerIndex),
                         input.GetActionAxis(InputAction::LookY, m_selectedPlayerIndex));
    UI::UIToolkit::DrawLabel(415.0f, 574.0f, strActionAxes.c_str(), UI::TextScale::Small, ColorRGBA::Cyan());

    StackString<80> strActionBtns;
    strActionBtns.Format("ACCIONES: Salto:%s  Interact:%s  Pausa:%s  Atras:%s",
                         input.IsActionHeld(InputAction::Jump, m_selectedPlayerIndex) ? "[SI]" : "[NO]",
                         input.IsActionHeld(InputAction::Interact, m_selectedPlayerIndex) ? "[SI]" : "[NO]",
                         input.IsActionHeld(InputAction::Pause, m_selectedPlayerIndex) ? "[SI]" : "[NO]",
                         input.IsActionHeld(InputAction::Back, m_selectedPlayerIndex) ? "[SI]" : "[NO]");
    UI::UIToolkit::DrawLabel(415.0f, 598.0f, strActionBtns.c_str(), UI::TextScale::Small, ColorRGBA::White());

    // =========================================================================
    // 5. BARRA GUÍA INFERIOR
    // =========================================================================
    UI::UIToolkit::DrawPanel(s_panelBottomBar);
    UI::UIToolkit::DrawLabel(30.0f, 656.0f, "TOCA '< VOLVER' O PULSA [PLUS + MINUS] PARA SALIR | TOCA PESTANAS PARA CAMBIAR JUGADOR", UI::TextScale::Small, ColorRGBA{255, 230, 100, 255});
    UI::UIToolkit::DrawLabel(30.0f, 678.0f, "TODOS LOS BOTONES LIBRES PARA TEST EN MATRIZ | STICK IZQ: AVATAR 3D | STICK DER: CAMARA", UI::TextScale::Small, ColorRGBA{180, 220, 255, 240});
}

} // namespace WiiU
