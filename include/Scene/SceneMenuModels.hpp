#pragma once

#include "Scene/IScene.hpp"
#include "Scene/Starfield3D.hpp"
#include "Graphics/UIComponents.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <functional>

namespace WiiU {

class SceneMenuModels : public IScene {
public:
    SceneMenuModels();

    void OnEnter() override;
    void Update(float dt) override;
    void Render(SDL_Renderer* renderer) override;

    [[nodiscard]] std::string GetSceneName() const override {
        return "Menu Demos Modelos";
    }
    [[nodiscard]] bool IsMenu() const override {
        return true;
    }

private:
    struct ModelMenuItem {
        UIButton button;
        std::string description;
        const char* actionName;
        std::function<void()> onSelect;
    };

    void InitMenuItems();

    Starfield3D m_starfield;
    UIBackButton m_backButton;
    std::vector<ModelMenuItem> m_menuItems;
};

} // namespace WiiU
