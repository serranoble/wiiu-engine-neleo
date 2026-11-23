#include "Graphics/PostProcessHDRGX2.hpp"
#include "Graphics/PostProcessHDRShaderBinary.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <gx2r/draw.h>
#include <gfd.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU::Graphics {

bool PostProcessHDRGX2::Initialize(uint32_t width, uint32_t height) {
    if (m_initialized && m_width == width && m_height == height) {
        return true;
    }
    Shutdown();

    if (width < 64u || height < 64u || width > 1920u || height > 1080u) {
        return false;
    }
    m_width = width;
    m_height = height;

    // Alinear dimensiones de Bloom a múltiplos de 32 (evita macro-tiles parciales en RGBA16F en Latte/Cemu: 320x192 en 720p, 64x64 en 256p)
    const uint32_t rawBloomW = (width >= 256u) ? (width / 4u) : 64u;
    const uint32_t rawBloomH = (height >= 256u) ? (height / 4u) : 64u;
    const uint32_t bloomW = (rawBloomW + 31u) & ~31u;
    const uint32_t bloomH = (rawBloomH + 31u) & ~31u;

    if (!m_hdrRT.Initialize(m_width, m_height, "PostFX_HDR_RT", RenderTargetFormat::RGBA16_Float, true) ||
        !m_bloomPingRT.Initialize(bloomW, bloomH, "PostFX_BloomPing_RT", RenderTargetFormat::RGBA16_Float, false) ||
        !m_bloomPongRT.Initialize(bloomW, bloomH, "PostFX_BloomPong_RT", RenderTargetFormat::RGBA16_Float, false) ||
        !InitShaderAndQuad()) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

bool PostProcessHDRGX2::InitShaderAndQuad() {
#if defined(__WIIU__) || defined(__wiiu__)
    std::vector<uint8_t> shaderData;
    if (!AssetManager::LoadBinaryFile("shaders/postprocess_hdr_gx2.gsh", shaderData) || shaderData.empty()) {
        shaderData.assign(POSTPROCESS_HDR_GX2_GSH_DATA,
                          POSTPROCESS_HDR_GX2_GSH_DATA + POSTPROCESS_HDR_GX2_GSH_SIZE);
    }
    if (shaderData.empty()) return false;

    const uint32_t vsHdrSize  = GFDGetVertexShaderHeaderSize(0, shaderData.data());
    const uint32_t vsProgSize = GFDGetVertexShaderProgramSize(0, shaderData.data());
    const uint32_t psHdrSize  = GFDGetPixelShaderHeaderSize(0, shaderData.data());
    const uint32_t psProgSize = GFDGetPixelShaderProgramSize(0, shaderData.data());
    if (vsHdrSize == 0u || vsProgSize == 0u || psHdrSize == 0u || psProgSize == 0u) {
        return false;
    }

    m_vs        = static_cast<GX2VertexShader*>(MemoryManager::Instance().Alloc(vsHdrSize, MemoryTag::ShaderProgram, "PostHDR_VS_Hdr"));
    m_vsProgram = MemoryManager::Instance().AllocAligned(256u, vsProgSize, MemoryTag::ShaderProgram, "PostHDR_VS_Prog");
    m_ps        = static_cast<GX2PixelShader*>(MemoryManager::Instance().Alloc(psHdrSize, MemoryTag::ShaderProgram, "PostHDR_PS_Hdr"));
    m_psProgram = MemoryManager::Instance().AllocAligned(256u, psProgSize, MemoryTag::ShaderProgram, "PostHDR_PS_Prog");

    if (!m_vs || !m_vsProgram || !m_ps || !m_psProgram ||
        !GFDGetVertexShader(m_vs, m_vsProgram, 0, shaderData.data()) ||
        !GFDGetPixelShader(m_ps, m_psProgram, 0, shaderData.data())) {
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_vsProgram, vsProgSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_psProgram, psProgSize);

    // Reflexión dinámica de slots TMU (uSceneHDR y uBloomTex)
    m_sceneTexSlot = -1;
    m_bloomTexSlot = -1;
    if (m_ps->samplerVarCount > 0u && m_ps->samplerVars != nullptr) {
        for (uint32_t i = 0u; i < m_ps->samplerVarCount; ++i) {
            const auto& sv = m_ps->samplerVars[i];
            if (!sv.name) continue;
            if (std::strcmp(sv.name, "uSceneHDR") == 0) {
                m_sceneTexSlot = static_cast<int32_t>(sv.location);
            } else if (std::strcmp(sv.name, "uBloomTex") == 0) {
                m_bloomTexSlot = static_cast<int32_t>(sv.location);
            }
        }
    }
    m_hasReflectedSamplers = (m_sceneTexSlot >= 0 && m_bloomTexSlot >= 0);
    if (m_sceneTexSlot < 0) m_sceneTexSlot = 0;
    if (m_bloomTexSlot < 0) m_bloomTexSlot = 1;

    GX2AttribStream attrs[2]{};
    attrs[0].location   = 0u;
    attrs[0].buffer     = 0u;
    attrs[0].offset     = 0u;
    attrs[0].format     = GX2_ATTRIB_FORMAT_FLOAT_32_32;
    attrs[0].type       = GX2_ATTRIB_INDEX_PER_VERTEX;
    attrs[0].aluDivisor = 0u;
    attrs[0].mask       = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    attrs[1].location   = 1u;
    attrs[1].buffer     = 0u;
    attrs[1].offset     = 2u * sizeof(float);
    attrs[1].format     = GX2_ATTRIB_FORMAT_FLOAT_32_32;
    attrs[1].type       = GX2_ATTRIB_INDEX_PER_VERTEX;
    attrs[1].aluDivisor = 0u;
    attrs[1].mask       = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(2u, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    m_fsProgram = MemoryManager::Instance().AllocAligned(256u, fsSize, MemoryTag::ShaderProgram, "PostHDR_FS_Prog");
    if (!m_fsProgram) return false;

    GX2InitFetchShaderEx(&m_fs, static_cast<uint8_t*>(m_fsProgram), 2u, attrs,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_fsProgram, fsSize);

    GX2InitSampler(&m_linearClampSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    GX2InitSamplerXYFilter(&m_linearClampSampler, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_XY_FILTER_MODE_LINEAR, GX2_TEX_ANISO_RATIO_NONE);
    GX2InitSamplerZMFilter(&m_linearClampSampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);

    // Quad fullscreen en TRIANGLE_STRIP (GX2: y=+1 es arriba -> v=0; y=-1 es abajo -> v=1)
    static constexpr float kQuadVerts[16] = {
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f
    };

    std::memset(&m_quadVBO, 0, sizeof(m_quadVBO));
    m_quadVBO.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
    m_quadVBO.elemSize = 4u * sizeof(float);
    m_quadVBO.elemCount = 4u;
    if (!GX2RCreateBuffer(&m_quadVBO)) {
        return false;
    }
    void* mapped = GX2RLockBufferEx(&m_quadVBO, GX2R_RESOURCE_BIND_NONE);
    if (!mapped) {
        GX2RDestroyBufferEx(&m_quadVBO, GX2R_RESOURCE_BIND_NONE);
        return false;
    }
    std::memcpy(mapped, kQuadVerts, sizeof(kQuadVerts));
    GX2RUnlockBufferEx(&m_quadVBO, GX2R_RESOURCE_BIND_NONE);
    m_quadCreated = true;
#else
    m_sceneTexSlot = 0;
    m_bloomTexSlot = 1;
    m_hasReflectedSamplers = true;
#endif
    return true;
}

void PostProcessHDRGX2::Shutdown() {
#if defined(__WIIU__) || defined(__wiiu__)
    const bool hasGpu = m_quadCreated || m_vs || m_ps || m_fsProgram ||
                        m_hdrRT.IsReady() || m_bloomPingRT.IsReady() || m_bloomPongRT.IsReady();
    if (hasGpu && ProcUIIsRunning() && ProcUIInForeground() && !ProcUIInShutdown() &&
        Window::HasForeground() && !Window::IsExitingProcUI()) {
        GX2DrawDone();
    }

    if (m_quadCreated) {
        GX2RDestroyBufferEx(&m_quadVBO, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_quadVBO, 0, sizeof(m_quadVBO));
        m_quadCreated = false;
    }
    if (m_vs)        { void* raw = m_vs; MemoryManager::Instance().Free(raw); m_vs = nullptr; }
    if (m_vsProgram) { MemoryManager::Instance().FreeAligned(m_vsProgram); m_vsProgram = nullptr; }
    if (m_ps)        { void* raw = m_ps; MemoryManager::Instance().Free(raw); m_ps = nullptr; }
    if (m_psProgram) { MemoryManager::Instance().FreeAligned(m_psProgram); m_psProgram = nullptr; }
    if (m_fsProgram) { MemoryManager::Instance().FreeAligned(m_fsProgram); m_fsProgram = nullptr; }
#endif
    m_bloomPongRT.Shutdown();
    m_bloomPingRT.Shutdown();
    m_hdrRT.Shutdown();
    m_hasReflectedSamplers = false;
    m_sceneTexSlot = -1;
    m_bloomTexSlot = -1;
    m_initialized = false;
}

#if defined(__WIIU__) || defined(__wiiu__)
bool PostProcessHDRGX2::BeginHDRScenePass(float clearR, float clearG, float clearB, float clearA) {
    if (!m_initialized) return false;
    return m_hdrRT.BeginPass(clearR, clearG, clearB, clearA, 1.0f);
}

void PostProcessHDRGX2::DrawFullscreenPass(const float postData[16],
                                           GX2Texture* sceneTex,
                                           GX2Texture* bloomTex) {
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetFetchShader(&m_fs);
    GX2SetVertexShader(m_vs);
    GX2SetPixelShader(m_ps);

    alignas(256) float alignedData[16];
    std::memcpy(alignedData, postData, sizeof(alignedData));
    GX2SetPixelUniformReg(0u, 16u, alignedData);

    if (sceneTex && m_sceneTexSlot >= 0) {
        const uint32_t slot = static_cast<uint32_t>(m_sceneTexSlot);
        GX2SetPixelTexture(sceneTex, slot);
        GX2SetPixelSampler(&m_linearClampSampler, slot);
    }
    if (bloomTex && m_bloomTexSlot >= 0) {
        const uint32_t slot = static_cast<uint32_t>(m_bloomTexSlot);
        GX2SetPixelTexture(bloomTex, slot);
        GX2SetPixelSampler(&m_linearClampSampler, slot);
    }

    RHIPipelineCache::Instance().BindState(
        RHIPipelineState(false, false, RHI_COMPARE_ALWAYS, BlendMode::Opaque, false));

    GX2RSetAttributeBuffer(&m_quadVBO, 0u, m_quadVBO.elemSize, 0u);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4u, 0u, 1u);
}

void PostProcessHDRGX2::EndHDRScenePassAndCompose(const PostProcessHDRSettings& settings,
                                                  GX2ContextState* restoreContext,
                                                  GX2ColorBuffer* restoreColor,
                                                  GX2DepthBuffer* restoreDepth,
                                                  uint32_t restoreWidth,
                                                  uint32_t restoreHeight) {
    if (!m_initialized) return;

    m_hdrRT.FlushForSampling();

    const float bloomTexelX = 1.0f / static_cast<float>(m_bloomPingRT.GetWidth());
    const float bloomTexelY = 1.0f / static_cast<float>(m_bloomPingRT.GetHeight());

    float postData[16]{};
    postData[1]  = static_cast<float>(settings.toneMap);
    postData[2]  = settings.exposure;
    postData[3]  = settings.bloomIntensity;
    postData[4]  = bloomTexelX;
    postData[5]  = bloomTexelY;
    postData[6]  = settings.bloomThreshold;
    postData[7]  = settings.gammaEnabled ? 1.0f : 0.0f;
    postData[8]  = settings.vignetteStrength;
    postData[9]  = settings.bloomEnabled ? 1.0f : 0.0f;

    // 1. Pases de Bloom Kawase 2D Isotrópico (Bright-Pass -> Kawase r=1.5 -> Kawase r=2.75)
    if (settings.bloomEnabled) {
        if (m_bloomPingRT.BeginPass(0.0f, 0.0f, 0.0f, 1.0f)) {
            postData[0] = 0.0f; // BrightPass
            DrawFullscreenPass(postData, m_hdrRT.GetColorTexture(), m_hdrRT.GetColorTexture());
            m_bloomPingRT.FlushForSampling();
        }
        if (m_bloomPongRT.BeginPass(0.0f, 0.0f, 0.0f, 1.0f)) {
            postData[0] = 1.0f;  // Kawase 2D Pass 1
            postData[10] = 1.5f; // uPostData[2].z = kawaseOffset
            DrawFullscreenPass(postData, m_bloomPingRT.GetColorTexture(), m_bloomPingRT.GetColorTexture());
            m_bloomPongRT.FlushForSampling();
        }
        if (m_bloomPingRT.BeginPass(0.0f, 0.0f, 0.0f, 1.0f)) {
            postData[0] = 1.0f;   // Kawase 2D Pass 2
            postData[10] = 2.75f; // uPostData[2].z = kawaseOffset
            DrawFullscreenPass(postData, m_bloomPongRT.GetColorTexture(), m_bloomPongRT.GetColorTexture());
            m_bloomPingRT.FlushForSampling();
        }
    }

    // 2. Restaurar framebuffer principal TV (1280x720) y ejecutar Composite HDR + Tone Mapping + sRGB
    m_hdrRT.EndPass(restoreContext, restoreColor, restoreDepth, restoreWidth, restoreHeight);

    postData[0] = 3.0f; // Composite
    GX2Texture* bloomTex = settings.bloomEnabled ? m_bloomPingRT.GetColorTexture() : m_hdrRT.GetColorTexture();
    DrawFullscreenPass(postData, m_hdrRT.GetColorTexture(), bloomTex);
}
#endif

} // namespace WiiU::Graphics
