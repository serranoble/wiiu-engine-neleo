#include "Scene/SceneQA_RaytracingBVH.hpp"
#include "Core/Window.hpp"
#include "Graphics/RaytracingBVHShaderBinary.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cmath>
#include <cstdio>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/surface.h>
#endif

namespace WiiU {

namespace {
constexpr float kFovYRad   = 1.04719755f; // 60 grados
constexpr float kAspect169 = 1280.0f / 720.0f;
} // namespace

void SceneQA_RaytracingBVH::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 1.8f, 9.2f}, 0.0f, -0.14f);

    m_resPresetIdx = 2u; // 540p por defecto
    m_shadowMode = 2u;   // Penumbra suave
    m_bvhEnabled = true;
    m_heatmapEnabled = false;
    m_animPaused = false;
    m_rtTime = 0.0f;

    if (m_rtShader.Initialize("shaders/raytracing_bvh.gsh",
                              RAYTRACING_BVH_GSH_DATA,
                              RAYTRACING_BVH_GSH_SIZE)) {
        (void)m_rtShader.EnableOffscreenScaler(1280u, 720u);
        m_offCamPos            = m_rtShader.GetPixelUniformOffset("uCamPos");
        m_offCamForward        = m_rtShader.GetPixelUniformOffset("uCamForward");
        m_offCamRight          = m_rtShader.GetPixelUniformOffset("uCamRight");
        m_offCamUp             = m_rtShader.GetPixelUniformOffset("uCamUp");
        m_offLightPos          = m_rtShader.GetPixelUniformOffset("uLightPos");
        m_offRenderFlags       = m_rtShader.GetPixelUniformOffset("uRenderFlags");
        m_offSunDirAndNodes    = m_rtShader.GetPixelUniformOffset("uSunDirAndNodes");
        m_offScreenBounds      = m_rtShader.GetPixelUniformOffset("uScreenBounds");
        m_offShadowFloorBounds = m_rtShader.GetPixelUniformOffset("uShadowFloorBounds");
        m_offSpheres           = m_rtShader.GetPixelUniformOffset("uSpheres");
        m_offSphereColors      = m_rtShader.GetPixelUniformOffset("uSphereColors");
        m_offBvhNodes          = m_rtShader.GetPixelUniformOffset("uBvhNodes");
    }
}

void SceneQA_RaytracingBVH::OnQAExit() {
    if (m_rtShader.IsReady()) {
        WaitForGpuIdleSafe();
    }
    m_rtShader.Shutdown();
}

const char* SceneQA_RaytracingBVH::GetShadowModeLabel() const noexcept {
    switch (m_shadowMode) {
        case 0u: return "APAGADAS";
        case 1u: return "DURAS (RAY HARD)";
        case 2u: return "PENUMBRA SUAVE";
        default: return "PENUMBRA SUAVE";
    }
}

void SceneQA_RaytracingBVH::OnQAUpdate(float dt) {
    if (!m_animPaused) {
        m_rtTime += dt;
    }

    bool resPrev = false;
    bool resNext = false;
    bool shadPrev = false;
    bool shadNext = false;
    bool toggleBvh = false;
    bool toggleHeat = false;
    bool togglePause = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  resPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) resNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    shadPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  shadNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleBvh = true;
        if (pad.WasJustPressed(VirtualButton::A))         toggleHeat = true;
        if (pad.WasJustPressed(VirtualButton::L) ||
            pad.WasJustPressed(VirtualButton::R))         togglePause = true;
    }

    if (resPrev || resNext) {
        AudioManager::Instance().PlayClick();
        const size_t n = RHIFullscreenShader::STANDARD_16X9_PRESET_COUNT;
        m_resPresetIdx = resPrev ? ((m_resPresetIdx + n - 1u) % n) : ((m_resPresetIdx + 1u) % n);
    }
    if (shadPrev || shadNext) {
        AudioManager::Instance().PlayClick();
        m_shadowMode = shadPrev ? ((m_shadowMode + 2u) % 3u) : ((m_shadowMode + 1u) % 3u);
    }
    if (toggleBvh) {
        AudioManager::Instance().PlayClick();
        m_bvhEnabled = !m_bvhEnabled;
    }
    if (toggleHeat) {
        AudioManager::Instance().PlayClick();
        m_heatmapEnabled = !m_heatmapEnabled;
    }
    if (togglePause) {
        AudioManager::Instance().PlayClick();
        m_animPaused = !m_animPaused;
    }
}

