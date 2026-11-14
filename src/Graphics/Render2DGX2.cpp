#include "Graphics/Render2DGX2.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/NativeGX2ShaderBinary.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cmath>
#include <cstring>
#include <malloc.h>

namespace WiiU {

bool Render2DGX2::Init() {
    if (m_initialized) {
        return true;
    }

    m_vertexBatch.reserve(MAX_VERTICES);

#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Cargar Shader Group de CafeGLSL (shaders/native_gx2.gsh)
    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/native_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(NATIVE_GX2_GSH_DATA, NATIVE_GX2_GSH_DATA + NATIVE_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            // Carga Directa GFD (100% independiente de WHB y de heaps no inicializados)
                uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
                uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
                uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
                uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

                if (vsHeaderSize > 0 && vsProgSize > 0 && psHeaderSize > 0 && psProgSize > 0) {
                    m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::UI, "R2DGX2_VS_Hdr"));
                    m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::UI, "R2DGX2_VS_Prog");
                    m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::UI, "R2DGX2_PS_Hdr"));
                    m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::UI, "R2DGX2_PS_Prog");

                    if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                        BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, shaderData.data());
                        BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, shaderData.data());
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                        if (vsOk && psOk) {
                            GX2AttribStream attrs[3];
                            std::memset(attrs, 0, sizeof(attrs));
                            attrs[0].location = 0;
                            attrs[0].buffer = 0;
                            attrs[0].offset = offsetof(Render2DVertexGX2, pos);
                            attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                            attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[0].aluDivisor = 0;
                            attrs[0].mask = 0x00010205;
                            attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[1].location = 1;
                            attrs[1].buffer = 0;
                            attrs[1].offset = offsetof(Render2DVertexGX2, normal);
                            attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
                            attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[1].aluDivisor = 0;
                            attrs[1].mask = 0x00010205;
                            attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            attrs[2].location = 2;
                            attrs[2].buffer = 0;
                            attrs[2].offset = offsetof(Render2DVertexGX2, color);
                            attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                            attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                            attrs[2].aluDivisor = 0;
                            attrs[2].mask = 0x00010203;
                            attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                            uint32_t fsSize = GX2CalcFetchShaderSizeEx(3, GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                                       GX2_TESSELLATION_MODE_DISCRETE);
                            m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::UI, "R2DGX2_FS_Prog");
                            if (m_directFsProgram) {
                                GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 3, attrs,
                                                     GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                     GX2_TESSELLATION_MODE_DISCRETE);
                                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);
                                m_useDirectGFD = true;
                                m_shaderLoaded = true;
                            }
                        }
                    }
                }
            }
        }

    // 2. Crear Búfer Dinámico de Vértices en VRAM con Double-Buffering (GX2R)
    if (m_shaderLoaded && !m_bufferCreated) {
        bool allBuffersOk = true;
        for (size_t i = 0; i < BUFFER_SLOTS; ++i) {
            m_vertexBuffers[i].flags = static_cast<GX2RResourceFlags>(
                GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
            m_vertexBuffers[i].elemSize = sizeof(Render2DVertexGX2);
            m_vertexBuffers[i].elemCount = static_cast<uint32_t>(MAX_VERTICES);

            if (!GX2RCreateBuffer(&m_vertexBuffers[i])) {
                allBuffersOk = false;
            }
        }
        m_bufferCreated = allBuffersOk;
        m_currentBufferSlot = 0;
    }

    m_initialized = (m_shaderLoaded && m_bufferCreated);
    return m_initialized;
#else
    m_initialized = true;
    return true;
#endif
}

void Render2DGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_bufferCreated) {
        for (size_t i = 0; i < BUFFER_SLOTS; ++i) {
            GX2RDestroyBufferEx(&m_vertexBuffers[i], GX2R_RESOURCE_BIND_NONE);
        }
        m_bufferCreated = false;
        m_currentBufferSlot = 0;
    }

    if (m_useDirectGFD) {
        if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); }
        if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); }
        if (m_directPs) { void* raw = static_cast<void*>(m_directPs); MemoryManager::Instance().Free(raw); m_directPs = nullptr; }
        if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); }
        if (m_directVs) { void* raw = static_cast<void*>(m_directVs); MemoryManager::Instance().Free(raw); m_directVs = nullptr; }
        m_useDirectGFD = false;
        m_shaderLoaded = false;
    }
#endif
    m_vertexBatch.clear();
    m_vertexBatch.shrink_to_fit();
    m_initialized = false;
}

