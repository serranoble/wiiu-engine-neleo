#pragma once

#include "Scene/GraphicsQASceneBase.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/ParticleSystem3D.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include <cstdint>
#include <string>

namespace WiiU {

/**
 * @brief Suite G7: Validación de Terreno Heightmap con Splatting (arena/pasto/roca/nieve),
 *        Ruido 3D Procedural (Mármol y Madera), Sistema de Partículas 3D Zero-Alloc (fuego/humo)
 *        y Renderizado Estilizado Toon / Cel Shading NPR con contorno (outline).
 *
 * Referencias:
 * - CGP (Gordon & Clevenger, 3ª Ed.): Cap. 12 (Terreno/Heightmap), Cap. 14 (Ruido 3D procedural)
 * - RTR4 (Akenine-Möller et al., 4ª Ed.): Cap. 13 (Billboards/Partículas), Cap. 15 (NPR Toon/Outline)
 */
class SceneQA_ProceduralNPR final : public GraphicsQASceneBase {
public:
    SceneQA_ProceduralNPR() noexcept = default;
    ~SceneQA_ProceduralNPR() override { OnQAExit(); }

    [[nodiscard]] std::string GetSceneName() const override {
        return "QA G7: Terreno, Ruido 3D, Particulas y Toon";
    }

protected:
    void OnQAEnter() override;
    void OnQAExit() override;
    void OnQAUpdate(float dt) override;
    void OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) override;
    void OnQARenderHUD() override;

    [[nodiscard]] const char* GetSuiteCode() const noexcept override { return "SUITE G7"; }
    [[nodiscard]] const char* GetSuiteTitle() const noexcept override {
        return "TERRENO HEIGHTMAP SPLATTING, RUIDO 3D (MARMOL/MADERA), PARTICULAS Y TOON NPR";
    }
    [[nodiscard]] const char* GetControlsHint() const noexcept override {
        return "DPAD IZQ/DER:ESTILO (REAL/TOON)  DPAD ARR/ABJ:RUIDO 3D  X:PARTICULAS  A:PAUSA SOL";
    }

private:
    [[nodiscard]] const char* GetNprModeLabel() const noexcept;
    [[nodiscard]] const char* GetMaterialPresetLabel() const noexcept;

    StaticMesh m_terrainMesh;
    StaticMesh m_sphereMesh;
    StaticMesh m_pillarMesh;
    StaticMesh m_particleQuadMesh;

    Graphics::ParticleSystem3D m_particles;

    LightBlockUBO m_savedLightBlock{};
    bool m_lightBlockSaved{false};

    uint32_t m_nprMode{1u};        // 0=Realista, 1=Toon+Outline, 2=Toon sin Outline
    uint32_t m_materialPreset{0u}; // 0=Mix (Mármol+Madera+Toon), 1=Todo Mármol 3D, 2=Todo Madera 3D
    bool m_particlesEnabled{true};
    bool m_sunPaused{false};
    float m_sunTime{0.0f};
};

} // namespace WiiU
