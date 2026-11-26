#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Graphics/Animation/Skeleton.hpp"
#include "Graphics/Animation/AnimationClip.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Neleo {

/**
 * @brief Escena de validación del Asset Cooker para modelos animados con Skinning GPU (.wmesh 80B + .wskel + .wanim + .wtex).
 *
 * Renderiza una única instancia animada de bird_orange con Linear Blend Skinning (LBS) en silicio GX2
 * para verificar la cadena completa cooker -> GPU sin sobrecarga de stress test.
 */
class SceneModelBird final : public GraphicsQASceneBase {
public:
    SceneModelBird() noexcept = default;
    ~SceneModelBird() override { OnQAExit(); }

    void LoadAsync() override;
    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Validacion Cooker: Pajaro Animado";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "COOKER SKINNING"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "VALIDACION DE MODELO ANIMADO COCINADO (BIRD ORANGE .WMESH/.WSKEL/.WANIM/.WTEX)";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:CLIP  ARR/ABJ:GIRO  A:PAUSA  X:ARCILLA  -:WTEX ON/OFF  B:MENU";
    }

private:
    void EvaluatePose(float time) noexcept;
    void FreeStagedBuffers() noexcept;

    std::vector<uint8_t> m_stagedBirdMesh;
    std::vector<uint8_t> m_stagedBirdTex;

    StaticMesh m_birdMesh;
    GX2Texture m_birdTex{};
    Skeleton m_skeleton{};
    std::vector<AnimationClip> m_clips;
    std::array<Matrix4x4, MAX_SKELETON_JOINTS> m_bonePalette{};

    bool m_birdMeshLoaded{false};
    bool m_skelLoaded{false};
    bool m_clipsLoaded{false};
    bool m_birdTexLoaded{false};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    size_t m_activeClipIdx{0u};
    bool m_animPaused{false};
    bool m_clayMode{false};
    bool m_useTextures{true};
    bool m_autoRotate{true};
    float m_animTime{0.0f};
    float m_turntableAngle{0.0f};
};

} // namespace Neleo
