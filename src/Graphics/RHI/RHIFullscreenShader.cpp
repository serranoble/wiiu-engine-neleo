#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/FullscreenBlitShaderBinary.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2r/draw.h>
#include <gfd.h>
#include <proc_ui/procui.h>
#endif

namespace Neleo {

namespace {

struct QuadVertex {
    float x, y;
    float u, v;
};

} // namespace

#if defined(__WIIU__) || defined(__wiiu__)
RHIFullscreenShader::OutputTargetBinding RHIFullscreenShader::s_frameTarget{};

void RHIFullscreenShader::SetActiveFrameTargets(GX2ColorBuffer* colorBuffer, GX2DepthBuffer* depthBuffer) noexcept {
    if (colorBuffer) {
        s_frameTarget.colorBuffer = colorBuffer;
        s_frameTarget.depthBuffer = depthBuffer;
        s_frameTarget.width = (colorBuffer->surface.width > 0) ? colorBuffer->surface.width : 1280u;
        s_frameTarget.height = (colorBuffer->surface.height > 0) ? colorBuffer->surface.height : 720u;
    } else {
        s_frameTarget = OutputTargetBinding{};
    }
}

void RHIFullscreenShader::ClearActiveFrameTargets() noexcept {
    s_frameTarget = OutputTargetBinding{};
}

void RHIFullscreenShader::SetOutputTarget(const OutputTargetBinding& target) noexcept {
    if (target.colorBuffer) {
        m_outputTarget.colorBuffer = target.colorBuffer;
        m_outputTarget.depthBuffer = target.depthBuffer;
        m_outputTarget.width = (target.width > 0) ? target.width
                             : ((target.colorBuffer->surface.width > 0) ? target.colorBuffer->surface.width : 1280u);
        m_outputTarget.height = (target.height > 0) ? target.height
                              : ((target.colorBuffer->surface.height > 0) ? target.colorBuffer->surface.height : 720u);
    } else {
        ClearOutputTarget();
    }
}

void RHIFullscreenShader::ClearOutputTarget() noexcept {
    m_outputTarget = OutputTargetBinding{};
}

RHIFullscreenShader::OutputTargetBinding RHIFullscreenShader::ResolveOutputTarget(
    const OutputTargetBinding* explicitTarget) const noexcept {
    const OutputTargetBinding* src = nullptr;
    if (explicitTarget && explicitTarget->colorBuffer) {
        src = explicitTarget;
    } else if (m_outputTarget.colorBuffer) {
        src = &m_outputTarget;
    } else {
        src = &s_frameTarget;
    }

    OutputTargetBinding resolved = *src;
    if (resolved.colorBuffer) {
        if (resolved.width == 0) {
            resolved.width = (resolved.colorBuffer->surface.width > 0) ? resolved.colorBuffer->surface.width : 1280u;
        }
        if (resolved.height == 0) {
            resolved.height = (resolved.colorBuffer->surface.height > 0) ? resolved.colorBuffer->surface.height : 720u;
        }
    } else {
        resolved.width = 1280u;
        resolved.height = 720u;
    }
    return resolved;
}
#endif

bool RHIFullscreenShader::PreloadBinary(const char* shaderAssetPath) {
    m_stagedBinary.clear();
    if (!shaderAssetPath) {
        return false;
    }
    if (!AssetManager::LoadBinaryFile(shaderAssetPath, m_stagedBinary) || m_stagedBinary.empty()) {
        m_stagedBinary.clear();
        return false;
    }
    return true;
}

bool RHIFullscreenShader::Initialize(const char* shaderAssetPath,
                                     const uint8_t* fallbackData,
                                     size_t fallbackSize) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_quadCreated) {
        m_stagedBinary.clear();
        m_stagedBinary.shrink_to_fit();
        return true;
    }

    if (m_stagedBinary.empty() && shaderAssetPath) {
        if (!AssetManager::LoadBinaryFile(shaderAssetPath, m_stagedBinary)) {
            m_stagedBinary.clear();
        }
    }

    bool loaded = !m_stagedBinary.empty() && TryLoadDirectGFD(m_stagedBinary.data());
    if (!loaded && fallbackData && fallbackSize > 0) {
        loaded = TryLoadDirectGFD(fallbackData);
    }

    m_stagedBinary.clear();
    m_stagedBinary.shrink_to_fit();

    if (!loaded) {
        Shutdown();
        return false;
    }

    if (!CreateQuadBuffer()) {
        Shutdown();
        return false;
    }

    return true;
