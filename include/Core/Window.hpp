#pragma once

#include "Graphics/Color.hpp"
#include <string>

namespace Neleo {

/**
 * @brief Capa de Abstracción de Ventana y Swapchain Multiplataforma (PAL).
 *        Wii U: Cafe OS ProcUI + GX2 Nativo.
 *        Windows: Win32 HWND + WGL OpenGL 3.3 Core.
 *        Sin tipos ni dependencias de SDL2 o APIs gráficas propietarias en la cabecera pública.
 */
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    // Prevent accidental copying (Rule of 5 for raw platform resources)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    bool Init();
    void Clear(const ColorRGBA& color = ColorRGBA::Black());
    void Present();
    void Shutdown();
    void SetSwapInterval(int interval) noexcept;
    void ToggleFullscreen() noexcept;

    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }

    // Accesores multiplataforma opacos (0 fugas <gx2/*.h> o <windows.h> en include/Core/)
    [[nodiscard]] static void* GetActiveNativeColorBuffer() noexcept;
    [[nodiscard]] static void* GetActiveNativeDepthBuffer() noexcept;
    [[nodiscard]] static void* GetActiveNativeContextState() noexcept;
    [[nodiscard]] static void* GetActiveNativeWindowHandle() noexcept;
    [[nodiscard]] static bool HasForeground() noexcept;
    [[nodiscard]] static bool IsExitingProcUI() noexcept;
    static void NotifyExitingProcUI() noexcept;
    static void AcquireForeground() noexcept;
    static void ReleaseForeground() noexcept;
    static void PumpPlatformEvents() noexcept;
    static void ClearActive(const ColorRGBA& color = ColorRGBA::Black()) noexcept;
    static void PresentActive() noexcept;

private:
    std::string m_title;
    int m_width{1280};
    int m_height{720};
    bool m_ownsWindow{false};
};

} // namespace Neleo
