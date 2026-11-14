#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <array>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/shaders.h>
#include <gx2/surface.h>
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
 * @brief Formato de vértice para renderizado 3D de luz y sombras nativo en silicio GX2.
 * Coincide 1:1 con `shadows_gx2.vert` y `shaders/shadows_gx2.gsh`.
 */
struct ShadowsGX2Vertex {
    float pos[3];    // Attribute 0: vec3 aPos
    float normal[3]; // Attribute 1: vec3 aNormal
    float color[4];  // Attribute 2: vec4 aColor
};

/**
 * @brief Demo 9: Escena 100% Nativa GX2 de Iluminación Puntual y Sombras en Silicio.
 * 
 * Implementa una simulación lumínica física completa sobre AMD Latte:
 * 1. Fuente de luz central con atenuación física inversa del cuadrado de la distancia.
 * 2. Brillo de la luz controlable interactivamente en tiempo real mediante un slider táctil.
 * 3. 6 geometrías tridimensionales variadas apoyadas sobre un terreno simple de losas de mármol.
 * 4. Proyección analítica de sombras arrojadas con gradiente de penumbra difusa sobre el terreno.
 * 5. Orbe de luz central emisivo con pulsación y paletas de color conmutables.
 * 6. Z-Buffer acelerado por hardware en eDRAM a 560 GB/s.
 * 7. HUD y Slider 100% en silicio con Render2DGX2 y cero alocaciones en el bucle a 60 FPS.
 */
class SceneShadowsGX2 : public IScene {
public:
    SceneShadowsGX2();
    ~SceneShadowsGX2() override;

    SceneShadowsGX2(const SceneShadowsGX2&) = delete;
    SceneShadowsGX2& operator=(const SceneShadowsGX2&) = delete;
    SceneShadowsGX2(SceneShadowsGX2&&) = delete;
    SceneShadowsGX2& operator=(SceneShadowsGX2&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return { "click.wav", "shaders/shadows_gx2.gsh" };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 9: Luz y Terreno 3D (100% GX2 Nativo)";
    }

    [[nodiscard]] bool IsPureGX2() const noexcept override {
#if defined(__WIIU__) || defined(__wiiu__)
        return true;
#else
        return false;
#endif
    }

private:
    void InitGeometry();
    void BuildFloorMesh();
    void BuildObjectsMesh();
    void BuildLightOrbMesh();

    bool InitGX2Pipeline();
    void DestroyGX2Pipeline();
    void BindColorBuffer(SDL_Renderer* renderer);

    void RenderScene3D_GX2();
    void RenderHUD_GX2();

    // Geometría en memoria del host
    std::vector<ShadowsGX2Vertex> m_floorVertices;
    std::vector<uint16_t> m_floorIndices;

    std::vector<ShadowsGX2Vertex> m_objectsVertices;
    std::vector<uint16_t> m_objectsIndices;

    std::vector<ShadowsGX2Vertex> m_lightOrbVertices;
    std::vector<uint16_t> m_lightOrbIndices;

    // Recursos de hardware GX2 / VRAM (Búfer de vértices unificado idéntico a SceneGX2Native)
#if defined(__WIIU__) || defined(__wiiu__)
    GX2RBuffer m_vertexBuffer{};
    GX2RBuffer m_floorIBO{};
    GX2RBuffer m_objectsIBO{};
    GX2RBuffer m_lightOrbIBO{};

    GX2DepthBuffer m_depthBuffer{};
    void* m_depthBufferMem{nullptr};
    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};

    // Cargador WHB / Direct GFD
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
    bool m_depthBufferCreated{false};
    bool m_colorBufferBound{false};

    // Estado físico de la luz central (Fija en X=0, Z=0)
    float m_lightBrightness{1.0f};      // Rango 0.0f a 2.0f (0% a 200%)
    float m_lightHeight{2.2f};          // Altura actual interpolada de la luz
    float m_targetLightHeight{2.2f};    // Altura objetivo al conmutar con el botón ALTURA
    float m_lightX{0.0f};
    float m_lightY{2.2f};
    float m_lightZ{0.0f};
    int m_lightColorIndex{0};           // 0: Oro Cálido, 1: Blanco Puro, 2: Cian Sheikah, 3: Fuego Solar

    // Cámara orbital 3D con amortiguamiento exponencial (OPT-164)
    float m_camPitch{0.48f};            // Ángulo vertical actual
    float m_camYaw{0.85f};              // Ángulo horizontal actual
    float m_camDistance{9.2f};          // Distancia actual al centro de la escena
    float m_targetCamPitch{0.48f};      // Ángulo vertical objetivo
    float m_targetCamYaw{0.85f};        // Ángulo horizontal objetivo
    float m_targetCamDistance{9.2f};    // Distancia objetivo
    bool m_autoRotateCam{false};

    // Interacción táctil
    bool m_isDraggingSlider{false};
    bool m_isDraggingCamera{false};
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};

    // Telemetría
    float m_smoothedFps{60.0f};
    uint32_t m_gpuDraws{0};
    uint32_t m_triangleCount{0};

    // Trazado de rayos y resolución dinámica (Inicia en 1080p por defecto)
    bool m_raytracingEnabled{true};     // Ray Tracing GX activado por defecto
    int m_resPresetIndex{0};            // 0: 1080p Full HD (2.07M px), 1: 720p nativo (922K px), 2: 648p escalado (746K px)
};

} // namespace WiiU
