#include "Scene/SceneQA_StressCulling.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/Mesh/ProceduralMeshBuilder.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cmath>
#include <cstdio>

namespace WiiU {

namespace {
constexpr uint32_t kInstancePresets[3] = {256u, 512u, 1000u};
constexpr float kFovYRad = 1.04719755f; // 60 grados (coherente con GraphicsQASceneBase)
constexpr float kAspect169 = 1280.0f / 720.0f;
constexpr float kDebugFrustumNear = 0.5f;
constexpr float kDebugFrustumFar  = 52.0f;

// Construcción directa O(1) de matriz TRS (Traslación * RotaciónY * Escala uniforme) sin 2 multiplicaciones 4x4
[[nodiscard]] inline Matrix4x4 MakeTRSY(const Point3D& pos, float angleRad, float s) noexcept {
    const float c = std::cos(angleRad) * s;
    const float sn = std::sin(angleRad) * s;
    Matrix4x4 m{};
    m.m[0]  =  c;     m.m[4]  = 0.0f; m.m[8]  = sn;    m.m[12] = pos.x;
    m.m[1]  =  0.0f;  m.m[5]  = s;    m.m[9]  = 0.0f;  m.m[13] = pos.y;
    m.m[2]  = -sn;    m.m[6]  = 0.0f; m.m[10] = c;     m.m[14] = pos.z;
    m.m[3]  =  0.0f;  m.m[7]  = 0.0f; m.m[11] = 0.0f;  m.m[15] = 1.0f;
    return m;
}
} // namespace

void SceneQA_StressCulling::BuildInstanceField() noexcept {
    // Distribución determinista en espiral áurea 3D (cero rand()/heap) para cobertura homogénea
    constexpr float kGoldenAngle = 2.39996323f;
    for (uint32_t i = 0u; i < MAX_INSTANCES; ++i) {
        const float fi = static_cast<float>(i);
        const float frac = (fi + 0.5f) / static_cast<float>(MAX_INSTANCES);
        const float radius = 6.5f + std::sqrt(frac) * 44.0f;
        const float angle = fi * kGoldenAngle;
        const float yWave = 1.6f + std::fmod(fi * 1.37f, 11.5f);
        const float scale = 0.42f + std::fmod(fi * 0.29f, 0.55f);

        m_instances[i].basePos = Point3D{
            std::cos(angle) * radius,
            yWave,
            std::sin(angle) * radius - 8.0f
        };
        m_instances[i].scale = scale;
        m_instances[i].radius = scale * 1.35f;
        m_instances[i].phase = std::fmod(fi * 0.61f, 6.283185f);
        m_instances[i].rotSpeed = 0.45f + std::fmod(fi * 0.17f, 1.15f);
    }
}

void SceneQA_StressCulling::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 7.2f, 22.0f}, 0.0f, -0.22f);

    m_countPreset = 2u; // 1,000 instancias por defecto
    m_cullingEnabled = true;
    m_lodEnabled = true;
    m_freezeFrustum = false;
    m_animTime = 0.0f;
    m_visibleCount = 0u;
    m_culledCount = 0u;
    m_instDrawCalls = 0u;

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    BuildInstanceField();

    // Suelo de referencia + 3 niveles LOD diferenciados por geometría y color
    (void)Graphics::ProceduralMeshBuilder::CreatePlane(m_floorMesh, 120.0f, 120.0f, 16.0f, 16.0f, 8u, 8u, 0.16f, 0.19f, 0.24f);
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_lodMesh0, 0.85f, 14u, 10u, 0.22f, 0.92f, 0.68f); // LOD0: Verde-Cian (280 tris)
    (void)Graphics::ProceduralMeshBuilder::CreateUVSphere(m_lodMesh1, 0.85f,  8u,  6u, 0.96f, 0.82f, 0.24f); // LOD1: Amarillo-Dorado (96 tris)
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_lodMesh2,     1.40f, 1.0f,     0.95f, 0.40f, 0.22f); // LOD2: Naranja-Rojo (12 tris)

    m_lodGroup.SetMeshes(&m_lodMesh0, &m_lodMesh1, &m_lodMesh2);
    m_lodGroup.SetDistances(20.0f, 38.0f);

    // 6 bloques rectangulares procedurales orbitando (reemplazo sin skinning para aislar silicio GX2)
    (void)Graphics::ProceduralMeshBuilder::CreateCube(m_orbitRectMesh, 1.0f, 1.0f, 0.96f, 0.54f, 0.22f);
    UpdateOrbitRects(0.0f);
}

