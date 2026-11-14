#include "Core/ECS/Systems/MeshRenderSystem.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"

namespace WiiU {

void MeshRenderSystem::Render(ECSWorld& world, ForwardRendererGX2& renderer) {
#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Pase de Mallas Estáticas (excluyendo entidades con SkeletalMeshComponent)
    world.ForEach<TransformComponent, MeshComponent, MaterialComponent>(
        [&world, &renderer](Entity ent,
                            const TransformComponent& transform,
                            const MeshComponent& mesh,
                            const MaterialComponent& mat) {
            if (!mesh.isVisible || !mat.isVisible || !mesh.vbo || !mesh.ibo || mesh.indexCount == 0) {
                return;
            }
            if (world.GetComponent<SkeletalMeshComponent>(ent) != nullptr) {
                return;
            }

            renderer.RenderMeshBuffers(
                *static_cast<GX2RBuffer*>(mesh.vbo),
                *static_cast<GX2RBuffer*>(mesh.ibo),
                mesh.indexCount,
                mesh.vertexStride,
                transform.worldMatrix,
                nullptr,
                nullptr,
                mat.metallic,
                true,
                mat.customParam != 0.0f ? mat.customParam : 2.0f,
                0.38f
            );
        }
    );

    // 2. Pase de Mallas Esqueléticas Animadas (LBS)
    world.ForEach<TransformComponent, MeshComponent, SkeletalMeshComponent>(
        [&renderer](Entity /*ent*/,
                    const TransformComponent& transform,
                    const MeshComponent& mesh,
                    const SkeletalMeshComponent& skel) {
            if (!mesh.isVisible || !mesh.vbo || !mesh.ibo || mesh.indexCount == 0 || !skel.IsValid()) {
                return;
            }

            renderer.RenderSkinnedMesh(
                *static_cast<GX2RBuffer*>(mesh.vbo),
                *static_cast<GX2RBuffer*>(mesh.ibo),
                mesh.indexCount,
                mesh.vertexStride,
                transform.worldMatrix,
                skel.skinningMatrices,
                skel.jointCount,
                nullptr,
                nullptr,
                0.0f,
                false
            );
        }
    );
#else
    (void)world;
    (void)renderer;
#endif
}

} // namespace WiiU
