#pragma once

#include <SDL2/SDL.h>
#include "Graphics/Color.hpp"
#include <string>

namespace WiiU {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    // Prevent accidental copying (Rule of 5 for raw SDL resource management)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    bool Init();
    void Clear(const ColorRGBA& color = ColorRGBA::Black());
    void Present();
    void Shutdown();

    SDL_Renderer* GetRenderer() const { return m_renderer; }
    SDL_Window* GetSDLWindow() const { return m_window; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    std::string m_title;
    int m_width{1280};
    int m_height{720};
    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
};

} // namespace WiiU
