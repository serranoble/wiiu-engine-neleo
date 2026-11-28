#include "Scene/SceneQA_EnvironmentWater.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Core/Window.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
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
constexpr uint32_t kIORPresetCount = 4u;
} // namespace

void SceneQA_EnvironmentWater::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 3.6f, 11.2f}, 0.0f, -0.14f);

    m_iblEnabled = true;
    m_useProceduralSky = false;
    m_wavesPaused = false;
    m_iorPreset = 2u; // 1.52 Vidrio
    m_waterTime = 0.0f;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    // Geometría en VRAM
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_skyboxCubeMesh, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreatePlane(m_waterPlaneMesh, 140.0f, 140.0f, 1.0f, 1.0f, 12u, 12u, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_sphereMesh, 1.20f, 36u, 24u, 1.0f, 1.0f, 1.0f);

    // Render Target off-screen 640x360 para Reflexión Planar real del agua (CGP Cap. 15)
    (void)m_reflectionRT.Initialize(640u, 360u, "QA_G4_PlanarReflRT");

    // Cargar CubeMap real de día (.wcube -> Atlas 2D 3x2) + generar Atlas 2D procedural de respaldo/comparativa
    m_dayCubeLoaded = Graphics::TextureLoaderGX2::LoadCookedCubeMapAsAtlasFromFile(
        "models/skybox_day.wcube", m_dayCubeMap, "QA_G4_DayCubeAtlas");
    m_procCubeLoaded = Graphics::TextureLoaderGX2::CreateProceduralSkyCubeAtlas(
        m_procCubeMap, 256u, "QA_G4_ProcSkyAtlas");

    if (!m_dayCubeLoaded) {
        m_useProceduralSky = true;
    }
}

void SceneQA_EnvironmentWater::OnQAExit() {
    WaitForGpuIdleSafe();
    m_reflectionRT.Shutdown();
    if (m_dayCubeLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_dayCubeMap);
        m_dayCubeLoaded = false;
    }
    if (m_procCubeLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_procCubeMap);
        m_procCubeLoaded = false;
    }
    m_skyboxCubeMesh.Destroy();
    m_waterPlaneMesh.Destroy();
    m_sphereMesh.Destroy();

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

float SceneQA_EnvironmentWater::GetCurrentIOR() const noexcept {
    switch (m_iorPreset) {
        case 0u: return 1.10f;
        case 1u: return 1.33f;
        case 2u: return 1.52f;
        case 3u: return 2.42f;
        default: return 1.52f;
    }
}

const char* SceneQA_EnvironmentWater::GetCurrentIORLabel() const noexcept {
    switch (m_iorPreset) {
        case 0u: return "1.10 (HIELO SUAVE)";
        case 1u: return "1.33 (AGUA)";
        case 2u: return "1.52 (VIDRIO CROWN)";
        case 3u: return "2.42 (DIAMANTE)";
        default: return "1.52";
    }
}

void SceneQA_EnvironmentWater::OnQAUpdate(float dt) {
    if (!m_wavesPaused) {
        m_waterTime += dt;
    }

    bool toggleIbl = false;
    bool iorPrev = false;
    bool iorNext = false;
    bool toggleSky = false;
    bool togglePause = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft) ||
            pad.WasJustPressed(VirtualButton::DPadRight)) toggleIbl = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    iorPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  iorNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleSky = true;
        if (pad.WasJustPressed(VirtualButton::A))         togglePause = true;
    }

    if (toggleIbl) {
        AudioManager::Instance().PlayClick();
        m_iblEnabled = !m_iblEnabled;
    }
    if (iorPrev || iorNext) {
        AudioManager::Instance().PlayClick();
        m_iorPreset = iorPrev ? ((m_iorPreset + kIORPresetCount - 1u) % kIORPresetCount)
                              : ((m_iorPreset + 1u) % kIORPresetCount);
    }
    if (toggleSky && m_dayCubeLoaded && m_procCubeLoaded) {
        AudioManager::Instance().PlayClick();
        m_useProceduralSky = !m_useProceduralSky;
    }
    if (togglePause) {
        AudioManager::Instance().PlayClick();
        m_wavesPaused = !m_wavesPaused;
    }
}

