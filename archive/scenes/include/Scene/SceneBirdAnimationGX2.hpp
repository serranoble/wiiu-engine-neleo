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
#include <memory>

#if defined(__WIIU__) || defined(__wiiu__)
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
 * @brief [Hito 97] Escena de Demostración del Pájaro Naranja Animado (BirdOrange FBX) en Silicio GX2.
 * 
 * Ingesta de modelo real FBX articulado mediante el Cooker offline:
 * 1. Malla esquelética en formato `.wmesh` con 3,046 vértices y 5,062 triángulos (stride 80B).
 * 2. Jerarquía topológica de 21 articulaciones (`Skeleton`) y animación de salto/piar (`AnimationClip`, 61 kfs).
 * 3. Deformación Linear Blend Skinning (LBS) acelerada en Vertex Shader CafeGLSL mediante registros uniformes KC2.
 * 4. Colores base del plumaje muestreados desde `BirdOrange_BaseColor.png` por vértice.
 * 5. Pedestal cilíndrico de estudio con sombra de contacto suave.
 * 6. Cámara orbital desacoplada con cinemática elástica Spring-Arm anti-clipping.
 */
class SceneBirdAnimationGX2 : public IScene {
public:
    SceneBirdAnimationGX2();
    ~SceneBirdAnimationGX2() override;

    SceneBirdAnimationGX2(const SceneBirdAnimationGX2&) = delete;
    SceneBirdAnimationGX2& operator=(const SceneBirdAnimationGX2&) = delete;
    SceneBirdAnimationGX2(SceneBirdAnimationGX2&&) = delete;
    SceneBirdAnimationGX2& operator=(SceneBirdAnimationGX2&&) = delete;

    void LoadAsync() override;
    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {
            "click.wav",
            "shaders/skinning_gx2.gsh",
            "models/bird_orange.wmesh",
            "models/bird_orange.wskel",
            "models/bird_orange.wanim"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Pajaro Animado GX2 (Bird Orange FBX)";
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
    void BindColorBuffer(SDL_Renderer* renderer);
    void RenderScene3D_GX2();
    void RenderHUD_GX2();
    void SelectClip(int index);

    // ECS World y Entidades
    ECSWorld m_ecsWorld;
    Entity m_cameraEntity{};
    Entity m_birdEntity{};

    // Datos de animación esquelética
    Skeleton m_skeleton{};
    std::vector<AnimationClip> m_animationClips{};
    int m_activeClipIndex{0};
    float m_playbackSpeed{1.0f};
    bool m_isPaused{false};
    bool m_clayMode{false};

    // Staging buffers cargados asíncronamente en Core 2
    std::vector<uint8_t> m_stagedMesh;
    std::vector<uint8_t> m_stagedSkel;
    std::vector<uint8_t> m_stagedAnim;

    // Búferes GPU GX2R para el pájaro
#if defined(__WIIU__) || defined(__wiiu__)
    GX2RBuffer m_birdVBO{};
    GX2RBuffer m_birdIBO{};
    bool m_birdBuffersCreated{false};
    uint32_t m_birdIndexCount{0};
    uint32_t m_birdVertexCount{0};
#endif

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
    Point3D m_camTarget{0.0f, 1.4f, 0.0f};
    Point3D m_targetCamTarget{0.0f, 1.4f, 0.0f};
    float m_camYaw{0.45f};
    float m_targetCamYaw{0.45f};
    float m_camPitch{0.20f};
    float m_targetCamPitch{0.20f};
    float m_camDistance{5.2f};
    float m_targetCamDistance{5.2f};
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
