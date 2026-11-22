#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include "Graphics/ShadowDepthShaderBinary.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <gx2r/draw.h>
#include <gx2r/surface.h>
#include <gfd.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

ShadowMapRendererGX2& ShadowMapRendererGX2::Instance() noexcept {
    static ShadowMapRendererGX2 instance;
    return instance;
}

bool ShadowMapRendererGX2::Initialize(uint32_t resolution) {
    if (m_initialized && m_resolution == resolution) return true;
    Shutdown();

    m_resolution = (resolution >= 256u && resolution <= 2048u) ? resolution : DEFAULT_RESOLUTION;

#if defined(__WIIU__) || defined(__wiiu__)
    if (!InitDepthShader()) {
        Shutdown();
        return false;
    }

    std::memset(&m_shadowDepthBuffer, 0, sizeof(m_shadowDepthBuffer));
    m_shadowDepthBuffer.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    m_shadowDepthBuffer.surface.width     = m_resolution;
    m_shadowDepthBuffer.surface.height    = m_resolution;
    m_shadowDepthBuffer.surface.depth     = 1;
    m_shadowDepthBuffer.surface.mipLevels = 1;
    m_shadowDepthBuffer.surface.format    = GX2_SURFACE_FORMAT_FLOAT_R32;
    m_shadowDepthBuffer.surface.aa        = GX2_AA_MODE1X;
    m_shadowDepthBuffer.surface.use       = static_cast<GX2SurfaceUse>(GX2_SURFACE_USE_DEPTH_BUFFER | GX2_SURFACE_USE_TEXTURE);
    m_shadowDepthBuffer.surface.tileMode  = GX2_TILE_MODE_DEFAULT;
    m_shadowDepthBuffer.viewNumSlices     = 1;
    m_shadowDepthBuffer.depthClear        = 1.0f;
    GX2CalcSurfaceSizeAndAlignment(&m_shadowDepthBuffer.surface);

    const auto flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_DEPTH_BUFFER | GX2R_RESOURCE_BIND_TEXTURE |
        GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);

    if (!GX2RCreateSurface(&m_shadowDepthBuffer.surface, flags)) {
        Shutdown();
        return false;
    }
    // Orden canonico de Cafe OS: GX2InitDepthBufferRegs PRIMERO, luego GX2InitDepthBufferHiZEnable
    GX2InitDepthBufferRegs(&m_shadowDepthBuffer);
    GX2InitDepthBufferHiZEnable(&m_shadowDepthBuffer, FALSE);
    m_surfaceCreated = true;

    // Vista GX2Texture sobre la misma superficie de profundidad para muestreo PCF 2x2 en TMU
    std::memset(&m_shadowTexture, 0, sizeof(m_shadowTexture));
    m_shadowTexture.surface        = m_shadowDepthBuffer.surface;
    m_shadowTexture.surface.format = GX2_SURFACE_FORMAT_FLOAT_R32;
    m_shadowTexture.viewFirstMip   = 0;
    m_shadowTexture.viewNumMips    = 1;
    m_shadowTexture.viewFirstSlice = 0;
    m_shadowTexture.viewNumSlices  = 1;
    m_shadowTexture.compMap        = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_X, GX2_SQ_SEL_X, GX2_SQ_SEL_1);
    GX2InitTextureRegs(&m_shadowTexture);

    // Sampler hardware PCF 2x2 con comparacion de profundidad nativa en Latte
    GX2InitSampler(&m_shadowSampler, GX2_TEX_CLAMP_MODE_CLAMP_BORDER, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerBorderType(&m_shadowSampler, GX2_TEX_BORDER_TYPE_WHITE);
    GX2InitSamplerDepthCompare(&m_shadowSampler, GX2_COMPARE_FUNC_LEQUAL);
    GX2InitSamplerZMFilter(&m_shadowSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_POINT);
#endif

    m_initialized = true;
    return true;
}

