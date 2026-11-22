#include "Core/ECS/Systems/MeshRenderSystem.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Core/ECS/ECSWorld.hpp"
#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"
#include "Core/Config/CVarRegistry.hpp"
#include "Graphics/RenderQueue.hpp"
#include "Graphics/SkyboxRenderPass.hpp"

namespace WiiU {

void MeshRenderSystem::Render(ECSWorld& world, ForwardRendererGX2& renderer, bool enableEarlyZPrePass) {
#if defined(__WIIU__) || defined(__wiiu__)
    static RenderQueue s_queue{};
    s_queue.Clear();

    const Point3D camPos = renderer.GetCameraPosition();
    const Matrix4x4& viewM = renderer.GetViewMatrix();
    // Eje -Z en espacio de vista (tercera fila de la matriz de vista column-major: m[2], m[6], m[10])
    const Point3D camForward{-viewM.m[2], -viewM.m[6], -viewM.m[10]};

    // 1. Recolectar mallas estaticas en RenderQueue Stateless (0 heap allocs, 0 sqrt)
    world.ForEach<TransformComponent, MeshComponent, MaterialComponent>(
        [&world, &camPos, &camForward](Entity ent,
                                       const TransformComponent& transform,
                                       const MeshComponent& mesh,
                                       const MaterialComponent& mat) {
            if (!mesh.isVisible || !mat.isVisible || !mesh.vbo || !mesh.ibo || mesh.indexCount == 0) {
                return;
            }
            if (world.GetComponent<SkeletalMeshComponent>(ent) != nullptr) {
                return;
            }

            const Point3D worldPos{transform.worldMatrix.m[12], transform.worldMatrix.m[13], transform.worldMatrix.m[14]};
            const Point3D d = worldPos - camPos;
            const float viewDepth = Point3D::Dot(d, camForward);
            const RenderLayer layer = (mat.blendMode == BlendMode::Opaque)
                                      ? RenderLayer::Opaque
                                      : ((mat.blendMode == BlendMode::AlphaCutout) ? RenderLayer::AlphaCutout : RenderLayer::Transparent);

            RenderCommand cmd{};
            cmd.layer = layer;
            cmd.sortKey = RenderQueue::BuildSortKey(layer, mat.blendMode, 1u,
                                                    static_cast<uint16_t>(mat.textureId & 0xFFFFu),
                                                    viewDepth);
            cmd.vbo = mesh.vbo;
            cmd.ibo = mesh.ibo;
            cmd.indexCount = mesh.indexCount;
            cmd.vertexStride = mesh.vertexStride;
            cmd.use32BitIndices = (mesh.indexType == 1);
            cmd.modelMatrix = transform.worldMatrix;
            cmd.diffuseTexture = mat.diffuseTexture;
            cmd.envCubeMap = mat.envCubeMap;
            cmd.reflectionIntensity = mat.metallic;
            cmd.normalMapEnabled = true;
            cmd.bumpScale = (mat.customParam != 0.0f) ? mat.customParam : 2.0f;
            cmd.ambientIntensity = 0.38f;
            cmd.blendMode = mat.blendMode;
            cmd.depthTest = mat.depthTest;
            cmd.depthWrite = mat.depthWrite;
            cmd.isSkinned = false;

            (void)s_queue.Submit(cmd);
        }
    );

    // 2. Recolectar mallas esqueleticas animadas (LBS)
    world.ForEach<TransformComponent, MeshComponent, SkeletalMeshComponent>(
        [&world, &camPos, &camForward](Entity ent,
                                       const TransformComponent& transform,
                                       const MeshComponent& mesh,
                                       const SkeletalMeshComponent& skel) {
            if (!mesh.isVisible || !mesh.vbo || !mesh.ibo || mesh.indexCount == 0 || !skel.IsValid()) {
                return;
            }

            const Point3D worldPos{transform.worldMatrix.m[12], transform.worldMatrix.m[13], transform.worldMatrix.m[14]};
            const Point3D d = worldPos - camPos;
            const float viewDepth = Point3D::Dot(d, camForward);

            RenderCommand cmd{};
            cmd.layer = RenderLayer::Opaque;
            cmd.sortKey = RenderQueue::BuildSortKey(RenderLayer::Opaque, BlendMode::Opaque, 2u, 0u, viewDepth);
            cmd.vbo = mesh.vbo;
            cmd.ibo = mesh.ibo;
            cmd.indexCount = mesh.indexCount;
            cmd.vertexStride = mesh.vertexStride;
            cmd.use32BitIndices = (mesh.indexType == 1);
            cmd.modelMatrix = transform.worldMatrix;
            if (const auto* mat = world.GetComponent<MaterialComponent>(ent)) {
                if (!mat->isVisible) {
                    return;
                }
                cmd.diffuseTexture = mat->diffuseTexture;
                cmd.envCubeMap = mat->envCubeMap;
                cmd.reflectionIntensity = mat->metallic;
            }
            cmd.boneMatrices = skel.skinningMatrices;
            cmd.boneCount = skel.jointCount;
            cmd.isSkinned = true;
            cmd.clayMode = false;

            (void)s_queue.Submit(cmd);
        }
    );

    if (CVarRegistry::Instance().GetBool("r.render_queue_sort", true)) {
        s_queue.Sort();
    }

    // 3. Pase opcional Hardware Early-Z Depth Pre-Pass (solo geometria Opaque)
    const bool runEarlyZ = enableEarlyZPrePass && CVarRegistry::Instance().GetBool("r.earlyz", true);
    if (runEarlyZ) {
        renderer.BeginDepthPrePass();
        s_queue.FlushDepthPrePass(renderer);
        renderer.EndDepthPrePass();
    }

    // 4. Despacho ordenado de capas Opacas y AlphaCutout (RHI_COMPARE_EQUAL + depthWrite=false si Early-Z activo)
    s_queue.FlushRange(renderer, RenderLayer::Opaque, RenderLayer::AlphaCutout, runEarlyZ);

    // 5. [OPT-351 / OPT-355] Pase modular de Skybox ENTRE opacos y transparentes (Early-Z sin pisar transparencias)
    (void)SkyboxRenderPass::Render(world, renderer);

    // 6. Despacho ordenado Back-to-Front de capas Transparent y Overlay con BlendMode real
    s_queue.FlushRange(renderer, RenderLayer::Transparent, RenderLayer::Overlay, false);
#else
    (void)world;
    (void)renderer;
    (void)enableEarlyZPrePass;
#endif
}

} // namespace WiiU
