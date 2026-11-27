#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Neleo {

/**
 * @brief [OPT-459] Cubetas de ordenamiento de la Render Queue PBR del motor.
 *
 * 0 = Opaque      : Front-to-Back (maximiza rechazo Early-Z / HiZ en AMD Latte y OpenGL 3.3).
 * 1 = AlphaCutout : Front-to-Back tras los opacos (Early-Z descarta pixeles ocultos antes del discard).
 * 2 = Transparent : Back-to-Front (blending correcto automatico sin ordenamiento manual en escenas).
 */
enum class RenderBucket : uint8_t {
    Opaque      = 0u,
    AlphaCutout = 1u,
    Transparent = 2u
};

/**
 * @brief Payload compacto de material sin alignas(256) ni reservedPad[48] (~88 B vs 512 B).
 * Evita inflacion de memoria y mantiene la RenderQueuePBR cache-friendly en IBM Espresso L1D/L2.
 */
struct CompactMaterialPBR {
    float albedoTint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float materialParams[4]{0.0f, 1.0f, 2.0f, 0.38f};
    float pbrParams[4]{0.5f, 0.0f, 1.0f, 0.0f};
    float emissive[4]{0.0f, 0.0f, 0.0f, 0.0f};
    RHITexturePtr diffuseTexture{nullptr};
    RHITexturePtr envCubeMap{nullptr};
    uint16_t shaderId{0u};
    BlendMode blendMode{BlendMode::Opaque};
    bool depthTest{true};
    bool depthWrite{true};
    bool backfaceCull{true};

    void FromBlock(const MaterialPropertyBlock& src) noexcept {
        std::memcpy(albedoTint, src.ubo.albedoTint, sizeof(albedoTint));
        std::memcpy(materialParams, src.ubo.materialParams, sizeof(materialParams));
        std::memcpy(pbrParams, src.ubo.pbrParams, sizeof(pbrParams));
        std::memcpy(emissive, src.ubo.emissive, sizeof(emissive));
        diffuseTexture = src.diffuseTexture;
        envCubeMap = src.envCubeMap;
        shaderId = src.shaderId;
        blendMode = src.blendMode;
        depthTest = src.depthTest;
        depthWrite = src.depthWrite;
        backfaceCull = src.backfaceCull;
    }

    void ToBlock(MaterialPropertyBlock& dst) const noexcept {
        std::memcpy(dst.ubo.albedoTint, albedoTint, sizeof(albedoTint));
        std::memcpy(dst.ubo.materialParams, materialParams, sizeof(materialParams));
        std::memcpy(dst.ubo.pbrParams, pbrParams, sizeof(pbrParams));
        std::memcpy(dst.ubo.emissive, emissive, sizeof(emissive));
        dst.diffuseTexture = diffuseTexture;
        dst.envCubeMap = envCubeMap;
        dst.shaderId = shaderId;
        dst.blendMode = blendMode;
        dst.depthTest = depthTest;
        dst.depthWrite = depthWrite;
        dst.backfaceCull = backfaceCull;
    }
};

struct RenderCommandPBR {
    const StaticMesh* mesh{nullptr};
    Matrix4x4 modelMatrix{};
    CompactMaterialPBR material{};
    uint32_t shadingMode{0u};
};
static_assert(sizeof(RenderCommandPBR) <= 192u, "RenderCommandPBR debe permanecer compacto (<= 192 B, sin alignas(256))");

/**
 * @brief Entrada ligera de 16 bytes para ordenamiento L1D cache-friendly (256 x 16 B = 4 KB).
 */
struct SortEntryPBR {
    uint64_t sortKey{0u};
    uint32_t cmdIndex{0u};
};
static_assert(sizeof(SortEntryPBR) == 16u, "SortEntryPBR debe medir 16 bytes");

[[nodiscard]] inline RenderBucket ClassifyRenderBucket(const MaterialPropertyBlock& material,
                                                       uint32_t shadingMode) noexcept {
    if (material.blendMode == BlendMode::AlphaBlend || material.blendMode == BlendMode::Additive) {
        return RenderBucket::Transparent;
    }
    const bool isMode7Cutout = (shadingMode == 7u) && (material.ubo.materialParams[0] > 0.5f);
    if (material.blendMode == BlendMode::AlphaCutout || isMode7Cutout) {
        return RenderBucket::AlphaCutout;
    }
    return RenderBucket::Opaque;
}

[[nodiscard]] inline uint16_t QuantizeDepth16(float dist, float maxDist = 256.0f) noexcept {
    if (!std::isfinite(dist) || dist <= 0.0f) {
        return 0u;
    }
    const float norm = std::clamp(dist / maxDist, 0.0f, 1.0f);
    return static_cast<uint16_t>(norm * 65535.0f + 0.5f);
}

/**
 * @brief Construye una SortKey determinista de 64 bits (Christer Ericson / GEA Cap. 11).
 *
 * Layout:
 *   [63..62] : RenderBucket (0=Opaque, 1=AlphaCutout, 2=Transparent)
 *   [61..46] : Profundidad cuantizada 16-bit (ascendente Front-to-Back en 0/1; descendente Back-to-Front en 2)
 *   [45..40] : shadingMode (6 bits)
 *   [39..0]  : Tag de estado (puntero de textura/malla truncado para agrupar state changes a igual profundidad)
 */