bool ShadowMapRendererGX2::InitDepthShader() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded) return true;

    std::vector<uint8_t> shaderData;
    if (!AssetManager::LoadBinaryFile("shaders/shadow_depth_gx2.gsh", shaderData) || shaderData.empty()) {
        shaderData.assign(SHADOW_DEPTH_GX2_GSH_DATA, SHADOW_DEPTH_GX2_GSH_DATA + SHADOW_DEPTH_GX2_GSH_SIZE);
    }
    if (shaderData.empty()) return false;

    const uint32_t vsHdrSize  = GFDGetVertexShaderHeaderSize(0, shaderData.data());
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
    if (vsHdrSize == 0 || vsProgSize == 0) {
        return false;
    }

    m_depthVs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "ShadowDepth_VS_Hdr"));
    m_depthVsProgram = MemoryManager::Instance().AllocAligned(256, vsProgSize, MemoryTag::ShaderProgram, "ShadowDepth_VS_Prog");

    if (!m_depthVs || !m_depthVsProgram ||
        !GFDGetVertexShader(m_depthVs, m_depthVsProgram, 0, shaderData.data())) {
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_depthVsProgram, vsProgSize);

    GX2AttribStream attr{};
    attr.location   = 0;
    attr.buffer     = 0;
    attr.offset     = offsetof(MeshVertex, pos);
    attr.format     = GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
    attr.type       = GX2_ATTRIB_INDEX_PER_VERTEX;
    attr.aluDivisor = 0;
    attr.mask       = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
    attr.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(1, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    m_depthFsProgram = MemoryManager::Instance().AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "ShadowDepth_FS_Prog");
    if (!m_depthFsProgram) return false;

    GX2InitFetchShaderEx(&m_depthFs, static_cast<uint8_t*>(m_depthFsProgram), 1, &attr,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_depthFsProgram, fsSize);
    m_shaderLoaded = true;
    return true;
#else
    return true;
#endif
}

void ShadowMapRendererGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_surfaceCreated || m_shaderLoaded) {
        if (ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown()) {
            GX2DrawDone();
        }
    }
    if (m_surfaceCreated) {
        GX2RDestroySurfaceEx(&m_shadowDepthBuffer.surface, GX2R_RESOURCE_BIND_NONE);
        m_surfaceCreated = false;
    }
    // Liberacion incondicional de cualquier puntero no nulo (0 leaks ante fallos parciales de Init)
    if (m_depthVs)        { void* p = m_depthVs; MemoryManager::Instance().Free(p); m_depthVs = nullptr; }
    if (m_depthVsProgram) { MemoryManager::Instance().FreeAligned(m_depthVsProgram); m_depthVsProgram = nullptr; }
    if (m_depthFsProgram) { MemoryManager::Instance().FreeAligned(m_depthFsProgram); m_depthFsProgram = nullptr; }
    m_shaderLoaded = false;
#endif
    m_initialized = false;
}

#if defined(__WIIU__) || defined(__wiiu__)
bool ShadowMapRendererGX2::BeginShadowPass(const Matrix4x4& lightViewProj, GX2ContextState* contextState,
                                           bool cullFrontFaces,
                                           float depthBiasUnits,
                                           float depthBiasSlope) {
    if (!m_initialized || !m_surfaceCreated || !m_shaderLoaded || contextState == nullptr) {
        return false;
    }

    m_lightViewProj = lightViewProj;
    GX2ClearDepthStencilEx(&m_shadowDepthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_DEPTH);

    GX2SetContextState(contextState);
    GX2SetDepthBuffer(&m_shadowDepthBuffer);
    GX2SetViewport(0.0f, 0.0f, static_cast<float>(m_resolution), static_cast<float>(m_resolution), 0.0f, 1.0f);
    GX2SetScissor(0, 0, m_resolution, m_resolution);

    // Modo Z-only a doble tasa de AMD Latte: Color Writes OFF + Null Pixel Shader + Slope Depth Bias hardware
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, FALSE);
    GX2SetTargetChannelMasks(static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0),
                             static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0),
                             static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0),
                             static_cast<GX2ChannelMask>(0), static_cast<GX2ChannelMask>(0));
    GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LEQUAL);
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW,
                         cullFrontFaces ? TRUE : FALSE,
                         cullFrontFaces ? FALSE : TRUE,
                         FALSE,
                         GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
                         TRUE, TRUE, FALSE);
    GX2SetPolygonOffset(depthBiasUnits, depthBiasSlope, depthBiasUnits, depthBiasSlope, 0.0f);

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetFetchShader(&m_depthFs);
    GX2SetVertexShader(m_depthVs);
    GX2SetPixelShader(nullptr);
    return true;
}

