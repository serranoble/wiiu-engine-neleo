#pragma once

#include "Graphics/Color.hpp"
#include <string>

#if !defined(__WIIU__) && !defined(__wiiu__)
struct SDL_Window;
struct SDL_Renderer;
#endif

namespace WiiU {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    // Prevent accidental copying (Rule of 5 for raw platform resources)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    bool Init();
    void Clear(const ColorRGBA& color = ColorRGBA::Black());
    void Present();
    void Shutdown();
    void SetSwapInterval(int interval) noexcept;

    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }

    // Accesores multiplataforma opacos (0 fugas <gx2/*.h> o <SDL2/SDL.h> en include/Core/)
    [[nodiscard]] static void* GetActiveNativeColorBuffer() noexcept;
    [[nodiscard]] static void* GetActiveNativeDepthBuffer() noexcept;
    [[nodiscard]] static void* GetActiveNativeContextState() noexcept;
    [[nodiscard]] static void* GetActiveRendererHandle() noexcept;
    [[nodiscard]] static bool HasForeground() noexcept;
    [[nodiscard]] static bool IsExitingProcUI() noexcept;
    static void NotifyExitingProcUI() noexcept;
    static void AcquireForeground() noexcept;
    static void ReleaseForeground() noexcept;

private:
    std::string m_title;
    int m_width{1280};
    int m_height{720};
#if !defined(__WIIU__) && !defined(__wiiu__)
    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
#endif
};

} // namespace WiiU
