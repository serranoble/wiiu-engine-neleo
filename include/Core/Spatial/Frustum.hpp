#pragma once

#include "Core/Spatial/FrustumTypes.hpp"
#include "Graphics/Math3D.hpp"
#include <array>
#include <cmath>

namespace WiiU {

/**
 * @brief Volumen de visión 3D (6 planos normalizados) con extracción Gribb-Hartmann
 *        y pruebas analíticas Esfera/AABB Zero-Alloc para Frustum Culling (RTR4 Cap. 19).
 */
class ViewFrustum {
public:
    enum PlaneIndex : uint32_t {
        Left   = 0u,
        Right  = 1u,
        Bottom = 2u,
        Top    = 3u,
        Near   = 4u,
        Far    = 5u
    };

    ViewFrustum() noexcept = default;

    /**
     * @brief Extrae los 6 planos normalizados desde una matriz View-Projection column-major (M = Proj * View).
     */
    void ExtractFromViewProj(const Matrix4x4& vp) noexcept {
        // Filas de la matriz column-major vp.m[col * 4 + row]
        const float r0x = vp.m[0],  r0y = vp.m[4],  r0z = vp.m[8],  r0w = vp.m[12];
        const float r1x = vp.m[1],  r1y = vp.m[5],  r1z = vp.m[9],  r1w = vp.m[13];
        const float r2x = vp.m[2],  r2y = vp.m[6],  r2z = vp.m[10], r2w = vp.m[14];
        const float r3x = vp.m[3],  r3y = vp.m[7],  r3z = vp.m[11], r3w = vp.m[15];

        // Left: row3 + row0
        m_planes[Left]   = {{r3x + r0x, r3y + r0y, r3z + r0z}, r3w + r0w};
        // Right: row3 - row0
        m_planes[Right]  = {{r3x - r0x, r3y - r0y, r3z - r0z}, r3w - r0w};
        // Bottom: row3 + row1
        m_planes[Bottom] = {{r3x + r1x, r3y + r1y, r3z + r1z}, r3w + r1w};
        // Top: row3 - row1
        m_planes[Top]    = {{r3x - r1x, r3y - r1y, r3z - r1z}, r3w - r1w};
        // Near: row3 + row2 (OpenGL/GX2 NDC z in [-1, 1])
        m_planes[Near]   = {{r3x + r2x, r3y + r2y, r3z + r2z}, r3w + r2w};
        // Far: row3 - row2
        m_planes[Far]    = {{r3x - r2x, r3y - r2y, r3z - r2z}, r3w - r2w};

        for (auto& p : m_planes) {
            p.Normalize();
        }
    }

    /**
     * @brief Retorna true si la esfera interseca o está dentro del frustum.
     */
    [[nodiscard]] bool TestSphere(const Point3D& center, float radius) const noexcept {
        for (const auto& p : m_planes) {
            if (p.SignedDistance(center) < -radius) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Retorna true si la caja AABB [minPt, maxPt] interseca o está dentro del frustum.
     */
    [[nodiscard]] bool TestAABB(const Point3D& minPt, const Point3D& maxPt) const noexcept {
        for (const auto& p : m_planes) {
            const Point3D pVertex{
                (p.normal.x >= 0.0f) ? maxPt.x : minPt.x,
                (p.normal.y >= 0.0f) ? maxPt.y : minPt.y,
                (p.normal.z >= 0.0f) ? maxPt.z : minPt.z
            };
            if (p.SignedDistance(pVertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Calcula los 8 vértices en espacio mundo de una pirámide de visión para DebugDraw3D.
     *        outCorners[0..3] = plano Near (BL, BR, TR, TL); outCorners[4..7] = plano Far (BL, BR, TR, TL).
     */
    static void ComputeCorners(const Point3D& pos,
                               const Point3D& forward,
                               const Point3D& right,
                               const Point3D& up,
                               float fovYRad,
                               float aspect,
                               float nearDist,
                               float farDist,
                               Point3D outCorners[8]) noexcept {
        const float tanHalfFov = std::tan(fovYRad * 0.5f);
        const float nh = nearDist * tanHalfFov;
        const float nw = nh * aspect;
        const float fh = farDist * tanHalfFov;
        const float fw = fh * aspect;

        const Point3D nc = pos + forward * nearDist;
        const Point3D fc = pos + forward * farDist;

        outCorners[0] = nc - up * nh - right * nw; // Near BL
        outCorners[1] = nc - up * nh + right * nw; // Near BR
        outCorners[2] = nc + up * nh + right * nw; // Near TR
        outCorners[3] = nc + up * nh - right * nw; // Near TL

        outCorners[4] = fc - up * fh - right * fw; // Far BL
        outCorners[5] = fc - up * fh + right * fw; // Far BR
        outCorners[6] = fc + up * fh + right * fw; // Far TR
        outCorners[7] = fc + up * fh - right * fw; // Far TL
    }

    [[nodiscard]] const FrustumPlane& GetPlane(uint32_t idx) const noexcept {
        return m_planes[idx < 6u ? idx : 0u];
    }

private:
    std::array<FrustumPlane, 6> m_planes{};
};

} // namespace WiiU
