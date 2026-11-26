#pragma once

#include "Graphics/Math3D.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-275] Rejilla Espacial Hash 3D (Spatial Hash Grid Broadphase) Zero-Alloc.
 *
 * Estructura Data-Oriented en memoria contigua (sin asignaciones de heap) para consultas
 * espaciales O(1) amortizadas en CollisionSystem, RigidBodyPhysicsSystem y raycasts acusticos.
 * - Limpieza por frame en O(1) mediante sello de generacion (generation stamp).
 * - Los volumenes gigantes (ej. suelos extensos) se derivan a una lista auxiliar para
 *   evitar saturar las ranuras de celdas y garantizar cero falsos negativos.
 */
class SpatialHashGrid {
public:
    static constexpr size_t BUCKET_COUNT       = 256;
    static constexpr size_t BUCKET_MASK        = BUCKET_COUNT - 1;
    static constexpr size_t MAX_CELL_NODES     = 1024;
    static constexpr size_t MAX_OVERSIZED      = 24;
    static constexpr size_t MAX_TRACKED_IDS    = 256;
    static constexpr int32_t MAX_SPAN_PER_AXIS = 4;

    explicit SpatialHashGrid(float cellSize = 4.0f) noexcept;

    void SetCellSize(float cellSize) noexcept;
    [[nodiscard]] float GetCellSize() const noexcept { return m_cellSize; }

    /**
     * @brief Reinicia la rejilla en O(1) avanzando la generacion (0 memset en hot path).
     */
    void Clear() noexcept;

    /**
     * @brief Inserta un identificador de objeto (ej. indice de proxy) acotado por su AABB mundial.
     */
    void Insert(uint32_t itemId, const Point3D& minPt, const Point3D& maxPt) noexcept;

    /**
     * @brief Inserta un objeto acotado por esfera mundial.
     */
    void InsertSphere(uint32_t itemId, const Point3D& center, float radius) noexcept {
        const Point3D ext{radius, radius, radius};
        Insert(itemId, center - ext, center + ext);
    }

    /**
     * @brief Consulta candidatos unicos solapados con el AABB [minPt, maxPt].
     * @return Cantidad de identificadores escritos en outIds (sin duplicados).
     */
    [[nodiscard]] size_t QueryAABB(const Point3D& minPt, const Point3D& maxPt,
                                   uint32_t* outIds, size_t maxOut) const noexcept;

    [[nodiscard]] size_t QuerySphere(const Point3D& center, float radius,
                                     uint32_t* outIds, size_t maxOut) const noexcept {
        const Point3D ext{radius, radius, radius};
        return QueryAABB(center - ext, center + ext, outIds, maxOut);
    }

    [[nodiscard]] size_t GetNodeCount() const noexcept { return m_nodeCount; }
    [[nodiscard]] size_t GetOversizedCount() const noexcept { return m_oversizedCount; }

private:
    struct CellNode {
        uint32_t itemId{0};
        uint16_t next{0xFFFFu};
    };

    [[nodiscard]] inline int32_t ToCellCoord(float v) const noexcept;
    [[nodiscard]] static inline uint32_t HashCell(int32_t cx, int32_t cy, int32_t cz) noexcept {
        // Primos grandes de Teschner et al. ("Optimized Spatial Hashing for Collision Detection")
        const uint32_t h = (static_cast<uint32_t>(cx) * 73856093u) ^
                           (static_cast<uint32_t>(cy) * 19349663u) ^
                           (static_cast<uint32_t>(cz) * 83492791u);
        return h & static_cast<uint32_t>(BUCKET_MASK);
    }

    float m_cellSize{4.0f};
    float m_invCellSize{0.25f};

    std::array<uint16_t, BUCKET_COUNT> m_bucketHeads{};
    std::array<uint32_t, BUCKET_COUNT> m_bucketStamp{};
    std::array<CellNode, MAX_CELL_NODES> m_nodes{};
    std::array<uint32_t, MAX_OVERSIZED> m_oversized{};

    uint32_t m_gridGeneration{1};
    size_t m_nodeCount{0};
    size_t m_oversizedCount{0};
};

} // namespace Neleo