void SceneQA_StressCulling::OnQAExit() {
    WaitForGpuIdleSafe();
    m_orbitRectMesh.Destroy();
    m_floorMesh.Destroy();
    m_lodMesh0.Destroy();
    m_lodMesh1.Destroy();
    m_lodMesh2.Destroy();

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

void SceneQA_StressCulling::UpdateOrbitRects(float time) noexcept {
    for (uint32_t b = 0u; b < ORBIT_RECT_COUNT; ++b) {
        const float fb = static_cast<float>(b);
        const float orbitAngle = time * (0.42f + (fb * 0.025f)) + fb * 1.0471975f;
        const float orbitRadius = 9.0f + std::fmod(fb * 2.3f, 9.0f);
        const float bx = std::cos(orbitAngle) * orbitRadius;
        const float bz = std::sin(orbitAngle) * orbitRadius - 6.0f;
        const float by = 7.5f + std::sin(time * 1.8f + fb * 0.9f) * 1.8f + (fb * 0.25f);
        const float heading = -orbitAngle;

        m_orbitRectModels[b] = Matrix4x4::Translation(bx, by, bz) *
                               Matrix4x4::RotationY(heading) *
                               Matrix4x4::Scale(2.8f, 0.55f, 1.1f);
    }
}

void SceneQA_StressCulling::OnQAUpdate(float dt) {
    m_animTime += dt;

    bool countPrev = false;
    bool countNext = false;
    bool toggleLod = false;
    bool toggleCull = false;
    bool toggleFreeze = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  countPrev = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) countNext = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp) ||
            pad.WasJustPressed(VirtualButton::DPadDown))  toggleLod = true;
        if (pad.WasJustPressed(VirtualButton::A))         toggleCull = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleFreeze = true;
    }

    if (countPrev || countNext) {
        AudioManager::Instance().PlayClick();
        m_countPreset = countPrev ? ((m_countPreset + 2u) % 3u) : ((m_countPreset + 1u) % 3u);
    }
    if (toggleLod) {
        AudioManager::Instance().PlayClick();
        m_lodEnabled = !m_lodEnabled;
    }
    if (toggleCull) {
        AudioManager::Instance().PlayClick();
        m_cullingEnabled = !m_cullingEnabled;
    }
    if (toggleFreeze) {
        AudioManager::Instance().PlayClick();
        m_freezeFrustum = !m_freezeFrustum;
        if (m_freezeFrustum) {
            // Congelar la pirámide de visión actual para inspeccionarla desde cámara libre (Debug Camera)
            const Point3D& pos = GetCameraPos();
            const float yaw = GetCameraYaw();
            const float pitch = GetCameraPitch();
            const float cosP = std::cos(pitch);
            const float sinP = std::sin(pitch);
            const float sinY = std::sin(yaw);
            const float cosY = std::cos(yaw);

            const Point3D fwd{sinY * cosP, sinP, -cosY * cosP};
            const Point3D right{cosY, 0.0f, sinY};
            const Point3D up{
                right.y * fwd.z - right.z * fwd.y,
                right.z * fwd.x - right.x * fwd.z,
                right.x * fwd.y - right.y * fwd.x
            };
            const Matrix4x4 freezeView = Matrix4x4::LookAt(pos, pos + fwd, Point3D{0.0f, 1.0f, 0.0f});
            const Matrix4x4 freezeProj = Matrix4x4::Perspective(kFovYRad, kAspect169, kDebugFrustumNear, kDebugFrustumFar);
            m_cullFrustum.ExtractFromViewProj(freezeProj * freezeView);
            m_cullOrigin = pos;
            ViewFrustum::ComputeCorners(pos, fwd, right, up, kFovYRad, kAspect169,
                                        kDebugFrustumNear, kDebugFrustumFar, m_frozenCorners);
        }
    }

    UpdateOrbitRects(m_animTime);
}