void SceneQA_EnvironmentWater::RenderSpheresAndSkybox(GX2Texture* activeCube, const Point3D& skyCenterPos) {
    auto& renderer = ForwardRendererGX2::Instance();
    const float iblFactor = m_iblEnabled ? 1.0f : 0.0f;
    const float bob = std::sin(m_waterTime * 1.4f) * 0.16f;

    // Esfera 1 (Izquierda): Cromo Puro (Metallic=1.0, Roughness=0.05 + IBL 100%)
    {
        MaterialPropertyBlock chromeMat{};
        chromeMat.envCubeMap = activeCube;
        chromeMat.ubo.albedoTint[0] = 0.96f; chromeMat.ubo.albedoTint[1] = 0.97f; chromeMat.ubo.albedoTint[2] = 0.99f;
        chromeMat.ubo.materialParams[0] = iblFactor;
        chromeMat.ubo.materialParams[1] = 0.0f;
        chromeMat.ubo.materialParams[3] = 0.25f;
        chromeMat.ubo.pbrParams[0] = 0.05f;
        chromeMat.ubo.pbrParams[1] = 1.00f;
        renderer.RenderStaticMeshPBR(m_sphereMesh, Matrix4x4::Translation(-4.6f, 2.5f + bob, -1.8f), chromeMat, 0u);
    }

    // Esfera 2 (Centro-Izquierda): Vidrio / Cristal Refractivo (Modo 4: Snell + Fresnel IOR configurable)
    {
        MaterialPropertyBlock glassMat{};
        glassMat.envCubeMap = activeCube;
        glassMat.ubo.albedoTint[0] = 0.92f; glassMat.ubo.albedoTint[1] = 0.98f; glassMat.ubo.albedoTint[2] = 1.00f;
        glassMat.ubo.pbrParams[3] = GetCurrentIOR();
        renderer.RenderStaticMeshPBR(m_sphereMesh, Matrix4x4::Translation(-1.6f, 2.7f - bob, -2.2f), glassMat, 4u);
    }

    // Esferas 3, 4 y 5 (Centro-Derecha -> Derecha): Trío PBR Oro + IBL (Roughness 0.15 / 0.45 / 0.80)
    const float roughLevels[3] = {0.15f, 0.45f, 0.80f};
    const float xPositions[3]  = {1.5f, 4.3f, 7.0f};
    for (uint32_t i = 0u; i < 3u; ++i) {
        MaterialPropertyBlock pbrIblMat{};
        pbrIblMat.envCubeMap = activeCube;
        pbrIblMat.ubo.albedoTint[0] = 0.96f; pbrIblMat.ubo.albedoTint[1] = 0.76f; pbrIblMat.ubo.albedoTint[2] = 0.34f;
        pbrIblMat.ubo.materialParams[0] = iblFactor;
        pbrIblMat.ubo.materialParams[1] = 0.0f;
        pbrIblMat.ubo.materialParams[3] = 0.28f;
        pbrIblMat.ubo.pbrParams[0] = roughLevels[i];
        pbrIblMat.ubo.pbrParams[1] = (i < 2u) ? 0.95f : 0.20f;
        const float dy = (i == 1u) ? bob : -bob;
        renderer.RenderStaticMeshPBR(
            m_sphereMesh,
            Matrix4x4::Translation(xPositions[i], 2.5f + dy, -1.8f + static_cast<float>(i) * 0.45f),
            pbrIblMat, 0u);
    }

    // Skybox Atlas 3x2 infinito (Modo 6, centrado en la cámara activa, depthWrite OFF, Early-Z)
    if (activeCube) {
        MaterialPropertyBlock skyMat{};
        skyMat.envCubeMap = activeCube;
        skyMat.depthTest = true;
        skyMat.depthWrite = false;
        skyMat.backfaceCull = false;
        const Matrix4x4 skyModel = Matrix4x4::Translation(skyCenterPos.x, skyCenterPos.y, skyCenterPos.z) *
                                   Matrix4x4::Scale(120.0f, 120.0f, 120.0f);
        renderer.RenderStaticMeshPBR(m_skyboxCubeMesh, skyModel, skyMat, 6u);
    }
}

