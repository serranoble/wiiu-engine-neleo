#pragma once

#include "Scene/GameScene3D.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/AI/NavGraph.hpp"
#include "Core/AI/AStarPathfinder.hpp"
#include "Core/AI/AIControllerComponent.hpp"
#include "Core/AI/AISteeringSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>

namespace WiiU {

/**
 * @brief [Hito 120] Escena Demostrativa de Inteligencia Artificial y Navegación (Capítulo 19 Jason Gregory).
 * 
 * Deriva de GameScene3D reutilizando el pipeline GX2 unificado (ForwardRendererGX2, eDRAM depth buffer, UIToolkit).
 * 
 * Implementa de manera visual y pedagógica:
 * 1. Grafo espacial de navegación (NavGraph) con nodos transitables y aristas métricas.
 * 2. Búsqueda de caminos A* determinista en tiempo real (0 heap allocations a 60 FPS).
 * 3. Comportamientos cinemáticos de dirección (Craig Reynolds: Seek, Arrive, VelocityToYaw).
 * 4. Máquina de Estados Finitos (HFSM: Patrol <-> Chase <-> Investigate <-> ReturnHome).
 * 5. Sensor de percepción cónico (FOV 110° + rango métrico + proximidad 360°).
 * 6. Visualización 3D wireframe mediante DebugDraw3D y telemetría HUD completa.
 */
class SceneAIDemoGX2 : public GameScene3D {
public:
    static constexpr uint32_t NUM_PILLARS = 4;
    static constexpr uint32_t NUM_WAYPOINT_NODES = 9;

    SceneAIDemoGX2();
    ~SceneAIDemoGX2() override;

    SceneAIDemoGX2(const SceneAIDemoGX2&) = delete;
    SceneAIDemoGX2& operator=(const SceneAIDemoGX2&) = delete;
    SceneAIDemoGX2(SceneAIDemoGX2&&) = delete;
    SceneAIDemoGX2& operator=(SceneAIDemoGX2&&) = delete;

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
            "models/skybox_day.wcube",
            "models/skydome.wmesh",
            "models/skydome.wtex",
            "models/pillar.wmesh",
            "models/beacon.wmesh",
            "models/obelisk.wmesh",
            "models/ground_plane.wmesh",
            "models/ground_grass.wtex"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Inteligencia Artificial y Navegacion (Cap. 19 Gregory)";
    }

private:
    void SetupNavGraph();
    bool CreateTextures();

    // Entidades del Nivel ECS
    Entity m_skydomeEntity{};
    Entity m_groundEntity{};
    Entity m_pillarEntities[NUM_PILLARS]{};
    Entity m_waypointEntities[NUM_WAYPOINT_NODES]{};
    Entity m_aiAgentEntity{};
    Entity m_playerEntity{};

    // Mallas Estáticas
    StaticMesh m_skydomeMesh;
    StaticMesh m_groundMesh;
    StaticMesh m_pillarMesh;
    StaticMesh m_beaconMesh;
    StaticMesh m_obeliskMesh;

    // Grafo de Navegación Espacial
    NavGraph m_navGraph;

    // Parámetros y Estado del Jugador
    Point3D m_playerPos{0.0f, 0.0f, 5.0f};
    float m_playerSpeed{4.5f};

    // Parámetros y Estado de Debug
    bool m_showGraphEdges{true};
    bool m_showAStarPath{true};
    bool m_showVisionCone{true};
    bool m_isPaused{false};
    float m_simulationTime{0.0f};

    // Staging buffers para carga asíncrona
    std::vector<uint8_t> m_stagedSkydomeMesh;
    std::vector<uint8_t> m_stagedGroundMesh;
    std::vector<uint8_t> m_stagedPillarMesh;
    std::vector<uint8_t> m_stagedBeaconMesh;
    std::vector<uint8_t> m_stagedObeliskMesh;

    ResourceHandle<GX2Texture> m_hSkydomeTexture{};
    ResourceHandle<GX2Texture> m_hGrassTexture{};
    ResourceHandle<GX2Texture> m_hWhiteTexture{};
    bool m_texturesCreated{false};
};

} // namespace WiiU
