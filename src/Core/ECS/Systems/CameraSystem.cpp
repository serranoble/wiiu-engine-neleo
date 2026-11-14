#include "Core/ECS/Systems/CameraSystem.hpp"
#include <cmath>

namespace WiiU {

void CameraSystem::Update(ECSWorld& world) {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

    world.ForEach<TransformComponent, CameraComponent>(
        [DEG_TO_RAD](Entity /*entity*/, const TransformComponent& transform, CameraComponent& camera) {
            if (!camera.isDirty && !transform.isDirty) {
                return;
            }

            // 1. Matriz de Vista (LookAt desde la posición actual de la cámara hacia el objetivo)
            camera.viewMatrix = Matrix4x4::LookAt(transform.position, camera.target, camera.up);

            // 2. Matriz de Proyección
            if (camera.projectionType == CameraProjectionType::Perspective) {
                float fovRad = camera.fov * DEG_TO_RAD;
                camera.projMatrix = Matrix4x4::Perspective(fovRad, camera.aspectRatio, camera.nearClip, camera.farClip);
            }

            // 3. Matriz Combinada View-Projection
            camera.viewProjMatrix = camera.projMatrix * camera.viewMatrix;
            camera.isDirty = false;
        }
    );
}

} // namespace WiiU
