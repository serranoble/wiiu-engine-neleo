#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-457] Clave bitmask de permutaciones PBR compiladas (Capa 3).
 *
 * Seleccion O(1) de la variante minima de shader GX2 necesaria para cada malla,
 * evitando codigo muerto (TBN, bucles de luces o PCF) y maximizando la ocupacion
 * de wavefronts en AMD Latte R700.
 */
enum ShaderVariantFlags : uint8_t {
    SHADER_VARIANT_NONE       = 0u,
    SHADER_VARIANT_MULTILIGHT = 1u << 0u,
    SHADER_VARIANT_NORMAL_MAP = 1u << 1u,
    SHADER_VARIANT_SHADOWS    = 1u << 2u
};

enum class PBRVariantIndex : uint8_t {
    DirOnly       = 0u, // MULTILIGHT=0, NORMAL_MAP=0, SHADOWS=0 (24 floats)
    DirBump       = 1u, // MULTILIGHT=0, NORMAL_MAP=1, SHADOWS=0 (24 floats)
    MultiLight    = 2u, // MULTILIGHT=1, NORMAL_MAP=0, SHADOWS=0 (68 floats)
    MultiBump     = 3u, // MULTILIGHT=1, NORMAL_MAP=1, SHADOWS=0 (68 floats)
    Shadow        = 4u, // MULTILIGHT=1, NORMAL_MAP=1, SHADOWS=1 (72 floats)
    Count         = 5u
};

[[nodiscard]] constexpr uint8_t MakeShaderVariantKey(bool hasMultiLight,
                                                     bool hasNormalMap,
                                                     bool hasShadows) noexcept {
    uint8_t key = SHADER_VARIANT_NONE;
    if (hasMultiLight) key |= SHADER_VARIANT_MULTILIGHT;
    if (hasNormalMap)  key |= SHADER_VARIANT_NORMAL_MAP;
    if (hasShadows)    key |= SHADER_VARIANT_SHADOWS;
    return key;
}

[[nodiscard]] constexpr PBRVariantIndex SelectPBRVariant(uint8_t variantKey) noexcept {
    if ((variantKey & SHADER_VARIANT_SHADOWS) != 0u) {
        return PBRVariantIndex::Shadow;
    }
    const bool multi = (variantKey & SHADER_VARIANT_MULTILIGHT) != 0u;
    const bool bump  = (variantKey & SHADER_VARIANT_NORMAL_MAP) != 0u;
    if (multi) {
        return bump ? PBRVariantIndex::MultiBump : PBRVariantIndex::MultiLight;
    }
    return bump ? PBRVariantIndex::DirBump : PBRVariantIndex::DirOnly;
}

/**
 * @brief [OPT-457] Light-Culling por Bounding Sphere en CPU (Zero-Alloc, O(N)).
 *
 * Transforma la esfera envolvente precalculada de StaticMesh al espacio mundo
 * usando la escala maxima de las 3 columnas de modelMatrix, y filtra unicamente
 * las luces puntuales cuyo radio intersecta la esfera del objeto.
 *
 * Reutilizable tanto en Wii U (ForwardRendererGX2) como en PC/ROG Ally (ForwardRendererGL).
 */
[[nodiscard]] inline uint32_t CullPointLightsForMesh(const StaticMesh& mesh,
                                                     const Matrix4x4& modelMatrix,
                                                     const LightBlockUBO& lights,
                                                     uint32_t* outLightIndices,
                                                     uint32_t maxOut) noexcept {
    if (!outLightIndices || maxOut == 0u || lights.activePointLights == 0u) {
        return 0u;
    }

    const uint32_t totalLights = std::min<uint32_t>(lights.activePointLights, LightBlockUBO::MAX_POINT_LIGHTS);
    const float localRad = mesh.GetBoundingRadius();
    if (localRad <= 0.0001f || !std::isfinite(localRad)) {
        // Sin bounding sphere valida: conservador, incluir hasta maxOut luces activas
        const uint32_t outCount = std::min(totalLights, maxOut);
        for (uint32_t i = 0u; i < outCount; ++i) {
            outLightIndices[i] = i;
        }
        return outCount;
    }

    const Point3D localCenter = mesh.GetBoundingCenter();
    const float wx = modelMatrix.m[0] * localCenter.x + modelMatrix.m[4] * localCenter.y +
                     modelMatrix.m[8] * localCenter.z + modelMatrix.m[12];
    const float wy = modelMatrix.m[1] * localCenter.x + modelMatrix.m[5] * localCenter.y +
                     modelMatrix.m[9] * localCenter.z + modelMatrix.m[13];
    const float wz = modelMatrix.m[2] * localCenter.x + modelMatrix.m[6] * localCenter.y +
                     modelMatrix.m[10] * localCenter.z + modelMatrix.m[14];

    const float sxSq = modelMatrix.m[0] * modelMatrix.m[0] + modelMatrix.m[1] * modelMatrix.m[1] + modelMatrix.m[2] * modelMatrix.m[2];
    const float sySq = modelMatrix.m[4] * modelMatrix.m[4] + modelMatrix.m[5] * modelMatrix.m[5] + modelMatrix.m[6] * modelMatrix.m[6];
    const float szSq = modelMatrix.m[8] * modelMatrix.m[8] + modelMatrix.m[9] * modelMatrix.m[9] + modelMatrix.m[10] * modelMatrix.m[10];
    const float maxScale = std::sqrt(std::max({sxSq, sySq, szSq, 0.0001f}));
    const float worldRad = localRad * maxScale;

    uint32_t count = 0u;
    for (uint32_t i = 0u; i < totalLights; ++i) {
        const float lx = lights.pointLightPosRadius[i][0];
        const float ly = lights.pointLightPosRadius[i][1];
        const float lz = lights.pointLightPosRadius[i][2];
        const float lRad = lights.pointLightPosRadius[i][3];
        if (lRad <= 0.0f) continue;

        const float dx = wx - lx;
        const float dy = wy - ly;
        const float dz = wz - lz;
        const float sumRad = worldRad + lRad;
        if ((dx * dx + dy * dy + dz * dz) <= (sumRad * sumRad)) {
            outLightIndices[count++] = i;
            if (count >= maxOut) {
                break;
            }
        }
    }
    return count;
}

} // namespace Neleo
