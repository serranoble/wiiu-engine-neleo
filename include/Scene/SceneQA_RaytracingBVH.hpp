#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Core/Spatial/StacklessBVH.hpp"
#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include <cstdint>
#include <string>

namespace Neleo {

/**
 * @brief Suite G8 (Final): Ray Tracing en GPU con BVH Stackless Pre-Order (24 nodos / 12 esferas),
 *        Sombras Duras / Penumbra Suave, Reflexiones recursivas, Heatmap de costo BVH
 *        y Render Scale dinámico (360p / 480p / 540p / 648p / 720p) sobre RHIFullscreenShader.
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 16 (Ray Tracing: intersección, sombras, reflexiones)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 11 (Global Illumination / RT), Cap. 22 (BVH Traversal)
 */
class SceneQA_RaytracingBVH final : public GraphicsQASceneBase {
public:
    SceneQA_RaytracingBVH() noexcept = default;
    ~SceneQA_RaytracingBVH() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G8: Ray Tracing GPU (BVH) y Render Scale";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G8"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "RAY TRACING EN GPU (BVH STACKLESS), SOMBRAS PENUMBRA, HEATMAP Y RENDER SCALE";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:RENDER SCALE (360P..720P)  DPAD ARR/ABJ:SOMBRAS  X:BVH/BRUTE  A:HEATMAP  L/R:PAUSA";
    }

private:
    [[nodiscard]] const char* GetShadowModeLabel() const noexcept;

    RHIFullscreenShader m_rtShader;
    StacklessBVHBuilder::BuildResult m_bvhResult{};

    // Offsets reflejados de uniformes del pixel shader raytracing_bvh.frag
    int32_t m_offCamPos{-1};
    int32_t m_offCamForward{-1};
    int32_t m_offCamRight{-1};
    int32_t m_offCamUp{-1};
    int32_t m_offLightPos{-1};
    int32_t m_offRenderFlags{-1};
    int32_t m_offSunDirAndNodes{-1};
    int32_t m_offScreenBounds{-1};
    int32_t m_offShadowFloorBounds{-1};
    int32_t m_offSpheres{-1};
    int32_t m_offSphereColors{-1};
    int32_t m_offBvhNodes{-1};

    size_t m_resPresetIdx{2u}; // Por defecto 540p (960x540 -> 1280x720 bilineal HW)
    uint32_t m_shadowMode{2u}; // 0=OFF, 1=Hard, 2=Soft Penumbra
    bool m_bvhEnabled{true};
    bool m_heatmapEnabled{false};
    bool m_animPaused{false};
    float m_rtTime{0.0f};
};

} // namespace Neleo
