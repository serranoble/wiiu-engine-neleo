#include "Scene/SceneQA_Shadows.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Core/Window.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cmath>
#include <cstdio>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/context.h>
#include <gx2/surface.h>
#endif

namespace Neleo {

namespace {
constexpr uint32_t kShadowModeCount = 5u;
constexpr uint32_t kBiasPresetCount = 3u;
} // namespace

void SceneQA_Shadows::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 7.5f, 15.5f}, 0.0f, -0.34f);

    m_shadowMode = 2u;       // PCF 3x3 por defecto
    m_biasPreset = 1u;       // Bias óptimo
    m_cullFrontFaces = true;
    m_sunPaused = false;
    m_sunTime = 0.45f;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    (void)ShadowMapRendererGX2::Instance().Initialize(ShadowMapRendererGX2::DEFAULT_RESOLUTION);

    // Escenario arquitectónico de alto contraste para sombras
    (void)Graphics::ProceduralMeshBuilder::CreatePlane(m_floorMesh, 34.0f, 34.0f, 8.0f, 8.0f, 2u, 2u, 0.92f, 0.92f, 0.94f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_pillarMesh, 1.0f, 1.0f, 0.88f, 0.86f, 0.82f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_lintelMesh, 1.0f, 1.0f, 0.82f, 0.78f, 0.72f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_floatingBoxMesh, 2.0f, 1.0f, 0.95f, 0.58f, 0.24f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_centerSphereMesh, 1.45f, 32u, 22u, 0.30f, 0.72f, 0.96f);

    m_brickTexLoaded = Graphics::TextureLoaderGX2::CreateProceduralReliefTexture(
        m_brickTex, 256u, "QA_G3_ShadowPatio");
}

void SceneQA_Shadows::OnQAExit() {
    WaitForGpuIdleSafe();
    if (m_brickTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_brickTex);
        m_brickTexLoaded = false;
    }
    m_floorMesh.Destroy();
    m_pillarMesh.Destroy();
    m_lintelMesh.Destroy();
    m_floatingBoxMesh.Destroy();
    m_centerSphereMesh.Destroy();

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

const char* SceneQA_Shadows::GetShadowModeName() const noexcept {
    switch (m_shadowMode) {
        case 0u: return "OFF (SIN SOMBRAS PROYECTADAS)";
        case 1u: return "HARD SHADOW MAP (1-TAP)";
        case 2u: return "PCF 3x3 SUAVE (9-TAPS)";
        case 3u: return "PCF 5x5 SUAVE (25-TAPS)";
        case 4u: return "DEBUG: MAPA DE SOMBRA (LUZ/SOMBRA)";
        default: return "PCF 3x3";
    }
}

const char* SceneQA_Shadows::GetBiasPresetName() const noexcept {
    switch (m_biasPreset) {
        case 0u: return "CERO BIAS (0.000 -> DEMUESTRA SHADOW ACNE)";
        case 1u: return "OPTIMO (0.002 + SLOPE BIAS)";
        case 2u: return "EXCESIVO (0.038 -> DEMUESTRA PETER PANNING)";
        default: return "OPTIMO";
    }
}

void SceneQA_Shadows::OnQAUpdate(float dt) {
    if (!m_sunPaused) {
        m_sunTime += dt * 0.45f;
    }

    bool modePrev = false;
    bool modeNext = false;
    bool biasPrev = false;
    bool biasNext = false;
    bool toggleCull = false;
    bool togglePause = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  modePrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) modeNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    biasPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  biasNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleCull = true;
        if (pad.WasJustPressed(VirtualButton::A))         togglePause = true;
    }

    if (modePrev || modeNext) {
        AudioManager::Instance().PlayClick();
        m_shadowMode = modePrev ? ((m_shadowMode + kShadowModeCount - 1u) % kShadowModeCount)
                                : ((m_shadowMode + 1u) % kShadowModeCount);
    }
    if (biasPrev || biasNext) {
        AudioManager::Instance().PlayClick();
        m_biasPreset = biasPrev ? ((m_biasPreset + kBiasPresetCount - 1u) % kBiasPresetCount)
                                : ((m_biasPreset + 1u) % kBiasPresetCount);
    }
    if (toggleCull) {
        AudioManager::Instance().PlayClick();
        m_cullFrontFaces = !m_cullFrontFaces;
    }
    if (togglePause) {
        AudioManager::Instance().PlayClick();
        m_sunPaused = !m_sunPaused;
    }
}

