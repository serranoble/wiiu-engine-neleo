#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/UIComponents.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ArenaVector.hpp"
#include "Audio/Audio3DTypes.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/AudioEmitterComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/LightComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/HierarchyComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/AudioSpatialSystem.hpp"
#include "Core/ECS/Systems/CameraSystem.hpp"
#include "Core/ECS/Systems/CullingSystem.hpp"
#include "Core/ECS/Systems/KinematicCharacterSystem.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/RHI/DisplayCompositor.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <array>
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
 * @brief Formato de vértice para renderizado 3D de entorno y geometrías texturizadas en silicio GX2.
 * Coincide 1:1 con `environment_gx2.vert` y `shaders/environment_gx2.gsh`.
 */
struct EnvironmentVertex {
    float pos[3];     // location 0: vec3 aPos
    float normal[3];  // location 1: vec3 aNormal
    float texCoord[2];// location 2: vec2 aTexCoord
    float color[4];   // location 3: vec4 aColor
};

/**
 * @brief Demo 10: Escena de Mundo Texturizado 3D y Entorno Dinámico (100% GX2 Nativo).
 * 
 * Implementa una simulación de mundo 3D completa sobre AMD Latte:
 * 1. Carga y muestreo de texturas GX2 en VRAM (pasto, ladrillos de muralla, piedra de roca).
 * 2. Terreno continuo con elevaciones armónicas y cuenca de laguna.
 * 3. Muralla de 1 metro de altura con textura de ladrillos y textura de piedra en roca poliédrica.
 * 4. Laguna con agua transparente, oleaje de río suave y sombreado Fresnel en eDRAM.
 * 5. Bóveda celeste con sol dinámico, efecto glow/corona solar y nubes procedurales con oclusión y sombras en tiempo real.
 * 6. Ergonomía táctil con Render2DGX2 y control de hora/viento/nubes.
 * 7. Cero asignaciones en el hot path a 60 FPS y gestión segura en MemoryManager.
 */
class SceneEnvironmentGX2 : public IScene {
public:
    SceneEnvironmentGX2();
    ~SceneEnvironmentGX2() override;

    SceneEnvironmentGX2(const SceneEnvironmentGX2&) = delete;
    SceneEnvironmentGX2& operator=(const SceneEnvironmentGX2&) = delete;
    SceneEnvironmentGX2(SceneEnvironmentGX2&&) = delete;
    SceneEnvironmentGX2& operator=(SceneEnvironmentGX2&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return { "click.wav", "shaders/environment_gx2.gsh" };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 10: Mundo Texturizado y Entorno Dinamico (100% GX2)";
    }

    [[nodiscard]] bool IsPureGX2() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

private:
    void BuildAndUploadMeshes();
    void BuildTerrainMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);
    void BuildWallMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);
    void BuildRockMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);
    void BuildLagoonMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);
    void BuildSkyDomeMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);
    void BuildSunOrbMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);
    void BuildPlayerMesh(ArenaVector<EnvironmentVertex>& verts, ArenaVector<uint16_t>& indices);

    bool InitGX2Pipeline();
    void DestroyGX2Pipeline();
    void BindColorBuffer(SDL_Renderer* renderer);

    bool CreateProceduralTextures();
    void DestroyProceduralTextures();

    void RenderScene3D_GX2();
    void RenderHUD_GX2();

    // Recursos de hardware GX2 / VRAM (VBOs e IBOs dedicados)
