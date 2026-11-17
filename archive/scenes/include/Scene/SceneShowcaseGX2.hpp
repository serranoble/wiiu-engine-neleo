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
#include <gx2/event.h>
#include <gx2r/buffer.h>
#include <gx2r/surface.h>
#include <gx2r/draw.h>
#include <whb/gfx.h>
#include <gfd.h>
#endif

namespace WiiU {

/**
 * @brief [Hito 109] Escena de Demostración de Técnicas Avanzadas de Shading GX2.
 * 
 * Implementación de técnicas fundamentales de Gordon & Clevenger (Capítulos 9 y 10):
 * 1. Fondo de Estudio Diáfano / Diorama (Clean Studio Backdrop) optimizado con GX2 Fast Clear.
 * 2. Normal Mapping / Micro-Relieve en espacio tangente (TBN) calculado analíticamente
 *    mediante derivadas en pantalla (dFdx/dFdy) sobre el modelo de la Casa Holandesa (.wmesh).
 * 3. Plano continuo de terreno con textura de césped (ground_grass.wtex) y filtrado anisotrópico 4x.
 * 4. Pájaro naranja dinámico con deformación Linear Blend Skinning (21 huesos) en silicio GX2
 *    y selector interactivo de animaciones (Salto, Piar, Mirar, Todo).
 */
class SceneShowcaseGX2 : public IScene {
public:
    SceneShowcaseGX2();
    ~SceneShowcaseGX2() override;

    SceneShowcaseGX2(const SceneShowcaseGX2&) = delete;
    SceneShowcaseGX2& operator=(const SceneShowcaseGX2&) = delete;
    SceneShowcaseGX2(SceneShowcaseGX2&&) = delete;
    SceneShowcaseGX2& operator=(SceneShowcaseGX2&&) = delete;

