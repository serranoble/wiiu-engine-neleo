#include "Scene/SceneQA_LightingPBR.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cmath>
#include <cstdio>

namespace Neleo {

namespace {
constexpr uint32_t kShadingModeCount = 4u;
constexpr uint32_t kAlbedoPresetCount = 4u;
constexpr uint32_t kBumpPresetCount = 3u;
} // namespace

void SceneQA_LightingPBR::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 5.6f, 11.8f}, 0.0f, -0.06f);

    m_shadingMode = 0u;
    m_albedoPreset = 0u;
    m_bumpPreset = 1u;
    m_lightsPaused = false;
    m_lightTime = 0.0f;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_sphereMesh, 0.82f, 20u, 14u, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_lightMarkerMesh, 0.22f, 14u, 10u, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_reliefCubeMesh, 2.8f, 2.0f, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreatePlane(m_floorPlane, 36.0f, 36.0f, 8.0f, 8.0f, 2u, 2u, 1.0f, 1.0f, 1.0f);

    m_reliefTexLoaded = Graphics::TextureLoaderGX2::CreateProceduralReliefTexture(
        m_reliefTex, 256u, "QA_G2_ReliefBrick");
}

void SceneQA_LightingPBR::OnQAExit() {
    WaitForGpuIdleSafe();
    if (m_reliefTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_reliefTex);
        m_reliefTexLoaded = false;
    }
    m_sphereMesh.Destroy();
    m_lightMarkerMesh.Destroy();
    m_reliefCubeMesh.Destroy();
    m_floorPlane.Destroy();

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

const char* SceneQA_LightingPBR::GetShadingModeName() const noexcept {
    switch (m_shadingMode) {
        case 0u: return "PBR COOK-TORRANCE (GGX + SMITH + SCHLICK)";
        case 1u: return "BLINN-PHONG CLASICO (CGP CAP. 7)";
        case 2u: return "DEBUG: NORMALES EN MUNDO (TBN)";
        case 3u: return "DEBUG: ROUGHNESS (R) / METALLIC (G)";
        default: return "PBR";
    }
}

const char* SceneQA_LightingPBR::GetAlbedoPresetName() const noexcept {
    switch (m_albedoPreset) {
        case 0u: return "ORO / COBRE";
        case 1u: return "ACERO / BLANCO";
        case 2u: return "CARMESI";
        case 3u: return "AZUL COBALTO";
        default: return "ORO";
    }
}

void SceneQA_LightingPBR::GetAlbedoRGB(float& outR, float& outG, float& outB) const noexcept {
    switch (m_albedoPreset) {
        case 0u: outR = 0.96f; outG = 0.76f; outB = 0.34f; break; // Oro
        case 1u: outR = 0.92f; outG = 0.93f; outB = 0.95f; break; // Acero / Blanco
        case 2u: outR = 0.90f; outG = 0.22f; outB = 0.20f; break; // Carmesí
        case 3u: outR = 0.24f; outG = 0.56f; outB = 0.96f; break; // Azul Cobalto
        default: outR = 0.96f; outG = 0.76f; outB = 0.34f; break;
    }
}

void SceneQA_LightingPBR::OnQAUpdate(float dt) {
    if (!m_lightsPaused) {
        m_lightTime += dt;
    }

    bool modePrev = false;
    bool modeNext = false;
    bool albPrev = false;
    bool albNext = false;
    bool cycleBump = false;
    bool togglePause = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  modePrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) modeNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    albPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  albNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         cycleBump = true;
        if (pad.WasJustPressed(VirtualButton::A))         togglePause = true;
    }

    if (modePrev || modeNext) {
        AudioManager::Instance().PlayClick();
        m_shadingMode = modePrev ? ((m_shadingMode + kShadingModeCount - 1u) % kShadingModeCount)
                                 : ((m_shadingMode + 1u) % kShadingModeCount);
    }
    if (albPrev || albNext) {
        AudioManager::Instance().PlayClick();
        m_albedoPreset = albPrev ? ((m_albedoPreset + kAlbedoPresetCount - 1u) % kAlbedoPresetCount)
                                 : ((m_albedoPreset + 1u) % kAlbedoPresetCount);
    }
    if (cycleBump) {
        AudioManager::Instance().PlayClick();
        m_bumpPreset = (m_bumpPreset + 1u) % kBumpPresetCount;
    }
    if (togglePause) {
        AudioManager::Instance().PlayClick();
        m_lightsPaused = !m_lightsPaused;
    }
}

