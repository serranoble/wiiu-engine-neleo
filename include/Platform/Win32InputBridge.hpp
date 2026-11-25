#pragma once

#include <array>
#include <cstdint>

namespace WiiU::Platform {

/**
 * @brief Estado interno compartido (Zero-Alloc, sin headers de SO) entre el bucle de mensajes Win32
 *        (`WindowWin32`) y el muestreador HID (`InputWin32`).
 */
struct Win32InputBridge {
    static constexpr size_t NUM_VIRTUAL_KEYS = 256u;

    std::array<bool, NUM_VIRTUAL_KEYS> keysDown{};
    std::array<bool, NUM_VIRTUAL_KEYS> keysPressed{};

    bool mouseLeftDown{false};
    bool mouseLeftPressed{false};
    float pointerX{0.0f}; // Espacio lógico 1280x720
    float pointerY{0.0f}; // Espacio lógico 1280x720

    bool quitRequested{false};
    bool toggleFullscreenRequested{false};

    // Rectángulo del viewport 16:9 activo dentro de la ventana cliente (píxeles reales)
    int viewportX{0};
    int viewportY{0};
    int viewportW{1280};
    int viewportH{720};

    void ConsumeFrameEdges() noexcept {
        keysPressed.fill(false);
        mouseLeftPressed = false;
        toggleFullscreenRequested = false;
    }
};

[[nodiscard]] Win32InputBridge& GetWin32InputBridge() noexcept;

} // namespace WiiU::Platform
