#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include "Core/ECS/Components/StudioTurntableComponent.hpp"
#include "Core/ECS/Components/SpringArmComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"
#include "Core/ECS/Components/AnimatorComponent.hpp"
#include "Graphics/Animation/Skeleton.hpp"
#include "Graphics/Animation/AnimationClip.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/UI/UIToolkit.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/clear.h>
#include <gx2/state.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/utils.h>
#include <gx2r/buffer.h>
#include <gx2r/surface.h>
#include <gx2r/draw.h>
#include <whb/gfx.h>
#include <gfd.h>
#endif

namespace WiiU {

/**
 * @brief [Hito 95 / Fase 5.2] Escena de Animación Esquelética y Linear Blend Skinning (LBS) en Silicio GX2.
 * 
 * Implementa el modelo de personajes articulados de Jason Gregory (Game Engine Architecture, Cap. 11):
 * 1. Malla esquelética en formato `.wmesh` con pesos y huesos en GPU (stride 80B, VBO alineado a 256B).
 * 2. Jerarquía topológica de articulaciones (`Skeleton`, 16 huesos) y pistas de keyframes (`AnimationClip`).
 * 3. Deformación Linear Blend Skinning (LBS) acelerada en Vertex Shader CafeGLSL mediante registros uniformes KC2.
 * 4. Cámara orbital elástica desacoplada con Spring-Arm anti-clipping y prevención de suelo.
 * 5. Cero alocaciones dinámicas (0 bytes malloc/new) en el hot path a 60 FPS.
 */
class SceneSkeletalAnimationGX2 : public IScene {
public:
    SceneSkeletalAnimationGX2();
    ~SceneSkeletalAnimationGX2() override;

    void LoadAsync() override;
    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {
            "click.wav",
            "shaders/skinning_gx2.gsh",
            "models/character.wmesh",
            "models/character.wskel",
            "models/character.wanim"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Animacion Esqueletica GX2 (LBS Hardware)";
    }

    [[nodiscard]] bool IsPureGX2() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

private:
    bool InitGX2Pipeline();
    void DestroyGX2Pipeline();
    void CreateStudioPedestalMeshes();
    void BindColorBuffer(SDL_Renderer* renderer);
    void RenderScene3D_GX2();
    void RenderHUD_GX2();
    void TriggerClipCrossFade(int clipIdx);
    void ToggleOverlayLayer();
    void CycleIKDemoMode();
    void ResetDemoCamera();
    void UpdateIKDemoTargets();
    void DrawIKDebugOverlay() const;

    // ECS World y Entidades
    ECSWorld m_ecsWorld;
    Entity m_cameraEntity{};
    Entity m_characterEntity{};
    Entity m_pedestalEntity{};
    Entity m_shadowEntity{};

    // Datos de animación esquelética y demostración Cap. 12 (Blending & IK)
    Skeleton m_skeleton{};
    std::vector<AnimationClip> m_animationClips{};
    int m_activeClipIndex{0};
    float m_playbackSpeed{1.0f};
    bool m_isPaused{false};
    bool m_clayMode{false};
    bool m_layerBlendActive{false};
    int m_ikDemoMode{1}; // 0=OFF, 1=Foot+LookAt IK, 2=Full IK + FABRIK
    float m_demoOrbitTime{0.0f};
    Point3D m_lookAtTargetPos{0.4f, 2.2f, 2.4f};
    Point3D m_leftFootTargetPos{0.35f, 0.72f, 0.18f};
    Point3D m_rightFootTargetPos{-0.35f, 0.40f, 0.12f};
    Point3D m_fabrikTargetPos{1.05f, 2.35f, 0.65f};

    // Staging buffers cargados asíncronamente en Core 2
    std::vector<uint8_t> m_stagedMesh;
    std::vector<uint8_t> m_stagedSkel;
    std::vector<uint8_t> m_stagedAnim;

    // Búferes GPU GX2R para el personaje
#if defined(__WIIU__) || defined(__wiiu__)
    GX2RBuffer m_characterVBO{};
    GX2RBuffer m_characterIBO{};
    bool m_characterBuffersCreated{false};
    uint32_t m_characterIndexCount{0};
    uint32_t m_characterVertexCount{0};
#endif

    // Mallas auxiliares (Pedestal y Sombra)
    StaticMesh m_pedestalMesh;
    StaticMesh m_shadowMesh;
    bool m_pedestalCreated{false};

    // Recursos de Textura de Estudio
    ResourceHandle<GX2Texture> m_hPedestalTexture{};
    ResourceHandle<GX2Texture> m_hShadowTexture{};
    ResourcePool<GX2Texture, 16> m_texturePool;
    bool m_texturesCreated{false};

    // Pipeline GX2 y Shaders
#if defined(__WIIU__) || defined(__wiiu__)
    WHBGfxShaderGroup m_shaderGroup{};
    bool m_shaderLoaded{false};

    GX2VertexShader* m_directVs{nullptr};
    void* m_directVsProgram{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    void* m_directPsProgram{nullptr};
    GX2FetchShader m_directFs{};
    void* m_directFsProgram{nullptr};

    GX2DepthBuffer m_depthBuffer{};
    bool m_depthBufferCreated{false};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};
#else
    void* m_pColorBuffer{nullptr};
#endif

    // Cámara orbital y cinemática elástica
    Point3D m_camTarget{0.0f, 1.8f, 0.0f};
    Point3D m_targetCamTarget{0.0f, 1.8f, 0.0f};
    float m_camYaw{0.45f};
    float m_targetCamYaw{0.45f};
    float m_camPitch{0.20f};
    float m_targetCamPitch{0.20f};
    float m_camDistance{5.5f};
    float m_targetCamDistance{5.5f};
    bool m_autoRotate{false};
    bool m_isDraggingCamera{false};
    float m_dragStartX{0.0f};
    float m_dragStartY{0.0f};
    float m_initialYaw{0.45f};
    float m_initialPitch{0.20f};

    // Telemetría de rendimiento a 60 FPS
    float m_fps{60.0f};
    float m_frameTimeMs{16.6f};
    float m_fpsTimer{0.0f};
    int m_frameCount{0};
    uint32_t m_drawCallsCount{0};
    uint32_t m_trianglesCount{0};
};

} // namespace WiiU
