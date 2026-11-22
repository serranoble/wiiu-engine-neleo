#include "Graphics/RHI/DisplayCompositor.hpp"
#include <algorithm>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/state.h>
#include <gx2/registers.h>
#endif

namespace WiiU {

DisplayCompositor& DisplayCompositor::Instance() noexcept {
    static DisplayCompositor s_instance;
    return s_instance;
}

void DisplayCompositor::CycleMode() noexcept {
    switch (m_mode) {
        case CompositorMode::TVOnly:
            m_mode = CompositorMode::DRCOnly;
            break;
        case CompositorMode::DRCOnly:
            m_mode = CompositorMode::PictureInPicture;
            break;
        case CompositorMode::PictureInPicture:
            m_mode = CompositorMode::SplitDualView;
            break;
        case CompositorMode::SplitDualView:
        default:
            m_mode = CompositorMode::TVOnly;
            break;
    }
}

const char* DisplayCompositor::GetModeName() const noexcept {
    switch (m_mode) {
        case CompositorMode::TVOnly:           return "TV 720p (Principal)";
        case CompositorMode::DRCOnly:          return "DRC GamePad 480p";
        case CompositorMode::PictureInPicture: return "PiP (GamePad Táctico)";
        case CompositorMode::SplitDualView:    return "Dual-View Split";
        default:                               return "Desconocido";
    }
}

void DisplayCompositor::SetExplicitViewport(float x, float y, float w, float h, float minZ, float maxZ) {
#if defined(__WIIU__) || defined(__wiiu__)
    const float sx = std::max(0.0f, x);
    const float sy = std::max(0.0f, y);
    const float sw = std::max(1.0f, w);
    const float sh = std::max(1.0f, h);
    const float z0 = std::clamp(minZ, 0.0f, 1.0f);
    const float z1 = std::clamp(maxZ, 0.0f, 1.0f);
    GX2SetViewport(sx, sy, sw, sh, z0, z1);
    GX2SetScissor(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy),
                  static_cast<uint32_t>(sw), static_cast<uint32_t>(sh));
#else
    (void)x; (void)y; (void)w; (void)h; (void)minZ; (void)maxZ;
#endif
}

void DisplayCompositor::ApplyViewport(const CameraComponent& camera) {
    float x = camera.viewport.x;
    float y = camera.viewport.y;
    float w = camera.viewport.width;
    float h = camera.viewport.height;

    // Ajuste dinámico según el modo de composición
    if (camera.displayTarget == DisplayTarget::DRC) {
        if (m_mode == CompositorMode::PictureInPicture) {
            // Esquina inferior derecha: 384x216 (16:9), con margen de seguridad sobre la barra de controles (Y=636)
            x = 1280.0f - 384.0f - 20.0f; // 876.0f
            y = 636.0f - 216.0f - 12.0f;  // 408.0f
            w = 384.0f;
            h = 216.0f;
        } else if (m_mode == CompositorMode::SplitDualView) {
            // Mitad derecha de la pantalla (640x720)
            x = 640.0f;
            y = 0.0f;
            w = 640.0f;
            h = 720.0f;
        } else if (m_mode == CompositorMode::DRCOnly) {
            // DRC Pantalla completa (1280x720)
            x = 0.0f;
            y = 0.0f;
            w = 1280.0f;
            h = 720.0f;
        }
    } else { // TV Camera
        if (m_mode == CompositorMode::SplitDualView) {
            // Mitad izquierda de la pantalla (640x720)
            x = 0.0f;
            y = 0.0f;
            w = 640.0f;
            h = 720.0f;
        } else {
            // Pantalla completa TV (1280x720)
            x = 0.0f;
            y = 0.0f;
            w = 1280.0f;
            h = 720.0f;
        }
    }

    SetExplicitViewport(x, y, w, h, camera.viewport.minDepth, camera.viewport.maxDepth);
}

} // namespace WiiU
