#include "Core/ECS/Systems/SpringArmSystem.hpp"
#include "Core/Physics/CameraRaycastHelper.hpp"
#include <cmath>
#include <algorithm>

namespace WiiU {

void SpringArmSystem::Update(ECSWorld& world, float dt) {
    if (dt <= 0.0f) return;

    world.ForEach<SpringArmComponent, StudioTurntableComponent>([&world, dt](Entity e, SpringArmComponent& arm, StudioTurntableComponent& tt) {
        if (!arm.collisionEnabled) {
            arm.currentArmLength = arm.targetArmLength;
            tt.distance = arm.targetArmLength;
            arm.isObstructed = false;
            return;
        }

        // 1. Sincronizar longitud objetivo con la deseada por el usuario (Zoom)
        arm.targetArmLength = std::clamp(tt.targetDistance, arm.minArmLength, arm.maxArmLength);

        // 2. Vector unitario de retroceso de la cámara hacia el ojo:
        // eye = target + distance * (sinYaw*cosPitch, sinPitch, -cosYaw*cosPitch)
        const float cosPitch = std::cos(tt.pitch);
        const float sinPitch = std::sin(tt.pitch);
        const float sinYaw = std::sin(tt.yaw);
        const float cosYaw = std::cos(tt.yaw);

        const Point3D rayDir = Point3D{
            sinYaw * cosPitch,
            sinPitch,
            -cosYaw * cosPitch
        }.Normalized();

        // 3. Barrido analítico compartido contra suelo, ColliderComponent (AABB/Sphere) y BoundingVolumeComponent
        const float maxAllowedDist = CameraRaycastHelper::SweepWorld(
            world,
            e,
            tt.target,
            rayDir,
            arm.targetArmLength,
            arm.minArmLength,
            arm.probeRadius,
            arm.minGroundY,
            true
        );

        arm.isObstructed = (maxAllowedDist < arm.targetArmLength - 0.05f);

        // 4. Suavizado asimétrico: retracción rápida (evitar clipping), retorno elástico suave
        const float damping = arm.isObstructed ? arm.collapseDamping : arm.extendDamping;
        const float alpha = std::clamp(1.0f - std::exp(-damping * dt), 0.0f, 1.0f);
        arm.currentArmLength += (maxAllowedDist - arm.currentArmLength) * alpha;

        // 5. Aplicar la distancia física resultante y sincronizar posición de cámara
        tt.distance = arm.currentArmLength;
        if (auto* trans = world.GetComponent<TransformComponent>(e)) {
            trans->SetPosition(tt.ComputeEyePosition());
        }
    });
}

} // namespace WiiU
