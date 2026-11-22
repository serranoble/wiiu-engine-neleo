#include "Core/Spatial/SpatialBVH.hpp"
#include <cmath>

namespace WiiU {

void SpatialBVH::Reserve(size_t maxPrimitives) {
    if (maxPrimitives == 0) return;
    m_prims.reserve(maxPrimitives);
    m_primIndices.reserve(maxPrimitives);
    m_nodes.reserve(maxPrimitives * 2);
}

void SpatialBVH::Build(const BVHPrimitive* primitives, size_t count) {
    m_nodes.clear();
    m_primIndices.clear();
    m_prims.clear();
    m_primCount = count;

    if (!primitives || count == 0) {
        return;
    }

    m_prims.assign(primitives, primitives + count);
    m_primIndices.resize(count);
    for (size_t i = 0; i < count; ++i) {
        m_primIndices[i] = static_cast<uint32_t>(i);
    }

    // Pre-asignar peor caso (2N - 1) nodos para evitar realocaciones durante Subdivide
    m_nodes.reserve(count * 2);
    m_nodes.emplace_back(); // Nodo raíz (índice 0)

    UpdateNodeBounds(0, 0, static_cast<uint32_t>(count));
    Subdivide(0, 0, static_cast<uint32_t>(count));
}

void SpatialBVH::UpdateNodeBounds(uint32_t nodeIdx, uint32_t first, uint32_t count) noexcept {
    BVHNode& node = m_nodes[nodeIdx];
    node.bounds.Reset();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t pIdx = m_primIndices[first + i];
        node.bounds.Expand(m_prims[pIdx].bounds);
    }
}

bool SpatialBVH::FindBestSAHSplit(uint32_t nodeIdx, uint32_t first, uint32_t count,
                                  int& outAxis, float& outSplitPos, float& outCost) const noexcept {
    const float parentArea = m_nodes[nodeIdx].bounds.SurfaceArea();
    outAxis = -1;
    outSplitPos = 0.0f;
    outCost = 1e30f;

    constexpr int BINS = 8;
    for (int axis = 0; axis < 3; ++axis) {
        float cMin = 1e30f;
        float cMax = -1e30f;
        for (uint32_t i = 0; i < count; ++i) {
            const Point3D& c = m_prims[m_primIndices[first + i]].centroid;
            const float val = (axis == 0) ? c.x : ((axis == 1) ? c.y : c.z);
            cMin = std::min(cMin, val);
            cMax = std::max(cMax, val);
        }
        if (cMax - cMin < 1e-5f) continue;

        struct Bin {
            SpatialAABB bounds;
            uint32_t count{0};
        };
        Bin bins[BINS];
        const float scale = static_cast<float>(BINS) / (cMax - cMin);

        for (uint32_t i = 0; i < count; ++i) {
            const BVHPrimitive& prim = m_prims[m_primIndices[first + i]];
            const float val = (axis == 0) ? prim.centroid.x : ((axis == 1) ? prim.centroid.y : prim.centroid.z);
            const int binIdx = std::clamp(static_cast<int>((val - cMin) * scale), 0, BINS - 1);
            bins[binIdx].count++;
            bins[binIdx].bounds.Expand(prim.bounds);
        }

        float leftArea[BINS - 1];
        float rightArea[BINS - 1];
        uint32_t leftCount[BINS - 1];
        uint32_t rightCount[BINS - 1];

        SpatialAABB leftBox, rightBox;
        uint32_t leftSum = 0, rightSum = 0;

        for (int i = 0; i < BINS - 1; ++i) {
            leftSum += bins[i].count;
            leftCount[i] = leftSum;
            if (bins[i].count > 0) leftBox.Expand(bins[i].bounds);
            leftArea[i] = (leftSum > 0) ? leftBox.SurfaceArea() : 0.0f;

            const int rIdx = BINS - 1 - i;
            rightSum += bins[rIdx].count;
            rightCount[rIdx - 1] = rightSum;
            if (bins[rIdx].count > 0) rightBox.Expand(bins[rIdx].bounds);
            rightArea[rIdx - 1] = (rightSum > 0) ? rightBox.SurfaceArea() : 0.0f;
        }

        const float binStep = (cMax - cMin) / static_cast<float>(BINS);
        for (int i = 0; i < BINS - 1; ++i) {
            if (leftCount[i] == 0 || rightCount[i] == 0) continue;
            const float cost = 1.0f + (leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i]) /
                                           std::max(parentArea, 1e-5f);
            if (cost < outCost) {
                outCost = cost;
                outAxis = axis;
                outSplitPos = cMin + static_cast<float>(i + 1) * binStep;
            }
        }
    }

    return outAxis != -1;
}