    void LoadAsync() override;
    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {
            "click.wav",
            "shaders/skydome_gx2.gsh",
            "shaders/advanced_mesh_gx2.gsh",
            "shaders/advanced_skel_gx2.gsh",
            "models/skybox_day.wcube",
            "models/skydome.wmesh",
            "models/skydome.wtex",
            "models/dutch_house.wmesh",
            "models/dutch_house_props.wmesh",
            "models/dutch_house.wtex",
            "models/dutch_house_props.wtex",
            "models/ground_plane.wmesh",
            "models/ground_grass.wtex",
            "models/bird_orange.wmesh",
            "models/bird_orange.wskel",
            "models/bird_orange.wanim",
            "models/bird_orange.wtex"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Materiales Avanzados GX2 (Diorama Estudio + Normal Map)";
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
    bool CreateTextures();
    bool LoadCookedCubeMap(std::string_view filepath);
    bool LoadCookedTexture(std::string_view filepath, ResourceHandle<GX2Texture>& outHandle, const char* debugLabel);
    void BindColorBuffer(SDL_Renderer* renderer);
    void RenderScene3D_GX2();
    void RenderHUD_GX2();
    void SelectClip(int index);

    // ECS World y Entidades
    ECSWorld m_ecsWorld;
    Entity m_cameraEntity{};
    Entity m_skydomeEntity{};
    Entity m_houseEntity{};
    Entity m_propsEntity{};
    Entity m_groundEntity{};
    Entity m_birdEntity{};

    // Datos de la Malla Estática (Skydome, Casa y Suelo)
    StaticMesh m_skydomeMesh;
    StaticMesh m_houseMesh;
    StaticMesh m_propsMesh;
    StaticMesh m_groundMesh;

    // Datos de animación esquelética y pájaro
    Skeleton m_skeleton{};
    std::vector<AnimationClip> m_animationClips{};
    int m_activeClipIndex{0};
    float m_playbackSpeed{1.0f};
    bool m_isPaused{false};

    // Parámetros interactivos de Shading
    bool m_normalMapEnabled{true};
    int m_reflectionModeIndex{2}; // 0 = 0%, 1 = 25%, 2 = 50%, 3 = 100%
    bool m_clayMode{false};

    // Staging buffers cargados asíncronamente en Core 2
    std::vector<uint8_t> m_stagedCubeMap;
    std::vector<uint8_t> m_stagedSkydomeMesh;
    std::vector<uint8_t> m_stagedHouseMesh;
    std::vector<uint8_t> m_stagedPropsMesh;
    std::vector<uint8_t> m_stagedGroundMesh;
    std::vector<uint8_t> m_stagedBirdMesh;
    std::vector<uint8_t> m_stagedBirdSkel;
    std::vector<uint8_t> m_stagedBirdAnim;

    // Búferes GPU GX2R para el pájaro
#if defined(__WIIU__) || defined(__wiiu__)
    GX2RBuffer m_birdVBO{};
    GX2RBuffer m_birdIBO{};
    bool m_birdBuffersCreated{false};
    uint32_t m_birdIndexCount{0};
    uint32_t m_birdVertexCount{0};

    // Recursos de textura
    GX2Texture m_skyboxTexture{};
    bool m_skyboxTextureCreated{false};
    GX2Sampler m_skyboxSampler{};
    GX2Sampler m_meshSampler{};
    GX2Sampler m_skydomeSampler{};

    ResourcePool<GX2Texture, 8> m_texturePool{};
    ResourceHandle<GX2Texture> m_hSkydomeTexture{};
    ResourceHandle<GX2Texture> m_hHouseTexture{};
    ResourceHandle<GX2Texture> m_hPropsTexture{};
    ResourceHandle<GX2Texture> m_hBirdTexture{};
    ResourceHandle<GX2Texture> m_hGrassTexture{};
    bool m_texturesCreated{false};

    // Shader 0: Skydome Hemisférico (doom_sky11)
    GX2VertexShader* m_skyVs{nullptr};
    void* m_skyVsProgram{nullptr};
    GX2PixelShader* m_skyPs{nullptr};
    void* m_skyPsProgram{nullptr};
    GX2FetchShader m_skyFs{};
    void* m_skyFsProgram{nullptr};
    bool m_skyShaderLoaded{false};

    // Shader 1: Advanced Mesh (Casa Holandesa con Normal Map y Reflexión)
    GX2VertexShader* m_meshVs{nullptr};
    void* m_meshVsProgram{nullptr};
    GX2PixelShader* m_meshPs{nullptr};
    void* m_meshPsProgram{nullptr};
    GX2FetchShader m_meshFs{};
    void* m_meshFsProgram{nullptr};
    bool m_meshShaderLoaded{false};

    // Shader 2: Advanced Skinned (Pájaro Naranja con LBS y Reflexión)
    GX2VertexShader* m_skelVs{nullptr};
    void* m_skelVsProgram{nullptr};
    GX2PixelShader* m_skelPs{nullptr};
    void* m_skelPsProgram{nullptr};
    GX2FetchShader m_skelFs{};
    void* m_skelFsProgram{nullptr};
    bool m_skelShaderLoaded{false};

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
    float m_camYaw{0.55f};
    float m_targetCamYaw{0.55f};
    float m_camPitch{0.22f};
    float m_targetCamPitch{0.22f};
    float m_camDistance{8.8f};
    float m_targetCamDistance{8.8f};
    bool m_autoRotate{false};
    bool m_isDraggingCamera{false};
    float m_dragStartX{0.0f};
    float m_dragStartY{0.0f};
    float m_initialYaw{0.55f};
    float m_initialPitch{0.22f};

    // Telemetría de rendimiento a 60 FPS
    float m_fps{60.0f};
    float m_frameTimeMs{16.6f};
    float m_fpsTimer{0.0f};
    int m_frameCount{0};
    uint32_t m_drawCallsCount{0};
    uint32_t m_trianglesCount{0};
};

} // namespace WiiU
