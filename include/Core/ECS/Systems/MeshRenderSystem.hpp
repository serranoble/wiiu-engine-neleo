#pragma once

#include "Graphics/ForwardRendererGX2.hpp"

namespace WiiU {

class ECSWorld;

/**
 * @brief MeshRenderSystem: Sistema ECS desacoplado para el despacho de mallas estáticas y skinneds al ForwardRendererGX2.
 * 
 * Itera sobre entidades con:
 * - TransformComponent
 * - MeshComponent (mallas estáticas)
 * - SkeletalMeshComponent (mallas animadas con paleta de huesos LBS)
 * - MaterialComponent (parámetros de brillo, relieve y reflectividad)
 */
class MeshRenderSystem {
public:
    static void Render(ECSWorld& world, ForwardRendererGX2& renderer);
};

} // namespace WiiU