#else
    (void)shaderAssetPath;
    (void)fallbackData;
    (void)fallbackSize;
    m_stagedBinary.clear();
    m_stagedBinary.shrink_to_fit();
    m_shaderLoaded = true;
    return true;
#endif
}

void RHIFullscreenShader::Shutdown() {
    m_stagedBinary.clear();
    m_stagedBinary.shrink_to_fit();
#if defined(__WIIU__) || defined(__wiiu__)
    const bool hasGpuResources = m_shaderLoaded || m_quadCreated || m_offscreenReady;
    if (hasGpuResources && ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }

    ShutdownOffscreenScaler();

    if (m_quadCreated) {
        GX2RDestroyBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
        m_quadCreated = false;
    }

    m_shaderLoaded = false;
    FreeDirectGFD();
#else
    m_shaderLoaded = false;
#endif
}

bool RHIFullscreenShader::IsReady() const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return m_shaderLoaded && m_quadCreated;
#else
    return m_shaderLoaded;
#endif
}

bool RHIFullscreenShader::EnableOffscreenScaler(uint32_t maxWidth, uint32_t maxHeight) {
#if defined(__WIIU__) || defined(__wiiu__)
    return InitOffscreenScaler(maxWidth, maxHeight);
#else
    (void)maxWidth;
    (void)maxHeight;
    return true;
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
int32_t RHIFullscreenShader::FindPixelUniformOffset(const GX2PixelShader* ps, const char* uniformName) noexcept {
    if (!ps || !ps->uniformVars || !uniformName) {
        return -1;
    }
    for (uint32_t i = 0; i < ps->uniformVarCount; ++i) {
        if (ps->uniformVars[i].name && std::strcmp(ps->uniformVars[i].name, uniformName) == 0) {
            return static_cast<int32_t>(ps->uniformVars[i].offset);
        }
    }
    return -1;
}
#endif

int32_t RHIFullscreenShader::GetPixelUniformOffset(const char* uniformName) const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return FindPixelUniformOffset(GetActivePixelShader(), uniformName);
#else
    (void)uniformName;
    return -1;
#endif
}

void RHIFullscreenShader::BindPipeline(uint32_t viewportW, uint32_t viewportH) noexcept {
    BindPipeline(viewportW, viewportH, nullptr);
}

