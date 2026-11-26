#pragma once

#include "Graphics/Math3D.hpp"
#include "Core/Spatial/FrustumTypes.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>

namespace Neleo {

/**
 * @brief [Jason Gregory Cap. 11.1: "Spatial Partitioning - Axis-Aligned Bounding Box"]
 * Caja delimitadora alineada a los ejes (AABB) para particionamiento espacial,
 * Frustum Culling jerárquico y aceleración de trazado de rayos.
 */
struct SpatialAABB {
    Point3D min{ 1e30f,  1e30f,  1e30f};
    Point3D max{-1e30f, -1e30f, -1e30f};

    constexpr SpatialAABB() noexcept = default;
    constexpr SpatialAABB(const Point3D& mn, const Point3D& mx) noexcept : min(mn), max(mx) {}

    void Reset() noexcept {
        min = { 1e30f,  1e30f,  1e30f};
        max = {-1e30f, -1e30f, -1e30f};
    }

    void Expand(const Point3D& p) noexcept {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    void Expand(const SpatialAABB& other) noexcept {
        min.x = std::min(min.x, other.min.x);
        min.y = std::min(min.y, other.min.y);
        min.z = std::min(min.z, other.min.z);
        max.x = std::max(max.x, other.max.x);
        max.y = std::max(max.y, other.max.y);
        max.z = std::max(max.z, other.max.z);
    }

    [[nodiscard]] Point3D Center() const noexcept {
        return Point3D{
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f
        };
    }

    [[nodiscard]] Point3D Extent() const noexcept {
        return Point3D{
            max.x - min.x,
            max.y - min.y,
            max.z - min.z
        };
    }

    [[nodiscard]] float SurfaceArea() const noexcept {
        Point3D e = Extent();
        if (e.x < 0.0f || e.y < 0.0f || e.z < 0.0f) return 0.0f;
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }

    [[nodiscard]] bool IntersectsAABB(const SpatialAABB& o) const noexcept {
        return (min.x <= o.max.x && max.x >= o.min.x) &&
               (min.y <= o.max.y && max.y >= o.min.y) &&
               (min.z <= o.max.z && max.z >= o.min.z);
    }

    /**
     * @brief Prueba de intersección Ray-AABB mediante el método de losas (Kay & Kajiya / Amy Williams).
     */
    [[nodiscard]] bool IntersectsRay(const Point3D& origin, const Point3D& invDir, float maxDist, float& outTNear) const noexcept {
        float t1 = (min.x - origin.x) * invDir.x;
        float t2 = (max.x - origin.x) * invDir.x;
        float tmin = std::min(t1, t2);
        float tmax = std::max(t1, t2);

        t1 = (min.y - origin.y) * invDir.y;
        t2 = (max.y - origin.y) * invDir.y;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));

