#pragma once

#include "Core/ECS/ECSWorld.hpp"

namespace WiiU {

/**
 * @brief [OPT-336] Sistema ECS de Muestreo de Animación y Cálculo de Skinning Matrices.
 * 
 * Basado en Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 11.2 & 11.3).
 * Resuelve la jerarquía de huesos en un solo forward loop O(N) lineal sin recursión,
 * con cero alocaciones en caliente y alineación a 256 bytes para hardware DMA.
 */
class AnimationSystem {
public:
    static void Update(ECSWorld& world, float dt);
};

} // namespace WiiU