void Render2DGX2::Begin() {
    if (!m_initialized) {
        Init();
    }

    m_vertexBatch.clear();

#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_shaderLoaded || !m_bufferCreated || !m_useDirectGFD || !m_directVs || !m_directPs) return;

    // 1. Vincular Fetch Shader y Shaders de Vértices y Píxeles
    GX2SetFetchShader(&m_directFetchShader);
    GX2SetVertexShader(m_directVs);
    GX2SetPixelShader(m_directPs);

    // 2. Configurar Uniforms de Proyección Ortográfica (1280x720) y Modelo Identidad
    Matrix4x4 ortho = Matrix4x4::Orthographic(0.0f, 1280.0f, 720.0f, 0.0f, -1.0f, 1.0f);
    Matrix4x4 model = Matrix4x4::Identity();

    float vsUniforms[32];
    std::memcpy(&vsUniforms[0], ortho.m, 16 * sizeof(float));
    std::memcpy(&vsUniforms[16], model.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 32, vsUniforms);

    // 3. Configurar Uniforms del Pixel Shader para renderizado 2D unshaded
    float psUniforms[16];
    // KC2[0]: uLightDir (w = 0 desactiva componente difusa y especular)
    psUniforms[0] = 0.0f; psUniforms[1] = 0.0f; psUniforms[2] = 1.0f; psUniforms[3] = 0.0f;
    // KC2[1]: uLightColor
    psUniforms[4] = 1.0f; psUniforms[5] = 1.0f; psUniforms[6] = 1.0f; psUniforms[7] = 1.0f;
    // KC2[2]: uCamPos
    psUniforms[8] = 0.0f; psUniforms[9] = 0.0f; psUniforms[10] = 5.0f; psUniforms[11] = 32.0f;
    // KC2[3]: uAmbient (rgb = 1.0 entrega el color exacto del vértice sin atenuación)
    psUniforms[12] = 1.0f; psUniforms[13] = 1.0f; psUniforms[14] = 1.0f; psUniforms[15] = 0.0f;
    GX2SetPixelUniformReg(0, 16, psUniforms);

    // 4. Estados de Rasterización para UI 2D Superpuesta mediante RHI Pipeline Cache
    RHIPipelineState state(false, false, RHI_COMPARE_ALWAYS, BlendMode::AlphaBlend, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);

    GX2SetViewport(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    GX2SetScissor(0, 0, 1280, 720);
#endif
}

void Render2DGX2::DrawRect(float x, float y, float w, float h, const ColorRGBA& color) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (m_vertexBatch.size() + 6 > MAX_VERTICES) return;

    const float r = static_cast<float>(color.r) * (1.0f / 255.0f);
    const float g = static_cast<float>(color.g) * (1.0f / 255.0f);
    const float b = static_cast<float>(color.b) * (1.0f / 255.0f);
    const float a = static_cast<float>(color.a) * (1.0f / 255.0f);

    // [OPT-312] Alineación estricta de píxel a texel (Jason Gregory, Game Engine Architecture 3rd Ed., Cap. 14.5)
    // Erradica el difuminado por interpolación sub-píxel y muestreo fraccionario en silicio AMD Latte
    const float x0 = std::floor(x + 0.5f);
    const float y0 = std::floor(y + 0.5f);
    const float x1 = std::floor(x + w + 0.5f);
    const float y1 = std::floor(y + h + 0.5f);

    const Render2DVertexGX2 v0 = { {x0, y0, 0.0f}, {0.0f, 0.0f, 1.0f}, {r, g, b, a} };
    const Render2DVertexGX2 v1 = { {x1, y0, 0.0f}, {0.0f, 0.0f, 1.0f}, {r, g, b, a} };
    const Render2DVertexGX2 v2 = { {x0, y1, 0.0f}, {0.0f, 0.0f, 1.0f}, {r, g, b, a} };
    const Render2DVertexGX2 v3 = { {x1, y1, 0.0f}, {0.0f, 0.0f, 1.0f}, {r, g, b, a} };

    // Triángulo 1 (CCW)
    m_vertexBatch.push_back(v0);
    m_vertexBatch.push_back(v2);
    m_vertexBatch.push_back(v1);

    // Triángulo 2 (CCW)
    m_vertexBatch.push_back(v1);
    m_vertexBatch.push_back(v2);
    m_vertexBatch.push_back(v3);
}

