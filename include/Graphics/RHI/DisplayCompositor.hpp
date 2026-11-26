#pragma once

#include "Core/ECS/Components/CameraComponent.hpp"
#include <cstdint>

namespace Neleo {

enum class CompositorMode : uint8_t {
    TVOnly = 0,
    DRCOnly,
    PictureInPicture,
    SplitDualView
};

/**
 * @brief [OPT-257] Compositor Asimétrico Multi-Pantalla para Nintendo Wii U.
 * 
 * Gobierna las regiones de viewport y tijera (scissor) para la TV (1280x720) y el
 * GamePad DRC (854x480). Permite composición en pantalla completa, Picture-in-Picture
 * o vistas duales asimétricas en tiempo real.
 */
class DisplayCompositor {
public:
    static DisplayCompositor& Instance() noexcept;

    void SetMode(CompositorMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] CompositorMode GetMode() const noexcept { return m_mode; }
    void CycleMode() noexcept;

    // Configura la región de hardware GX2 para la cámara indicada
    void ApplyViewport(const CameraComponent& camera);

    // Configura un viewport y scissor explícito en GX2
    void SetExplicitViewport(float x, float y, float w, float h, float minZ = 0.0f, float maxZ = 1.0f);

    [[nodiscard]] const char* GetModeName() const noexcept;

private:
    DisplayCompositor() noexcept = default;
    ~DisplayCompositor() = default;

    DisplayCompositor(const DisplayCompositor&) = delete;
    DisplayCompositor& operator=(const DisplayCompositor&) = delete;
    DisplayCompositor(DisplayCompositor&&) = delete;
    DisplayCompositor& operator=(DisplayCompositor&&) = delete;

    CompositorMode m_mode{CompositorMode::TVOnly};
};

} // namespace Neleo
