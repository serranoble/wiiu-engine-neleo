#include "Scene/SceneQA_ProceduralNPR.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace Neleo {

void SceneQA_ProceduralNPR::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 5.4f, 14.5f}, 0.0f, -0.22f);

    m_nprMode = 1u; // Toon + Outline por defecto para impacto visual inmediato
    m_materialPreset = 0u;
    m_particlesEnabled = true;
    m_sunPaused = false;
    m_sunTime = 0.0f;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    // 1. Terreno montañoso procedural con splatting por altura/pendiente (48x48 celdas)
    (void)Graphics::ProceduralMeshBuilder::CreateHeightmapTerrain(
        m_terrainMesh, 56.0f, 56.0f, 48u, 48u, 5.8f);

    // 2. Esculturas de exhibición (esfera + pedestal)
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_sphereMesh, 1.35f, 32u, 22u, 1.0f, 1.0f, 1.0f);
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_pillarMesh, 1.0f, 1.0f, 0.90f, 0.90f, 0.92f);

    // 3. Quad 2D centrado en XY para partículas billboard (2 triángulos, 0 allocs en render)
    {
        std::vector<MeshVertex> verts(4u);
        auto setV = [](MeshVertex& v, float x, float y, float u, float tv) {
            v.pos[0] = x; v.pos[1] = y; v.pos[2] = 0.0f;
            v.normal[0] = 0.0f; v.normal[1] = 0.0f; v.normal[2] = 1.0f;
            v.texCoord[0] = u; v.texCoord[1] = tv;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f; v.color[3] = 1.0f;
        };
        setV(verts[0], -0.5f, -0.5f, 0.0f, 1.0f);
        setV(verts[1],  0.5f, -0.5f, 1.0f, 1.0f);
        setV(verts[2],  0.5f,  0.5f, 1.0f, 0.0f);
        setV(verts[3], -0.5f,  0.5f, 0.0f, 0.0f);
        const std::vector<uint32_t> idx = {0u, 1u, 2u, 0u, 2u, 3u};
        (void)m_particleQuadMesh.CreateFromVertices(verts, idx);
    }

    const float centerH = Graphics::ProceduralMeshBuilder::EvaluateTerrainHeight(0.0f, -1.5f, 5.8f);
    m_particles.Reset(Point3D{0.0f, centerH + 0.35f, -1.5f});
    // Pre-calentar 1.2s para que la hoguera ya tenga llama y humo al abrir la escena
    for (int i = 0; i < 40; ++i) {
        m_particles.Update(0.03f, true);
    }
}

void SceneQA_ProceduralNPR::OnQAExit() {
    WaitForGpuIdleSafe();
    m_terrainMesh.Destroy();
    m_sphereMesh.Destroy();
    m_pillarMesh.Destroy();
    m_particleQuadMesh.Destroy();

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

const char* SceneQA_ProceduralNPR::GetNprModeLabel() const noexcept {
    switch (m_nprMode) {
        case 0u: return "REALISTA (SUAVE)";
        case 1u: return "TOON 4-BANDAS + OUTLINE";
        case 2u: return "TOON 4-BANDAS (SIN OUTLINE)";
        default: return "TOON 4-BANDAS + OUTLINE";
    }
}

const char* SceneQA_ProceduralNPR::GetMaterialPresetLabel() const noexcept {
    switch (m_materialPreset) {
        case 0u: return "MIX (MARMOL + MADERA + TOON)";
        case 1u: return "TODO MARMOL 3D";
        case 2u: return "TODO MADERA 3D";
        default: return "MIX";
    }
}

void SceneQA_ProceduralNPR::OnQAUpdate(float dt) {
    if (!m_sunPaused) {
        m_sunTime += dt;
    }
    m_particles.Update(dt, m_particlesEnabled);

    bool nprPrev = false;
    bool nprNext = false;
    bool matPrev = false;
    bool matNext = false;
    bool toggleParticles = false;
    bool toggleSun = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  nprPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) nprNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp))    matPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadDown))  matNext = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleParticles = true;
        if (pad.WasJustPressed(VirtualButton::A))         toggleSun = true;
    }

    if (nprPrev || nprNext) {
        AudioManager::Instance().PlayClick();
        m_nprMode = nprPrev ? ((m_nprMode + 2u) % 3u) : ((m_nprMode + 1u) % 3u);
    }
    if (matPrev || matNext) {
        AudioManager::Instance().PlayClick();
        m_materialPreset = matPrev ? ((m_materialPreset + 2u) % 3u) : ((m_materialPreset + 1u) % 3u);
    }
    if (toggleParticles) {
        AudioManager::Instance().PlayClick();
        m_particlesEnabled = !m_particlesEnabled;
    }
    if (toggleSun) {
        AudioManager::Instance().PlayClick();
        m_sunPaused = !m_sunPaused;
    }
}

