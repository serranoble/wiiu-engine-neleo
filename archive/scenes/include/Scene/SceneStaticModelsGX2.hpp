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
#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/CameraSystem.hpp"
#include "Core/ECS/Systems/CullingSystem.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <string_view>
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
 * @brief Escena de Modelos Estáticos (.wmesh / .wtex) en Silicio GX2.
 * 
 * Renderiza la Casa Holandesa (Dutch House) en el centro de la escena sobre fondo negro:
 * 1. Carga directa a VRAM de `dutch_house.wmesh`, `dutch_house_props.wmesh` y `dutch_house_alpha.wmesh`.
 * 2. Carga de texturas cocinadas `.wtex` (512x512 RGBA8) sin decodificación en caliente.
 * 3. Orquestación mediante ECS C++20 (`ECSWorld`, `TransformSystem`, `CameraSystem`, `CullingSystem` en Core 2).
 * 4. Cero asignaciones dinámicas (0 bytes malloc/new) en el bucle de 60 FPS.
 */
class SceneStaticModelsGX2 : public IScene {
public:
    SceneStaticModelsGX2();
    ~SceneStaticModelsGX2() override;

    SceneStaticModelsGX2(const SceneStaticModelsGX2&) = delete;
    SceneStaticModelsGX2& operator=(const SceneStaticModelsGX2&) = delete;
    SceneStaticModelsGX2(SceneStaticModelsGX2&&) = delete;
    SceneStaticModelsGX2& operator=(SceneStaticModelsGX2&&) = delete;

    void LoadAsync() override;
    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {
            "click.wav",
            "shaders/environment_gx2.gsh",
            "models/dutch_house.wmesh",
            "models/dutch_house_props.wmesh",
            "models/dutch_house_alpha.wmesh",
            "models/dutch_house.wtex",
            "models/dutch_house_props.wtex",
            "models/dutch_house_alpha.wtex"
        };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Modelos Estaticos: Casa Holandesa (100% GX2)";
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

    bool CreateModelTextures();
    bool LoadCookedTexture(std::string_view filepath, const std::vector<uint8_t>& stagedBuffer, ResourceHandle<GX2Texture>& outHandle, const char* debugLabel);
    bool CreateSolidTexture(ResourceHandle<GX2Texture>& outHandle, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char* debugLabel);
    bool CreateContactShadowTexture(ResourceHandle<GX2Texture>& outHandle, const char* debugLabel);
    bool CreateStudioPedestalMeshes();
    void UpdateSamplerFilter();
    void DestroyModelTextures();

    void RenderScene3D_GX2();
    void RenderHUD_GX2();

#if defined(__WIIU__) || defined(__wiiu__)
    ResourcePool<GX2Texture, 8> m_texturePool;
    ResourceHandle<GX2Texture> m_hHouseTexture;
    ResourceHandle<GX2Texture> m_hPropsTexture;
    ResourceHandle<GX2Texture> m_hAlphaTexture;
    ResourceHandle<GX2Texture> m_hClayTexture;
    GX2Sampler m_sampler{};
    bool m_texturesCreated{false};

    GX2DepthBuffer m_depthBuffer{};
    bool m_depthBufferCreated{false};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};

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

    // Modelos 3D Estáticos Cocinados (.wmesh) de la Casa Holandesa
    StaticMesh m_houseMesh;
    StaticMesh m_propsMesh;
    StaticMesh m_alphaMesh;
    bool m_modelsLoaded{false};

    // Búferes de Staging en RAM (Pilar 2 — Precarga asíncrona en Core 2 libre de llamadas GX2)
    std::vector<uint8_t> m_stagedHouseTex;
    std::vector<uint8_t> m_stagedPropsTex;
    std::vector<uint8_t> m_stagedAlphaTex;
    std::vector<uint8_t> m_stagedHouseMesh;
    std::vector<uint8_t> m_stagedPropsMesh;
    std::vector<uint8_t> m_stagedAlphaMesh;
    bool m_asyncStaged{false};

    // Subsistema ECS
    ECSWorld m_ecsWorld;
    Entity m_houseEntity{};
    Entity m_propsEntity{};
    Entity m_alphaEntity{};
    Entity m_cameraEntity{};

    // Cámara Orbital de Estudio
    Point3D m_camTarget{0.0f, 2.55f, 0.0f};
    Point3D m_targetCamTarget{0.0f, 2.55f, 0.0f};
    float m_camYaw{0.65f};
    float m_camPitch{0.28f};
    float m_camDistance{10.5f};
    float m_targetCamYaw{0.65f};
    float m_targetCamPitch{0.28f};
    float m_targetCamDistance{10.5f};
    bool m_isDraggingCamera{false};
    float m_dragStartX{0.0f};
    float m_dragStartY{0.0f};
    float m_initialYaw{0.0f};
    float m_initialPitch{0.0f};

    // Opciones Interactivas del Visor de Modelos
    float m_animTime{0.0f};
    bool m_autoRotate{true};
    bool m_showProps{true};
    bool m_clayMode{false};
    bool m_showPedestal{true};  // Plataforma de estudio con sombra de contacto suave
    int m_bumpPresetIndex{1};   // 0: OFF (0%), 1: SUAVE (50%), 2: NORMAL (100%), 3: FUERTE (150%)
    // [OPT-325] Ciclo Dinámico de Día y Noche (1 minuto por etapa, 4 etapas = 240s)
    float m_dayNightTime{60.0f}; // Inicia en 60.0s (Mediodía, luz plena y clara)
    bool m_dayNightAuto{true};   // Progresión continua automática a 60s/etapa
    int m_resPresetIndex{3};    // 0: 480p, 1: 648p, 2: 720p, 3: 1080p (Inicia en 1080p por defecto)
    int m_filterPresetIndex{0}; // 0: Point (Sin filtros activos), 1: Bilineal, ..., 6: Aniso 16x (Inicia sin filtros)
    int m_stressPresetIndex{0}; // 0: 1 Casa, 1: 4 Casas (2x2), 2: 9 Casas (3x3), 3: 16 Casas (4x4) [Prueba de Estrés]

    // Entidades del Pedestal de Estudio y Sombra de Contacto
    Entity m_pedestalEntity{};
    Entity m_shadowEntity{};
    ResourceHandle<GX2Texture> m_hPedestalTexture;
    ResourceHandle<GX2Texture> m_hShadowTexture;
    StaticMesh m_pedestalMesh;
    StaticMesh m_shadowMesh;
    bool m_pedestalCreated{false};

    // Telemetría
    uint32_t m_lastFrameTicks{0};
    float m_smoothedFps{60.0f};
};

} // namespace WiiU