#if defined(__WIIU__) || defined(__wiiu__)
void RHIFullscreenShader::BindPipeline(uint32_t viewportW,
                                       uint32_t viewportH,
                                       const OutputTargetBinding* outputTarget) noexcept {
    if (!IsReady()) return;

    m_boundTarget = ResolveOutputTarget(outputTarget);
    const uint32_t safeOutW = std::max<uint32_t>(1u, m_boundTarget.width);
    const uint32_t safeOutH = std::max<uint32_t>(1u, m_boundTarget.height);
    const uint32_t reqW = std::clamp(viewportW, std::min<uint32_t>(64u, safeOutW), safeOutW);
    const uint32_t reqH = std::clamp(viewportH, std::min<uint32_t>(36u, safeOutH), safeOutH);

    const bool wantsScale = (reqW < safeOutW || reqH < safeOutH) && (m_boundTarget.colorBuffer != nullptr);
    if (wantsScale && m_offscreenReady && m_offscreenWidth >= reqW && m_offscreenHeight >= reqH) {
        m_activeViewportW = reqW;
        m_activeViewportH = reqH;
        m_renderingOffscreen = true;
        GX2SetColorBuffer(&m_offscreenColorBuffer, GX2_RENDER_TARGET_0);
    } else {
        m_renderingOffscreen = false;
        m_activeViewportW = safeOutW;
        m_activeViewportH = safeOutH;
        if (m_boundTarget.colorBuffer != nullptr) {
            GX2SetColorBuffer(m_boundTarget.colorBuffer, GX2_RENDER_TARGET_0);
        }
    }

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetFetchShader(&m_directFetchShader);
    GX2SetVertexShader(m_mainShader.vs);
    GX2SetPixelShader(m_mainShader.ps);

    RHIPipelineCache::Instance().Invalidate();
    RHIPipelineState state(false, false, RHI_COMPARE_ALWAYS, BlendMode::Opaque, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);

    GX2SetViewport(0.0f, 0.0f,
                   static_cast<float>(m_activeViewportW),
                   static_cast<float>(m_activeViewportH),
                   0.0f, 1.0f);
    GX2SetScissor(0, 0, m_activeViewportW, m_activeViewportH);
}
#else
void RHIFullscreenShader::SetActiveFrameTargets(void* colorBuffer, void* depthBuffer) noexcept {
    (void)colorBuffer;
    (void)depthBuffer;
}

void RHIFullscreenShader::ClearActiveFrameTargets() noexcept {}

void RHIFullscreenShader::SetOutputTarget(const OutputTargetBinding& target) noexcept {
    (void)target;
}

void RHIFullscreenShader::ClearOutputTarget() noexcept {}

void RHIFullscreenShader::BindPipeline(uint32_t viewportW,
                                       uint32_t viewportH,
                                       const OutputTargetBinding* outputTarget) noexcept {
    (void)viewportW;
    (void)viewportH;
    (void)outputTarget;
}
#endif

void RHIFullscreenShader::SetPixelUniform4fv(int32_t offset, uint32_t floatCount, const float* data) const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (offset >= 0 && floatCount > 0 && data) {
        GX2SetPixelUniformReg(static_cast<uint32_t>(offset), floatCount, data);
    }
#else
    (void)offset;
    (void)floatCount;
    (void)data;
#endif
}

