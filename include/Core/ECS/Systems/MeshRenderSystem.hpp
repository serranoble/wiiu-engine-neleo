#pragma once

namespace Neleo {

class ECSWorld;
class ForwardRendererGX2;

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
    /**
     * @brief [OPT-27 / OPT-260 / OPT-261 / OPT-351] Recolecta entidades ECS en RenderQueue 64-bit,
     *        ordena por estado/profundidad, ejecuta Depth Pre-Pass opcional, despacha opacos/transparentes
     *        y finaliza con SkyboxRenderPass (Early-Z).
     */
    static void Render(ECSWorld& world, ForwardRendererGX2& renderer, bool enableEarlyZPrePass = false);
};

} // namespace Neleo
