#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>
#include <string>

namespace Neleo {

/**
 * @brief Suite G2: Validación de Iluminación Analítica (Direccional + 4 Puntuales + 1 Spot Light),
 *        PBR Cook-Torrance (GGX/Smith/Schlick) vs Blinn-Phong y Normal/Bump Mapping (TBN).
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 7 (Iluminación y 3 tipos de luz), Cap. 10 (Bump/Normal Mapping)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 5, Cap. 6.7, Cap. 9 (Physically Based Shading)
 */
class SceneQA_LightingPBR final : public GraphicsQASceneBase {
public:
    SceneQA_LightingPBR() noexcept = default;
    ~SceneQA_LightingPBR() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G2: Iluminacion y PBR";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G2"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "qa.g2.title";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "qa.g2.controls";
    }

private:
    [[nodiscard]] const char* GetShadingModeName() const noexcept;
    [[nodiscard]] const char* GetAlbedoPresetName() const noexcept;
    void GetAlbedoRGB(float& outR, float& outG, float& outB) const noexcept;

    StaticMesh m_sphereMesh;
    StaticMesh m_lightMarkerMesh;
    StaticMesh m_reliefCubeMesh;
    StaticMesh m_floorPlane;

    GX2Texture m_reliefTex{};
    bool m_reliefTexLoaded{false};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    uint32_t m_shadingMode{0u};   // 0=PBR, 1=Blinn-Phong, 2=Normals, 3=Rough/Metal
    uint32_t m_albedoPreset{0u};  // 0=Oro, 1=Acero, 2=Carmesi, 3=Cobalto
    uint32_t m_bumpPreset{1u};    // 0=OFF (0.0), 1=MEDIO (2.5), 2=ALTO (5.0)
    bool m_lightsPaused{false};
    float m_lightTime{0.0f};
};

} // namespace Neleo
