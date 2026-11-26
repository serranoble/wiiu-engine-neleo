#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/AudioEmitterComponent.hpp"

namespace Neleo {

/**
 * @brief [OPT-246] Sistema de Audio Espacial 3D orientado a datos (AudioSpatialSystem).
 * 
 * Itera mediante Sparse-Set View conjunta sobre (TransformComponent, AudioEmitterComponent).
 * Sincroniza la posición espacial 3D física de los emisores acústicos con AudioEngine3D
 * y gestiona el ciclo de vida de los canales del Voice Pool de 32 canales en tiempo real.
 */
class AudioSpatialSystem {
public:
    static void Update(ECSWorld& world);
    static void Update(ECSWorld& world, float dt);
};

} // namespace Neleo