        t1 = (min.z - origin.z) * invDir.z;
        t2 = (max.z - origin.z) * invDir.z;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));

        if (tmax >= std::max(0.0f, tmin) && tmin < maxDist) {
            outTNear = std::max(0.0f, tmin);
            return true;
        }
        return false;
    }

    /**
     * @brief Prueba de intersección AABB-Frustum (p-vertex test contra los 6 planos).
     */
    [[nodiscard]] bool IntersectsFrustum(const FrustumPlane planes[6]) const noexcept {
        for (int i = 0; i < 6; ++i) {
            const Point3D& n = planes[i].normal;
            Point3D pVertex{
                (n.x >= 0.0f) ? max.x : min.x,
                (n.y >= 0.0f) ? max.y : min.y,
                (n.z >= 0.0f) ? max.z : min.z
            };
            if (planes[i].SignedDistance(pVertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

/**
 * @brief Primitiva de entrada para construcción del árbol BVH.
 */
struct BVHPrimitive {
    SpatialAABB bounds;
    Point3D centroid{0.0f, 0.0f, 0.0f};
    uint32_t userId{0};
};

/**
 * @brief Nodo plano de BVH alineado estrictamente a una línea de caché L1D de Espresso (32 bytes).
 * Si primCount > 0: nodo hoja (leftFirst es índice inicial en el arreglo de índices).
 * Si primCount == 0: nodo interno (hijo izquierdo = leftFirst, hijo derecho = leftFirst + 1).
 */
struct alignas(32) BVHNode {
    SpatialAABB bounds;     // 24 bytes
    uint32_t leftFirst{0};  // 4 bytes
    uint32_t primCount{0};  // 4 bytes
};
static_assert(sizeof(BVHNode) == 32, "BVHNode debe caber exactamente en una linea de cache L1D de 32B");

/**
 * @brief Resultado de consulta RayCast sobre el BVH.
 */
struct BVHRayHit {
    bool hit{false};
    float distance{1e30f};
    uint32_t userId{0};
    uint32_t nodesVisited{0};
};

/**
 * @brief [Jason Gregory Cap. 11.1: "Bounding Volume Hierarchy (BVH)"]
 * Árbol de particionamiento espacial plano y determinista.
 * - Cero punteros por nodo (indexación por enteros uint32_t).
 * - Recorrido iterativo con pila local en stack (0 alocaciones en consultas).
 * - Soporta reconstrucción rápida por fotograma para objetos dinámicos (reutiliza memoria).
 */
class SpatialBVH {
public:
    SpatialBVH() = default;

    /**
     * @brief Reserva capacidad inicial para evitar realocaciones durante el ciclo de juego.
     */
    void Reserve(size_t maxPrimitives);

    /**
     * @brief Construye o reconstruye el árbol BVH usando división por Surface Area Heuristic (SAH).
     */
    void Build(const BVHPrimitive* primitives, size_t count);

    /**
     * @brief Consulta iterativa contra los 6 planos del Frustum en O(log N).
     */
    void QueryFrustum(const FrustumPlane planes[6], uint32_t* outUserIds, size_t maxOut, size_t& outCount) const noexcept;

    /**
     * @brief Consulta iterativa por rango AABB en O(log N).
     */
    void QueryAABB(const SpatialAABB& queryBox, uint32_t* outUserIds, size_t maxOut, size_t& outCount) const noexcept;

    /**
     * @brief Trazado de rayo iterativo sobre los volúmenes del árbol BVH.
     */
    void RayCast(const Point3D& origin, const Point3D& dir, float maxDist, BVHRayHit& outHit) const noexcept;

    /**
     * @brief Recorrido genérico iterativo O(log N) sobre el BVH con pila estática (0 alocaciones).
     * @param nodeIntersects Predicado `bool(const SpatialAABB&)` para podar nodos internos.
     * @param onLeafPrimitive Callback `bool(const BVHPrimitive&)` por cada primitiva en hoja (retornar false detiene).
     */
    template <typename NodePred, typename LeafFn>
    void Traverse(NodePred&& nodeIntersects, LeafFn&& onLeafPrimitive) const noexcept {
        if (IsEmpty()) return;
        uint32_t stack[64];
        int stackPtr = 0;
        stack[stackPtr++] = 0;

        while (stackPtr > 0) {
            const uint32_t nodeIdx = stack[--stackPtr];
            const BVHNode& node = m_nodes[nodeIdx];
            if (!nodeIntersects(node.bounds)) {
                continue;
            }
            if (node.primCount > 0) {
                for (uint32_t i = 0; i < node.primCount; ++i) {
                    const uint32_t pIdx = m_primIndices[node.leftFirst + i];
                    if (!onLeafPrimitive(m_prims[pIdx])) {
                        return;
                    }
                }
            } else if (stackPtr + 2 <= 64) {
                stack[stackPtr++] = node.leftFirst;
                stack[stackPtr++] = node.leftFirst + 1;
            }
        }
    }

    /**
     * @brief Empaqueta los nodos BVH en orden Pre-Order Stackless para GPU AMD Latte (vec4).
     * Cada nodo ocupa 2 vec4 (8 floats):
     *   vec4[0] = (min.x, min.y, min.z, hoja ? float(leftFirst) : float(skipNodeIdx))
     *   vec4[1] = (max.x, max.y, max.z, float(primCount))
     * En orden Pre-Order, el hijo izquierdo de todo nodo interno reside en `nodeIdx + 1`
     * y al fallar el AABB el shader salta sin pila directa o secuencialmente a `skipNodeIdx`.
     * @return Número total de nodos empaquetados.
     */
    [[nodiscard]] size_t PackNodesForGPU(float* outVec4Buffer, size_t maxNodes) const noexcept;

    [[nodiscard]] const std::vector<BVHNode>& GetNodes() const noexcept { return m_nodes; }
    [[nodiscard]] const std::vector<uint32_t>& GetPrimIndices() const noexcept { return m_primIndices; }
    [[nodiscard]] size_t GetNodeCount() const noexcept { return m_nodes.size(); }
    [[nodiscard]] size_t GetPrimitiveCount() const noexcept { return m_primCount; }
    [[nodiscard]] bool IsEmpty() const noexcept { return m_nodes.empty() || m_primCount == 0; }

private:
    void Subdivide(uint32_t nodeIdx, uint32_t first, uint32_t count);
    [[nodiscard]] bool FindBestSAHSplit(uint32_t nodeIdx, uint32_t first, uint32_t count,
                                        int& outAxis, float& outSplitPos, float& outCost) const noexcept;
    [[nodiscard]] uint32_t PartitionPrimitives(uint32_t first, uint32_t count, int axis, float splitPos) noexcept;
    void UpdateNodeBounds(uint32_t nodeIdx, uint32_t first, uint32_t count) noexcept;
    void PackSubtreePreOrder(uint32_t treeNodeIdx, float* outVec4Buffer, size_t maxNodes, size_t& outCursor) const noexcept;

    std::vector<BVHNode> m_nodes;
    std::vector<BVHPrimitive> m_prims;
    std::vector<uint32_t> m_primIndices;
    size_t m_primCount{0};
};

} // namespace Neleo
