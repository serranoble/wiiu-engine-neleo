#include "Core/ECS/Systems/TransformSystem.hpp"

namespace WiiU {

void TransformSystem::Update(ECSWorld& world) {
    // Pase 1: Recalcular matrices TRS locales en entidades que han cambiado (dirty-checking)
    world.ForEach<TransformComponent>([](Entity /*entity*/, TransformComponent& transform) {
        if (!transform.isDirty) {
            return;
        }

        Matrix4x4 tMat = Matrix4x4::Translation(transform.position.x, transform.position.y, transform.position.z);
        Matrix4x4 rxMat = Matrix4x4::RotationX(transform.rotation.x);
        Matrix4x4 ryMat = Matrix4x4::RotationY(transform.rotation.y);
        Matrix4x4 rzMat = Matrix4x4::RotationZ(transform.rotation.z);
        Matrix4x4 sMat = Matrix4x4::Scale(transform.scale.x, transform.scale.y, transform.scale.z);

        // Composición canónica TRS: T * (Ry * Rx * Rz) * S
        Matrix4x4 rMat = ryMat * (rxMat * rzMat);
        transform.worldMatrix = tMat * rMat * sMat;
        transform.isDirty = false;
    });

    // Pase 2: Resolución de Jerarquías Padre-Hijo (OPT-254 Flat Scene Graph)
    world.ForEach<TransformComponent, HierarchyComponent>(
        [&world](Entity /*entity*/, TransformComponent& childTransform, const HierarchyComponent& hierarchy) {
            if (!hierarchy.HasParent()) {
                return;
            }

            const auto* parentTransform = world.GetComponent<TransformComponent>(hierarchy.parent);
            if (!parentTransform) {
                return;
            }

            // Componer matriz de mundo del hijo multiplicando la del padre por el TRS local del hijo
            Matrix4x4 localTRS = Matrix4x4::Translation(childTransform.position.x, childTransform.position.y, childTransform.position.z)
                               * (Matrix4x4::RotationY(childTransform.rotation.y) * (Matrix4x4::RotationX(childTransform.rotation.x) * Matrix4x4::RotationZ(childTransform.rotation.z)))
                               * Matrix4x4::Scale(childTransform.scale.x, childTransform.scale.y, childTransform.scale.z);

            childTransform.worldMatrix = parentTransform->worldMatrix * localTRS;
        }
    );
}

} // namespace WiiU
