#pragma once

#include "Scene/GameScene3D.hpp"
#include "Core/Time/GameClock.hpp"
#include "Core/Events/GameplayEventBus.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Core/ECS/Components/AudioEmitterComponent.hpp"
#include "Core/ECS/Systems/AudioSpatialSystem.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"
#include "Core/ECS/Components/AnimatorComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Core/ECS/Components/ColliderComponent.hpp"
#include "Core/ECS/Components/TriggerComponent.hpp"
#include "Graphics/Animation/Skeleton.hpp"
#include "Graphics/Animation/AnimationClip.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>

namespace WiiU {

/**
 * @brief [Hito 116 / 119] Escena Interactiva de Colisiones, Resolución de Obstáculos y Triggers ECS.
 * Deriva de GameScene3D reutilizando la base unificada de renderizado, cámara y recursos GPU.
 * 
 * Demuestra el funcionamiento de la arquitectura modular de Game Engine Architecture (Cap. 13, 14, 15, 16):
 * - Control de personaje en 3ª persona sobre el Pájaro Naranja (21 huesos LBS) con Stick Izquierdo y salto con A.
 * - Cámara orbital desacoplada vía OrbitCameraSystem (Stick Derecho / ZL / ZR).
 * - Colisión sólida (Sphere vs AABB y Sphere vs Sphere) con deslizamiento suave (Slide Response).
 * - Triggers espaciales (TriggerComponent): balizas, bounce pad y zona de meta.
 * - Visualización de debug wireframe 3D desacoplada mediante DebugDraw3D.
 */
class SceneCollisionTriggerDemoGX2 : public GameScene3D {
public:
    static constexpr uint32_t NUM_PILLARS = 4;
    static constexpr uint32_t NUM_BEACONS = 5;

    SceneCollisionTriggerDemoGX2();
    ~SceneCollisionTriggerDemoGX2() override;

    SceneCollisionTriggerDemoGX2(const SceneCollisionTriggerDemoGX2&) = delete;
    SceneCollisionTriggerDemoGX2& operator=(const SceneCollisionTriggerDemoGX2&) = delete;
    SceneCollisionTriggerDemoGX2(SceneCollisionTriggerDemoGX2&&) = delete;
    SceneCollisionTriggerDemoGX2& operator=(SceneCollisionTriggerDemoGX2&&) = delete;

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
            "shaders/advanced_skel_gx2.gsh",
            "models/skybox_day.wcube",
            "models/skydome.wmesh",
            "models/skydome.wtex",
            "models/dutch_house.wmesh",
            "models/dutch_house_props.wmesh",
            "models/dutch_house.wtex",
            "models/dutch_house_props.wtex",
            "models/pillar.wmesh",
            "models/beacon.wmesh",
            "models/obelisk.wmesh",
            "models/ground_plane.wmesh",
            "models/ground_grass.wtex",
            "models/bird_orange.wmesh",
            "models/bird_orange.wskel",
            "models/bird_orange.wanim",
            "models/bird_orange.wtex"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Colisiones y Triggers ECS (Juego Interactivo 3D)";
    }

private:
    bool CreateTextures();
    void ResetGameplayState();

    // Entidades del Nivel
    Entity m_skydomeEntity{};
    Entity m_groundEntity{};
    Entity m_houseEntity{};
    Entity m_propsEntity{};
    Entity m_pillarEntities[NUM_PILLARS]{};
    Entity m_beaconEntities[NUM_BEACONS]{};
    Entity m_obeliskEntity{};
    Entity m_bouncePadEntity{};
    Entity m_dopplerDroneEntity{};
    Entity m_playerEntity{};

    // Recursos de Mallas Estáticas (Flyweight compartido entre entidades)
    StaticMesh m_skydomeMesh;
    StaticMesh m_groundMesh;
    StaticMesh m_houseMesh;
    StaticMesh m_propsMesh;
    StaticMesh m_pillarMesh;
    StaticMesh m_beaconMesh;
    StaticMesh m_obeliskMesh;
    StaticMesh m_birdMesh;

    // Animación Esquelética del Jugador (Pájaro 21 huesos)
    Skeleton m_skeleton{};
    std::vector<AnimationClip> m_animationClips{};
    int m_currentAnimClip{2}; // 2 = Mirar (Idle), 0 = Salto (Run/Jump), 1 = Piar (Celebrate)

    // Estado de Gameplay y Telemetría de Colisiones / Tiempo / Audio / Eventos (Hito 133)
    GameClock m_gameClock{};
    GameplayEventBus m_eventBus{};
    uint32_t m_collectedBeacons{0};
    bool m_inGoalZone{false};
    bool m_levelCompleted{false};
    bool m_showDebugInfo{true};
    bool m_isPaused{false};
    bool m_bulletTimeActive{false};
    bool m_splineCameraActive{false};
    bool m_lastAcousticInZone{false};
    float m_elapsedTime{0.0f};
    float m_droneAngle{0.0f};
    float m_lastBounceTime{-10.0f};
    Point3D m_prevPlayerPos{0.0f, 0.0f, 1.2f};
    bool m_isDraggingCamera{false};
    float m_dragStartX{0.0f};
    float m_dragStartY{0.0f};
    float m_initialYaw{0.0f};
    float m_initialPitch{20.0f};

    // Staging Buffers para carga asíncrona en Core 2
    std::vector<uint8_t> m_stagedCubeMap;
    std::vector<uint8_t> m_stagedSkydomeMesh;
    std::vector<uint8_t> m_stagedGroundMesh;
    std::vector<uint8_t> m_stagedHouseMesh;
    std::vector<uint8_t> m_stagedPropsMesh;
    std::vector<uint8_t> m_stagedPillarMesh;
    std::vector<uint8_t> m_stagedBeaconMesh;
    std::vector<uint8_t> m_stagedObeliskMesh;
    std::vector<uint8_t> m_stagedBirdMesh;

    ResourceHandle<GX2Texture> m_hSkydomeTexture{};
    ResourceHandle<GX2Texture> m_hGrassTexture{};
    ResourceHandle<GX2Texture> m_hHouseTexture{};
    ResourceHandle<GX2Texture> m_hPropsTexture{};
    ResourceHandle<GX2Texture> m_hBirdTexture{};
    ResourceHandle<GX2Texture> m_hWhiteTexture{};
    bool m_texturesCreated{false};
};

} // namespace WiiU