void SceneQA_Shadows::OnQARender3D(const Matrix4x4& /*viewMatrix*/, const Matrix4x4& /*projMatrix*/) {
    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    // Dirección del Sol rotando en órbita elevada para proyectar sombras dinámicas largas
    const Point3D sunDir = Point3D{
        std::cos(m_sunTime) * 0.72f,
        0.78f,
        std::sin(m_sunTime) * 0.72f
    }.Normalized();

    lights.SetDirectionalLight(sunDir, Point3D{1.0f, 0.96f, 0.88f}, 32.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();

    // Matriz View-Projection ortográfica del Sol cubriendo el patio completo ([-18..+18])
    const Point3D sunEye = sunDir * 24.0f;
    const Matrix4x4 lightView = Matrix4x4::LookAt(sunEye, Point3D{0.0f, 1.5f, 0.0f}, Point3D{0.0f, 1.0f, 0.0f});
    const Matrix4x4 lightProj = Matrix4x4::Orthographic(-18.0f, 18.0f, -18.0f, 18.0f, 1.0f, 52.0f);
    const Matrix4x4 lightVP = lightProj * lightView;

    // Transformaciones de la arquitectura (compartidas entre Shadow Pass y Lit Pass)
    const Matrix4x4 floorModel = Matrix4x4::Identity();
    const Matrix4x4 pillarModels[4] = {
        Matrix4x4::Translation(-5.0f, 2.75f, -4.5f) * Matrix4x4::Scale(1.1f, 5.5f, 1.1f),
        Matrix4x4::Translation( 5.0f, 2.75f, -4.5f) * Matrix4x4::Scale(1.1f, 5.5f, 1.1f),
        Matrix4x4::Translation(-5.0f, 2.75f,  2.5f) * Matrix4x4::Scale(1.1f, 5.5f, 1.1f),
        Matrix4x4::Translation( 5.0f, 2.75f,  2.5f) * Matrix4x4::Scale(1.1f, 5.5f, 1.1f)
    };
    const Matrix4x4 lintelBack = Matrix4x4::Translation(0.0f, 5.8f, -4.5f) * Matrix4x4::Scale(11.6f, 0.9f, 1.4f);
    const Matrix4x4 lintelSideL = Matrix4x4::Translation(-5.0f, 5.8f, -1.0f) * Matrix4x4::Scale(1.3f, 0.85f, 8.2f);
    const Matrix4x4 lintelSideR = Matrix4x4::Translation( 5.0f, 5.8f, -1.0f) * Matrix4x4::Scale(1.3f, 0.85f, 8.2f);

    const Quaternion boxRot = Quaternion::FromAxisAngle(Point3D{0.35f, 1.0f, 0.25f}, m_sunTime * 1.1f);
    const Matrix4x4 floatingBoxModel = Matrix4x4::FromTRS(Point3D{2.6f, 2.1f, -0.4f}, boxRot, Point3D{1.0f, 1.0f, 1.0f});
    const Matrix4x4 sphereModel = Matrix4x4::Translation(-1.4f, 1.45f, -1.0f);

    // Parámetros de Bias (Hardware + Shader) según preset
    const float hwBiasUnits = (m_biasPreset == 0u) ? 0.0f : ((m_biasPreset == 1u) ? 1.5f : 0.0f);
    const float hwBiasSlope = (m_biasPreset == 0u) ? 0.0f : ((m_biasPreset == 1u) ? 1.5f : 0.0f);
    const float shaderBias  = (m_biasPreset == 0u) ? 0.0f : ((m_biasPreset == 1u) ? 0.0022f : 0.038f);

    // =========================================================================
    // PASE 1: SHADOW MAP DEPTH PASS (Z-ONLY EN AMD LATTE GX2 / OPENGL 3.3)
    // =========================================================================
    auto& shadowMgr = ShadowMapRendererGX2::Instance();
    if (m_shadowMode > 0u && shadowMgr.IsReady()) {
#if defined(__WIIU__) || defined(__wiiu__)
        auto* ctx = static_cast<GX2ContextState*>(Window::GetActiveNativeContextState());
        auto* mainColor = static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
        auto* mainDepth = static_cast<GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer());
        const uint32_t restoreW = mainColor ? mainColor->surface.width : 1280u;
        const uint32_t restoreH = mainColor ? mainColor->surface.height : 720u;
#else
        void* ctx = nullptr;
        void* mainColor = nullptr;
        void* mainDepth = nullptr;
        const uint32_t restoreW = 1280u;
        const uint32_t restoreH = 720u;
#endif
        if (shadowMgr.BeginShadowPass(lightVP, ctx, m_cullFrontFaces, hwBiasUnits, hwBiasSlope)) {
            for (uint32_t i = 0u; i < 4u; ++i) {
                shadowMgr.RenderCaster(m_pillarMesh, pillarModels[i]);
            }
            shadowMgr.RenderCaster(m_lintelMesh, lintelBack);
            shadowMgr.RenderCaster(m_lintelMesh, lintelSideL);
            shadowMgr.RenderCaster(m_lintelMesh, lintelSideR);
            shadowMgr.RenderCaster(m_floatingBoxMesh, floatingBoxModel);
            shadowMgr.RenderCaster(m_centerSphereMesh, sphereModel);
            shadowMgr.EndShadowPass(ctx, mainColor, mainDepth, restoreW, restoreH);
        }
    }

    // =========================================================================
    // PASE 2: RENDERIZADO PRINCIPAL PBR + RECEPCIÓN DE SOMBRAS
    // =========================================================================
    const float res = static_cast<float>(ShadowMapRendererGX2::Instance().GetResolution());
    renderer.SetDirectionalShadowState(lightVP, m_shadowMode, shaderBias, (res > 0.0f) ? (1.0f / res) : (1.0f / 1024.0f), 0.14f);

    MaterialPropertyBlock floorMat{};
    floorMat.diffuseTexture = m_brickTexLoaded ? static_cast<void*>(&m_brickTex) : nullptr;
    floorMat.ubo.materialParams[1] = 1.0f;
    floorMat.ubo.materialParams[2] = 1.8f;
    floorMat.ubo.materialParams[3] = 0.28f;
    floorMat.ubo.pbrParams[0] = 0.78f;
    floorMat.ubo.pbrParams[1] = 0.02f;
    renderer.RenderStaticMeshPBR(m_floorMesh, floorModel, floorMat, 0u);

    MaterialPropertyBlock stoneMat{};
    stoneMat.ubo.materialParams[1] = 0.0f;
    stoneMat.ubo.materialParams[3] = 0.30f;
    stoneMat.ubo.pbrParams[0] = 0.65f;
    stoneMat.ubo.pbrParams[1] = 0.05f;
    for (uint32_t i = 0u; i < 4u; ++i) {
        renderer.RenderStaticMeshPBR(m_pillarMesh, pillarModels[i], stoneMat, 0u);
    }
    renderer.RenderStaticMeshPBR(m_lintelMesh, lintelBack, stoneMat, 0u);
    renderer.RenderStaticMeshPBR(m_lintelMesh, lintelSideL, stoneMat, 0u);
    renderer.RenderStaticMeshPBR(m_lintelMesh, lintelSideR, stoneMat, 0u);

    MaterialPropertyBlock boxMat{};
    boxMat.ubo.materialParams[1] = 0.0f;
    boxMat.ubo.materialParams[3] = 0.28f;
    boxMat.ubo.pbrParams[0] = 0.35f;
    boxMat.ubo.pbrParams[1] = 0.25f;
    renderer.RenderStaticMeshPBR(m_floatingBoxMesh, floatingBoxModel, boxMat, 0u);

    MaterialPropertyBlock sphereMat{};
    sphereMat.ubo.materialParams[1] = 0.0f;
    sphereMat.ubo.materialParams[3] = 0.28f;
    sphereMat.ubo.pbrParams[0] = 0.22f;
    sphereMat.ubo.pbrParams[1] = 0.65f;
    renderer.RenderStaticMeshPBR(m_centerSphereMesh, sphereModel, sphereMat, 0u);
}

void SceneQA_Shadows::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 776, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), NELEO_LOC_CSTR("qa.g3.line1"), GetShadowModeName());
    std::snprintf(line2, sizeof(line2), NELEO_LOC_CSTR("qa.g3.line2"), GetBiasPresetName());
    std::snprintf(line3, sizeof(line3), NELEO_LOC_CSTR("qa.g3.line3"),
                  ShadowMapRendererGX2::Instance().GetResolution(),
                  ShadowMapRendererGX2::Instance().GetResolution(),
                  m_cullFrontFaces ? "ON" : "OFF",
                  m_sunPaused ? NELEO_LOC_CSTR("common.paused") : NELEO_LOC_CSTR("qa.g3.rotating"));

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString(NELEO_LOC("qa.g3.tip"),
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
