#pragma once

#include "Core/ECS/ECSWorld.hpp"

namespace Neleo {

/**
 * @brief [OPT-125] Sistema ECS de Cinemática Inversa Esquelética (Two-Bone, Look-At & FABRIK).
 *
 * Basado en Jason Gregory (Game Engine Architecture, 3ª Ed., Cap. 12.5).
 * Ejecuta como post-proceso sobre la pose FK muestreada de `SkeletalMeshComponent`,
 * con cero asignaciones dinámicas y actualización topológica de `skinningMatrices`.
 */
class IKSystem {
public:
    static void Update(ECSWorld& world, float dt) noexcept;
};

} // namespace Neleo