void SceneQA_RaytracingBVH::OnQARender3D(const Matrix4x4& /*viewMatrix*/, const Matrix4x4& /*projMatrix*/) {
    if (!m_rtShader.IsReady()) return;

    // 1. Construir 12 esferas animadas (metálicas + dieléctricas) y BVH Stackless Zero-Alloc
    std::array<StacklessBVHBuilder::SpherePrim, 12> spheres{};

    // Esfera central cromada (alta reflectividad)
    spheres[0] = {Point3D{0.0f, 0.25f + std::sin(m_rtTime * 1.1f) * 0.20f, 0.0f}, 1.35f, {0.95f, 0.96f, 0.99f, 0.85f}};
    // 2 esferas laterales grandes (oro y rubí reflectivo)
    spheres[1] = {Point3D{-3.1f, -0.10f + std::cos(m_rtTime * 1.3f) * 0.25f, -0.8f}, 1.05f, {0.96f, 0.74f, 0.22f, 0.65f}};
    spheres[2] = {Point3D{ 3.1f, -0.10f + std::sin(m_rtTime * 1.2f + 1.0f) * 0.25f, -0.8f}, 1.05f, {0.92f, 0.18f, 0.26f, 0.50f}};

    // 9 esferas satélite orbitando en 2 anillos 3D
    static constexpr float kPalette[9][4] = {
        {0.18f, 0.78f, 0.96f, 0.45f},
        {0.24f, 0.90f, 0.44f, 0.35f},
        {0.85f, 0.32f, 0.95f, 0.55f},
        {0.98f, 0.56f, 0.16f, 0.30f},
        {0.96f, 0.92f, 0.34f, 0.60f},
        {0.34f, 0.52f, 0.98f, 0.40f},
        {0.94f, 0.94f, 0.96f, 0.75f},
        {0.16f, 0.88f, 0.76f, 0.48f},
        {0.96f, 0.36f, 0.58f, 0.38f}
    };
    for (uint32_t i = 0u; i < 9u; ++i) {
        const float fi = static_cast<float>(i);
        const float ringR = (i < 5u) ? 2.55f : 4.25f;
        const float speed = (i < 5u) ? 0.65f : -0.45f;
        const float ang = m_rtTime * speed + fi * 1.256637f;
        const float yOff = (i < 5u) ? (1.35f + std::sin(m_rtTime * 1.7f + fi) * 0.35f)
                                    : (-0.55f + std::cos(m_rtTime * 1.4f + fi) * 0.30f);
        spheres[3u + i] = {
            Point3D{std::cos(ang) * ringR, yOff, std::sin(ang) * ringR},
            (i < 5u) ? 0.46f : 0.58f,
            {kPalette[i][0], kPalette[i][1], kPalette[i][2], kPalette[i][3]}
        };
    }

    StacklessBVHBuilder::Build(spheres.data(), 12u, m_bvhResult);

    // 2. Base ortonormal de la cámara libre
    const Point3D& camPos = GetCameraPos();
    const float yaw = GetCameraYaw();
    const float pitch = GetCameraPitch();
    const float cosP = std::cos(pitch);
    const float sinP = std::sin(pitch);
    const float sinY = std::sin(yaw);
    const float cosY = std::cos(yaw);

    const Point3D camFwd{sinY * cosP, sinP, -cosY * cosP};
    const Point3D camRight{cosY, 0.0f, sinY};
    const Point3D camUp{
        camRight.y * camFwd.z - camRight.z * camFwd.y,
        camRight.z * camFwd.x - camRight.x * camFwd.z,
        camRight.x * camFwd.y - camRight.y * camFwd.x
    };

    const float tanHalfFovY = std::tan(kFovYRad * 0.5f);
    const float lightAng = m_rtTime * 0.42f + 0.7f;
    const Point3D lightPos{std::cos(lightAng) * 9.0f, 8.5f, std::sin(lightAng) * 9.0f};
    const float lightLen = std::sqrt(lightPos.x * lightPos.x + lightPos.y * lightPos.y + lightPos.z * lightPos.z);
    const float invLightLen = (lightLen > 1e-4f) ? (1.0f / lightLen) : 1.0f;

    // 3. Vincular framebuffer TV como destino final del RHIFullscreenShader y renderizar al Render Scale elegido
    RHIFullscreenShader::OutputTargetBinding outTarget{};
#if defined(__WIIU__) || defined(__wiiu__)
    outTarget.colorBuffer = static_cast<GX2ColorBuffer*>(Window::GetActiveNativeColorBuffer());
    outTarget.depthBuffer = static_cast<GX2DepthBuffer*>(Window::GetActiveNativeDepthBuffer());
#endif
    outTarget.width = 1280u;
    outTarget.height = 720u;

    const auto& preset = RHIFullscreenShader::STANDARD_16X9_PRESETS[m_resPresetIdx];
    m_rtShader.BindPipeline(preset, &outTarget);

    const float uCamPos[4]            = {camPos.x, camPos.y, camPos.z, tanHalfFovY};
    const float uCamForward[4]        = {camFwd.x, camFwd.y, camFwd.z, kAspect169};
    const float uCamRight[4]          = {camRight.x, camRight.y, camRight.z, m_rtTime};
    const float uCamUp[4]             = {camUp.x, camUp.y, camUp.z, 0.0f};
    const float uLightPos[4]          = {lightPos.x, lightPos.y, lightPos.z, 1.35f};
    const float uRenderFlags[4]       = {
        m_bvhEnabled ? 1.0f : 0.0f,
        static_cast<float>(m_shadowMode),
        m_heatmapEnabled ? 1.0f : 0.0f,
        static_cast<float>(m_bvhResult.primCount)
    };
    const float uSunDirAndNodes[4]    = {
        lightPos.x * invLightLen,
        lightPos.y * invLightLen,
        lightPos.z * invLightLen,
        static_cast<float>(m_bvhResult.nodeCount)
    };
    // Rango amplio seguro en NDC y plano de sombra y = -1.25 (evita recortes prematuros al girar la cámara)
    const float uScreenBounds[4]      = {-1.1f, -1.1f, 1.1f, 1.1f};
    const float uShadowFloorBounds[4] = {-18.0f, -18.0f, 18.0f, 18.0f};

    m_rtShader.SetPixelUniform4fv(m_offCamPos, 4u, uCamPos);
    m_rtShader.SetPixelUniform4fv(m_offCamForward, 4u, uCamForward);
    m_rtShader.SetPixelUniform4fv(m_offCamRight, 4u, uCamRight);
    m_rtShader.SetPixelUniform4fv(m_offCamUp, 4u, uCamUp);
    m_rtShader.SetPixelUniform4fv(m_offLightPos, 4u, uLightPos);
    m_rtShader.SetPixelUniform4fv(m_offRenderFlags, 4u, uRenderFlags);
    m_rtShader.SetPixelUniform4fv(m_offSunDirAndNodes, 4u, uSunDirAndNodes);
    m_rtShader.SetPixelUniform4fv(m_offScreenBounds, 4u, uScreenBounds);
    m_rtShader.SetPixelUniform4fv(m_offShadowFloorBounds, 4u, uShadowFloorBounds);
    m_rtShader.SetPixelUniform4fv(m_offSpheres, 48u, m_bvhResult.packedSpheres.data());
    m_rtShader.SetPixelUniform4fv(m_offSphereColors, 48u, m_bvhResult.packedColors.data());
    m_rtShader.SetPixelUniform4fv(m_offBvhNodes, 192u, m_bvhResult.packedNodes.data());

    m_rtShader.DrawQuad();
}

