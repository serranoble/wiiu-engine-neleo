#include "Graphics/RHI/RenderTargetGX2.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>

#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <gx2/clear.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU::Graphics {

bool RenderTargetGX2::Initialize(uint32_t width,
                                 uint32_t height,
                                 const char* debugLabel,
                                 RenderTargetFormat format,
                                 bool withDepth) {
    if (m_initialized && m_width == width && m_height == height &&
        m_format == format && m_withDepth == withDepth) {
        return true;
    }
    Shutdown();

    if (width < 32u || height < 32u || width > 1920u || height > 1080u) {
        return false;
    }

    m_width = width;
    m_height = height;
    m_format = format;
    m_withDepth = withDepth;

#if defined(__WIIU__) || defined(__wiiu__)
    const char* label = (debugLabel && debugLabel[0] != '\0') ? debugLabel : "RenderTargetGX2";
    const GX2SurfaceFormat gx2ColorFmt = (format == RenderTargetFormat::RGBA16_Float)
        ? GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16
        : GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;

    // 1. Color Buffer (RGBA8 o HDR RGBA16F) con uso dual COLOR_BUFFER | TEXTURE
    std::memset(&m_colorBuffer, 0, sizeof(m_colorBuffer));
    m_colorBuffer.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    m_colorBuffer.surface.width     = m_width;
    m_colorBuffer.surface.height    = m_height;
    m_colorBuffer.surface.depth     = 1u;
    m_colorBuffer.surface.mipLevels = 1u;
    m_colorBuffer.surface.format    = gx2ColorFmt;
    m_colorBuffer.surface.aa        = GX2_AA_MODE1X;
    m_colorBuffer.surface.use       = static_cast<GX2SurfaceUse>(
        GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);
    m_colorBuffer.surface.tileMode  = GX2_TILE_MODE_DEFAULT;
    m_colorBuffer.viewNumSlices     = 1u;
    GX2CalcSurfaceSizeAndAlignment(&m_colorBuffer.surface);

    const auto colorFlags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_COLOR_BUFFER | GX2R_RESOURCE_BIND_TEXTURE |
        GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
    if (!GX2RCreateSurface(&m_colorBuffer.surface, colorFlags)) {
        Shutdown();
        return false;
    }
    GX2InitColorBufferRegs(&m_colorBuffer);
    m_colorCreated = true;

    // 2. Depth Buffer FLOAT_R32 opcional (orden estricto Cafe OS: Regs -> HiZEnable)
    if (m_withDepth) {
        std::memset(&m_depthBuffer, 0, sizeof(m_depthBuffer));
        m_depthBuffer.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
        m_depthBuffer.surface.width     = m_width;
        m_depthBuffer.surface.height    = m_height;
        m_depthBuffer.surface.depth     = 1u;
        m_depthBuffer.surface.mipLevels = 1u;
        m_depthBuffer.surface.format    = GX2_SURFACE_FORMAT_FLOAT_R32;
        m_depthBuffer.surface.aa        = GX2_AA_MODE1X;
        m_depthBuffer.surface.use       = GX2_SURFACE_USE_DEPTH_BUFFER;
        m_depthBuffer.surface.tileMode  = GX2_TILE_MODE_DEFAULT;
        m_depthBuffer.viewNumSlices     = 1u;
        m_depthBuffer.depthClear        = 1.0f;
        GX2CalcSurfaceSizeAndAlignment(&m_depthBuffer.surface);

        const auto depthFlags = static_cast<GX2RResourceFlags>(
            GX2R_RESOURCE_BIND_DEPTH_BUFFER |
            GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
        if (!GX2RCreateSurface(&m_depthBuffer.surface, depthFlags)) {
            Shutdown();
            return false;
        }
        GX2InitDepthBufferRegs(&m_depthBuffer);
        GX2InitDepthBufferHiZEnable(&m_depthBuffer, FALSE);
        m_depthCreated = true;
    }

    // 3. Vista GX2Texture aliased sobre el Color Buffer para lectura directa en TMU
    std::memset(&m_colorTexture, 0, sizeof(m_colorTexture));
    m_colorTexture.surface        = m_colorBuffer.surface;
    m_colorTexture.viewFirstMip   = 0u;
    m_colorTexture.viewNumMips    = 1u;
    m_colorTexture.viewFirstSlice = 0u;
    m_colorTexture.viewNumSlices  = 1u;
    m_colorTexture.compMap        = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
    GX2InitTextureRegs(&m_colorTexture);

    // 4. ContextState dedicado (aislado de 1280x720 en Cemu 2.6 y Latte HW)
    m_contextState = static_cast<GX2ContextState*>(MemoryManager::Instance().AllocAligned(
        GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState), MemoryTag::ContextState, label));
    if (!m_contextState) {
        Shutdown();
        return false;
    }
    std::memset(m_contextState, 0, sizeof(GX2ContextState));
    GX2SetupContextStateEx(m_contextState, TRUE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, m_contextState, sizeof(GX2ContextState));
#else
    (void)debugLabel;
#endif

    m_initialized = true;
    return true;
}

void RenderTargetGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    if ((m_colorCreated || m_depthCreated || m_contextState != nullptr) &&
        ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }

    if (m_contextState != nullptr) {
        void* rawCtx = m_contextState;
        MemoryManager::Instance().FreeAligned(rawCtx);
        m_contextState = nullptr;
    }
    if (m_depthCreated) {
        GX2RDestroySurfaceEx(&m_depthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_depthBuffer, 0, sizeof(m_depthBuffer));
        m_depthCreated = false;
    }
    if (m_colorCreated) {
        GX2RDestroySurfaceEx(&m_colorBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_colorBuffer, 0, sizeof(m_colorBuffer));
        std::memset(&m_colorTexture, 0, sizeof(m_colorTexture));
        m_colorCreated = false;
    }
#endif
    m_initialized = false;
    m_width = 0u;
    m_height = 0u;
    m_format = RenderTargetFormat::RGBA8_UNorm;
    m_withDepth = true;
}

#if defined(__WIIU__) || defined(__wiiu__)
bool RenderTargetGX2::BeginPass(float clearR,
                                float clearG,
                                float clearB,
                                float clearA,
                                float clearDepth) {
    if (!m_initialized || !m_contextState) return false;

    GX2SetContextState(m_contextState);
    GX2ClearColor(&m_colorBuffer, clearR, clearG, clearB, clearA);
    if (m_withDepth && m_depthCreated) {
        GX2ClearDepthStencilEx(&m_depthBuffer, clearDepth, 0u, GX2_CLEAR_FLAGS_DEPTH);
    }
    GX2SetContextState(m_contextState);

    GX2SetColorBuffer(&m_colorBuffer, GX2_RENDER_TARGET_0);
    if (m_withDepth && m_depthCreated) {
        GX2SetDepthBuffer(&m_depthBuffer);
    }
    GX2SetViewport(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f);
    GX2SetScissor(0u, 0u, m_width, m_height);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    RHIPipelineCache::Instance().Invalidate();
    return true;
}

void RenderTargetGX2::FlushForSampling() {
    if (!m_initialized) return;
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,
                  m_colorBuffer.surface.image, m_colorBuffer.surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,
                  m_colorTexture.surface.image, m_colorTexture.surface.imageSize);
}

void RenderTargetGX2::EndPass(GX2ContextState* restoreContext,
                              GX2ColorBuffer* restoreColor,
                              GX2DepthBuffer* restoreDepth,
                              uint32_t restoreWidth,
                              uint32_t restoreHeight) {
    if (!m_initialized) return;

    FlushForSampling();

    if (restoreContext) {
        GX2SetContextState(restoreContext);
    }
    if (restoreColor) {
        GX2SetColorBuffer(restoreColor, GX2_RENDER_TARGET_0);
    }
    if (restoreDepth) {
        GX2SetDepthBuffer(restoreDepth);
    }
    GX2SetViewport(0.0f, 0.0f, static_cast<float>(restoreWidth), static_cast<float>(restoreHeight), 0.0f, 1.0f);
    GX2SetScissor(0u, 0u, restoreWidth, restoreHeight);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    RHIPipelineCache::Instance().Invalidate();
}
#endif

} // namespace WiiU::Graphics
