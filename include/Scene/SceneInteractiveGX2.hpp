#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <array>

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
#include <gx2r/buffer.h>
#include <gx2r/surface.h>
#include <gx2r/draw.h>
#include <whb/gfx.h>
#include <gfd.h>
#endif

namespace WiiU {

/**
 * @brief Formato de vértice para renderizado de geometría 3D en silicio GX2.
 * Coincide 1:1 con el shader CafeGLSL `shaders/native_gx2.gsh`.
 */
struct InteractiveGX2Vertex {
    float pos[3];    // Attribute 0: vec3 aPos
    float normal[3]; // Attribute 1: vec3 aNormal
    float color[4];  // Attribute 2: vec4 aColor
};

/**
 * @brief Estructura para el sistema de ondas de choque reactivas al tacto.
 */
struct ShockwaveRingGX2 {
    float x{0.0f};
    float y{0.0f};
    float radius{0.0f};
    float maxRadius{160.0f};
    float alpha{0.0f};
    ColorRGBA color{0, 240, 255, 255};
    bool active{false};
};

/**
 * @brief Demo 8: Sandbox Interactivo 100% Nativo en Hardware GX2 (Latte GPU).
 * 
 * Implementa un entorno interactivo completo en silicio:
 * 1. Objeto 3D facetado con sombreado de hardware Blinn-Phong en shaders CafeGLSL.
 * 2. Z-Buffer acelerado en eDRAM de Latte a 560 GB/s.
 * 3. Widget holográfico arrastrable con físicas de inercia y rebote elástico.
 * 4. Estela de arrastre con desvanecimiento alfa en silicio.
 * 5. Sistema de ondas de choque multitáctiles (hasta 8 anillos concurrentes).
 * 6. HUD de control en silicio mediante Render2DGX2 con retroalimentación acústica.
 * 7. Cero llamadas a funciones de dibujo de SDL2 en Wii U y cero asignaciones en caliente a 60 FPS.
 */
class SceneInteractiveGX2 : public IScene {
public:
    SceneInteractiveGX2();
    ~SceneInteractiveGX2() override;

    SceneInteractiveGX2(const SceneInteractiveGX2&) = delete;
    SceneInteractiveGX2& operator=(const SceneInteractiveGX2&) = delete;
    SceneInteractiveGX2(SceneInteractiveGX2&&) = delete;
    SceneInteractiveGX2& operator=(SceneInteractiveGX2&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return { "click.wav", "shaders/native_gx2.gsh" };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 8: Sandbox Interactivo (100% GX2 Nativo)";
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
    void RenderInteractive2D_GX2();

    void InitCubeGeometry();
    void MutateCubePalette();
    void SpawnShockwave(float x, float y, const ColorRGBA& col);
    void ResetWidgetPosition();

    // Estado de Cámara y Dinámica 3D
    float m_camYaw{0.0f};
    float m_camPitch{0.35f};
    float m_camDistance{5.2f};
    float m_cubeRotX{0.0f};
    float m_cubeRotY{0.0f};
    float m_cubeRotZ{0.0f};
    bool m_autoRotateCube{true};
    bool m_rotateLight{true};
    float m_lightAngle{0.0f};
    Point3D m_lightDir{0.577f, 0.75f, 0.45f};

    // Geometría 3D del Cubo en VRAM
    std::vector<InteractiveGX2Vertex> m_cubeVertices;
    std::vector<uint16_t> m_cubeIndices;
    int m_paletteIndex{0};

    // Widget Interactivo Arrastrable (Holograma con Inercia)
    static constexpr float ANCHOR_X = 595.0f;
    static constexpr float ANCHOR_Y = 315.0f;
    static constexpr float DOCKING_RADIUS = 95.0f;
    static constexpr float OMEGA_0 = 8.5f; // Frecuencia natural para amortiguamiento crítico zeta=1.0

    float m_widgetX{ANCHOR_X};
    float m_widgetY{ANCHOR_Y};
    float m_widgetW{90.0f};
    float m_widgetH{90.0f};
    float m_widgetVx{0.0f};
    float m_widgetVy{0.0f};
    bool m_isDraggingWidget{false};
    bool m_isHomingToAnchor{false};
    float m_dragGrabOffsetX{0.0f};
    float m_dragGrabOffsetY{0.0f};
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};

    // Estela de arrastre (Buffer circular estático)
    static constexpr size_t TRAIL_POINTS = 14;
    struct TrailPoint { float x, y, alpha; };
    std::array<TrailPoint, TRAIL_POINTS> m_trailHistory{};
    size_t m_trailHead{0};
    float m_trailTimer{0.0f};

    // Sistema de Ondas de Choque Táctiles (Cola fija de 8 anillos)
    static constexpr size_t MAX_SHOCKWAVES = 8;
    std::array<ShockwaveRingGX2, MAX_SHOCKWAVES> m_shockwaves{};

    // Telemetría y Rendimiento
    Uint32 m_lastFrameTicks{0};
    float m_smoothedFps{60.0f};
    uint32_t m_gpuDrawCalls{0};

    // Botones del HUD en Silicio
    UIButton m_backButton;
    UIButton m_colorButton;
    UIButton m_rotateButton;
    UIButton m_lightButton;
    UIButton m_resetButton;

#if defined(__WIIU__) || defined(__wiiu__)
    // Recursos de Silicio Latte GX2
    WHBGfxShaderGroup m_shaderGroup{};
    bool m_shaderLoaded{false};
    GX2RBuffer m_vertexBuffer{};
    GX2RBuffer m_indexBuffer{};
    bool m_buffersCreated{false};

    GX2DepthBuffer m_depthBuffer{};
    bool m_depthBufferCreated{false};

    GX2ContextState* m_nativeContextState{nullptr};
    GX2ColorBuffer* m_pColorBuffer{nullptr};
    bool m_colorBufferBound{false};
#endif
};

} // namespace WiiU
