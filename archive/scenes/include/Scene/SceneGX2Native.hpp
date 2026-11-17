#pragma once

#include "Scene/IScene.hpp"
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>

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
 * @brief Vertex format for 100% hardware GX2 rendering with per-vertex position,
 * normal, and RGBA color attributes.
 */
struct NativeGX2Vertex {
    float pos[3];    // Attribute 0: vec3 aPos
    float normal[3]; // Attribute 1: vec3 aNormal
    float color[4];  // Attribute 2: vec4 aColor
};

/**
 * @brief Scene demonstrating 100% Pure GX2 Hardware 3D Rendering (Zero SDL in 3D pipeline).
 * 
 * Features:
 * - Dedicated CafeGLSL Vertex & Pixel Shaders (native_gx2.gsh).
 * - Hardware Z-Buffer (Depth Buffer in Latte eDRAM @ ~560 GB/s bandwidth).
 * - GPU-side Model-View-Projection (MVP) matrix transformations.
 * - Hardware Blinn-Phong per-pixel specular highlights and diffuse shading.
 * - Multi-object 3D scene (central faceted cube + 4 intersecting orbital satellite cubes).
 * - Zero CPU triangle sorting, zero vertex projection cost on Espresso CPU.
 */
class SceneGX2Native : public IScene {
public:
    SceneGX2Native();
    ~SceneGX2Native() override;

    SceneGX2Native(const SceneGX2Native&) = delete;
    SceneGX2Native& operator=(const SceneGX2Native&) = delete;
    SceneGX2Native(SceneGX2Native&&) = delete;
    SceneGX2Native& operator=(SceneGX2Native&&) = delete;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return { "click.wav", "shaders/native_gx2.gsh" };
    }

    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 5: 100% GX2 Nativo (Latte GPU)";
    }

    [[nodiscard]] bool IsPureGX2() const override {
        return true;
    }

private:
    void BuildGeometry();
    bool InitGX2Pipeline();
    void DestroyGX2Pipeline();
    void BindColorBuffer(SDL_Renderer* renderer);
    void RenderSceneGX2();
    void RenderHUD_GX2();

    // Camera & Controls
    float m_camYaw{0.0f};
    float m_camPitch{0.45f};
    float m_camDistance{5.5f};
    bool m_isDraggingCamera{false};
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};

    // Object Rotations & Dynamics
    float m_cubeRotX{0.0f};
    float m_cubeRotY{0.0f};
    float m_cubeRotZ{0.0f};
    float m_orbitAngle{0.0f};

    // Light Parameters
    Point3D m_lightDir{0.5f, 0.85f, 0.35f};

    // UI & Diagnostic State (100% GX2 Nativo)
    bool m_backHover{false};
    bool m_hudToggleHover{false};
    bool m_showHUD{true};

    // Telemetry
    float m_smoothedFps{60.0f};
    Uint32 m_lastFrameTicks{0};
    uint32_t m_gpuDrawCalls{0};
    uint32_t m_totalTriangles{0};

    // Static geometry templates
    std::vector<NativeGX2Vertex> m_cubeVertices;
    std::vector<uint16_t> m_cubeIndices;

    std::vector<NativeGX2Vertex> m_platformVertices;
    std::vector<uint16_t> m_platformIndices;

    bool m_shaderLoaded{false};
    bool m_buffersCreated{false};
    bool m_depthBufferCreated{false};
    bool m_useDirectGFD{false};
    bool m_colorBufferBound{false};

    // Diagnósticos detallados para el HUD
    uint32_t m_diagGshBytes{0};
    bool m_diagLoadOk{false};
    bool m_diagWhbOk{false};
    bool m_diagDirectGfdOk{false};
    bool m_diagVboOk{false};
    bool m_diagIboOk{false};

#if defined(__WIIU__) || defined(__wiiu__)
    WHBGfxShaderGroup m_shaderGroup{};
    GX2RBuffer m_vertexBuffer{};
    GX2RBuffer m_indexBufferCube{};
    GX2RBuffer m_indexBufferPlatform{};
    GX2DepthBuffer m_depthBuffer{};
    GX2ColorBuffer* m_pColorBuffer{nullptr};

    // Fallback de carga directa GFD (sin dependencia de WHB)
    GX2VertexShader* m_directVs{nullptr};
    void* m_directVsProgram{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    void* m_directPsProgram{nullptr};
    GX2FetchShader m_directFetchShader{};
    void* m_directFsProgram{nullptr};
    GX2ContextState* m_nativeContextState{nullptr};
#endif
};

} // namespace WiiU
