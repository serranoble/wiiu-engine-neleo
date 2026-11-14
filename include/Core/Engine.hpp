#pragma once

#include "Core/Window.hpp"
#include <SDL2/SDL.h>
#include <memory>

namespace WiiU {

class IScene;

class Engine {
public:
    static constexpr const char* DEFAULT_WINDOW_TITLE = "WiiU Demos";
    static constexpr int DEFAULT_WINDOW_WIDTH = 1280;
    static constexpr int DEFAULT_WINDOW_HEIGHT = 720;

    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool Init(std::unique_ptr<IScene> initialScene = nullptr);
    void Run();
    void Shutdown();

    bool IsRunning() const { return m_running; }
    void Stop() { m_running = false; }

private:
    void Update(float dt);
    void Render();

    Window m_window;
    bool m_running{false};
    bool m_wasPureGX2{false};
};

} // namespace WiiU
