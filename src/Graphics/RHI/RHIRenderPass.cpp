#include "Graphics/RHI/RHIRenderPass.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/registers.h>
#endif

namespace WiiU {

void RHIRenderPass::Begin(const RHIRenderPassDesc& desc) {
    s_insidePass = true;
    s_activeDesc = desc;
    ++s_passCount;

#if defined(__WIIU__) || defined(__wiiu__)
    if (desc.autoInvalidateCaches) {
        // [OPT-265] Invalidacion granular segura solo sobre punteros de imagen validos (0 direcciones ilegales)
        if (HasAny(desc.initialAccess, RHIAccessFlags::ColorAttachmentWrite) &&
            desc.colorBuffer != nullptr && desc.colorBuffer->surface.image != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER,
                          desc.colorBuffer->surface.image,
                          desc.colorBuffer->surface.imageSize);
        }
        if (HasAny(desc.initialAccess, RHIAccessFlags::DepthStencilWrite) &&
            desc.depthBuffer != nullptr && desc.depthBuffer->surface.image != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER,
                          desc.depthBuffer->surface.image,
                          desc.depthBuffer->surface.imageSize);
        }
        if (HasAny(desc.initialAccess, RHIAccessFlags::ShaderResourceRead) &&
            desc.colorBuffer != nullptr && desc.colorBuffer->surface.image != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,
                          desc.colorBuffer->surface.image,
                          desc.colorBuffer->surface.imageSize);
        }
    }

    bool performedClear = false;
    if (desc.colorLoadAction == RHILoadAction::Clear && desc.colorBuffer != nullptr) {
        GX2ClearColor(desc.colorBuffer,
                      desc.clearColor[0], desc.clearColor[1],
                      desc.clearColor[2], desc.clearColor[3]);
        performedClear = true;
    }

    if (desc.depthLoadAction == RHILoadAction::Clear && desc.depthBuffer != nullptr) {
        // [OPT-21 / OPT-39 / OPT-267] Fast-Clear con Stencil solo en formatos con plano Stencil real (D24_S8)
        const bool hasStencil =
            (desc.depthBuffer->surface.format == GX2_SURFACE_FORMAT_FLOAT_D24_S8) ||
            (desc.depthBuffer->surface.format == GX2_SURFACE_FORMAT_UNORM_R24_X8);
        const GX2ClearFlags clearFlags = (desc.enableHiZFastClear && desc.depthBuffer->hiZPtr != nullptr && hasStencil)
                                         ? GX2_CLEAR_FLAGS_BOTH
                                         : GX2_CLEAR_FLAGS_DEPTH;
        GX2ClearDepthStencilEx(desc.depthBuffer, desc.clearDepth, desc.clearStencil, clearFlags);
        performedClear = true;
    }

    // Enlazar contexto, targets y viewport siempre que se especifiquen (tanto Clear como Load/DontCare)
    if (desc.contextState != nullptr) {
        GX2SetContextState(desc.contextState);
    }
    if (desc.colorBuffer != nullptr) {
        GX2SetColorBuffer(desc.colorBuffer, GX2_RENDER_TARGET_0);
        const uint32_t w = desc.colorBuffer->surface.width;
        const uint32_t h = desc.colorBuffer->surface.height;
        if (w > 0 && h > 0) {
            GX2SetViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f);
            GX2SetScissor(0, 0, w, h);
        }
    }
    if (desc.depthBuffer != nullptr) {
        GX2SetDepthBuffer(desc.depthBuffer);
        if (desc.colorBuffer == nullptr) {
            const uint32_t w = desc.depthBuffer->surface.width;
            const uint32_t h = desc.depthBuffer->surface.height;
            if (w > 0 && h > 0) {
                GX2SetViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f);
                GX2SetScissor(0, 0, w, h);
            }
        }
    }
    if (performedClear) {
        GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
    }
    if (performedClear || desc.contextState != nullptr || desc.colorBuffer != nullptr || desc.depthBuffer != nullptr) {
        RHIPipelineCache::Instance().Invalidate();
    }
#endif
}

void RHIRenderPass::End() {
    if (!s_insidePass) {
        return;
    }

#if defined(__WIIU__) || defined(__wiiu__)
    if (s_activeDesc.autoFlushOnEnd) {
        GX2Flush();
    }

    if (s_activeDesc.autoInvalidateCaches &&
        HasAny(s_activeDesc.finalAccess, RHIAccessFlags::ShaderResourceRead)) {
        if (s_activeDesc.colorBuffer != nullptr && s_activeDesc.colorBuffer->surface.image != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,
                          s_activeDesc.colorBuffer->surface.image,
                          s_activeDesc.colorBuffer->surface.imageSize);
        }
        if (s_activeDesc.depthBuffer != nullptr && s_activeDesc.depthBuffer->surface.image != nullptr) {
            GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,
                          s_activeDesc.depthBuffer->surface.image,
                          s_activeDesc.depthBuffer->surface.imageSize);
        }
    }
#endif

    s_insidePass = false;
}

} // namespace WiiU