void Render2DGX2::DrawBox(float x, float y, float w, float h, const ColorRGBA& fillColor,
                          const ColorRGBA& borderColor, float borderWidth) {
    // 1. Relleno interior
    if (fillColor.a > 0) {
        DrawRect(x, y, w, h, fillColor);
    }

    // 2. Borde exterior
    if (borderColor.a > 0 && borderWidth > 0.0f) {
        // Superior
        DrawRect(x, y, w, borderWidth, borderColor);
        // Inferior
        DrawRect(x, y + h - borderWidth, w, borderWidth, borderColor);
        // Izquierda
        DrawRect(x, y + borderWidth, borderWidth, h - 2.0f * borderWidth, borderColor);
        // Derecha
        DrawRect(x + w - borderWidth, y + borderWidth, borderWidth, h - 2.0f * borderWidth, borderColor);
    }
}

void Render2DGX2::DrawText(std::string_view text, float x, float y, float scale, const ColorRGBA& color) {
    if (text.empty() || scale <= 0.0f) return;

    // [OPT-313] Cuantización global de escala: toda llamada con escala fraccionaria (1.5f, 1.6f, 1.85f...)
    // se redondea al entero más cercano (mínimo 1x). Una fuente bitmap 5x7 sólo es nítida a escala entera.
    scale = std::floor(scale + 0.5f);
    if (scale < 1.0f) scale = 1.0f;

    // [OPT-312] Ajuste a cuadrícula entera de píxeles y cuantización de glifos bitmap 5x7
    float curX = std::floor(x + 0.5f);
    const float startY = std::floor(y + 0.5f);
    uint8_t cols[5];

    for (char c : text) {
        Render2D::GetFont5x7(c, cols);
        // Optimización por líneas horizontales: agrupar píxeles contiguos por fila (RLE)
        for (int row = 0; row < 7; ++row) {
            int startCol = -1;
            int runLen = 0;
            for (int col = 0; col < 5; ++col) {
                if (cols[col] & (1 << row)) {
                    if (startCol < 0) {
                        startCol = col;
                        runLen = 1;
                    } else {
                        runLen++;
                    }
                } else {
                    if (startCol >= 0) {
                        const float px0 = std::floor(curX + static_cast<float>(startCol) * scale + 0.5f);
                        float px1 = std::floor(curX + static_cast<float>(startCol + runLen) * scale + 0.5f);
                        const float py0 = std::floor(startY + static_cast<float>(row) * scale + 0.5f);
                        float py1 = std::floor(startY + static_cast<float>(row + 1) * scale + 0.5f);
                        if (px1 <= px0) px1 = px0 + 1.0f;
                        if (py1 <= py0) py1 = py0 + 1.0f;
                        DrawRect(px0, py0, px1 - px0, py1 - py0, color);
                        startCol = -1;
                        runLen = 0;
                    }
                }
            }
            if (startCol >= 0) {
                const float px0 = std::floor(curX + static_cast<float>(startCol) * scale + 0.5f);
                float px1 = std::floor(curX + static_cast<float>(startCol + runLen) * scale + 0.5f);
                const float py0 = std::floor(startY + static_cast<float>(row) * scale + 0.5f);
                float py1 = std::floor(startY + static_cast<float>(row + 1) * scale + 0.5f);
                if (px1 <= px0) px1 = px0 + 1.0f;
                if (py1 <= py0) py1 = py0 + 1.0f;
                DrawRect(px0, py0, px1 - px0, py1 - py0, color);
            }
        }
        curX += std::floor(6.0f * scale + 0.5f);
    }
}

void Render2DGX2::DrawButton(std::string_view label, float x, float y, float w, float h, bool isHovered,
                             const ColorRGBA& normalFill, const ColorRGBA& hoverFill,
                             const ColorRGBA& normalBorder, const ColorRGBA& hoverBorder,
                             const ColorRGBA& textColor, float textScale) {
    const ColorRGBA& bg = isHovered ? hoverFill : normalFill;
    const ColorRGBA& border = isHovered ? hoverBorder : normalBorder;

    DrawBox(x, y, w, h, bg, border, 2.0f);

    // [OPT-312] Cuantización de escala a múltiplos enteros (1x, 2x, 3x) para nitidez absoluta de fuente bitmap
    // Si el texto supera el ancho con escala 2x+, se degrada limpiamente a escala entera 1x (5x7 nítida)
    // en lugar de usar factores de punto flotante arbitrarios que difuminan los glifos.
    float effectiveScale = std::floor(textScale + 0.5f);
    if (effectiveScale < 1.0f) effectiveScale = 1.0f;

    const float maxTextWidth = w - 8.0f;
    float rawWidth = static_cast<float>(label.length() * 6) * effectiveScale;
    if (rawWidth > maxTextWidth && effectiveScale > 1.0f) {
        effectiveScale = 1.0f;
        rawWidth = static_cast<float>(label.length() * 6) * effectiveScale;
    }

    const float textWidth = rawWidth;
    const float textHeight = 7.0f * effectiveScale;
    const float tx = std::floor(x + (w - textWidth) * 0.5f);
    const float ty = std::floor(y + (h - textHeight) * 0.5f);

    DrawText(label, tx, ty, effectiveScale, textColor);
}

