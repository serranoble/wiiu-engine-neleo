#include "Scene/SceneQA_PostProcessAlpha.hpp"
#include "Core/Window.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/context.h>
#include <gx2/surface.h>
#endif

namespace Neleo {

namespace {
constexpr uint32_t kToneMapCount = 3u;
constexpr uint32_t kFogModeCount = 4u;
} // namespace

void SceneQA_PostProcessAlpha::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 3.2f, 11.8f}, 0.0f, -0.14f);

    m_settings = Graphics::PostProcessHDRSettings{};
    m_settings.toneMap = Graphics::ToneMapOperator::ACES;
    m_settings.exposure = 1.05f;
    m_settings.bloomThreshold = 1.00f;
    m_settings.bloomIntensity = 0.90f;
    m_settings.vignetteStrength = 0.35f;
    m_settings.bloomEnabled = true;
    m_settings.gammaEnabled = true;

    m_fogMode = 2u; // Exp por defecto
    m_animTime = 0.0f;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    // Geometría en VRAM
    (void)Graphics::ProceduralMeshBuilder::CreatePlane(m_floorMesh, 64.0f, 64.0f, 12.0f, 12.0f, 8u, 8u, 0.24f, 0.26f, 0.30f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_pillarMesh, 1.0f, 1.0f, 0.42f, 0.44f, 0.50f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_orbMesh, 0.72f, 28u, 20u, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_panelMesh, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    m_cutoutTexLoaded = Graphics::TextureLoaderGX2::CreateProceduralAlphaCutoutTexture(
        m_cutoutTex, 128u, "QA_G5_AlphaCutout");

    (void)m_postFX.Initialize(1280u, 720u);
}

void SceneQA_PostProcessAlpha::OnQAExit() {
    WaitForGpuIdleSafe();
    m_postFX.Shutdown();
    if (m_cutoutTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_cutoutTex);
        m_cutoutTexLoaded = false;
    }
    m_floorMesh.Destroy();
    m_pillarMesh.Destroy();
    m_orbMesh.Destroy();
    m_panelMesh.Destroy();

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

const char* SceneQA_PostProcessAlpha::GetToneMapLabel() const noexcept {
    switch (m_settings.toneMap) {
        case Graphics::ToneMapOperator::NoneClamp: return "OFF (CLAMP)";
        case Graphics::ToneMapOperator::Reinhard:  return "REINHARD";
        case Graphics::ToneMapOperator::ACES:      return "ACES FILMIC";
        default: return "ACES FILMIC";
    }
}

const char* SceneQA_PostProcessAlpha::GetFogLabel() const noexcept {
    switch (m_fogMode) {
        case 0u: return "APAGADA";
        case 1u: return "LINEAL (6M..38M)";
        case 2u: return "EXPONENCIAL (EXP)";
        case 3u: return "EXP CUADRATICA (EXP2)";
        default: return "APAGADA";
    }
}

void SceneQA_PostProcessAlpha::OnQAUpdate(float dt) {
    m_animTime += dt;

    bool tmPrev = false;
    bool tmNext = false;
    bool fogPrev = false;
    bool fogNext = false;
    bool toggleBloom = false;
    bool toggleGamma = false;
    bool expDown = false;
    bool expUp = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  tmPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) tmNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    fogPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  fogNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleBloom = true;
        if (pad.WasJustPressed(VirtualButton::A))         toggleGamma = true;
        if (pad.WasJustPressed(VirtualButton::L))         expDown = true;
        if (pad.WasJustPressed(VirtualButton::R))         expUp = true;
    }

    if (tmPrev || tmNext) {
        AudioManager::Instance().PlayClick();
        uint32_t cur = static_cast<uint32_t>(m_settings.toneMap);
        cur = tmPrev ? ((cur + kToneMapCount - 1u) % kToneMapCount)
                     : ((cur + 1u) % kToneMapCount);
        m_settings.toneMap = static_cast<Graphics::ToneMapOperator>(cur);
    }
    if (fogPrev || fogNext) {
        AudioManager::Instance().PlayClick();
        m_fogMode = fogPrev ? ((m_fogMode + kFogModeCount - 1u) % kFogModeCount)
                            : ((m_fogMode + 1u) % kFogModeCount);
    }
    if (toggleBloom) {
        AudioManager::Instance().PlayClick();
        m_settings.bloomEnabled = !m_settings.bloomEnabled;
    }
    if (toggleGamma) {
        AudioManager::Instance().PlayClick();
        m_settings.gammaEnabled = !m_settings.gammaEnabled;
    }
    if (expDown) {
        AudioManager::Instance().PlayClick();
        m_settings.exposure = std::clamp(m_settings.exposure - 0.15f, 0.45f, 2.25f);
    }
    if (expUp) {
        AudioManager::Instance().PlayClick();
        m_settings.exposure = std::clamp(m_settings.exposure + 0.15f, 0.45f, 2.25f);
    }
}

