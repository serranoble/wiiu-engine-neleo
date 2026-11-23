#include "Scene/SceneQA_PipelineTextures.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cstdio>

namespace WiiU {

namespace {
constexpr uint32_t kFilterPresetCount = 7u;
} // namespace

void SceneQA_PipelineTextures::OnQAEnter() {
    // Cámara baja mirando a lo largo de la pista para acentuar el ángulo rasante (anisotropía/mipmaps)
    SetDefaultCamera(Point3D{0.0f, 3.2f, 14.5f}, 0.0f, -0.18f);

    m_filterPresetIndex = 5u; // Aniso 8x + Trilinear
    m_wrapMode = TextureWrapMode::Wrap;
    m_useColorCodedMips = false;
    m_orbitPaused = false;
    m_orbitTime = 0.0f;

    // 1. Construir mallas procedurales en VRAM
    (void)Graphics::ProceduralMeshBuilder::CreatePlane(
        m_runwayPlane, 18.0f, 120.0f, 6.0f, 40.0f, 2u, 8u, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(
        m_uvCube, 2.2f, 3.0f, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(
        m_sunSphere, 1.45f, 40u, 28u, 1.0f, 0.85f, 0.25f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(
        m_planetSphere, 0.72f, 32u, 22u, 0.25f, 0.75f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(
        m_moonSphere, 0.30f, 24u, 16u, 0.88f, 0.90f, 0.95f);

    // 2. Generar texturas procedurales con cadena completa de mipmaps (256x256 -> 9 niveles)
    m_checkerLoaded = Graphics::TextureLoaderGX2::CreateProceduralMipmapCheckerTexture(
        m_checkerTex, 256u, 8u, false, "QA_G1_CheckerMips");
    m_mipColorLoaded = Graphics::TextureLoaderGX2::CreateProceduralMipmapCheckerTexture(
        m_mipColorTex, 256u, 8u, true, "QA_G1_ColorCodeMips");

    ApplyCurrentSamplerPreset();
}

void SceneQA_PipelineTextures::OnQAExit() {
    WaitForGpuIdleSafe();
    if (m_checkerLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_checkerTex);
        m_checkerLoaded = false;
    }
    if (m_mipColorLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_mipColorTex);
        m_mipColorLoaded = false;
    }
    m_runwayPlane.Destroy();
    m_uvCube.Destroy();
    m_sunSphere.Destroy();
    m_planetSphere.Destroy();
    m_moonSphere.Destroy();
}

void SceneQA_PipelineTextures::ApplyCurrentSamplerPreset() noexcept {
    auto& renderer = ForwardRendererGX2::Instance();
    renderer.SetTextureWrapMode(m_wrapMode);

    switch (m_filterPresetIndex) {
        case 0u:
            renderer.SetTextureFilterMode(TextureFilterMode::Point);
            renderer.SetTextureMipFilter(TextureMipFilterMode::None);
            break;
        case 1u:
            renderer.SetTextureFilterMode(TextureFilterMode::Bilinear);
            renderer.SetTextureMipFilter(TextureMipFilterMode::None);
            break;
        case 2u:
            renderer.SetTextureFilterMode(TextureFilterMode::Bilinear);
            renderer.SetTextureMipFilter(TextureMipFilterMode::Point);
            break;
        case 3u:
            renderer.SetTextureFilterMode(TextureFilterMode::Bilinear);
            renderer.SetTextureMipFilter(TextureMipFilterMode::Linear);
            break;
        case 4u:
            renderer.SetTextureFilterMode(TextureFilterMode::Aniso4x);
            renderer.SetTextureMipFilter(TextureMipFilterMode::Linear);
            break;
        case 5u:
            renderer.SetTextureFilterMode(TextureFilterMode::Aniso8x);
            renderer.SetTextureMipFilter(TextureMipFilterMode::Linear);
            break;
        case 6u:
        default:
            renderer.SetTextureFilterMode(TextureFilterMode::Aniso16x);
            renderer.SetTextureMipFilter(TextureMipFilterMode::Linear);
            break;
    }
}

const char* SceneQA_PipelineTextures::GetFilterPresetName() const noexcept {
    switch (m_filterPresetIndex) {
        case 0u: return "POINT (SIN MIP)";
        case 1u: return "BILINEAR (SIN MIP / LOD 0)";
        case 2u: return "BILINEAR + MIP POINT";
        case 3u: return "TRILINEAR (MIP LINEAR)";
        case 4u: return "ANISO 4X + TRILINEAR";
        case 5u: return "ANISO 8X + TRILINEAR";
        case 6u: return "ANISO 16X + TRILINEAR";
        default: return "DESCONOCIDO";
    }
}

const char* SceneQA_PipelineTextures::GetWrapModeName() const noexcept {
    switch (m_wrapMode) {
        case TextureWrapMode::Wrap:   return "WRAP (REPEAT)";
        case TextureWrapMode::Clamp:  return "CLAMP TO EDGE";
        case TextureWrapMode::Mirror: return "MIRRORED REPEAT";
        default:                      return "WRAP";
    }
}

void SceneQA_PipelineTextures::OnQAUpdate(float dt) {
    if (!m_orbitPaused) {
        m_orbitTime += dt;
    }

    bool filterPrev = false;
    bool filterNext = false;
    bool wrapPrev = false;
    bool wrapNext = false;
    bool toggleMips = false;
    bool togglePause = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  filterPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) filterNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    wrapPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  wrapNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleMips = true;
        if (pad.WasJustPressed(VirtualButton::A))         togglePause = true;
    }

    if (filterPrev || filterNext) {
        AudioManager::Instance().PlayClick();
        if (filterPrev) {
            m_filterPresetIndex = (m_filterPresetIndex + kFilterPresetCount - 1u) % kFilterPresetCount;
        } else {
            m_filterPresetIndex = (m_filterPresetIndex + 1u) % kFilterPresetCount;
        }
        ApplyCurrentSamplerPreset();
    }

    if (wrapPrev || wrapNext) {
        AudioManager::Instance().PlayClick();
        const uint8_t cur = static_cast<uint8_t>(m_wrapMode);
        const uint8_t next = wrapPrev ? static_cast<uint8_t>((cur + 2u) % 3u)
                                      : static_cast<uint8_t>((cur + 1u) % 3u);
        m_wrapMode = static_cast<TextureWrapMode>(next);
        ApplyCurrentSamplerPreset();
    }

    if (toggleMips) {
        AudioManager::Instance().PlayClick();
        m_useColorCodedMips = !m_useColorCodedMips;
    }

    if (togglePause) {
        AudioManager::Instance().PlayClick();
        m_orbitPaused = !m_orbitPaused;
    }
}

void SceneQA_PipelineTextures::OnQARender3D(const Matrix4x4& /*viewMatrix*/, const Matrix4x4& /*projMatrix*/) {
    auto& renderer = ForwardRendererGX2::Instance();

    GX2Texture* activeTex = nullptr;
    if (m_useColorCodedMips && m_mipColorLoaded) {
        activeTex = &m_mipColorTex;
    } else if (m_checkerLoaded) {
        activeTex = &m_checkerTex;
    }

    // 1. Pista larga en perspectiva (Z: -106 a +14) para inspeccionar Mipmaps y Anisotropía
    const Matrix4x4 runwayModel = Matrix4x4::Translation(0.0f, 0.0f, -46.0f);
    renderer.RenderStaticMesh(m_runwayPlane, runwayModel, activeTex, nullptr, 0.0f, false, 0.0f, 0.55f);

    // 2. Tres cubos con UV [0..3] a ambos lados de la pista para validar Wrap / Clamp / Mirror
    const Quaternion cubeSpin = Quaternion::FromAxisAngle(Point3D{0.0f, 1.0f, 0.0f}, m_orbitTime * 0.45f);
    const Matrix4x4 cubeLeftModel = Matrix4x4::FromTRS(Point3D{-5.8f, 1.6f, 2.5f}, cubeSpin, Point3D{1.0f, 1.0f, 1.0f});
    const Matrix4x4 cubeRightModel = Matrix4x4::FromTRS(Point3D{5.8f, 1.6f, 2.5f}, cubeSpin.Conjugate(), Point3D{1.0f, 1.0f, 1.0f});
    const Matrix4x4 cubeFarModel = Matrix4x4::FromTRS(Point3D{0.0f, 1.6f, -24.0f}, cubeSpin, Point3D{1.4f, 1.4f, 1.4f});

    renderer.RenderStaticMesh(m_uvCube, cubeLeftModel, activeTex, nullptr, 0.0f, false, 0.0f, 0.50f);
    renderer.RenderStaticMesh(m_uvCube, cubeRightModel, activeTex, nullptr, 0.0f, false, 0.0f, 0.50f);
    renderer.RenderStaticMesh(m_uvCube, cubeFarModel, activeTex, nullptr, 0.0f, false, 0.0f, 0.50f);

    // 3. Jerarquía MVP + Cuaterniones (Sol -> Planeta inclinado -> Luna) sin gimbal lock
    const Quaternion sunRot = Quaternion::FromAxisAngle(Point3D{0.0f, 1.0f, 0.0f}, m_orbitTime * 0.6f);
    const Matrix4x4 sunWorld = Matrix4x4::FromTRS(Point3D{0.0f, 5.2f, -4.0f}, sunRot, Point3D{1.0f, 1.0f, 1.0f});
    renderer.RenderStaticMesh(m_sunSphere, sunWorld, nullptr, nullptr, 0.0f, false, 0.0f, 0.70f);

    // Planeta orbita al Sol en un plano inclinado 23.5 grados (composición de cuaterniones)
    const Quaternion orbitalTilt = Quaternion::FromAxisAngle(Point3D{0.0f, 0.0f, 1.0f}, 0.41f);
    const Quaternion planetOrbit = Quaternion::FromAxisAngle(Point3D{0.0f, 1.0f, 0.0f}, m_orbitTime * 1.15f);
    const Quaternion planetSpin  = Quaternion::FromAxisAngle(Point3D{0.2f, 1.0f, 0.0f}, m_orbitTime * 2.4f);
    const Matrix4x4 planetLocal  = Matrix4x4::FromQuaternion(orbitalTilt * planetOrbit)
                                 * Matrix4x4::Translation(4.4f, 0.0f, 0.0f)
                                 * Matrix4x4::FromQuaternion(planetSpin);
    const Matrix4x4 planetWorld  = Matrix4x4::Translation(0.0f, 5.2f, -4.0f) * planetLocal;
    renderer.RenderStaticMesh(m_planetSphere, planetWorld, nullptr, nullptr, 0.0f, false, 0.0f, 0.50f);

    // Luna orbita al Planeta heredando su transformación completa
    const Quaternion moonOrbit = Quaternion::FromAxisAngle(Point3D{0.15f, 1.0f, 0.0f}, m_orbitTime * 3.1f);
    const Matrix4x4 moonLocal  = Matrix4x4::FromQuaternion(moonOrbit)
                               * Matrix4x4::Translation(1.65f, 0.0f, 0.0f);
    const Matrix4x4 moonWorld  = planetWorld * moonLocal;
    renderer.RenderStaticMesh(m_moonSphere, moonWorld, nullptr, nullptr, 0.0f, false, 0.0f, 0.55f);
}

void SceneQA_PipelineTextures::OnQARenderHUD() {
    // Panel lateral de estado específico de Suite G1 (Zero-Alloc)
    Render2D::DrawBox(18, 92, 680, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), "FILTRO GX2 : %s", GetFilterPresetName());
    std::snprintf(line2, sizeof(line2), "UV WRAP    : %s (CUBOS UV 3X)", GetWrapModeName());
    std::snprintf(line3, sizeof(line3), "TEXTURA    : %s | ORBITA: %s",
                  m_useColorCodedMips ? "MIPMAP COLOR-CODED" : "CHECKERBOX (9 MIPS)",
                  m_orbitPaused ? "PAUSA" : "ACTIVA");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});

    if (m_useColorCodedMips) {
        Render2D::DrawPixelString("M0:BLANCO M1:CIAN M2:VERDE M3:AMAR M4:NAR M5+:ROJO",
                                  30, 184, 2, ColorRGBA::Yellow());
    } else {
        Render2D::DrawPixelString("CONSEJO: PRESIONA (X) PARA VER TRANSICIONES DE MIPMAP",
                                  30, 184, 2, ColorRGBA{190, 215, 245, 255});
    }
}

} // namespace WiiU
