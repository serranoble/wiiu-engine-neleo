#pragma once

#include <array>
#include <cstdint>

namespace Neleo::Platform {

/**
 * @brief Estado interno compartido (Zero-Alloc, sin headers de SO) entre el bucle de mensajes Win32
 *        (`WindowWin32`) y el muestreador HID (`InputWin32`).
 */
struct Win32InputBridge {
    static constexpr size_t NUM_VIRTUAL_KEYS = 256u;
    static constexpr size_t MAX_TYPED_CHARS = 16u;

    std::array<bool, NUM_VIRTUAL_KEYS> keysDown{};
    std::array<bool, NUM_VIRTUAL_KEYS> keysPressed{};
    std::array<char, MAX_TYPED_CHARS> typedChars{};
    uint8_t typedCharCount{0u};

    bool mouseLeftDown{false};
    bool mouseLeftPressed{false};
    float pointerX{0.0f}; // Espacio lógico 1280x720
    float pointerY{0.0f}; // Espacio lógico 1280x720

    bool quitRequested{false};
    bool toggleFullscreenRequested{false};
    bool textInputCapture{false}; // Suprime mapeo QWERTY->gamepad mientras un teclado virtual modal captura WM_CHAR

    // Rectángulo del viewport 16:9 activo dentro de la ventana cliente (píxeles reales)
    int viewportX{0};
    int viewportY{0};
    int viewportW{1280};
    int viewportH{720};

    void PushTypedChar(char ch) noexcept {
        if (typedCharCount < MAX_TYPED_CHARS) {
            typedChars[typedCharCount++] = ch;
        }
    }

    void ClearTypedChars() noexcept {
        typedCharCount = 0u;
    }

    void ConsumeFrameEdges() noexcept {
        keysPressed.fill(false);
        typedCharCount = 0u;
        mouseLeftPressed = false;
        toggleFullscreenRequested = false;
    }
};

[[nodiscard]] Win32InputBridge& GetWin32InputBridge() noexcept;

} // namespace Neleo::Platform