void SceneQA_StressCulling::OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    m_lastView = viewMatrix;
    m_lastProj = projMatrix;

    lights.SetDirectionalLight(Point3D{0.45f, 0.78f, 0.42f}, Point3D{0.96f, 0.94f, 0.90f}, 32.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();

    // Actualizar frustum activo si no está congelado en modo Debug Camera
    if (!m_freezeFrustum) {
        m_cullFrustum.ExtractFromViewProj(projMatrix * viewMatrix);
        m_cullOrigin = GetCameraPos();
    }

    // 1. Plano suelo de referencia
    {
        MaterialPropertyBlock floorMat{};
        floorMat.ubo.albedoTint[0] = 0.70f; floorMat.ubo.albedoTint[1] = 0.75f; floorMat.ubo.albedoTint[2] = 0.85f;
        floorMat.ubo.pbrParams[0] = 0.72f;
        floorMat.ubo.pbrParams[1] = 0.05f;
        floorMat.ubo.materialParams[3] = 0.35f;
        renderer.RenderStaticMeshPBR(m_floorMesh, Matrix4x4::Translation(0.0f, 0.0f, 0.0f), floorMat, 0u);
    }

    // 2. Frustum Culling + Clasificación en 3 buckets LOD (Zero-Alloc)
    m_lodCounts[0] = 0u;
    m_lodCounts[1] = 0u;
    m_lodCounts[2] = 0u;
    m_visibleCount = 0u;
    m_culledCount = 0u;

    const uint32_t totalInstances = kInstancePresets[m_countPreset];
    for (uint32_t i = 0u; i < totalInstances; ++i) {
        const InstanceRecord& rec = m_instances[i];
        const float bobY = std::sin(m_animTime * 1.3f + rec.phase) * 0.45f;
        const Point3D pos{rec.basePos.x, rec.basePos.y + bobY, rec.basePos.z};

        if (m_cullingEnabled && !m_cullFrustum.TestSphere(pos, rec.radius)) {
            ++m_culledCount;
            continue;
        }

        ++m_visibleCount;
        const float dx = pos.x - m_cullOrigin.x;
        const float dy = pos.y - m_cullOrigin.y;
        const float dz = pos.z - m_cullOrigin.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        const uint32_t lod = m_lodEnabled ? m_lodGroup.SelectLevel(distSq) : 0u;

        const float angle = m_animTime * rec.rotSpeed + rec.phase;
        m_lodBuckets[lod][m_lodCounts[lod]++] = MakeTRSY(pos, angle, rec.scale);
    }

    // 3. Hardware Instancing en mínimos draw calls (lotes de hasta 256 por nivel LOD)
    MaterialPropertyBlock instMat{};
    instMat.ubo.materialParams[0] = 0.0f;
    instMat.ubo.materialParams[1] = 0.0f;
    instMat.ubo.materialParams[3] = 0.42f;

    for (uint32_t lod = 0u; lod < MeshLODGroup::MAX_LEVELS; ++lod) {
        const uint32_t count = m_lodCounts[lod];
        const StaticMesh* mesh = m_lodGroup.GetMesh(lod);
        if (count > 0u && mesh != nullptr && mesh->IsLoaded()) {
            renderer.RenderStaticMeshInstanced(*mesh, m_lodBuckets[lod].data(), count, nullptr, nullptr, &instMat);
        }
    }
    m_instDrawCalls = renderer.GetInstancedDrawCalls();

    // 4. 6 bloques rectangulares orbitales en PBR estándar (reemplazo sin skinning)
    if (m_orbitRectMesh.IsLoaded()) {
        MaterialPropertyBlock rectMat{};
        rectMat.ubo.albedoTint[0] = 0.96f; rectMat.ubo.albedoTint[1] = 0.56f; rectMat.ubo.albedoTint[2] = 0.22f;
        rectMat.ubo.pbrParams[0] = 0.38f;
        rectMat.ubo.pbrParams[1] = 0.15f;
        rectMat.ubo.materialParams[3] = 0.35f;
        for (uint32_t b = 0u; b < ORBIT_RECT_COUNT; ++b) {
            renderer.RenderStaticMeshPBR(m_orbitRectMesh, m_orbitRectModels[b], rectMat, 0u);
        }
    }
}

void SceneQA_StressCulling::DrawFrozenFrustumWireframe() const noexcept {
    DebugDraw3D::SetMatrices(m_lastView, m_lastProj);

    const ColorRGBA nearCol{0, 255, 255, 255};   // Cian brillante (Near)
    const ColorRGBA farCol{255, 230, 40, 255};   // Amarillo (Far)
    const ColorRGBA edgeCol{80, 255, 130, 255};  // Verde (Aristas laterales)

    for (uint32_t i = 0u; i < 4u; ++i) {
        const uint32_t next = (i + 1u) % 4u;
        DebugDraw3D::DrawLine(m_frozenCorners[i],      m_frozenCorners[next],      2.5f, nearCol);
        DebugDraw3D::DrawLine(m_frozenCorners[4u + i], m_frozenCorners[4u + next], 2.5f, farCol);
        DebugDraw3D::DrawLine(m_frozenCorners[i],      m_frozenCorners[4u + i],    2.0f, edgeCol);
    }
}

void SceneQA_StressCulling::OnQARenderHUD() {
    // Dibujar pirámide 3D del frustum congelado dentro del bloque Render2D::Begin()/End()
    if (m_freezeFrustum) {
        DrawFrozenFrustumWireframe();
    }

    Render2D::DrawBox(18, 92, 1080, 126, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    const uint32_t total = kInstancePresets[m_countPreset];
    const float cullPct = (total > 0u) ? (100.0f * static_cast<float>(m_culledCount) / static_cast<float>(total)) : 0.0f;

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1), "INSTANCIAS: %u VISIBLES / %u TOTAL | CULLED: %u (%.1f%%) | HW DRAWS: %u",
                  m_visibleCount, total, m_culledCount, static_cast<double>(cullPct), m_instDrawCalls);
    std::snprintf(line2, sizeof(line2), "LOD DINAM.: %s (LOD0=%u VERDE | LOD1=%u AMAR | LOD2=%u ROJO)",
                  m_lodEnabled ? "ACTIVO" : "APAGADO", m_lodCounts[0], m_lodCounts[1], m_lodCounts[2]);
    std::snprintf(line3, sizeof(line3), "CULLING: %s | DEBUG FRUSTUM: %s | BLOQUES ORBITALES: %u (PBR)",
                  m_cullingEnabled ? "ON" : "OFF",
                  m_freezeFrustum ? "CONGELADO (DEBUG)" : "EN VIVO",
                  ORBIT_RECT_COUNT);

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA{150, 255, 175, 255});
    Render2D::DrawPixelString("CONSEJO: PULSA (X) PARA CONGELAR EL FRUSTUM Y MUEVETE CON L-STICK PARA VER EL CULLING 3D",
                              30, 184, 2, ColorRGBA::Yellow());
}

} // namespace WiiU
