#include "Graphics/RHI/RHIPipelineCache.hpp"

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/state.h>
#include <gx2/registers.h>
#endif

namespace WiiU {

RHIPipelineCache& RHIPipelineCache::Instance() noexcept {
    static RHIPipelineCache s_instance;
    return s_instance;
}

void RHIPipelineCache::Invalidate() noexcept {
    m_currentHash = 0xFFFFFFFFFFFFFFFFULL;
    m_lastDirtyBits = RHIStateBits::All;
}

void RHIPipelineCache::BindState(const RHIPipelineState& state) {
    // [OPT-262] Silicon State Guard: Validación de invariantes de hardware
    [[maybe_unused]] RHIValidationResult valRes = state.Validate();
#if defined(DEBUG) || !defined(NDEBUG)
    // En builds con aserciones habilitadas o autodiagnósticos, verificar validez
    if (valRes != RHIValidationResult::Valid) {
        // Estado con riesgo o valores ilegales detectado por la capa de validación
    }
#endif

    uint64_t newHash = state.ComputeHash();
    if (newHash == m_currentHash) {
        ++m_redundantBypasses;
        m_lastDirtyBits = RHIStateBits::None;
        return; // Supresión 100% de llamadas de registro redundantes a GPU Latte
    }

    ++m_stateChanges;

    // [OPT-264] Cálculo branchless de sub-estados modificados mediante máscara de bits
    RHIStateBits dirty = (m_currentHash == 0xFFFFFFFFFFFFFFFFULL)
                       ? RHIStateBits::All
                       : m_currentState.ComputeDiff(state);
    m_lastDirtyBits = dirty;

#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Configurar Depth / Stencil si algún bit de profundidad cambió
    if (HasAny(dirty, RHIStateBits::AllDepth)) {
        GX2SetDepthOnlyControl(
            state.depthTest ? TRUE : FALSE,
            state.depthWrite ? TRUE : FALSE,
            static_cast<GX2CompareFunction>(state.depthFunc)
        );
    }

    // 2. Configurar Blend Mode y Hardware Alpha-Test si cambió
    if (HasAny(dirty, RHIStateBits::AllBlend)) {
        if (state.blendMode == BlendMode::AlphaBlend) {
            GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
            GX2SetBlendControl(
                GX2_RENDER_TARGET_0,
                GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD,
                TRUE,
                GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD
            );
        } else if (state.blendMode == BlendMode::Additive) {
            GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
            GX2SetBlendControl(
                GX2_RENDER_TARGET_0,
                GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD,
                TRUE,
                GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD
            );
        } else if (state.blendMode == BlendMode::AlphaCutout) {
            // [OPT-301] Hardware Alpha-Test en silicio Latte (SX register sx_alpha_test_control)
            // Descarta fragmentos con alpha <= 0.50f antes del Depth / Color Write
            GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.5f);
            GX2SetBlendControl(
                GX2_RENDER_TARGET_0,
                GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD,
                FALSE,
                GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD
            );
        } else {
            // Opaque
            GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
            GX2SetBlendControl(
                GX2_RENDER_TARGET_0,
                GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD,
                FALSE,
                GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD
            );
        }
    }

    // 3. Configurar Culling si algún bit de rasterizado cambió (cullFront = FALSE, cullBack = cullEnable)
    if (HasAny(dirty, RHIStateBits::AllRaster)) {
        GX2SetCullOnlyControl(
            static_cast<GX2FrontFace>(state.frontFace),
            FALSE,
            state.cullEnable ? TRUE : FALSE
        );
    }
#endif

    m_currentState = state;
    m_currentHash = newHash;
}

} // namespace WiiU
