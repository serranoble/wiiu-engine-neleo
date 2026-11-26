#include "Scene/SceneModelHouse.hpp"
#include "Core/AssetManager.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cstdio>

namespace Neleo {

namespace {
struct HouseLightPreset {
    const char* name;
    Point3D dir;
    Point3D color;
};

constexpr uint32_t kLightPresetCount = 3u;
constexpr HouseLightPreset kLightPresets[kLightPresetCount] = {
    { "MANANA",    Point3D{ 0.78f, 0.48f, -0.35f}, Point3D{1.00f, 0.86f, 0.64f} },
    { "MEDIODIA",  Point3D{ 0.28f, 0.92f,  0.34f}, Point3D{1.00f, 0.98f, 0.95f} },
    { "ATARDECER", Point3D{-0.82f, 0.36f,  0.44f}, Point3D{1.00f, 0.62f, 0.34f} }
};
} // namespace

std::vector<std::string> SceneModelHouse::GetRequiredAssets() const {
    return {
        "models/dutch_house.wmesh",
        "models/dutch_house_props.wmesh",
        "models/dutch_house_alpha.wmesh",
        "models/dutch_house.wtex",
        "models/dutch_house_props.wtex",
        "models/dutch_house_alpha.wtex"
    };
}

void SceneModelHouse::LoadAsync() {
    // E/S pura en Core 2 (cero llamadas a GX2 fuera de Core 1)
    AssetManager::LoadBinaryFile("models/dutch_house.wmesh", m_stagedHouseMesh);
    AssetManager::LoadBinaryFile("models/dutch_house_props.wmesh", m_stagedPropsMesh);
    AssetManager::LoadBinaryFile("models/dutch_house_alpha.wmesh", m_stagedAlphaMesh);
    AssetManager::LoadBinaryFile("models/dutch_house.wtex", m_stagedHouseTex);
    AssetManager::LoadBinaryFile("models/dutch_house_props.wtex", m_stagedPropsTex);
    AssetManager::LoadBinaryFile("models/dutch_house_alpha.wtex", m_stagedAlphaTex);
}

void SceneModelHouse::FreeStagedBuffers() noexcept {
    std::vector<uint8_t>{}.swap(m_stagedHouseMesh);
    std::vector<uint8_t>{}.swap(m_stagedPropsMesh);
    std::vector<uint8_t>{}.swap(m_stagedAlphaMesh);
    std::vector<uint8_t>{}.swap(m_stagedHouseTex);
    std::vector<uint8_t>{}.swap(m_stagedPropsTex);
    std::vector<uint8_t>{}.swap(m_stagedAlphaTex);
}

void SceneModelHouse::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 4.2f, 11.5f}, 0.0f, -0.18f);

    m_lightPreset = 1u;
    m_autoRotate = true;
    m_showProps = true;
    m_useTextures = true;
    m_turntableAngle = 0.0f;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    m_houseMeshLoaded = !m_stagedHouseMesh.empty()
        ? m_houseMesh.LoadFromMemory(m_stagedHouseMesh.data(), m_stagedHouseMesh.size())
        : m_houseMesh.LoadFromFile("models/dutch_house.wmesh");
    m_propsMeshLoaded = !m_stagedPropsMesh.empty()
        ? m_propsMesh.LoadFromMemory(m_stagedPropsMesh.data(), m_stagedPropsMesh.size())
        : m_propsMesh.LoadFromFile("models/dutch_house_props.wmesh");
    m_alphaMeshLoaded = !m_stagedAlphaMesh.empty()
        ? m_alphaMesh.LoadFromMemory(m_stagedAlphaMesh.data(), m_stagedAlphaMesh.size())
        : m_alphaMesh.LoadFromFile("models/dutch_house_alpha.wmesh");

    m_houseTexLoaded = !m_stagedHouseTex.empty()
        ? Graphics::TextureLoaderGX2::LoadCookedTextureFromMemory(
              m_stagedHouseTex.data(), m_stagedHouseTex.size(), m_houseTex, "ModelHouse_Tex")
        : Graphics::TextureLoaderGX2::LoadCookedTextureFromFile(
              "models/dutch_house.wtex", m_houseTex, "ModelHouse_Tex");
    m_propsTexLoaded = !m_stagedPropsTex.empty()
        ? Graphics::TextureLoaderGX2::LoadCookedTextureFromMemory(
              m_stagedPropsTex.data(), m_stagedPropsTex.size(), m_propsTex, "ModelHouse_PropsTex")
        : Graphics::TextureLoaderGX2::LoadCookedTextureFromFile(
              "models/dutch_house_props.wtex", m_propsTex, "ModelHouse_PropsTex");
    m_alphaTexLoaded = !m_stagedAlphaTex.empty()
        ? Graphics::TextureLoaderGX2::LoadCookedTextureFromMemory(
              m_stagedAlphaTex.data(), m_stagedAlphaTex.size(), m_alphaTex, "ModelHouse_AlphaTex")
        : Graphics::TextureLoaderGX2::LoadCookedTextureFromFile(
              "models/dutch_house_alpha.wtex", m_alphaTex, "ModelHouse_AlphaTex");

    FreeStagedBuffers();
}

