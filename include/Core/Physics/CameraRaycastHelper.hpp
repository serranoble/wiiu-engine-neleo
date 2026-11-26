#pragma once

#include "Graphics/Math3D.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/ColliderComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include <algorithm>
#include <cmath>

namespace Neleo {

/**
 * @brief [Hito 133] Utilidad compartida de raycast analitico para brazos de camara (Cap. 17).
 *
 * Evita duplicacion de logica geometrica entre `SpringArmSystem` y `OrbitCameraSystem`:
 * - Interseccion Ray-Sphere analitica.
 * - Interseccion Ray-AABB por algoritmo de slabs de Kay-Kajiya.
 * - Barrido unificado sobre `ColliderComponent` (`Layer_StaticWorld`) y `BoundingVolumeComponent`.
 */
struct CameraRaycastHelper {
    static inline bool IntersectRaySphere(const Point3D& origin,
                                          const Point3D& dirNorm,
                                          const Point3D& center,
                                          float radius,
                                          float maxDist,
                                          float& outHitDist) noexcept {
        const Point3D toCenter = center - origin;
        const float distSq = Point3D::Dot(toCenter, toCenter);
        const float radiusSq = radius * radius;
        if (distSq <= radiusSq) {
            return false;
        }

        const float tProj = Point3D::Dot(toCenter, dirNorm);
        if (tProj <= 0.0f) {
            return false;
        }

        const float perpDistSq = distSq - tProj * tProj;
        if (perpDistSq >= radiusSq) {
            return false;
        }

        const float tHalfChord = std::sqrt(std::max(0.0f, radiusSq - perpDistSq));
        const float hitDist = tProj - tHalfChord;
        if (hitDist > 0.0f && hitDist < maxDist) {
            outHitDist = hitDist;
            return true;
        }
        return false;
    }

    static inline bool IntersectRayAABB(const Point3D& origin,
                                        const Point3D& dirNorm,
                                        const Point3D& boxMin,
                                        const Point3D& boxMax,
                                        float maxDist,
                                        float& outHitDist) noexcept {
        // Omitir si el pivote esta dentro del AABB (ej. camara tocando interior)
        if (origin.x >= boxMin.x && origin.x <= boxMax.x &&
            origin.y >= boxMin.y && origin.y <= boxMax.y &&
            origin.z >= boxMin.z && origin.z <= boxMax.z) {
            return false;
        }

        float tMin = 0.0f;
        float tMax = maxDist;

        auto testSlab = [&](float orig, float dir, float minB, float maxB) noexcept -> bool {
            if (std::abs(dir) < 1e-6f) {
                return orig >= minB && orig <= maxB;
            }
            const float invD = 1.0f / dir;
            float t1 = (minB - orig) * invD;
            float t2 = (maxB - orig) * invD;
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            return tMin <= tMax;
        };

        if (!testSlab(origin.x, dirNorm.x, boxMin.x, boxMax.x)) return false;
        if (!testSlab(origin.y, dirNorm.y, boxMin.y, boxMax.y)) return false;
        if (!testSlab(origin.z, dirNorm.z, boxMin.z, boxMax.z)) return false;

        if (tMin > 0.0f && tMin < maxDist) {
            outHitDist = tMin;
            return true;
        }
        return false;
    }

    static inline float SweepWorld(ECSWorld& world,
                                   Entity ignoreEntity,
                                   const Point3D& pivot,
                                   const Point3D& rayDirNorm,
                                   float targetDist,
                                   float minDist,
                                   float probeRadius,
                                   float minGroundY,
                                   bool includeBoundingVolumes) {
        float maxAllowedDist = std::max(minDist, targetDist);

        // 1. Plano de suelo minimo para evitar perforacion bajo el terreno
        const float minEyeY = minGroundY + probeRadius;
        if (rayDirNorm.y < -0.001f) {
            const float distToGround = (pivot.y - minEyeY) / (-rayDirNorm.y);
            if (distToGround >= 0.0f && distToGround < maxAllowedDist) {
                maxAllowedDist = std::max(minDist, distToGround);
            }
        }

        // 2. Obstaculos fisicos estaticos (ColliderComponent: Layer_StaticWorld)
        world.ForEach<TransformComponent, ColliderComponent>(
            [&](Entity ent, const TransformComponent& trans, const ColliderComponent& col) {
                if (ent == ignoreEntity || !col.isSolid) {
                    return;
                }
                if ((col.layer & CollisionLayer::Layer_StaticWorld) == 0) {
                    return;
                }

                const Point3D center = trans.position + col.localOffset;
                float hitDist = maxAllowedDist;
                if (col.shape == ColliderShape::Sphere) {
                    if (IntersectRaySphere(pivot, rayDirNorm, center, col.radius + probeRadius, maxAllowedDist, hitDist)) {
                        maxAllowedDist = std::max(minDist, hitDist);
                    }
                } else {
                    const Point3D inflate{probeRadius, probeRadius, probeRadius};
                    const Point3D bMin = center - col.halfExtents - inflate;
                    const Point3D bMax = center + col.halfExtents + inflate;
                    if (IntersectRayAABB(pivot, rayDirNorm, bMin, bMax, maxAllowedDist, hitDist)) {
                        maxAllowedDist = std::max(minDist, hitDist);
                    }
                }
            }
        );

        // 3. Volumenes delimitadores de mallas (SpringArm en escenas tipo StudioTurntable)
        if (includeBoundingVolumes) {
            world.ForEach<BoundingVolumeComponent, TransformComponent>(
                [&](Entity ent, const BoundingVolumeComponent& bvol, const TransformComponent& trans) {
                    if (ent == ignoreEntity) {
                        return;
                    }
                    const Point3D center = trans.position + bvol.center;
                    const float radius = bvol.radius + probeRadius;
                    float hitDist = maxAllowedDist;
                    if (IntersectRaySphere(pivot, rayDirNorm, center, radius, maxAllowedDist, hitDist)) {
                        maxAllowedDist = std::max(minDist, hitDist);
                    }
                }
            );
        }

        return maxAllowedDist;
    }
};

} // namespace Neleo
