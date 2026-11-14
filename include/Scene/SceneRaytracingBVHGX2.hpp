#pragma once

#include "Scene/GameScene3D.hpp"
#include "Core/Spatial/SpatialBVH.hpp"
#include "Core/ECS/Systems/CullingSystem.hpp"
#include "Graphics/ShadowSystem.hpp"
#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace WiiU {

/**
 * @brief Escena interactiva de Ray Tracing GPU con BVH Stackless, Pre-Culling CPU Paralelo y Sombras CSM (Cap. 11).
 *
 * Optimizaciones de silicio Espresso + AMD Latte:
 * 1. Recorrido BVH Pre-Order Stackless (`OPT-70`): 0 arreglos en GPRs del Pixel Shader.
 * 2. Paralelismo asimétrico Core 1 + Core 2 vía `JobSystem` (`OPT-01`, `OPT-106`):
 *    - Core 2: cinemática, construcción SAH, empaquetado Stackless y Culling BVH.
 *    - Core 1: cálculo de cascadas CSM, dirección solar y rectángulos de descarte GPU.
 * 3. Early Screen-Space Reject (`uScreenBounds`) y Early Floor Shadow Culling (`uShadowFloorBounds`).
 */
class SceneRaytracingBVHGX2 final : public GameScene3D {
public:
    static constexpr size_t SPHERE_COUNT = 12;
    static constexpr size_t MAX_GPU_BVH_NODES = 24; // 2*12 - 1 = 23 <= 24

    SceneRaytracingBVHGX2() = default;
    ~SceneRaytracingBVHGX2() override { m_fullscreenShader.Shutdown(); }

    SceneRaytracingBVHGX2(const SceneRaytracingBVHGX2&) = delete;
    SceneRaytracingBVHGX2& operator=(const SceneRaytracingBVHGX2&) = delete;
    SceneRaytracingBVHGX2(SceneRaytracingBVHGX2&&) = delete;
    SceneRaytracingBVHGX2& operator=(SceneRaytracingBVHGX2&&) = delete;

    void LoadAsync() override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return { "shaders/raytracing_bvh.gsh" };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 7: Ray Tracing BVH & Sombras CSM (Cap. 11)";
    }

protected:
    void OnGameInit() override;
    void OnGameExit() override;
    void OnGameUpdate(float dt) override;
    void OnGameRender3D(ForwardRendererGX2& renderer) override;
    void OnGameRenderDebug3D() override;
    void OnGameRenderHUD() override;

private:
    struct SphereInstance {
        Point3D baseCenter;
        Point3D currentCenter;
        float radius{0.6f};
        float phase{0.0f};
        float speed{1.0f};
        float orbitAmp{0.5f};
        float colorR{1.0f};
        float colorG{1.0f};
        float colorB{1.0f};
        float reflectivity{0.5f};
    };

    struct PixelUniformOffsets {
        int32_t camPos{-1};
        int32_t camForward{-1};
        int32_t camRight{-1};
        int32_t camUp{-1};
        int32_t lightPos{-1};
        int32_t renderFlags{-1};
        int32_t sunDirAndNodes{-1};
        int32_t screenBounds{-1};
        int32_t shadowFloorBounds{-1};
        int32_t spheres{-1};
        int32_t sphereColors{-1};
        int32_t bvhNodes{-1};
    };

    struct BVHParallelJobParams {
        SceneRaytracingBVHGX2* self{nullptr};
        CameraComponent camera{};
        bool animateSpheres{false};
    };

    static void BVHParallelJobTask(void* userData);

    void InitSpheres();
    void AdvanceSpheres() noexcept;
    void RebuildBVHAndUniforms();
    void ComputeAccelerationBounds() noexcept;
    void HandleControls(float dt);
    void CycleShadowMode() noexcept;

    std::array<SphereInstance, SPHERE_COUNT> m_spheres{};
    std::array<Entity, SPHERE_COUNT> m_sphereEntities{};
    SpatialBVH m_bvh;

    // Búferes alineados listos para transmisión a RHI
    std::array<float, SPHERE_COUNT * 4> m_gpuSpheres{};
    std::array<float, SPHERE_COUNT * 4> m_gpuSphereColors{};
    std::array<float, MAX_GPU_BVH_NODES * 8> m_gpuBvhNodes{};
    std::array<float, 4> m_gpuScreenBounds{-1.0f, -1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_gpuShadowFloorBounds{-25.0f, -25.0f, 25.0f, 25.0f};
    Point3D m_sunDir{0.4f, 0.8f, 0.45f};
    size_t m_packedNodeCount{0};

    // Subsistema de Sombras en Cascada (Cap. 11.3)
    ShadowConfig m_shadowConfig{};
    std::array<ShadowCascade, ShadowSystem::MAX_CASCADES> m_cascades{};

    // Pase Fullscreen desacoplado en engine_graphics (RHI)
    RHIFullscreenShader m_fullscreenShader;
    PixelUniformOffsets m_uniforms{};
    BVHParallelJobParams m_jobParams{};

    // Estado de simulación y visualización
    Point3D m_lightPos{3.5f, 7.5f, 4.0f};
    float m_animTime{0.0f};
    float m_lightRadius{0.85f};
    bool m_bvhEnabled{true};
    bool m_heatmapEnabled{false};
    bool m_showBvhWireframe{true};
    bool m_paused{false};
    bool m_smpActive{false};
    int m_shadowMode{2}; // 0 = OFF, 1 = HARD, 2 = SOFT PENUMBRA
    size_t m_visibleEntities{0};
    bool m_prevTouchDown{false};
    bool m_exitRequested{false};
    bool m_asyncPrepared{false};
};

} // namespace WiiU
