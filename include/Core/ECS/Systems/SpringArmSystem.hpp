#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/SpringArmComponent.hpp"
#include "Core/ECS/Components/StudioTurntableComponent.hpp"

namespace Neleo {

/**
 * @brief [OPT-321] Sistema ECS de Barrido de Colisión y Amortiguamiento de Brazo de Cámara.
 * 
 * Basado en Mark Haigh-Hutchinson (Real-Time Cameras) y Jason Gregory (Cap. 16.4).
 * Evalúa el rayo desde el objetivo hacia el ojo de la cámara:
 * 1. Prueba de plano horizontal contra minGroundY (evita penetración de suelo o pedestal).
 * 2. Barrido esférico contra los volúmenes BoundingVolumeComponent de las entidades vivas.
 * 3. Integración exponencial asimétrica sobre currentArmLength.
 * 4. Acoplamiento transparente con StudioTurntableComponent::distance.
 */
class SpringArmSystem {
public:
    static void Update(ECSWorld& world, float dt);
};

} // namespace Neleo
