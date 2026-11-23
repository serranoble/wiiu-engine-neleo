#pragma once

#include "Graphics/Math3D.hpp"
#include <array>
#include <cstdint>

namespace WiiU {

/**
 * @brief Constructor Zero-Alloc de BVH Stackless Pre-Order (hasta 24 nodos / 12 esferas)
 *        compatible 1:1 con `uBvhNodes[48]` de `raytracing_bvh.frag` (RTR4 Cap. 22).
 *
 * Layout por nodo `i`:
 * - `packedNodes[i * 8 + 0..2]` = AABB min (x, y, z)
 * - `packedNodes[i * 8 + 3]`    = payload: `firstPrim` si hoja (`primCount > 0`), o `skipIndex` si interno (`primCount == 0`)
 * - `packedNodes[i * 8 + 4..6]` = AABB max (x, y, z)
 * - `packedNodes[i * 8 + 7]`    = `primCount` (`> 0` hoja, `0` nodo interno)
 */
class StacklessBVHBuilder {
public:
    static constexpr uint32_t MAX_PRIMITIVES = 12u;
    static constexpr uint32_t MAX_NODES      = 24u;
    static constexpr uint32_t MAX_LEAF_PRIMS = 2u;

    struct SpherePrim {
        Point3D center{0.0f, 0.0f, 0.0f};
        float radius{0.5f};
        float color[4]{1.0f, 1.0f, 1.0f, 0.25f}; // rgb = albedo, a = reflectivity
    };

    struct BuildResult {
        std::array<float, MAX_PRIMITIVES * 4u> packedSpheres{};
        std::array<float, MAX_PRIMITIVES * 4u> packedColors{};
        std::array<float, MAX_NODES * 8u>      packedNodes{};
        uint32_t primCount{0u};
        uint32_t nodeCount{0u};
        Point3D rootMin{0.0f, 0.0f, 0.0f};
        Point3D rootMax{0.0f, 0.0f, 0.0f};
    };

    /**
     * @brief Construye el BVH Stackless Pre-Order en stack (0 asignaciones dinámicas).
     */
    static void Build(const SpherePrim* inputPrims, uint32_t count, BuildResult& out) noexcept {
        out.primCount = 0u;
        out.nodeCount = 0u;
        out.rootMin = Point3D{0.0f, 0.0f, 0.0f};
        out.rootMax = Point3D{0.0f, 0.0f, 0.0f};
        if (!inputPrims || count == 0u) return;
        out.primCount = (count > MAX_PRIMITIVES) ? MAX_PRIMITIVES : count;

        std::array<SpherePrim, MAX_PRIMITIVES> work{};
        for (uint32_t i = 0u; i < out.primCount; ++i) {
            work[i] = inputPrims[i];
        }

        BuildRecursive(work.data(), 0u, out.primCount, out);

        for (uint32_t i = 0u; i < out.primCount; ++i) {
            out.packedSpheres[i * 4u + 0u] = work[i].center.x;
            out.packedSpheres[i * 4u + 1u] = work[i].center.y;
            out.packedSpheres[i * 4u + 2u] = work[i].center.z;
            out.packedSpheres[i * 4u + 3u] = work[i].radius;

            out.packedColors[i * 4u + 0u]  = work[i].color[0];
            out.packedColors[i * 4u + 1u]  = work[i].color[1];
            out.packedColors[i * 4u + 2u]  = work[i].color[2];
            out.packedColors[i * 4u + 3u]  = work[i].color[3];
        }

        if (out.nodeCount > 0u) {
            out.rootMin = Point3D{out.packedNodes[0], out.packedNodes[1], out.packedNodes[2]};
            out.rootMax = Point3D{out.packedNodes[4], out.packedNodes[5], out.packedNodes[6]};
        }
    }

private:
    static void ComputeBounds(const SpherePrim* prims, uint32_t start, uint32_t count,
                              Point3D& outMin, Point3D& outMax) noexcept {
        outMin = Point3D{
            prims[start].center.x - prims[start].radius,
            prims[start].center.y - prims[start].radius,
            prims[start].center.z - prims[start].radius
        };
        outMax = Point3D{
            prims[start].center.x + prims[start].radius,
            prims[start].center.y + prims[start].radius,
            prims[start].center.z + prims[start].radius
        };
        for (uint32_t i = start + 1u; i < start + count; ++i) {
            const SpherePrim& s = prims[i];
            if (s.center.x - s.radius < outMin.x) outMin.x = s.center.x - s.radius;
            if (s.center.y - s.radius < outMin.y) outMin.y = s.center.y - s.radius;
            if (s.center.z - s.radius < outMin.z) outMin.z = s.center.z - s.radius;
            if (s.center.x + s.radius > outMax.x) outMax.x = s.center.x + s.radius;
            if (s.center.y + s.radius > outMax.y) outMax.y = s.center.y + s.radius;
            if (s.center.z + s.radius > outMax.z) outMax.z = s.center.z + s.radius;
        }
    }