void RHIFullscreenShader::DrawQuad() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!IsReady()) return;

    GX2RSetAttributeBuffer(&m_quadBuffer, 0, sizeof(QuadVertex), 0);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);

    if (m_renderingOffscreen && m_offscreenReady && m_boundTarget.colorBuffer != nullptr) {
        GX2Flush();
        GX2Invalidate(static_cast<GX2InvalidateMode>(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE),
                      m_offscreenTexture.surface.image, m_offscreenTexture.surface.imageSize);

        GX2SetColorBuffer(m_boundTarget.colorBuffer, GX2_RENDER_TARGET_0);
        if (m_boundTarget.depthBuffer != nullptr) {
            GX2SetDepthBuffer(m_boundTarget.depthBuffer);
        }
        GX2SetViewport(0.0f, 0.0f,
                       static_cast<float>(m_boundTarget.width),
                       static_cast<float>(m_boundTarget.height),
                       0.0f, 1.0f);
        GX2SetScissor(0, 0, m_boundTarget.width, m_boundTarget.height);

        GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
        GX2SetFetchShader(&m_blitFetchShader);
        GX2SetVertexShader(m_blitShader.vs);
        GX2SetPixelShader(m_blitShader.ps);
        GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);

        const float texW = static_cast<float>(std::max<uint32_t>(1u, m_offscreenWidth));
        const float texH = static_cast<float>(std::max<uint32_t>(1u, m_offscreenHeight));
        const float resW = static_cast<float>(m_activeViewportW);
        const float resH = static_cast<float>(m_activeViewportH);
        const float uvTransform[4] = {
            0.0f,
            resH / texH,
            resW / texW,
            -resH / texH
        };
        const float uvClamp[4] = {
            0.5f / texW,
            0.5f / texH,
            (resW - 0.5f) / texW,
            (resH - 0.5f) / texH
        };
        if (m_blitTransformOffset >= 0) {
            GX2SetPixelUniformReg(static_cast<uint32_t>(m_blitTransformOffset), 4, uvTransform);
        }
        if (m_blitClampOffset >= 0) {
            GX2SetPixelUniformReg(static_cast<uint32_t>(m_blitClampOffset), 4, uvClamp);
        }
        GX2SetPixelTexture(&m_offscreenTexture, m_blitSamplerLocation);
        GX2SetPixelSampler(&m_blitSampler, m_blitSamplerLocation);

        GX2RSetAttributeBuffer(&m_quadBuffer, 0, sizeof(QuadVertex), 0);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);

        RHIPipelineCache::Instance().Invalidate();
    }

    m_renderingOffscreen = false;
    m_boundTarget = OutputTargetBinding{};
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
bool RHIFullscreenShader::LoadShaderPair(const uint8_t* data, DirectShaderPair& out, const char* tag) {
    if (!data || data[0] != 'G' || data[1] != 'f' || data[2] != 'x' || data[3] != '2') {
        return false;
    }

    const uint32_t vsHdr = GFDGetVertexShaderHeaderSize(0, data);
    const uint32_t vsProg = GFDGetVertexShaderProgramSize(0, data);
    const uint32_t psHdr = GFDGetPixelShaderHeaderSize(0, data);
    const uint32_t psProg = GFDGetPixelShaderProgramSize(0, data);
    if (vsHdr == 0 || vsProg == 0 || psHdr == 0 || psProg == 0) {
        return false;
    }

    auto& mm = MemoryManager::Instance();
    out.vs = static_cast<GX2VertexShader*>(mm.Alloc(vsHdr, MemoryTag::ShaderProgram, tag));
    out.vsProgram = mm.AllocAligned(256, vsProg, MemoryTag::ShaderProgram, tag);
    out.ps = static_cast<GX2PixelShader*>(mm.Alloc(psHdr, MemoryTag::ShaderProgram, tag));
    out.psProgram = mm.AllocAligned(256, psProg, MemoryTag::ShaderProgram, tag);

    if (!out.vs || !out.vsProgram || !out.ps || !out.psProgram ||
        !GFDGetVertexShader(out.vs, out.vsProgram, 0, data) ||
        !GFDGetPixelShader(out.ps, out.psProgram, 0, data)) {
        FreeShaderPair(out);
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, out.vsProgram, vsProg);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, out.psProgram, psProg);
    return true;
}

void RHIFullscreenShader::FreeShaderPair(DirectShaderPair& pair) noexcept {
    auto& mm = MemoryManager::Instance();
    if (pair.psProgram) { mm.FreeAligned(pair.psProgram); }
    if (pair.ps)        { void* p = pair.ps; mm.Free(p); pair.ps = nullptr; }
    if (pair.vsProgram) { mm.FreeAligned(pair.vsProgram); }
    if (pair.vs)        { void* p = pair.vs; mm.Free(p); pair.vs = nullptr; }
}

const GX2PixelShader* RHIFullscreenShader::GetActivePixelShader() const noexcept {
    if (!m_shaderLoaded) return nullptr;
    return m_mainShader.ps;
}

void RHIFullscreenShader::FreeDirectGFD() noexcept {
    if (m_directFsProgram) {
        MemoryManager::Instance().FreeAligned(m_directFsProgram);
    }
    FreeShaderPair(m_mainShader);
}

