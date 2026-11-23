#pragma once

#include "Graphics/RHI/RenderTargetGX2.hpp"
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/context.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2r/buffer.h>
#endif

namespace WiiU::Graphics {

enum class ToneMapOperator : uint8_t {
    NoneClamp = 0u,
    Reinhard  = 1u,
    ACES      = 2u
};

struct PostProcessHDRSettings {
    ToneMapOperator toneMap{ToneMapOperator::ACES};
    float exposure{1.05f};
    float bloomThreshold{1.05f};
    float bloomIntensity{0.85f};
    float vignetteStrength{0.35f};
    bool bloomEnabled{true};
    bool gammaEnabled{true};
};

/**
 * @brief Pipeline reutilizable de Post-Procesado HDR + Bloom Multipase para AMD Latte GX2.
 *
 * Encadena:
 * 1. Escena HDR en RenderTargetGX2 (1280x720 RGBA16_Float + Depth D32F).
 * 2. Bright-Pass HDR (320x180 RGBA16_Float).
 * 3. Blur Gaussiano separable 9-tap H -> V (320x180 RGBA16_Float).
 * 4. Composite final en framebuffer TV (Tone Mapping ACES/Reinhard/OFF + Exposición + Vignette + sRGB Gamma).
 */
class PostProcessHDRGX2 {
public:
    PostProcessHDRGX2() noexcept = default;
    ~PostProcessHDRGX2() { Shutdown(); }

    PostProcessHDRGX2(const PostProcessHDRGX2&) = delete;
    PostProcessHDRGX2& operator=(const PostProcessHDRGX2&) = delete;
    PostProcessHDRGX2(PostProcessHDRGX2&&) = delete;
    PostProcessHDRGX2& operator=(PostProcessHDRGX2&&) = delete;

    [[nodiscard]] bool Initialize(uint32_t width = 1280u, uint32_t height = 720u);
    void Shutdown();

    [[nodiscard]] bool IsReady() const noexcept { return m_initialized; }
    [[nodiscard]] bool HasAllSamplerSlots() const noexcept { return m_initialized && m_hasReflectedSamplers; }
    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] const RenderTargetGX2& GetHDRRenderTarget() const noexcept { return m_hdrRT; }
    [[nodiscard]] const RenderTargetGX2& GetBloomPingRenderTarget() const noexcept { return m_bloomPingRT; }
    [[nodiscard]] const RenderTargetGX2& GetBloomPongRenderTarget() const noexcept { return m_bloomPongRT; }

#if defined(__WIIU__) || defined(__wiiu__)
    [[nodiscard]] bool BeginHDRScenePass(float clearR = 0.02f,
                                         float clearG = 0.03f,
                                         float clearB = 0.05f,
                                         float clearA = 1.0f);

    void EndHDRScenePassAndCompose(const PostProcessHDRSettings& settings,
                                   GX2ContextState* restoreContext,
                                   GX2ColorBuffer* restoreColor,
                                   GX2DepthBuffer* restoreDepth,
                                   uint32_t restoreWidth = 1280u,
                                   uint32_t restoreHeight = 720u);
#endif

private:
    bool InitShaderAndQuad();
#if defined(__WIIU__) || defined(__wiiu__)
    void DrawFullscreenPass(const float postData[16],
                            GX2Texture* sceneTex,
                            GX2Texture* bloomTex);
#endif

    bool m_initialized{false};
    bool m_hasReflectedSamplers{false};
    uint32_t m_width{1280u};
    uint32_t m_height{720u};
    int32_t m_sceneTexSlot{-1};
    int32_t m_bloomTexSlot{-1};

    RenderTargetGX2 m_hdrRT;
    RenderTargetGX2 m_bloomPingRT;
    RenderTargetGX2 m_bloomPongRT;

#if defined(__WIIU__) || defined(__wiiu__)
    GX2VertexShader* m_vs{nullptr};
    void* m_vsProgram{nullptr};
    GX2PixelShader* m_ps{nullptr};
    void* m_psProgram{nullptr};
    GX2FetchShader m_fs{};
    void* m_fsProgram{nullptr};
    GX2Sampler m_linearClampSampler{};
    GX2RBuffer m_quadVBO{};
    bool m_quadCreated{false};
#endif
};

} // namespace WiiU::Graphics
