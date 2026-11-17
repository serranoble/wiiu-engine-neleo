#pragma once

#include <SDL2/SDL.h>
#include "Input/InputManager.hpp"

namespace WiiU {

class DraggableSquare {
public:
    DraggableSquare(float startX, float startY, float size);

    void Update(float dt, const TouchState& touchState, InputManager& input);
    void Render(SDL_Renderer* renderer);

    bool IsDragging() const { return m_isDragging; }

private:
    float m_x;
    float m_y;
    float m_size;
    bool m_isDragging{false};
    float m_grabOffsetX{0.0f};
    float m_grabOffsetY{0.0f};
};

} // namespace WiiU