void SceneQA_RaytracingBVH::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 1020, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    const auto& preset = RHIFullscreenShader::STANDARD_16X9_PRESETS[m_resPresetIdx];
    const float pixelPct = 100.0f * static_cast<float>(preset.width * preset.height) / (1280.0f * 720.0f);

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), "RENDER SCALE: %s (%ux%u = %.0f%% PX) | ACELERACION: %s (%u NODOS)",
                  preset.shortLabel, preset.width, preset.height, static_cast<double>(pixelPct),
                  m_bvhEnabled ? "BVH STACKLESS" : "FUERZA BRUTA", m_bvhResult.nodeCount);
    std::snprintf(line2, sizeof(line2), "SOMBRAS RT  : %s | HEATMAP COSTO BVH: %s",
                  GetShadowModeLabel(), m_heatmapEnabled ? "ACTIVO" : "APAGADO");
    std::snprintf(line3, sizeof(line3), "ESCENA GPU  : 12 ESFERAS REFLECTIVAS | ANIMACION: %s | UPSCALER BILINEAL HW",
                  m_animPaused ? "PAUSA" : "ACTIVA");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString("CONSEJO: ACTIVA HEATMAP (A) Y ALTERNA BVH VS BRUTE-FORCE (X) PARA VER EL AHORRO DE TESTS",
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace WiiU
