#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Starfield3D.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <functional>

namespace WiiU {

class SceneMenuSDL : public IScene {
public:
    SceneMenuSDL();

    void OnEnter() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Menu Demos SDL";
    }
    [[nodiscard]] bool IsMenu() const override {
        return true;
    }

private:
    struct SDLMenuItem {
        UIButton button;
        std::string description;
        const char* actionName;
        std::function<void()> onSelect;
    };

    void InitMenuItems();

    Starfield3D m_starfield;
    UIBackButton m_backButton;
    std::vector<SDLMenuItem> m_menuItems;
};

} // namespace WiiU
