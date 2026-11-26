#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/StudioTurntableComponent.hpp"

namespace Neleo {

/**
 * @brief [OPT-302] Sistema ECS para actualización desacoplada de plataformas de estudio (StudioTurntableSystem).
 * 
 * Gestiona la integración temporal de rotación turntable continua,
 * el amortiguamiento exponencial hacia los ángulos objetivos y la renormalización acoplada
 * en el grupo circular SO(2) [-2*pi, +2*pi] sin salto visual.
 */
class StudioTurntableSystem {
public:
    static void Update(ECSWorld& world, float dt);
};

} // namespace Neleo