uint32_t SpatialBVH::PartitionPrimitives(uint32_t first, uint32_t count, int axis, float splitPos) noexcept {
    if (axis < 0) {
        return count / 2;
    }

    uint32_t i = first;
    uint32_t j = first + count - 1;
    while (i <= j) {
        const Point3D& c = m_prims[m_primIndices[i]].centroid;
        const float val = (axis == 0) ? c.x : ((axis == 1) ? c.y : c.z);
        if (val < splitPos) {
            ++i;
        } else {
            std::swap(m_primIndices[i], m_primIndices[j]);
            if (j == 0) break;
            --j;
        }
    }

    const uint32_t leftCount = i - first;
    return (leftCount == 0 || leftCount == count) ? (count / 2) : leftCount;
}

void SpatialBVH::Subdivide(uint32_t nodeIdx, uint32_t first, uint32_t count) {
    if (count <= 2) {
        m_nodes[nodeIdx].leftFirst = first;
        m_nodes[nodeIdx].primCount = count;
        return;
    }

    int bestAxis = -1;
    float bestSplitPos = 0.0f;
    float bestCost = 1e30f;
    const bool foundSplit = FindBestSAHSplit(nodeIdx, first, count, bestAxis, bestSplitPos, bestCost);

    const float leafCost = static_cast<float>(count);
    if (count <= 4 && (!foundSplit || bestCost >= leafCost)) {
        m_nodes[nodeIdx].leftFirst = first;
        m_nodes[nodeIdx].primCount = count;
        return;
    }

    const uint32_t leftCount = PartitionPrimitives(first, count, bestAxis, bestSplitPos);

    // Asignar ambos hijos de forma contigua (leftIdx, leftIdx + 1)
    uint32_t leftChildIdx = static_cast<uint32_t>(m_nodes.size());
    m_nodes.emplace_back();
    m_nodes.emplace_back();

    m_nodes[nodeIdx].leftFirst = leftChildIdx;
    m_nodes[nodeIdx].primCount = 0; // Nodo interno

    UpdateNodeBounds(leftChildIdx, first, leftCount);
    UpdateNodeBounds(leftChildIdx + 1, first + leftCount, count - leftCount);

    Subdivide(leftChildIdx, first, leftCount);
    Subdivide(leftChildIdx + 1, first + leftCount, count - leftCount);
}

void SpatialBVH::QueryFrustum(const FrustumPlane planes[6], uint32_t* outUserIds, size_t maxOut, size_t& outCount) const noexcept {
    outCount = 0;
    if (!outUserIds || maxOut == 0) return;

    Traverse(
        [planes](const SpatialAABB& box) noexcept { return box.IntersectsFrustum(planes); },
        [outUserIds, maxOut, &outCount](const BVHPrimitive& prim) noexcept {
            if (outCount >= maxOut) return false;
            outUserIds[outCount++] = prim.userId;
            return outCount < maxOut;
        }
    );
}

void SpatialBVH::QueryAABB(const SpatialAABB& queryBox, uint32_t* outUserIds, size_t maxOut, size_t& outCount) const noexcept {
    outCount = 0;
    if (!outUserIds || maxOut == 0) return;

    Traverse(
        [&queryBox](const SpatialAABB& box) noexcept { return box.IntersectsAABB(queryBox); },
        [&queryBox, outUserIds, maxOut, &outCount](const BVHPrimitive& prim) noexcept {
            if (outCount >= maxOut) return false;
            if (prim.bounds.IntersectsAABB(queryBox)) {
                outUserIds[outCount++] = prim.userId;
            }
            return outCount < maxOut;
        }
    );
}