bool RHIFullscreenShader::InitQuadFetchShader(const GX2VertexShader* vs,
                                                GX2FetchShader& outFetch,
                                                void*& outProgram,
                                                const char* tag) {
    uint32_t posLoc = 0;
    uint32_t uvLoc = 1;
    if (vs && vs->attribVars) {
        for (uint32_t i = 0; i < vs->attribVarCount; ++i) {
            const char* name = vs->attribVars[i].name;
            if (!name) continue;
            if (std::strcmp(name, "aPos") == 0) {
                posLoc = vs->attribVars[i].location;
            } else if (std::strcmp(name, "aTexCoord") == 0 || std::strcmp(name, "aUV") == 0) {
                uvLoc = vs->attribVars[i].location;
            }
        }
    }

    GX2AttribStream attrs[2];
    std::memset(attrs, 0, sizeof(attrs));
    attrs[0].location = posLoc;
    attrs[0].buffer = 0;
    attrs[0].offset = 0;
    attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
    attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
    attrs[0].mask = 0x00010405;
    attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    attrs[1].location = uvLoc;
    attrs[1].buffer = 0;
    attrs[1].offset = 8;
    attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
    attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
    attrs[1].mask = 0x00010405;
    attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(2, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    outProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, tag);
    if (!outProgram) {
        return false;
    }

    GX2InitFetchShaderEx(&outFetch, static_cast<uint8_t*>(outProgram), 2, attrs,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, outProgram, fsSize);
    return true;
}

bool RHIFullscreenShader::TryLoadDirectGFD(const uint8_t* data) {
    if (!LoadShaderPair(data, m_mainShader, "RHIFS_MainShader")) {
        return false;
    }

    if (!InitQuadFetchShader(m_mainShader.vs, m_directFetchShader, m_directFsProgram, "RHIFS_FS_Prog")) {
        FreeDirectGFD();
        return false;
    }

    m_shaderLoaded = true;
    return true;
}

bool RHIFullscreenShader::InitOffscreenScaler(uint32_t maxWidth, uint32_t maxHeight) {
    const uint32_t reqW = std::clamp(maxWidth, 64u, 1920u);
    const uint32_t reqH = std::clamp(maxHeight, 36u, 1080u);
    if (m_offscreenReady) {
        if (m_offscreenWidth >= reqW && m_offscreenHeight >= reqH) {
            return true;
        }
        if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
            Window::HasForeground() && !Window::IsExitingProcUI()) {
            GX2DrawDone();
        }
        ShutdownOffscreenScaler();
    }

    if (!LoadShaderPair(FULLSCREEN_BLIT_GSH_DATA, m_blitShader, "RHIFS_BlitShader")) {
        return false;
    }

    if (!InitQuadFetchShader(m_blitShader.vs, m_blitFetchShader, m_blitFsProgram, "RHIFS_BlitFS_Prog")) {
        ShutdownOffscreenScaler();
        return false;
    }

    m_blitTransformOffset = FindPixelUniformOffset(m_blitShader.ps, "uBlitUVTransform");
    m_blitClampOffset     = FindPixelUniformOffset(m_blitShader.ps, "uBlitUVClamp");
    if (m_blitTransformOffset < 0 || m_blitClampOffset < 0) {
        ShutdownOffscreenScaler();
        return false;
    }

    m_blitSamplerLocation = 0;
    if (m_blitShader.ps && m_blitShader.ps->samplerVars) {
        for (uint32_t i = 0; i < m_blitShader.ps->samplerVarCount; ++i) {
            if (m_blitShader.ps->samplerVars[i].name &&
                std::strcmp(m_blitShader.ps->samplerVars[i].name, "uSourceTex") == 0) {
                m_blitSamplerLocation = m_blitShader.ps->samplerVars[i].location;
                break;
            }
        }
    }

    std::memset(&m_offscreenColorBuffer, 0, sizeof(m_offscreenColorBuffer));
    m_offscreenColorBuffer.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    m_offscreenColorBuffer.surface.width     = reqW;
    m_offscreenColorBuffer.surface.height    = reqH;
    m_offscreenColorBuffer.surface.depth     = 1;
    m_offscreenColorBuffer.surface.mipLevels = 1;
    m_offscreenColorBuffer.surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    m_offscreenColorBuffer.surface.aa        = GX2_AA_MODE1X;
    m_offscreenColorBuffer.surface.use       = static_cast<GX2SurfaceUse>(
        GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    m_offscreenColorBuffer.surface.tileMode  = GX2_TILE_MODE_DEFAULT;
    m_offscreenColorBuffer.viewMip           = 0;
    m_offscreenColorBuffer.viewFirstSlice    = 0;
    m_offscreenColorBuffer.viewNumSlices     = 1;
    GX2CalcSurfaceSizeAndAlignment(&m_offscreenColorBuffer.surface);

    const uint32_t align = std::max<uint32_t>(256u, m_offscreenColorBuffer.surface.alignment);
    m_offscreenImagePtr = MemoryManager::Instance().AllocAligned(
        align, m_offscreenColorBuffer.surface.imageSize, MemoryTag::Texture, "RHIFS_OffscreenRT");
    if (!m_offscreenImagePtr) {
        ShutdownOffscreenScaler();
        return false;
    }

    std::memset(m_offscreenImagePtr, 0, m_offscreenColorBuffer.surface.imageSize);
    m_offscreenColorBuffer.surface.image = m_offscreenImagePtr;
    GX2InitColorBufferRegs(&m_offscreenColorBuffer);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, m_offscreenImagePtr, m_offscreenColorBuffer.surface.imageSize);

    std::memset(&m_offscreenTexture, 0, sizeof(m_offscreenTexture));
    m_offscreenTexture.surface        = m_offscreenColorBuffer.surface;
    m_offscreenTexture.viewFirstMip   = 0;
    m_offscreenTexture.viewNumMips    = 1;
    m_offscreenTexture.viewFirstSlice = 0;
    m_offscreenTexture.viewNumSlices  = 1;
    m_offscreenTexture.compMap        = 0x00010203;
    GX2InitTextureRegs(&m_offscreenTexture);

    GX2InitSampler(&m_blitSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_blitSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_blitSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
    GX2InitSamplerLOD(&m_blitSampler, 0.0f, 0.0f, 0.0f);

    m_offscreenWidth = m_offscreenTexture.surface.width;
    m_offscreenHeight = m_offscreenTexture.surface.height;
    m_offscreenReady = true;
    return true;
}

