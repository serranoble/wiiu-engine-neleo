#pragma once

#include "Core/Spatial/FrustumTypes.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"

namespace Neleo {

class SpatialBVH;

/**
 * @brief [OPT-256] Sistema de Culling de Frustum y Primitivas Pequeñas en Core 2 / Hilo de Trabajo.
 * 
 * Extrae los 6 planos del frustum desde CameraComponent y evalúa en paralelo o en lote
 * las esferas delimitadoras de BoundingVolumeComponent, actualizando MeshComponent::isVisible.
 * Previene el desperdicio de ciclos en el Primitive Setup Engine de AMD Latte (R700).
 */
class CullingSystem {
public:
    // Extrae los 6 planos del frustum desde la matriz View-Projection (Gribb-Hartmann)
    static void ExtractPlanes(const Matrix4x4& vp, FrustumPlane planes[6]) noexcept;

    // Actualiza la visibilidad de todas las entidades con (TransformComponent, MeshComponent, BoundingVolumeComponent)
    static void Update(ECSWorld& world, const CameraComponent& camera,
                       float screenWidth = 1280.0f, float screenHeight = 720.0f);

    // [OPT-269] Actualización paralela de visibilidad despachando el cálculo geométrico a Core 2 vía JobSystem
    static void UpdateParallel(ECSWorld& world, const CameraComponent& camera,
                               float screenWidth = 1280.0f, float screenHeight = 720.0f);

    // [Cap. 11.1/11.2] Frustum Culling jerárquico en O(log N) mediante árbol SpatialBVH
    static size_t UpdateWithBVH(ECSWorld& world, const SpatialBVH& bvh, const CameraComponent& camera,
                                float screenWidth = 1280.0f, float screenHeight = 720.0f);
};

} // namespace Neleo