void SceneQA_PostProcessAlpha::OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    const Point3D& cam = GetCameraPos();

    // Posiciones animadas de los 3 Orbes Emisivos HDR (Cian, Magenta, Dorado)
    const float bobA = std::sin(m_animTime * 1.6f) * 0.35f;
    const float bobB = std::cos(m_animTime * 1.9f) * 0.35f;
    const float bobC = std::sin(m_animTime * 1.3f + 1.4f) * 0.35f;

    const Point3D orbPos[3] = {
        Point3D{-4.2f, 2.4f + bobA, -3.5f},
        Point3D{ 0.0f, 3.0f + bobB, -5.2f},
        Point3D{ 4.2f, 2.4f + bobC, -3.5f}
    };
    const float orbCol[3][3] = {
        {0.18f, 0.88f, 1.00f}, // Cian neón
        {1.00f, 0.22f, 0.78f}, // Magenta neón
        {1.00f, 0.78f, 0.20f}  // Dorado cálido
    };

    lights.SetDirectionalLight(Point3D{0.35f, 0.75f, 0.55f}, Point3D{0.55f, 0.62f, 0.78f}, 32.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();
    for (uint32_t i = 0u; i < 3u; ++i) {
        (void)lights.AddPointLight(orbPos[i], 14.0f, Point3D{orbCol[i][0], orbCol[i][1], orbCol[i][2]}, 4.5f);
    }

    const float fogModeF = static_cast<float>(m_fogMode);
    const float fogDensity = (m_fogMode == 3u) ? 0.042f : 0.048f;

    bool hdrPassActive = false;
    if (m_postFX.IsReady() && m_postFX.BeginHDRScenePass(0.025f, 0.040f, 0.070f, 1.0f)) {
        renderer.BeginFrame(cam, viewMatrix, projMatrix);
        hdrPassActive = true;
    }

    // 1. PASE OPACO: Suelo + Columnas arquitectónicas (Modo 7 HDR lineal + Niebla)
    {
        MaterialPropertyBlock floorMat{};
        floorMat.ubo.albedoTint[0] = 0.36f; floorMat.ubo.albedoTint[1] = 0.38f; floorMat.ubo.albedoTint[2] = 0.44f;
        floorMat.ubo.materialParams[0] = 0.0f; // alphaCutout OFF
        floorMat.ubo.materialParams[1] = fogModeF;
        floorMat.ubo.materialParams[2] = fogDensity;
        floorMat.ubo.materialParams[3] = 1.0f; // alpha=1
        floorMat.ubo.pbrParams[0] = 0.45f;
        floorMat.ubo.pbrParams[3] = 0.0f; // emissive OFF
        renderer.SubmitStaticMeshPBR(m_floorMesh, Matrix4x4::Translation(0.0f, 0.0f, 0.0f), floorMat, 7u);

        const float pillarX[4] = {-6.5f, -2.5f, 2.5f, 6.5f};
        const float pillarZ[2] = {-7.5f, -14.5f};
        MaterialPropertyBlock pillarMat = floorMat;
        pillarMat.ubo.albedoTint[0] = 0.50f; pillarMat.ubo.albedoTint[1] = 0.52f; pillarMat.ubo.albedoTint[2] = 0.58f;
        pillarMat.ubo.pbrParams[0] = 0.60f;
        for (uint32_t rz = 0u; rz < 2u; ++rz) {
            for (uint32_t rx = 0u; rx < 4u; ++rx) {
                const Matrix4x4 m = Matrix4x4::Translation(pillarX[rx], 2.4f, pillarZ[rz]) *
                                    Matrix4x4::Scale(0.95f, 4.8f, 0.95f);
                renderer.SubmitStaticMeshPBR(m_pillarMesh, m, pillarMat, 7u);
            }
        }
    }

    // 2. EMISIVOS HDR (> 1.0): 3 Orbes de alta luminancia (5.8x) para Bloom + Tone Mapping ACES/Reinhard
    for (uint32_t i = 0u; i < 3u; ++i) {
        MaterialPropertyBlock emMat{};
        emMat.ubo.albedoTint[0] = orbCol[i][0];
        emMat.ubo.albedoTint[1] = orbCol[i][1];
        emMat.ubo.albedoTint[2] = orbCol[i][2];
        emMat.ubo.materialParams[0] = 0.0f;
        emMat.ubo.materialParams[1] = fogModeF;
        emMat.ubo.materialParams[2] = fogDensity;
        emMat.ubo.materialParams[3] = 1.0f;
        emMat.ubo.pbrParams[3] = 5.8f; // Emisivo HDR > 1.0
        renderer.SubmitStaticMeshPBR(m_orbMesh, Matrix4x4::Translation(orbPos[i].x, orbPos[i].y, orbPos[i].z), emMat, 7u);
    }

    // 3. ALPHA-CUTOUT (discard): 4 Celosías con follaje recortado por canal Alpha
    if (m_cutoutTexLoaded) {
        const float cutX[4] = {-5.4f, -2.2f, 2.2f, 5.4f};
        MaterialPropertyBlock cutMat{};
        cutMat.diffuseTexture = &m_cutoutTex;
        cutMat.ubo.albedoTint[0] = 1.0f; cutMat.ubo.albedoTint[1] = 1.0f; cutMat.ubo.albedoTint[2] = 1.0f;
        cutMat.ubo.materialParams[0] = 1.0f; // alphaCutout ON (discard si a < 0.5)
        cutMat.ubo.materialParams[1] = fogModeF;
        cutMat.ubo.materialParams[2] = fogDensity;
        cutMat.ubo.materialParams[3] = 1.0f;
        cutMat.ubo.pbrParams[0] = 0.55f;
        cutMat.ubo.pbrParams[3] = 0.0f;

        for (uint32_t i = 0u; i < 4u; ++i) {
            const float z = (i == 0u || i == 3u) ? -0.4f : -1.8f;
            const Matrix4x4 m = Matrix4x4::Translation(cutX[i], 1.45f, z) *
                                Matrix4x4::Scale(2.1f, 2.6f, 0.06f);
            renderer.SubmitStaticMeshPBR(m_panelMesh, m, cutMat, 7u);
        }
    }

    // 4. VITRALES TRANSLÚCIDOS: ordenamiento Back-to-Front delegado 100% a RenderQueue del motor [OPT-459]
    {
        struct GlassPanel {
            Point3D pos;
            float rgb[3];
            float alpha;
        };
        const GlassPanel panels[3] = {
            {Point3D{-0.65f, 1.85f,  2.2f}, {0.95f, 0.22f, 0.25f}, 0.42f}, // Rojo
            {Point3D{ 0.00f, 1.95f,  0.6f}, {0.20f, 0.92f, 0.42f}, 0.42f}, // Verde
            {Point3D{ 0.65f, 2.05f, -1.0f}, {0.22f, 0.56f, 0.98f}, 0.42f}  // Azul
        };

        for (uint32_t k = 0u; k < 3u; ++k) {
            const GlassPanel& gp = panels[k];
            MaterialPropertyBlock glassMat{};
            glassMat.blendMode = BlendMode::AlphaBlend;
            glassMat.depthTest = true;
            glassMat.depthWrite = false;
            glassMat.ubo.albedoTint[0] = gp.rgb[0];
            glassMat.ubo.albedoTint[1] = gp.rgb[1];
            glassMat.ubo.albedoTint[2] = gp.rgb[2];
            glassMat.ubo.materialParams[0] = 0.0f;
            glassMat.ubo.materialParams[1] = fogModeF;
            glassMat.ubo.materialParams[2] = fogDensity;
            glassMat.ubo.materialParams[3] = gp.alpha;
            glassMat.ubo.pbrParams[0] = 0.22f;
            glassMat.ubo.pbrParams[3] = 0.35f; // Leve auto-luminiscencia de vitral retroiluminado
            const Matrix4x4 m = Matrix4x4::Translation(gp.pos.x, gp.pos.y, gp.pos.z) *
                                Matrix4x4::Scale(2.6f, 2.9f, 0.06f);
            renderer.SubmitStaticMeshPBR(m_panelMesh, m, glassMat, 7u);
        }
    }

    // Despachar cola ordenada (Opacos Front-to-Back -> Cutout -> Vitrales Back-to-Front) antes de cerrar FBO HDR
    renderer.FlushRenderQueue();

    // 5. RESOLVE HDR -> BLOOM MULTIPASE (BrightPass + BlurH + BlurV) -> COMPOSITE TV (1280x720)
    if (hdrPassActive) {
#if defined(__WIIU__) || defined(__wiiu__)
        m_postFX.EndHDRScenePassAndCompose(
            m_settings,
            static_cast<GX2ContextState*>(Window::GetActiveNativeContextState()),
            static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer()),
            static_cast<GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer()),
            1280u, 720u);
#else
        m_postFX.EndHDRScenePassAndCompose(m_settings, nullptr, nullptr, nullptr, 1280u, 720u);
#endif
    }
}

void SceneQA_PostProcessAlpha::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 1020, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), "TONEMAP : %s | EXPOSICION: %.2fX | GAMMA sRGB: %s",
                  GetToneMapLabel(), static_cast<double>(m_settings.exposure),
                  m_settings.gammaEnabled ? "ON" : "OFF (LINEAL)");
    std::snprintf(line2, sizeof(line2), "BLOOM   : %s (320x192 KAWASE 2D 5-TAP) | NIEBLA: %s",
                  m_settings.bloomEnabled ? "ACTIVO" : "APAGADO", GetFogLabel());
    std::snprintf(line3, sizeof(line3), "ESCENA  : 3 ORBES HDR 5.8X | 4 CELOSIAS ALPHA-CUTOUT | 3 VITRALES BACK-TO-FRONT");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString("CONSEJO: COMPARA ACES VS OFF(CLAMP) Y GIRA DETRAS DE LOS VITRALES PARA VER SORTING",
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
