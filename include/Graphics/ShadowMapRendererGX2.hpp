#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/ShadowSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/context.h>
#include <gx2/shaders.h>
#include <gx2/sampler.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2r/buffer.h>
#endif

namespace WiiU {

/**
 * @brief [OPT-126 / OPT-311 / OPT-327] Renderizador Hardware de Shadow Map PCF 2x2 para AMD Latte GX2.
 *
 * Administra:
 * - Superficie de profundidad dedicada (D32_FLOAT) con uso dual DEPTH_BUFFER | TEXTURE.
 * - Sampler hardware con comparacion de profundidad PCF 2x2 (GX2InitSamplerDepthCompare).
 * - Shader depth-only compilado en CafeGLSL (shadow_depth_gx2.gsh + fallback ROM).
 * - Cero alocaciones dinamicas en el bucle de render a 60 FPS.
 */
class ShadowMapRendererGX2 {
public:
    static constexpr uint32_t DEFAULT_RESOLUTION = 1024;

    static ShadowMapRendererGX2& Instance() noexcept;

    ShadowMapRendererGX2(const ShadowMapRendererGX2&) = delete;
    ShadowMapRendererGX2& operator=(const ShadowMapRendererGX2&) = delete;
    ShadowMapRendererGX2(ShadowMapRendererGX2&&) = delete;
    ShadowMapRendererGX2& operator=(ShadowMapRendererGX2&&) = delete;

    bool Initialize(uint32_t resolution = DEFAULT_RESOLUTION);
    void Shutdown();

    [[nodiscard]] bool IsReady() const noexcept { return m_initialized; }
    [[nodiscard]] uint32_t GetResolution() const noexcept { return m_resolution; }
    [[nodiscard]] const Matrix4x4& GetLightViewProj() const noexcept { return m_lightViewProj; }

#if defined(__WIIU__) || defined(__wiiu__)
    /**
     * @brief Inicia el pase de profundidad hacia el Shadow Map (Null PS + color writes OFF + hardware slope bias).
     *        Requiere contextState != nullptr para restaurar contexto tras GX2ClearDepthStencilEx.
     */
    bool BeginShadowPass(const Matrix4x4& lightViewProj, GX2ContextState* contextState,
                         bool cullFrontFaces = true,
                         float depthBiasUnits = 2.0f,
                         float depthBiasSlope = 1.5f);

    void RenderCaster(const StaticMesh& mesh, const Matrix4x4& modelMatrix);

    void RenderCasterBuffers(GX2RBuffer& vbo, GX2RBuffer& ibo, uint32_t indexCount,
                             uint32_t vertexStride, const Matrix4x4& modelMatrix,
                             bool use32BitIndices = false);

    /**
     * @brief Finaliza el pase de sombra, invalida cache de textura y restaura targets principales.
     */
    void EndShadowPass(GX2ContextState* contextState,
                       GX2ColorBuffer* restoreColor,
                       GX2DepthBuffer* restoreDepth,
                       uint32_t restoreWidth = 1280,
                       uint32_t restoreHeight = 720);

    /**
     * @brief Enlaza la textura de profundidad y el sampler hardware PCF 2x2 al slot indicado.
     */
    void BindShadowMap(uint32_t texSlot = 2, uint32_t samplerSlot = 2) const;

    /**
     * @brief Enlaza la textura de profundidad con muestreo puntual directo (sin DepthCompare) para PCF manual 3x3/5x5 y Debug.
     */
    void BindShadowMapRaw(uint32_t texSlot = 2, uint32_t samplerSlot = 2) const;

    [[nodiscard]] GX2Texture* GetShadowTexture() noexcept { return m_initialized ? &m_shadowTexture : nullptr; }
    [[nodiscard]] GX2Sampler* GetShadowSampler() noexcept { return m_initialized ? &m_shadowSampler : nullptr; }
    [[nodiscard]] GX2DepthBuffer* GetShadowDepthBuffer() noexcept { return m_initialized ? &m_shadowDepthBuffer : nullptr; }
#else
    bool BeginShadowPass(const Matrix4x4& lightViewProj, void* contextState,
                         bool cullFrontFaces = true,
                         float depthBiasUnits = 2.0f,
                         float depthBiasSlope = 1.5f);

    void RenderCaster(const StaticMesh& mesh, const Matrix4x4& modelMatrix);

    void RenderCasterBuffers(GX2RBuffer& vbo, GX2RBuffer& ibo, uint32_t indexCount,
                             uint32_t vertexStride, const Matrix4x4& modelMatrix,
                             bool use32BitIndices = false);

    void EndShadowPass(void* contextState,
                       void* restoreColor,
                       void* restoreDepth,
                       uint32_t restoreWidth = 1280,
                       uint32_t restoreHeight = 720);

    void BindShadowMap(uint32_t texSlot = 2, uint32_t samplerSlot = 2) const;
    void BindShadowMapRaw(uint32_t texSlot = 2, uint32_t samplerSlot = 2) const;

    [[nodiscard]] GX2Texture* GetShadowTexture() noexcept { return m_initialized ? &m_shadowTexture : nullptr; }
#endif

private:
    ShadowMapRendererGX2() noexcept = default;
    ~ShadowMapRendererGX2() = default; // Teardown explicito en Engine::Shutdown() (evita Static Destruction Order Fiasco)

    bool InitDepthShader();

    bool m_initialized{false};
    uint32_t m_resolution{DEFAULT_RESOLUTION};
    Matrix4x4 m_lightViewProj{};

#if defined(__WIIU__) || defined(__wiiu__)
    GX2DepthBuffer m_shadowDepthBuffer{};
    GX2Texture m_shadowTexture{};
    GX2Sampler m_shadowSampler{};
    GX2Sampler m_rawDepthSampler{};
    GX2ContextState* m_shadowContextState{nullptr};
    bool m_surfaceCreated{false};

    GX2VertexShader* m_depthVs{nullptr};
    void* m_depthVsProgram{nullptr};
    GX2PixelShader* m_depthPs{nullptr};
    void* m_depthPsProgram{nullptr};
    GX2FetchShader m_depthFs{};
    void* m_depthFsProgram{nullptr};
    bool m_shaderLoaded{false};
#else
    GX2Texture m_shadowTexture{};
    uint32_t m_shadowFbo{0u};
    uint32_t m_depthProgram{0u};
    uint32_t m_shadowVao{0u};
    int32_t m_mvpLoc{-1};
    int32_t m_savedViewport[4]{0, 0, 1280, 720};
    bool m_hasSavedViewport{false};
    bool m_inShadowPass{false};
#endif
};

} // namespace WiiU
