#include "Graphics/RenderQueue.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace WiiU {

uint64_t RenderQueue::BuildSortKey(RenderLayer layer,
                                   BlendMode blendMode,
                                   uint16_t shaderId,
                                   uint16_t materialId,
                                   float viewDepth) noexcept {
    // Guarda IEEE-754 anti-NaN/Inf inmune a -ffast-math y cuantizacion float a 24 bits (sin promocion double)
    uint32_t depthBits = 0;
    std::memcpy(&depthBits, &viewDepth, sizeof(depthBits));
    const bool isFinite = (depthBits & 0x7F800000u) != 0x7F800000u;
    const float safeDepth = isFinite ? std::clamp(viewDepth, 0.0f, 500.0f) : 500.0f;
    const float normDepth = safeDepth * (1.0f / 500.0f);
    uint32_t depthKey = static_cast<uint32_t>(normDepth * 16777215.0f) << 8u;

    // Transparentes se ordenan Back-to-Front (invertir bits de profundidad)
    if (layer == RenderLayer::Transparent || blendMode == BlendMode::AlphaBlend || blendMode == BlendMode::Additive) {
        depthKey = ~depthKey;
    }

    const uint64_t lBits = (static_cast<uint64_t>(layer) & 0xFu) << 60u;
    const uint64_t bBits = (static_cast<uint64_t>(blendMode) & 0x3u) << 58u;
    const uint64_t sBits = (static_cast<uint64_t>(shaderId) & 0x3FFu) << 48u;
    const uint64_t mBits = (static_cast<uint64_t>(materialId) & 0xFFFFu) << 32u;
    return lBits | bBits | sBits | mBits | static_cast<uint64_t>(depthKey);
}

bool RenderQueue::Submit(const RenderCommand& cmd) noexcept {
    if (m_count >= MAX_COMMANDS || !cmd.vbo || !cmd.ibo || cmd.indexCount == 0) {
        return false;
    }
    const uint16_t idx = static_cast<uint16_t>(m_count++);
    m_commands[idx] = cmd;
    m_sortEntries[idx] = SortEntry{cmd.sortKey, idx};
    return true;
}

void RenderQueue::Sort() noexcept {
    if (m_count <= 1) return;
    std::sort(m_sortEntries.begin(), m_sortEntries.begin() + static_cast<std::ptrdiff_t>(m_count),
              [](const SortEntry& a, const SortEntry& b) noexcept {
                  return a.key < b.key;
              });
}

void RenderQueue::FlushDepthPrePass(ForwardRendererGX2& renderer) const {
#if defined(__WIIU__) || defined(__wiiu__)
    for (size_t i = 0; i < m_count; ++i) {
        const RenderCommand& cmd = m_commands[m_sortEntries[i].cmdIndex];
        if (cmd.isSkinned || cmd.layer != RenderLayer::Opaque ||
            cmd.blendMode != BlendMode::Opaque || !cmd.depthWrite || !cmd.depthTest) {
            continue;
        }
        renderer.RenderDepthPrePassBuffers(
            *static_cast<GX2RBuffer*>(cmd.vbo),
            *static_cast<GX2RBuffer*>(cmd.ibo),
            cmd.indexCount,
            cmd.vertexStride,
            cmd.modelMatrix,
            cmd.use32BitIndices
        );
    }
#else
    (void)renderer;
#endif
}

void RenderQueue::FlushRange(ForwardRendererGX2& renderer,
                             RenderLayer minLayer,
                             RenderLayer maxLayer,
                             bool earlyZEqualPass) const {
#if defined(__WIIU__) || defined(__wiiu__)
    const uint8_t minL = static_cast<uint8_t>(minLayer);
    const uint8_t maxL = static_cast<uint8_t>(maxLayer);

    for (size_t i = 0; i < m_count; ++i) {
        const RenderCommand& cmd = m_commands[m_sortEntries[i].cmdIndex];
        const uint8_t curL = static_cast<uint8_t>(cmd.layer);
        if (curL < minL || curL > maxL) {
            continue;
        }

        auto* diffTex = static_cast<GX2Texture*>(cmd.diffuseTexture);
        auto* envTex  = static_cast<GX2Texture*>(cmd.envCubeMap);

        if (cmd.isSkinned && cmd.boneMatrices && cmd.boneCount > 0) {
            renderer.RenderSkinnedMesh(
                *static_cast<GX2RBuffer*>(cmd.vbo),
                *static_cast<GX2RBuffer*>(cmd.ibo),
                cmd.indexCount,
                cmd.vertexStride,
                cmd.modelMatrix,
                cmd.boneMatrices,
                cmd.boneCount,
                diffTex,
                envTex,
                cmd.reflectionIntensity,
                cmd.clayMode,
                cmd.use32BitIndices
            );
        } else {
            const bool isOpaqueEarlyZ = earlyZEqualPass && (cmd.layer == RenderLayer::Opaque) &&
                                        (cmd.blendMode == BlendMode::Opaque) && cmd.depthWrite && cmd.depthTest;
            const bool effDepthWrite = isOpaqueEarlyZ ? false : cmd.depthWrite;
            const uint8_t effCompare = isOpaqueEarlyZ ? RHI_COMPARE_EQUAL : RHI_COMPARE_LEQUAL;

            renderer.RenderMeshBuffers(
                *static_cast<GX2RBuffer*>(cmd.vbo),
                *static_cast<GX2RBuffer*>(cmd.ibo),
                cmd.indexCount,
                cmd.vertexStride,
                cmd.modelMatrix,
                diffTex,
                envTex,
                cmd.reflectionIntensity,
                cmd.normalMapEnabled,
                cmd.bumpScale,
                cmd.ambientIntensity,
                cmd.use32BitIndices,
                cmd.blendMode,
                cmd.depthTest,
                effDepthWrite,
                effCompare
            );
        }
    }
#else
    (void)renderer; (void)minLayer; (void)maxLayer; (void)earlyZEqualPass;
#endif
}

void RenderQueue::Flush(ForwardRendererGX2& renderer) const {
    FlushRange(renderer, RenderLayer::DepthPrePass, RenderLayer::Overlay, false);
}

} // namespace WiiU