void Render2DGX2::DrawTriangle(float x0, float y0, const ColorRGBA& c0,
                               float x1, float y1, const ColorRGBA& c1,
                               float x2, float y2, const ColorRGBA& c2) {
    if (m_vertexBatch.size() + 3 > MAX_VERTICES) return;

    constexpr float inv255 = 1.0f / 255.0f;
    const Render2DVertexGX2 v0 = { {x0, y0, 0.0f}, {0.0f, 0.0f, 1.0f},
        {c0.r * inv255, c0.g * inv255, c0.b * inv255, c0.a * inv255} };
    const Render2DVertexGX2 v1 = { {x1, y1, 0.0f}, {0.0f, 0.0f, 1.0f},
        {c1.r * inv255, c1.g * inv255, c1.b * inv255, c1.a * inv255} };
    const Render2DVertexGX2 v2 = { {x2, y2, 0.0f}, {0.0f, 0.0f, 1.0f},
        {c2.r * inv255, c2.g * inv255, c2.b * inv255, c2.a * inv255} };

    m_vertexBatch.push_back(v0);
    m_vertexBatch.push_back(v1);
    m_vertexBatch.push_back(v2);
}

void Render2DGX2::DrawQuad(float x0, float y0, const ColorRGBA& c0,
                           float x1, float y1, const ColorRGBA& c1,
                           float x2, float y2, const ColorRGBA& c2,
                           float x3, float y3, const ColorRGBA& c3) {
    if (m_vertexBatch.size() + 6 > MAX_VERTICES) return;
    DrawTriangle(x0, y0, c0, x1, y1, c1, x2, y2, c2);
    DrawTriangle(x0, y0, c0, x2, y2, c2, x3, y3, c3);
}

void Render2DGX2::DrawCircle(float cx, float cy, float radius, const ColorRGBA& centerColor,
                             const ColorRGBA& edgeColor, int segments) {
    if (radius <= 0.0f || segments < 3) return;
    if (m_vertexBatch.size() + static_cast<size_t>(segments * 3) > MAX_VERTICES) return;

    const float stepAngle = 6.2831853f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float a0 = static_cast<float>(i) * stepAngle;
        float a1 = static_cast<float>(i + 1) * stepAngle;
        float x0 = cx + std::cos(a0) * radius;
        float y0 = cy + std::sin(a0) * radius;
        float x1 = cx + std::cos(a1) * radius;
        float y1 = cy + std::sin(a1) * radius;
        DrawTriangle(cx, cy, centerColor, x0, y0, edgeColor, x1, y1, edgeColor);
    }
}

void Render2DGX2::DrawLine(float x0, float y0, float x1, float y1, float width, const ColorRGBA& color) {
    if (m_vertexBatch.size() + 6 > MAX_VERTICES) return;
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float invLen = (width * 0.5f) / len;
    float px = -dy * invLen;
    float py = dx * invLen;
    DrawQuad(x0 + px, y0 + py, color,
             x1 + px, y1 + py, color,
             x1 - px, y1 - py, color,
             x0 - px, y0 - py, color);
}

void Render2DGX2::End() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!m_initialized || m_vertexBatch.empty()) {
        m_vertexBatch.clear();
        return;
    }

    // Subir vértices acumulados al búfer de vértices activo en VRAM (Double-Buffering Ping-Pong)
    GX2RBuffer& activeBuf = m_vertexBuffers[m_currentBufferSlot];
    void* ptr = GX2RLockBufferEx(&activeBuf, GX2R_RESOURCE_BIND_NONE);
    if (ptr) {
        std::memcpy(ptr, m_vertexBatch.data(), m_vertexBatch.size() * sizeof(Render2DVertexGX2));
        GX2RUnlockBufferEx(&activeBuf, GX2R_RESOURCE_BIND_NONE);

        // Vincular búfer de atributos y emitir 1 sola llamada GPU
        GX2RSetAttributeBuffer(&activeBuf, 0, sizeof(Render2DVertexGX2), 0);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, static_cast<uint32_t>(m_vertexBatch.size()), 0, 1);
        GX2Flush();

        // Avanzar ranura de búfer circular para el siguiente fotograma
        m_currentBufferSlot = (m_currentBufferSlot + 1) % BUFFER_SLOTS;
    }
#endif
    m_vertexBatch.clear();
}

} // namespace WiiU
