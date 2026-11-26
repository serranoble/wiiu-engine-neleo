#include "Scene/SceneModelBird.hpp"
#include "Core/AssetManager.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/Animation/AnimationLoader.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Color.hpp"
#include "Input/InputManager.hpp"
#include "Audio/AudioManager.hpp"
#include <cstdio>

namespace Neleo {

std::vector<std::string> SceneModelBird::GetRequiredAssets() const {
    return {
        "models/bird_orange.wmesh",
        "models/bird_orange.wskel",
        "models/bird_orange.wanim",
        "models/bird_orange.wtex"
    };
}

void SceneModelBird::LoadAsync() {
    // E/S y parseo CPU en Core 2 (cero llamadas a GX2 fuera de Core 1)
    AssetManager::LoadBinaryFile("models/bird_orange.wmesh", m_stagedBirdMesh);
    m_skelLoaded = AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton) && (m_skeleton.jointCount > 0u);
    m_clipsLoaded = AnimationLoader::LoadClips("models/bird_orange.wanim", m_clips) && !m_clips.empty();
    AssetManager::LoadBinaryFile("models/bird_orange.wtex", m_stagedBirdTex);
}

void SceneModelBird::FreeStagedBuffers() noexcept {
    std::vector<uint8_t>{}.swap(m_stagedBirdMesh);
    std::vector<uint8_t>{}.swap(m_stagedBirdTex);
}

void SceneModelBird::OnQAEnter() {
    SetDefaultCamera(Point3D{0.0f, 2.2f, 6.2f}, 0.0f, -0.15f);

    m_activeClipIdx = 0u;
    m_animPaused = false;
    m_clayMode = false;
    m_useTextures = true;
    m_autoRotate = true;
    m_animTime = 0.0f;
    m_turntableAngle = 0.0f;

    for (uint32_t j = 0u; j < MAX_SKELETON_JOINTS; ++j) {
        m_bonePalette[j] = Matrix4x4::Identity();
    }

    m_savedLightBlock = MaterialRegistry::Instance().GetActiveLightBlock();
    m_lightBlockSaved = true;

    m_birdMeshLoaded = !m_stagedBirdMesh.empty()
        ? m_birdMesh.LoadFromMemory(m_stagedBirdMesh.data(), m_stagedBirdMesh.size())
        : m_birdMesh.LoadFromFile("models/bird_orange.wmesh");
    if (!m_skelLoaded || m_skeleton.jointCount == 0u) {
        m_skelLoaded = AnimationLoader::LoadSkeleton("models/bird_orange.wskel", m_skeleton) && (m_skeleton.jointCount > 0u);
    }
    if (!m_clipsLoaded || m_clips.empty()) {
        m_clipsLoaded = AnimationLoader::LoadClips("models/bird_orange.wanim", m_clips) && !m_clips.empty();
    }
    m_birdTexLoaded = !m_stagedBirdTex.empty()
        ? Graphics::TextureLoaderGX2::LoadCookedTextureFromMemory(
              m_stagedBirdTex.data(), m_stagedBirdTex.size(), m_birdTex, "ModelBird_Tex")
        : Graphics::TextureLoaderGX2::LoadCookedTextureFromFile(
              "models/bird_orange.wtex", m_birdTex, "ModelBird_Tex");

    FreeStagedBuffers();
    EvaluatePose(0.0f);
}

void SceneModelBird::OnQAExit() {
    FreeStagedBuffers();

    const bool hasResources = m_birdMeshLoaded || m_birdTexLoaded;
    if (hasResources) {
        WaitForGpuIdleSafe();
    }

    if (m_birdTexLoaded) {
        Graphics::TextureLoaderGX2::DestroyTexture(m_birdTex);
        m_birdTexLoaded = false;
    }
    if (m_birdMeshLoaded) {
        m_birdMesh.Destroy();
        m_birdMeshLoaded = false;
    }

    std::vector<AnimationClip>{}.swap(m_clips);
    m_clipsLoaded = false;
    m_skelLoaded = false;
    m_skeleton.jointCount = 0u;

    if (m_lightBlockSaved) {
        MaterialRegistry::Instance().GetActiveLightBlock() = m_savedLightBlock;
        m_lightBlockSaved = false;
    }
}