void ShadowMapRendererGX2::RenderCaster(const StaticMesh& mesh, const Matrix4x4& modelMatrix) {
    if (!mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;
    RenderCasterBuffers(*const_cast<GX2RBuffer*>(mesh.GetVBO()),
                        *const_cast<GX2RBuffer*>(mesh.GetIBO()),
                        mesh.GetIndexCount(),
                        mesh.GetHeader().vertexStride,
                        modelMatrix,
                        mesh.GetHeader().indexFormat == 1);
}

void ShadowMapRendererGX2::RenderCasterBuffers(GX2RBuffer& vbo, GX2RBuffer& ibo, uint32_t indexCount,
                                               uint32_t vertexStride, const Matrix4x4& modelMatrix,
                                               bool use32BitIndices) {
    if (!m_initialized || indexCount == 0) return;

    const Matrix4x4 lightMVP = m_lightViewProj * modelMatrix;
    alignas(256) float ubo[16];
    std::memcpy(ubo, lightMVP.m, sizeof(ubo));
    GX2SetVertexUniformReg(0, 16, ubo);

    GX2RSetAttributeBuffer(&vbo, 0, vertexStride, 0);
    GX2RDrawIndexed(GX2_PRIMITIVE_MODE_TRIANGLES, &ibo,
                    use32BitIndices ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16,
                    indexCount, 0, 0, 1);
}

void ShadowMapRendererGX2::EndShadowPass(GX2ContextState* contextState,
                                         GX2ColorBuffer* restoreColor,
                                         GX2DepthBuffer* restoreDepth,
                                         uint32_t restoreWidth,
                                         uint32_t restoreHeight) {
    if (!m_initialized) return;

    GX2Flush();
    if (m_shadowDepthBuffer.surface.image != nullptr) {
        GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER, m_shadowDepthBuffer.surface.image,
                      m_shadowDepthBuffer.surface.imageSize);
    }
    if (m_shadowTexture.surface.image != nullptr) {
        GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, m_shadowTexture.surface.image,
                      m_shadowTexture.surface.imageSize);
    }

    if (contextState) {
        GX2SetContextState(contextState);
    }
    if (restoreColor) {
        GX2SetColorBuffer(restoreColor, GX2_RENDER_TARGET_0);
    }
    if (restoreDepth) {
        GX2SetDepthBuffer(restoreDepth);
    }
    GX2SetViewport(0.0f, 0.0f, static_cast<float>(restoreWidth), static_cast<float>(restoreHeight), 0.0f, 1.0f);
    GX2SetScissor(0, 0, restoreWidth, restoreHeight);
    GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE,
                         GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
                         FALSE, FALSE, FALSE);
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
                             GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
    RHIPipelineCache::Instance().Invalidate();
}

void ShadowMapRendererGX2::BindShadowMap(uint32_t texSlot, uint32_t samplerSlot) const {
    if (!m_initialized || !m_surfaceCreated) return;
    GX2SetPixelTexture(const_cast<GX2Texture*>(&m_shadowTexture), texSlot);
    GX2SetPixelSampler(const_cast<GX2Sampler*>(&m_shadowSampler), samplerSlot);
}
#endif

} // namespace WiiU
