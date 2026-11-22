#include "Graphics/SkyboxRenderPass.hpp"
#include "Core/ECS/Components/SkyboxComponent.hpp"

namespace WiiU {

bool SkyboxRenderPass::Render(ECSWorld& world, ForwardRendererGX2& renderer) {
    bool rendered = false;
    world.ForEach<SkyboxComponent>(
        [&renderer, &rendered](Entity /*ent*/, SkyboxComponent& sky) {
            if (rendered || !sky.enabled || !sky.mesh || !sky.skyTexture) {
                return;
            }
#if defined(__WIIU__) || defined(__wiiu__)
            renderer.RenderSkydome(*sky.mesh, static_cast<GX2Texture*>(sky.skyTexture));
            rendered = true;
#else
            (void)renderer;
#endif
        }
    );
    return rendered;
}

} // namespace WiiU
