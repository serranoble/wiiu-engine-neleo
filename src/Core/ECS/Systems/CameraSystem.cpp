#include "Core/ECS/Systems/CameraSystem.hpp"
#include <cmath>

namespace Neleo {

void CameraSystem::Update(ECSWorld& world) {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

    world.ForEach<TransformComponent, CameraComponent>(
        [DEG_TO_RAD](Entity /*entity*/, const TransformComponent& transform, CameraComponent& camera) {
            const bool posChanged = (transform.position.x != camera.lastPosition.x) ||
                                    (transform.position.y != camera.lastPosition.y) ||
                                    (transform.position.z != camera.lastPosition.z);
            if (!camera.isDirty && !transform.isDirty && !posChanged) {
                return;
            }

            // 1. Matriz de Vista (LookAt desde la posición actual de la cámara hacia el objetivo)
            camera.viewMatrix = Matrix4x4::LookAt(transform.position, camera.target, camera.up);
            camera.lastPosition = transform.position;

            // 2. Matriz de Proyección (Perspectiva u Ortográfica)
            if (camera.projectionType == CameraProjectionType::Perspective) {
                float fovRad = camera.fov * DEG_TO_RAD;
                camera.projMatrix = Matrix4x4::Perspective(fovRad, camera.aspectRatio, camera.nearClip, camera.farClip);
            } else {
                const float halfH = (camera.orthoHalfHeight > 0.01f) ? camera.orthoHalfHeight : 10.0f;
                const float halfW = halfH * camera.aspectRatio;
                camera.projMatrix = Matrix4x4::Orthographic(-halfW, halfW, -halfH, halfH, camera.nearClip, camera.farClip);
            }

            // 3. Matriz Combinada View-Projection
            camera.viewProjMatrix = camera.projMatrix * camera.viewMatrix;
            camera.isDirty = false;
        }
    );
}

} // namespace Neleo