void RHIFullscreenShader::ShutdownOffscreenScaler() noexcept {
    if (m_offscreenImagePtr) {
        MemoryManager::Instance().FreeAligned(m_offscreenImagePtr);
    }
    if (m_blitFsProgram) {
        MemoryManager::Instance().FreeAligned(m_blitFsProgram);
    }
    FreeShaderPair(m_blitShader);
    std::memset(&m_offscreenColorBuffer, 0, sizeof(m_offscreenColorBuffer));
    std::memset(&m_offscreenTexture, 0, sizeof(m_offscreenTexture));
    m_blitTransformOffset = -1;
    m_blitClampOffset = -1;
    m_blitSamplerLocation = 0;
    m_offscreenWidth = 0;
    m_offscreenHeight = 0;
    m_offscreenReady = false;
    m_renderingOffscreen = false;
    m_boundTarget = OutputTargetBinding{};
}

bool RHIFullscreenShader::CreateQuadBuffer() {
    static constexpr QuadVertex VERTS[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f },
        {  1.0f, -1.0f, 1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 0.0f },
        {  1.0f,  1.0f, 1.0f, 1.0f }
    };

    std::memset(&m_quadBuffer, 0, sizeof(m_quadBuffer));
    m_quadBuffer.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
    );
    m_quadBuffer.elemSize = sizeof(QuadVertex);
    m_quadBuffer.elemCount = 6;
    if (!GX2RCreateBuffer(&m_quadBuffer)) {
        return false;
    }

    if (void* dst = GX2RLockBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE)) {
        std::memcpy(dst, VERTS, sizeof(VERTS));
        GX2RUnlockBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
        m_quadCreated = true;
        return true;
    }

    GX2RDestroyBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
    return false;
}
#endif

} // namespace Neleo
