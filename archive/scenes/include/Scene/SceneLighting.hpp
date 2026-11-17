#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Cube3DObject.hpp"
#include "Graphics/Light.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>

namespace WiiU {

class SceneLighting : public IScene {
public:
    SceneLighting();

    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::vector<std::string> GetRequiredAssets() const override {
        return {"click.wav"};
    }
    [[nodiscard]] std::string GetSceneName() const override {
        return "Demo 1: Iluminacion 3D";
    }

private:
    Cube3DObject m_cube;
    DirectionalLight m_light;
    float m_lightOrbitAngle{0.0f};
    UIBackButton m_backButton;
    float m_smoothedFps{60.0f};
};

} // namespace WiiU
