#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>
#include <string>

namespace WiiU {

/**
 * @brief Suite G3: Validación de Sombras en Tiempo Real (Shadow Mapping 2-Pass en AMD Latte GX2,
 *        Hard vs PCF 3x3 vs PCF 5x5, Depth/Slope Bias [Acne vs Óptimo vs Peter Panning] y Front-Face Culling).
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 8 (Shadow Mapping 2-Pass, Acne, Peter Panning, PCF)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 7 (Shadows: Depth/Slope Bias, PCF, Front-Face Culling)
 */
class SceneQA_Shadows final : public GraphicsQASceneBase {
public:
    SceneQA_Shadows() noexcept = default;
    ~SceneQA_Shadows() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G3: Sombras (PCF y Bias)";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G3"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "SOMBRAS 2-PASS GX2 (HARD / PCF 3x3 / PCF 5x5, BIAS Y ACNE/PANNING)";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:MODO PCF  DPAD ARR/ABJ:BIAS (ACNE/OPT/PAN)  X:FRONT-CULL  A:PAUSA SOL";
    }

private:
    [[nodiscard]] const char* GetShadowModeName() const noexcept;
    [[nodiscard]] const char* GetBiasPresetName() const noexcept;

    StaticMesh m_floorMesh;
    StaticMesh m_pillarMesh;
    StaticMesh m_lintelMesh;
    StaticMesh m_floatingBoxMesh;
    StaticMesh m_centerSphereMesh;

    GX2Texture m_brickTex{};
    bool m_brickTexLoaded{false};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    uint32_t m_shadowMode{2u};      // 0=OFF, 1=Hard, 2=PCF 3x3, 3=PCF 5x5, 4=Debug Shadow
    uint32_t m_biasPreset{1u};      // 0=Cero (Acne), 1=Optimo, 2=Excesivo (Peter Panning)
    bool m_cullFrontFaces{true};    // Front-face culling en Shadow Pass (RTR4 Cap. 7.4)
    bool m_sunPaused{false};
    float m_sunTime{0.45f};
};

} // namespace WiiU
