#pragma once

#include "Core/ECS/ECSWorld.hpp"
#include "Graphics/ForwardRendererGX2.hpp"

namespace Neleo {

/**
 * @brief [OPT-351 / OPT-355 / OPT-367] Pase Modular de Skybox con Early-Z Reversed Draw Order.
 *
 * Se invoca al final del pase de geometria opaca para que el Z-Buffer / Hi-Z de AMD Latte
 * descarte automaticamente todos los fragmentos cubiertos por la geometria del mundo antes
 * de evaluar el Pixel Shader del cielo (Z = W = 1.0, compare = LEQUAL, depthWrite = false).
 */
class SkyboxRenderPass {
public:
    static bool Render(ECSWorld& world, ForwardRendererGX2& renderer);
};

} // namespace Neleo
