#pragma once

#include <SDL2/SDL.h>

namespace WiiU {

class RingEffect {
public:
    void Trigger();
    void Update(float dt);
    void Render(SDL_Renderer* renderer, int centerX, int centerY);

    bool IsActive() const { return m_active; }

private:
    bool m_active{false};
    float m_timer{0.0f};
    float m_duration{1.0f};
    float m_maxRadius{130.0f};
};

} // namespace WiiU
