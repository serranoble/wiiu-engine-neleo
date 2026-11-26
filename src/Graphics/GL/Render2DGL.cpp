#include "Graphics/GL/Render2DGL.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include "Graphics/Render2D.hpp"
#include <cmath>
#include <cstdint>

namespace Neleo {

namespace {

constexpr const char* kVertShader2D =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    float ndcX = (aPos.x / 1280.0) * 2.0 - 1.0;\n"
    "    float ndcY = 1.0 - (aPos.y / 720.0) * 2.0;\n"
    "    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

constexpr const char* kFragShader2D =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vColor;\n"
    "}\n";

Graphics::GL::GLuint CompileShader(Graphics::GL::GLenum type, const char* src) noexcept {
    const auto& gl = Graphics::GL::GetFunctions();
    if (!gl.CreateShader || !gl.ShaderSource || !gl.CompileShader || !gl.GetShaderiv) return 0u;
    const Graphics::GL::GLuint shader = gl.CreateShader(type);
    if (shader == 0u) return 0u;
    gl.ShaderSource(shader, 1, &src, nullptr);
    gl.CompileShader(shader);
    Graphics::GL::GLint compiled = 0;
    gl.GetShaderiv(shader, Graphics::GL::GL_COMPILE_STATUS_VAL, &compiled);
    if (compiled == 0) {
        if (gl.DeleteShader) gl.DeleteShader(shader);
        return 0u;
    }
    return shader;
}

} // namespace

Render2DGL& Render2DGL::Instance() noexcept {
    static Render2DGL s_instance;
    return s_instance;
}

bool Render2DGL::Init() noexcept {
    if (m_initialized) return true;
    if (!Graphics::GL::IsLoaded() && !Graphics::GL::LoadFunctions()) {
        return false;
    }
    const auto& gl = Graphics::GL::GetFunctions();
    if (!gl.CreateProgram || !gl.GenVertexArrays || !gl.GenBuffers) {
        return false;
    }

    const Graphics::GL::GLuint vs = CompileShader(Graphics::GL::GL_VERTEX_SHADER_VAL, kVertShader2D);
    const Graphics::GL::GLuint fs = CompileShader(Graphics::GL::GL_FRAGMENT_SHADER_VAL, kFragShader2D);
    if (vs == 0u || fs == 0u) {
        if (vs && gl.DeleteShader) gl.DeleteShader(vs);
        if (fs && gl.DeleteShader) gl.DeleteShader(fs);
        return false;
    }

    const Graphics::GL::GLuint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    Graphics::GL::GLint linked = 0;
    gl.GetProgramiv(prog, Graphics::GL::GL_LINK_STATUS_VAL, &linked);
    if (linked == 0) {
        gl.DeleteProgram(prog);
        return false;
    }

    Graphics::GL::GLuint vao = 0u;
    Graphics::GL::GLuint vbo = 0u;
    gl.GenVertexArrays(1, &vao);
    gl.GenBuffers(1, &vbo);

    gl.BindVertexArray(vao);
    gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, vbo);
    gl.BufferData(Graphics::GL::GL_ARRAY_BUFFER_VAL,
                  static_cast<Graphics::GL::GLsizeiptr>(MAX_VERTICES * sizeof(Vertex2D)),
                  nullptr,
                  Graphics::GL::GL_DYNAMIC_DRAW_VAL);

    gl.EnableVertexAttribArray(0u);
    gl.VertexAttribPointer(0u, 2, Graphics::GL::GL_FLOAT_VAL, Graphics::GL::GL_FALSE_VAL,
                           static_cast<Graphics::GL::GLsizei>(sizeof(Vertex2D)), nullptr);
    gl.EnableVertexAttribArray(1u);
    gl.VertexAttribPointer(1u, 4, Graphics::GL::GL_FLOAT_VAL, Graphics::GL::GL_FALSE_VAL,
                           static_cast<Graphics::GL::GLsizei>(sizeof(Vertex2D)),
                           reinterpret_cast<const void*>(sizeof(float) * 2u));

    gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, 0u);
    gl.BindVertexArray(0u);

    m_program = prog;
    m_vao = vao;
    m_vbo = vbo;
    m_vertexCount = 0u;
    m_initialized = true;
    return true;
}

void Render2DGL::Shutdown() noexcept {
    if (!m_initialized) return;
    const auto& gl = Graphics::GL::GetFunctions();
    if (gl.DeleteBuffers && m_vbo != 0u) {
        const Graphics::GL::GLuint b = m_vbo;
        gl.DeleteBuffers(1, &b);
    }
    if (gl.DeleteVertexArrays && m_vao != 0u) {
        const Graphics::GL::GLuint a = m_vao;
        gl.DeleteVertexArrays(1, &a);
    }
    if (gl.DeleteProgram && m_program != 0u) {
        gl.DeleteProgram(m_program);
    }
    m_vbo = 0u;
    m_vao = 0u;
    m_program = 0u;
    m_vertexCount = 0u;
    m_inBatch = false;
    m_initialized = false;
}

void Render2DGL::Begin() noexcept {
    if (!m_initialized && !Init()) return;
    m_vertexCount = 0u;
    m_inBatch = true;

    const auto& gl = Graphics::GL::GetFunctions();
    if (gl.Disable) {
        gl.Disable(Graphics::GL::GL_DEPTH_TEST_VAL);
        gl.Disable(Graphics::GL::GL_CULL_FACE_VAL);
        gl.Disable(Graphics::GL::GL_SCISSOR_TEST_VAL);
    }
    if (gl.Enable) {
        gl.Enable(Graphics::GL::GL_BLEND_VAL);
    }
    if (gl.BlendFunc) {
        gl.BlendFunc(Graphics::GL::GL_SRC_ALPHA_VAL, Graphics::GL::GL_ONE_MINUS_SRC_ALPHA_VAL);
    }
}

void Render2DGL::Flush() noexcept {
    if (m_vertexCount == 0u || !m_initialized) return;
    const auto& gl = Graphics::GL::GetFunctions();
    gl.UseProgram(m_program);
    gl.BindVertexArray(m_vao);
    gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, m_vbo);
    gl.BufferSubData(Graphics::GL::GL_ARRAY_BUFFER_VAL, 0,
                     static_cast<Graphics::GL::GLsizeiptr>(m_vertexCount * sizeof(Vertex2D)),
                     m_vertices.data());
    gl.DrawArrays(Graphics::GL::GL_TRIANGLES_VAL, 0, static_cast<Graphics::GL::GLsizei>(m_vertexCount));
    gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, 0u);
    gl.BindVertexArray(0u);
    gl.UseProgram(0u);
    m_vertexCount = 0u;
}

void Render2DGL::AppendQuad(float x, float y, float w, float h, float r, float g, float b, float a) noexcept {
    if (!m_initialized || w <= 0.0f || h <= 0.0f) return;
    if (m_vertexCount + 6u > MAX_VERTICES) {
        Flush();
    }

    const float x2 = x + w;
    const float y2 = y + h;
    Vertex2D* v = &m_vertices[m_vertexCount];
    v[0] = {x,  y,  r, g, b, a};
    v[1] = {x2, y,  r, g, b, a};
    v[2] = {x2, y2, r, g, b, a};
    v[3] = {x,  y,  r, g, b, a};
    v[4] = {x2, y2, r, g, b, a};
    v[5] = {x,  y2, r, g, b, a};
    m_vertexCount += 6u;
}

void Render2DGL::DrawRect(float x, float y, float w, float h, const ColorRGBA& color) noexcept {
    if (color.a == 0u || w <= 0.0f || h <= 0.0f) return;
    constexpr float kInv255 = 1.0f / 255.0f;
    AppendQuad(x, y, w, h,
               static_cast<float>(color.r) * kInv255,
               static_cast<float>(color.g) * kInv255,
               static_cast<float>(color.b) * kInv255,
               static_cast<float>(color.a) * kInv255);
}