void SceneQA_LightingPBR::OnQARender3D(const Matrix4x4& /*viewMatrix*/, const Matrix4x4& /*projMatrix*/) {
    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    // 1. Configurar Luz Direccional suave + 4 Luces Puntuales orbitando + 1 Spot Light barriendo
    lights.SetDirectionalLight(Point3D{0.45f, 0.72f, 0.52f}, Point3D{0.65f, 0.68f, 0.75f}, 32.0f);
    lights.ClearPointLights();

    const float t = m_lightTime;
    const Point3D pPos[4] = {
        Point3D{std::cos(t * 0.9f) * 6.5f, 7.6f + std::sin(t * 1.3f) * 1.4f, 1.2f},
        Point3D{std::sin(t * 0.75f) * 6.5f, 3.4f + std::cos(t * 1.1f) * 1.2f, 1.5f},
        Point3D{-7.8f + std::cos(t * 1.2f) * 1.8f, 4.8f, -1.5f + std::sin(t * 1.2f) * 2.2f},
        Point3D{ 7.8f + std::sin(t * 1.2f) * 1.8f, 4.8f, -1.5f + std::cos(t * 1.2f) * 2.2f}
    };
    const Point3D pCol[4] = {
        Point3D{1.00f, 0.82f, 0.45f}, // Cálida dorada
        Point3D{0.30f, 0.88f, 1.00f}, // Cian
        Point3D{1.00f, 0.35f, 0.75f}, // Magenta (sobre pilar TBN izquierdo)
        Point3D{0.35f, 1.00f, 0.58f}  // Esmeralda (sobre pilar TBN derecho)
    };
    for (uint32_t i = 0u; i < 4u; ++i) {
        lights.AddPointLight(pPos[i], 14.0f, pCol[i], 14.0f);
    }

    // Spot Light (linterna con cono suave inner=14°, outer=26°) barriendo la matriz PBR y el suelo
    const Point3D spotPos{std::sin(t * 0.55f) * 4.5f, 9.5f, 6.5f};
    const Point3D spotTarget{std::sin(t * 0.85f) * 5.2f, 3.2f, -3.5f};
    lights.SetSpotLight(spotPos, spotTarget - spotPos, Point3D{1.0f, 0.98f, 0.88f},
                        22.0f, 28.0f, 0.965f, 0.895f);

    const float bumpScale = (m_bumpPreset == 0u) ? 0.0f : ((m_bumpPreset == 1u) ? 2.5f : 5.0f);
    const bool bumpOn = (m_bumpPreset > 0u);

    // 2. Suelo con textura procedural de mampostería para mostrar el cono del Spot Light y relieve TBN
    MaterialPropertyBlock floorMat{};
    floorMat.diffuseTexture = m_reliefTexLoaded ? static_cast<void*>(&m_reliefTex) : nullptr;
    floorMat.ubo.materialParams[1] = bumpOn ? 1.0f : 0.0f;
    floorMat.ubo.materialParams[2] = bumpScale;
    floorMat.ubo.materialParams[3] = 0.25f;
    floorMat.ubo.pbrParams[0] = 0.65f; // roughness
    floorMat.ubo.pbrParams[1] = 0.05f; // dielectrico
    renderer.SetDepthPrePassEnabled(true);
    renderer.SubmitStaticMeshPBR(m_floorPlane, Matrix4x4::Translation(0.0f, 0.0f, -4.0f), floorMat, m_shadingMode);

    // 3. Dos cubos laterales de prueba de Normal / Bump Mapping TBN bajo luces rasantes
    MaterialPropertyBlock reliefMat = floorMat;
    reliefMat.ubo.pbrParams[0] = 0.42f;
    reliefMat.ubo.pbrParams[1] = 0.10f;
    const Quaternion slowSpin = Quaternion::FromAxisAngle(Point3D{0.0f, 1.0f, 0.0f}, t * 0.30f);
    renderer.SubmitStaticMeshPBR(
        m_reliefCubeMesh,
        Matrix4x4::FromTRS(Point3D{-8.2f, 2.2f, -2.8f}, slowSpin, Point3D{1.0f, 1.0f, 1.0f}),
        reliefMat, m_shadingMode);
    renderer.SubmitStaticMeshPBR(
        m_reliefCubeMesh,
        Matrix4x4::FromTRS(Point3D{8.2f, 2.2f, -2.8f}, slowSpin.Conjugate(), Point3D{1.0f, 1.0f, 1.0f}),
        reliefMat, m_shadingMode);

    // 4. Matriz 5x5 de Esferas PBR (Eje X: Roughness 0.08..0.94, Eje Y: Metallic 0.0..1.0)
    float albR = 1.0f, albG = 1.0f, albB = 1.0f;
    GetAlbedoRGB(albR, albG, albB);

    MaterialPropertyBlock sphereMat{};
    sphereMat.ubo.albedoTint[0] = albR;
    sphereMat.ubo.albedoTint[1] = albG;
    sphereMat.ubo.albedoTint[2] = albB;
    sphereMat.ubo.albedoTint[3] = 1.0f;
    sphereMat.ubo.materialParams[1] = 0.0f; // Esferas lisas para evaluar BRDF puro
    sphereMat.ubo.materialParams[2] = 0.0f;
    sphereMat.ubo.materialParams[3] = 0.28f;

    for (uint32_t row = 0u; row < 5u; ++row) {
        const float metallic = static_cast<float>(row) / 4.0f;
        const float py = 1.8f + static_cast<float>(row) * 2.0f;
        for (uint32_t col = 0u; col < 5u; ++col) {
            const float roughness = 0.08f + (static_cast<float>(col) / 4.0f) * 0.86f;
            const float px = -4.2f + static_cast<float>(col) * 2.1f;

            sphereMat.ubo.pbrParams[0] = roughness;
            sphereMat.ubo.pbrParams[1] = metallic;
            renderer.SubmitStaticMeshPBR(
                m_sphereMesh,
                Matrix4x4::Translation(px, py, -4.0f),
                sphereMat, m_shadingMode);
        }
    }

    // 5. Marcadores visuales de las 4 luces puntuales orbitando
    for (uint32_t i = 0u; i < 4u; ++i) {
        MaterialPropertyBlock markerMat{};
        markerMat.ubo.albedoTint[0] = pCol[i].x;
        markerMat.ubo.albedoTint[1] = pCol[i].y;
        markerMat.ubo.albedoTint[2] = pCol[i].z;
        markerMat.ubo.materialParams[3] = 1.0f;
        markerMat.ubo.pbrParams[0] = 0.1f;
        markerMat.ubo.pbrParams[1] = 0.0f;
        renderer.SubmitStaticMeshPBR(
            m_lightMarkerMesh,
            Matrix4x4::Translation(pPos[i].x, pPos[i].y, pPos[i].z),
            markerMat, 1u);
    }
}

void SceneQA_LightingPBR::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 768, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    const char* bumpLabel = (m_bumpPreset == 0u) ? "OFF (0.0)" : ((m_bumpPreset == 1u) ? "MEDIO (2.5)" : "ALTO (5.0)");
    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), "SHADING : %s", GetShadingModeName());
    std::snprintf(line2, sizeof(line2), "ALBEDO  : %s | BUMP TBN: %s | LUCES: %s",
                  GetAlbedoPresetName(), bumpLabel, m_lightsPaused ? "PAUSA" : "ORBITA");
    std::snprintf(line3, sizeof(line3), "MATRIZ 5x5: X=ROUGHNESS (0.08->0.94) | Y=METALLIC (0.0->1.0)");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString("LUCES ACTIVAS: 1 DIRECCIONAL + 4 PUNTUALES + 1 SPOT LIGHT",
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