void SceneModelBird::EvaluatePose(float time) noexcept {
    if (!m_skelLoaded || m_skeleton.jointCount == 0u || !m_clipsLoaded || m_clips.empty()) {
        return;
    }

    const uint32_t jointCount = (m_skeleton.jointCount <= MAX_SKELETON_JOINTS)
        ? m_skeleton.jointCount
        : MAX_SKELETON_JOINTS;

    Matrix4x4 localPoses[MAX_SKELETON_JOINTS]{};
    Matrix4x4 globalPoses[MAX_SKELETON_JOINTS]{};
    for (uint32_t j = 0u; j < jointCount; ++j) {
        localPoses[j] = m_skeleton.joints[j].localBindPose;
        globalPoses[j] = Matrix4x4::Identity();
    }

    const AnimationClip& clip = m_clips[m_activeClipIdx % m_clips.size()];
    clip.SamplePose(time, localPoses, jointCount);

    for (uint32_t j = 0u; j < jointCount; ++j) {
        const int16_t parent = m_skeleton.joints[j].parentIndex;
        if (parent >= 0 && static_cast<uint32_t>(parent) < j) {
            globalPoses[j] = globalPoses[parent] * localPoses[j];
        } else {
            globalPoses[j] = localPoses[j];
        }
        m_bonePalette[j] = globalPoses[j] * m_skeleton.joints[j].invBindPose;
    }
}

void SceneModelBird::OnQAUpdate(float dt) {
    bool prevClip = false;
    bool nextClip = false;
    bool togglePause = false;
    bool toggleClay = false;
    bool toggleRotate = false;
    bool toggleTex = false;

    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = InputManager::Instance().GetGamepad(p);
        if (!pad.isConnected) continue;
        if (pad.WasJustPressed(VirtualButton::DPadLeft))  prevClip = true;
        if (pad.WasJustPressed(VirtualButton::DPadRight)) nextClip = true;
        if (pad.WasJustPressed(VirtualButton::A))         togglePause = true;
        if (pad.WasJustPressed(VirtualButton::X))         toggleClay = true;
        if (pad.WasJustPressed(VirtualButton::Minus))     toggleTex = true;
        if (pad.WasJustPressed(VirtualButton::DPadUp) ||
            pad.WasJustPressed(VirtualButton::DPadDown))  toggleRotate = true;
    }

    if ((prevClip || nextClip) && m_clipsLoaded && !m_clips.empty()) {
        AudioManager::Instance().PlayClick();
        const size_t n = m_clips.size();
        m_activeClipIdx = prevClip ? ((m_activeClipIdx + n - 1u) % n) : ((m_activeClipIdx + 1u) % n);
        m_animTime = 0.0f;
    }
    if (togglePause) {
        AudioManager::Instance().PlayClick();
        m_animPaused = !m_animPaused;
    }
    if (toggleClay) {
        AudioManager::Instance().PlayClick();
        m_clayMode = !m_clayMode;
    }
    if (toggleRotate) {
        AudioManager::Instance().PlayClick();
        m_autoRotate = !m_autoRotate;
    }
    if (toggleTex) {
        AudioManager::Instance().PlayClick();
        m_useTextures = !m_useTextures;
    }

    if (!m_animPaused) {
        m_animTime += dt;
    }
    if (m_autoRotate) {
        m_turntableAngle += dt * 0.45f;
        if (m_turntableAngle >= 6.2831853f) {
            m_turntableAngle -= 6.2831853f;
        }
    }

    EvaluatePose(m_animTime);
}

