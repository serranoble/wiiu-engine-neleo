#pragma once

#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include <string_view>
#include <vector>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/state.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <gfd.h>
#endif

namespace Neleo {

/**
 * @brief Formato de vértice compacto (32 B = 1 línea L1D Espresso) para renderizado 2D GX2.
 * Coincide 1:1 con el shader CafeGLSL `ui2d_gx2.gsh` y el atlas de fuente bitmap 5x7.
 */
struct Render2DVertexGX2 {
    float pos[2];   // Attribute 0: vec2 aPos (x, y en espacio virtual 1280x720)
    float uv[2];    // Attribute 1: vec2 aTexCoord (atlas de fuente o celda blanca sólida)
    float color[4]; // Attribute 2: vec4 aColor (r, g, b, a normalizados 0..1)
};
static_assert(sizeof(Render2DVertexGX2) == 32u, "Render2DVertexGX2 must be 32 bytes (1 Espresso L1D line)");

/**
 * @brief Subsistema de renderizado 2D 100% nativo sobre la GPU AMD Latte (GX2).
 * 
 * Diseñado específicamente para UI y HUD en escenas nativas 100% libres de librerías externas:
 * - Dibuja rectángulos, bordes, botones y texto bitmap 5x7 directamente en silicio GX2.
 * - Proyección ortográfica automática en coordenadas de pantalla (1280x720).
 * - Agrupamiento por lotes (Batching) con emisión en 1 sola llamada GPU (GX2DrawEx).
 * - Cero asignaciones de memoria en el bucle caliente a 60 FPS.
 */
class Render2DGX2 {
public:
    static Render2DGX2& Instance() {
        static Render2DGX2 instance;
        return instance;
    }

    bool Init();
    void Shutdown();

    // Inicia un nuevo lote de dibujo 2D (configura proyección ortográfica, shaders y estados)
    void Begin();

    // Dibuja un rectángulo sólido
    void DrawRect(float x, float y, float w, float h, const ColorRGBA& color);

    // Dibuja una caja con relleno y borde
    void DrawBox(float x, float y, float w, float h, const ColorRGBA& fillColor, 
                 const ColorRGBA& borderColor, float borderWidth = 2.0f);

    // Dibuja texto pixel art 5x7 escalado
    void DrawText(std::string_view text, float x, float y, float scale, const ColorRGBA& color);

    // Dibuja un botón interactivo estilizado
    void DrawButton(std::string_view label, float x, float y, float w, float h, bool isHovered,
                    const ColorRGBA& normalFill, const ColorRGBA& hoverFill,
                    const ColorRGBA& normalBorder, const ColorRGBA& hoverBorder,
                    const ColorRGBA& textColor, float textScale = 2.0f);

    // Dibuja un triángulo con colores individuales por vértice
    void DrawTriangle(float x0, float y0, const ColorRGBA& c0,
                      float x1, float y1, const ColorRGBA& c1,
                      float x2, float y2, const ColorRGBA& c2);

    // Dibuja un cuadrilátero con colores individuales por vértice (para gradientes)
    void DrawQuad(float x0, float y0, const ColorRGBA& c0,
                  float x1, float y1, const ColorRGBA& c1,
                  float x2, float y2, const ColorRGBA& c2,
                  float x3, float y3, const ColorRGBA& c3);

    // Dibuja un disco/círculo sólido o con gradiente radial (centro hacia borde)
    void DrawCircle(float cx, float cy, float radius, const ColorRGBA& centerColor,
                    const ColorRGBA& edgeColor, int segments = 12);

    // Dibuja una línea con grosor en espacio 2D
    void DrawLine(float x0, float y0, float x1, float y1, float width, const ColorRGBA& color);

    // Emite el lote completo de vértices acumulados a la GPU Latte
    void End();

    // Avanza el anillo de búferes VRAM al finalizar el fotograma completo
    void NextFrame() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool IsFontAtlasReady() const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
        return m_initialized && m_fontAtlasCreated;
#else
        return m_initialized;
#endif
    }
    [[nodiscard]] size_t GetQueuedVertices() const noexcept { return m_vertexBatch.size(); }

private:
    Render2DGX2() = default;
    ~Render2DGX2() = default;

    Render2DGX2(const Render2DGX2&) = delete;
    Render2DGX2& operator=(const Render2DGX2&) = delete;
    Render2DGX2(Render2DGX2&&) = delete;
    Render2DGX2& operator=(Render2DGX2&&) = delete;

    static constexpr size_t MAX_VERTICES = 48000;
    static constexpr size_t BUFFER_SLOTS = 3;
    std::vector<Render2DVertexGX2> m_vertexBatch;
    bool m_initialized{false};

#if defined(__WIIU__) || defined(__wiiu__)
    GX2RBuffer m_vertexBuffers[BUFFER_SLOTS]{};
    bool m_bufferSlotCreated[BUFFER_SLOTS]{};
    uint32_t m_currentBufferSlot{0};
    uint32_t m_bufferVertexOffset{0};
    bool m_shaderLoaded{false};
    bool m_bufferCreated{false};
    bool m_useDirectGFD{false};

    GX2VertexShader* m_directVs{nullptr};
    void* m_directVsProgram{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    void* m_directPsProgram{nullptr};
    GX2FetchShader m_directFetchShader{};
    void* m_directFsProgram{nullptr};

    GX2Texture m_fontAtlas{};
    GX2Sampler m_fontSampler{};
    int32_t m_fontSamplerSlot{0};
    bool m_fontAtlasCreated{false};
#endif
};

} // namespace Neleo
