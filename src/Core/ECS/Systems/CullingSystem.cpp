#include "Core/ECS/Systems/CullingSystem.hpp"
#include "Core/Spatial/SpatialBVH.hpp"
#include "Core/JobSystem.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

namespace {

[[nodiscard]] float ComputeCotHalfFov(float fovDeg) noexcept {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    const float tanHalfFov = std::tan((fovDeg * 0.5f) * DEG_TO_RAD);
    return (tanHalfFov > 1e-4f) ? (1.0f / tanHalfFov) : 1.0f;
}

void ComputeWorldSphere(const TransformComponent& transform, const BoundingVolumeComponent& bounding,
                        Point3D& outCenter, float& outRadius) noexcept {
    const Matrix4x4& m = transform.worldMatrix;
    outCenter = Point3D{
        m.m[0] * bounding.center.x + m.m[4] * bounding.center.y + m.m[8]  * bounding.center.z + m.m[12],
        m.m[1] * bounding.center.x + m.m[5] * bounding.center.y + m.m[9]  * bounding.center.z + m.m[13],
        m.m[2] * bounding.center.x + m.m[6] * bounding.center.y + m.m[10] * bounding.center.z + m.m[14]
    };
    const float maxScale = std::max({std::abs(transform.scale.x),
                                     std::abs(transform.scale.y),
                                     std::abs(transform.scale.z)});
    outRadius = bounding.radius * maxScale;
}

[[nodiscard]] bool ShouldCullSmallPrimitive(const FrustumPlane& nearPlane, const Point3D& worldCenter,
                                            float worldRadius, float cotHalfFov, float halfScreenH,
                                            const BoundingVolumeComponent& bounding) noexcept {
    if (!bounding.enableSmallPrimitiveCulling) return false;
    const float distToCam = nearPlane.SignedDistance(worldCenter);
    if (distToCam <= worldRadius) return false;
    const float screenRadius = (worldRadius * cotHalfFov / distToCam) * halfScreenH;
    return screenRadius < bounding.minScreenRadius;
}

} // namespace

void CullingSystem::ExtractPlanes(const Matrix4x4& vp, FrustumPlane planes[6]) noexcept {
    // La matriz vp tiene orden columna-mayor (m[c*4 + r]):
    // Fila 0: m[0], m[4], m[8], m[12]
    // Fila 1: m[1], m[5], m[9], m[13]
    // Fila 2: m[2], m[6], m[10], m[14]
    // Fila 3: m[3], m[7], m[11], m[15]

    // 0: Plano Izquierdo: Fila 3 + Fila 0
    planes[0].normal.x = vp.m[3]  + vp.m[0];
    planes[0].normal.y = vp.m[7]  + vp.m[4];
    planes[0].normal.z = vp.m[11] + vp.m[8];
    planes[0].distance = vp.m[15] + vp.m[12];
    planes[0].Normalize();

    // 1: Plano Derecho: Fila 3 - Fila 0
    planes[1].normal.x = vp.m[3]  - vp.m[0];
    planes[1].normal.y = vp.m[7]  - vp.m[4];
    planes[1].normal.z = vp.m[11] - vp.m[8];
    planes[1].distance = vp.m[15] - vp.m[12];
    planes[1].Normalize();

    // 2: Plano Inferior: Fila 3 + Fila 1
    planes[2].normal.x = vp.m[3]  + vp.m[1];
    planes[2].normal.y = vp.m[7]  + vp.m[5];
    planes[2].normal.z = vp.m[11] + vp.m[9];
    planes[2].distance = vp.m[15] + vp.m[13];
    planes[2].Normalize();

    // 3: Plano Superior: Fila 3 - Fila 1
    planes[3].normal.x = vp.m[3]  - vp.m[1];
    planes[3].normal.y = vp.m[7]  - vp.m[5];
    planes[3].normal.z = vp.m[11] - vp.m[9];
    planes[3].distance = vp.m[15] - vp.m[13];
    planes[3].Normalize();

    // 4: Plano Cercano (Near): Fila 3 + Fila 2
    planes[4].normal.x = vp.m[3]  + vp.m[2];
    planes[4].normal.y = vp.m[7]  + vp.m[6];
    planes[4].normal.z = vp.m[11] + vp.m[10];
    planes[4].distance = vp.m[15] + vp.m[14];
    planes[4].Normalize();

    // 5: Plano Lejano (Far): Fila 3 - Fila 2
    planes[5].normal.x = vp.m[3]  - vp.m[2];
    planes[5].normal.y = vp.m[7]  - vp.m[6];
    planes[5].normal.z = vp.m[11] - vp.m[10];
    planes[5].distance = vp.m[15] - vp.m[14];
    planes[5].Normalize();
}

