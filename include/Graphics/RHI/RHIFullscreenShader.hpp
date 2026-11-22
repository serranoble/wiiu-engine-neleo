#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2r/buffer.h>
#endif

namespace WiiU {

/**
 * @brief Preset estándar 16:9 para escalado dinámico interno sobre `RHIFullscreenShader`.
 */
struct RHIResolutionPreset {
    uint32_t width{1280};
    uint32_t height{720};
    const char* shortLabel{"720p"};
};

/**
 * @brief [Jason Gregory Cap. 10-11 / RHI] Pase Fullscreen reutilizable sobre GPU AMD Latte (GX2).
 *
 * Encapsula la carga de shaders `.gsh` (Direct GFD), creación del VBO de quad,
 * reflexión de offsets de uniformes de píxel, escalado bilineal por hardware desde resoluciones
 * internas 16:9 hacia el framebuffer nativo 1280x720, enlace vía `RHIPipelineCache` y `GX2DrawEx`.
 */
class RHIFullscreenShader {
public:
    static constexpr size_t STANDARD_16X9_PRESET_COUNT = 5;
    static constexpr RHIResolutionPreset STANDARD_16X9_PRESETS[STANDARD_16X9_PRESET_COUNT] = {
        {  640u, 360u, "360p" }, // 16:9 nHD (25.0% píxeles)
        {  854u, 480u, "480p" }, // 16:9 FWVGA GamePad (44.5% píxeles)
        {  960u, 540u, "540p" }, // 16:9 qHD (56.25% píxeles - por defecto)
        { 1152u, 648u, "648p" }, // 16:9 HD- (81.0% píxeles)
        { 1280u, 720u, "720p" }  // 16:9 HD nativo (100.0% píxeles)
    };

    struct OutputTargetBinding {
#if defined(__WIIU__) || defined(__wiiu__)
        GX2ColorBuffer* colorBuffer{nullptr};
        GX2DepthBuffer* depthBuffer{nullptr};
#else
        void* colorBuffer{nullptr};
        void* depthBuffer{nullptr};
#endif
        uint32_t width{1280};
        uint32_t height{720};
    };

    class ScopedActiveFrameTargets {
    public:
#if defined(__WIIU__) || defined(__wiiu__)
        ScopedActiveFrameTargets(GX2ColorBuffer* colorBuffer, GX2DepthBuffer* depthBuffer) noexcept {
            RHIFullscreenShader::SetActiveFrameTargets(colorBuffer, depthBuffer);
        }
#else
        ScopedActiveFrameTargets(void* colorBuffer, void* depthBuffer) noexcept {
            RHIFullscreenShader::SetActiveFrameTargets(colorBuffer, depthBuffer);
        }
#endif
        ~ScopedActiveFrameTargets() noexcept {
            RHIFullscreenShader::ClearActiveFrameTargets();
        }
        ScopedActiveFrameTargets(const ScopedActiveFrameTargets&) = delete;
        ScopedActiveFrameTargets& operator=(const ScopedActiveFrameTargets&) = delete;
    };

    RHIFullscreenShader() = default;
    ~RHIFullscreenShader() { Shutdown(); }

    RHIFullscreenShader(const RHIFullscreenShader&) = delete;
    RHIFullscreenShader& operator=(const RHIFullscreenShader&) = delete;
    RHIFullscreenShader(RHIFullscreenShader&&) = delete;
    RHIFullscreenShader& operator=(RHIFullscreenShader&&) = delete;

#if defined(__WIIU__) || defined(__wiiu__)
    static void SetActiveFrameTargets(GX2ColorBuffer* colorBuffer, GX2DepthBuffer* depthBuffer) noexcept;
#else
    static void SetActiveFrameTargets(void* colorBuffer, void* depthBuffer) noexcept;
#endif
    static void ClearActiveFrameTargets() noexcept;
    void SetOutputTarget(const OutputTargetBinding& target) noexcept;
    void ClearOutputTarget() noexcept;

