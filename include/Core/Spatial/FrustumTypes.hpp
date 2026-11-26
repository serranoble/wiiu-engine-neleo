#pragma once

#include "Graphics/Math3D.hpp"
#include <cmath>

namespace Neleo {

/**
 * @brief [OPT-256] Ecuación analítica de plano 3D (Ax + By + Cz + D = 0).
 */
struct FrustumPlane {
    Point3D normal{0.0f, 0.0f, 0.0f};
    float distance{0.0f};

    void Normalize() noexcept {
        const float lenSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        if (lenSq > 1e-12f) {
            const float inv = 1.0f / std::sqrt(lenSq);
            normal.x *= inv;
            normal.y *= inv;
            normal.z *= inv;
            distance *= inv;
        }
    }

    [[nodiscard]] float SignedDistance(const Point3D& point) const noexcept {
        return normal.x * point.x + normal.y * point.y + normal.z * point.z + distance;
    }
};

} // namespace Neleo