void CullingSystem::Update(ECSWorld& world, const CameraComponent& camera,
                           float /*screenWidth*/, float screenHeight) {
    FrustumPlane planes[6];
    ExtractPlanes(camera.viewProjMatrix, planes);

    const float cotHalfFov = ComputeCotHalfFov(camera.fov);
    const float halfScreenH = screenHeight * 0.5f;

    world.ForEach<TransformComponent, MeshComponent, BoundingVolumeComponent>(
        [&planes, cotHalfFov, halfScreenH](Entity /*entity*/,
                                           const TransformComponent& transform,
                                           MeshComponent& mesh,
                                           BoundingVolumeComponent& bounding) {
            Point3D worldCenter;
            float worldRadius = 0.0f;
            ComputeWorldSphere(transform, bounding, worldCenter, worldRadius);

            bool isCulled = false;
            if (bounding.enableFrustumCulling) {
                for (int i = 0; i < 6; ++i) {
                    if (planes[i].SignedDistance(worldCenter) < -worldRadius) {
                        isCulled = true;
                        break;
                    }
                }
            }

            if (!isCulled && ShouldCullSmallPrimitive(planes[4], worldCenter, worldRadius, cotHalfFov, halfScreenH, bounding)) {
                isCulled = true;
            }

            bounding.isCulled = isCulled;
            mesh.isVisible = !isCulled;
        }
    );
}

void CullingSystem::UpdateParallel(ECSWorld& world, const CameraComponent& camera,
                                   float screenWidth, float screenHeight) {
    // Para conjuntos de entidades típicos en silicio Espresso (< 256 entidades),
    // el frustum culling secuencial en Core 1 toma < 1 us.
    // Ejecutarlo en línea elimina latencia de sincronización entre núcleos y previene deadlocks.
    Update(world, camera, screenWidth, screenHeight);
}

size_t CullingSystem::UpdateWithBVH(ECSWorld& world, const SpatialBVH& bvh, const CameraComponent& camera,
                                    float screenWidth, float screenHeight) {
    if (bvh.IsEmpty()) {
        Update(world, camera, screenWidth, screenHeight);
        size_t visible = 0;
        world.ForEach<MeshComponent>([&visible](Entity /*e*/, MeshComponent& m) {
            if (m.isVisible) ++visible;
        });
        return visible;
    }

    FrustumPlane planes[6];
    ExtractPlanes(camera.viewProjMatrix, planes);

    // 1. Marcar como ocultas inicialmente todas las mallas sujetas a culling
    world.ForEach<MeshComponent, BoundingVolumeComponent>(
        [](Entity /*entity*/, MeshComponent& mesh, BoundingVolumeComponent& bounding) {
            if (bounding.enableFrustumCulling) {
                bounding.isCulled = true;
                mesh.isVisible = false;
            }
        }
    );

    const float cotHalfFov = ComputeCotHalfFov(camera.fov);
    const float halfScreenH = screenHeight * 0.5f;

    // 2. Consulta jerárquica en O(log N) sin buffers intermedios ni alocaciones
    size_t finalVisible = 0;
    bvh.Traverse(
        [&planes](const SpatialAABB& box) noexcept { return box.IntersectsFrustum(planes); },
        [&](const BVHPrimitive& prim) noexcept {
            const Entity ent = static_cast<Entity>(prim.userId);
            auto* mesh = world.GetComponent<MeshComponent>(ent);
            auto* bounding = world.GetComponent<BoundingVolumeComponent>(ent);
            if (!mesh || !bounding) return true;

            bool culled = false;
            if (const auto* tf = world.GetComponent<TransformComponent>(ent)) {
                Point3D worldCenter;
                float worldRadius = 0.0f;
                ComputeWorldSphere(*tf, *bounding, worldCenter, worldRadius);
                culled = ShouldCullSmallPrimitive(planes[4], worldCenter, worldRadius, cotHalfFov, halfScreenH, *bounding);
            }

            bounding->isCulled = culled;
            mesh->isVisible = !culled;
            if (!culled) {
                ++finalVisible;
            }
            return true;
        }
    );

    return finalVisible;
}

} // namespace WiiU
