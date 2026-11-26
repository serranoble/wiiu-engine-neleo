#pragma once

#include "Core/Window.hpp"
#include <memory>

namespace Neleo {

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
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    bool Init(std::unique_ptr<IScene> initialScene = nullptr);
    void Run();
    void Shutdown();

    [[nodiscard]] bool IsRunning() const noexcept { return m_running; }
    void Stop() noexcept { m_running = false; }

private:
    void Update(float dt);
    void Render();

    Window m_window;
    bool m_initialized{false};
    bool m_running{false};
    int m_lastSwapInterval{-1};
};

} // namespace Neleo
