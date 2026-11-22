#pragma once

#include "Input/InputManager.hpp"

namespace WiiU {

class HUDOverlay {
public:
    void Render(const InputManager& input, float fps = 60.0f);
};

} // namespace WiiU
