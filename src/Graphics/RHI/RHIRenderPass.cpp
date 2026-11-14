#include "Graphics/RHI/RHIRenderPass.hpp"

namespace WiiU {

void RHIRenderPass::Begin(const RHIRenderPassDesc& desc) {
    s_insidePass = true;
    s_activeDesc = desc;
    ++s_passCount;

#if defined(__WIIU__) || defined(__wiiu__)
    if (desc.autoInvalidateCaches) {
        // [OPT-265] Inserción automática de barreras de invalidación de caché
        GX2InvalidateMode invMode = GX2_INVALIDATE_MODE_NONE;
        if (HasAny(desc.initialAccess, RHIAccessFlags::ShaderResourceRead)) {
            invMode |= GX2_INVALIDATE_MODE_TEXTURE;
        }
        if (HasAny(desc.initialAccess, RHIAccessFlags::ColorAttachmentWrite)) {
            invMode |= GX2_INVALIDATE_MODE_COLOR_BUFFER;
        }
        if (HasAny(desc.initialAccess, RHIAccessFlags::DepthStencilWrite)) {
            invMode |= GX2_INVALIDATE_MODE_DEPTH_BUFFER;
        }
        if (invMode != GX2_INVALIDATE_MODE_NONE) {
            GX2Invalidate(invMode, reinterpret_cast<void*>(0xFFFFFFFF), 0xFFFFFFFF);
        }

        // Invalidación granular de superficies específicas si están asignadas
        if (HasAny(desc.initialAccess, RHIAccessFlags::ColorAttachmentWrite) && desc.colorBuffer != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, desc.colorBuffer->surface.image, desc.colorBuffer->surface.imageSize);
        }
        if (HasAny(desc.initialAccess, RHIAccessFlags::DepthStencilWrite) && desc.depthBuffer != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER, desc.depthBuffer->surface.image, desc.depthBuffer->surface.imageSize);
        }
    }

    // [OPT-265] Carga determinista / Limpieza de framebuffers (Load Actions)
    if (desc.colorLoadAction == RHILoadAction::Clear && desc.colorBuffer != nullptr) {
        GX2ClearColor(desc.colorBuffer,
                      desc.clearColor[0], desc.clearColor[1],
                      desc.clearColor[2], desc.clearColor[3]);
    }

    if (desc.depthLoadAction == RHILoadAction::Clear && desc.depthBuffer != nullptr) {
        GX2ClearDepthStencilEx(desc.depthBuffer, desc.clearDepth, desc.clearStencil, GX2_CLEAR_FLAGS_DEPTH);
    }
#endif
}

void RHIRenderPass::End() {
    if (!s_insidePass) {
        return;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    // [OPT-265] Sincronización y drenado de GPU Latte al finalizar el pase
    if (s_activeDesc.autoFlushOnEnd) {
        GX2Flush();
    }

    if (s_activeDesc.autoInvalidateCaches) {
        // Si el render pass finaliza escribiendo texturas para muestreo en pases subsecuentes
        if (HasAny(s_activeDesc.finalAccess, RHIAccessFlags::ShaderResourceRead)) {
            GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, reinterpret_cast<void*>(0xFFFFFFFF), 0xFFFFFFFF);
        }
    }
#endif

    s_insidePass = false;
}

} // namespace WiiU