[[nodiscard]] inline uint64_t BuildRenderSortKey(const StaticMesh& mesh,
                                                 const Matrix4x4& modelMatrix,
                                                 const MaterialPropertyBlock& material,
                                                 uint32_t shadingMode,
                                                 const Point3D& camPos) noexcept {
    const RenderBucket bucket = ClassifyRenderBucket(material, shadingMode);

    const Point3D localCenter = mesh.GetBoundingCenter();
    const float wx = modelMatrix.m[0] * localCenter.x + modelMatrix.m[4] * localCenter.y +
                     modelMatrix.m[8] * localCenter.z + modelMatrix.m[12];
    const float wy = modelMatrix.m[1] * localCenter.x + modelMatrix.m[5] * localCenter.y +
                     modelMatrix.m[9] * localCenter.z + modelMatrix.m[13];
    const float wz = modelMatrix.m[2] * localCenter.x + modelMatrix.m[6] * localCenter.y +
                     modelMatrix.m[10] * localCenter.z + modelMatrix.m[14];

    const float dx = wx - camPos.x;
    const float dy = wy - camPos.y;
    const float dz = wz - camPos.z;
    const float centerDist = std::sqrt(dx * dx + dy * dy + dz * dz);

    float sortDist = centerDist;
    if (bucket != RenderBucket::Transparent) {
        // Sesgo por radio envolvente en opacos/cutout: evita que planos extensos (suelos)
        // se dibujen antes que mallas compactas apoyadas sobre ellos.
        const float sxSq = modelMatrix.m[0] * modelMatrix.m[0] + modelMatrix.m[1] * modelMatrix.m[1] + modelMatrix.m[2] * modelMatrix.m[2];
        const float sySq = modelMatrix.m[4] * modelMatrix.m[4] + modelMatrix.m[5] * modelMatrix.m[5] + modelMatrix.m[6] * modelMatrix.m[6];
        const float szSq = modelMatrix.m[8] * modelMatrix.m[8] + modelMatrix.m[9] * modelMatrix.m[9] + modelMatrix.m[10] * modelMatrix.m[10];
        const float maxScale = std::sqrt(std::max({sxSq, sySq, szSq, 0.0001f}));
        const float worldRad = std::max(mesh.GetBoundingRadius(), 0.0f) * maxScale;
        sortDist = centerDist + worldRad * 0.25f;
    }

    const uint16_t depth16 = QuantizeDepth16(sortDist);
    const uint16_t orderedDepth = (bucket == RenderBucket::Transparent)
        ? static_cast<uint16_t>(0xFFFFu - depth16)
        : depth16;

    const uint64_t bucketBits = (static_cast<uint64_t>(bucket) & 0x3ull) << 62u;
    const uint64_t depthBits  = static_cast<uint64_t>(orderedDepth) << 46u;
    const uint64_t modeBits   = (static_cast<uint64_t>(shadingMode) & 0x3Full) << 40u;
    const uint64_t stateTag   = (reinterpret_cast<uintptr_t>(material.diffuseTexture) ^
                                 (reinterpret_cast<uintptr_t>(&mesh) >> 4u)) & 0xFFFFFFFFFFull;

    return bucketBits | depthBits | modeBits | stateTag;
}

/**
 * @brief [OPT-459] Cola de renderizado Zero-Alloc (capacidad fija en memoria plana + SortEntryPBR 16B).
 * Compartida por ForwardRendererGX2 (Wii U) y ForwardRendererGL (Windows / ROG Ally).
 */
template <uint32_t Capacity = 256u>
class BasicRenderQueuePBR {
public:
    static constexpr uint32_t MAX_COMMANDS = Capacity;

    void Clear() noexcept { m_count = 0u; }

    [[nodiscard]] bool Submit(const StaticMesh& mesh,
                              const Matrix4x4& modelMatrix,
                              const MaterialPropertyBlock& material,
                              uint32_t shadingMode,
                              const Point3D& camPos) noexcept {
        if (m_count >= MAX_COMMANDS) {
            return false;
        }
        const uint32_t idx = m_count++;
        RenderCommandPBR& cmd = m_commands[idx];
        cmd.mesh = &mesh;
        cmd.modelMatrix = modelMatrix;
        cmd.material.FromBlock(material);
        cmd.shadingMode = shadingMode;

        m_sortEntries[idx].sortKey = BuildRenderSortKey(mesh, modelMatrix, material, shadingMode, camPos);
        m_sortEntries[idx].cmdIndex = idx;
        return true;
    }

    void Sort() noexcept {
        if (m_count <= 1u) {
            return;
        }
        std::sort(m_sortEntries, m_sortEntries + m_count,
                  [](const SortEntryPBR& a, const SortEntryPBR& b) noexcept {
                      return a.sortKey < b.sortKey;
                  });
    }

    [[nodiscard]] uint32_t GetCount() const noexcept { return m_count; }
    [[nodiscard]] const RenderCommandPBR& GetSortedCommand(uint32_t sortedIdx) const noexcept {
        return m_commands[m_sortEntries[sortedIdx].cmdIndex];
    }

private:
    SortEntryPBR m_sortEntries[MAX_COMMANDS]{};
    RenderCommandPBR m_commands[MAX_COMMANDS]{};
    uint32_t m_count{0u};
};

using RenderQueuePBR = BasicRenderQueuePBR<256u>;
static_assert(sizeof(RenderQueuePBR) <= 65536u, "RenderQueuePBR debe mantenerse <= 64 KB");

} // namespace Neleo