void Render2DGL::DrawBox(float x, float y, float w, float h, const ColorRGBA& fillColor, const ColorRGBA& borderColor, float borderThickness) noexcept {
    if (fillColor.a > 0u) {
        DrawRect(x, y, w, h, fillColor);
    }
    if (borderColor.a > 0u && borderThickness > 0.0f) {
        const float t = borderThickness;
        DrawRect(x, y, w, t, borderColor);
        DrawRect(x, y + h - t, w, t, borderColor);
        DrawRect(x, y + t, t, h - 2.0f * t, borderColor);
        DrawRect(x + w - t, y + t, t, h - 2.0f * t, borderColor);
    }
}

void Render2DGL::DrawLine(float x0, float y0, float x1, float y1, float width, const ColorRGBA& color) noexcept {
    if (color.a == 0u || !m_initialized) return;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq < 0.0001f) {
        const float halfW = (width > 1.0f) ? (width * 0.5f) : 1.0f;
        DrawRect(x0 - halfW, y0 - halfW, halfW * 2.0f, halfW * 2.0f, color);
        return;
    }

    if (m_vertexCount + 6u > MAX_VERTICES) {
        Flush();
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    const float halfW = (width > 1.0f ? width : 1.5f) * 0.5f;
    const float nx = -dy * invLen * halfW;
    const float ny =  dx * invLen * halfW;

    constexpr float kInv255 = 1.0f / 255.0f;
    const float r = static_cast<float>(color.r) * kInv255;
    const float g = static_cast<float>(color.g) * kInv255;
    const float b = static_cast<float>(color.b) * kInv255;
    const float a = static_cast<float>(color.a) * kInv255;

    Vertex2D* v = &m_vertices[m_vertexCount];
    v[0] = {x0 + nx, y0 + ny, r, g, b, a};
    v[1] = {x1 + nx, y1 + ny, r, g, b, a};
    v[2] = {x1 - nx, y1 - ny, r, g, b, a};
    v[3] = {x0 + nx, y0 + ny, r, g, b, a};
    v[4] = {x1 - nx, y1 - ny, r, g, b, a};
    v[5] = {x0 - nx, y0 - ny, r, g, b, a};
    m_vertexCount += 6u;
}

void Render2DGL::DrawText(std::string_view text, float x, float y, float scale, const ColorRGBA& color) noexcept {
    if (scale <= 0.0f || text.empty() || color.a == 0u || !m_initialized) return;

    constexpr float kInv255 = 1.0f / 255.0f;
    const float r = static_cast<float>(color.r) * kInv255;
    const float g = static_cast<float>(color.g) * kInv255;
    const float b = static_cast<float>(color.b) * kInv255;
    const float a = static_cast<float>(color.a) * kInv255;

    float curX = x;
    uint8_t cols[5]{};
    for (char c : text) {
        Render2D::GetFont5x7(c, cols);
        for (int col = 0; col < 5; ++col) {
            const uint8_t line = cols[col];
            if (line == 0u) continue;
            for (int row = 0; row < 7; ++row) {
                if ((line & (1u << row)) != 0u) {
                    AppendQuad(curX + static_cast<float>(col) * scale,
                               y + static_cast<float>(row) * scale,
                               scale, scale, r, g, b, a);
                }
            }
        }
        curX += 6.0f * scale;
    }
}

void Render2DGL::End() noexcept {
    if (!m_inBatch) return;
    Flush();
    m_inBatch = false;

    const auto& gl = Graphics::GL::GetFunctions();
    if (gl.Disable) {
        gl.Disable(Graphics::GL::GL_BLEND_VAL);
    }
    if (gl.Enable) {
        gl.Enable(Graphics::GL::GL_DEPTH_TEST_VAL);
    }
}

} // namespace Neleo
