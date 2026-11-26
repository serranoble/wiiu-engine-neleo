#include "Core/Physics/SpatialHashGrid.hpp"
#include <algorithm>
#include <cmath>

namespace Neleo {

SpatialHashGrid::SpatialHashGrid(float cellSize) noexcept {
    SetCellSize(cellSize);
    m_bucketHeads.fill(0xFFFFu);
    m_bucketStamp.fill(0u);
}

void SpatialHashGrid::SetCellSize(float cellSize) noexcept {
    m_cellSize = (cellSize > 0.25f) ? cellSize : 0.25f;
    m_invCellSize = 1.0f / m_cellSize;
}

int32_t SpatialHashGrid::ToCellCoord(float v) const noexcept {
    const float scaled = std::clamp(v * m_invCellSize, -1.0e6f, 1.0e6f);
    return static_cast<int32_t>(std::floor(scaled));
}

void SpatialHashGrid::Clear() noexcept {
    m_nodeCount = 0;
    m_oversizedCount = 0;
    ++m_gridGeneration;
    if (m_gridGeneration == 0u) {
        m_bucketStamp.fill(0u);
        m_gridGeneration = 1u;
    }
}

void SpatialHashGrid::Insert(uint32_t itemId, const Point3D& minPt, const Point3D& maxPt) noexcept {
    const int32_t minX = ToCellCoord(minPt.x);
    const int32_t minY = ToCellCoord(minPt.y);
    const int32_t minZ = ToCellCoord(minPt.z);
    const int32_t maxX = ToCellCoord(maxPt.x);
    const int32_t maxY = ToCellCoord(maxPt.y);
    const int32_t maxZ = ToCellCoord(maxPt.z);

    const int32_t spanX = maxX - minX + 1;
    const int32_t spanY = maxY - minY + 1;
    const int32_t spanZ = maxZ - minZ + 1;

    if (spanX > MAX_SPAN_PER_AXIS || spanY > MAX_SPAN_PER_AXIS || spanZ > MAX_SPAN_PER_AXIS) {
        if (m_oversizedCount < MAX_OVERSIZED) {
            m_oversized[m_oversizedCount++] = itemId;
        }
        return;
    }

    for (int32_t z = minZ; z <= maxZ; ++z) {
        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t x = minX; x <= maxX; ++x) {
                if (m_nodeCount >= MAX_CELL_NODES) {
                    if (m_oversizedCount < MAX_OVERSIZED) {
                        m_oversized[m_oversizedCount++] = itemId;
                    }
                    return;
                }
                const uint32_t bucket = HashCell(x, y, z);
                uint16_t head = (m_bucketStamp[bucket] == m_gridGeneration)
                                ? m_bucketHeads[bucket]
                                : static_cast<uint16_t>(0xFFFFu);

                const uint16_t nodeIdx = static_cast<uint16_t>(m_nodeCount++);
                m_nodes[nodeIdx].itemId = itemId;
                m_nodes[nodeIdx].next = head;
                m_bucketHeads[bucket] = nodeIdx;
                m_bucketStamp[bucket] = m_gridGeneration;
            }
        }
    }
}

size_t SpatialHashGrid::QueryAABB(const Point3D& minPt, const Point3D& maxPt,
                                  uint32_t* outIds, size_t maxOut) const noexcept {
    if (!outIds || maxOut == 0) return 0;

    // Bitset local en pila de 32 bytes (1 linea de cache L1D Espresso): 100% reentrante y thread-safe
    static_assert(MAX_TRACKED_IDS % 64u == 0u, "MAX_TRACKED_IDS must be a multiple of 64");
    std::array<uint64_t, MAX_TRACKED_IDS / 64u> seenBits{};

    size_t outCount = 0;
    auto tryEmit = [&](uint32_t id) {
        if (outCount >= maxOut) return;
        if (id < MAX_TRACKED_IDS) {
            const size_t word = static_cast<size_t>(id >> 6u);
            const uint64_t mask = uint64_t(1) << (id & 63u);
            if ((seenBits[word] & mask) != 0u) return;
            seenBits[word] |= mask;
        } else {
            for (size_t k = 0; k < outCount; ++k) {
                if (outIds[k] == id) return;
            }
        }
        outIds[outCount++] = id;
    };

    for (size_t i = 0; i < m_oversizedCount; ++i) {
        tryEmit(m_oversized[i]);
    }

    const int32_t minX = ToCellCoord(minPt.x);
    const int32_t minY = ToCellCoord(minPt.y);
    const int32_t minZ = ToCellCoord(minPt.z);
    const int32_t maxX = ToCellCoord(maxPt.x);
    const int32_t maxY = ToCellCoord(maxPt.y);
    const int32_t maxZ = ToCellCoord(maxPt.z);

    // Si la consulta abarca demasiadas celdas, emitir todos los nodos registrados de forma segura
    if ((maxX - minX) > 6 || (maxY - minY) > 6 || (maxZ - minZ) > 6) {
        for (size_t n = 0; n < m_nodeCount; ++n) {
            tryEmit(m_nodes[n].itemId);
        }
        return outCount;
    }

    for (int32_t z = minZ; z <= maxZ; ++z) {
        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t x = minX; x <= maxX; ++x) {
                const uint32_t bucket = HashCell(x, y, z);
                if (m_bucketStamp[bucket] != m_gridGeneration) continue;

                uint16_t idx = m_bucketHeads[bucket];
                while (idx != 0xFFFFu && outCount < maxOut) {
                    tryEmit(m_nodes[idx].itemId);
                    idx = m_nodes[idx].next;
                }
            }
        }
    }

    return outCount;
}

} // namespace Neleo