void SpatialBVH::RayCast(const Point3D& origin, const Point3D& dir, float maxDist, BVHRayHit& outHit) const noexcept {
    outHit = BVHRayHit{};
    outHit.distance = maxDist;
    if (IsEmpty()) return;

    const Point3D invDir{
        (std::abs(dir.x) > 1e-8f) ? (1.0f / dir.x) : std::copysign(1e8f, dir.x),
        (std::abs(dir.y) > 1e-8f) ? (1.0f / dir.y) : std::copysign(1e8f, dir.y),
        (std::abs(dir.z) > 1e-8f) ? (1.0f / dir.z) : std::copysign(1e8f, dir.z)
    };

    uint32_t stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    while (stackPtr > 0) {
        const uint32_t nodeIdx = stack[--stackPtr];
        const BVHNode& node = m_nodes[nodeIdx];
        outHit.nodesVisited++;

        float nodeT = 0.0f;
        if (!node.bounds.IntersectsRay(origin, invDir, outHit.distance, nodeT)) {
            continue;
        }

        if (node.primCount > 0) {
            for (uint32_t i = 0; i < node.primCount; ++i) {
                uint32_t pIdx = m_primIndices[node.leftFirst + i];
                float primT = 0.0f;
                if (m_prims[pIdx].bounds.IntersectsRay(origin, invDir, outHit.distance, primT)) {
                    outHit.hit = true;
                    outHit.distance = primT;
                    outHit.userId = m_prims[pIdx].userId;
                }
            }
        } else {
            // Ordenar hijos por proximidad para acelerar el descarte temprano (early-out)
            uint32_t leftIdx = node.leftFirst;
            uint32_t rightIdx = node.leftFirst + 1;
            float tLeft = 1e30f, tRight = 1e30f;
            bool hitLeft = m_nodes[leftIdx].bounds.IntersectsRay(origin, invDir, outHit.distance, tLeft);
            bool hitRight = m_nodes[rightIdx].bounds.IntersectsRay(origin, invDir, outHit.distance, tRight);

            if (hitLeft && hitRight) {
                if (tLeft < tRight) {
                    if (stackPtr + 2 <= 64) {
                        stack[stackPtr++] = rightIdx;
                        stack[stackPtr++] = leftIdx;
                    }
                } else {
                    if (stackPtr + 2 <= 64) {
                        stack[stackPtr++] = leftIdx;
                        stack[stackPtr++] = rightIdx;
                    }
                }
            } else if (hitLeft && stackPtr < 64) {
                stack[stackPtr++] = leftIdx;
            } else if (hitRight && stackPtr < 64) {
                stack[stackPtr++] = rightIdx;
            }
        }
    }
}

void SpatialBVH::PackSubtreePreOrder(uint32_t treeNodeIdx, float* outVec4Buffer,
                                     size_t maxNodes, size_t& outCursor) const noexcept {
    if (treeNodeIdx >= m_nodes.size() || outCursor >= maxNodes) {
        return;
    }

    const size_t myOutIdx = outCursor++;
    const BVHNode& n = m_nodes[treeNodeIdx];
    float* dst = outVec4Buffer + (myOutIdx * 8);

    // vec4[0].xyz = min, vec4[1].xyz = max, vec4[1].w = primCount
    dst[0] = n.bounds.min.x;
    dst[1] = n.bounds.min.y;
    dst[2] = n.bounds.min.z;
    dst[4] = n.bounds.max.x;
    dst[5] = n.bounds.max.y;
    dst[6] = n.bounds.max.z;
    dst[7] = static_cast<float>(n.primCount);

    if (n.primCount > 0) {
        dst[3] = static_cast<float>(n.leftFirst);
    } else {
        PackSubtreePreOrder(n.leftFirst, outVec4Buffer, maxNodes, outCursor);
        PackSubtreePreOrder(n.leftFirst + 1, outVec4Buffer, maxNodes, outCursor);
        dst[3] = static_cast<float>(outCursor);
    }
}

size_t SpatialBVH::PackNodesForGPU(float* outVec4Buffer, size_t maxNodes) const noexcept {
    if (!outVec4Buffer || maxNodes == 0 || m_nodes.empty()) return 0;

    size_t outCursor = 0;
    PackSubtreePreOrder(0, outVec4Buffer, maxNodes, outCursor);
    return outCursor;
}

} // namespace WiiU
