#include "Graphics/ShadowSystem.hpp"
#include <algorithm>
#include <cmath>

namespace Neleo {

void ShadowSystem::ComputeCascadeSplits(float nearPlane, float farPlane, uint32_t count,
                                        float lambda, float* outSplits) noexcept {
    if (!outSplits || count == 0) return;

    const float n = std::max(0.01f, nearPlane);
    const float f = std::max(n + 1.0f, farPlane);
    const float ratio = f / n;
    const float range = f - n;
    const uint32_t clampedCount = std::min(count, MAX_CASCADES);

    for (uint32_t i = 0; i < clampedCount; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(clampedCount);
        const float logSplit = n * std::pow(ratio, p);
        const float uniformSplit = n + range * p;
        outSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

void ShadowSystem::ComputeCascades(const ShadowCameraDesc& camera,
                                   const Point3D& lightDir, const ShadowConfig& config,
                                   ShadowCascade outCascades[MAX_CASCADES]) noexcept {
    const uint32_t count = std::clamp(config.cascadeCount, 1u, MAX_CASCADES);
    const float nearZ = std::max(0.05f, camera.nearClip);
    const float farZ = std::min(camera.farClip, config.maxShadowDistance);

    float splits[MAX_CASCADES]{};
    ComputeCascadeSplits(nearZ, farZ, count, config.splitLambda, splits);

    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    const float halfFovY = (camera.fovDeg * 0.5f) * DEG_TO_RAD;
    const float tanHalfFovY = std::tan(halfFovY);
    const float tanHalfFovX = tanHalfFovY * camera.aspectRatio;

    const Point3D camForward = (camera.target - camera.position).Normalized();
    const Point3D safeUp = (std::abs(Point3D::Dot(camForward, camera.up.Normalized())) > 0.98f)
                           ? Point3D{0.0f, 0.0f, 1.0f} : camera.up;
    const Point3D camRight = Point3D::Cross(camForward, safeUp).Normalized();
    const Point3D camUp = Point3D::Cross(camRight, camForward);

    Point3D lDir = lightDir.Normalized();
    if (lDir.Length() < 0.001f) {
        lDir = Point3D{0.4f, -0.85f, 0.34f}.Normalized();
    }
    const Point3D lightUp = (std::abs(lDir.y) > 0.98f) ? Point3D{0.0f, 0.0f, 1.0f} : Point3D{0.0f, 1.0f, 0.0f};

    float prevSplit = nearZ;
    const float res = static_cast<float>(std::max(128u, config.shadowMapResolution));

    for (uint32_t c = 0; c < count; ++c) {
        const float curSplit = splits[c];

        // 8 vértices del sub-frustum en espacio de mundo
        const float nearH = prevSplit * tanHalfFovY;
        const float nearW = prevSplit * tanHalfFovX;
        const float farH = curSplit * tanHalfFovY;
        const float farW = curSplit * tanHalfFovX;

        const Point3D nc = camera.position + camForward * prevSplit;
        const Point3D fc = camera.position + camForward * curSplit;

        Point3D corners[8] = {
            nc + camUp * nearH - camRight * nearW,
            nc + camUp * nearH + camRight * nearW,
            nc - camUp * nearH - camRight * nearW,
            nc - camUp * nearH + camRight * nearW,
            fc + camUp * farH - camRight * farW,
            fc + camUp * farH + camRight * farW,
            fc - camUp * farH - camRight * farW,
            fc - camUp * farH + camRight * farW
        };

        // Centroide del sub-frustum y esfera delimitadora invariante a rotación de cámara
        Point3D center{0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 8; ++i) {
            center = center + corners[i];
        }
        center = center * (1.0f / 8.0f);

        float radius = 0.0f;
        for (int i = 0; i < 8; ++i) {
            radius = std::max(radius, (corners[i] - center).Length());
        }
        // Cuantización del radio para estabilidad temporal
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Estabilización de texels (Texel Snapping) para erradicar shimmering en bordes
        const float worldUnitsPerTexel = (radius * 2.0f) / res;
        Matrix4x4 tempView = Matrix4x4::LookAt(center - lDir * (radius * 2.0f), center, lightUp);

        // Proyectar centroide a espacio de luz y ajustar al texel discreto más cercano
        const float lx = tempView.m[0] * center.x + tempView.m[4] * center.y + tempView.m[8] * center.z + tempView.m[12];
        const float ly = tempView.m[1] * center.x + tempView.m[5] * center.y + tempView.m[9] * center.z + tempView.m[13];
        const float snappedX = std::floor(lx / worldUnitsPerTexel) * worldUnitsPerTexel;
        const float snappedY = std::floor(ly / worldUnitsPerTexel) * worldUnitsPerTexel;

        // Desplazar centro en los ejes locales de luz
        const Point3D lRight{tempView.m[0], tempView.m[4], tempView.m[8]};
        const Point3D lUp{tempView.m[1], tempView.m[5], tempView.m[9]};
        center = center + lRight * (snappedX - lx) + lUp * (snappedY - ly);

        const Point3D lightEye = center - lDir * (radius * 2.0f);
        ShadowCascade& cascade = outCascades[c];
        cascade.splitDepthNear = prevSplit;
        cascade.splitDepthFar = curSplit;
        cascade.orthoRadius = radius;
        cascade.texelWorldSize = worldUnitsPerTexel;
        cascade.lightViewMatrix = Matrix4x4::LookAt(lightEye, center, lightUp);
        cascade.lightProjMatrix = Matrix4x4::Orthographic(-radius, radius, -radius, radius, 0.1f, radius * 4.5f);
        cascade.lightViewProjMatrix = cascade.lightProjMatrix * cascade.lightViewMatrix;

        prevSplit = curSplit;
    }
}

float ShadowSystem::ComputeSoftShadowPenumbra(float occluderDist, float lightRadius, float minFactor) noexcept {
    if (occluderDist <= 0.0f) return minFactor;
    // Penumbra proporcional al radio de la luz y la distancia al bloqueador (PCSS / Soft RT)
    const float penumbra = std::clamp(occluderDist * lightRadius * 0.25f, 0.0f, 1.0f);
    return std::clamp(minFactor + penumbra * (1.0f - minFactor), minFactor, 1.0f);
}

} // namespace Neleo