void SceneQA_ProceduralNPR::OnQARender3D(const Matrix4x4& /*viewMatrix*/, const Matrix4x4& /*projMatrix*/) {
    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    const float sunAngle = m_sunTime * 0.35f + 0.65f;
    const Point3D sunDir{std::cos(sunAngle) * 0.72f, 0.62f, std::sin(sunAngle) * 0.72f};
    lights.SetDirectionalLight(sunDir, Point3D{1.0f, 0.96f, 0.88f}, 32.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();

    // Luz cálida parpadeante de la hoguera central
    const float centerH = Graphics::ProceduralMeshBuilder::EvaluateTerrainHeight(0.0f, -1.5f, 5.8f);
    if (m_particlesEnabled) {
        const float flicker = 3.2f + std::sin(m_sunTime * 11.0f) * 0.45f + std::cos(m_sunTime * 17.0f) * 0.30f;
        (void)lights.AddPointLight(Point3D{0.0f, centerH + 1.35f, -1.5f}, 14.0f, Point3D{1.0f, 0.56f, 0.18f}, flicker);
    }

    const float toonFlag = (m_nprMode > 0u) ? 1.0f : 0.0f;
    const float outlineFlag = (m_nprMode == 1u) ? 1.0f : 0.0f;

    // 1. Terreno Heightmap con Splatting de 4 biomas (Modo 8, subMode=0)
    {
        MaterialPropertyBlock terrMat{};
        terrMat.ubo.pbrParams[3] = 0.0f;      // subMode 0 = Terrain Splatting
        terrMat.ubo.materialParams[0] = toonFlag;
        terrMat.ubo.materialParams[1] = 0.0f; // sin outline en terreno
        renderer.RenderStaticMeshPBR(m_terrainMesh, Matrix4x4::Identity(), terrMat, 8u);
    }

    // 2. Tres esculturas sobre la meseta: Izq = Mármol 3D, Der = Madera 3D, Fondo = Toon NPR puro
    const float leftSub  = (m_materialPreset == 2u) ? 2.0f : 1.0f;
    const float rightSub = (m_materialPreset == 1u) ? 1.0f : 2.0f;
    const float spin = m_sunTime * 0.45f;

    // Escultura Izquierda (Mármol 3D)
    {
        const float h = Graphics::ProceduralMeshBuilder::EvaluateTerrainHeight(-4.4f, -1.8f, 5.8f);
        MaterialPropertyBlock mat{};
        mat.ubo.pbrParams[3] = leftSub;
        mat.ubo.materialParams[0] = toonFlag;
        mat.ubo.materialParams[1] = outlineFlag;
        renderer.RenderStaticMeshPBR(
            m_sphereMesh,
            Matrix4x4::Translation(-4.4f, h + 1.85f, -1.8f) * Matrix4x4::RotationY(spin),
            mat, 8u);
    }

    // Escultura Derecha (Madera 3D)
    {
        const float h = Graphics::ProceduralMeshBuilder::EvaluateTerrainHeight(4.4f, -1.8f, 5.8f);
        MaterialPropertyBlock mat{};
        mat.ubo.pbrParams[3] = rightSub;
        mat.ubo.materialParams[0] = toonFlag;
        mat.ubo.materialParams[1] = outlineFlag;
        renderer.RenderStaticMeshPBR(
            m_sphereMesh,
            Matrix4x4::Translation(4.4f, h + 1.85f, -1.8f) * Matrix4x4::RotationY(-spin),
            mat, 8u);
    }

    // Escultura Central-Fondo (Toon / Cel Shading NPR con contorno o Ruido 3D si se selecciona Todo Mármol/Madera)
    {
        const float h = Graphics::ProceduralMeshBuilder::EvaluateTerrainHeight(0.0f, -5.6f, 5.8f);
        MaterialPropertyBlock mat{};
        if (m_materialPreset == 0u) {
            mat.ubo.albedoTint[0] = 0.24f; mat.ubo.albedoTint[1] = 0.68f; mat.ubo.albedoTint[2] = 0.96f;
            mat.ubo.pbrParams[3] = 0.0f; // Modo 9 subMode=0 -> Toon NPR
            mat.ubo.materialParams[1] = outlineFlag;
            renderer.RenderStaticMeshPBR(
                m_sphereMesh,
                Matrix4x4::Translation(0.0f, h + 2.15f, -5.6f) * Matrix4x4::Scale(1.18f, 1.18f, 1.18f),
                mat, 9u);
        } else {
            mat.ubo.pbrParams[3] = (m_materialPreset == 1u) ? 1.0f : 2.0f;
            mat.ubo.materialParams[0] = toonFlag;
            mat.ubo.materialParams[1] = outlineFlag;
            renderer.RenderStaticMeshPBR(
                m_sphereMesh,
                Matrix4x4::Translation(0.0f, h + 2.15f, -5.6f) * Matrix4x4::Scale(1.18f, 1.18f, 1.18f),
                mat, 8u);
        }
    }

    // Troncos / base de la hoguera central (Madera 3D)
    {
        MaterialPropertyBlock logMat{};
        logMat.ubo.pbrParams[3] = 2.0f; // Madera 3D
        logMat.ubo.materialParams[0] = toonFlag;
        logMat.ubo.materialParams[1] = outlineFlag;
        renderer.RenderStaticMeshPBR(
            m_pillarMesh,
            Matrix4x4::Translation(0.0f, centerH + 0.22f, -1.5f) *
                Matrix4x4::RotationY(0.55f) * Matrix4x4::Scale(1.6f, 0.34f, 0.42f),
            logMat, 8u);
        renderer.RenderStaticMeshPBR(
            m_pillarMesh,
            Matrix4x4::Translation(0.0f, centerH + 0.22f, -1.5f) *
                Matrix4x4::RotationY(-0.55f) * Matrix4x4::Scale(1.6f, 0.34f, 0.42f),
            logMat, 8u);
    }

    // 3. Partículas 3D Billboard Zero-Alloc (Humo AlphaBlend primero -> Fuego Aditivo después)
    if (m_particles.GetActiveCount() > 0u) {
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

        const auto& pool = m_particles.GetParticles();
        // Sub-pase A: Humo (AlphaBlend, depthWrite OFF)
        for (const auto& p : pool) {
            if (!p.active || !p.isSmoke) continue;
            const float t = p.age / p.life;
            const float alpha = (1.0f - t) * 0.42f;
            MaterialPropertyBlock smMat{};
            smMat.blendMode = BlendMode::AlphaBlend;
            smMat.depthTest = true;
            smMat.depthWrite = false;
            smMat.ubo.albedoTint[0] = 0.48f; smMat.ubo.albedoTint[1] = 0.48f; smMat.ubo.albedoTint[2] = 0.52f;
            smMat.ubo.pbrParams[3] = 1.0f; // Modo 9 subMode=1 -> Partícula radial
            smMat.ubo.materialParams[3] = alpha;
            renderer.RenderStaticMeshPBR(
                m_particleQuadMesh,
                Graphics::ParticleSystem3D::MakeBillboardMatrix(p.pos, camRight, camUp, camFwd, p.size),
                smMat, 9u);
        }
        // Sub-pase B: Fuego / Chispas (Additive, depthWrite OFF)
        for (const auto& p : pool) {
            if (!p.active || p.isSmoke) continue;
            const float t = p.age / p.life;
            MaterialPropertyBlock fireMat{};
            fireMat.blendMode = BlendMode::Additive;
            fireMat.depthTest = true;
            fireMat.depthWrite = false;
            fireMat.ubo.albedoTint[0] = 1.0f;
            fireMat.ubo.albedoTint[1] = (1.0f - t) * 0.85f + 0.12f;
            fireMat.ubo.albedoTint[2] = (1.0f - t) * (1.0f - t) * 0.42f;
            fireMat.ubo.pbrParams[3] = 1.0f; // Modo 9 subMode=1 -> Partícula radial
            fireMat.ubo.materialParams[3] = (1.0f - t) * 0.92f;
            renderer.RenderStaticMeshPBR(
                m_particleQuadMesh,
                Graphics::ParticleSystem3D::MakeBillboardMatrix(p.pos, camRight, camUp, camFwd, p.size),
                fireMat, 9u);
        }
    }
}

void SceneQA_ProceduralNPR::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 1020, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), "ESTILO  : %s | RUIDO 3D: %s",
                  GetNprModeLabel(), GetMaterialPresetLabel());
    std::snprintf(line2, sizeof(line2), "TERRENO : HEIGHTMAP 48x48 (4 BIOMAS) | PARTICULAS: %s (%u/%u ACTIVAS)",
                  m_particlesEnabled ? "ACTIVO" : "PAUSA",
                  m_particles.GetActiveCount(), Graphics::ParticleSystem3D::MAX_PARTICLES);
    std::snprintf(line3, sizeof(line3), "SOL     : %s | IZQ=MARMOL 3D | CENTRO=HOGUERA+TOON | DER=MADERA 3D",
                  m_sunPaused ? "PAUSADO" : "ORBITANDO");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString("CONSEJO: ALTERNA REALISTA VS TOON+OUTLINE (DPAD IZQ/DER) Y OBSERVA LAS BANDAS Y SILUETAS",
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
