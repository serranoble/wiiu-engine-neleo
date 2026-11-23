#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/PostProcessHDRGX2.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>
#include <string>

namespace WiiU {

/**
 * @brief Suite G5: Validación de Transparencia (Back-to-Front Alpha Blending), Alpha-Cutout (discard),
 *        Render Target HDR (RGBA16F), Tone Mapping (ACES / Reinhard / Clamp), Bloom Multipase y Niebla.
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 14 (Niebla Lineal/Exp/Exp2, Blending, Clipping)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 5.5 (Transparencia/Sorting), Cap. 5.6 (sRGB/Gamma),
 *   Cap. 8 (HDR & Tone Mapping ACES/Reinhard), Cap. 12 (Bloom & Vignette)
 */
class SceneQA_PostProcessAlpha final : public GraphicsQASceneBase {
public:
    SceneQA_PostProcessAlpha() noexcept = default;
    ~SceneQA_PostProcessAlpha() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G5: HDR, Bloom, Alpha y Niebla";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G5"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "TRANSPARENCIA, ALPHA-CUTOUT, HDR (RGBA16F), TONE MAPPING ACES, BLOOM Y NIEBLA";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:TONEMAP  DPAD ARR/ABJ:NIEBLA  X:BLOOM  A:GAMMA sRGB  L/R:EXPOSICION";
    }

private:
    [[nodiscard]] const char* GetToneMapLabel() const noexcept;
    [[nodiscard]] const char* GetFogLabel() const noexcept;

    StaticMesh m_floorMesh;
    StaticMesh m_pillarMesh;
    StaticMesh m_orbMesh;
    StaticMesh m_panelMesh;

    GX2Texture m_cutoutTex{};
    bool m_cutoutTexLoaded{false};

    Graphics::PostProcessHDRGX2 m_postFX;
    Graphics::PostProcessHDRSettings m_settings{};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    uint32_t m_fogMode{2u}; // 0=OFF, 1=Lineal, 2=Exp, 3=Exp2
    float m_animTime{0.0f};
};

} // namespace WiiU
