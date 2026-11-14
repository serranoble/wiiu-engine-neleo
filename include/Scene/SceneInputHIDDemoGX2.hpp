#pragma once

#include "Scene/GameScene3D.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Core/ResourceHandle.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include "Input/InputManager.hpp"
#include "Input/VirtualGamepad.hpp"
#include "Input/RumbleManager.hpp"
#include "Input/ActionMapping.hpp"
#include <vector>
#include <string>
#include <string_view>

namespace WiiU {

/**
 * @brief [Hito 127] Escena Demostrativa de Dispositivos de Entrada (HID) y Hápticos.
 * Conforme a Jason Gregory (Game Engine Architecture 3ª Edición, Capítulo 8).
 * 
 * Reutiliza la arquitectura unificada GameScene3D con pipeline GX2 de silicio:
 * 1. Detección en tiempo real de mandos Nintendo (GamePad DRC, Pro Controller, Wiimote,
 *    Wiimote + Nunchuk, Classic Controller) y PC (SDL2).
 * 2. Visualizador analógico de zona muerta radial continua de Jason Gregory (Cap. 8.2).
 * 3. Matriz interactiva de botones digitales con flancos (JustPressed / Held / JustReleased).
 * 4. Suite interactiva de pruebas hápticas (SubtleTick, JumpLand, DamageImpact, HeavyExplosion).
 * 5. Evaluación en vivo de mapeo de acciones semánticas (ActionMap).
 * 6. Ajuste dinámico de EngineConfig (rumbleEnabled, deadzoneRadius).
 * 7. Modelo 3D interactivo que responde en tiempo real a las entradas de los mandos.
 */
class SceneInputHIDDemoGX2 : public GameScene3D {
public:
    SceneInputHIDDemoGX2();
    ~SceneInputHIDDemoGX2() override;

    SceneInputHIDDemoGX2(const SceneInputHIDDemoGX2&) = delete;
    SceneInputHIDDemoGX2& operator=(const SceneInputHIDDemoGX2&) = delete;
    SceneInputHIDDemoGX2(SceneInputHIDDemoGX2&&) = delete;
    SceneInputHIDDemoGX2& operator=(SceneInputHIDDemoGX2&&) = delete;

    void LoadAsync() override;

    // Métodos del ciclo de vida GameScene3D
    void OnGameInit() override;
    void OnGameExit() override;
    void OnGameUpdate(float dt) override;
    void OnGameRender3D(ForwardRendererGX2& renderer) override;
    void OnGameRenderDebug3D() override;
    void OnGameRenderHUD() override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {
            "click.wav",
            "shaders/skydome_gx2.gsh",
            "shaders/advanced_mesh_gx2.gsh",
            "models/skydome.wmesh",
            "models/skydome.wtex",
            "models/ground_plane.wmesh",
            "models/ground_grass.wtex",
            "models/beacon.wmesh",
            "models/pillar.wmesh"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Diagnostico de Controles HID (Cap. 8)";
    }

private:
    bool CreateTextures();

    // Mallas y Geometría 3D
    StaticMesh m_skydomeMesh;
    StaticMesh m_groundMesh;
    StaticMesh m_actorMesh;
    StaticMesh m_pillarMesh;

    std::vector<uint8_t> m_stagedSkydomeMesh;
    std::vector<uint8_t> m_stagedGroundMesh;
    std::vector<uint8_t> m_stagedActorMesh;
    std::vector<uint8_t> m_stagedPillarMesh;

    // Entidades del mundo ECS
    Entity m_skydomeEntity{};
    Entity m_groundEntity{};
    Entity m_actorEntity{};
    Entity m_pillarEntity{};

    // Handles de texturas gestionados automáticamente por GameScene3D
    ResourceHandle<GX2Texture> m_hSkydomeTex{};
    ResourceHandle<GX2Texture> m_hGrassTex{};
    ResourceHandle<GX2Texture> m_hWhiteTex{};
    bool m_texturesCreated{false};

    // Estado del Actor 3D interactivo
    Point3D m_actorPosition{0.0f, 0.4f, 0.0f};
    float m_actorYaw{0.0f};
    float m_actorVerticalVel{0.0f};
    bool m_isGrounded{true};
    float m_actorTilt{0.0f};

    // Ranura de inspección seleccionada (0: Jugador 1 / DRC, 1..3: Jugadores 2..4)
    size_t m_selectedPlayerIndex{0};

    // Estado de depuración y telemetría de hápticos
    float m_rumbleIndicatorTimer{0.0f};
    const char* m_lastRumblePreset{"NINGUNO"};
};

} // namespace WiiU
