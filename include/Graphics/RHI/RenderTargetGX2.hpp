#pragma once

#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/context.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#endif

namespace WiiU::Graphics {

enum class RenderTargetFormat : uint8_t {
    RGBA8_UNorm  = 0u,
    RGBA16_Float = 1u
};

/**
 * @brief Render Target off-screen reutilizable para AMD Latte GX2 (RGBA8 / HDR RGBA16F + Depth D32F opcional + GX2Texture).
 *
 * Administra un GX2ContextState dedicado para operar a resoluciones independientes del
 * framebuffer principal (p. ej. 640x360 para Reflexión Planar en Suite G4, 1280x720 HDR RGBA16F
 * y 320x180 Bloom en Suite G5) sin conflictos de dimensiones en Cemu 2.6 ni en hardware real Wii U.
 */
class RenderTargetGX2 {
public:
    RenderTargetGX2() noexcept = default;
    ~RenderTargetGX2() { Shutdown(); }

    RenderTargetGX2(const RenderTargetGX2&) = delete;
    RenderTargetGX2& operator=(const RenderTargetGX2&) = delete;
    RenderTargetGX2(RenderTargetGX2&&) = delete;
    RenderTargetGX2& operator=(RenderTargetGX2&&) = delete;

    [[nodiscard]] bool Initialize(uint32_t width,
                                  uint32_t height,
                                  const char* debugLabel = "RenderTargetGX2",
                                  RenderTargetFormat format = RenderTargetFormat::RGBA8_UNorm,
                                  bool withDepth = true);
    void Shutdown();

    [[nodiscard]] bool IsReady() const noexcept { return m_initialized; }
    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] RenderTargetFormat GetFormat() const noexcept { return m_format; }
    [[nodiscard]] bool HasDepthBuffer() const noexcept { return m_withDepth; }

#if defined(__WIIU__) || defined(__wiiu__)
    /**
     * @brief Activa el contexto dedicado del Render Target, enlaza Color (+ Depth opcional) y limpia.
     */
    [[nodiscard]] bool BeginPass(float clearR = 0.0f,
                                 float clearG = 0.0f,
                                 float clearB = 0.0f,
                                 float clearA = 1.0f,
                                 float clearDepth = 1.0f);

    /**
     * @brief Invalida cachés de color/textura para lectura inmediata en TMU y restaura el framebuffer principal.
     */
    void EndPass(GX2ContextState* restoreContext,
                 GX2ColorBuffer* restoreColor,
                 GX2DepthBuffer* restoreDepth,
                 uint32_t restoreWidth = 1280u,
                 uint32_t restoreHeight = 720u);

    /**
     * @brief Invalida cachés de color/textura al finalizar escritura para encadenar otro pase off-screen sin restaurar TV.
     */
    void FlushForSampling();

    [[nodiscard]] GX2Texture* GetColorTexture() noexcept { return m_initialized ? &m_colorTexture : nullptr; }
    [[nodiscard]] GX2ColorBuffer* GetColorBuffer() noexcept { return m_initialized ? &m_colorBuffer : nullptr; }
    [[nodiscard]] GX2DepthBuffer* GetDepthBuffer() noexcept { return (m_initialized && m_withDepth) ? &m_depthBuffer : nullptr; }
    [[nodiscard]] const GX2ColorBuffer* GetColorBuffer() const noexcept { return m_initialized ? &m_colorBuffer : nullptr; }
    [[nodiscard]] const GX2DepthBuffer* GetDepthBuffer() const noexcept { return (m_initialized && m_withDepth) ? &m_depthBuffer : nullptr; }
#else
    [[nodiscard]] bool BeginPass(float clearR = 0.0f,
                                 float clearG = 0.0f,
                                 float clearB = 0.0f,
                                 float clearA = 1.0f,
                                 float clearDepth = 1.0f);
    void EndPass(void* restoreContext,
                 void* restoreColor,
                 void* restoreDepth,
                 uint32_t restoreWidth = 1280u,
                 uint32_t restoreHeight = 720u);
    void FlushForSampling();

    [[nodiscard]] GX2Texture* GetColorTexture() noexcept { return m_initialized ? &m_colorTexture : nullptr; }
#endif

private:
    bool m_initialized{false};
    uint32_t m_width{0u};
    uint32_t m_height{0u};
    RenderTargetFormat m_format{RenderTargetFormat::RGBA8_UNorm};
    bool m_withDepth{true};

#if defined(__WIIU__) || defined(__wiiu__)
    GX2ColorBuffer m_colorBuffer{};
    GX2DepthBuffer m_depthBuffer{};
    GX2Texture m_colorTexture{};
    GX2ContextState* m_contextState{nullptr};
    bool m_colorCreated{false};
    bool m_depthCreated{false};
#else
    GX2Texture m_colorTexture{};
    uint32_t m_fbo{0u};
    uint32_t m_depthRbo{0u};
    int32_t m_savedViewport[4]{0, 0, 1280, 720};
    bool m_hasSavedViewport{false};
#endif
};

} // namespace WiiU::Graphics
