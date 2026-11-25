#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace WiiU {

/**
 * @brief Escena de validación del Asset Cooker para modelos estáticos (.wmesh 48B + .wtex).
 *
 * Carga por separado la casa holandesa cocinada (dutch_house, props y vegetación alpha-cutout)
 * sobre un suelo procedural para verificar fidelidad de geometría, UVs, normales, alpha-cutout
 * y rendimiento en Cemu y Nintendo Wii U real.
 */
class SceneModelHouse final : public GraphicsQASceneBase {
public:
    SceneModelHouse() noexcept = default;
    ~SceneModelHouse() override { OnQAExit(); }

    void LoadAsync() override;
    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Validacion Cooker: Casa Estatica";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "COOKER ESTATICO"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "VALIDACION DE MODELO ESTATICO COCINADO (DUTCH HOUSE .WMESH + .WTEX)";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:LUZ  A:GIRO  X:PROPS  -:TEXTURAS ON/OFF  L/R-STICK:CAM  B:MENU";
    }

private:
    void FreeStagedBuffers() noexcept;

    std::vector<uint8_t> m_stagedHouseMesh;
    std::vector<uint8_t> m_stagedPropsMesh;
    std::vector<uint8_t> m_stagedAlphaMesh;
    std::vector<uint8_t> m_stagedHouseTex;
    std::vector<uint8_t> m_stagedPropsTex;
    std::vector<uint8_t> m_stagedAlphaTex;

    StaticMesh m_floorMesh;
    StaticMesh m_houseMesh;
    StaticMesh m_propsMesh;
    StaticMesh m_alphaMesh;

    GX2Texture m_houseTex{};
    GX2Texture m_propsTex{};
    GX2Texture m_alphaTex{};

    bool m_houseMeshLoaded{false};
    bool m_propsMeshLoaded{false};
    bool m_alphaMeshLoaded{false};
    bool m_houseTexLoaded{false};
    bool m_propsTexLoaded{false};
    bool m_alphaTexLoaded{false};

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    uint32_t m_lightPreset{1u}; // 0=Mañana, 1=Mediodía, 2=Atardecer
    bool m_autoRotate{true};
    bool m_showProps{true};
    bool m_useTextures{true};
    float m_turntableAngle{0.0f};
};

} // namespace WiiU
