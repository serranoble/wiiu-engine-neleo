#include "Graphics/Render2DGX2.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/UI2DShaderBinary.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cmath>
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <gx2/event.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <proc_ui/procui.h>
#endif

namespace Neleo {

namespace {
constexpr uint32_t kFontAtlasW = 128u;
constexpr uint32_t kFontAtlasH = 64u;
constexpr float kInvAtlasW = 1.0f / static_cast<float>(kFontAtlasW);
constexpr float kInvAtlasH = 1.0f / static_cast<float>(kFontAtlasH);
// Centro de la celda 0 (8x8 blanco sólido) para rectángulos, cajas, líneas y triángulos
constexpr float kSolidU = 4.0f * kInvAtlasW;
constexpr float kSolidV = 4.0f * kInvAtlasH;
} // namespace

bool Render2DGX2::Init() {
    if (m_initialized) {
        return true;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    m_vertexBatch.reserve(MAX_VERTICES);
    // 1. Cargar Shader dedicado 2D ultra-ligero (shaders/ui2d_gx2.gsh — ~4 GPRs, 0 iluminación 3D)
    if (!m_shaderLoaded) {
        std::vector<uint8_t> shaderData;
        bool loaded = AssetManager::LoadBinaryFile("shaders/ui2d_gx2.gsh", shaderData);
        if (!loaded || shaderData.empty()) {
            shaderData.assign(UI2D_GX2_GSH_DATA, UI2D_GX2_GSH_DATA + UI2D_GX2_GSH_SIZE);
            loaded = true;
        }

        if (loaded && !shaderData.empty()) {
            const uint32_t vsHeaderSize = GFDGetVertexShaderHeaderSize(0, shaderData.data());
            const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
            const uint32_t psHeaderSize = GFDGetPixelShaderHeaderSize(0, shaderData.data());
            const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());

            if (vsHeaderSize > 0u && vsProgSize > 0u && psHeaderSize > 0u && psProgSize > 0u) {
                m_directVs = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHeaderSize, MemoryTag::UI, "UI2D_VS_Hdr"));
                m_directVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::UI, "UI2D_VS_Prog");
                m_directPs = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHeaderSize, MemoryTag::UI, "UI2D_PS_Hdr"));
                m_directPsProgram = MemoryManager::Instance().AllocAligned(256, psProgSize, MemoryTag::UI, "UI2D_PS_Prog");

                if (m_directVs && m_directVsProgram && m_directPs && m_directPsProgram) {
                    const BOOL vsOk = GFDGetVertexShader(m_directVs, m_directVsProgram, 0, shaderData.data());
                    const BOOL psOk = GFDGetPixelShader(m_directPs, m_directPsProgram, 0, shaderData.data());
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProgSize);
                    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProgSize);

                    if (vsOk && psOk) {
                        GX2AttribStream attrs[3]{};
                        attrs[0].location = 0u;
                        attrs[0].buffer = 0u;
                        attrs[0].offset = offsetof(Render2DVertexGX2, pos);
                        attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                        attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[0].aluDivisor = 0u;
                        attrs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                        attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[1].location = 1u;
                        attrs[1].buffer = 0u;
                        attrs[1].offset = offsetof(Render2DVertexGX2, uv);
                        attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
                        attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[1].aluDivisor = 0u;
                        attrs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
                        attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        attrs[2].location = 2u;
                        attrs[2].buffer = 0u;
                        attrs[2].offset = offsetof(Render2DVertexGX2, color);
                        attrs[2].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
                        attrs[2].type = GX2_ATTRIB_INDEX_PER_VERTEX;
                        attrs[2].aluDivisor = 0u;
                        attrs[2].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
                        attrs[2].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

                        const uint32_t fsSize = GX2CalcFetchShaderSizeEx(
                            3u, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
                        m_directFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::UI, "UI2D_FS_Prog");
                        if (m_directFsProgram) {
                            GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 3u, attrs,
                                                 GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                 GX2_TESSELLATION_MODE_DISCRETE);
                            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);

                            m_fontSamplerSlot = 0;
                            if (m_directPs->samplerVarCount > 0u && m_directPs->samplerVars != nullptr) {
                                for (uint32_t s = 0u; s < m_directPs->samplerVarCount; ++s) {
                                    const auto& svar = m_directPs->samplerVars[s];
                                    if (svar.name && std::strcmp(svar.name, "uFontAtlas") == 0) {
                                        m_fontSamplerSlot = static_cast<int32_t>(svar.location);
                                        break;
                                    }
                                }
                            }

                            m_useDirectGFD = true;
                            m_shaderLoaded = true;
                        }
                    }
                }
            }
        }
    }

    // 2. Generar Atlas 2D 128x64 RGBA8 de Fuente Bitmap 5x7 (16x8 celdas de 8x8 + celda 0 blanca sólida)
    if (m_shaderLoaded && !m_fontAtlasCreated) {
        std::memset(&m_fontAtlas, 0, sizeof(GX2Texture));
        m_fontAtlas.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        m_fontAtlas.surface.width = kFontAtlasW;
        m_fontAtlas.surface.height = kFontAtlasH;
        m_fontAtlas.surface.depth = 1u;
        m_fontAtlas.surface.mipLevels = 1u;
        m_fontAtlas.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        m_fontAtlas.surface.aa = GX2_AA_MODE1X;
        m_fontAtlas.surface.use = GX2_SURFACE_USE_TEXTURE;
        m_fontAtlas.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        GX2CalcSurfaceSizeAndAlignment(&m_fontAtlas.surface);

        const uint32_t align = (m_fontAtlas.surface.alignment > 256u) ? m_fontAtlas.surface.alignment : 256u;
        m_fontAtlas.surface.image = MemoryManager::Instance().AllocAligned(
            align, m_fontAtlas.surface.imageSize, MemoryTag::UI, "UI2D_FontAtlas");
        if (m_fontAtlas.surface.image) {
            auto* dst = static_cast<uint8_t*>(m_fontAtlas.surface.image);
            const uint32_t pitchBytes = m_fontAtlas.surface.pitch * 4u;
            std::memset(dst, 0, m_fontAtlas.surface.imageSize);

            // Inicializar todo a RGB blanco con alpha 0 (evita sangrado oscuro en bordes)
            for (uint32_t y = 0u; y < kFontAtlasH; ++y) {
                uint8_t* rowPtr = dst + static_cast<size_t>(y) * pitchBytes;
                for (uint32_t x = 0u; x < kFontAtlasW; ++x) {
                    rowPtr[x * 4u + 0u] = 255u;
                    rowPtr[x * 4u + 1u] = 255u;
                    rowPtr[x * 4u + 2u] = 255u;
                    rowPtr[x * 4u + 3u] = 0u;
                }
            }

            // Celda 0 (0..7, 0..7): blanco sólido opaco para primitivas sin textura
            for (uint32_t y = 0u; y < 8u; ++y) {
                uint8_t* rowPtr = dst + static_cast<size_t>(y) * pitchBytes;
                for (uint32_t x = 0u; x < 8u; ++x) {
                    rowPtr[x * 4u + 3u] = 255u;
                }
            }

            // Celdas 1..127: glifos bitmap 5x7 centrados en (cellX + 1..5, cellY + 0..6)
            uint8_t cols[5]{};
            for (uint32_t ch = 1u; ch < 128u; ++ch) {
                Render2D::GetFont5x7(static_cast<char>(ch), cols);
                const uint32_t cellX = (ch & 15u) * 8u;
                const uint32_t cellY = (ch >> 4u) * 8u;
                for (uint32_t row = 0u; row < 7u; ++row) {
                    uint8_t* rowPtr = dst + static_cast<size_t>(cellY + row) * pitchBytes;
                    for (uint32_t col = 0u; col < 5u; ++col) {
                        if (cols[col] & (1u << row)) {
                            const uint32_t px = cellX + 1u + col;
                            rowPtr[px * 4u + 3u] = 255u;
                        }
                    }
                }
            }

            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, m_fontAtlas.surface.image, m_fontAtlas.surface.imageSize);
            m_fontAtlas.viewFirstMip = 0u;
            m_fontAtlas.viewNumMips = 1u;
            m_fontAtlas.viewFirstSlice = 0u;
            m_fontAtlas.viewNumSlices = 1u;
            m_fontAtlas.compMap = 0x00010203u;
            GX2InitTextureRegs(&m_fontAtlas);

            // Sampler POINT + single-mip blindado (MIP_FILTER_NONE + MIP_PERF_DISABLE + LOD 0..0)
            GX2InitSampler(&m_fontSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
            GX2InitSamplerXYFilter(&m_fontSampler, GX2_TEX_XY_FILTER_MODE_POINT, GX2_TEX_XY_FILTER_MODE_POINT, GX2_TEX_ANISO_RATIO_NONE);
            GX2InitSamplerZMFilter(&m_fontSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
            GX2InitSamplerFilterAdjust(&m_fontSampler, FALSE, GX2_TEX_MIP_PERF_MODE_DISABLE, GX2_TEX_Z_PERF_MODE_DISABLED);
            GX2InitSamplerLOD(&m_fontSampler, 0.0f, 0.0f, 0.0f);

            m_fontAtlasCreated = true;
        }
    }

    // 3. Crear Búfer Dinámico de Vértices en VRAM con Triple-Buffering (GX2R)
    if (m_shaderLoaded && m_fontAtlasCreated && !m_bufferCreated) {
        bool allBuffersOk = true;
        for (size_t i = 0; i < BUFFER_SLOTS; ++i) {
            std::memset(&m_vertexBuffers[i], 0, sizeof(GX2RBuffer));
            m_vertexBuffers[i].flags = static_cast<GX2RResourceFlags>(
                GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
            m_vertexBuffers[i].elemSize = sizeof(Render2DVertexGX2);
            m_vertexBuffers[i].elemCount = static_cast<uint32_t>(MAX_VERTICES);

            if (GX2RCreateBuffer(&m_vertexBuffers[i])) {
                m_bufferSlotCreated[i] = true;
            } else {
                m_bufferSlotCreated[i] = false;
                allBuffersOk = false;
            }
        }
        m_bufferCreated = allBuffersOk;
        m_currentBufferSlot = 0;
        m_bufferVertexOffset = 0;
    }

    m_initialized = (m_shaderLoaded && m_fontAtlasCreated && m_bufferCreated);
    if (!m_initialized) {
        Shutdown();
    }
    return m_initialized;
#else
    m_initialized = true;
    return true;
#endif
}

void Render2DGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    bool hasGpuResources = m_shaderLoaded || m_fontAtlasCreated || m_directVs || m_directPs || m_directFsProgram;
    for (size_t i = 0; i < BUFFER_SLOTS; ++i) {
        if (m_bufferSlotCreated[i]) hasGpuResources = true;
    }
    if (hasGpuResources && ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }

    for (size_t i = 0; i < BUFFER_SLOTS; ++i) {
        if (m_bufferSlotCreated[i]) {
            GX2RDestroyBufferEx(&m_vertexBuffers[i], GX2R_RESOURCE_BIND_NONE);
            std::memset(&m_vertexBuffers[i], 0, sizeof(GX2RBuffer));
            m_bufferSlotCreated[i] = false;
        }
    }
    m_bufferCreated = false;
    m_currentBufferSlot = 0;
    m_bufferVertexOffset = 0;

    if (m_fontAtlas.surface.image) {
        MemoryManager::Instance().FreeAligned(m_fontAtlas.surface.image);
        std::memset(&m_fontAtlas, 0, sizeof(GX2Texture));
    }
    m_fontAtlasCreated = false;
    m_fontSamplerSlot = 0;

    if (m_directFsProgram) { MemoryManager::Instance().FreeAligned(m_directFsProgram); m_directFsProgram = nullptr; }
    if (m_directPsProgram) { MemoryManager::Instance().FreeAligned(m_directPsProgram); m_directPsProgram = nullptr; }
    if (m_directPs)        { void* raw = static_cast<void*>(m_directPs); MemoryManager::Instance().Free(raw); m_directPs = nullptr; }
    if (m_directVsProgram) { MemoryManager::Instance().FreeAligned(m_directVsProgram); m_directVsProgram = nullptr; }
    if (m_directVs)        { void* raw = static_cast<void*>(m_directVs); MemoryManager::Instance().Free(raw); m_directVs = nullptr; }
    m_useDirectGFD = false;
    m_shaderLoaded = false;
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
    if (!m_shaderLoaded || !m_fontAtlasCreated || !m_bufferCreated || !m_useDirectGFD || !m_directVs || !m_directPs) return;

    // 1. Asegurar modo de registros uniformes y vincular shaders ui2d_gx2
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetFetchShader(&m_directFetchShader);
    GX2SetVertexShader(m_directVs);
    GX2SetPixelShader(m_directPs);

    // 2. Configurar Uniform de Proyección Ortográfica (1280x720) — solo 16 floats (64 B)
    const Matrix4x4 ortho = Matrix4x4::Orthographic(0.0f, 1280.0f, 720.0f, 0.0f, -1.0f, 1.0f);
    alignas(256) float vsUniforms[16];
    std::memcpy(vsUniforms, ortho.m, 16 * sizeof(float));
    GX2SetVertexUniformReg(0, 16, vsUniforms);

    // 3. Vincular Atlas de Fuente 5x7 + celda blanca sólida (1 única textura para todo el pase 2D)
    const uint32_t texSlot = (m_fontSamplerSlot >= 0) ? static_cast<uint32_t>(m_fontSamplerSlot) : 0u;
    GX2SetPixelTexture(&m_fontAtlas, texSlot);
    GX2SetPixelSampler(&m_fontSampler, texSlot);

    // 4. Estados de Rasterización para UI 2D Superpuesta mediante RHI Pipeline Cache (RT0 blend = 0x01)
    RHIPipelineCache::Instance().Invalidate();
    RHIPipelineState state(false, false, RHI_COMPARE_ALWAYS, BlendMode::AlphaBlend, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);

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

    const float x0 = std::floor(x + 0.5f);
    const float y0 = std::floor(y + 0.5f);
    const float x1 = std::floor(x + w + 0.5f);
    const float y1 = std::floor(y + h + 0.5f);

    const Render2DVertexGX2 v0 = { {x0, y0}, {kSolidU, kSolidV}, {r, g, b, a} };
    const Render2DVertexGX2 v1 = { {x1, y0}, {kSolidU, kSolidV}, {r, g, b, a} };
    const Render2DVertexGX2 v2 = { {x0, y1}, {kSolidU, kSolidV}, {r, g, b, a} };
    const Render2DVertexGX2 v3 = { {x1, y1}, {kSolidU, kSolidV}, {r, g, b, a} };

    m_vertexBatch.push_back(v0);
    m_vertexBatch.push_back(v2);
    m_vertexBatch.push_back(v1);

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

    // [OPT-313 / OPT-453] Escala entera + 1 único quad (2 triángulos) por glifo sobre el atlas 128x64
    scale = std::floor(scale + 0.5f);
    if (scale < 1.0f) scale = 1.0f;

    const float r = static_cast<float>(color.r) * (1.0f / 255.0f);
    const float g = static_cast<float>(color.g) * (1.0f / 255.0f);
    const float b = static_cast<float>(color.b) * (1.0f / 255.0f);
    const float a = static_cast<float>(color.a) * (1.0f / 255.0f);

    float curX = std::floor(x + 0.5f);
    const float startY = std::floor(y + 0.5f);
    const float glyphW = 5.0f * scale;
    const float glyphH = 7.0f * scale;
    const float advanceX = 6.0f * scale;

    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc > ' ' && uc < 128u) {
            if (m_vertexBatch.size() + 6 > MAX_VERTICES) break;

            const uint32_t cellX = (static_cast<uint32_t>(uc) & 15u) * 8u;
            const uint32_t cellY = (static_cast<uint32_t>(uc) >> 4u) * 8u;
            const float u0 = static_cast<float>(cellX + 1u) * kInvAtlasW;
            const float v0 = static_cast<float>(cellY) * kInvAtlasH;
            const float u1 = static_cast<float>(cellX + 6u) * kInvAtlasW;
            const float v1 = static_cast<float>(cellY + 7u) * kInvAtlasH;

            const float x0 = curX;
            const float y0 = startY;
            const float x1 = curX + glyphW;
            const float y1 = startY + glyphH;

            const Render2DVertexGX2 q0 = { {x0, y0}, {u0, v0}, {r, g, b, a} };
            const Render2DVertexGX2 q1 = { {x1, y0}, {u1, v0}, {r, g, b, a} };
            const Render2DVertexGX2 q2 = { {x0, y1}, {u0, v1}, {r, g, b, a} };
            const Render2DVertexGX2 q3 = { {x1, y1}, {u1, v1}, {r, g, b, a} };

            m_vertexBatch.push_back(q0);
            m_vertexBatch.push_back(q2);
            m_vertexBatch.push_back(q1);

            m_vertexBatch.push_back(q1);
            m_vertexBatch.push_back(q2);
            m_vertexBatch.push_back(q3);
        }
        curX += advanceX;
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
    const Render2DVertexGX2 v0 = { {x0, y0}, {kSolidU, kSolidV},
        {c0.r * inv255, c0.g * inv255, c0.b * inv255, c0.a * inv255} };
    const Render2DVertexGX2 v1 = { {x1, y1}, {kSolidU, kSolidV},
        {c1.r * inv255, c1.g * inv255, c1.b * inv255, c1.a * inv255} };
    const Render2DVertexGX2 v2 = { {x2, y2}, {kSolidU, kSolidV},
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
        RHIPipelineCache::Instance().Invalidate();
        return;
    }

    const uint32_t vertCount = static_cast<uint32_t>(m_vertexBatch.size());
    if (vertCount > static_cast<uint32_t>(MAX_VERTICES)) {
        m_vertexBatch.clear();
        RHIPipelineCache::Instance().Invalidate();
        return;
    }

    if (m_bufferVertexOffset + vertCount > static_cast<uint32_t>(MAX_VERTICES)) {
        if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
            Window::HasForeground() && !Window::IsExitingProcUI()) {
            GX2DrawDone();
        }
        m_currentBufferSlot = (m_currentBufferSlot + 1u) % BUFFER_SLOTS;
        m_bufferVertexOffset = 0u;
    }

    // Subir vértices acumulados al sub-rango libre del búfer activo en VRAM (Triple-Buffering)
    GX2RBuffer& activeBuf = m_vertexBuffers[m_currentBufferSlot];
    void* ptr = GX2RLockBufferEx(&activeBuf, GX2R_RESOURCE_BIND_NONE);
    if (ptr) {
        uint8_t* dst = static_cast<uint8_t*>(ptr) +
                       static_cast<size_t>(m_bufferVertexOffset) * sizeof(Render2DVertexGX2);
        const uint32_t byteSize = static_cast<uint32_t>(vertCount * sizeof(Render2DVertexGX2));
        std::memcpy(dst, m_vertexBatch.data(), byteSize);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, dst, byteSize);
        GX2RUnlockBufferEx(&activeBuf, GX2R_RESOURCE_DISABLE_CPU_INVALIDATE);

        // Vincular búfer de atributos y emitir 1 sola llamada GPU desde el offset del pase
        // (sin GX2Flush redundante: el flush canónico ocurre al presentar el frame en Window::Present).
        GX2RSetAttributeBuffer(&activeBuf, 0, sizeof(Render2DVertexGX2), 0);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, vertCount, m_bufferVertexOffset, 1);

        m_bufferVertexOffset += vertCount;
    }
    RHIPipelineCache::Instance().Invalidate();
#endif
    m_vertexBatch.clear();
}

void Render2DGX2::NextFrame() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_bufferVertexOffset > 0u) {
        m_currentBufferSlot = (m_currentBufferSlot + 1u) % BUFFER_SLOTS;
        m_bufferVertexOffset = 0u;
    }
#endif
}

} // namespace Neleo
