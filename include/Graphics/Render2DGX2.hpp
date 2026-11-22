#pragma once

#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include <string_view>
#include <vector>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/shaders.h>
#include <gx2/surface.h>
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

namespace WiiU {

/**
 * @brief Formato de vértice para renderizado 2D nativo por hardware GX2.
 * Coincide 1:1 con el shader CafeGLSL `native_gx2.gsh`.
 */
struct Render2DVertexGX2 {
    float pos[3];    // Attribute 0: vec3 aPos (x, y, 0.0f)
    float normal[3]; // Attribute 1: vec3 aNormal (0.0f, 0.0f, 1.0f)
    float color[4];  // Attribute 2: vec4 aColor (r, g, b, a normalizados 0..1)
};

/**
 * @brief Subsistema de renderizado 2D 100% nativo sobre la GPU AMD Latte (GX2).
 * 
 * Diseñado específicamente para UI y HUD en escenas que prescinden completamente de SDL2:
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
#endif
};

} // namespace WiiU
