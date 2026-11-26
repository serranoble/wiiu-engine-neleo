#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>
#include <string>

namespace Neleo {

/**
 * @brief Suite G1: Validación de Pipeline 3D, Transformaciones Jerárquicas (Cuaterniones/MVP),
 *        Mipmaps reales, Filtrado (Point/Bilinear/Trilinear/Aniso 16x) y UV Wrapping (Wrap/Clamp/Mirror).
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 4, Cap. 5, Cap. 6
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 2, Cap. 4, Cap. 6
 */
class SceneQA_PipelineTextures final : public GraphicsQASceneBase {
public:
    SceneQA_PipelineTextures() noexcept = default;
    ~SceneQA_PipelineTextures() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G1: Pipeline y Texturas";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G1"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "PIPELINE, TRANSFORMACIONES (MVP/QUAT), MIPMAPS Y FILTRADO GX2";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:FILTRO  DPAD ARR/ABJ:UV WRAP  X:MIP COLOR-CODE  A:PAUSA ORBITA";
    }

private:
    void ApplyCurrentSamplerPreset() noexcept;
    [[nodiscard]] const char* GetFilterPresetName() const noexcept;
    [[nodiscard]] const char* GetWrapModeName() const noexcept;

    // Geometría procedural en VRAM
    StaticMesh m_runwayPlane;
    StaticMesh m_uvCube;
    StaticMesh m_sunSphere;
    StaticMesh m_planetSphere;
    StaticMesh m_moonSphere;

    // Texturas procedurales con cadena completa de mipmaps (256x256 -> 9 niveles)
    GX2Texture m_checkerTex{};
    GX2Texture m_mipColorTex{};
    bool m_checkerLoaded{false};
    bool m_mipColorLoaded{false};

    // Parámetros interactivos QA
    uint32_t m_filterPresetIndex{5u}; // Por defecto: Aniso 8x + Trilinear
    TextureWrapMode m_wrapMode{TextureWrapMode::Wrap};
    bool m_useColorCodedMips{false};
    bool m_orbitPaused{false};
    float m_orbitTime{0.0f};
};

} // namespace Neleo
