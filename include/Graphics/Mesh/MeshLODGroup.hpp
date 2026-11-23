#pragma once

#include <cstdint>

namespace WiiU {

class StaticMesh;

/**
 * @brief Selector reutilizable de 3 niveles de detalle (LOD0 Alta, LOD1 Media, LOD2 Baja)
 *        por distancia al cuadrado (O(1), Zero-Alloc, RTR4 Cap. 19).
 */
class MeshLODGroup {
public:
    static constexpr uint32_t MAX_LEVELS = 3u;

    MeshLODGroup() noexcept = default;

    void SetMeshes(const StaticMesh* lod0High,
                   const StaticMesh* lod1Mid,
                   const StaticMesh* lod2Low) noexcept {
        m_meshes[0] = lod0High;
        m_meshes[1] = lod1Mid ? lod1Mid : lod0High;
        m_meshes[2] = lod2Low ? lod2Low : m_meshes[1];
    }

    void SetDistances(float lod1Dist, float lod2Dist) noexcept {
        const float d1 = (lod1Dist > 0.1f) ? lod1Dist : 10.0f;
        const float d2 = (lod2Dist > d1) ? lod2Dist : (d1 + 10.0f);
        m_lod1DistSq = d1 * d1;
        m_lod2DistSq = d2 * d2;
    }

    [[nodiscard]] uint32_t SelectLevel(float distanceSq) const noexcept {
        if (distanceSq < m_lod1DistSq) return 0u;
        if (distanceSq < m_lod2DistSq) return 1u;
        return 2u;
    }

    [[nodiscard]] const StaticMesh* GetMesh(uint32_t level) const noexcept {
        return m_meshes[(level < MAX_LEVELS) ? level : 2u];
    }

    [[nodiscard]] float GetLOD1DistanceSq() const noexcept { return m_lod1DistSq; }
    [[nodiscard]] float GetLOD2DistanceSq() const noexcept { return m_lod2DistSq; }

private:
    const StaticMesh* m_meshes[MAX_LEVELS]{nullptr, nullptr, nullptr};
    float m_lod1DistSq{324.0f};  // 18m^2
    float m_lod2DistSq{1296.0f}; // 36m^2
};

} // namespace WiiU
