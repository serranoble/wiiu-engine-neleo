#include "Core/Window.hpp"
#include <utility>

namespace WiiU {

Window::Window(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height) {}

Window::~Window() {
    Shutdown();
}

Window::Window(Window&& other) noexcept
    : m_title(std::move(other.m_title)),
      m_width(other.m_width),
      m_height(other.m_height),
      m_window(other.m_window),
      m_renderer(other.m_renderer) {
    other.m_window = nullptr;
    other.m_renderer = nullptr;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        Shutdown();
        m_title = std::move(other.m_title);
        m_width = other.m_width;
        m_height = other.m_height;
        m_window = other.m_window;
        m_renderer = other.m_renderer;
        other.m_window = nullptr;
        other.m_renderer = nullptr;
    }
    return *this;
}

bool Window::Init() {
    m_window = SDL_CreateWindow(m_title.c_str(), 
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                                m_width, m_height, 0);
    if (!m_window) return false;

    // Habilitar aceleración por hardware con sincronización vertical (VSync) nativa de hardware
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!m_renderer) return false;

    // Activar VSync de hardware (60Hz en TV / GamePad Wii U) por defecto
    SDL_GL_SetSwapInterval(1);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    return true;
}

void Window::Clear(const ColorRGBA& color) {
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
}

void Window::Present() {
    if (m_renderer) {
        SDL_RenderPresent(m_renderer);
    }
}

void Window::Shutdown() {
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

} // namespace WiiU