    static void BuildRecursive(SpherePrim* prims, uint32_t start, uint32_t count, BuildResult& out) noexcept {
        if (out.nodeCount >= MAX_NODES || count == 0u) return;

        const uint32_t myIdx = out.nodeCount++;
        Point3D bMin{}, bMax{};
        ComputeBounds(prims, start, count, bMin, bMax);

        out.packedNodes[myIdx * 8u + 0u] = bMin.x;
        out.packedNodes[myIdx * 8u + 1u] = bMin.y;
        out.packedNodes[myIdx * 8u + 2u] = bMin.z;
        out.packedNodes[myIdx * 8u + 4u] = bMax.x;
        out.packedNodes[myIdx * 8u + 5u] = bMax.y;
        out.packedNodes[myIdx * 8u + 6u] = bMax.z;

        // Hoja si <= MAX_LEAF_PRIMS o si no quedan suficientes nodos para dividir (se necesitan >= 2 nodos hijos)
        if (count <= MAX_LEAF_PRIMS || (out.nodeCount + 2u > MAX_NODES)) {
            out.packedNodes[myIdx * 8u + 3u] = static_cast<float>(start);
            out.packedNodes[myIdx * 8u + 7u] = static_cast<float>(count);
            return;
        }

        // Eje de mayor extensión de centroides
        const float ex = bMax.x - bMin.x;
        const float ey = bMax.y - bMin.y;
        const float ez = bMax.z - bMin.z;
        const uint32_t axis = (ex >= ey && ex >= ez) ? 0u : ((ey >= ez) ? 1u : 2u);

        auto coord = [axis](const SpherePrim& s) noexcept -> float {
            if (axis == 0u) return s.center.x;
            if (axis == 1u) return s.center.y;
            return s.center.z;
        };

        // Insertion sort O(N^2) in-place (N <= 12 -> <= 66 comparaciones, 0 allocs)
        for (uint32_t i = start + 1u; i < start + count; ++i) {
            const SpherePrim key = prims[i];
            const float keyVal = coord(key);
            uint32_t j = i;
            while (j > start && coord(prims[j - 1u]) > keyVal) {
                prims[j] = prims[j - 1u];
                --j;
            }
            prims[j] = key;
        }

        const uint32_t leftCount = count / 2u;
        const uint32_t rightCount = count - leftCount;

        // Nodo interno (primCount = 0); skipIndex se rellena al concluir ambos subárboles
        out.packedNodes[myIdx * 8u + 7u] = 0.0f;
        BuildRecursive(prims, start, leftCount, out);
        BuildRecursive(prims, start + leftCount, rightCount, out);
        out.packedNodes[myIdx * 8u + 3u] = static_cast<float>(out.nodeCount);
    }
};

} // namespace WiiU