void SceneQA_EnvironmentWater::OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    // Sol alineado con el disco solar del CubeMap
    lights.SetDirectionalLight(Point3D{0.45f, 0.52f, -0.72f}, Point3D{1.0f, 0.96f, 0.88f}, 64.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();

    renderer.SetShaderTime(m_waterTime);

    GX2Texture* activeCube = nullptr;
    if (m_useProceduralSky && m_procCubeLoaded) {
        activeCube = &m_procCubeMap;
    } else if (m_dayCubeLoaded) {
        activeCube = &m_dayCubeMap;
    } else if (m_procCubeLoaded) {
        activeCube = &m_procCubeMap;
    }

    // =========================================================================
    // PASE 1: REFLEXIÓN PLANAR EN RENDER TARGET 640x360 (Cámara reflejada Y -> -Y)
    // =========================================================================
    const Point3D& cam = GetCameraPos();
    const float yaw = GetCameraYaw();
    const float pitch = GetCameraPitch();
    const float cosP = std::cos(pitch);
    const float sinP = std::sin(pitch);
    const float sinY = std::sin(yaw);
    const float cosY = std::cos(yaw);

    const Point3D reflPos{cam.x, -cam.y, cam.z};
    const Point3D reflDir{sinY * cosP, -sinP, -cosY * cosP};
    const Matrix4x4 reflView = Matrix4x4::LookAt(reflPos, reflPos + reflDir, Point3D{0.0f, 1.0f, 0.0f});
    const Matrix4x4 reflVP = projMatrix * reflView;

    bool planarReflReady = false;
    if (m_reflectionRT.IsReady() &&
        m_reflectionRT.BeginPass(0.46f, 0.68f, 0.88f, 1.0f, 1.0f)) {
        renderer.BeginFrame(reflPos, reflView, projMatrix);
        RenderSpheresAndSkybox(activeCube, reflPos);
#if defined(__WIIU__) || defined(__wiiu__)
        m_reflectionRT.EndPass(
            static_cast<GX2ContextState*>(Window::GetActiveNativeContextState()),
            static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer()),
            static_cast<GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer()),
            1280u, 720u);
#else
        m_reflectionRT.EndPass(nullptr, nullptr, nullptr, 1280u, 720u);
#endif
        renderer.BeginFrame(cam, viewMatrix, projMatrix);
        planarReflReady = true;
    }

    // =========================================================================
    // PASE 2: ESCENA PRINCIPAL (Lago físico con Reflexión Planar + 5 Esferas + Skybox)
    // =========================================================================
    {
        renderer.SetDirectionalShadowState(reflVP, 0u, 0.0f, 1.0f / 640.0f, 1.0f);
        renderer.SetTextureWrapMode(TextureWrapMode::Clamp);

        MaterialPropertyBlock waterMat{};
        waterMat.diffuseTexture = planarReflReady ? static_cast<void*>(m_reflectionRT.GetColorTexture()) : nullptr;
        waterMat.envCubeMap = activeCube;
        waterMat.ubo.materialParams[0] = planarReflReady ? 1.0f : 0.0f; // Habilita Reflexión Planar RTT
        waterMat.ubo.pbrParams[3] = 1.0f; // waveAmplitude
        renderer.RenderStaticMeshPBR(m_waterPlaneMesh, Matrix4x4::Translation(0.0f, 0.0f, 0.0f), waterMat, 5u);

        renderer.ResetMeshSamplerDefaults();
    }

    RenderSpheresAndSkybox(activeCube, cam);
}

void SceneQA_EnvironmentWater::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 820, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    char line1[128];
    char line2[128];
    std::snprintf(line1, sizeof(line1), NELEO_LOC_CSTR("qa.g4.line1"),
                  m_useProceduralSky ? NELEO_LOC_CSTR("qa.g4.sky_proc") : "SKYBOX_DAY.WCUBE (ATLAS)",
                  m_iblEnabled ? NELEO_LOC_CSTR("common.active") : NELEO_LOC_CSTR("common.off"));
    std::snprintf(line2, sizeof(line2), NELEO_LOC_CSTR("qa.g4.line2"),
                  GetCurrentIORLabel(),
                  m_wavesPaused ? NELEO_LOC_CSTR("common.paused") : NELEO_LOC_CSTR("qa.g4.animated"));

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(NELEO_LOC("qa.g4.line3"), 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString(NELEO_LOC("qa.g4.tip"),
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