    [[nodiscard]] bool PreloadBinary(const char* shaderAssetPath);
    [[nodiscard]] bool Initialize(const char* shaderAssetPath,
                                  const uint8_t* fallbackData = nullptr,
                                  size_t fallbackSize = 0);
    [[nodiscard]] bool EnableOffscreenScaler(uint32_t maxWidth = 1280, uint32_t maxHeight = 720);
    void Shutdown();

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] int32_t GetPixelUniformOffset(const char* uniformName) const noexcept;

    void BindPipeline(uint32_t viewportW = 1280, uint32_t viewportH = 720) noexcept;
    void BindPipeline(uint32_t viewportW, uint32_t viewportH, const OutputTargetBinding* outputTarget) noexcept;
    void BindPipeline(const RHIResolutionPreset& preset, const OutputTargetBinding* outputTarget = nullptr) noexcept {
        BindPipeline(preset.width, preset.height, outputTarget);
    }
    void SetPixelUniform4fv(int32_t offset, uint32_t floatCount, const float* data) const noexcept;
    void DrawQuad() noexcept;

private:
#if defined(__WIIU__) || defined(__wiiu__)
    struct DirectShaderPair {
        GX2VertexShader* vs{nullptr};
        void* vsProgram{nullptr};
        GX2PixelShader* ps{nullptr};
        void* psProgram{nullptr};
    };

    [[nodiscard]] static bool LoadShaderPair(const uint8_t* data, DirectShaderPair& out, const char* tag);
    static void FreeShaderPair(DirectShaderPair& pair) noexcept;
    [[nodiscard]] static bool InitQuadFetchShader(const GX2VertexShader* vs,
                                                  GX2FetchShader& outFetch,
                                                  void*& outProgram,
                                                  const char* tag);
    [[nodiscard]] static int32_t FindPixelUniformOffset(const GX2PixelShader* ps, const char* uniformName) noexcept;
    [[nodiscard]] OutputTargetBinding ResolveOutputTarget(const OutputTargetBinding* explicitTarget) const noexcept;

    [[nodiscard]] bool TryLoadDirectGFD(const uint8_t* data);
    void FreeDirectGFD() noexcept;
    [[nodiscard]] bool InitOffscreenScaler(uint32_t maxWidth, uint32_t maxHeight);
    void ShutdownOffscreenScaler() noexcept;
    [[nodiscard]] bool CreateQuadBuffer();
    [[nodiscard]] const GX2PixelShader* GetActivePixelShader() const noexcept;

    static OutputTargetBinding s_frameTarget;
    OutputTargetBinding m_outputTarget{};
    OutputTargetBinding m_boundTarget{};

    bool m_shaderLoaded{false};

    DirectShaderPair m_mainShader{};
    GX2FetchShader m_directFetchShader{};
    void* m_directFsProgram{nullptr};

    DirectShaderPair m_blitShader{};
    GX2FetchShader m_blitFetchShader{};
    void* m_blitFsProgram{nullptr};
    GX2ColorBuffer m_offscreenColorBuffer{};
    GX2Texture m_offscreenTexture{};
    GX2Sampler m_blitSampler{};
    void* m_offscreenImagePtr{nullptr};
    int32_t m_blitTransformOffset{-1};
    int32_t m_blitClampOffset{-1};
    uint32_t m_blitSamplerLocation{0};
    uint32_t m_offscreenWidth{0};
    uint32_t m_offscreenHeight{0};
    uint32_t m_activeViewportW{1280};
    uint32_t m_activeViewportH{720};
    bool m_offscreenReady{false};
    bool m_renderingOffscreen{false};

    GX2RBuffer m_quadBuffer{};
    bool m_quadCreated{false};
#endif

    std::vector<uint8_t> m_stagedBinary;
};

} // namespace WiiU
