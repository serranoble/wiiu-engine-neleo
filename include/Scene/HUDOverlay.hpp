#pragma once

#include <SDL2/SDL.h>
#include "Input/InputManager.hpp"

namespace WiiU {

class HUDOverlay {
public:
    void Render(SDL_Renderer* renderer, const InputManager& input, float fps = 60.0f);
};

} // namespace WiiU