void SceneModelHouse::OnQAExit() {
    FreeStagedBuffers();

    const bool hasResources = m_houseMeshLoaded || m_propsMeshLoaded || m_alphaMeshLoaded ||
                              m_houseTexLoaded || m_propsTexLoaded || m_alphaTexLoaded;
    if (hasResources) {
        WaitForGpuIdleSafe();
    }

    if (m_alphaTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_alphaTex);
        m_alphaTexLoaded = false;
    }
    if (m_propsTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_propsTex);
        m_propsTexLoaded = false;
    }
    if (m_houseTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_houseTex);
        m_houseTexLoaded = false;
    }

    if (m_alphaMeshLoaded) { m_alphaMesh.Destroy(); m_alphaMeshLoaded = false; }
    if (m_propsMeshLoaded) { m_propsMesh.Destroy(); m_propsMeshLoaded = false; }
    if (m_houseMeshLoaded) { m_houseMesh.Destroy(); m_houseMeshLoaded = false; }

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

void SceneModelHouse::OnQAUpdate(float dt) {
    bool prevLight = false;
    bool nextLight = false;
    bool toggleRotate = false;
    bool toggleProps = false;
    bool toggleTex = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  prevLight = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) nextLight = true;
        if (pad.WasJustPressed(VirtualButton::A))         toggleRotate = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleProps = true;
        if (pad.WasJustPressed(VirtualButton::Minus))     toggleTex = true;
    }

    if (prevLight || nextLight) {
        AudioManager::Instance().PlayClick();
        m_lightPreset = prevLight
            ? ((m_lightPreset + kLightPresetCount - 1u) % kLightPresetCount)
            : ((m_lightPreset + 1u) % kLightPresetCount);
    }
    if (toggleRotate) {
        AudioManager::Instance().PlayClick();
        m_autoRotate = !m_autoRotate;
    }
    if (toggleProps) {
        AudioManager::Instance().PlayClick();
        m_showProps = !m_showProps;
    }
    if (toggleTex) {
        AudioManager::Instance().PlayClick();
        m_useTextures = !m_useTextures;
    }

    if (m_autoRotate) {
        m_turntableAngle += dt * 0.35f;
        if (m_turntableAngle >= 6.2831853f) {
            m_turntableAngle -= 6.2831853f;
        }
    }
}