#if defined(__WIIU__) || defined(__wiiu__)
    GX2RBuffer m_terrainVBO{};
    GX2RBuffer m_terrainIBO{};
    GX2RBuffer m_wallVBO{};
    GX2RBuffer m_wallIBO{};
    GX2RBuffer m_rockVBO{};
    GX2RBuffer m_rockIBO{};
    GX2RBuffer m_lagoonVBO{};
    GX2RBuffer m_lagoonIBO{};
    GX2RBuffer m_skyDomeVBO{};
    GX2RBuffer m_skyDomeIBO{};
    GX2RBuffer m_sunOrbVBO{};
    GX2RBuffer m_sunOrbIBO{};
    GX2RBuffer m_playerVBO{};
    GX2RBuffer m_playerIBO{};

    // Texturas nativas GX2 administradas mediante ResourcePool & ResourceHandle (OPT-213)
    ResourcePool<GX2Texture, 4> m_texturePool;
    ResourceHandle<GX2Texture> m_hGrassTexture;
    ResourceHandle<GX2Texture> m_hBrickTexture;
    ResourceHandle<GX2Texture> m_hRockTexture;
    GX2Sampler m_sampler{};
    bool m_texturesCreated{false};

    GX2DepthBuffer m_depthBuffer{};
    bool m_depthBufferCreated{false};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};

    // Shaders Direct GFD / WHB
    WHBGfxShaderGroup m_shaderGroup{};
    bool m_useDirectGFD{false};
    void* m_directVsProgram{nullptr};
    void* m_directPsProgram{nullptr};
    void* m_directFsProgram{nullptr};
    GX2VertexShader* m_directVs{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    GX2FetchShader m_directFs{};
#endif

    bool m_shaderLoaded{false};
    bool m_buffersCreated{false};

    // Parámetros de Cámara Orbital y Navegación
    Point3D m_camTarget{0.0f, 0.8f, 0.0f};
    Point3D m_targetCamTarget{0.0f, 0.8f, 0.0f};
    float m_camYaw{0.85f};
    float m_camPitch{0.48f};
    float m_camDistance{15.5f};
    float m_targetCamYaw{0.85f};
    float m_targetCamPitch{0.48f};
    float m_targetCamDistance{15.5f};
    bool m_isDraggingCamera{false};
    bool m_autoRotateCam{false};
    float m_dragStartX{0.0f};
    float m_dragStartY{0.0f};
    float m_initialYaw{0.0f};
    float m_initialPitch{0.0f};

    // Modo de Cámara
    enum class CameraMode : uint8_t {
        ThirdPersonFollow,
        FreeOrbit
    };
    CameraMode m_cameraMode{CameraMode::ThirdPersonFollow};

    // Parámetros de Simulación y Entorno
    float m_animTime{0.0f};
    int m_timeOfDayIndex{1}; // 0: Mañana, 1: Mediodía, 2: Atardecer
    int m_cloudCoverageIndex{1}; // 0: Despejado (0.25), 1: Parcial (0.55), 2: Cubierto (0.82)
    int m_windSpeedIndex{1}; // 0: Calma, 1: Brisa, 2: Fuerte
    bool m_waterActive{true};
    bool m_sunGlowActive{true};

    // Telemetría
    uint32_t m_lastFrameTicks{0};
    float m_smoothedFps{60.0f};
    float m_measuredSunOcclusion{0.0f};
    float m_measuredLagoonOcclusion{0.0f};
    float m_measuredSunGlow{1.0f};
    float m_playerHorizontalSpeed{0.0f};
    float m_playerVerticalSpeed{0.0f};
    bool m_playerGrounded{false};

    // Subsistema ECS y Entidades de Entorno (OPT-251..OPT-254)
    ECSWorld m_ecsWorld;
    Entity m_terrainEntity{};
    Entity m_wallEntity{};
    Entity m_rockEntity{};
    Entity m_lagoonEntity{};
    Entity m_sunEntity{};
    Entity m_windEntity{};
    Entity m_cameraEntity{};
    Entity m_drcCameraEntity{};
    Entity m_playerEntity{};

    void RenderPass3D(const CameraComponent& camera,
                      const Point3D& camPos,
                      const Matrix4x4& vpMatrix,
                      const Point3D& sunDir,
                      float sunGlow,
                      float cloudCoverage,
                      const Point3D& windDir);

    void AddVertex(ArenaVector<EnvironmentVertex>& verts,
                   float px, float py, float pz,
                   float nx, float ny, float nz,
                   float u, float v,
                   const ColorRGBA& col);
};

} // namespace WiiU
