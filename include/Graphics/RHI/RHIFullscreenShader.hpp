#pragma once

#include <cstdint>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/shaders.h>
#include <gx2r/buffer.h>
#include <whb/gfx.h>
#endif

namespace WiiU {

/**
 * @brief [Jason Gregory Cap. 10-11 / RHI] Pase Fullscreen reutilizable sobre GPU AMD Latte (GX2).
 *
 * Encapsula la carga de shaders `.gsh` (WHB + fallback Direct GFD), creación del VBO de quad,
 * reflexión de offsets de uniformes de píxel, enlace de estado vía `RHIPipelineCache` y emisión `GX2DrawEx`.
 * Desacopla por completo a las escenas de juego/demo de las cabeceras de driver `<gx2/*.h>`.
 */
class RHIFullscreenShader {
public:
    RHIFullscreenShader() = default;
    ~RHIFullscreenShader() { Shutdown(); }

    RHIFullscreenShader(const RHIFullscreenShader&) = delete;
    RHIFullscreenShader& operator=(const RHIFullscreenShader&) = delete;
    RHIFullscreenShader(RHIFullscreenShader&&) = delete;
    RHIFullscreenShader& operator=(RHIFullscreenShader&&) = delete;

    [[nodiscard]] bool PreloadBinary(const char* shaderAssetPath);
    [[nodiscard]] bool Initialize(const char* shaderAssetPath);
    void Shutdown();

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] int32_t GetPixelUniformOffset(const char* uniformName) const noexcept;

    void BindPipeline(uint32_t viewportW = 1280, uint32_t viewportH = 720) noexcept;
    void SetPixelUniform4fv(int32_t offset, uint32_t floatCount, const float* data) const noexcept;
    void DrawQuad() noexcept;

private:
#if defined(__WIIU__) || defined(__wiiu__)
    [[nodiscard]] bool TryLoadWHB(const uint8_t* data);
    [[nodiscard]] bool TryLoadDirectGFD(const uint8_t* data);
    [[nodiscard]] bool CreateQuadBuffer();
    [[nodiscard]] const GX2PixelShader* GetActivePixelShader() const noexcept;

    WHBGfxShaderGroup m_shaderGroup{};
    bool m_shaderLoaded{false};
    bool m_useDirectGFD{false};

    GX2VertexShader* m_directVs{nullptr};
    void* m_directVsProgram{nullptr};
    GX2PixelShader* m_directPs{nullptr};
    void* m_directPsProgram{nullptr};
    GX2FetchShader m_directFetchShader{};
    void* m_directFsProgram{nullptr};

    GX2RBuffer m_quadBuffer{};
    bool m_quadCreated{false};
#endif

    std::vector<uint8_t> m_stagedBinary;
};

} // namespace WiiU
