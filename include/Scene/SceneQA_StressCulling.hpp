#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Core/Spatial/Frustum.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/MeshLODGroup.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace Neleo {

/**
 * @brief Suite G6: Stress Test de Hardware Instancing (hasta 1,000 instancias en lotes de 256),
 *        Frustum Culling con Cámara Debug (pirámide 3D en DebugDraw3D), LOD Dinámico de 3 niveles
 *        y 6 bloques rectangulares orbitales en PBR estándar.
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 4.9 (Instancing), Cap. 11-13 (Geometría procedural)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 18 (Pipeline Opt), Cap. 19 (Culling & LOD)
 */
class SceneQA_StressCulling final : public GraphicsQASceneBase {
public:
    static constexpr uint32_t MAX_INSTANCES = 1000u;
    static constexpr uint32_t ORBIT_RECT_COUNT = 6u;

    SceneQA_StressCulling() noexcept = default;
    ~SceneQA_StressCulling() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G6: Instancing, Culling y LOD";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G6"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "qa.g6.title";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "qa.g6.controls";
    }

private:
    struct InstanceRecord {
        Point3D basePos{};
        float scale{1.0f};
        float radius{1.0f};
        float phase{0.0f};
        float rotSpeed{0.5f};
    };

    void BuildInstanceField() noexcept;
    void UpdateOrbitRects(float time) noexcept;
    void DrawFrozenFrustumWireframe() const noexcept;

    StaticMesh m_floorMesh;
    StaticMesh m_lodMesh0; // High (UVSphere 14x10, verde-cian)
    StaticMesh m_lodMesh1; // Mid  (UVSphere 8x6, amarillo-dorado)
    StaticMesh m_lodMesh2; // Low  (Cube 12 tris, naranja-rojo)
    MeshLODGroup m_lodGroup;

    // Bloques rectangulares orbitales en PBR estándar
    StaticMesh m_orbitRectMesh;
    std::array<Matrix4x4, ORBIT_RECT_COUNT> m_orbitRectModels{};

    // Búferes fijos Zero-Alloc en memoria miembro (0 heap allocs en Update/Render a 60 FPS)
    std::array<InstanceRecord, MAX_INSTANCES> m_instances{};
    std::array<Matrix4x4, MAX_INSTANCES> m_lodBuckets[MeshLODGroup::MAX_LEVELS]{};
    uint32_t m_lodCounts[MeshLODGroup::MAX_LEVELS]{0u, 0u, 0u};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    // Estado interactivo y de depuración
    uint32_t m_countPreset{2u}; // 0=256, 1=512, 2=1000
    bool m_cullingEnabled{true};
    bool m_lodEnabled{true};
    bool m_freezeFrustum{false};

    ViewFrustum m_cullFrustum;
    Point3D m_cullOrigin{0.0f, 6.0f, 18.0f};
    Point3D m_frozenCorners[8]{};
    Matrix4x4 m_lastView{Matrix4x4::Identity()};
    Matrix4x4 m_lastProj{Matrix4x4::Identity()};

    uint32_t m_visibleCount{0u};
    uint32_t m_culledCount{0u};
    uint32_t m_instDrawCalls{0u};
    float m_animTime{0.0f};
};

} // namespace Neleo
