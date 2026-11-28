#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/RHI/RenderTargetGX2.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>
#include <string>

namespace Neleo {

/**
 * @brief Suite G4: Validación de Entorno, Skybox CubeMap (.wcube / Procedural),
 *        IBL (Reflexiones ambientales PBR), Vidrio Refractivo (Fresnel + IOR) y Agua Realista 3D.
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 9 (Skyboxes, CubeMaps, Reflexión/Refracción), Cap. 15 (Agua)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 10 (IBL / Environment Mapping), Cap. 13 (Skybox)
 */
class SceneQA_EnvironmentWater final : public GraphicsQASceneBase {
public:
    SceneQA_EnvironmentWater() noexcept = default;
    ~SceneQA_EnvironmentWater() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G4: Entorno, IBL y Agua";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G4"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "qa.g4.title";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "qa.g4.controls";
    }

private:
    void RenderSpheresAndSkybox(GX2Texture* activeCube, const Point3D& skyCenterPos);
    [[nodiscard]] float GetCurrentIOR() const noexcept;
    [[nodiscard]] const char* GetCurrentIORLabel() const noexcept;

    StaticMesh m_skyboxCubeMesh;
    StaticMesh m_waterPlaneMesh;
    StaticMesh m_sphereMesh;

    Graphics::RenderTargetGX2 m_reflectionRT;
    GX2Texture m_dayCubeMap{};
    GX2Texture m_procCubeMap{};
    bool m_dayCubeLoaded{false};
    bool m_procCubeLoaded{false};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    bool m_iblEnabled{true};
    bool m_useProceduralSky{false};
    bool m_wavesPaused{false};
    uint32_t m_iorPreset{2u}; // 0=1.10 (Hielo), 1=1.33 (Agua), 2=1.52 (Vidrio), 3=2.42 (Diamante)
    float m_waterTime{0.0f};
};

} // namespace Neleo