void SceneModelHouse::OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    (void)viewMatrix;
    (void)projMatrix;

    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    const HouseLightPreset& lp = kLightPresets[m_lightPreset];
    lights.SetDirectionalLight(lp.dir, lp.color, 32.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();

    const Matrix4x4 model = Matrix4x4::RotationY(m_turntableAngle) * Matrix4x4::Translation(0.63f, 0.0f, -0.16f);

    // 1. Estructura principal de la casa cocinada (.wmesh + .wtex)
    if (m_houseMeshLoaded) {
        MaterialPropertyBlock houseMat{};
        houseMat.diffuseTexture = (m_useTextures && m_houseTexLoaded) ? &m_houseTex : nullptr;
        houseMat.ubo.albedoTint[0] = 1.0f; houseMat.ubo.albedoTint[1] = 1.0f; houseMat.ubo.albedoTint[2] = 1.0f;
        houseMat.ubo.materialParams[1] = 0.0f; // Desactivar relieve dFdx/dFdy sobre textura cocinada
        houseMat.ubo.pbrParams[0] = 0.70f;
        houseMat.ubo.pbrParams[1] = 0.04f;
        houseMat.ubo.materialParams[3] = 0.40f;
        renderer.RenderStaticMeshPBR(m_houseMesh, model, houseMat, 0u);
    }

    // 2. Props y vegetación Alpha-Cutout en el mismo espacio PBR sRGB (modo 0u)
    if (m_showProps) {
        if (m_propsMeshLoaded) {
            MaterialPropertyBlock propsMat{};
            propsMat.diffuseTexture = (m_useTextures && m_propsTexLoaded) ? &m_propsTex : nullptr;
            propsMat.ubo.albedoTint[0] = 1.0f; propsMat.ubo.albedoTint[1] = 1.0f; propsMat.ubo.albedoTint[2] = 1.0f;
            propsMat.ubo.materialParams[1] = 0.0f;
            propsMat.ubo.pbrParams[0] = 0.65f;
            propsMat.ubo.pbrParams[1] = 0.05f;
            propsMat.ubo.materialParams[3] = 0.40f;
            renderer.RenderStaticMeshPBR(m_propsMesh, model, propsMat, 0u);
        }

        if (m_alphaMeshLoaded) {
            MaterialPropertyBlock alphaMat{};
            alphaMat.diffuseTexture = (m_useTextures && m_alphaTexLoaded) ? &m_alphaTex : nullptr;
            alphaMat.blendMode = m_useTextures ? BlendMode::AlphaCutout : BlendMode::Opaque;
            alphaMat.backfaceCull = false;
            alphaMat.ubo.albedoTint[0] = 1.0f; alphaMat.ubo.albedoTint[1] = 1.0f; alphaMat.ubo.albedoTint[2] = 1.0f;
            alphaMat.ubo.materialParams[1] = 0.0f;
            alphaMat.ubo.pbrParams[0] = 0.72f;
            alphaMat.ubo.pbrParams[1] = 0.02f;
            alphaMat.ubo.materialParams[3] = 0.42f;
            renderer.RenderStaticMeshPBR(m_alphaMesh, model, alphaMat, 0u);
        }
    }
}

void SceneModelHouse::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 1080, 104, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    const uint32_t totalVerts =
        (m_houseMeshLoaded ? m_houseMesh.GetVertexCount() : 0u) +
        (m_propsMeshLoaded ? m_propsMesh.GetVertexCount() : 0u) +
        (m_alphaMeshLoaded ? m_alphaMesh.GetVertexCount() : 0u);
    const uint32_t totalTris =
        ((m_houseMeshLoaded ? m_houseMesh.GetIndexCount() : 0u) +
         (m_propsMeshLoaded ? m_propsMesh.GetIndexCount() : 0u) +
         (m_alphaMeshLoaded ? m_alphaMesh.GetIndexCount() : 0u)) / 3u;

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1),
                  "ARCHIVOS .WMESH: CASA=%s (%uB) | PROPS=%s | ALPHA=%s",
                  m_houseMeshLoaded ? "OK" : "ERR",
                  m_houseMeshLoaded ? m_houseMesh.GetHeader().vertexStride : 0u,
                  m_propsMeshLoaded ? "OK" : "ERR",
                  m_alphaMeshLoaded ? "OK" : "ERR");
    std::snprintf(line2, sizeof(line2),
                  "TEXTURAS .WTEX : CASA=%s | PROPS=%s | ALPHA=%s | GEOMETRIA: %u VERTS / %u TRIS",
                  m_houseTexLoaded ? "OK" : "ERR",
                  m_propsTexLoaded ? "OK" : "ERR",
                  m_alphaTexLoaded ? "OK" : "ERR",
                  totalVerts, totalTris);
    std::snprintf(line3, sizeof(line3),
                  "LUZ: %s | GIRO: %s | PROPS: %s | TEXTURAS (-): %s",
                  kLightPresets[m_lightPreset].name,
                  m_autoRotate ? "ACTIVO" : "PAUSA",
                  m_showProps ? "VISIBLES" : "OCULTOS",
                  m_useTextures ? "RGBA8 WTEX" : "SOLO VCOLOR");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