void SceneModelBird::OnQARender3D(const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    (void)viewMatrix;
    (void)projMatrix;

    auto& renderer = ForwardRendererGX2::Instance();
    auto& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    lights.SetDirectionalLight(Point3D{0.55f, 0.85f, 0.42f}, Point3D{1.0f, 0.96f, 0.90f}, 32.0f);
    lights.ClearPointLights();
    lights.ClearSpotLight();

    // 1. Modelo único animado con GPU Skinning (stride 80 B + paleta <= 24 huesos)
    if (m_birdMeshLoaded && m_skelLoaded && m_skeleton.jointCount > 0u) {
        const uint32_t clampedJoints = (m_skeleton.jointCount <= MAX_SKELETON_JOINTS)
            ? m_skeleton.jointCount
            : MAX_SKELETON_JOINTS;
        const Matrix4x4 model = Matrix4x4::RotationY(m_turntableAngle);
        GX2Texture* texPtr = (m_useTextures && m_birdTexLoaded) ? &m_birdTex : nullptr;
        renderer.RenderSkinnedMesh(
            m_birdMesh,
            model,
            m_bonePalette.data(),
            clampedJoints,
            texPtr,
            nullptr,
            0.35f,
            m_clayMode);
    }
}

void SceneModelBird::OnQARenderHUD() {
    Render2D::DrawBox(18, 92, 1080, 104, ColorRGBA{10, 20, 36, 205}, ColorRGBA::Yellow());

    const char* clipName = (m_clipsLoaded && !m_clips.empty())
        ? m_clips[m_activeClipIdx % m_clips.size()].name
        : "NINGUNO";
    const float clipDur = (m_clipsLoaded && !m_clips.empty())
        ? m_clips[m_activeClipIdx % m_clips.size()].duration
        : 0.0f;
    const float sampleTime = (m_clipsLoaded && !m_clips.empty())
        ? m_clips[m_activeClipIdx % m_clips.size()].ResolveSampleTime(m_animTime)
        : 0.0f;

    char line1[128];
    char line2[128];
    char line3[128];
    std::snprintf(line1, sizeof(line1),
                  "ARCHIVOS COOKER: WMESH=%s (%uB) | WSKEL=%s (%u HUESOS) | WANIM=%s | WTEX=%s",
                  m_birdMeshLoaded ? "OK" : "ERR",
                  m_birdMeshLoaded ? m_birdMesh.GetHeader().vertexStride : 0u,
                  m_skelLoaded ? "OK" : "ERR",
                  m_skelLoaded ? m_skeleton.jointCount : 0u,
                  m_clipsLoaded ? "OK" : "ERR",
                  m_birdTexLoaded ? "OK" : "ERR");
    std::snprintf(line2, sizeof(line2),
                  "CLIP [%u/%u]: %s (T=%.2f/%.2fs) | GEOMETRIA: %u VERTS / %u TRIS",
                  m_clipsLoaded ? static_cast<unsigned>((m_activeClipIdx % m_clips.size()) + 1u) : 0u,
                  static_cast<unsigned>(m_clips.size()),
                  clipName,
                  static_cast<double>(sampleTime),
                  static_cast<double>(clipDur),
                  m_birdMeshLoaded ? m_birdMesh.GetVertexCount() : 0u,
                  m_birdMeshLoaded ? (m_birdMesh.GetIndexCount() / 3u) : 0u);
    std::snprintf(line3, sizeof(line3),
                  "ANIM: %s | MODO: %s | WTEX (-): %s | GIRO: %s",
                  m_animPaused ? "PAUSADA" : "ACTIVA",
                  m_clayMode ? "ARCILLA (CLAY)" : "VCOLOR + OJOS",
                  m_useTextures ? "ON" : "OFF",
                  m_autoRotate ? "ACTIVO" : "PAUSA");

    Render2D::DrawPixelString(line1, 30, 104, 2, ColorRGBA::Cyan());
    Render2D::DrawPixelString(line2, 30, 130, 2, ColorRGBA::White());
    Render2D::DrawPixelString(line3, 30, 156, 2, ColorRGBA::Yellow());
}

} // namespace Neleo
