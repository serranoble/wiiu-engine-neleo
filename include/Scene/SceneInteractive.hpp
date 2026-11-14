#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Cube3DObject.hpp"
#include "Scene/DraggableSquare.hpp"
#include "Scene/RingEffect.hpp"
#include "Scene/HUDOverlay.hpp"
#include "Graphics/Light.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>

namespace WiiU {

class SceneInteractive : public IScene {
public:
    SceneInteractive();

    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"click.wav"};
    }
    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 3: Sandbox Interactivo";
    }

private:
    Cube3DObject m_cube;
    DraggableSquare m_square;
    RingEffect m_ring;
    HUDOverlay m_hud;
    DirectionalLight m_light;
    UIBackButton m_backButton;
    float m_smoothedFps{60.0f};
};

} // namespace WiiU
