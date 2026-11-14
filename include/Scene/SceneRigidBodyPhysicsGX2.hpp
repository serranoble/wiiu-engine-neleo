#pragma once

#include "Scene/GameScene3D.hpp"
#include "Core/Time/GameClock.hpp"
#include "Core/ECS/Systems/RigidBodyPhysicsSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace WiiU {

/**
 * @brief [Hito 131] Escena Interactiva Showcase de Dinámica Rígida, Constraints y Física de Personajes (Cap. 14).
 *
 * Totalmente desacoplada de headers <gx2/*.h> y <vpad/input.h> mediante capas de abstracción
 * del motor (`GameScene3D`, `ForwardRendererGX2`, `StaticMesh`, `DebugDraw3D`, `UIToolkit`, `InputManager`).
 *
 * Demuestra en tiempo real a 60 FPS en Nintendo Wii U:
 * - Pirámide de 10 cajas rígidas con Impulsos Secuenciales, fricción de Coulomb y Baumgarte stabilization.
 * - Péndulo/cadena de 3 eslabones con restricciones XPBD (`BallSocketPin` / `Distance`).
 * - Oscilador elástico vertical con muelle de Hooke y amortiguamiento viscoso (`Spring`).
 * - Muñeco articulado (Ragdoll simplificado de 5 cuerpos) suspendido y unido por constraints.
 * - Esferas de prueba con distintos coeficientes de restitución ($e = 0.15, 0.55, 0.90$).
 * - Acoplamiento bidireccional entre el avatar `KinematicCharacterComponent` (KCC) y cuerpos rígidos.
 * - Disparo de proyectiles físicos, onda expansiva radial y telemetría/hibernación (Sleeping).
 */
class SceneRigidBodyPhysicsGX2 final : public GameScene3D {
public:
    static constexpr size_t MAX_PROJECTILES = 16;

    SceneRigidBodyPhysicsGX2() = default;
    ~SceneRigidBodyPhysicsGX2() override = default;

    SceneRigidBodyPhysicsGX2(const SceneRigidBodyPhysicsGX2&) = delete;
    SceneRigidBodyPhysicsGX2& operator=(const SceneRigidBodyPhysicsGX2&) = delete;
    SceneRigidBodyPhysicsGX2(SceneRigidBodyPhysicsGX2&&) = delete;
    SceneRigidBodyPhysicsGX2& operator=(SceneRigidBodyPhysicsGX2&&) = delete;

    void LoadAsync() override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {};
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 8: Dinamica Rigida y Constraints (Cap. 14)";
    }

protected:
    void OnGameInit() override;
    void OnGameExit() override;
    void OnGameUpdate(float dt) override;
    void OnGameRender3D(ForwardRendererGX2& renderer) override;
    void OnGameRenderDebug3D() override;
    void OnGameRenderHUD() override;

private:
    struct StagedMeshData {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;

        void Clear() noexcept {
            std::vector<MeshVertex>().swap(vertices);
            std::vector<uint32_t>().swap(indices);
        }
    };

    void ResetPhysicsPlayground();
    void FireProjectileFromCamera();
    void TriggerExplosion();
    void UpdateCameraAndCharacterInput(float dt);

    RigidBodyPhysicsSystem m_physicsSystem;
    GameClock m_gameClock{};

    // Mallas procedurales en VRAM (vertex colors activados via textura fallback 0,0,0,0)
    StaticMesh m_floorMesh;
    StaticMesh m_crateMesh;
    StaticMesh m_steelMesh;
    StaticMesh m_sphereRedMesh;
    StaticMesh m_sphereGoldMesh;
    StaticMesh m_avatarMesh;

    // Staging CPU generado asíncronamente en Core 2
    StagedMeshData m_stagedFloor;
    StagedMeshData m_stagedCrate;
    StagedMeshData m_stagedSteel;
    StagedMeshData m_stagedSphereRed;
    StagedMeshData m_stagedSphereGold;
    StagedMeshData m_stagedAvatar;

    Entity m_floorEntity{Entity::Null};
    Entity m_playerEntity{Entity::Null};

    std::array<Entity, MAX_PROJECTILES> m_projectiles{};
    size_t m_projectileCursor{0};
    size_t m_projectileCount{0};

    float m_simTime{0.0f};
    float m_explosionFlashTimer{0.0f};
    float m_explosionImpulse{22.0f};
    bool m_showDebugWireframe{true};
    bool m_isPaused{false};
};

} // namespace WiiU
