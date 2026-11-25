#pragma once

#include "Graphics/Color.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace WiiU {

/**
 * @brief Subsistema de renderizado 2D batcheado en OpenGL 3.3 Core (0 dependencias externas).
 *
 * Paridad directa con `Render2DGX2`:
 * - Espacio lógico fijo de 1280x720 escalado proporcionalmente al framebuffer activo.
 * - Búfer de vértices pre-alocado en CPU (Zero-Alloc en `Begin`/`End`).
 * - Emisión batcheada de quads, cajas, líneas y texto bitmap 5x7.
 */
class Render2DGL {
public:
    static Render2DGL& Instance() noexcept;

    Render2DGL(const Render2DGL&) = delete;
    Render2DGL& operator=(const Render2DGL&) = delete;
    Render2DGL(Render2DGL&&) = delete;
    Render2DGL& operator=(Render2DGL&&) = delete;

    bool Init() noexcept;
    void Shutdown() noexcept;

    void Begin() noexcept;
    void DrawRect(float x, float y, float w, float h, const ColorRGBA& color) noexcept;
    void DrawBox(float x, float y, float w, float h, const ColorRGBA& fillColor, const ColorRGBA& borderColor, float borderThickness = 2.0f) noexcept;
    void DrawLine(float x0, float y0, float x1, float y1, float width, const ColorRGBA& color) noexcept;
    void DrawText(std::string_view text, float x, float y, float scale, const ColorRGBA& color) noexcept;
    void End() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    Render2DGL() noexcept = default;
    ~Render2DGL() = default;

    struct Vertex2D {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };

    static constexpr uint32_t MAX_VERTICES = 32768u;

    void Flush() noexcept;
    void AppendQuad(float x, float y, float w, float h, float r, float g, float b, float a) noexcept;

    std::array<Vertex2D, MAX_VERTICES> m_vertices{};
    uint32_t m_vertexCount{0u};
    uint32_t m_program{0u};
    uint32_t m_vao{0u};
    uint32_t m_vbo{0u};
    bool m_initialized{false};
    bool m_inBatch{false};
};

} // namespace WiiU
